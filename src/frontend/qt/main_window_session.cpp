#include "main_window_internal.h"

void MainWindow::resetPacing() {
    nextFrame_ = clock_.nsecsElapsed();
    clockPacer_.reset();
}

void MainWindow::tick() {
    refreshControllerGate();
    if (!emulator_ || paused_ || stateOperation_ || menuDepth_)
        return;
    const qint64 now = clock_.nsecsElapsed();
    if (rewind_) {
        if (now >= nextFrame_) {
            rewindFrame();
            if (turbo_)
                rewindFrame();
            nextFrame_ = now + nominalFrameNanoseconds(emulator_ ? &emulator_->gb : nullptr);
            clockPacer_.reset();
        }
        return;
    }
    pumpAudio();
    // Starting a native audio stream may dispatch window/session actions.
    if (!emulator_ || paused_ || stateOperation_)
        return;
    if (!turbo_ && now < nextFrame_)
        return;
    // Bound each event-loop turn so dialogs, input, and close remain responsive.
    if (audio_ && !turbo_ && audioBuffer_->bytesAvailable() > audioQueueLimit_)
        return;
    const uint64_t session = sessionId_;
    const uint64_t timeline = timelineId_;
    const uint64_t startCycles = emulator_->gb.elapsed_cycles;
    const unsigned speed = speedSteps_;
    if (!runFrame() || paused_ || !emulator_ || sessionId_ != session || timelineId_ != timeline)
        return;
    if (emulator_->gb.elapsed_cycles < startCycles) {
        resetPacing();
        return;
    }
    // LCD startup and re-enabling can finish a partial image. Charge only
    // consumed base-clock time, including the two-dot CGB machine cycles.
    const auto interval = clockPacer_.advance(emulator_->gb.elapsed_cycles - startCycles, speed,
                                              cupid_gb_base_clock_hz(&emulator_->gb));
    if (!interval || nextFrame_ > std::numeric_limits<qint64>::max() - *interval) {
        setPaused(true);
        reportError("Host pacing duration could not be computed.");
        return;
    }
    nextFrame_ += *interval;
    // A short LCD interval must not shrink the host-stall recovery window.
    const qint64 nominalInterval = nominalFrameNanoseconds(&emulator_->gb, speed);
    if (turbo_ || now - nextFrame_ > nominalInterval * 4) {
        nextFrame_ = now;
        clockPacer_.reset();
    }
}

bool MainWindow::runFrame() {
    if (!emulator_)
        return false;
    auto &gb = emulator_->gb;
    const uint32_t rate = cupid_gb_apu_get_sample_rate(&gb);
    if (recorder_ && recorder_->active() && recorder_->sampleRate() != rate && !stopRecording())
        return false;
    if (audioConverter_)
        audioConverter_->setInputRate(double(rate) * speedSteps_ / 16);
    const uint64_t startCycles = gb.elapsed_cycles;
    // Presentation events also cover LCD-off intervals. Keep a guest-clock bound
    // for a stalled or temporarily frozen display pipeline.
    for (unsigned steps = 0; steps < 80000; ++steps) {
        if (!inputTrace_.empty()) {
            const uint64_t frame = (gb.elapsed_cycles - telemetryStartCycles_) / 70224;
            while (inputTraceIndex_ < inputTrace_.size() &&
                   inputTrace_[inputTraceIndex_].first <= frame) {
                physicalJoypad_ = inputTrace_[inputTraceIndex_++].second;
                setJoypad(physicalJoypad_);
            }
        }
        if (debugger_ && debugger_->bp_count) {
            const auto stop = cupid_gb_debugger_check(debugger_.get(), &gb);
            if (stop == CUPID_GB_DEBUG_STOP_BREAKPOINT || stop == CUPID_GB_DEBUG_STOP_ERROR) {
                stopDebugger(stop == CUPID_GB_DEBUG_STOP_ERROR
                                 ? "Breakpoint condition evaluation failed."
                                 : "Breakpoint reached.");
                return true;
            }
        }
        // A before-instruction debugger stop must retain a pending frame latch.
        // Clear it only when this call actually proceeds into guest execution.
        if (!steps)
            gb.frame_ready = false;
        if (!cupid_emulator_step(emulator_.get())) {
            setPaused(true);
            reportError("CPU execution stopped. The cartridge remains open for inspection.");
            return false;
        }
        if (gb.sgb.player_count != controllerPlayerCount_)
            refreshControllerPlayerCount();
        if (gb.frame_ready || gb.elapsed_cycles - startCycles >= 70224)
            break;
    }
    int16_t samples[CUPID_GB_APU_BUF_FRAMES * 2];
    const auto frames = cupid_gb_apu_drain(&gb, samples, CUPID_GB_APU_BUF_FRAMES);
    generatedAudioFrames_ += frames;
    if (recorder_ && recorder_->active() && !recorder_->append(samples, frames)) {
        const QString error = recorder_->errorString();
        if (stopRecording())
            reportError("Audio recording stopped: " + error);
    }
    if (audio_ && !turbo_ && !paused_) {
        const QByteArray converted = audioConverter_->convert(samples, frames);
        audioBuffer_->append(converted.constData(), converted.size());
        pumpAudio();
    }
    present(gb.frame_ready);
    updatePrinter();
    finishControllerRumble();
    advanceControls();
    watchBattery();
    rememberFrame();
    return true;
}

QByteArray MainWindow::captureState(bool reportFailure, bool rewindSnapshot) {
    if (!emulator_)
        return {};
    const size_t size = cupid_gb_state_size(&emulator_->gb);
    if (!size || size > 16 * 1024 * 1024) {
        if (reportFailure)
            reportError("Cannot determine a valid emulator state size.");
        return {};
    }
    QByteArray bytes(qsizetype(size), Qt::Uninitialized);
    size_t written = 0;
    const auto result =
        rewindSnapshot
            ? cupid_gb_save_state_with_context(&emulator_->gb,
                                               reinterpret_cast<uint8_t *>(bytes.data()), size,
                                               &written, rewindContext_.get())
            : cupid_gb_save_state(&emulator_->gb, reinterpret_cast<uint8_t *>(bytes.data()), size,
                                  &written);
    if (result != CUPID_GB_STATE_OK || written > size) {
        if (reportFailure)
            reportError("Cannot capture emulator state: " +
                        QString::fromUtf8(cupid_gb_state_result_name(result)));
        return {};
    }
    bytes.resize(qsizetype(written));
    return bytes;
}

bool MainWindow::restoreState(const QByteArray &bytes, bool reportFailure, bool rewindSnapshot) {
    if (!emulator_)
        return false;
    if (cupid_native_v1_is_state(reinterpret_cast<const uint8_t *>(bytes.constData()),
                                 size_t(bytes.size()))) {
        if (reportFailure)
            reportError("This older state cannot resume with the current pixel pipeline. "
                        "Use File > Recover Battery from Older State to export its battery data.");
        return false;
    }
    const auto result =
        rewindSnapshot
            ? cupid_gb_load_state_with_context(&emulator_->gb,
                                               reinterpret_cast<const uint8_t *>(bytes.constData()),
                                               size_t(bytes.size()), rewindContext_.get())
            : cupid_gb_load_state(&emulator_->gb,
                                  reinterpret_cast<const uint8_t *>(bytes.constData()),
                                  size_t(bytes.size()));
    if (result != CUPID_GB_STATE_OK) {
        if (reportFailure)
            reportError("Cannot load emulator state: " +
                        QString::fromUtf8(cupid_gb_state_result_name(result)));
        return false;
    }
    ++timelineId_;
    resetPrinter();
    resetControllerRumble();
    controllerPlayerCount_ =
        cupid_gb_sgb_active(&emulator_->gb) ? emulator_->gb.sgb.player_count : 1;
    return true;
}

QString MainWindow::slotPath(int slot, bool legacy) const {
    const QFileInfo info(savePath_);
    return info.dir().filePath(info.completeBaseName() +
                               QString(legacy ? ".sn%1" : ".s%1").arg(slot));
}

bool MainWindow::saveSlot(int slot) {
    if (!emulator_ || slot < 0 || slot > 9)
        return false;
    QScopedValueRollback<bool> operation(stateOperation_, true);
    const QString path = slotPath(slot);
    const QString canonical = QFileInfo(path).canonicalFilePath();
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    if (!canonical.isEmpty() &&
        (canonical.compare(QFileInfo(romPath_).canonicalFilePath(), pathCase) == 0 ||
         canonical.compare(QFileInfo(savePath_).canonicalFilePath(), pathCase) == 0)) {
        reportError(
            "The state path points to the cartridge or battery save. Choose separate files.");
        return false;
    }
    const QByteArray bytes = captureState();
    if (bytes.isEmpty())
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        reportError("Cannot save state: " + file.errorString());
        return false;
    }
    statusBar()->showMessage(QString("State saved to slot %1").arg(slot ? slot : 10), 3000);
    resetPacing();
    return true;
}

bool MainWindow::loadSlot(int slot) {
    if (!emulator_ || slot < 0 || slot > 9)
        return false;
    QString path = slotPath(slot);
    // An existing but unreadable or invalid primary file must not select a different state.
    if (!QFileInfo::exists(path))
        path = slotPath(slot, true);
    if (!loadStateFile(path))
        return false;
    statusBar()->showMessage(QString("State loaded from slot %1").arg(slot ? slot : 10), 3000);
    return true;
}

bool MainWindow::loadStateFile(const QString &path) {
    if (!emulator_) {
        reportError("Open a cartridge before loading a state file.");
        return false;
    }
    QScopedValueRollback<bool> operation(stateOperation_, true);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 16 * 1024 * 1024) {
        reportError("Cannot read state file: " + path + "\n" + file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() != file.size()) {
        reportError("State file read failed: " + file.errorString());
        return false;
    }
    if (!restoreState(bytes, true))
        return false;
    resetDebuggerTimeline();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    if (recorder_ && recorder_->active() &&
        recorder_->sampleRate() != cupid_gb_apu_get_sample_rate(&emulator_->gb))
        stopRecording();
    // Pending samples belong to the saved timeline, before the next forward instruction.
    int16_t discarded[CUPID_GB_APU_BUF_FRAMES * 2];
    cupid_gb_apu_drain(&emulator_->gb, discarded, CUPID_GB_APU_BUF_FRAMES);
    // Host input remains current even when the saved guest frame held another button.
    applyHeldButtons();
    if (paused_ && debuggerInputPaused_)
        debuggerPlayerButtons_ = livePlayerButtons();
    batteryDirty_ = true;
    saveTimer_.start();
    rewind_ = false;
    turbo_ = false;
    clearAudio();
    resetPacing();
    resetRewind();
    rememberFrame();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    present();
    statusBar()->showMessage("State loaded", 3000);
    return true;
}

bool MainWindow::recoverBattery(const QString &statePath, const QString &outputPath) {
    if (!emulator_)
        return false;
    auto pathIdentity = [](const QString &path) {
        const QFileInfo file(path);
        return file.exists() ? file.canonicalFilePath() : file.absoluteFilePath();
    };
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    for (const QString &protectedPath : {romPath_, savePath_, statePath}) {
        if (pathIdentity(outputPath).compare(pathIdentity(protectedPath), pathCase) == 0) {
            reportError("Export recovery to a separate file. Cartridge, battery, and state inputs "
                        "are protected.");
            return false;
        }
    }
    QFile file(statePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        reportError("Cannot read the older state: " + file.errorString());
        return false;
    }
    const QByteArray state = file.readAll();
    if (state.size() != file.size())
        return false;
    auto recovered = std::make_unique<CupidNativeV1Battery>();
    auto result = cupid_native_v1_recover_battery(
        reinterpret_cast<const uint8_t *>(state.constData()), size_t(state.size()),
        reinterpret_cast<const uint8_t *>(romData_.constData()), size_t(romData_.size()),
        emulator_->gb.cartridge_ram_size, recovered.get());
    if (result != CUPID_NATIVE_V1_OK) {
        reportError("Cannot recover battery: " +
                    QString::fromLatin1(cupid_native_v1_result_name(result)));
        return false;
    }
    const auto size = cupid_native_v1_battery_size(recovered.get());
    if (!size || size != cupid_gb_battery_size(&emulator_->gb)) {
        reportError("The recovered battery layout does not match this cartridge.");
        return false;
    }
    QByteArray bytes(qsizetype(size), Qt::Uninitialized);
    size_t written = 0;
    result = cupid_native_v1_export_battery(
        recovered.get(), reinterpret_cast<uint8_t *>(bytes.data()), size, &written);
    QSaveFile output(outputPath);
    if (result != CUPID_NATIVE_V1_OK || written != size || !output.open(QIODevice::WriteOnly) ||
        output.write(bytes) != bytes.size() || !output.commit()) {
        reportError("Cannot export recovered battery: " + output.errorString());
        return false;
    }
    statusBar()->showMessage("Recovered battery exported; the running cartridge is unchanged",
                             5000);
    return true;
}

void MainWindow::resetRewind() {
    rewindStates_.clear();
    rewindContext_.reset();
    rewindBytes_ = 0;
    rewindFailed_ = false;
    rewind_ = false;
    rewindSnapshotsCaptured_ = 0;
    rewindCaptureNanoseconds_ = 0;
    rewindCaptureMaximumNanoseconds_ = 0;
}

void MainWindow::rememberFrame() {
    const int seconds = std::clamp(settings_.value("emulation/rewindSeconds", 120).toInt(), 0, 600);
    if (!emulator_ || !seconds || rewindFailed_)
        return;
    const uint32_t clockHz = cupid_gb_base_clock_hz(&emulator_->gb);
    if (!rewindContext_) {
        auto context = std::make_unique<CupidGbStateContext>();
        const auto result = cupid_gb_state_context_init(context.get(), &emulator_->gb);
        if (result != CUPID_GB_STATE_OK) {
            rewindFailed_ = true;
            statusBar()->showMessage(
                "Rewind disabled: cartridge identity could not be initialized");
            return;
        }
        // Cartridge bytes are immutable in this frontend. Reset this context whenever
        // opening/resetting a cartridge, loading a disk state, or adding ROM-editing support.
        rewindContext_ = std::move(context);
    }
    QElapsedTimer captureTimer;
    captureTimer.start();
    const QByteArray state = captureState(false, true);
    QByteArray compressed = state.isEmpty() ? QByteArray{} : qCompress(state, 1);
    if (compressed.isEmpty()) {
        rewindFailed_ = true;
        statusBar()->showMessage("Rewind disabled: cannot capture emulator state");
        return;
    }
    compressed.squeeze();
    const qint64 captureTime = captureTimer.nsecsElapsed();
    ++rewindSnapshotsCaptured_;
    rewindCaptureNanoseconds_ += captureTime;
    rewindCaptureMaximumNanoseconds_ = std::max(rewindCaptureMaximumNanoseconds_, captureTime);
    rewindBytes_ += compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
    rewindStates_.push_back({std::move(compressed), emulator_->gb.elapsed_cycles});
    const int budgetMiB = std::clamp(settings_.value("emulation/rewindMiB", 256).toInt(), 16, 256);
    const qsizetype budget = qsizetype(budgetMiB) * 1024 * 1024;
    if (rewindStates_.back().compressed.capacity() + qsizetype(sizeof(RewindSnapshot)) > budget) {
        rewindStates_.clear();
        rewindBytes_ = 0;
        rewindFailed_ = true;
        statusBar()->showMessage("Rewind disabled: one state exceeds the selected memory limit");
        return;
    }
    bool budgetLimited = false;
    while (rewindStates_.size() > 1 &&
           (rewindBytes_ > budget || rewindStates_.back().cycles - rewindStates_.front().cycles >
                                         uint64_t(seconds) * clockHz)) {
        budgetLimited |= rewindBytes_ > budget;
        rewindBytes_ -=
            rewindStates_.front().compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
        rewindStates_.pop_front();
    }
    if (budgetLimited && clock_.elapsed() - lastRewindBudgetNotice_ > 1000) {
        lastRewindBudgetNotice_ = clock_.elapsed();
        const double available =
            double(rewindStates_.back().cycles - rewindStates_.front().cycles) / clockHz;
        statusBar()->showMessage(QString("Rewind memory limit: %1 seconds retained within %2 MiB")
                                     .arg(available, 0, 'f', 1)
                                     .arg(budgetMiB),
                                 2000);
    }
}

bool MainWindow::rewindFrame() {
    if (!emulator_ || rewindFailed_ || rewindStates_.size() < 2) {
        statusBar()->showMessage(rewindFailed_ ? "Rewind unavailable: snapshot capture failed"
                                               : "No earlier rewind history");
        return false;
    }
    // The last entry describes the currently displayed frame. Restore its predecessor.
    const auto &previous = rewindStates_[rewindStates_.size() - 2];
    const QByteArray state = qUncompress(previous.compressed);
    if (state.isEmpty() || !restoreState(state, false, true)) {
        rewindFailed_ = true;
        statusBar()->showMessage("Rewind disabled: stored state could not be restored");
        return false;
    }
    resetDebuggerTimeline();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    applyHeldButtons();
    batteryDirty_ = true;
    saveTimer_.start();
    rewindBytes_ -= rewindStates_.back().compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
    rewindStates_.pop_back();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    present();
    return true;
}

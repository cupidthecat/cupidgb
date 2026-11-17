/**
 * @file    main_window_session.cpp
 * @brief   Qt pacing, state files, and bounded rewind session management.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "main_window_internal.h"
#include "rewind_compression.h"

#include <QScopeGuard>

void MainWindow::resetPacing() {
    ++pacingGeneration_;
    nextFrame_ = clock_.nsecsElapsed();
    clockPacer_.reset();
    refreshTimerInterval();
}

void MainWindow::refreshTimerInterval() {
    // A transient state operation may unwind without resetting pacing. Keep its active
    // interval so native audio callbacks cannot strand a running session at idle cadence.
    const bool idle = !emulator_ || paused_ || menuDepth_ || menuInputSuspended_ ||
                      (rewind_ && (rewindFailed_ || rewindExhausted_));
    const int interval = idle ? 16 : 1;
    if (timer_.interval() != interval)
        timer_.setInterval(interval);
}

void MainWindow::setTurboCap(unsigned quarters) {
    if (quarters < 6 || quarters > 16)
        quarters = 0;
    settings_.setValue("emulation/turboCap", quarters * 25);
    for (size_t i = 0; i < turboCapActions_.size(); ++i)
        turboCapActions_[i]->setChecked(turboCapChoices[i] == quarters);
    if (turboCapQuarters_ != quarters) {
        turboCapQuarters_ = quarters;
        resetPacing();
    }
}

void MainWindow::tick() {
    const auto refreshInterval = qScopeGuard([this] { refreshTimerInterval(); });
    refreshControllerGate();
    if (!emulator_ || paused_ || stateOperation_ || menuDepth_ || menuInputSuspended_)
        return;
    const uint64_t entrySession = sessionId_;
    const uint64_t entryTimeline = timelineId_;
    const uint64_t entryPacing = pacingGeneration_;
    if (audioModeResetPending_)
        clearAudio();
    pumpAudio();
    // Starting a native audio stream may dispatch window/session actions.
    if (!emulator_ || paused_ || stateOperation_ || menuDepth_ || menuInputSuspended_ ||
        sessionId_ != entrySession || timelineId_ != entryTimeline ||
        pacingGeneration_ != entryPacing)
        return;
    if (rewind_ && (rewindFailed_ || rewindExhausted_))
        return;
    const qint64 now = clock_.nsecsElapsed();
    const bool turbo = turbo_;
    const bool rewind = rewind_;
    const bool slow = slowHeld_;
    const unsigned cap = turbo ? turboCapQuarters_ : GuestClockPacer::normalTurboQuarters;
    const bool uncapped = turbo && !cap;
    if (!uncapped && now < nextFrame_)
        return;
    // Bound each event-loop turn so dialogs, input, and close remain responsive.
    if (audio_ && !turbo_ && audioBuffer_->bytesAvailable() > audioQueueLimit_)
        return;
    const uint64_t session = sessionId_;
    const uint64_t pacing = pacingGeneration_;
    const unsigned speed = speedSteps_;
    PlaybackResult result;
    const bool ran = rewind ? rewindFrame(&result) : runFrame(Playback::Forward, &result);
    if (!ran || paused_ || !emulator_ || stateOperation_ || menuDepth_ || menuInputSuspended_ ||
        sessionId_ != session || timelineId_ != result.timeline)
        return;
    // Audio callbacks can dispatch a speed preference or held-key change. Keep
    // the new deadline instead of charging an old-rate quantum to that timeline.
    if (pacingGeneration_ != pacing || turbo_ != turbo || rewind_ != rewind || slowHeld_ != slow ||
        (turbo && turboCapQuarters_ != cap)) {
        if (pacingGeneration_ == pacing)
            resetPacing();
        return;
    }
    if (uncapped) {
        nextFrame_ = now;
        clockPacer_.reset();
        return;
    }
    // LCD startup and re-enabling can finish a partial image. Charge only
    // consumed presentation time, including adapter holds and two-dot CGB cycles.
    // Rewind restores an earlier clock before executing. Its positive replay
    // interval, not the net movement through history, determines the deadline.
    const auto interval = clockPacer_.advance(result.presentationCycles, speed,
                                              cupid_gb_base_clock_hz(&emulator_->gb), cap);
    if (!interval || nextFrame_ > std::numeric_limits<qint64>::max() - *interval) {
        setPaused(true);
        reportError("Host pacing duration could not be computed.");
        return;
    }
    nextFrame_ += *interval;
    // A short LCD interval must not shrink the host-stall recovery window.
    const qint64 nominalInterval = nominalFrameNanoseconds(&emulator_->gb, speed, cap);
    if (now - nextFrame_ > nominalInterval * 4) {
        nextFrame_ = now;
        clockPacer_.reset();
    }
}

bool MainWindow::runFrame(Playback playback, PlaybackResult *result) {
    if (!emulator_)
        return false;
    const bool replay = playback != Playback::Forward;
    const bool lastStep = playback == Playback::RewindEnd;
    if (!replay)
        rewindExhausted_ = false;
    auto &gb = emulator_->gb;
    const uint32_t rate = cupid_gb_apu_get_sample_rate(&gb);
    if (!replay && recorder_ && recorder_->active() && recorder_->sampleRate() != rate &&
        !stopRecording())
        return false;
    if (audioConverter_)
        audioConverter_->setInputRate(double(rate) * speedSteps_ / 16);
    const uint64_t startPresentation = cupid_gb_presentation_cycles(&gb);
    const uint64_t startGuest = gb.elapsed_cycles;
    if (result)
        *result = {0, 0, timelineId_};
    CoreAudioCollector collected;
    bool collectionFailed = !collectAudio(collected);
    bool executionFailed = false;
    bool attemptedHardware = false;
    bool attemptedGuest = false;
    QString debuggerStop;
    // Presentation time also advances while adapter startup holds the CPU.
    // The hardware-call guard stays finite even if the display cannot publish.
    for (unsigned steps = 0; !collectionFailed && steps < 80000; ++steps) {
        if (CoreAudioCollector::needsDrain(gb) && !collectAudio(collected)) {
            collectionFailed = true;
            break;
        }
        if (!replay && !inputTrace_.empty() && gb.elapsed_cycles >= telemetryStartCycles_) {
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
                debuggerStop = stop == CUPID_GB_DEBUG_STOP_ERROR
                                   ? "Breakpoint condition evaluation failed."
                                   : "Breakpoint reached.";
                break;
            }
        }
        // A before-instruction stop retains a pending frame latch. A host-only
        // call still performs real display work, but owns no instruction history.
        if (!steps)
            gb.frame_ready = false;
        const bool held = cupid_gb_sgb_startup_held(&gb);
        const bool watching = !held && !replay && debugger_ && debugger_->wp_count;
        if (watching)
            cupid_gb_debugger_watch_begin(debugger_.get(), &gb);
        attemptedHardware = true;
        attemptedGuest |= !held;
        const bool executed = cupid_emulator_step(emulator_.get());
        const auto watchStop =
            watching ? cupid_gb_debugger_watch_end(debugger_.get(), &gb) : CUPID_GB_DEBUG_STOP_NONE;
        if (!executed) {
            executionFailed = true;
            break;
        }
        if (gb.sgb.player_count != controllerPlayerCount_)
            refreshControllerPlayerCount();
        if (watchStop == CUPID_GB_DEBUG_STOP_WATCHPOINT || watchStop == CUPID_GB_DEBUG_STOP_ERROR) {
            debuggerStop = debuggerWatchMessage(watchStop == CUPID_GB_DEBUG_STOP_ERROR);
            break;
        }
        if (lastStep || gb.frame_ready ||
            cupid_gb_presentation_cycles(&gb) - startPresentation >= 70224)
            break;
    }
    // Stop paths retain the final step's PCM too. Collection itself performs no
    // presentation, hardware work, pacing charge, or backend callback.
    if (!collectionFailed)
        collectionFailed = !collectAudio(collected);
    const uint64_t presentationCycles = cupid_gb_presentation_cycles(&gb) - startPresentation;
    const uint64_t guestCycles = gb.elapsed_cycles - startGuest;
    if (result) {
        result->presentationCycles = presentationCycles;
        result->guestCycles = guestCycles;
    }
    if (replay)
        replayedGuestCycles_ += guestCycles;
    const bool stopping = collectionFailed || executionFailed || !debuggerStop.isEmpty();
    const bool completedFrame = attemptedHardware && gb.frame_ready;
    const uint64_t session = sessionId_;
    const uint64_t timeline = timelineId_;
    const bool wasPaused = paused_;
    const unsigned menuDepth = menuDepth_;
    const bool menuInputSuspended = menuInputSuspended_;
    const auto unchanged = [this, session, timeline, wasPaused, menuDepth, menuInputSuspended] {
        return emulator_ && sessionId_ == session && timelineId_ == timeline &&
               paused_ == wasPaused && menuDepth_ == menuDepth &&
               menuInputSuspended_ == menuInputSuspended;
    };
    QByteArray converted;
    if (!stopping && audio_ && audioConverter_ && !paused_)
        converted = convertPlaybackAudio(collected.samples(), collected.frames(), audio_->format());
    // Publish before a host audio call can dispatch pause/load/close. A solely
    // held call bypasses automatic rewind, while explicit snapshots remain valid.
    if (!stopping && completedFrame) {
        advanceScreenMessages();
        present(Presentation::Frame);
        advanceControls();
        if (attemptedGuest)
            rememberFrame();
        if (replay)
            ++replayedFrames_;
    } else if (!stopping && !replay) {
        present(Presentation::Redraw);
    }
    if (attemptedHardware) {
        updatePrinter();
        finishControllerRumble();
        watchBattery();
    }
    if (!replay)
        recordAudio(collected);
    if (!unchanged())
        return true;
    if (collectionFailed || executionFailed) {
        setPaused(true);
        reportError(collectionFailed
                        ? "Audio output could not be preserved. Emulation is paused."
                        : "CPU execution stopped. The cartridge remains open for inspection.");
        return false;
    }
    if (!debuggerStop.isEmpty()) {
        stopDebugger(debuggerStop);
        return true;
    }
    if (audio_ && !paused_ && !converted.isEmpty()) {
        audioBuffer_->append(converted.constData(), converted.size());
        pumpAudio();
    }
    // No guest or presentation state is touched after a backend callback.
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
            ? cupid_gb_load_state_live_with_context(
                  &emulator_->gb, reinterpret_cast<const uint8_t *>(bytes.constData()),
                  size_t(bytes.size()), rewindContext_.get())
            : cupid_gb_load_state_live(&emulator_->gb,
                                       reinterpret_cast<const uint8_t *>(bytes.constData()),
                                       size_t(bytes.size()));
    if (result != CUPID_GB_STATE_OK) {
        if (reportFailure)
            reportError("Cannot load emulator state: " +
                        QString::fromUtf8(cupid_gb_state_result_name(result)));
        return false;
    }
    ++timelineId_;
    cancelCameraCapture();
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
    notifyOnScreen(QString("State saved to slot %1").arg(slot ? slot : 10));
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
    notifyOnScreen(QString("State loaded from slot %1").arg(slot ? slot : 10));
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
    // The receiving output history survives a load. Discard its queued frames
    // before resuming guest execution at the restored instruction boundary.
    if (!discardCoreAudio())
        return false;
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
    present(Presentation::Reset);
    statusBar()->showMessage("State loaded", 3000);
    notifyOnScreen("State loaded");
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
    rewindAgeCycles_ = emulator_ ? cupid_gb_presentation_cycles(&emulator_->gb) : 0;
    rewindPresentationCycles_ = rewindAgeCycles_;
    rewindBytes_ = 0;
    rewindFailed_ = false;
    rewindExhausted_ = false;
    rewind_ = false;
    rewindSnapshotsCaptured_ = 0;
    rewindCaptureNanoseconds_ = 0;
    rewindCaptureMaximumNanoseconds_ = 0;
}

void MainWindow::trackRewindTime(uint64_t presentationCycles) {
    if (presentationCycles >= rewindPresentationCycles_)
        rewindAgeCycles_ += presentationCycles - rewindPresentationCycles_;
    rewindPresentationCycles_ = presentationCycles;
}

void MainWindow::rebaseRewindAfterReset(uint64_t previousPresentationCycles) {
    trackRewindTime(previousPresentationCycles);
    rewindPresentationCycles_ = emulator_ ? cupid_gb_presentation_cycles(&emulator_->gb) : 0;
    /* Byte-identical replacement ROMs can retain snapshots, but trusted contexts
     * still belong to a particular allocation and must be rebound after the swap. */
    rewindContext_.reset();
    rewindExhausted_ = false;
    rewind_ = false;
}

void MainWindow::rememberFrame() {
    const int seconds = std::clamp(settings_.value("emulation/rewindSeconds", 120).toInt(), 0, 600);
    if (!emulator_ || !seconds || rewindFailed_)
        return;
    trackRewindTime(cupid_gb_presentation_cycles(&emulator_->gb));
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
        // Cartridge bytes are immutable in this frontend. Replacement allocations
        // need a new binding even when their byte-identical snapshots remain usable.
        rewindContext_ = std::move(context);
    }
    HostElapsedTimer captureTimer;
    captureTimer.start();
    const QByteArray state = captureState(false, true);
    QByteArray compressed = CupidRewind::compressSnapshot(state);
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
    rewindStates_.push_back({std::move(compressed), rewindAgeCycles_});
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

bool MainWindow::rewindFrame(PlaybackResult *result) {
    if (!emulator_ || rewindFailed_ || rewindExhausted_) {
        statusBar()->showMessage(rewindFailed_ ? "Rewind unavailable: snapshot capture failed"
                                               : "No earlier rewind history");
        return false;
    }
    const size_t consumed = turbo_ ? 3 : 2;
    const bool exhausted = rewindStates_.size() < consumed;
    if (!rewindStates_.empty()) {
        // History includes the current interval. Consume the target too, then
        // replay it once and append the resulting presentation. The first normal
        // replay repeats the current image; later intervals move back one each.
        const size_t target = rewindStates_.size() > consumed ? rewindStates_.size() - consumed : 0;
        const QByteArray state = qUncompress(rewindStates_[target].compressed);
        if (state.isEmpty() || !restoreState(state, false, true)) {
            rewindFailed_ = true;
            statusBar()->showMessage("Rewind disabled: stored state could not be restored");
            return false;
        }
        rewindAgeCycles_ = rewindStates_[target].cycles;
        rewindPresentationCycles_ = cupid_gb_presentation_cycles(&emulator_->gb);
        // Live restores retain the receiving output history. Discard pending
        // frames so only newly generated replay samples enter the host stream.
        if (!discardCoreAudio())
            return false;
        resetDebuggerTimeline();
        if (inspectorsWindow_)
            inspectorsWindow_->sessionChanged();
        applyHeldButtons();
        batteryDirty_ = true;
        saveTimer_.start();
        while (rewindStates_.size() > target) {
            rewindBytes_ -=
                rewindStates_.back().compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
            rewindStates_.pop_back();
        }
    }
    rewindExhausted_ = exhausted;
    if (exhausted)
        statusBar()->showMessage("No earlier rewind history");
    // The terminal boundary executes one hardware call, then waits for release.
    // It does not invent a frame, control ramp, or display-message event.
    return runFrame(exhausted ? Playback::RewindEnd : Playback::Rewind, result);
}

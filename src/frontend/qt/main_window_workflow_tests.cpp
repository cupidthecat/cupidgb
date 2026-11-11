#include "inspector_renderer.h"
#include "main_window_internal.h"

bool MainWindow::smokeTest() {
    QTemporaryDir directory;
    if (!directory.isValid())
        return smokeFailure(__FILE__, __LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom[0x147] = 0x09; // ROM + RAM + battery, used to check persistence.
    rom[0x149] = 0x02;
    if (automaticModel(rom) != CUPID_GB_MODEL_DMG_ABC)
        return smokeFailure(__FILE__, __LINE__);
    auto modelProbe = rom;
    modelProbe[0x14b] = 1;
    if (automaticModel(modelProbe) != CUPID_GB_MODEL_CGB)
        return smokeFailure(__FILE__, __LINE__);
    modelProbe[0x146] = 3;
    if (automaticModel(modelProbe) != CUPID_GB_MODEL_SGB)
        return smokeFailure(__FILE__, __LINE__);
    modelProbe[0x143] = char(0x80);
    if (automaticModel(modelProbe) != CUPID_GB_MODEL_CGB)
        return smokeFailure(__FILE__, __LINE__);
    modelProbe = rom;
    modelProbe[0x14b] = 0x33;
    modelProbe[0x144] = '0';
    modelProbe[0x145] = '1';
    if (automaticModel(modelProbe) != CUPID_GB_MODEL_CGB)
        return smokeFailure(__FILE__, __LINE__);
    const QString path = directory.filePath(QString::fromUtf8("cartridge-\xc3\xa9.gb"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    file.close();
    auto dropFile = [this](const QString &path) {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter({1, 1}, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(this, &enter);
        QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(this, &drop);
        return drop.isAccepted();
    };
    if (!openRom(path) || !runFrame() || image_.size() != QSize(160, 144))
        return smokeFailure(__FILE__, __LINE__);
    if (audioCheck_ && (!audio_ || !audioPriming_ || audioBuffer_->bytesAvailable() <= 0 ||
                        audioPrimeEvents_ != 0))
        return smokeFailure(__FILE__, __LINE__);
    const auto beforeInvalidBoot = captureState();
    settings_.setValue("emulation/bootDirectory", directory.filePath("missing-boot-directory"));
    if (openRom(path) || captureState() != beforeInvalidBoot)
        return smokeFailure(__FILE__, __LINE__);
    settings_.remove("emulation/bootDirectory");
    failed_ = false;
    pauseAction_->trigger();
    const uint16_t pc = emulator_->gb.cpu.pc;
    tick();
    if (emulator_->gb.cpu.pc != pc || !pauseAction_->isChecked() ||
        (audioCheck_ && (!audioPriming_ || audioBuffer_->bytesAvailable() != 0)))
        return smokeFailure(__FILE__, __LINE__);
    setPaused(false);
    const auto beforeTimer = emulator_->gb.elapsed_cycles;
    QEventLoop loop;
    QTimer::singleShot(75, &loop, &QEventLoop::quit);
    loop.exec();
    if (emulator_->gb.elapsed_cycles <= beforeTimer)
        return smokeFailure(__FILE__, __LINE__);
    emulator_->gb.io_registers[0] = 0x10;
    QKeyEvent down(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(display_, &down);
    if ((emulator_->gb.joypad & 0x10) != 0 || !(emulator_->gb.interrupt_flags & 0x10))
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent upWithControl(QEvent::KeyRelease, Qt::Key_X, Qt::ControlModifier);
    QApplication::sendEvent(display_, &upWithControl);
    if ((emulator_->gb.joypad & 0x10) == 0)
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent turboDown(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent turboUp(QEvent::KeyRelease, Qt::Key_Space, Qt::AltModifier);
    QApplication::sendEvent(display_, &turboDown);
    QApplication::sendEvent(display_, &turboUp);
    if (turbo_)
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent slowDown(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    QKeyEvent slowUp(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
    QApplication::sendEvent(display_, &slowDown);
    for (unsigned frame = 0; frame < 8; ++frame)
        if (!runFrame())
            return smokeFailure(__FILE__, __LINE__);
    if (speedSteps_ != 8 || !slowHeld_)
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(display_, &slowUp);
    for (unsigned frame = 0; frame < 8; ++frame)
        if (!runFrame())
            return smokeFailure(__FILE__, __LINE__);
    if (speedSteps_ != 16 || slowHeld_)
        return smokeFailure(__FILE__, __LINE__);
    specialKeys_[3] = Qt::Key_C;
    QKeyEvent rapidDown(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier);
    QKeyEvent rapidUp(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(display_, &rapidDown);
    for (unsigned frame = 0; frame < 8; ++frame) {
        if (bool(emulator_->gb.joypad & 0x10) != (frame % 4 >= 2) || !runFrame())
            return smokeFailure(__FILE__, __LINE__);
    }
    QApplication::sendEvent(display_, &rapidUp);
    if (!(emulator_->gb.joypad & 0x10))
        return smokeFailure(__FILE__, __LINE__);
    specialKeys_[3] = 0;
    QApplication::sendEvent(display_, &down);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(this, &deactivate);
    if (emulator_->gb.joypad != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    auto *running = emulator_.get();
    if (openRom(directory.filePath("missing.gb")) || emulator_.get() != running)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false; // The missing-file error above is intentional.
    QFile saveRom(directory.filePath("not-a-cartridge.sav"));
    if (!saveRom.open(QIODevice::WriteOnly) || saveRom.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    saveRom.close();
    if (openRom(saveRom.fileName()) || emulator_.get() != running ||
        !saveRom.open(QIODevice::ReadOnly) || saveRom.readAll() != rom)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false; // Rejecting a save file must preserve the running cartridge and input bytes.
    cupid_gb_write_u8(&emulator_->gb, 0xa017, 0x39);
    watchBattery();
    QEventLoop saveLoop;
    QTimer::singleShot(320, &saveLoop, &QEventLoop::quit);
    saveLoop.exec();
    QFile automaticSave(savePath_);
    if (batteryDirty_ || !automaticSave.open(QIODevice::ReadOnly) ||
        automaticSave.readAll()[23] != char(0x39))
        return smokeFailure(__FILE__, __LINE__);
    automaticSave.close();
    cupid_gb_write_u8(&emulator_->gb, 0xa017, 0x40);
    watchBattery();
    QTimer::singleShot(120, this, [this] {
        cupid_gb_write_u8(&emulator_->gb, 0xa017, 0x41);
        watchBattery();
    });
    QEventLoop debounceLoop;
    QTimer::singleShot(320, &debounceLoop, &QEventLoop::quit);
    debounceLoop.exec();
    if (!batteryDirty_ || !automaticSave.open(QIODevice::ReadOnly) ||
        automaticSave.readAll()[23] != char(0x39))
        return smokeFailure(__FILE__, __LINE__);
    automaticSave.close();
    QEventLoop stableLoop;
    QTimer::singleShot(100, &stableLoop, &QEventLoop::quit);
    stableLoop.exec();
    if (batteryDirty_ || !automaticSave.open(QIODevice::ReadOnly) ||
        automaticSave.readAll()[23] != char(0x41))
        return smokeFailure(__FILE__, __LINE__);
    automaticSave.close();
    const QString shot = directory.filePath("screen.png");
    emulator_->gb.cartridge_ram[23] = 0x5a;
    ++emulator_->gb.battery_write_generation;
    if (!image_.save(shot) || QImage(shot).size() != image_.size() || !restart() ||
        emulator_->gb.cartridge_ram[23] != 0x5a)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    const QByteArray checkpoint = captureState();
    // Version 2 retains the frozen payload prefix but has no new timing latches.
    // Loading its file must preserve that file and let the core initialize missing history.
    QByteArray version2 = checkpoint.first(300518);
    auto putState32 = [](QByteArray &bytes, qsizetype offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes[offset + i] = char(value >> (i * 8));
    };
    putState32(version2, 8, 2);
    putState32(version2, 12, uint32_t(version2.size()));
    const QByteArray version2Digest = QCryptographicHash::hash(
        version2.first(96) + version2.mid(128), QCryptographicHash::Sha256);
    std::memcpy(version2.data() + 96, version2Digest.constData(), 32);
    QFile version2File(directory.filePath("version2.s3"));
    if (checkpoint.size() <= version2.size() || !version2File.open(QIODevice::WriteOnly) ||
        version2File.write(version2) != version2.size())
        return smokeFailure(__FILE__, __LINE__);
    version2File.close();
    if (!loadStateFile(version2File.fileName()) || !paused_ ||
        captureState().mid(128, 300518 - 128) != version2.mid(128) ||
        !version2File.open(QIODevice::ReadOnly) || version2File.readAll() != version2 ||
        !restoreState(checkpoint, false) || captureState() != checkpoint)
        return smokeFailure(__FILE__, __LINE__);
    version2File.close();
    // Frozen legacy wire offsets are used only to exercise explicit battery recovery.
    QByteArray olderState(300349, char(0));
    std::memcpy(olderState.data(), "CUPIDGB", 8);
    auto putLegacy32 = [&](qsizetype offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            olderState[offset + i] = char(value >> (i * 8));
    };
    putLegacy32(8, 1);
    putLegacy32(12, uint32_t(olderState.size()));
    putLegacy32(20, uint32_t(rom.size()));
    const auto romDigest = QCryptographicHash::hash(rom, QCryptographicHash::Sha256);
    std::memcpy(olderState.data() + 32, romDigest.constData(), 32);
    olderState[16529 + 23] = char(0x72);
    const auto stateDigest = QCryptographicHash::hash(olderState.first(96) + olderState.mid(128),
                                                      QCryptographicHash::Sha256);
    std::memcpy(olderState.data() + 96, stateDigest.constData(), 32);
    QFile olderFile(directory.filePath("older.s1"));
    if (!olderFile.open(QIODevice::WriteOnly) || olderFile.write(olderState) != olderState.size())
        return smokeFailure(__FILE__, __LINE__);
    olderFile.close();
    const QString recoveredPath = directory.filePath(QString::fromUtf8("recovered-\xc3\xa9.sav"));
    if (restoreState(olderState, false) || captureState() != checkpoint ||
        !recoverBattery(olderFile.fileName(), recoveredPath) || captureState() != checkpoint)
        return smokeFailure(__FILE__, __LINE__);
    QFile recovered(recoveredPath);
    if (!recovered.open(QIODevice::ReadOnly) || recovered.size() != 8192 ||
        recovered.readAll()[23] != char(0x72))
        return smokeFailure(__FILE__, __LINE__);
    if (recoverBattery(olderFile.fileName(), savePath_) ||
        recoverBattery(olderFile.fileName(), romPath_) ||
        recoverBattery(olderFile.fileName(), olderFile.fileName()) || captureState() != checkpoint)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false;
    olderState[16529] ^= 1;
    if (!olderFile.open(QIODevice::WriteOnly) || olderFile.write(olderState) != olderState.size())
        return smokeFailure(__FILE__, __LINE__);
    olderFile.close();
    if (recoverBattery(olderFile.fileName(), directory.filePath("bad-export.sav")) ||
        QFileInfo::exists(directory.filePath("bad-export.sav")) || captureState() != checkpoint)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false;
    if (checkpoint.isEmpty() || !saveSlot(1) || !saveSlot(0) ||
        saveSlotActions_[1]->shortcut() != QKeySequence("Ctrl+1") ||
        loadSlotActions_[0]->shortcut() != QKeySequence("Ctrl+Shift+0"))
        return smokeFailure(__FILE__, __LINE__);
    const QString nativeStatePath = slotPath(1);
    if (openRom(nativeStatePath) || captureState() != checkpoint)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false; // State files must never be interpreted as new cartridges.
    if (!runFrame() || !dropFile(nativeStatePath) || !paused_ || captureState() != checkpoint)
        return smokeFailure(__FILE__, __LINE__);
    if (!runFrame() || !runFrame())
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray expectedReplay = captureState();
    if (!loadSlot(1) || !paused_ || captureState() != checkpoint || !runFrame() || !runFrame() ||
        captureState() != expectedReplay)
        return smokeFailure(__FILE__, __LINE__);
    if (!saveSlot(2) || !QFile::rename(slotPath(2), slotPath(2, true)) || !runFrame() ||
        !loadSlot(2) || captureState() != expectedReplay)
        return smokeFailure(__FILE__, __LINE__);
    QFile corrupt(slotPath(2));
    if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("invalid") != 7)
        return smokeFailure(__FILE__, __LINE__);
    corrupt.close();
    const QByteArray beforeInvalid = captureState();
    if (loadSlot(2) || !paused_ || captureState() != beforeInvalid)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false; // A malformed primary must not load the valid legacy state.
    if (dropFile(slotPath(2)) || !paused_ || captureState() != beforeInvalid)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false; // A damaged dropped state must preserve the running cartridge.
    setPaused(false);
    QApplication::sendEvent(display_, &down);
    if (!saveSlot(3))
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent releasedA(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(display_, &releasedA);
    if (!loadSlot(3) || paused_ || (emulator_->gb.joypad & 0x10) == 0)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray runningBeforeInvalid = captureState();
    if (loadSlot(2) || paused_ || captureState() != runningBeforeInvalid)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false; // Rejected loads must also preserve an unpaused session.
    setPaused(true);
    const QString alternatePath = directory.filePath("different-cartridge.gb");
    QByteArray alternate = rom;
    alternate[0x150] = 1;
    QFile alternateFile(alternatePath);
    if (!alternateFile.open(QIODevice::WriteOnly) ||
        alternateFile.write(alternate) != alternate.size())
        return smokeFailure(__FILE__, __LINE__);
    alternateFile.close();
    QFile originalSlot(slotPath(1));
    if (!originalSlot.open(QIODevice::ReadOnly))
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray originalBytes = originalSlot.readAll();
    originalSlot.close();
    if (!openRom(alternatePath))
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    QFile wrongSlot(slotPath(1));
    if (!wrongSlot.open(QIODevice::WriteOnly) ||
        wrongSlot.write(originalBytes) != originalBytes.size())
        return smokeFailure(__FILE__, __LINE__);
    wrongSlot.close();
    const QByteArray beforeWrongRom = captureState();
    if (loadSlot(1) || !paused_ || captureState() != beforeWrongRom)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false; // A state belonging to another cartridge must preserve this session.
    if (dropFile(nativeStatePath) || !paused_ || captureState() != beforeWrongRom)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false;
    if (!openRom(path))
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    resetRewind();
    rememberFrame();
    for (int i = 0; i < 6; ++i)
        if (!runFrame())
            return smokeFailure(__FILE__, __LINE__);
    const QByteArray forwardState = captureState();
    for (int i = 0; i < 3; ++i)
        if (!rewindFrame())
            return smokeFailure(__FILE__, __LINE__);
    for (int i = 0; i < 3; ++i)
        if (!runFrame())
            return smokeFailure(__FILE__, __LINE__);
    if (captureState() != forwardState) {
        qCritical("Rewind workflow failed to reproduce the forward state");
        return smokeFailure(__FILE__, __LINE__);
    }
    const auto beforeRewind = emulator_->gb.elapsed_cycles;
    setPaused(false);
    QKeyEvent rightDown(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QKeyEvent rightUp(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(display_, &rightDown);
    QKeyEvent rewindDown(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QKeyEvent rewindUp(QEvent::KeyRelease, Qt::Key_Tab, Qt::ControlModifier);
    QApplication::sendEvent(display_, &rewindDown);
    tick();
    QApplication::sendEvent(display_, &rewindUp);
    if (rewind_ || emulator_->gb.elapsed_cycles >= beforeRewind ||
        (emulator_->gb.joypad & 1) != 0) {
        qCritical("Rewind key workflow failed: held=%d cycles=%llu before=%llu joypad=%02x",
                  rewind_, qulonglong(emulator_->gb.elapsed_cycles), qulonglong(beforeRewind),
                  emulator_->gb.joypad);
        return smokeFailure(__FILE__, __LINE__);
    }
    // Shift+Tab arrives as Backtab on desktop platforms and must still operate rewind.
    QKeyEvent shiftedRewindDown(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QKeyEvent shiftedRewindUp(QEvent::KeyRelease, Qt::Key_Backtab, Qt::ShiftModifier);
    QApplication::sendEvent(display_, &shiftedRewindDown);
    if (!rewind_) {
        qCritical("Shift+Tab did not start rewind");
        return smokeFailure(__FILE__, __LINE__);
    }
    QApplication::sendEvent(display_, &shiftedRewindUp);
    if (rewind_) {
        qCritical("Shift+Tab release did not stop rewind");
        return smokeFailure(__FILE__, __LINE__);
    }
    QApplication::sendEvent(display_, &rightUp);
    if ((emulator_->gb.joypad & 1) == 0)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    const QByteArray beforeBudget = captureState();
    settings_.setValue("emulation/rewindMiB", 16);
    resetRewind();
    uint32_t random = 0x619ad325;
    auto randomize = [&random](auto &bytes) {
        for (auto &byte : bytes) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            byte = uint8_t(random);
        }
    };
    for (int i = 0; i < 100; ++i) {
        randomize(emulator_->gb.cartridge_ram);
        randomize(emulator_->gb.work_ram);
        randomize(emulator_->gb.video_ram);
        rememberFrame();
    }
    if (rewindFailed_ || rewindBytes_ > 16 * 1024 * 1024 || rewindStates_.size() >= 100 ||
        rewindStates_.size() < 2) {
        qCritical("Rewind memory limit workflow failed");
        return smokeFailure(__FILE__, __LINE__);
    }
    settings_.setValue("emulation/rewindSeconds", 0);
    resetRewind();
    rememberFrame();
    if (!rewindStates_.empty())
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("emulation/rewindMiB", 256);
    if (!restoreState(beforeBudget, true))
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("emulation/rewindSeconds", 1);
    resetRewind();
    rememberFrame();
    const uint64_t durationStart = emulator_->gb.elapsed_cycles;
    for (int i = 0; i < 90; ++i)
        if (!runFrame())
            return smokeFailure(__FILE__, __LINE__);
    // Guest execution exceeds the duration limit while memory remains far below its limit.
    if (rewindFailed_ ||
        emulator_->gb.elapsed_cycles - durationStart <= cupid_gb_base_clock_hz(&emulator_->gb) ||
        rewindStates_.size() < 2 || rewindStates_.size() >= 91 ||
        rewindStates_.front().cycles <= durationStart ||
        rewindStates_.back().cycles - rewindStates_.front().cycles >
            cupid_gb_base_clock_hz(&emulator_->gb) ||
        rewindBytes_ >= 16 * 1024 * 1024) {
        qCritical("Rewind duration limit workflow failed");
        return smokeFailure(__FILE__, __LINE__);
    }
    settings_.setValue("emulation/rewindSeconds", 120);
    if (!restoreState(beforeBudget, true))
        return smokeFailure(__FILE__, __LINE__);
    resetRewind();
    rememberFrame();
    if (audioCheck_) {
        if (!audio_ || audioErrorEvents_) {
            qCritical("Audio workflow check requires a working audio output device");
            return smokeFailure(__FILE__, __LINE__);
        }
        auto waitForward = [this] {
            setPaused(false);
            const uint64_t before = generatedAudioFrames_;
            const uint64_t primedBefore = audioPrimeEvents_;
            QEventLoop loop;
            QTimer::singleShot(350, Qt::PreciseTimer, &loop, &QEventLoop::quit);
            loop.exec();
            const bool passed = audio_ && audio_->error() == QAudio::NoError &&
                                audio_->processedUSecs() > 0 && generatedAudioFrames_ > before &&
                                !audioPriming_ && audioPrimeEvents_ > primedBefore;
            if (!passed)
                qCritical("Audio resume check failed: sink=%d state=%d error=%d processed=%lld "
                          "generated=%llu priming=%d completions=%llu previous=%llu queued=%lld",
                          bool(audio_), audio_ ? int(audio_->state()) : -1,
                          audio_ ? int(audio_->error()) : -1,
                          audio_ ? qlonglong(audio_->processedUSecs()) : 0,
                          qulonglong(generatedAudioFrames_ - before), audioPriming_,
                          qulonglong(audioPrimeEvents_), qulonglong(primedBefore),
                          audioBuffer_ ? qlonglong(audioBuffer_->bytesAvailable()) : -1);
            return passed;
        };
        if (!waitForward() || !saveSlot(9) || !waitForward())
            return smokeFailure(__FILE__, __LINE__);
        if (!loadSlot(9) || audioBuffer_->bytesAvailable() != 0 || !audioPriming_ || !waitForward())
            return smokeFailure(__FILE__, __LINE__);
        const uint64_t beforeBackwards = emulator_->gb.elapsed_cycles;
        QApplication::sendEvent(display_, &rewindDown);
        QEventLoop backwardsLoop;
        QTimer::singleShot(75, Qt::PreciseTimer, &backwardsLoop, &QEventLoop::quit);
        backwardsLoop.exec();
        QApplication::sendEvent(display_, &rewindUp);
        if (rewind_ || emulator_->gb.elapsed_cycles >= beforeBackwards ||
            audioBuffer_->bytesAvailable() != 0 || !audioPriming_ || !waitForward() ||
            audioErrorEvents_)
            return smokeFailure(__FILE__, __LINE__);
        qInfo("CupidGB real-audio save/load and rewind transition checks passed");
    }
    const auto originalKeys = keys_;
    const auto originalSpecialKeys = specialKeys_;
    const auto originalPauseShortcut = pauseAction_->shortcut();
    auto settingsDialog = [this](const auto &edit) {
        bool checked = false;
        QTimer command;
        command.setInterval(1);
        connect(&command, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "settings")
                return;
            command.stop();
            checked = edit(*dialog);
            if (dialog->isVisible())
                dialog->reject();
        });
        command.start();
        configure();
        return checked;
    };
    setPaused(true);
    const QByteArray displayState = captureState();
    if (displayState.isEmpty())
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t ordinaryPaints = display_->paintCount;
    display_->grab();
    display_->grab();
    if (display_->paintCount < ordinaryPaints + 2 || display_->collectPaintIntervals ||
        !display_->paintIntervals.empty() || display_->lastPaintAt)
        return smokeFailure(__FILE__, __LINE__);
    scalingActions_[2]->trigger();
    if (display_->scaling != DisplayScaling::Fill ||
        settings_.value("video/scaling").toInt() != int(DisplayScaling::Fill))
        return smokeFailure(__FILE__, __LINE__);
    scalingActions_[1]->trigger();
    if (display_->scaling != DisplayScaling::Aspect)
        return smokeFailure(__FILE__, __LINE__);
    scalingActions_[0]->trigger();
    windowSizeActions_[2]->trigger();
    if (settings_.value("video/defaultScale").toInt() != 3 || !windowSizeActions_[2]->isChecked())
        return smokeFailure(__FILE__, __LINE__);
    windowSizeActions_[1]->trigger();
    for (size_t i = 0; i < paletteActions_.size(); ++i) {
        paletteActions_[i]->trigger();
        if (!paletteActions_[i]->isChecked() || settings_.value("video/palette").toInt() != int(i))
            return smokeFailure(__FILE__, __LINE__);
    }
    if (captureState() != displayState)
        return smokeFailure(__FILE__, __LINE__);
    const auto accurateBlend = size_t(FrameBlending::Accurate);
    const auto simpleBlend = size_t(FrameBlending::Simple);
    const auto disabledBlend = size_t(FrameBlending::Disabled);
    if (!blendingActions_[accurateBlend]->isChecked() ||
        !colorCorrectionActions_[size_t(ColorCorrection::Balanced)]->isChecked() ||
        colorTemperatureSlider_->value() != 10)
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("video/blending", false);
    applyDisplaySettings();
    if (!blendingActions_[disabledBlend]->isChecked())
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("video/blending", true);
    applyDisplaySettings();
    if (!blendingActions_[simpleBlend]->isChecked())
        return smokeFailure(__FILE__, __LINE__);
    settings_.remove("video/blending");
    applyDisplaySettings();
    if (!settingsDialog([&](QDialog &dialog) {
            auto *scaling = dialog.findChild<QComboBox *>("displayScaling");
            auto *palette = dialog.findChild<QComboBox *>("monochromePalette");
            auto *color = dialog.findChild<QLineEdit *>("customPalette/0");
            auto *blend = dialog.findChild<QComboBox *>("frameBlending");
            auto *correction = dialog.findChild<QComboBox *>("colorCorrection");
            auto *temperature = dialog.findChild<QSlider *>("colorTemperature");
            if (!scaling || !palette || !color || !blend ||
                blend->currentIndex() != int(FrameBlending::Accurate) || !correction ||
                !temperature ||
                correction->currentData().toInt() != int(ColorCorrection::Balanced) ||
                temperature->value() != 10)
                return smokeFailure(__FILE__, __LINE__);
            scaling->setCurrentIndex(int(DisplayScaling::Fill));
            palette->setCurrentIndex(int(MonochromePalette::Custom));
            color->setText("#123456");
            blend->setCurrentIndex(int(FrameBlending::Disabled));
            correction->setCurrentIndex(correction->findData(int(ColorCorrection::BoostContrast)));
            temperature->setValue(0);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            return true;
        }) ||
        display_->scaling != DisplayScaling::Integer ||
        settings_.contains("video/customPalette/0") || settings_.contains("video/blendingMode") ||
        !blendingActions_[accurateBlend]->isChecked() ||
        settings_.contains("video/colorCorrection") ||
        settings_.contains("video/colorTemperature") || colorTemperatureSlider_->value() != 10)
        return smokeFailure(__FILE__, __LINE__);
    if (!settingsDialog([&](QDialog &dialog) {
            auto *palette = dialog.findChild<QComboBox *>("monochromePalette");
            auto *color = dialog.findChild<QLineEdit *>("customPalette/0");
            auto *blend = dialog.findChild<QComboBox *>("frameBlending");
            auto *correction = dialog.findChild<QComboBox *>("colorCorrection");
            auto *temperature = dialog.findChild<QSlider *>("colorTemperature");
            if (!palette || !color || !blend || !correction || !temperature)
                return smokeFailure(__FILE__, __LINE__);
            palette->setCurrentIndex(int(MonochromePalette::Custom));
            blend->setCurrentIndex(int(FrameBlending::Simple));
            correction->setCurrentIndex(correction->findData(int(ColorCorrection::Curves)));
            temperature->setValue(0);
            color->setText("#123456");
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return !dialog.isVisible();
        }) ||
        settings_.value("video/customPalette/0").toString() != "#123456" ||
        settings_.value("video/blendingMode").toInt() != int(FrameBlending::Simple) ||
        !blendingActions_[simpleBlend]->isChecked() ||
        !colorCorrectionActions_[size_t(ColorCorrection::Curves)]->isChecked() ||
        settings_.value("video/colorCorrection").toInt() != int(ColorCorrection::Curves) ||
        settings_.value("video/colorTemperature").toInt() != 0 ||
        colorTemperatureSlider_->value() != 0 || captureState() != displayState)
        return smokeFailure(__FILE__, __LINE__);
    if (!settingsDialog([&](QDialog &dialog) {
            auto *color = dialog.findChild<QLineEdit *>("customPalette/0");
            if (!color)
                return smokeFailure(__FILE__, __LINE__);
            color->setText("#zzzzzz");
            bool warningShown = false;
            QTimer::singleShot(0, &dialog, [&] {
                if (auto *warning =
                        qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    warningShown = true;
                    warning->accept();
                }
            });
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return warningShown && dialog.isVisible();
        }) ||
        settings_.value("video/customPalette/0").toString() != "#123456")
        return smokeFailure(__FILE__, __LINE__);
    paletteActions_[0]->trigger();
    std::fill(std::begin(emulator_->gb.frame_buffer), std::end(emulator_->gb.frame_buffer), 0);
    present();
    if (image_.pixel(0, 0) != QColor("#c6de8c").rgb())
        return smokeFailure(__FILE__, __LINE__);
    paletteActions_[4]->trigger();
    if (image_.pixel(0, 0) != QColor("#123456").rgb())
        return smokeFailure(__FILE__, __LINE__);
    paletteActions_[1]->trigger();
    std::fill(std::begin(emulator_->gb.frame_buffer), std::end(emulator_->gb.frame_buffer), 3);
    present(true);
    std::fill(std::begin(emulator_->gb.frame_buffer), std::end(emulator_->gb.frame_buffer), 0);
    present(true);
    if (image_.pixel(0, 0) != qRgb(255, 255, 255) ||
        display_->image.pixel(0, 0) != qRgb(186, 186, 186))
        return smokeFailure(__FILE__, __LINE__);
    blendingActions_[accurateBlend]->trigger();
    const int darkerRow = emulator_->gb.lcd_frame_odd ? 0 : 1;
    if (!blendingActions_[accurateBlend]->isChecked() ||
        settings_.value("video/blendingMode").toInt() != int(FrameBlending::Accurate) ||
        display_->image.pixel(0, darkerRow) != qRgb(155, 155, 155) ||
        display_->image.pixel(0, darkerRow ^ 1) != qRgb(212, 212, 212))
        return smokeFailure(__FILE__, __LINE__);
    const QImage blendedFrame = display_->image;
    present();
    if (display_->image != blendedFrame || !saveSlot(7) || !loadSlot(7) ||
        !previousDisplayFrame_.isNull() || display_->image != image_)
        return smokeFailure(__FILE__, __LINE__);
    const QImage frameBeforeAdvance = currentDisplayFrame_;
    if (!runFrame() || previousDisplayFrame_ != frameBeforeAdvance ||
        display_->image != blendDisplayFrames(image_, frameBeforeAdvance, FrameBlending::Accurate,
                                              emulator_->gb.lcd_frame_odd))
        return smokeFailure(__FILE__, __LINE__);
    resetRewind();
    rememberFrame();
    if (!runFrame() || !runFrame() || !rewindFrame() || !previousDisplayFrame_.isNull() ||
        display_->image != image_ || !restoreState(displayState, false))
        return smokeFailure(__FILE__, __LINE__);
    blendingActions_[disabledBlend]->trigger();
    colorCorrectionActions_[size_t(ColorCorrection::Balanced)]->trigger();
    colorTemperatureSlider_->setValue(10);
    paletteActions_[0]->trigger();
    auto changeMappings = [](QDialog &dialog) {
        auto *button = dialog.findChild<QKeySequenceEdit *>("input/4");
        auto *mode = dialog.findChild<QKeySequenceEdit *>("input/special/0");
        auto *command = dialog.findChild<QKeySequenceEdit *>("shortcut/pause");
        if (!button || !mode || !command)
            return smokeFailure(__FILE__, __LINE__);
        button->setKeySequence(QKeySequence(Qt::Key_J));
        mode->setKeySequence(QKeySequence(Qt::Key_Control));
        command->setKeySequence(QKeySequence("Ctrl+F8"));
        return true;
    };
    setPaused(false);
    QApplication::sendEvent(display_, &down);
    if (!settingsDialog([&](QDialog &dialog) {
            if (!changeMappings(dialog) || !paused_ || emulator_->gb.joypad != 0xff)
                return smokeFailure(__FILE__, __LINE__);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            return true;
        }) ||
        paused_ || keys_ != originalKeys || specialKeys_ != originalSpecialKeys ||
        pauseAction_->shortcut() != originalPauseShortcut)
        return smokeFailure(__FILE__, __LINE__);
    if (!settingsDialog([&](QDialog &dialog) {
            if (!changeMappings(dialog))
                return smokeFailure(__FILE__, __LINE__);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return !dialog.isVisible();
        }) ||
        paused_ || keys_[4] != Qt::Key_J || specialKeys_[0] != Qt::Key_Control ||
        pauseAction_->shortcut() != QKeySequence("Ctrl+F8") ||
        settings_.value("input/4").toInt() != Qt::Key_J ||
        settings_.value("input/special/0").toInt() != Qt::Key_Control ||
        settings_.value("shortcuts/pause").toString() != "Ctrl+F8")
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent controlDown(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
    QKeyEvent controlUp(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
    QApplication::sendEvent(display_, &controlDown);
    if (!turbo_)
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(display_, &controlUp);
    if (turbo_)
        return smokeFailure(__FILE__, __LINE__);
    const auto appliedKeys = keys_;
    if (!settingsDialog([&](QDialog &dialog) {
            auto *conflict = dialog.findChild<QKeySequenceEdit *>("input/5");
            if (!conflict)
                return smokeFailure(__FILE__, __LINE__);
            conflict->setKeySequence(QKeySequence(Qt::Key_J));
            bool warningShown = false;
            QTimer::singleShot(0, &dialog, [&] {
                auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (warning) {
                    warningShown = true;
                    warning->accept();
                }
            });
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return warningShown && dialog.isVisible();
        }) ||
        paused_ || keys_ != appliedKeys)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    const QString recordingPath = directory.filePath("forward-audio.wav");
    if (startRecording(romPath_) || startRecording(savePath_))
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false;
    if (!startRecording(recordingPath) || !recorder_->active() ||
        recordingAction_->text() != "Stop Audio &Recording")
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t audioBeforeRecording = generatedAudioFrames_;
    resetRewind();
    rememberFrame();
    if (!saveSlot(8) || !runFrame() || !runFrame())
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t recordedFrames = recorder_->framesWritten();
    const QByteArray deviceState = captureState();
    const QByteArray missingOutput("cupidgb-missing-test-output");
    settings_.setValue("audio/outputDevice", missingOutput);
    setupAudio();
    if (!audioDeviceFallback_ || activeAudioDeviceId_ != QMediaDevices::defaultAudioOutput().id() ||
        !recorder_->active() || recorder_->framesWritten() != recordedFrames ||
        captureState() != deviceState || (audioCheck_ && !audio_))
        return smokeFailure(__FILE__, __LINE__);
    if (!settingsDialog([&](QDialog &dialog) {
            auto *output = dialog.findChild<QComboBox *>("audioOutputDevice");
            if (!output || output->currentData().toByteArray() != missingOutput ||
                !output->currentText().startsWith("Unavailable device"))
                return smokeFailure(__FILE__, __LINE__);
            output->setCurrentIndex(0);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            return true;
        }) ||
        settings_.value("audio/outputDevice").toByteArray() != missingOutput)
        return smokeFailure(__FILE__, __LINE__);
    const auto outputs = QMediaDevices::audioOutputs();
    const QByteArray selectedOutput = outputs.isEmpty() ? QByteArray() : outputs.front().id();
    if (!settingsDialog([&](QDialog &dialog) {
            auto *output = dialog.findChild<QComboBox *>("audioOutputDevice");
            if (!output)
                return smokeFailure(__FILE__, __LINE__);
            const int selectedIndex = output->findData(selectedOutput);
            if (selectedIndex < 0)
                return smokeFailure(__FILE__, __LINE__);
            output->setCurrentIndex(selectedIndex);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return !dialog.isVisible();
        }) ||
        settings_.value("audio/outputDevice").toByteArray() != selectedOutput ||
        audioDeviceFallback_ || !recorder_->active() ||
        recorder_->framesWritten() != recordedFrames || captureState() != deviceState ||
        (!selectedOutput.isEmpty() && activeAudioDeviceId_ != selectedOutput))
        return smokeFailure(__FILE__, __LINE__);
    // A controlled device-list notification exercises deferred renegotiation. It
    // does not substitute for physical disconnection tests on different backends.
    QMetaObject::invokeMethod(mediaDevices_, "audioOutputsChanged", Qt::DirectConnection);
    QEventLoop deviceEvents;
    QTimer::singleShot(20, &deviceEvents, &QEventLoop::quit);
    deviceEvents.exec();
    if (!recorder_->active() || recorder_->framesWritten() != recordedFrames ||
        captureState() != deviceState)
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("audio/outputDevice", QByteArray());
    setupAudio();
    if (!recordedFrames || recordedFrames != generatedAudioFrames_ - audioBeforeRecording ||
        !rewindFrame() || recorder_->framesWritten() != recordedFrames || !loadSlot(8) ||
        !recorder_->active() || recorder_->framesWritten() != recordedFrames)
        return smokeFailure(__FILE__, __LINE__);
    tick();
    if (recorder_->framesWritten() != recordedFrames || !runFrame() ||
        recorder_->framesWritten() <= recordedFrames)
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t finalFrames = recorder_->framesWritten();
    recordingAction_->trigger();
    QFile recorded(recordingPath);
    if (recorder_->active() || !recorded.open(QIODevice::ReadOnly) ||
        recorded.size() != qint64(44 + finalFrames * 4))
        return smokeFailure(__FILE__, __LINE__);
    recorded.close();
    const QString rateChangePath = directory.filePath("rate-change.wav");
    if (!startRecording(rateChangePath) || !runFrame() ||
        !cupid_gb_apu_set_sample_rate(&emulator_->gb, 48000) || !runFrame() ||
        recorder_->active() || QFileInfo(rateChangePath).size() <= 44)
        return smokeFailure(__FILE__, __LINE__);
    const QString loadRatePath = directory.filePath("load-rate-change.wav");
    if (!startRecording(loadRatePath) || !loadSlot(8) || recorder_->active() ||
        QFileInfo(loadRatePath).size() != 44)
        return smokeFailure(__FILE__, __LINE__);
    const QString resetRecordingPath = directory.filePath("reset-audio.wav");
    if (!startRecording(resetRecordingPath) || !runFrame() || !restart() || recorder_->active() ||
        QFileInfo(resetRecordingPath).size() <= 44)
        return smokeFailure(__FILE__, __LINE__);
    const QString closingRecordingPath = directory.filePath("close-audio.wav");
    if (!startRecording(closingRecordingPath) || !runFrame())
        return smokeFailure(__FILE__, __LINE__);
    if (!closeRom() || closeAction_->isEnabled())
        return smokeFailure(__FILE__, __LINE__);
    if (recorder_->active() || recordingAction_->isEnabled() ||
        QFileInfo(closingRecordingPath).size() <= 44)
        return smokeFailure(__FILE__, __LINE__);
    const QString exitRecordingPath = directory.filePath("exit-audio.wav");
    {
        MainWindow closing(true);
        if (!closing.openRom(path) || !closing.startRecording(exitRecordingPath) ||
            !closing.runFrame())
            return smokeFailure(__FILE__, __LINE__);
    }
    if (QFileInfo(exitRecordingPath).size() <= 44)
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("input/4", int(Qt::Key_A));
    settings_.setValue("input/special/0", int(Qt::Key_T));
    settings_.setValue("shortcuts/pause", "Ctrl+F9");
    settings_.setValue("video/scaling", int(DisplayScaling::Aspect));
    settings_.setValue("video/defaultScale", 3);
    settings_.setValue("video/palette", int(MonochromePalette::Teal));
    settings_.sync();
    {
        MainWindow restored(true);
        if (restored.keys_[4] != Qt::Key_A || restored.specialKeys_[0] != Qt::Key_T ||
            restored.pauseAction_->shortcut() != QKeySequence("Ctrl+F9") ||
            restored.display_->scaling != DisplayScaling::Aspect ||
            !restored.paletteActions_[3]->isChecked() ||
            !restored.windowSizeActions_[2]->isChecked())
            return smokeFailure(__FILE__, __LINE__);
    }
    InputKeyEdit capture(0);
    QKeyEvent tabCapture(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(&capture, &tabCapture);
    if (capture.keySequence() != QKeySequence(Qt::Key_Tab))
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(&capture, &slowDown);
    if (capture.keySequence() != QKeySequence(Qt::Key_Shift))
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(&capture, &controlDown);
    if (capture.keySequence() != QKeySequence(Qt::Key_Control))
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(&capture, &shiftedRewindDown);
    if (capture.keySequence() != QKeySequence(Qt::Key_Tab))
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_SGB));
    QFile superCartridge(directory.filePath("super.gb"));
    auto superData = rom;
    superData[0x146] = 3;
    if (!superCartridge.open(QIODevice::WriteOnly) ||
        superCartridge.write(superData) != superData.size())
        return smokeFailure(__FILE__, __LINE__);
    superCartridge.close();
    if (!openRom(superCartridge.fileName()) || image_.size() != QSize(256, 224) ||
        display_->minimumSize() != image_.size())
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    // Border tiles may overlap the central image. Hiding the surrounding frame
    // must retain those overlays, including their palette-selected colors.
    emulator_->gb.sgb.border_tiles[0] = 0xff;
    emulator_->gb.sgb.border_palettes[1] = 0x001f;
    present(true);
    if (image_.pixel(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y) != qRgb(255, 0, 0) ||
        !previousDisplayFrame_.isNull() || display_->image != image_)
        return smokeFailure(__FILE__, __LINE__);
    const QImage bordered = image_;
    blendingActions_[accurateBlend]->trigger();
    emulator_->gb.sgb.border_palettes[1] = 0x0000;
    present(true);
    if (display_->image != blendDisplayFrames(image_, bordered, FrameBlending::Simple, false) ||
        display_->image.pixel(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y) != qRgb(186, 0, 0))
        return smokeFailure(__FILE__, __LINE__);
    blendingActions_[disabledBlend]->trigger();
    emulator_->gb.sgb.border_palettes[1] = 0x001f;
    present();
    borderAction_->trigger();
    if (image_.size() != QSize(160, 144) || display_->minimumSize() != image_.size() ||
        image_ != bordered.copy(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y, 160, 144))
        return smokeFailure(__FILE__, __LINE__);
    borderAction_->trigger();
    if (image_.size() != bordered.size())
        return smokeFailure(__FILE__, __LINE__);
    emulator_->gb.sgb.border_palettes[1] = 0x294a;
    present(true);
    if (image_.pixel(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y) != qRgb(58, 58, 58))
        return smokeFailure(__FILE__, __LINE__);
    colorTemperatureSlider_->setValue(20);
    const QString correctedShot = directory.filePath("corrected-color.png");
    if (image_.pixel(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y) != qRgb(58, 0, 0) ||
        display_->image != image_ || !previousDisplayFrame_.isNull() ||
        !image_.save(correctedShot, "PNG") || QImage(correctedShot) != image_)
        return smokeFailure(__FILE__, __LINE__);
    colorTemperatureSlider_->setValue(10);
    if (!closeRom())
        return smokeFailure(__FILE__, __LINE__);
    // Exercise model selection and boot overlay after cartridge initialization.
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    if (!openRom(path))
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    const uint16_t sampleColor = uint16_t(10u << 5);
    std::fill(std::begin(emulator_->gb.frame_buffer_color),
              std::end(emulator_->gb.frame_buffer_color), sampleColor);
    const QByteArray colorState = captureState();
    present(true);
    if (image_.pixel(0, 0) != (*cgbColorTable_)[sampleColor] ||
        image_.pixel(0, 0) == inspectorRgb555(sampleColor))
        return smokeFailure(__FILE__, __LINE__);
    colorCorrectionActions_[size_t(ColorCorrection::Disabled)]->trigger();
    if (image_.pixel(0, 0) != inspectorRgb555(sampleColor) || !previousDisplayFrame_.isNull() ||
        captureState() != colorState)
        return smokeFailure(__FILE__, __LINE__);
    colorCorrectionActions_[size_t(ColorCorrection::Balanced)]->trigger();
    if (!closeRom())
        return smokeFailure(__FILE__, __LINE__);
    QFile boot(directory.filePath("cgb_boot.bin"));
    if (!boot.open(QIODevice::WriteOnly) || boot.write(QByteArray(0x900, char(0))) != 0x900)
        return smokeFailure(__FILE__, __LINE__);
    boot.close();
    const QString priorDirectory = directory.filePath("saved-boot-preference");
    settings_.setValue("emulation/bootDirectory", priorDirectory);
    {
        MainWindow session(true, false, false, directory.path());
        if (!session.openRom(path) || !session.emulator_->gb.boot_rom_enabled ||
            session.bootDirectory() != directory.path() ||
            settings_.value("emulation/bootDirectory").toString() != priorDirectory)
            return smokeFailure(__FILE__, __LINE__);
        for (const bool accept : {false, true}) {
            bool checked = false;
            QTimer command;
            command.setInterval(1);
            connect(&command, &QTimer::timeout, &session, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "settings")
                    return;
                command.stop();
                auto *edit = dialog ? dialog->findChild<QLineEdit *>("bootDirectory") : nullptr;
                checked = edit && edit->text() == directory.path();
                if (dialog)
                    dialog->findChild<QDialogButtonBox *>()
                        ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
                        ->click();
            });
            command.start();
            session.configure();
            if (!checked || session.bootDirectory() != directory.path() ||
                settings_.value("emulation/bootDirectory").toString() !=
                    (accept ? directory.path() : priorDirectory) ||
                session.bootDirectoryOverride_.isNull() != accept)
                return smokeFailure(__FILE__, __LINE__);
        }
    }
    if (!openRom(path) || emulator_->gb.model != CUPID_GB_MODEL_CGB ||
        !emulator_->gb.boot_rom_enabled || emulator_->gb.cpu.pc != 0)
        return smokeFailure(__FILE__, __LINE__);
    if (!closeRom())
        return smokeFailure(__FILE__, __LINE__);
    if (dropFile(nativeStatePath) || emulator_)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false; // Loading a dropped state requires an active cartridge.
    if (!debuggerSmokeTest(directory.path()))
        return smokeFailure(__FILE__, __LINE__);
    if (!pacingSmokeTest(path) || !modelSmokeTest(directory.path()) ||
        !cheatSmokeTest(path, directory.path()) || !printerSmokeTest(path, directory.path()) ||
        !controllerSmokeTest(directory.path()) || display_->collectPaintIntervals ||
        !display_->paintIntervals.empty() || display_->lastPaintAt)
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB Qt workflow smoke test passed");
    return !failed_;
}

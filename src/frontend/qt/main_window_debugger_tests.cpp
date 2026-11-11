#include "main_window_internal.h"

bool MainWindow::debuggerSmokeTest(const QString &directory) {
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
    settings_.setValue("emulation/bootDirectory", QString());
    bootDirectoryOverride_ = {};
    QByteArray rom(32768, char(0));
    const std::array<uint8_t, 3> entry = {0xc3, 0x50, 0x01};
    const std::array<uint8_t, 11> program = {0x31, 0xfe, 0xff, 0x3e, 0x11, 0xcd,
                                             0x60, 0x01, 0x3c, 0x18, 0xfe};
    std::memcpy(rom.data() + 0x100, entry.data(), entry.size());
    std::memcpy(rom.data() + 0x150, program.data(), program.size());
    rom[0x160] = char(0x3c);
    rom[0x161] = char(0xc9);
    rom.replace(0x134, 14, QByteArray("DEBUG WORKFLOW", 14));
    uint8_t checksum = 0;
    for (int i = 0x134; i <= 0x14c; ++i)
        checksum = uint8_t(checksum - uint8_t(rom[i]) - 1);
    rom[0x14d] = char(checksum);
    const QString path = QDir(directory).filePath(QString::fromUtf8("debugger-\xc3\xa9.gb"));
    QFile cartridge(path);
    if (!cartridge.open(QIODevice::WriteOnly) || cartridge.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    cartridge.close();
    if (!openRom(path))
        return smokeFailure(__FILE__, __LINE__);
    physicalJoypad_ = 0xef;
    cupid_gb_set_buttons(&emulator_->gb, physicalJoypad_);
    const QByteArray beforeTools = captureState();
    openDebugger();
    if (!paused_ || !debuggerWindow_ || !debugger_ || !debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    QEvent deactivateTools(QEvent::WindowDeactivate);
    QEvent deactivateApp(QEvent::ApplicationDeactivate);
    QApplication::sendEvent(this, &deactivateTools);
    QApplication::sendEvent(this, &deactivateApp);
    if (emulator_->gb.joypad != 0xef || physicalJoypad_ != 0xff || captureState() != beforeTools)
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent lateRelease(QEvent::KeyRelease, keys_[4], Qt::NoModifier);
    QKeyEvent latePress(QEvent::KeyPress, keys_[5], Qt::NoModifier);
    QApplication::sendEvent(this, &lateRelease);
    QApplication::sendEvent(display_, &latePress);
    const int oldRapidA = specialKeys_[3];
    specialKeys_[3] = Qt::Key_V;
    QKeyEvent rapidPress(QEvent::KeyPress, Qt::Key_V, Qt::NoModifier);
    QKeyEvent rapidRelease(QEvent::KeyRelease, Qt::Key_V, Qt::NoModifier);
    QApplication::sendEvent(this, &rapidPress);
    QApplication::sendEvent(display_, &rapidRelease);
    specialKeys_[3] = oldRapidA;
    if (captureState() != beforeTools || physicalJoypad_ != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    openInspectors();
    QApplication::sendEvent(this, &deactivateTools);
    if (captureState() != beforeTools)
        return smokeFailure(__FILE__, __LINE__);
    inspectorsWindow_->close();
    setPaused(false);
    physicalJoypad_ = 0xef;
    cupid_gb_set_buttons(&emulator_->gb, physicalJoypad_);
    QApplication::sendEvent(this, &deactivateTools);
    if (emulator_->gb.joypad != 0xff || physicalJoypad_ != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    pauseDebugger();
    const auto complete = [this] {
        QElapsedTimer deadline;
        deadline.start();
        while (debuggerTimer_.isActive() && deadline.elapsed() < 5000)
            debuggerSlice();
        return !debuggerTimer_.isActive();
    };
    debuggerWindow_->submitCommand("set pc 336");
    if (emulator_->gb.cpu.pc != 0x150)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set pc 0x0153");
    if (emulator_->gb.cpu.pc != 0x153)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set pc $0150");
    const QByteArray beforeStep = captureState();
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.pc != 0x153 || debuggerHistory_.size() != 1 ||
        !debuggerBackstep() || captureState() != beforeStep)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set pc $0153");
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.a != 0x11 || emulator_->gb.cpu.pc != 0x155)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("next");
    if (!complete() || emulator_->gb.cpu.pc != 0x158 || emulator_->gb.cpu.a != 0x12 ||
        !debuggerBackstep() || emulator_->gb.cpu.pc != 0x161 || emulator_->gb.cpu.sp != 0xfffc)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.pc != 0x158)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set pc $0155");
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.pc != 0x160)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("finish");
    if (!complete() || emulator_->gb.cpu.pc != 0x158 || !debuggerBackstep() ||
        emulator_->gb.cpu.pc != 0x161)
        return smokeFailure(__FILE__, __LINE__);
    if (debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    frameAction_->trigger();
    const QByteArray afterFrameAdvance = captureState();
    if (!paused_ || !debuggerHistory_.empty() || debuggerTimer_.isActive() || debuggerBackstep() ||
        captureState() != afterFrameAdvance)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set sp $fffe");
    debuggerWindow_->submitCommand("set pc $0150");
    debuggerWindow_->submitCommand("break $00:$0155");
    debuggerWindow_->submitCommand("continue");
    if (paused_ || !runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x155 ||
        !debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    if (!saveSlot(8))
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("step");
    if (!complete() || !loadSlot(8))
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t loadedCycles = emulator_->gb.elapsed_cycles;
    debuggerWindow_->submitCommand("continue");
    if (!runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x155 ||
        emulator_->gb.elapsed_cycles != loadedCycles)
        return smokeFailure(__FILE__, __LINE__);
    frameAction_->trigger();
    if (!paused_ || emulator_->gb.cpu.pc == 0x155 || !debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set pc $0150");
    debuggerWindow_->submitCommand("continue");
    if (!runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x155)
        return smokeFailure(__FILE__, __LINE__);
    pauseAction_->trigger();
    if (paused_ || !runFrame() || emulator_->gb.cpu.pc == 0x155)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("break");
    debuggerWindow_->submitCommand("set pc $0150");
    debuggerWindow_->submitCommand("continue");
    if (!runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x155)
        return smokeFailure(__FILE__, __LINE__);
    // Resuming a hit skips it once; a later visit must stop at the same point.
    debuggerWindow_->submitCommand("continue");
    if (paused_ || !runFrame() || emulator_->gb.cpu.pc == 0x155)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("break");
    debuggerWindow_->submitCommand("delete all");
    if (!paused_ || debugger_->bp_count)
        return smokeFailure(__FILE__, __LINE__);
    // Conditions use live register and memory values, and print must leave the
    // emulated timeline intact. Invalid conditions cannot replace a valid one.
    debuggerWindow_->submitCommand("set pc $0158");
    debuggerWindow_->submitCommand("set a 0");
    debuggerWindow_->submitCommand("break $0158 if a == 3");
    if (debugger_->bp_count != 1 ||
        QString::fromUtf8(debugger_->breakpoints[0].condition) != "a == 3")
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("break $0158 if a = 4");
    if (debugger_->bp_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray beforeExpression = captureState();
    debuggerWindow_->submitCommand("print ($ffff + 2) * 3");
    if (captureState() != beforeExpression || !debuggerWindow_->outputText().contains("$0003 (3)"))
        return smokeFailure(__FILE__, __LINE__);
    // The normal program loops at $0159; use a bounded explicit instruction
    // sequence to check false and true conditions at the same execution point.
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.a != 1)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set pc $0158");
    if (cupid_gb_debugger_match_breakpoint(debugger_.get(), &emulator_->gb))
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set a 3");
    debuggerWindow_->submitCommand("continue");
    if (!runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x158 || emulator_->gb.cpu.a != 3)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("delete all");
    const QString symbolsPath = QDir(directory).filePath("debugger.sym");
    QFile symbols(symbolsPath);
    if (!symbols.open(QIODevice::WriteOnly) ||
        symbols.write("00:0155 Subcall\n00:0160 Inner\n") <= 0)
        return smokeFailure(__FILE__, __LINE__);
    symbols.close();
    const QByteArray beforeSymbols = captureState();
    if (!loadDebuggerSymbols(symbolsPath) || debugger_->symbol_count != 2 ||
        captureState() != beforeSymbols)
        return smokeFailure(__FILE__, __LINE__);
    const QString invalidPath = QDir(directory).filePath("invalid.sym");
    QFile invalid(invalidPath);
    if (!invalid.open(QIODevice::WriteOnly) || invalid.write("invalid symbol file\n") <= 0)
        return smokeFailure(__FILE__, __LINE__);
    invalid.close();
    if (loadDebuggerSymbols(invalidPath) || debugger_->symbol_count != 2 ||
        captureState() != beforeSymbols)
        return smokeFailure(__FILE__, __LINE__);
    // Replacing symbols can invalidate a live condition. Ordinary playback must
    // pause at that boundary too, rather than silently executing past the error.
    debuggerWindow_->submitCommand("set pc $0158");
    debuggerWindow_->submitCommand("break $0158 if Inner != 0");
    const QString withoutInnerPath = QDir(directory).filePath("debugger-without-inner.sym");
    QFile withoutInner(withoutInnerPath);
    if (!withoutInner.open(QIODevice::WriteOnly) || withoutInner.write("00:0155 Subcall\n") <= 0)
        return smokeFailure(__FILE__, __LINE__);
    withoutInner.close();
    if (!loadDebuggerSymbols(withoutInnerPath) || debugger_->symbol_count != 1 ||
        debugger_->bp_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    auto *const conditionSession = emulator_.get();
    debuggerWindow_->submitCommand("continue");
    emulator_->gb.frame_ready = true;
    const QByteArray beforeConditionError = captureState();
    const uint64_t conditionCycles = emulator_->gb.elapsed_cycles;
    if (paused_ || !runFrame() || !paused_ || emulator_.get() != conditionSession ||
        emulator_->gb.cpu.pc != 0x158 || emulator_->gb.elapsed_cycles != conditionCycles ||
        debugger_->last_stop != CUPID_GB_DEBUG_STOP_ERROR || debuggerTimer_.isActive() ||
        !emulator_->gb.frame_ready || !debuggerWindow_->isVisible() ||
        captureState() != beforeConditionError ||
        !debuggerWindow_->outputText().contains("Breakpoint condition evaluation failed."))
        return smokeFailure(__FILE__, __LINE__);
    const QString beforeErrorInspection = debuggerWindow_->outputText();
    debuggerWindow_->submitCommand("print pc");
    if (debuggerWindow_->outputText() == beforeErrorInspection ||
        captureState() != beforeConditionError || !loadDebuggerSymbols(symbolsPath))
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("delete all");
    debuggerWindow_->submitCommand("set pc $0155");
    debuggerWindow_->submitCommand("set sp $fffe");
    debuggerWindow_->submitCommand("break $0160 if Inner != 0");
    if (!loadDebuggerSymbols(withoutInnerPath) || debugger_->symbol_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray beforeNextConditionError = captureState();
    const uint64_t beforeNextConditionCycles = emulator_->gb.elapsed_cycles;
    debuggerWindow_->submitCommand("next");
    if (!complete() || !paused_ || emulator_->gb.cpu.pc != 0x160 ||
        emulator_->gb.elapsed_cycles <= beforeNextConditionCycles ||
        debugger_->last_stop != CUPID_GB_DEBUG_STOP_ERROR || debuggerHistory_.size() != 1 ||
        !debuggerWindow_->outputText().contains("Breakpoint condition evaluation failed.") ||
        !debuggerBackstep() || emulator_->gb.cpu.pc != 0x155 ||
        captureState() != beforeNextConditionError || !loadDebuggerSymbols(symbolsPath))
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("delete all");
    debuggerWindow_->submitCommand("break Inner");
    if (debugger_->bp_count != 1 || debugger_->breakpoints[0].address != 0x160)
        return smokeFailure(__FILE__, __LINE__);
    if (!restart() || debugger_->bp_count != 1 || debugger_->symbol_count != 2 ||
        !debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    pauseDebugger();
    debuggerWindow_->submitCommand("delete 1");
    debuggerWindow_->submitCommand("set af $123f");
    if (emulator_->gb.cpu.a != 0x12 || emulator_->gb.cpu.f != 0x30)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray beforeInvalid = captureState();
    debuggerWindow_->submitCommand("set f $1ff");
    debuggerWindow_->submitCommand("write $0100 $00");
    if (captureState() != beforeInvalid)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("set pc $0159");
    for (unsigned instruction = 0; instruction < 132; ++instruction) {
        debuggerWindow_->submitCommand("step");
        if (!complete())
            return smokeFailure(__FILE__, __LINE__);
    }
    if (debuggerHistory_.size() != 128 || debuggerHistoryBytes_ > 32 * 1024 * 1024)
        return smokeFailure(__FILE__, __LINE__);
    const auto historyCount = debuggerHistory_.size();
    if (openRom(QDir(directory).filePath("absent-debugger.gb")) ||
        debuggerHistory_.size() != historyCount)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false;
    const QByteArray beforeNoise = captureState();
    resetDebuggerTimeline();
    uint32_t randomState = 0x17382fadu;
    const auto randomByte = [&randomState] {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return uint8_t(randomState >> 24);
    };
    const auto fillNoise = [&randomByte](void *data, size_t length) {
        auto *bytes = static_cast<uint8_t *>(data);
        for (size_t i = 0; i < length; ++i)
            bytes[i] = randomByte();
    };
    auto &noisy = emulator_->gb;
    fillNoise(noisy.video_ram, sizeof(noisy.video_ram));
    fillNoise(noisy.cartridge_ram, sizeof(noisy.cartridge_ram));
    fillNoise(noisy.work_ram, sizeof(noisy.work_ram));
    fillNoise(noisy.high_ram, sizeof(noisy.high_ram));
    fillNoise(noisy.object_attribute_memory, sizeof(noisy.object_attribute_memory));
    fillNoise(noisy.apu.buf, sizeof(noisy.apu.buf));
    fillNoise(noisy.sgb.border_tiles, sizeof(noisy.sgb.border_tiles));
    fillNoise(noisy.sgb.attribute_files, sizeof(noisy.sgb.attribute_files));
    fillNoise(noisy.sgb.ram_palettes, sizeof(noisy.sgb.ram_palettes));
    fillNoise(noisy.sgb.border_map, sizeof(noisy.sgb.border_map));
    fillNoise(noisy.sgb.border_palettes, sizeof(noisy.sgb.border_palettes));
    for (size_t i = 0; i < 160 * 144; ++i) {
        noisy.frame_buffer[i] = randomByte() & 3u;
        noisy.sgb.freeze_buffer[i] = randomByte() & 3u;
        noisy.frame_buffer_color[i] =
            uint16_t((uint16_t(randomByte()) * 256u + randomByte()) & 0x7fffu);
    }
    for (auto &attribute : noisy.sgb.attribute_map)
        attribute = randomByte() & 3u;
    for (unsigned instruction = 0; instruction < 132; ++instruction) {
        debuggerWindow_->submitCommand("step");
        if (!complete())
            return smokeFailure(__FILE__, __LINE__);
    }
    qsizetype retainedBytes = 0;
    for (const auto &snapshot : debuggerHistory_)
        retainedBytes += snapshot.capacity() + qsizetype(sizeof(QByteArray));
    if (debuggerHistory_.empty() || debuggerHistory_.size() >= 128 ||
        retainedBytes != debuggerHistoryBytes_ || retainedBytes > 32 * 1024 * 1024 ||
        !restoreState(beforeNoise, false))
        return smokeFailure(__FILE__, __LINE__);
    resetDebuggerTimeline();
    const QString debuggerAudio = QDir(directory).filePath("debugger-audio.wav");
    if (!startRecording(debuggerAudio))
        return smokeFailure(__FILE__, __LINE__);
    for (unsigned instruction = 0; instruction < 16; ++instruction) {
        debuggerWindow_->submitCommand("step");
        if (!complete())
            return smokeFailure(__FILE__, __LINE__);
    }
    const uint64_t debuggerAudioFrames = recorder_->framesWritten();
    if (!debuggerAudioFrames || !debuggerBackstep() || !recorder_->active() ||
        recorder_->framesWritten() != debuggerAudioFrames || !stopRecording())
        return smokeFailure(__FILE__, __LINE__);
    if (!saveSlot(9))
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("step");
    if (!complete() || !loadSlot(9) || !debuggerHistory_.empty() || debuggerTimer_.isActive())
        return smokeFailure(__FILE__, __LINE__);
    openInspectors();
    if (!inspectorsWindow_ || !paused_ || !inspectorsWindow_->writeByte(0xc000, 0x5a) ||
        cupid_gb_debug_peek(&emulator_->gb, 0xc000) != 0x5a ||
        inspectorsWindow_->writeByte(0x100, 0) || !debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    for (int tab = 0; tab < 5; ++tab)
        inspectorsWindow_->selectTab(tab);
    debuggerWindow_->submitCommand("finish");
    if (!debuggerTimer_.isActive())
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent escapeDebugger(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(debuggerWindow_, &escapeDebugger);
    if (debuggerWindow_->isVisible() || debuggerTimer_.isActive() || !debuggerHistory_.empty() ||
        !paused_)
        return smokeFailure(__FILE__, __LINE__);
    openDebugger();
    debuggerWindow_->submitCommand("finish");
    if (!debuggerTimer_.isActive())
        return smokeFailure(__FILE__, __LINE__);
    if (!openRom(path) || debuggerTimer_.isActive() || !debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    const QString replacementPath = QDir(directory).filePath("replacement-debugger.gb");
    QFile replacement(replacementPath);
    if (!replacement.open(QIODevice::WriteOnly) || replacement.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    replacement.close();
    QFile companion(QDir(directory).filePath("replacement-debugger.sym"));
    if (!companion.open(QIODevice::WriteOnly) || companion.write("00:0159 Replacement\n") <= 0)
        return smokeFailure(__FILE__, __LINE__);
    companion.close();
    debuggerWindow_->submitCommand("break Inner");
    if (!openRom(replacementPath) || debugger_->bp_count || debugger_->symbol_count != 1 ||
        QString::fromUtf8(cupid_gb_debugger_symbol_for_address(debugger_.get(), 0, 0x159, true)) !=
            "Replacement")
        return smokeFailure(__FILE__, __LINE__);
    rom[0x150] = char(0xd3);
    const QString illegalPath = QDir(directory).filePath("illegal-debugger.gb");
    QFile illegal(illegalPath);
    if (!illegal.open(QIODevice::WriteOnly) || illegal.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    illegal.close();
    if (!openRom(illegalPath))
        return smokeFailure(__FILE__, __LINE__);
    openDebugger();
    debuggerWindow_->submitCommand("set pc $0150");
    const QByteArray beforeIllegal = captureState();
    debuggerWindow_->submitCommand("step");
    if (!complete() || !emulator_->gb.cpu.halted ||
        debugger_->last_stop != CUPID_GB_DEBUG_STOP_ERROR || debuggerHistory_.size() != 1 ||
        !debuggerBackstep() || captureState() != beforeIllegal)
        return smokeFailure(__FILE__, __LINE__);
    const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QDir captures(captureDirectory);
        if (!captures.mkpath("."))
            return smokeFailure(__FILE__, __LINE__);
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
        rom[0x143] = char(0x80);
        rom[0x150] = char(0x31);
        checksum = 0;
        for (int i = 0x134; i <= 0x14c; ++i)
            checksum = uint8_t(checksum - uint8_t(rom[i]) - 1);
        rom[0x14d] = char(checksum);
        const QString capturePath = QDir(directory).filePath("capture-debugger.gbc");
        QFile captureRom(capturePath);
        if (!captureRom.open(QIODevice::WriteOnly) || captureRom.write(rom) != rom.size())
            return smokeFailure(__FILE__, __LINE__);
        captureRom.close();
        if (!openRom(capturePath))
            return smokeFailure(__FILE__, __LINE__);
        openDebugger();
        debuggerWindow_->submitCommand("set pc $0150");
        if (!loadDebuggerSymbols(symbolsPath))
            return smokeFailure(__FILE__, __LINE__);
        debuggerWindow_->submitCommand("break Inner");
        auto &fixture = emulator_->gb;
        for (size_t bank = 0; bank < 2; ++bank) {
            for (size_t tile = 0; tile < 384; ++tile) {
                for (size_t row = 0; row < 8; ++row) {
                    fixture.video_ram[bank * 0x2000 + tile * 16 + row * 2] =
                        uint8_t(0x81u >> ((tile + row) % 4));
                    fixture.video_ram[bank * 0x2000 + tile * 16 + row * 2 + 1] =
                        uint8_t((tile & 1u) ? 0x55u : 0xaau);
                }
            }
        }
        for (size_t cell = 0; cell < 1024; ++cell) {
            fixture.video_ram[0x1800 + cell] = uint8_t(cell);
            fixture.video_ram[0x1c00 + cell] = uint8_t(255 - cell);
            fixture.video_ram[0x3800 + cell] = uint8_t((cell / 32) & 7u);
            fixture.video_ram[0x3c00 + cell] = uint8_t((cell / 32) & 7u);
        }
        for (size_t object = 0; object < 40; ++object) {
            fixture.object_attribute_memory[object * 4] = uint8_t(16 + (object / 10) * 24);
            fixture.object_attribute_memory[object * 4 + 1] = uint8_t(8 + (object % 10) * 16);
            fixture.object_attribute_memory[object * 4 + 2] = uint8_t(object);
            fixture.object_attribute_memory[object * 4 + 3] = uint8_t(object & 7u);
        }
        for (size_t color = 0; color < 32; ++color) {
            const uint16_t rgb = uint16_t((color % 4) * 10 + (((color / 4) % 4) * 10 << 5) +
                                          ((color % 4) * 8 << 10));
            fixture.cgb_bg_palette_ram[color * 2] = uint8_t(rgb);
            fixture.cgb_bg_palette_ram[color * 2 + 1] = uint8_t(rgb >> 8);
            fixture.cgb_obj_palette_ram[color * 2] = uint8_t(rgb);
            fixture.cgb_obj_palette_ram[color * 2 + 1] = uint8_t(rgb >> 8);
        }
        refreshDebugger();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!debuggerWindow_->grab().save(captures.filePath("debugger.png")))
            return smokeFailure(__FILE__, __LINE__);
        openInspectors();
        const std::array<const char *, 5> names = {"memory.png", "tiles.png", "maps.png",
                                                   "objects.png", "palettes.png"};
        for (int tab = 0; tab < 5; ++tab) {
            inspectorsWindow_->selectTab(tab);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (!inspectorsWindow_->grab().save(captures.filePath(names[size_t(tab)])))
                return smokeFailure(__FILE__, __LINE__);
        }
    }
    if (!closeRom())
        return smokeFailure(__FILE__, __LINE__);
    inspectorsWindow_->refresh();
    const auto noRomOutput = debuggerWindow_->outputText();
    debuggerWindow_->submitCommand("step");
    if (emulator_ || debuggerTimer_.isActive() || debuggerWindow_->outputText() == noRomOutput ||
        !debuggerWindow_->findChild<QPlainTextEdit *>("debuggerRegisters")->toPlainText().isEmpty())
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->close();
    inspectorsWindow_->close();
    return true;
}

/**
 * @file    main_window_debugger_tests.cpp
 * @brief   Runtime debugger command, timeline, and inspector workflow regression tests.
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
#include "main_window_state_test_helpers.h"

#include <QScopeGuard>
#include <QTableWidget>

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
    const QByteArray beforeStep = hardwareSnapshotForTest(emulator_->gb);
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.pc != 0x153 || debuggerHistory_.size() != 1 ||
        !debuggerBackstep() || !matchesHardwareForTest(emulator_->gb, beforeStep))
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
    const QString symbolsPath =
        QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() + ".sym");
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
    const QByteArray beforeNextConditionError = hardwareSnapshotForTest(emulator_->gb);
    const uint64_t beforeNextConditionCycles = emulator_->gb.elapsed_cycles;
    debuggerWindow_->submitCommand("next");
    if (!complete() || !paused_ || emulator_->gb.cpu.pc != 0x160 ||
        emulator_->gb.elapsed_cycles <= beforeNextConditionCycles ||
        debugger_->last_stop != CUPID_GB_DEBUG_STOP_ERROR || debuggerHistory_.size() != 1 ||
        !debuggerWindow_->outputText().contains("Breakpoint condition evaluation failed.") ||
        !debuggerBackstep() || emulator_->gb.cpu.pc != 0x155 ||
        !matchesHardwareForTest(emulator_->gb, beforeNextConditionError) ||
        !loadDebuggerSymbols(symbolsPath))
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
    fillNoise(noisy.apu.output.buf, sizeof(noisy.apu.output.buf));
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
    if (!rememberDebuggerStep() ||
        !debuggerWritePhysical(InspectorMemoryRegion::WorkRam, 1, 0xfff, 0xb7) ||
        emulator_->gb.work_ram[0x1fff] != 0xb7 || !debuggerHistory_.empty() || !paused_)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray physicalBoundary = captureState();
    const uint64_t physicalTimeline = timelineId_;
    if (debuggerWritePhysical(InspectorMemoryRegion::WorkRam, 2, 0, 0) ||
        debuggerWritePhysical(InspectorMemoryRegion::VideoRam, 1, 0, 0) ||
        debuggerWritePhysical(InspectorMemoryRegion::Rom, 0, 0x100, 0) ||
        debuggerWritePhysical(InspectorMemoryRegion::CartridgeRam, 0, 0, 0) ||
        captureState() != physicalBoundary || timelineId_ != physicalTimeline)
        return smokeFailure(__FILE__, __LINE__);
    {
        QByteArray bankedRom = rom;
        bankedRom[0x143] = char(0x80);
        bankedRom[0x147] = char(0x1b);
        bankedRom[0x149] = char(4);
        uint8_t sum = 0;
        for (int i = 0x134; i <= 0x14c; ++i)
            sum = uint8_t(sum - uint8_t(bankedRom[i]) - 1);
        bankedRom[0x14d] = char(sum);
        const QString bankedPath = QDir(directory).filePath("physical-debugger.gbc");
        QFile file(bankedPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(bankedRom) != bankedRom.size())
            return smokeFailure(__FILE__, __LINE__);
        file.close();
        MainWindow physical(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_CGB);
        physical.timer_.stop();
        if (!physical.openRom(bankedPath))
            return smokeFailure(__FILE__, __LINE__);
        physical.openInspectors();
        auto *region = physical.inspectorsWindow_->findChild<QComboBox *>("inspectorMemoryRegion");
        auto *bank = physical.inspectorsWindow_->findChild<QComboBox *>("inspectorMemoryBank");
        auto *memory = physical.inspectorsWindow_->findChild<QTableWidget *>("inspectorMemory");
        if (!region || !bank || !memory)
            return smokeFailure(__FILE__, __LINE__);
        region->setCurrentIndex(int(InspectorMemoryRegion::VideoRam));
        bank->setCurrentIndex(1);
        memory->item(0, 0)->setText("37");
        if (physical.emulator_->gb.video_ram[0x2000] != 0x37 || !physical.paused_ ||
            !physical.debuggerWritePhysical(InspectorMemoryRegion::WorkRam, 7, 0xfff, 0x81) ||
            physical.emulator_->gb.work_ram[0x7fff] != 0x81)
            return smokeFailure(__FILE__, __LINE__);
        const uint64_t generation = physical.emulator_->gb.battery_write_generation;
        if (!physical.rememberDebuggerStep() ||
            !physical.debuggerWritePhysical(InspectorMemoryRegion::CartridgeRam, 15, 0x1fff,
                                            0x69) ||
            physical.emulator_->gb.battery_write_generation != generation + 1 ||
            !physical.debuggerHistory_.empty() || !physical.flushBattery(true))
            return smokeFailure(__FILE__, __LINE__);
        QFile saved(physical.savePath_);
        if (!saved.open(QIODevice::ReadOnly))
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray battery = saved.readAll();
        if (battery.size() < 0x20000 || battery[0x1ffff] != char(0x69))
            return smokeFailure(__FILE__, __LINE__);
        saved.close();
        if (!physical.closeRom() || !physical.openRom(bankedPath) ||
            cupid_gb_cartridge_ram(&physical.emulator_->gb)[0x1ffff] != 0x69 ||
            !physical.closeRom())
            return smokeFailure(__FILE__, __LINE__);
    }
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
    QByteArray illegalRom = rom;
    illegalRom[0x143] = char(0x80);
    illegalRom[0x150] = char(0xd3);
    illegalRom[0x151] = char(0x3c); // INC A; JR back to INC after a debugger releases HALT.
    illegalRom[0x152] = char(0x18);
    illegalRom[0x153] = char(0xfd);
    uint8_t illegalChecksum = 0;
    for (int i = 0x134; i <= 0x14c; ++i)
        illegalChecksum = uint8_t(illegalChecksum - uint8_t(illegalRom[i]) - 1);
    illegalRom[0x14d] = char(illegalChecksum);
    const QString illegalPath = QDir(directory).filePath("illegal-debugger.gb");
    QFile illegal(illegalPath);
    if (!illegal.open(QIODevice::WriteOnly) || illegal.write(illegalRom) != illegalRom.size())
        return smokeFailure(__FILE__, __LINE__);
    illegal.close();
    if (!openRom(illegalPath))
        return smokeFailure(__FILE__, __LINE__);
    openDebugger();
    debuggerWindow_->submitCommand("set pc $0150");
    debuggerWindow_->submitCommand("write $ffff $1f");
    const QByteArray beforeIllegal = hardwareSnapshotForTest(emulator_->gb);
    const uint64_t beforeIllegalCycles = emulator_->gb.elapsed_cycles;
    const auto beforeIllegalOutput = debuggerWindow_->outputText().size();
    const bool beforeIllegalFailure = failed_;
    debuggerWindow_->submitCommand("step");
    if (!complete() || !emulator_->gb.cpu.halted || emulator_->gb.interrupt_enable ||
        emulator_->gb.cpu.pc != 0x151 || emulator_->gb.elapsed_cycles != beforeIllegalCycles + 4 ||
        debugger_->last_stop != CUPID_GB_DEBUG_STOP_ILLEGAL_OPCODE ||
        debuggerHistory_.size() != 1 ||
        !debuggerWindow_->outputText()
             .mid(beforeIllegalOutput)
             .contains("Illegal opcode: CPU halted") ||
        failed_ != beforeIllegalFailure || !debuggerBackstep() ||
        !matchesHardwareForTest(emulator_->gb, beforeIllegal))
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("step");
    if (!complete() || !saveSlot(9))
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray lockedState = hardwareSnapshotForTest(emulator_->gb);
    const uint64_t lockedCycles = emulator_->gb.elapsed_cycles;
    debuggerWindow_->submitCommand("write $ff0f $1f");
    debuggerWindow_->submitCommand("step");
    if (!complete() || !emulator_->gb.cpu.halted || emulator_->gb.cpu.pc != 0x151 ||
        emulator_->gb.interrupt_enable || emulator_->gb.elapsed_cycles != lockedCycles + 4)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("continue");
    const uint64_t beforeContinue = emulator_->gb.elapsed_cycles;
    if (paused_ || !runFrame() || paused_ || failed_ != beforeIllegalFailure ||
        !emulator_->gb.cpu.halted || emulator_->gb.cpu.pc != 0x151 ||
        emulator_->gb.elapsed_cycles <= beforeContinue)
        return smokeFailure(__FILE__, __LINE__);
    pauseDebugger();
    // IF cannot wake the locked CPU until a host explicitly restores IE.
    debuggerWindow_->submitCommand("write $ffff $1f");
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.halted || emulator_->gb.cpu.pc != 0x151)
        return smokeFailure(__FILE__, __LINE__);
    const uint8_t beforeReleasedA = emulator_->gb.cpu.a;
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.pc != 0x152 ||
        emulator_->gb.cpu.a != uint8_t(beforeReleasedA + 1) || !loadSlot(9) ||
        !matchesHardwareForTest(emulator_->gb, lockedState) || !emulator_->gb.cpu.halted ||
        emulator_->gb.interrupt_enable)
        return smokeFailure(__FILE__, __LINE__);
    debuggerWindow_->submitCommand("reset reload");
    if (!emulator_ || emulator_->gb.cpu.halted || emulator_->gb.cpu.pc != 0x100 ||
        emulator_->gb.elapsed_cycles || failed_ != beforeIllegalFailure)
        return smokeFailure(__FILE__, __LINE__);
    // The debugger remains open while playback uses its own battery and state paths.
    const QString playbackPath = QDir(directory).filePath("illegal-playback.gb");
    if (!QFile::copy(illegalPath, playbackPath))
        return smokeFailure(__FILE__, __LINE__);
    for (const auto model : {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB}) {
        MainWindow session(true, true, false, QStringLiteral(""), int(model));
        session.timer_.stop();
        if (!session.openRom(playbackPath) || session.debugger_)
            return smokeFailure(__FILE__, __LINE__);
        for (unsigned frame = 0; frame < 3; ++frame) {
            const uint64_t beforeCycles = session.emulator_->gb.elapsed_cycles;
            const uint64_t beforeAudio = session.generatedAudioFrames_;
            if (!session.runFrame() || session.failed_ || session.paused_ || session.debugger_ ||
                !session.emulator_->gb.cpu.halted || session.emulator_->gb.cpu.pc != 0x151 ||
                session.emulator_->gb.interrupt_enable || !session.emulator_->gb.frame_ready ||
                session.emulator_->gb.elapsed_cycles <= beforeCycles ||
                session.generatedAudioFrames_ <= beforeAudio)
                return smokeFailure(__FILE__, __LINE__);
        }
        const QByteArray savedLock = hardwareSnapshotForTest(session.emulator_->gb);
        if (!session.saveSlot(8) || !session.runFrame() || !session.loadSlot(8) ||
            !matchesHardwareForTest(session.emulator_->gb, savedLock) || !session.runFrame() ||
            session.failed_ || session.paused_ || !session.emulator_->gb.cpu.halted ||
            !session.restart() || session.emulator_->gb.cpu.halted ||
            session.emulator_->gb.cpu.pc != 0x100 || !session.runFrame() || session.failed_ ||
            session.paused_ || !session.emulator_->gb.cpu.halted || !session.closeRom())
            return smokeFailure(__FILE__, __LINE__);
    }
    // Both MMM01 dump layouts share logical labels and qualified expressions.
    QByteArray menuFirst(0x20000, char(0));
    for (int bank = 0; bank < 8; ++bank)
        std::memset(menuFirst.data() + bank * 0x4000, bank, 0x4000);
    const auto menuHeader = [&menuFirst](int offset, uint8_t type) {
        for (int byte = 0; byte < 48; ++byte)
            menuFirst[offset + 0x104 + byte] = char(byte + 1);
        menuFirst.replace(offset + 0x134, 16,
                          QByteArray("DEBUG MMM01", 11).leftJustified(16, '\0'));
        menuFirst[offset + 0x143] = char(0x80);
        menuFirst[offset + 0x146] = 0;
        menuFirst[offset + 0x147] = char(type);
        menuFirst[offset + 0x148] = 2;
        menuFirst[offset + 0x149] = 0;
        uint8_t sum = 0;
        for (int byte = 0x134; byte <= 0x14c; ++byte)
            sum = uint8_t(sum - uint8_t(menuFirst[offset + byte]) - 1);
        menuFirst[offset + 0x14d] = char(sum);
    };
    menuHeader(0, 0x0b);
    menuHeader(0x8000, 0x01);
    for (unsigned color = 0; color < 2; ++color) {
        settings_.setValue("emulation/model",
                           int(color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC));
        for (unsigned layout = 0; layout < 2; ++layout) {
            const QByteArray image =
                layout ? menuFirst.mid(0x8000) + menuFirst.left(0x8000) : menuFirst;
            const QString stem = QString("debugger-mmm01-%1-%2").arg(color).arg(layout);
            const QString imagePath = QDir(directory).filePath(stem + ".gb");
            QFile imageFile(imagePath);
            QFile labelsFile(QDir(directory).filePath(stem + ".sym"));
            if (!imageFile.open(QIODevice::WriteOnly) || imageFile.write(image) != image.size() ||
                !labelsFile.open(QIODevice::WriteOnly) ||
                labelsFile.write("fffe:0200 MenuEntry\nffff:4200 MenuUpper\n03:4200 GameEntry\n") <=
                    0)
                return smokeFailure(__FILE__, __LINE__);
            imageFile.close();
            labelsFile.close();
            if (!openRom(imagePath))
                return smokeFailure(__FILE__, __LINE__);
            openDebugger();
            openInspectors();
            debuggerWindow_->submitCommand("set pc $0200");
            auto *context = inspectorsWindow_->findChild<QLabel *>("inspectorContext");
            auto *listing = debuggerWindow_->findChild<QPlainTextEdit *>("debuggerDisassembly");
            if (!context || !listing ||
                !context->text().contains("ROM0 bank 65534 | ROMX bank 65535") ||
                !listing->toPlainText().contains("MenuEntry:\n$fffe:$0200"))
                return smokeFailure(__FILE__, __LINE__);
            const QByteArray beforeMenuQueries = captureState();
            debuggerWindow_->submitCommand("print pc");
            if (!debuggerWindow_->outputText().contains("$1fe:$0200 (512)"))
                return smokeFailure(__FILE__, __LINE__);
            debuggerWindow_->submitCommand("print [$1ff:$4200]");
            if (!debuggerWindow_->outputText().contains("$0001 (1)") ||
                captureState() != beforeMenuQueries)
                return smokeFailure(__FILE__, __LINE__);
            for (unsigned region = 0; region < 2; ++region) {
                const uint16_t menuAddress = region ? 0x4200 : 0x0200;
                const int menuBank = region ? 0xffff : 0xfffe;
                const QString location = QString("$%1").arg(menuAddress, 4, 16, QChar('0'));
                debuggerWindow_->submitCommand("set pc " + location);
                const QByteArray beforeMenuBreaks = captureState();
                const std::array<QString, 2> commands = {
                    region ? "break MenuUpper" : "break MenuEntry",
                    QString("break $%1:%2").arg(menuBank, 0, 16).arg(location)};
                for (const auto &command : commands) {
                    debuggerWindow_->submitCommand("delete all");
                    debuggerWindow_->submitCommand(command);
                    if (debugger_->bp_count != 1 ||
                        debugger_->breakpoints[0].bank != (menuBank & 0x1ff) ||
                        cupid_gb_debugger_match_breakpoint(debugger_.get(), &emulator_->gb))
                        return smokeFailure(__FILE__, __LINE__);
                }
                // Host callers can address the full mapped identity explicitly.
                debuggerWindow_->submitCommand("delete all");
                if (!cupid_gb_debugger_breakpoint_add(debugger_.get(), menuBank, menuAddress) ||
                    debugger_->breakpoints[0].bank != menuBank ||
                    !cupid_gb_debugger_match_breakpoint(debugger_.get(), &emulator_->gb))
                    return smokeFailure(__FILE__, __LINE__);
                debuggerWindow_->submitCommand("delete all");
                debuggerWindow_->submitCommand("break *:" + location);
                if (debugger_->bp_count != 1 || debugger_->breakpoints[0].bank != -1 ||
                    !cupid_gb_debugger_match_breakpoint(debugger_.get(), &emulator_->gb) ||
                    captureState() != beforeMenuBreaks)
                    return smokeFailure(__FILE__, __LINE__);
                debuggerWindow_->submitCommand("delete all");
            }
            auto &mapped = emulator_->gb;
            cupid_gb_write_u8(&mapped, 0x2000, 3);
            cupid_gb_write_u8(&mapped, 0x4000, 0);
            cupid_gb_write_u8(&mapped, 0x6000, 0);
            cupid_gb_write_u8(&mapped, 0, 0x40);
            debuggerWindow_->submitCommand("set pc $4200");
            if (!context->text().contains("ROM0 bank 0 | ROMX bank 3") ||
                !listing->toPlainText().contains("GameEntry:\n$03:$4200"))
                return smokeFailure(__FILE__, __LINE__);
            const QByteArray beforeGameQueries = captureState();
            debuggerWindow_->submitCommand("print {3:$4200}");
            if (!debuggerWindow_->outputText().contains("$0505 (1285)"))
                return smokeFailure(__FILE__, __LINE__);
            const auto oldOutputSize = debuggerWindow_->outputText().size();
            debuggerWindow_->submitCommand("x $03:$4200 1");
            const QString memoryOutput = debuggerWindow_->outputText().mid(oldOutputSize);
            if (!memoryOutput.contains("05") || memoryOutput.contains("not currently mapped"))
                return smokeFailure(__FILE__, __LINE__);
            debuggerWindow_->submitCommand("break GameEntry if [3:$4200] == 5");
            if (debugger_->bp_count != 1 || debugger_->breakpoints[0].bank != 3 ||
                !cupid_gb_debugger_match_breakpoint(debugger_.get(), &mapped) ||
                captureState() != beforeGameQueries)
                return smokeFailure(__FILE__, __LINE__);
        }
    }
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
        inspectorsWindow_->selectTab(0);
        auto *memoryRegion = inspectorsWindow_->findChild<QComboBox *>("inspectorMemoryRegion");
        auto *memoryBank = inspectorsWindow_->findChild<QComboBox *>("inspectorMemoryBank");
        const QByteArray beforeBanks = captureState();
        memoryRegion->setCurrentIndex(int(InspectorMemoryRegion::WorkRam));
        memoryBank->setCurrentIndex(7);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!inspectorsWindow_->grab().save(captures.filePath("memory-wram-bank7.png")))
            return smokeFailure(__FILE__, __LINE__);
        memoryRegion->setCurrentIndex(int(InspectorMemoryRegion::VideoRam));
        memoryBank->setCurrentIndex(1);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!inspectorsWindow_->grab().save(captures.filePath("memory-vram-bank1.png")) ||
            captureState() != beforeBanks)
            return smokeFailure(__FILE__, __LINE__);
        memoryRegion->setCurrentIndex(int(InspectorMemoryRegion::Mapped));
        auto *grid = inspectorsWindow_->findChild<QCheckBox *>("inspectorGrid");
        auto *scrolling = inspectorsWindow_->findChild<QCheckBox *>("inspectorScrolling");
        if (!grid || !scrolling)
            return smokeFailure(__FILE__, __LINE__);
        fixture.io_registers[0x42] = 224;
        fixture.io_registers[0x43] = 200;
        const QByteArray overlayState = captureState();
        grid->setChecked(true);
        scrolling->setChecked(true);
        inspectorsWindow_->selectTab(2);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!inspectorsWindow_->grab().save(captures.filePath("maps-overlay.png")))
            return smokeFailure(__FILE__, __LINE__);
        inspectorsWindow_->selectTab(1);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!inspectorsWindow_->grab().save(captures.filePath("tiles-grid.png")) ||
            captureState() != overlayState)
            return smokeFailure(__FILE__, __LINE__);
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
    return reloadSmokeTest(directory);
}

bool MainWindow::reloadSmokeTest(const QString &directory) {
    MainWindow session(true, true, false, QStringLiteral(""));
    session.timer_.stop();
    const auto previousModel = session.settings_.value("emulation/model");
    const auto restoreSettings = qScopeGuard([&session, &previousModel] {
        session.settings_.setValue("emulation/model", previousModel);
    });
    session.settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
    if (!session.reloadAction_ || session.reloadAction_->isEnabled() || session.reloadRom())
        return smokeFailure(__FILE__, __LINE__);
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0xc3);
    rom[0x101] = char(0x50);
    rom[0x102] = char(0x01);
    rom[0x147] = char(0x03);
    rom[0x149] = char(0x02);
    rom[0x150] = char(0x3e);
    rom[0x151] = char(0x11);
    rom[0x152] = char(0x18);
    rom[0x153] = char(0xfe);
    uint8_t checksum = 0;
    for (int i = 0x134; i <= 0x14c; ++i)
        checksum = uint8_t(checksum - uint8_t(rom[i]) - 1);
    rom[0x14d] = char(checksum);
    const QString path = QDir(directory).filePath(QString::fromUtf8("reload-\xc3\xa9.gb"));
    const QFileInfo info(path);
    const QString symbolsPath = info.dir().filePath(info.completeBaseName() + ".sym");
    if (!write(path, rom) || !write(symbolsPath, "00:0150 OldEntry\n") || !session.openRom(path) ||
        !session.reloadAction_->isEnabled())
        return smokeFailure(__FILE__, __LINE__);
    session.ensureDebugger();
    session.pauseDebugger();
    session.debuggerWindow_->submitCommand("break $0152");
    session.debuggerWindow_->submitCommand("step");
    session.debuggerSlice();
    if (session.debuggerTimer_.isActive() || session.debuggerHistory_.empty() ||
        session.debugger_->bp_count != 1 || session.debugger_->symbol_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    cupid_gb_write_u8(&session.emulator_->gb, 0x0000, 0x0a);
    cupid_gb_write_u8(&session.emulator_->gb, 0xa000, 0x6d);
    const QByteArray beforeInvalidCommand = session.captureState();
    session.debuggerWindow_->submitCommand("reset reload extra");
    session.debuggerWindow_->submitCommand("reset quick extra");
    if (session.captureState() != beforeInvalidCommand || session.debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    rom[0x151] = char(0x42);
    if (!write(path, rom) || !write(symbolsPath, "00:0152 NewEntry\n"))
        return smokeFailure(__FILE__, __LINE__);
    // Reload retains the active hardware even when the next Reset preference changes.
    session.settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    session.debuggerWindow_->submitCommand("reset reload");
    uint16_t symbolBank = 0;
    uint16_t symbolAddress = 0;
    if (!session.paused_ || session.debuggerTimer_.isActive() ||
        session.emulator_->gb.cpu.pc != 0x100 ||
        session.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC || session.romData_ != rom ||
        session.emulator_->gb.cartridge_ram[0] != 0x6d || !session.debuggerHistory_.empty() ||
        session.rewindStates_.size() > 1 || session.debugger_->bp_count != 1 ||
        session.debugger_->breakpoints[0].address != 0x152 ||
        !cupid_gb_debugger_lookup_symbol(session.debugger_.get(), "NewEntry", &symbolBank,
                                         &symbolAddress) ||
        symbolAddress != 0x152 ||
        cupid_gb_debugger_lookup_symbol(session.debugger_.get(), "OldEntry", &symbolBank,
                                        &symbolAddress))
        return smokeFailure(__FILE__, __LINE__);
    // The reload action refreshes symbols even when the cartridge bytes did not change.
    if (!write(symbolsPath, "00:0150 UpdatedEntry\n"))
        return smokeFailure(__FILE__, __LINE__);
    session.reloadAction_->trigger();
    if (!session.paused_ ||
        !cupid_gb_debugger_lookup_symbol(session.debugger_.get(), "UpdatedEntry", &symbolBank,
                                         &symbolAddress) ||
        symbolAddress != 0x150 || session.debugger_->symbol_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(false);
    if (!QFile::remove(symbolsPath))
        return smokeFailure(__FILE__, __LINE__);
    session.reloadAction_->trigger();
    if (session.paused_ || session.debugger_->symbol_count || session.debugger_->bp_count != 1 ||
        session.emulator_->gb.cpu.pc != 0x100)
        return smokeFailure(__FILE__, __LINE__);
    session.pauseDebugger();
    session.debuggerWindow_->submitCommand("step");
    session.debuggerSlice();
    const QByteArray beforeFailure = session.captureState();
    const auto historySize = session.debuggerHistory_.size();
    if (!historySize || !write(symbolsPath, "invalid symbols\n") || session.reloadRom() ||
        session.captureState() != beforeFailure || session.debuggerHistory_.size() != historySize ||
        !session.paused_ || session.debugger_->bp_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    if (!write(symbolsPath, "00:0150 ValidEntry\n") || !write(path, QByteArray(8, char(0))) ||
        session.reloadRom() || session.captureState() != beforeFailure ||
        session.debuggerHistory_.size() != historySize || session.debugger_->symbol_count)
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    if (!QFile::remove(path) || session.reloadRom() || session.captureState() != beforeFailure ||
        session.debuggerHistory_.size() != historySize || !session.paused_)
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    if (!session.closeRom() || session.reloadAction_->isEnabled() || session.reloadRom())
        return smokeFailure(__FILE__, __LINE__);
    // File reload must also work before a debugger has ever been created.
    MainWindow plain(true, true, false, QStringLiteral(""), int(CUPID_GB_MODEL_DMG_ABC));
    plain.timer_.stop();
    if (!write(path, rom) || !plain.openRom(path) || plain.debugger_)
        return smokeFailure(__FILE__, __LINE__);
    rom[0x151] = char(0x23);
    if (!write(path, rom))
        return smokeFailure(__FILE__, __LINE__);
    plain.reloadAction_->trigger();
    if (plain.romData_ != rom || plain.paused_ || plain.debugger_ ||
        plain.emulator_->gb.cartridge_ram[0] != 0x6d || !plain.closeRom())
        return smokeFailure(__FILE__, __LINE__);
    return true;
}

/**
 * @file    main_window_watchpoint_tests.cpp
 * @brief   Runtime watchpoint commands, guest access stops, and session lifecycle checks.
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

bool MainWindow::watchpointSmokeTest(const QString &directory) {
    QByteArray rom(32768, char(0));
    const std::array<uint8_t, 3> entry = {0xc3, 0x50, 0x01};
    const std::array<uint8_t, 17> program = {0x3e, 0x12, 0xea, 0x00, 0xc0, 0xfa, 0x00, 0xc0, 0x3c,
                                             0xea, 0x01, 0xc0, 0xcd, 0x70, 0x01, 0x18, 0xfe};
    const std::array<uint8_t, 6> routine = {0x3e, 0x44, 0xea, 0x02, 0xc0, 0xc9};
    const std::array<uint8_t, 3> bankWrite = {0xea, 0x00, 0xd0};
    std::memcpy(rom.data() + 0x100, entry.data(), entry.size());
    std::memcpy(rom.data() + 0x150, program.data(), program.size());
    std::memcpy(rom.data() + 0x170, routine.data(), routine.size());
    std::memcpy(rom.data() + 0x180, bankWrite.data(), bankWrite.size());
    rom.replace(0x134, 10, QByteArray("WATCH TEST", 10));
    rom[0x143] = char(0x80);
    uint8_t checksum = 0;
    for (int index = 0x134; index <= 0x14c; ++index)
        checksum = uint8_t(checksum - uint8_t(rom[index]) - 1);
    rom[0x14d] = char(checksum);
    const QString path = QDir(directory).filePath("watchpoints.gbc");
    QFile cartridge(path);
    if (!cartridge.open(QIODevice::WriteOnly) || cartridge.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    cartridge.close();

    MainWindow session(true, true, false, QStringLiteral(""), int(CUPID_GB_MODEL_CGB), uint64_t(0));
    session.timer_.stop();
    if (!session.openRom(path))
        return smokeFailure(__FILE__, __LINE__);
    session.openDebugger();
    const auto command = [&session](const QString &text) {
        session.debuggerWindow_->submitCommand(text);
        session.timer_.stop();
    };
    const auto complete = [&session] {
        QElapsedTimer deadline;
        deadline.start();
        while (session.debuggerTimer_.isActive() && deadline.elapsed() < 5000)
            session.debuggerSlice();
        return !session.debuggerTimer_.isActive();
    };
    const auto hit = [&session](uint32_t id, uint16_t address, uint8_t access) {
        const auto &debugger = *session.debugger_;
        return session.paused_ && debugger.last_stop == CUPID_GB_DEBUG_STOP_WATCHPOINT &&
               debugger.last_watch_hit.id == id && debugger.last_watch_hit.address == address &&
               debugger.last_watch_hit.access == access;
    };
    command("write $ffff 0");
    command("write $c000 $11");
    command("set pc $0152");
    command("set a $12");
    command("watch $c000 if (old == $11) && (new == $12)");
    if (session.debugger_->wp_count != 1 ||
        session.debugger_->watchpoints[0].access != CUPID_GB_DEBUG_ACCESS_WRITE)
        return smokeFailure(__FILE__, __LINE__);
    const uint32_t firstId = session.debugger_->watchpoints[0].id;
    if (!session.saveSlot(7))
        return smokeFailure(__FILE__, __LINE__);

    // Inspection and host edits do not enter a guest instruction's watch scope.
    const QByteArray beforeInspection = session.captureState();
    command("print [$c000]");
    command("x $c000 2");
    if (session.captureState() != beforeInspection || session.debugger_->last_watch_hit.id ||
        !session.debuggerWrite(0xc000, 0x11) || session.debugger_->last_watch_hit.id)
        return smokeFailure(__FILE__, __LINE__);
    command("set a $13");
    command("step");
    if (!complete() || session.emulator_->gb.cpu.pc != 0x155 ||
        cupid_gb_debug_peek(&session.emulator_->gb, 0xc000) != 0x13 ||
        session.debugger_->last_watch_hit.id)
        return smokeFailure(__FILE__, __LINE__);
    if (!session.loadSlot(7))
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray beforeWrite = hardwareSnapshotForTest(session.emulator_->gb);
    command("step");
    if (!complete() || !hit(firstId, 0xc000, CUPID_GB_DEBUG_ACCESS_WRITE) ||
        session.emulator_->gb.cpu.pc != 0x155 ||
        cupid_gb_debug_peek(&session.emulator_->gb, 0xc000) != 0x12 ||
        session.debugger_->last_watch_hit.new_value != 0x12 ||
        session.debuggerHistory_.size() != 1 || !session.debuggerBackstep() ||
        !matchesHardwareForTest(session.emulator_->gb, beforeWrite) ||
        session.debugger_->watchpoints[0].id != firstId)
        return smokeFailure(__FILE__, __LINE__);

    // Continue uses normal gameplay execution and stops after the actual store.
    command("continue");
    if (session.paused_ || !session.runFrame() ||
        !hit(firstId, 0xc000, CUPID_GB_DEBUG_ACCESS_WRITE) ||
        session.emulator_->gb.cpu.pc != 0x155 || !session.debuggerHistory_.empty())
        return smokeFailure(__FILE__, __LINE__);
    command("unwatch all");
    auto *address = session.debuggerWindow_->findChild<QLineEdit *>("debuggerWatchpointAddress");
    auto *mode = session.debuggerWindow_->findChild<QComboBox *>("debuggerWatchpointMode");
    auto *add = session.debuggerWindow_->findChild<QPushButton *>("debuggerAddWatchpoint");
    if (!address || !mode || !add)
        return smokeFailure(__FILE__, __LINE__);
    address->setText("$c000 if (old == 0) && (new == 0)");
    mode->setCurrentIndex(mode->findData("r"));
    add->click();
    if (session.debugger_->wp_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    const uint32_t readId = session.debugger_->watchpoints[0].id;
    command("step");
    if (!complete() || readId != firstId || !hit(readId, 0xc000, CUPID_GB_DEBUG_ACCESS_READ) ||
        session.emulator_->gb.cpu.pc != 0x158 || session.emulator_->gb.cpu.a != 0x12 ||
        session.debugger_->last_watch_hit.old_value || session.debugger_->last_watch_hit.new_value)
        return smokeFailure(__FILE__, __LINE__);

    command("unwatch");
    command("watch/w $c000 to $c001");
    command("set pc $0159");
    command("step");
    if (!complete() || session.emulator_->gb.cpu.pc != 0x15c ||
        session.debugger_->last_watch_hit.id)
        return smokeFailure(__FILE__, __LINE__);
    const uint32_t exclusiveId = session.debugger_->watchpoints[0].id;
    command("watch/w $c000 to $c001 inclusive");
    if (session.debugger_->wp_count != 2)
        return smokeFailure(__FILE__, __LINE__);
    const uint32_t inclusiveId = session.debugger_->watchpoints[1].id;
    command("set pc $0159");
    command("step");
    if (!complete() || !hit(inclusiveId, 0xc001, CUPID_GB_DEBUG_ACCESS_WRITE))
        return smokeFailure(__FILE__, __LINE__);
    command(QString("unwatch %1").arg(exclusiveId));
    if (session.debugger_->wp_count != 1 || session.debugger_->watchpoints[0].id != inclusiveId)
        return smokeFailure(__FILE__, __LINE__);
    for (const QString &invalid :
         {QString("unwatch 0"), QString("unwatch abc"), QString("watch/w $c001 to $c000"),
          QString("watch/w $c000 to $c000 inclusive"), QString("watch/w $01:$d000 to $02:$d010"),
          QString("watch/x $c000"), QString("watch/w $c000 if new = 4")}) {
        command(invalid);
        if (session.debugger_->wp_count != 1 || session.debugger_->watchpoints[0].id != inclusiveId)
            return smokeFailure(__FILE__, __LINE__);
    }

    // The start expression owns bank selection. An end-only qualifier does not
    // constrain an otherwise unqualified range, while two explicit banks must agree.
    command("watch/r $d000 to $02:$d010 inclusive");
    if (session.debugger_->wp_count != 2 || session.debugger_->watchpoints[1].bank != -1 ||
        session.debugger_->watchpoints[1].end != 0xd010)
        return smokeFailure(__FILE__, __LINE__);
    command("unwatch all");
    command("watch/w $02:$d000");
    if (session.debugger_->wp_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    const uint32_t bankId = session.debugger_->watchpoints[0].id;
    command("write $ff70 1");
    command("set pc $0180");
    command("step");
    if (!complete() || session.debugger_->last_watch_hit.id)
        return smokeFailure(__FILE__, __LINE__);
    command("write $ff70 2");
    command("set pc $0180");
    command("step");
    if (!complete() || !hit(bankId, 0xd000, CUPID_GB_DEBUG_ACCESS_WRITE) ||
        session.debugger_->last_watch_hit.bank != 2 || session.emulator_->gb.cpu.pc != 0x183)
        return smokeFailure(__FILE__, __LINE__);

    // A condition that loses a symbol still reports the completed access and
    // retains its history, unlike an execution breakpoint checked before a step.
    const QString symbolsPath = QDir(directory).filePath("watchpoints.sym");
    QFile symbols(symbolsPath);
    const QByteArray symbolText("00:0012 Expected\n");
    if (!symbols.open(QIODevice::WriteOnly) || symbols.write(symbolText) != symbolText.size())
        return smokeFailure(__FILE__, __LINE__);
    symbols.close();
    if (!session.loadDebuggerSymbols(symbolsPath))
        return smokeFailure(__FILE__, __LINE__);
    command("unwatch all");
    command("watch/w $c000 if new == Expected");
    if (session.debugger_->wp_count != 1 || !symbols.open(QIODevice::WriteOnly))
        return smokeFailure(__FILE__, __LINE__);
    symbols.close();
    if (!session.loadDebuggerSymbols(symbolsPath))
        return smokeFailure(__FILE__, __LINE__);
    command("set pc $0152");
    command("set a $12");
    const QByteArray beforeConditionError = hardwareSnapshotForTest(session.emulator_->gb);
    command("step");
    if (!complete() || session.debugger_->last_stop != CUPID_GB_DEBUG_STOP_ERROR ||
        session.emulator_->gb.cpu.pc != 0x155 || session.debuggerHistory_.size() != 1 ||
        !session.debuggerWindow_->outputText().contains("condition evaluation failed at") ||
        !session.debuggerBackstep() ||
        !matchesHardwareForTest(session.emulator_->gb, beforeConditionError))
        return smokeFailure(__FILE__, __LINE__);

    // Watches interrupt Next and Finish without discarding their triggering step.
    command("unwatch all");
    command("watch/w $c002 if new == $44");
    if (session.debugger_->wp_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    const uint32_t callId = session.debugger_->watchpoints[0].id;
    command("set pc $015c");
    command("set sp $fffe");
    command("next");
    if (!complete() || !hit(callId, 0xc002, CUPID_GB_DEBUG_ACCESS_WRITE) ||
        session.emulator_->gb.cpu.pc != 0x175 || !session.debuggerBackstep() ||
        session.emulator_->gb.cpu.pc != 0x172)
        return smokeFailure(__FILE__, __LINE__);
    command("finish");
    if (!complete() || !hit(callId, 0xc002, CUPID_GB_DEBUG_ACCESS_WRITE) ||
        session.emulator_->gb.cpu.pc != 0x175)
        return smokeFailure(__FILE__, __LINE__);
    command("unwatch all");
    command("finish");
    if (!complete() || session.emulator_->gb.cpu.pc != 0x15f)
        return smokeFailure(__FILE__, __LINE__);

    command("watch/rw $c000 to $c003");
    if (session.debugger_->wp_count != 1)
        return smokeFailure(__FILE__, __LINE__);
    const uint32_t retainedId = session.debugger_->watchpoints[0].id;
    const auto retained = [&session, retainedId] {
        return session.debugger_->wp_count == 1 &&
               session.debugger_->watchpoints[0].id == retainedId &&
               session.debugger_->last_stop == CUPID_GB_DEBUG_STOP_NONE &&
               session.debuggerHistory_.empty();
    };
    if (!session.loadSlot(7) || !retained() || !session.restart() || !retained() ||
        !session.quickReset() || !retained())
        return smokeFailure(__FILE__, __LINE__);
    command("set pc $0152");
    command("set a $12");
    command("step");
    if (!complete() || !hit(retainedId, 0xc000, CUPID_GB_DEBUG_ACCESS_WRITE))
        return smokeFailure(__FILE__, __LINE__);
    command("watchpoints");
    if (!session.debuggerWindow_->outputText().contains("1 watchpoints configured.") ||
        !session.debuggerWatchpointListing().contains("[rw]"))
        return smokeFailure(__FILE__, __LINE__);

    const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QDir captures(captureDirectory);
        if (!captures.mkpath("."))
            return smokeFailure(__FILE__, __LINE__);
        session.debuggerWindow_->show();
        session.debuggerWindow_->raise();
        QApplication::processEvents();
        if (!session.debuggerWindow_->grab().save(captures.filePath("watchpoints.png")))
            return smokeFailure(__FILE__, __LINE__);
    }

    // A new cartridge starts a new debugger session; old accesses cannot leak in.
    const QString replacementPath = QDir(directory).filePath("watchpoints-replacement.gbc");
    rom[0x151] = char(0x45);
    QFile replacement(replacementPath);
    if (!replacement.open(QIODevice::WriteOnly) || replacement.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    replacement.close();
    if (!session.openRom(replacementPath) || session.debugger_->wp_count ||
        session.debugger_->last_watch_hit.id)
        return smokeFailure(__FILE__, __LINE__);
    return !session.failed_;
}

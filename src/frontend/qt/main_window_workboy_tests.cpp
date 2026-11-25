/**
 * @file    main_window_workboy_tests.cpp
 * @brief   Qt serial keyboard input, clock, accessory, and cartridge lifecycle regressions.
 * @author  cupidthecat
 * @date    2026-11-24
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

#include <QScopeGuard>
#include <cstdlib>
#include <ctime>

extern "C" {
#include "cupid/gb/timer.h"
#include "cupid/gb/workboy.h"
}

bool MainWindow::workBoySmokeTest(const QString &directory) {
    QTemporaryDir fixtures(QDir(directory).filePath("workboy-XXXXXX"));
    if (!fixtures.isValid())
        return smokeFailure(__FILE__, __LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    uint8_t checksum = 0;
    for (int address = 0x134; address <= 0x14c; ++address)
        checksum = uint8_t(checksum - uint8_t(rom[address]) - 1);
    rom[0x14d] = char(checksum);
    const QString first = fixtures.filePath("first.gb");
    const QString second = fixtures.filePath("second.gb");
    const QString peerPath = fixtures.filePath("peer.gb");
    for (const auto &path : {first, second, peerPath}) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
            return smokeFailure(__FILE__, __LINE__);
    }
    MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_CGB, 0);
    MainWindow peer(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_CGB, 0);
    session.timer_.stop();
    peer.timer_.stop();
    const auto previousClock = session.settings_.value("workboy/clockOffsetSeconds");
    const auto restoreClock = qScopeGuard([&] {
        if (previousClock.isValid())
            session.settings_.setValue("workboy/clockOffsetSeconds", previousClock);
        else
            session.settings_.remove("workboy/clockOffsetSeconds");
        session.settings_.sync();
    });
    if (session.workBoyAction_->isEnabled() || !session.openRom(first) || !peer.openRom(peerPath) ||
        !session.workBoyAction_->isEnabled())
        return smokeFailure(__FILE__, __LINE__);
    session.workBoyAction_->trigger();
    if (!session.workBoy_ || !session.workBoyAction_->isChecked() ||
        cupid_gb_workboy_enabled(session.workBoy_))
        return smokeFailure(__FILE__, __LINE__);
    auto *device = session.workBoy_;
    const auto send = [&session](uint8_t value) {
        auto &gb = session.emulator_->gb;
        cupid_gb_write_u8(&gb, 0xff01, value);
        cupid_gb_write_u8(&gb, 0xff02, 0x81);
        cupid_gb_tick(&gb, 1024);
        return cupid_gb_read_u8(&gb, 0xff01);
    };
    const auto exchange = [&send](const QByteArray &values) {
        QByteArray result;
        for (const unsigned char byte : values)
            result.append(char(send(byte)));
        return result;
    };
    const auto key = [&session](int value, bool pressed, const QString &text = QString(),
                                Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                                bool repeat = false) {
        QKeyEvent event(pressed ? QEvent::KeyPress : QEvent::KeyRelease, value, modifiers, text,
                        repeat);
        QCoreApplication::sendEvent(session.display_, &event);
    };
    // Attachment alone leaves normal controls available until the guest polls.
    key(session.keys_[4], true);
    if (session.emulator_->gb.joypad & 0x10)
        return smokeFailure(__FILE__, __LINE__);
    if (exchange(QByteArray("O\0\0", 3)) != QByteArray::fromHex("0000ff") ||
        !cupid_gb_workboy_enabled(device))
        return smokeFailure(__FILE__, __LINE__);
    // A control pressed before activation still owns its release afterward.
    key(session.keys_[4], false);
    if (session.emulator_->gb.joypad != 0xff)
        return smokeFailure(__FILE__, __LINE__);

    const auto checkKey = [&](int value, const QString &text, const QByteArray &expected,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        key(value, true, text, modifiers);
        const auto result = exchange(QByteArray(4, char(0)));
        key(value, false, text, modifiers);
        return result == expected && session.emulator_->gb.joypad == 0xff;
    };
    if (!checkKey(Qt::Key_A, "a", QByteArray::fromHex("ff1cffff")) ||
        !checkKey(Qt::Key_4, "4", QByteArray::fromHex("ff271cff")) ||
        !checkKey(Qt::Key_Dollar, "$", QByteArray::fromHex("ff321bff")) ||
        !checkKey(Qt::Key_F3, {}, QByteArray::fromHex("ff03ffff")) ||
        !checkKey(Qt::Key_Left, {}, QByteArray::fromHex("ff0dffff")) ||
        !checkKey(Qt::Key_Asterisk, "*", QByteArray::fromHex("ff21ffff"), Qt::KeypadModifier))
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_F3, Qt::NoModifier);
    shortcut.ignore();
    QCoreApplication::sendEvent(session.display_, &shortcut);
    QKeyEvent command(QEvent::ShortcutOverride, Qt::Key_A, Qt::ControlModifier, "a");
    command.ignore();
    if (!shortcut.isAccepted() || session.handleWorkBoyKey(&command, true, true))
        return smokeFailure(__FILE__, __LINE__);
    key(Qt::Key_A, true, "a");
    key(Qt::Key_F12, false);
    if (exchange(QByteArray(3, char(0))) != QByteArray::fromHex("ffffff"))
        return smokeFailure(__FILE__, __LINE__);
    key(Qt::Key_A, true, "a", Qt::NoModifier, true);
    if (exchange(QByteArray(3, char(0))) != QByteArray::fromHex("ffffff"))
        return smokeFailure(__FILE__, __LINE__);
    key(Qt::Key_Shift, true, {}, Qt::ShiftModifier);
    if (exchange(QByteArray(3, char(0))) != QByteArray::fromHex("ff27ff"))
        return smokeFailure(__FILE__, __LINE__);
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&session, &deactivate);
    if (session.workBoyShiftHeld_ ||
        exchange(QByteArray(3, char(0))) != QByteArray::fromHex("ff32ff"))
        return smokeFailure(__FILE__, __LINE__);

    // The accessory clock has its own persistent offset and accepts calendar writes.
    session.writeWorkBoyClock(1704164645);
    if (std::abs(session.readWorkBoyClock() - 1704164645) > 1 ||
        std::abs(peer.readWorkBoyClock() - 1704164645) > 1)
        return smokeFailure(__FILE__, __LINE__);
    QByteArray calendar(21, char(0));
    calendar[7] = 5;
    calendar[8] = 4;
    calendar[9] = 3;
    calendar[10] = 2;
    calendar[11] = 1;
    calendar[20] = 124;
    const auto calendarTime = [](int year) {
        std::tm local{};
        local.tm_year = year - 1900;
        local.tm_mday = 2;
        local.tm_hour = 3;
        local.tm_min = 4;
        local.tm_sec = 5;
        local.tm_isdst = -1;
        return int64_t(std::mktime(&local));
    };
    const auto target = calendarTime(2024);
    exchange(QByteArray("W\0\0", 3) + calendar);
    if (std::abs(session.readWorkBoyClock() - target) > 1)
        return smokeFailure(__FILE__, __LINE__);
    exchange(QByteArray(3, char(0)));

    calendar[20] = 125;
    exchange(QByteArray("W\0\0", 3) + calendar.first(20));
    cupid_gb_write_u8(&session.emulator_->gb, 0xff01, uint8_t(calendar.back()));
    cupid_gb_write_u8(&session.emulator_->gb, 0xff02, 0x81);
    const auto beforeClockCommit = session.captureState();
    const auto clockSetting = session.settings_.value("workboy/clockOffsetSeconds");
    if (beforeClockCommit.isEmpty() || !session.runFrame(Playback::Rewind) ||
        session.settings_.value("workboy/clockOffsetSeconds") != clockSetting ||
        !session.restoreState(beforeClockCommit, false) || !session.runFrame() ||
        session.settings_.value("workboy/clockOffsetSeconds") == clockSetting)
        return smokeFailure(__FILE__, __LINE__);
    const auto nextYear = calendarTime(2025);
    if (std::abs(session.readWorkBoyClock() - nextYear) > 1)
        return smokeFailure(__FILE__, __LINE__);
    session.writeWorkBoyClock(target);
    exchange(QByteArray(3, char(0)));

    key(Qt::Key_A, true, "a");
    const auto pending = session.captureState();
    if (pending.isEmpty() || exchange(QByteArray(3, char(0))) != QByteArray::fromHex("ff1cff") ||
        !session.restoreState(pending, false) || session.workBoy_ != device ||
        exchange(QByteArray(3, char(0))) != QByteArray::fromHex("ff1cff"))
        return smokeFailure(__FILE__, __LINE__);
    key(Qt::Key_A, true, "a");
    if (!session.openRom(second, false, ResetWorkflow::HotSwap) || session.workBoy_ != device ||
        !session.workBoyAction_->isChecked() || !cupid_gb_workboy_enabled(device) ||
        exchange(QByteArray(3, char(0))) != QByteArray::fromHex("ff1cff"))
        return smokeFailure(__FILE__, __LINE__);
    const auto beforeFailure = session.captureState();
    const auto broken = fixtures.filePath("missing.gb");
    if (session.openRom(broken, false, ResetWorkflow::HotSwap) ||
        session.captureState() != beforeFailure || session.workBoy_ != device)
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    if (!session.reloadRom() || session.workBoy_ != device || cupid_gb_workboy_enabled(device) ||
        exchange(QByteArray("O\0\0", 3)) != QByteArray::fromHex("0000ff") ||
        !session.quickReset() || session.workBoy_ != device || cupid_gb_workboy_enabled(device))
        return smokeFailure(__FILE__, __LINE__);
    {
        auto color = rom;
        color[0x143] = char(0x80);
        color[0x14d] = char(uint8_t(checksum - 0x80));
        const QString colorPath = fixtures.filePath("color.gbc");
        QFile file(colorPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(color) != color.size())
            return smokeFailure(__FILE__, __LINE__);
        file.close();
        MainWindow automatic(true, true, false, QStringLiteral(""), -1, 0);
        automatic.timer_.stop();
        if (!automatic.openRom(first) || automatic.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC)
            return smokeFailure(__FILE__, __LINE__);
        automatic.workBoyAction_->trigger();
        auto *retained = automatic.workBoy_;
        cupid_gb_write_u8(&automatic.emulator_->gb, 0xff01, 'O');
        cupid_gb_write_u8(&automatic.emulator_->gb, 0xff02, 0x81);
        cupid_gb_tick(&automatic.emulator_->gb, 1024);
        if (!cupid_gb_workboy_enabled(retained) ||
            !automatic.openRom(colorPath, false, ResetWorkflow::HotSwap) ||
            automatic.workBoy_ != retained || cupid_gb_workboy_enabled(retained) ||
            automatic.emulator_->gb.model != CUPID_GB_MODEL_CGB ||
            automatic.emulator_->gb.elapsed_cycles)
            return smokeFailure(__FILE__, __LINE__);
    }

    session.printerAction_->trigger();
    if (session.workBoy_ || !session.printer_ || !session.printerAction_->isChecked())
        return smokeFailure(__FILE__, __LINE__);
    session.workBoyAction_->trigger();
    peer.workBoyAction_->trigger();
    if (!session.workBoy_ || session.printer_ || !peer.workBoy_ || !session.connectLink(&peer) ||
        session.workBoy_ || peer.workBoy_ || !session.linkSession_)
        return smokeFailure(__FILE__, __LINE__);
    session.workBoyAction_->trigger();
    if (!session.workBoy_ || session.linkSession_ || peer.linkSession_ ||
        !peer.disconnectAction_->isChecked())
        return smokeFailure(__FILE__, __LINE__);
    session.disconnectAction_->trigger();
    if (session.workBoy_ || !session.disconnectAction_->isChecked() || !session.openRom(first) ||
        !session.workBoyAction_->isEnabled())
        return smokeFailure(__FILE__, __LINE__);
    session.workBoyAction_->trigger();
    if (!session.closeRom() || session.workBoy_ || session.workBoyAction_->isEnabled() ||
        std::abs(session.readWorkBoyClock() - target) > 1)
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB serial keyboard input, clock, and cartridge workflows passed");
    return true;
}

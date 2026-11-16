/**
 * @file    main_window_printer_tests.cpp
 * @brief   Qt printer workflow and timeline-restoration regression checks.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "main_window_internal.h"
#include "printer_window.h"

#include <QLabel>
#include <QPdfWriter>

extern "C" {
#include "cupid/gb/printer.h"
#include "cupid/gb/timer.h"
}

bool MainWindow::printerSmokeTest(const QString &path, const QString &directory) {
    MainWindow session(true, true);
    if (session.connectionsMenu_->isEnabled() || !session.openRom(path))
        return false;
    session.setPaused(true);
    session.printerAction_->trigger();
    if (!session.printer_ || !session.printerAction_->isChecked())
        return false;
    auto &gb = session.emulator_->gb;
    const auto advance = [&session](uint64_t baseCycles) {
        auto &core = session.emulator_->gb;
        const uint64_t end = core.elapsed_cycles + baseCycles;
        while (core.elapsed_cycles < end) {
            // The timer entry point accepts 16-bit CPU machine-cycle counts.
            const uint64_t quantum = core.double_speed ? 2 : 4;
            const uint64_t remaining = (end - core.elapsed_cycles + quantum - 1) / quantum;
            cupid_gb_tick(&core, uint16_t(std::min<uint64_t>(remaining, UINT16_MAX)));
        }
    };
    const auto send = [&session, &advance](uint8_t value) {
        auto &core = session.emulator_->gb;
        cupid_gb_write_u8(&core, 0xff01, value);
        cupid_gb_write_u8(&core, 0xff02, 0x81);
        advance(512 * 8);
        return cupid_gb_read_u8(&core, 0xff01);
    };
    const auto packetBytes = [](uint8_t command, const QByteArray &payload) {
        QByteArray bytes = QByteArray::fromHex("8833");
        uint16_t checksum = 0;
        for (uint8_t byte :
             {command, uint8_t(0), uint8_t(payload.size()), uint8_t(payload.size() >> 8)}) {
            bytes.append(char(byte));
            checksum += byte;
        }
        for (unsigned char byte : payload) {
            bytes.append(char(byte));
            checksum += byte;
        }
        bytes.append(char(checksum));
        bytes.append(char(checksum >> 8));
        bytes.append(char(0));
        bytes.append(char(0));
        return bytes;
    };
    const auto packet = [&send, &packetBytes](uint8_t command, const QByteArray &payload,
                                              uint8_t *status = nullptr) {
        const QByteArray bytes = packetBytes(command, payload);
        uint8_t acknowledgment = 0;
        for (qsizetype i = 0; i < bytes.size(); ++i) {
            const uint8_t reply = send(uint8_t(bytes[i]));
            if (i == bytes.size() - 2)
                acknowledgment = reply;
            if (status && i == bytes.size() - 1)
                *status = reply;
        }
        return acknowledgment == 0x81;
    };
    const QString png = QDir(directory).filePath("printer-smoke.png");
    QByteArray tiles(640, char(0xff));
    if (!packet(1, {}) || !packet(4, tiles) || !packet(2, QByteArray::fromHex("0123e440")))
        return false;
    // Paper commits inside the serial callback; only widget work is deferred.
    if (session.printerWindow_->isVisible() || !session.printerWindow_->saveFeed(png) ||
        QImage(png).size() != QSize(160, 21))
        return false;
    QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
    if (!session.printerWindow_->isVisible() || !session.printerWindow_->saveFeed(png))
        return false;
    const QImage image(png);
    if (image.size() != QSize(160, 21) || image.pixelColor(0, 0) != Qt::white ||
        image.pixelColor(159, 17) != Qt::black || image.pixelColor(159, 18) != Qt::white)
        return false;
    QFile pdf(QDir(directory).filePath("printer-smoke.pdf"));
    {
        QPdfWriter printer(pdf.fileName());
        printer.setResolution(144);
        if (!session.printerWindow_->printFeed(printer, printer.resolution()))
            return false;
    }
    if (!pdf.open(QIODevice::ReadOnly) || !pdf.read(5).startsWith("%PDF-"))
        return false;
    const QByteArray strip(160 * 8, char(0xaa));
    if (!session.printerWindow_->append(reinterpret_cast<const uint8_t *>(strip.constData()), 8, 1,
                                        0))
        return false;
    session.printerWindow_->refresh(true);
    if (!session.printerWindow_->saveFeed(png) || QImage(png).size() != QSize(160, 30) ||
        QImage(png).pixelColor(0, 22) != QColor(0xaa, 0xaa, 0xaa))
        return false;
    // Reconnecting can discard hardware state, but already emitted paper remains.
    if (!packet(1, {}) || !packet(4, tiles) || !packet(2, QByteArray::fromHex("0100e440")))
        return false;
    session.disconnectAction_->trigger();
    session.printerAction_->trigger();
    QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
    if (!session.printerWindow_->saveFeed(png) || QImage(png).height() != 46)
        return false;
    session.printerWindow_->hide();
    if (!session.printerWindow_->append(reinterpret_cast<const uint8_t *>(strip.constData()), 8, 0,
                                        0))
        return false;
    session.printerWindow_->refresh(true);
    if (!session.printerWindow_->saveFeed(png) || QImage(png).height() != 8)
        return false;

    const auto paper = [&session, &png] {
        return session.printerWindow_->saveFeed(png) ? QImage(png) : QImage{};
    };
    const auto printing = [&session] {
        const auto labels = session.printerWindow_->findChildren<QLabel *>();
        return std::any_of(labels.begin(), labels.end(),
                           [](const QLabel *label) { return label->text() == "Printing…"; });
    };
    enum class Restore { Disk, Rewind, Backstep };
    const QString statePath = QDir(directory).filePath("printer-continuation.state");
    const auto writeState = [&statePath](const QByteArray &state) {
        QSaveFile file(statePath);
        return file.open(QIODevice::WriteOnly) && file.write(state) == state.size() &&
               file.commit();
    };
    const auto remember = [&session, &writeState](Restore mode) {
        if (mode == Restore::Disk) {
            const QByteArray state = session.captureState();
            return !state.isEmpty() && writeState(state);
        }
        if (mode == Restore::Rewind) {
            session.resetRewind();
            session.rememberFrame();
            return !session.rewindFailed_ && session.rewindStates_.size() == 1;
        }
        session.resetDebuggerTimeline();
        return session.rememberDebuggerStep();
    };
    const auto restore = [&session, &statePath](Restore mode) {
        if (mode == Restore::Disk)
            return session.loadStateFile(statePath);
        if (mode == Restore::Rewind)
            return session.rewindFrame();
        return session.debuggerBackstep();
    };
    CupidGbPrinter *const attached = session.printer_;
    const CupidGbSerialLink registration = gb.serial_link;
    const auto sameAttachment = [&] {
        return session.printer_ == attached && session.printerAction_->isChecked() &&
               gb.serial_link.launch == registration.launch &&
               gb.serial_link.sample == registration.sample &&
               gb.serial_link.user_data == registration.user_data &&
               gb.serial_link.device == registration.device;
    };
    for (const auto mode : {Restore::Disk, Restore::Rewind, Restore::Backstep}) {
        session.resetPrinter();
        const int previousHeight = paper().height();
        if (!packet(1, {}))
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray data = packetBytes(4, tiles);
        constexpr qsizetype partial = 91;
        for (qsizetype i = 0; i < partial; ++i)
            send(uint8_t(data[i]));
        cupid_gb_write_u8(&gb, 0xff01, uint8_t(data[partial]));
        cupid_gb_write_u8(&gb, 0xff02, 0x81);
        for (unsigned steps = 0; steps < 1024 && gb.serial_bits_remaining > 5; ++steps)
            cupid_gb_tick(&gb, 1);
        if (gb.serial_bits_remaining != 5 || !(cupid_gb_read_u8(&gb, 0xff02) & 0x80) ||
            !remember(mode))
            return smokeFailure(__FILE__, __LINE__);
        const auto finishData = [&] {
            advance(512 * 8);
            bool acknowledged = false;
            for (qsizetype i = partial + 1; i < data.size(); ++i) {
                const uint8_t reply = send(uint8_t(data[i]));
                if (i == data.size() - 2)
                    acknowledged = reply == 0x81;
            }
            return acknowledged;
        };
        if (!finishData() || !packet(2, QByteArray::fromHex("0100e440")))
            return smokeFailure(__FILE__, __LINE__);
        const QImage committed = paper();
        if (committed.height() != previousHeight + 16)
            return smokeFailure(__FILE__, __LINE__);
        if (mode == Restore::Disk) {
            QFile saved(statePath);
            if (!saved.open(QIODevice::ReadOnly))
                return smokeFailure(__FILE__, __LINE__);
            const QByteArray valid = saved.readAll();
            saved.close();
            QByteArray corrupt = valid;
            if (corrupt.isEmpty())
                return smokeFailure(__FILE__, __LINE__);
            corrupt[corrupt.size() - 1] = char(uint8_t(corrupt.back()) ^ 0x80);
            const QByteArray before = session.captureState();
            const uint64_t timeline = session.timelineId_;
            if (!writeState(corrupt) || session.loadStateFile(statePath) ||
                session.captureState() != before || session.timelineId_ != timeline ||
                !sameAttachment() || paper() != committed || !writeState(valid))
                return smokeFailure(__FILE__, __LINE__);
            session.failed_ = false;
        }
        // A queued presentation still owns paper emitted on the abandoned timeline.
        // Only the accessory's incomplete byte and packet return to the checkpoint.
        if (!restore(mode) || !sameAttachment() || !session.paused_ || paper() != committed)
            return smokeFailure(__FILE__, __LINE__);
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        if (paper() != committed || !finishData() || !packet(2, QByteArray::fromHex("0100e440")))
            return smokeFailure(__FILE__, __LINE__);
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        const QImage replayed = paper();
        if (replayed.height() != previousHeight + 32 ||
            replayed.copy(0, previousHeight, 160, 16) !=
                replayed.copy(0, previousHeight + 16, 160, 16))
            return smokeFailure(__FILE__, __LINE__);

        session.resetPrinter();
        if (!packet(1, {}) || !packet(4, tiles) || !packet(2, QByteArray::fromHex("0100e440")))
            return smokeFailure(__FILE__, __LINE__);
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        const uint32_t clock = cupid_gb_base_clock_hz(&gb);
        advance(clock / 3);
        session.updatePrinter();
        if (!printing() || !remember(mode))
            return smokeFailure(__FILE__, __LINE__);
        advance(uint64_t(clock) * 3);
        session.updatePrinter();
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        if (printing() || !packet(4, tiles) || !packet(2, QByteArray::fromHex("0100e440")))
            return smokeFailure(__FILE__, __LINE__);
        const QImage currentPaper = paper();
        if (!restore(mode) || !sameAttachment() || paper() != currentPaper)
            return smokeFailure(__FILE__, __LINE__);
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        uint8_t status = 0;
        if (!printing() || !packet(0x0f, {}, &status) || status != 0x06)
            return smokeFailure(__FILE__, __LINE__);
        advance(clock);
        session.updatePrinter();
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        if (!printing())
            return smokeFailure(__FILE__, __LINE__);
        advance(clock);
        session.updatePrinter();
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        if (printing() || !packet(0x0f, {}, &status) || status != 0x04 || paper() != currentPaper)
            return smokeFailure(__FILE__, __LINE__);
    }
    session.resetPrinter();
    session.printerWindow_->hide();
    if (!packet(1, {}) || !packet(4, tiles) || !packet(2, QByteArray::fromHex("0100e440")))
        return smokeFailure(__FILE__, __LINE__);
    const QImage closingPaper = paper();
    if (closingPaper.height() != 16 || session.printerWindow_->isVisible() || !session.closeRom() ||
        !session.openRom(path))
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
    if (session.printerWindow_->isVisible() || paper() != closingPaper || session.printer_)
        return smokeFailure(__FILE__, __LINE__);
    session.printerAction_->trigger();
    if (!packet(1, {}) || !packet(4, tiles) || !packet(2, QByteArray::fromHex("0100e440")))
        return smokeFailure(__FILE__, __LINE__);
    QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
    if (!session.printerWindow_->isVisible() || paper().height() != 16)
        return smokeFailure(__FILE__, __LINE__);
    session.disconnectAction_->trigger();
    return !session.printer_ && session.disconnectAction_->isChecked() && session.closeRom();
}

/**
 * @file    main_window_ram_tests.cpp
 * @brief   Qt workflows for large cartridge RAM, persistence and rewind.
 * @author  cupidthecat
 * @date    2026-11-17
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

#include "cheats_window.h"
#include "main_window_internal.h"

#include <QScopeGuard>
#include <QTableView>

bool MainWindow::largeCartridgeRamSmokeTest(const QString &directory) {
    const bool hadRewind = settings_.contains("emulation/rewindSeconds");
    const QVariant rewindSetting = settings_.value("emulation/rewindSeconds");
    const bool hadBudget = settings_.contains("emulation/rewindMiB");
    const QVariant budgetSetting = settings_.value("emulation/rewindMiB");
    const bool hadClock = settings_.contains("emulation/rtcMode");
    const QVariant clockSetting = settings_.value("emulation/rtcMode");
    const auto restoreSettings = qScopeGuard([&] {
        if (hadRewind)
            settings_.setValue("emulation/rewindSeconds", rewindSetting);
        else
            settings_.remove("emulation/rewindSeconds");
        if (hadBudget)
            settings_.setValue("emulation/rewindMiB", budgetSetting);
        else
            settings_.remove("emulation/rewindMiB");
        if (hadClock)
            settings_.setValue("emulation/rtcMode", clockSetting);
        else
            settings_.remove("emulation/rtcMode");
    });
    settings_.setValue("emulation/rewindSeconds", 120);
    settings_.setValue("emulation/rtcMode", int(CUPID_GB_RTC_ACCURATE));
    const QDir fixtures(QDir(directory).filePath("large-cartridge-ram"));
    if (!QDir().mkpath(fixtures.path()))
        return smokeFailure(__FILE__, __LINE__);
    const auto writeFile = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };

    {
        // Small battery cartridges retain the unused part of the native SRAM prefix.
        QByteArray rom(0x8000, char(0));
        rom[0x100] = char(0x18);
        rom[0x101] = char(0xfe);
        rom[0x147] = 9;
        rom[0x149] = 2;
        uint8_t checksum = 0;
        for (int address = 0x134; address < 0x14d; ++address)
            checksum = uint8_t(checksum - uint8_t(rom[address]) - 1);
        rom[0x14d] = char(checksum);
        const QString path = fixtures.filePath("inline-retention.gb");
        if (!writeFile(path, rom))
            return smokeFailure(__FILE__, __LINE__);
        MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        session.timer_.stop();
        if (!session.openRom(path))
            return smokeFailure(__FILE__, __LINE__);
        for (unsigned workflow = 0; workflow < 3; ++workflow) {
            auto &gb = session.emulator_->gb;
            if (gb.cartridge_ram_size != 0x2000 || gb.cartridge_ram_heap)
                return smokeFailure(__FILE__, __LINE__);
            const uint8_t first = uint8_t(0x61 + workflow), last = uint8_t(0x94 + workflow);
            gb.cartridge_ram[0x2000] = first;
            gb.cartridge_ram[sizeof(gb.cartridge_ram) - 1] = last;
            const bool reset = workflow == 0   ? session.restart()
                               : workflow == 1 ? session.quickReset()
                                               : session.reloadRom();
            const auto &retained = session.emulator_->gb;
            if (!reset || retained.cartridge_ram[0x2000] != first ||
                retained.cartridge_ram[sizeof(retained.cartridge_ram) - 1] != last)
                return smokeFailure(__FILE__, __LINE__);
            session.setPaused(true);
        }
        if (!session.closeRom())
            return smokeFailure(__FILE__, __LINE__);
    }

    struct Marker {
        uint8_t bank;
        uint16_t address;
        uint8_t value;
    };

    const std::array<Marker, 3> markers = {
        {{15, 0xbfff, 0x31}, {16, 0xa000, 0x62}, {255, 0xbfff, 0x93}}};
    for (const bool color : {false, true}) {
        QByteArray rom(0x8000, char(0));
        rom[0x100] = char(0xc3);
        rom[0x101] = char(0x60);
        rom[0x102] = char(0x01);
        rom[0x143] = color ? char(0x80) : 0;
        rom[0x147] = char(0xbc);
        rom[0x149] = char(0xc1);
        rom[0x14a] = char(0x65);
        rom[0x152] = 9;
        rom[0x153] = 8;
        uint8_t checksum = 0;
        for (int address = 0x134; address < 0x14d; ++address)
            checksum = uint8_t(checksum - uint8_t(rom[address]) - 1);
        rom[0x14d] = char(checksum);
        std::vector<uint8_t> program = {0xf3, 0x3e, 3, 0xea, 3, 0};
        for (const auto &marker : markers)
            program.insert(program.end(), {0x3e, marker.bank, 0xea, 2, 0, 0x3e, marker.value, 0xea,
                                           uint8_t(marker.address), uint8_t(marker.address >> 8)});
        program.insert(program.end(), {0x18, 0xfe});
        std::memcpy(rom.data() + 0x160, program.data(), program.size());
        // Explicit debugger stepping later writes the last SRAM byte through the guest bus.
        rom[0x200] = char(0xea);
        rom[0x201] = char(0xff);
        rom[0x202] = char(0xbf);
        const QString stem = color ? "tpp1-cgb" : "tpp1-dmg";
        const QString path = fixtures.filePath(stem + (color ? ".gbc" : ".gb"));
        if (!writeFile(path, rom))
            return smokeFailure(__FILE__, __LINE__);
        MainWindow session(true, true, false, QStringLiteral(""),
                           color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC, 0);
        session.timer_.stop();
        if (!session.openRom(path) || !session.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        session.setPaused(true);
        const auto ramBytes = [&session] {
            const auto &gb = session.emulator_->gb;
            return QByteArray(reinterpret_cast<const char *>(cupid_gb_cartridge_ram_const(&gb)),
                              qsizetype(gb.cartridge_ram_size));
        };
        const auto writeByte = [&session](uint8_t bank, uint16_t address, uint8_t value) {
            auto &gb = session.emulator_->gb;
            cupid_gb_write_u8(&gb, 3, 3);
            cupid_gb_write_u8(&gb, 2, bank);
            cupid_gb_write_u8(&gb, address, value);
        };
        QByteArray expected(0x200000, char(0xff));
        for (const auto &marker : markers)
            expected[qsizetype(marker.bank) * 0x2000 + (marker.address & 0x1fff)] =
                char(marker.value);
        if (ramBytes() != expected || !session.closeRom() || !session.openRom(path) ||
            ramBytes() != expected)
            return smokeFailure(__FILE__, __LINE__);
        session.setPaused(true);
        QFile battery(session.savePath_);
        if (!battery.open(QIODevice::ReadOnly) || battery.size() != expected.size() + 20 ||
            battery.read(expected.size()) != expected)
            return smokeFailure(__FILE__, __LINE__);
        battery.close();

        // Validate a different candidate before touching the current session or its save.
        const QString rejectedPath = fixtures.filePath(stem + "-invalid.gb");
        const QString rejectedSave = fixtures.filePath(stem + "-invalid.sav");
        if (!writeFile(rejectedPath, rom) ||
            !writeFile(rejectedSave, QByteArray(expected.size() + 21, char(0))))
            return smokeFailure(__FILE__, __LINE__);
        const auto *owner = session.emulator_.get();
        const QByteArray beforeRejection = session.captureState();
        if (beforeRejection.isEmpty() || session.openRom(rejectedPath) ||
            session.emulator_.get() != owner || session.captureState() != beforeRejection ||
            ramBytes() != expected)
            return smokeFailure(__FILE__, __LINE__);
        session.failed_ = false;
        if (!session.saveSlot(9))
            return smokeFailure(__FILE__, __LINE__);
        writeByte(255, 0xbfff, 0x44);
        if (ramBytes() == expected || !session.loadSlot(9) || ramBytes() != expected)
            return smokeFailure(__FILE__, __LINE__);

        for (unsigned workflow = 0; workflow < 5; ++workflow) {
            for (const auto &marker : markers)
                writeByte(marker.bank, marker.address, uint8_t(marker.value + workflow + 1));
            const QByteArray retained = ramBytes();
            const bool reset = workflow == 0   ? session.restart()
                               : workflow == 1 ? session.quickReset()
                               : workflow == 2
                                   ? session.reloadRom()
                                   : session.resetConsole(workflow == 3 ? CUPID_GB_RESET_HARD
                                                                        : CUPID_GB_RESET_QUICK);
            if (!reset || ramBytes() != retained || session.rewindFailed_)
                return smokeFailure(__FILE__, __LINE__);
            session.setPaused(true);
        }
        if (!session.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        session.resetRewind();
        session.rememberFrame();
        writeByte(255, 0xbfff, 0x44);
        if (!session.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray beforeRewind = ramBytes();
        writeByte(255, 0xbfff, 0x55);
        if (!session.runFrame() || ramBytes() == beforeRewind || !session.rewindFrame() ||
            ramBytes() != beforeRewind)
            return smokeFailure(__FILE__, __LINE__);

        if (color) {
            // One profile is enough for the compression budget: keep the full
            // SRAM incompressible while changing markers between ten captures.
            constexpr unsigned captures = 10;
            constexpr qsizetype budget = 16 * 1024 * 1024;
            session.settings_.setValue("emulation/rewindMiB", 16);
            session.resetRewind();
            auto &gb = session.emulator_->gb;
            auto *ram = cupid_gb_cartridge_ram(&gb);
            uint32_t random = 0x619ad325;
            for (size_t offset = 0; offset < gb.cartridge_ram_size; ++offset) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                ram[offset] = uint8_t(random);
            }
            const uint64_t guestCycles = gb.elapsed_cycles;
            const unsigned restoredIndex = captures - (session.turbo_ ? 3u : 2u);
            QByteArray expectedBudgetRam;
            qsizetype largestCompressed = 0;
            for (unsigned capture = 0; capture < captures; ++capture) {
                ram[0] = uint8_t(capture);
                ram[0x1ffff] = uint8_t(0x30 + capture);
                ram[0x20000] = uint8_t(0x60 + capture);
                ram[0x1fffff] = uint8_t(0x90 + capture);
                if (capture == restoredIndex)
                    expectedBudgetRam = ramBytes();
                session.rememberFrame();
                if (session.rewindFailed_ || session.rewindStates_.empty() ||
                    session.rewindSnapshotsCaptured_ != capture + 1 ||
                    session.rewindBytes_ > budget || gb.elapsed_cycles != guestCycles)
                    return smokeFailure(__FILE__, __LINE__);
                qsizetype accounted = 0;
                for (const auto &snapshot : session.rewindStates_) {
                    accounted += snapshot.compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
                    largestCompressed = std::max(largestCompressed, snapshot.compressed.size());
                }
                if (accounted != session.rewindBytes_)
                    return smokeFailure(__FILE__, __LINE__);
            }
            if (largestCompressed < qsizetype(gb.cartridge_ram_size * 7 / 8) ||
                session.rewindStates_.size() >= captures || session.rewindStates_.size() < 2 ||
                session.rewindStates_.front().cycles != session.rewindStates_.back().cycles ||
                expectedBudgetRam.isEmpty())
                return smokeFailure(__FILE__, __LINE__);
            qInfo("CupidGB large SRAM rewind: model=cgb captures=%llu retained=%llu "
                  "retained_bytes=%lld budget_bytes=%lld largest_compressed_bytes=%lld "
                  "capture_total_ns=%lld capture_max_ns=%lld",
                  qulonglong(session.rewindSnapshotsCaptured_),
                  qulonglong(session.rewindStates_.size()), qlonglong(session.rewindBytes_),
                  qlonglong(budget), qlonglong(largestCompressed),
                  qlonglong(session.rewindCaptureNanoseconds_),
                  qlonglong(session.rewindCaptureMaximumNanoseconds_));
            if (!session.rewindFrame() || session.rewindFailed_ ||
                ramBytes() != expectedBudgetRam || session.rewindBytes_ > budget)
                return smokeFailure(__FILE__, __LINE__);
            // The generated guest is already in its final loop, so replay cannot
            // rewrite the markers. Release these large snapshots before later UI checks.
            session.resetRewind();
        }

        session.ensureDebugger();
        session.debuggerWindow_->submitCommand("set pc $0200");
        session.debuggerWindow_->submitCommand("set a $c7");
        cupid_gb_write_u8(&session.emulator_->gb, 3, 3);
        cupid_gb_write_u8(&session.emulator_->gb, 2, 255);
        session.resetDebuggerTimeline();
        const QByteArray beforeStep = ramBytes();
        session.debuggerWindow_->submitCommand("step");
        for (unsigned attempt = 0; session.debuggerTimer_.isActive() && attempt < 4; ++attempt)
            session.debuggerSlice();
        if (session.debuggerTimer_.isActive() || session.debuggerHistory_.size() != 1 ||
            uint8_t(ramBytes().back()) != 0xc7 || !session.debuggerBackstep() ||
            ramBytes() != beforeStep)
            return smokeFailure(__FILE__, __LINE__);

        // Exercise the real search and freeze controls on both sides of the old limit.
        session.openCheats(true);
        auto *type = session.cheatsWindow_->findChild<QComboBox *>("cheatSearchType");
        auto *condition = session.cheatsWindow_->findChild<QComboBox *>("cheatSearchCondition");
        auto *operand = session.cheatsWindow_->findChild<QLineEdit *>("cheatSearchOperand");
        auto *filter = session.cheatsWindow_->findChild<QPushButton *>("filterCheatSearch");
        auto *results = session.cheatsWindow_->findChild<QTableView *>("cheatSearchResults");
        auto *add = session.cheatsWindow_->findChild<QPushButton *>("addSearchCheat");
        if (!type || !condition || !operand || !filter || !results || !add)
            return smokeFailure(__FILE__, __LINE__);
        for (const auto searchType : {CheatSearchType::WordLittleEndian,
                                      CheatSearchType::WordBigEndian, CheatSearchType::Byte}) {
            if (!session.mutateCheats([](CupidGbCheats *cheats) {
                    cupid_gb_cheats_clear(cheats);
                    return true;
                }))
                return smokeFailure(__FILE__, __LINE__);
            auto &gb = session.emulator_->gb;
            auto *ram = cupid_gb_cartridge_ram(&gb);
            std::memset(ram, 0, gb.cartridge_ram_size);
            std::memset(gb.work_ram, 0, sizeof(gb.work_ram));
            std::memset(gb.high_ram, 0, sizeof(gb.high_ram));
            const bool word = searchType != CheatSearchType::Byte;
            const bool bigEndian = searchType == CheatSearchType::WordBigEndian;
            const size_t offset = word ? 0x1ffff : 0x1fffff;
            ram[offset] = word ? (bigEndian ? 0x12 : 0x34) : 0x77;
            if (word)
                ram[offset + 1] = bigEndian ? 0x34 : 0x12;
            session.refreshCheats(true);
            type->setCurrentIndex(int(searchType));
            condition->setCurrentIndex(int(CheatSearchCondition::Equal));
            operand->setText(word ? "1234" : "77");
            filter->click();
            if (results->model()->rowCount() != 1)
                return smokeFailure(__FILE__, __LINE__);
            results->selectRow(0);
            CheatSearch edits;
            edits.filter(gb, searchType, CheatSearchCondition::Equal, word ? 0x1234 : 0x77);
            const uint64_t generation = gb.battery_write_generation;
            if (edits.count() != 1 || !session.writeSearchMemory([&](CupidGb &state) {
                    return edits.writeValue(state, 0, word ? 0x5678 : 0x9a);
                }) ||
                gb.battery_write_generation != generation + 1)
                return smokeFailure(__FILE__, __LINE__);
            add->click();
            if (cupid_gb_cheats_count(session.cheats_.get()) != (word ? 2u : 1u))
                return smokeFailure(__FILE__, __LINE__);
            for (unsigned byte = 0; byte < (word ? 2u : 1u); ++byte) {
                const size_t physical = offset + byte;
                const uint8_t value = ram[physical];
                const auto *record = cupid_gb_cheats_get(session.cheats_.get(), byte);
                if (!record || record->bank != physical / 0x2000 ||
                    record->address != 0xa000 + (physical & 0x1fff) || record->value != value)
                    return smokeFailure(__FILE__, __LINE__);
                ram[physical] = 0;
                cupid_gb_write_u8(&gb, 3, 3);
                cupid_gb_write_u8(&gb, 2, uint8_t(record->bank));
                if (cupid_gb_read_u8(&gb, record->address) != value || ram[physical] != 0)
                    return smokeFailure(__FILE__, __LINE__);
            }
        }
        if (session.failed_ || !session.closeRom())
            return smokeFailure(__FILE__, __LINE__);
    }
    return true;
}

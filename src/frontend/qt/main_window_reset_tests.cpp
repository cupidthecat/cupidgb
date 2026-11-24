/**
 * @file    main_window_reset_tests.cpp
 * @brief   Qt reset workflows, retained recordings, firmware entry, and transactional failures.
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

#include <QScopeGuard>

extern "C" {
#include "../../gb/audio_state.h"
#include "../../gb/printer_internal.h"
#include "cupid/gb/timer.h"
}

namespace {
void selectResetTestWindow(QWidget *window) {
    // Offscreen fixtures have no window-manager activation. Select Qt's active
    // widget synchronously so shortcut delivery and restoration stay deterministic.
    // Keep this deprecated test-only entry point inside its narrow warning scope.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(window);
    QT_WARNING_POP
}

bool writeResetFixture(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray resetPhysicalMemory(const CupidGb &gb) {
    QByteArray result;
    const auto append = [&](const uint8_t *bytes, size_t count) {
        result.append(reinterpret_cast<const char *>(bytes), qsizetype(count));
    };
    append(gb.work_ram, sizeof(gb.work_ram));
    append(gb.high_ram, sizeof(gb.high_ram));
    append(gb.object_attribute_memory, sizeof(gb.object_attribute_memory));
    append(gb.extra_oam, sizeof(gb.extra_oam));
    append(gb.cgb_bg_palette_ram, sizeof(gb.cgb_bg_palette_ram));
    append(gb.cgb_obj_palette_ram, sizeof(gb.cgb_obj_palette_ram));
    for (const unsigned port : {0x46u, 0x48u, 0x49u})
        result.append(char(gb.io_registers[port]));
    return result;
}

void fillResetMemory(CupidGb &gb) {
    std::memset(gb.work_ram, 0x63, sizeof(gb.work_ram));
    std::memset(gb.high_ram, 0x74, sizeof(gb.high_ram));
    std::memset(gb.object_attribute_memory, 0x85, sizeof(gb.object_attribute_memory));
    std::memset(gb.extra_oam, 0x52, sizeof(gb.extra_oam));
    std::memset(gb.cgb_bg_palette_ram, 0x96, sizeof(gb.cgb_bg_palette_ram));
    std::memset(gb.cgb_obj_palette_ram, 0xa7, sizeof(gb.cgb_obj_palette_ram));
    gb.io_registers[0x46] = 0xb8;
    gb.io_registers[0x48] = 0xc9;
    gb.io_registers[0x49] = 0xda;
    std::memset(gb.cartridge_ram, 0x6b, gb.cartridge_ram_size);
}

struct ResetClock {
    int64_t now = INT64_C(1800000000);
    unsigned calls = 0;
};

int64_t resetTime(void *context) {
    auto &clock = *static_cast<ResetClock *>(context);
    ++clock.calls;
    return clock.now;
}
} // namespace

bool MainWindow::resetSmokeTest(const QString &directory) {
    std::vector<std::pair<QString, QVariant>> previousSettings;
    for (const auto &key : settings_.allKeys())
        previousSettings.emplace_back(key, settings_.value(key));
    const auto restoreSettings = qScopeGuard([&] {
        settings_.clear();
        for (const auto &[key, value] : previousSettings)
            settings_.setValue(key, value);
    });
    settings_.clear();
    settings_.setValue("emulation/rewindSeconds", 120);
    settings_.setValue("audio/highpassMode", int(CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM));
    settings_.setValue("audio/interferenceVolume", 35);
    if (!resetInputSmokeTest(directory))
        return smokeFailure(__FILE__, __LINE__);
    const QDir fixtures(QDir(directory).filePath("reset"));
    if (!QDir().mkpath(fixtures.filePath("firmware")))
        return smokeFailure(__FILE__, __LINE__);
    const auto cartridge = [&](const QString &name, uint8_t type, bool color = false) {
        QByteArray rom(32768, char(0));
        rom[0x100] = char(0x18);
        rom[0x101] = char(0xfe);
        rom[0x143] = color ? char(0x80) : 0;
        rom[0x147] = char(type);
        rom[0x149] = type == 0xbc ? char(0xc1) : char(2);
        if (type == 0xbc) {
            rom[0x14a] = char(0x65);
            rom[0x152] = 1;
            rom[0x153] = 8;
        }
        uint8_t checksum = 0;
        for (int address = 0x134; address <= 0x14c; ++address)
            checksum = uint8_t(checksum - uint8_t(rom[address]) - 1);
        rom[0x14d] = char(checksum);
        const QString path = fixtures.filePath(name + (color ? ".gbc" : ".gb"));
        return writeResetFixture(path, rom) ? path : QString();
    };
    const QString volatilePath = cartridge("volatile", 8);
    const QString colorPath = cartridge("color", 8, true);
    if (volatilePath.isEmpty() || colorPath.isEmpty())
        return smokeFailure(__FILE__, __LINE__);

    // Menu reset, quick reset, asset reload, and the two native console commands
    // have distinct history/pause contracts while sharing one retained audio stream.
    for (unsigned workflow = 0; workflow < 5; ++workflow) {
        MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        session.timer_.stop();
        if (!session.quickResetAction_ || session.quickResetAction_->isEnabled() ||
            session.showBackgroundAction_->isEnabled() || session.showObjectsAction_->isEnabled() ||
            session.quickResetAction_->shortcut() != QKeySequence("Ctrl+Alt+R") ||
            !session.openRom(volatilePath) || !session.quickResetAction_->isEnabled())
            return smokeFailure(__FILE__, __LINE__);
        session.setPaused(true);
        const auto visibleState = session.captureState();
        for (auto *layer : {session.showBackgroundAction_, session.showObjectsAction_}) {
            if (!layer->isEnabled() || !layer->isChecked())
                return smokeFailure(__FILE__, __LINE__);
            layer->trigger();
        }
        if (visibleState.isEmpty() || session.captureState() != visibleState ||
            !session.restoreState(visibleState, false) ||
            !session.emulator_->gb.hide_background_layer ||
            !session.emulator_->gb.hide_object_layer)
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray coldMemory = resetPhysicalMemory(session.emulator_->gb);
        if (!cupid_gb_apu_set_sample_rate(&session.emulator_->gb, 48000))
            return smokeFailure(__FILE__, __LINE__);
        session.audioChannelActions_[1]->setChecked(false);
        session.audioChannelActions_[3]->setChecked(false);
        session.applyAudioPolicy(&session.emulator_->gb);
        const QString recording = fixtures.filePath(QString("workflow-%1.wav").arg(workflow));
        if (!session.startRecording(recording) || !session.runFrame() || !session.runFrame() ||
            !session.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        session.setPrinterConnected(true);
        if (!session.printer_)
            return smokeFailure(__FILE__, __LINE__);
        auto &gb = session.emulator_->gb;
        cupid_gb_write_u8(&gb, 0xff01, 0x88);
        cupid_gb_write_u8(&gb, 0xff02, 0x81);
        cupid_gb_tick(&gb, 384);
        if (!session.printer_->state.incoming_bits || session.printer_->state.incoming_bits >= 8)
            return smokeFailure(__FILE__, __LINE__);
        fillResetMemory(gb);
        session.rememberFrame();
        const QByteArray physical = resetPhysicalMemory(gb);
        const auto output = std::make_unique<CupidGbAudioOutput>(gb.apu.output);
        const uint64_t recorded = session.recorder_->framesWritten();
        const uint64_t cycles = gb.elapsed_cycles;
        const uint64_t age = session.rewindStates_.back().cycles;
        const size_t history = session.rewindStates_.size();
        const auto *owner = session.emulator_.get();
        const auto *printer = session.printer_;
        const auto imageCallback = printer->on_image;
        const auto doneCallback = printer->on_done;
        if (workflow == 0)
            session.resetAction_->trigger();
        else if (workflow == 1)
            session.quickResetAction_->trigger();
        else if (workflow == 2)
            session.reloadAction_->trigger();
        else {
            session.ensureDebugger();
            session.debuggerWindow_->submitCommand(workflow == 3 ? "reset" : "reset quick");
        }
        const auto &reset = session.emulator_->gb;
        const bool quick = workflow == 1 || workflow == 4;
        if (session.failed_ || reset.elapsed_cycles || reset.cpu.pc != 0x100 ||
            session.paused_ != (workflow >= 2) ||
            (session.emulator_.get() == owner) != (workflow >= 3) ||
            resetPhysicalMemory(reset) != (quick ? physical : coldMemory) ||
            !reset.hide_background_layer || !reset.hide_object_layer ||
            reset.cartridge_ram[0] != (quick ? 0x6b : 0xff) ||
            std::memcmp(&reset.apu.output, output.get(), sizeof(*output)) ||
            !cupid_gb_audio_state_valid(&reset) || !session.recorder_->active() ||
            session.recorder_->sampleRate() != 48000 ||
            session.recorder_->framesWritten() != recorded || session.printer_ != printer ||
            printer->on_image != imageCallback || printer->on_done != doneCallback ||
            printer->state.phase != PRINTER_SYNC_FIRST || printer->state.incoming_bits ||
            reset.serial_link.user_data != printer || session.rewindFailed_)
            return smokeFailure(__FILE__, __LINE__);
        if (workflow == 0) {
            if (session.rewindStates_.size() != 1 || session.rewindStates_.front().cycles)
                return smokeFailure(__FILE__, __LINE__);
        } else {
            if (session.rewindStates_.size() != history + 1 ||
                session.rewindStates_.back().cycles != age || !session.rewindFrame() ||
                session.emulator_->gb.elapsed_cycles <= cycles ||
                session.recorder_->framesWritten() != recorded || session.rewindFailed_ ||
                session.rewindStates_.back().cycles < session.rewindStates_.front().cycles ||
                session.rewindStates_.back().cycles - session.rewindStates_.front().cycles >
                    UINT64_C(120) * cupid_gb_base_clock_hz(&session.emulator_->gb))
                return smokeFailure(__FILE__, __LINE__);
        }
        if (!session.runFrame() || session.recorder_->framesWritten() <= recorded ||
            !session.stopRecording() ||
            QFileInfo(recording).size() != qint64(44 + session.recorder_->framesWritten() * 4) ||
            !session.closeRom() || session.quickResetAction_->isEnabled() ||
            session.showBackgroundAction_->isEnabled() || session.showObjectsAction_->isEnabled())
            return smokeFailure(__FILE__, __LINE__);
    }

    // Independently authored firmware changes one RAM byte then releases its
    // overlay. Inspect native entry before running any of those instructions.
    for (const auto model :
         {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A, CUPID_GB_MODEL_CGB_B,
          CUPID_GB_MODEL_CGB_C, CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB}) {
        const bool color = cupid_gb_model_is_cgb(model);
        QByteArray firmware(color ? 0x900 : 0x100, char(0));
        const QByteArray begin = QByteArray::fromHex("3e31ea00c0c3fc00");
        std::memcpy(firmware.data(), begin.constData(), size_t(begin.size()));
        const QByteArray finish = QByteArray::fromHex("3e01e050");
        std::memcpy(firmware.data() + 0xfc, finish.constData(), size_t(finish.size()));
        const QString boot =
            fixtures.filePath(model == CUPID_GB_MODEL_CGB_0 ? "firmware/cgb0_boot.bin"
                              : color                       ? "firmware/cgb_boot.bin"
                                                            : "firmware/dmg_boot.bin");
        if (!writeResetFixture(boot, firmware))
            return smokeFailure(__FILE__, __LINE__);
        MainWindow session(true, true, false, fixtures.filePath("firmware"), int(model), 0);
        session.timer_.stop();
        if (!session.openRom(color ? colorPath : volatilePath))
            return smokeFailure(__FILE__, __LINE__);
        fillResetMemory(session.emulator_->gb);
        const QByteArray physical = resetPhysicalMemory(session.emulator_->gb);
        if (!session.quickReset() || !session.emulator_->gb.boot_rom_enabled ||
            session.emulator_->gb.cpu.pc || session.emulator_->gb.elapsed_cycles ||
            resetPhysicalMemory(session.emulator_->gb) != physical)
            return smokeFailure(__FILE__, __LINE__);
        for (unsigned step = 0; step < 5; ++step)
            if (!cupid_gb_step(&session.emulator_->gb))
                return smokeFailure(__FILE__, __LINE__);
        if (session.emulator_->gb.boot_rom_enabled || session.emulator_->gb.cpu.pc != 0x100 ||
            session.emulator_->gb.work_ram[0] != 0x31 ||
            session.emulator_->gb.work_ram[1] != 0x63 || session.emulator_->gb.elapsed_cycles != 60)
            return smokeFailure(__FILE__, __LINE__);
        session.bootDirectoryOverride_ = QStringLiteral("");
        fillResetMemory(session.emulator_->gb);
        if (!session.quickReset() || session.emulator_->gb.boot_rom_enabled ||
            session.emulator_->gb.cpu.pc != 0x100 || session.rewindStates_.size() != 1 ||
            session.emulator_->gb.work_ram[0] != 0x63 ||
            session.emulator_->gb.work_ram[0x2000] != (color ? 0 : 0x63) ||
            session.emulator_->gb.object_attribute_memory[0] != (color ? 0 : 0x85) ||
            session.emulator_->gb.extra_oam[0] != 0x52 ||
            session.emulator_->gb.extra_oam[sizeof(session.emulator_->gb.extra_oam) - 1] != 0x52)
            return smokeFailure(__FILE__, __LINE__);
    }

    // The new command participates in the same persisted shortcut mechanism,
    // including an explicitly disabled shortcut.
    for (const QString &shortcut : {QStringLiteral("Ctrl+Alt+F9"), QStringLiteral("")}) {
        settings_.setValue("shortcuts/quickReset", shortcut);
        MainWindow session(true, true, false, QStringLiteral(""));
        session.timer_.stop();
        if (session.quickResetAction_->shortcut() != QKeySequence(shortcut) ||
            std::find(session.shortcutActions_.begin(), session.shortcutActions_.end(),
                      session.quickResetAction_) == session.shortcutActions_.end())
            return smokeFailure(__FILE__, __LINE__);
    }
    settings_.remove("shortcuts/quickReset");

    // Ordinary Reset may adopt a newly selected model. Audio rate and active
    // recording are host-owned even when the oscillator and rewind identity change.
    {
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
        MainWindow session(true, true, false, QStringLiteral(""), std::nullopt, 0);
        session.timer_.stop();
        const QString recording = fixtures.filePath("model-change.wav");
        if (!session.openRom(volatilePath) ||
            !cupid_gb_apu_set_sample_rate(&session.emulator_->gb, 192000) ||
            !session.startRecording(recording) || !session.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        const uint64_t frames = session.recorder_->framesWritten();
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_SGB));
        if (!session.restart() || session.emulator_->gb.model != CUPID_GB_MODEL_SGB ||
            !cupid_gb_audio_state_valid(&session.emulator_->gb) ||
            cupid_gb_apu_get_sample_rate(&session.emulator_->gb) != 192000 ||
            !session.recorder_->active() || session.recorder_->framesWritten() != frames ||
            session.rewindStates_.size() != 1 || !session.runFrame() ||
            session.recorder_->framesWritten() <= frames || !session.stopRecording())
            return smokeFailure(__FILE__, __LINE__);
    }

    // Preserve the live clock provider and controller-specific clock storage.
    // HuC3 resets its clock/alarm digits and must schedule that persistent change.
    for (const uint8_t type : {uint8_t(0x10), uint8_t(0xbc), uint8_t(0xfe)}) {
        ResetClock clock;
        const QString path = cartridge(QString("clock-%1").arg(type), type);
        MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        session.timer_.stop();
        if (path.isEmpty() || !session.openRom(path))
            return smokeFailure(__FILE__, __LINE__);
        auto &gb = session.emulator_->gb;
        cupid_gb_set_rtc_time_source(&gb, resetTime, &clock);
        settings_.setValue("emulation/rtcMode", int(CUPID_GB_RTC_ACCURATE));
        cupid_gb_set_rtc_mode(&gb, CUPID_GB_RTC_ACCURATE);
        gb.rtc.s = 17;
        gb.rtc.ls = 29;
        gb.tpp1.live[3] = 31;
        gb.tpp1.latched[3] = 37;
        gb.huc3.minutes = 43;
        gb.huc3.alarm_minutes = 47;
        gb.rtc_cycles = 1234;
        const auto generation = gb.battery_write_generation;
        if (!session.quickReset())
            return smokeFailure(__FILE__, __LINE__);
        const auto &reset = session.emulator_->gb;
        if (reset.rtc_policy.time_source != resetTime ||
            reset.rtc_policy.time_user_data != &clock ||
            reset.rtc_policy.mode != CUPID_GB_RTC_ACCURATE || reset.rtc_cycles != 1234 ||
            reset.rtc.s != 17 || reset.rtc.ls != 29 || reset.tpp1.live[3] != 31 ||
            reset.tpp1.latched[3] != 37)
            return smokeFailure(__FILE__, __LINE__);
        if (type == 0xfe &&
            (reset.huc3.minutes || reset.huc3.alarm_minutes ||
             reset.battery_write_generation != generation + 1 || !session.batteryDirty_ ||
             !session.saveTimer_.isActive() || !session.flushBattery() || session.batteryDirty_))
            return smokeFailure(__FILE__, __LINE__);
    }

    // Preflight failures preserve the full live core, pending printer protocol,
    // recording, snapshots, ownership and host queue. Only the error display changes.
    {
        MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        session.timer_.stop();
        if (!session.openRom(volatilePath) ||
            !session.startRecording(fixtures.filePath("failed-reset.wav")) || !session.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        session.setPaused(true);
        session.ensureDebugger();
        session.setPrinterConnected(true);
        auto &gb = session.emulator_->gb;
        cupid_gb_write_u8(&gb, 0xff01, 0x88);
        cupid_gb_write_u8(&gb, 0xff02, 0x81);
        cupid_gb_tick(&gb, 384);
        session.rememberFrame();
        const auto before = std::make_unique<CupidGb>(gb);
        const auto printer = std::make_unique<CupidGbPrinter>(*session.printer_);
        const auto history = session.rewindStates_;
        const auto owner = session.emulator_.get();
        const auto sessionId = session.sessionId_;
        const auto audio = session.audioBuffer_;
        const auto frames = session.recorder_->framesWritten();
        const auto historyBytes = session.rewindBytes_;
        const auto unchanged = [&] {
            if (session.emulator_.get() != owner || session.sessionId_ != sessionId ||
                std::memcmp(before.get(), &session.emulator_->gb, sizeof(*before)) ||
                std::memcmp(printer.get(), session.printer_, sizeof(*printer)) ||
                !session.recorder_->active() || session.recorder_->framesWritten() != frames ||
                session.audioBuffer_ != audio || session.rewindBytes_ != historyBytes ||
                session.rewindStates_.size() != history.size() || !session.paused_)
                return false;
            for (size_t index = 0; index < history.size(); ++index)
                if (session.rewindStates_[index].compressed != history[index].compressed ||
                    session.rewindStates_[index].cycles != history[index].cycles)
                    return false;
            return true;
        };
        session.debuggerWindow_->submitCommand("reset quick extra");
        if (!unchanged() || session.resetConsole(CupidGbResetKind(99)) || !unchanged())
            return smokeFailure(__FILE__, __LINE__);
        session.failed_ = false;
        const QString symbols = fixtures.filePath("volatile.sym");
        if (!writeResetFixture(symbols, "invalid symbols\n") || session.quickReset() ||
            !unchanged())
            return smokeFailure(__FILE__, __LINE__);
        session.failed_ = false;
        if (!QFile::remove(symbols))
            return smokeFailure(__FILE__, __LINE__);
        session.bootDirectoryOverride_ = fixtures.filePath("missing-firmware");
        if (session.restart() || !unchanged())
            return smokeFailure(__FILE__, __LINE__);
        session.failed_ = false;
        session.bootDirectoryOverride_ = QStringLiteral("");
    }
    return true;
}

bool MainWindow::resetInputSmokeTest(const QString &directory) {
    const QDir fixtures(QDir(directory).filePath("reset-input"));
    if (!QDir().mkpath(fixtures.path()))
        return smokeFailure(__FILE__, __LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom[0x143] = char(0x80);
    rom[0x147] = char(0x22);
    uint8_t checksum = 0;
    for (int address = 0x134; address <= 0x14c; ++address)
        checksum = uint8_t(checksum - uint8_t(rom[address]) - 1);
    rom[0x14d] = char(checksum);
    const QString path = fixtures.filePath("held-input.gbc");
    if (!writeResetFixture(path, rom))
        return smokeFailure(__FILE__, __LINE__);
    const QPointer<QWidget> previousActive = QApplication::activeWindow();
    const auto restoreFocus =
        qScopeGuard([previousActive] { selectResetTestWindow(previousActive); });
    selectResetTestWindow(nullptr);

    // Exercise actions at their entry boundary. Menu navigation has its own
    // delivery gate; only actual key, pointer, and controller events change holds here.
    for (unsigned workflow = 0; workflow < 3; ++workflow) {
        for (const bool mouse : {false, true}) {
            MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_CGB, 0);
            session.timer_.stop();
            session.controllerSmokeActive_ = true;
            auto configuration = session.controllers_->configuration();
            configuration.enabled = false;
            configuration.background = false;
            session.controllers_->configure(configuration);
            if (!session.openRom(path))
                return smokeFailure(__FILE__, __LINE__);
            session.specialKeys_[4] = Qt::Key_B;
            session.mouseMotionAction_->setChecked(true);
            session.motionFocusActive_ = true;
            session.refreshControllerGate();
            const auto mouseEvent = [&](QEvent::Type type) {
                const auto *display = session.display_;
                const QRectF viewport =
                    displayViewport(display->image.size(), display->size(), display->scaling,
                                    display->devicePixelRatioF());
                const QPointF position = viewport.topLeft();
                const bool move = type == QEvent::MouseMove;
                const bool down = type == QEvent::MouseButtonPress;
                QMouseEvent event(type, position, QPointF(display->mapToGlobal(position.toPoint())),
                                  move ? Qt::NoButton : Qt::LeftButton,
                                  down ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(session.display_, &event);
            };
            const auto keyEvent = [&](bool down, int key, Qt::KeyboardModifiers modifiers = {}) {
                QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, key, modifiers);
                QApplication::sendEvent(session.display_, &event);
            };
            cupid::input::ControllerOutput delivered;
            delivered.buttons = {0xfe, 0xfd, 0xfb, 0xf7};
            delivered.rapid[2][0] = true;
            delivered.motionAvailable = true;
            delivered.motionX = 31;
            delivered.motionY = -11;
            // This uses the manager's delivery callback, while the motion suite
            // separately covers held SDL virtual devices across a native reset action.
            session.controllers_->outputChanged(delivered);
            keyEvent(true, session.keys_[4]);
            keyEvent(true, session.specialKeys_[4]);
            if (mouse) {
                mouseEvent(QEvent::MouseMove);
                mouseEvent(QEvent::MouseButtonPress);
            }
            session.advanceControls();
            auto &before = session.emulator_->gb;
            if (!mouse)
                cupid_gb_tick(&before, 1);
            if (before.joyp_sample_pending != mouse)
                return smokeFailure(__FILE__, __LINE__);
            if (before.joypad != 0xc0 || before.joypad_extra[0] != 0xfd ||
                before.joypad_extra[1] != 0xeb || before.joypad_extra[2] != 0xf7 ||
                before.mbc7.motion_x != (mouse ? 112 : 31) ||
                before.mbc7.motion_y != (mouse ? 112 : -11) || session.rapidPhase_[1] != 1 ||
                session.controllerRapidPhase_[2][0] != 1)
                return smokeFailure(__FILE__, __LINE__);
            const auto held = session.keyboardSpecialHeld_;
            const auto rapid = session.rapidPhase_;
            const auto controllerRapid = session.controllerRapidPhase_;
            const auto source = session.motionSource_;
            const uint8_t physical = session.physicalJoypad_;
            const auto controls = session.controllerOutput_;
            const auto core = std::make_unique<CupidGb>(before);
            const auto *owner = session.emulator_.get();
            const auto controlsUnchanged = [&] {
                return session.physicalJoypad_ == physical &&
                       session.keyboardSpecialHeld_ == held && session.rapidPhase_ == rapid &&
                       session.controllerRapidPhase_ == controllerRapid &&
                       session.controllerOutput_ == controls && session.motionSource_ == source &&
                       session.mouseButtonA_ == mouse;
            };
            const auto kind = workflow == 0   ? ResetWorkflow::Normal
                              : workflow == 1 ? ResetWorkflow::Quick
                                              : ResetWorkflow::Reload;
            session.bootDirectoryOverride_ = fixtures.filePath("missing-firmware");
            if (session.resetCartridge(kind) || session.emulator_.get() != owner ||
                std::memcmp(core.get(), &session.emulator_->gb, sizeof(*core)) ||
                !controlsUnchanged())
                return smokeFailure(__FILE__, __LINE__);
            session.failed_ = false;
            session.bootDirectoryOverride_ = QStringLiteral("");

            QAction *action = workflow == 0   ? session.resetAction_
                              : workflow == 1 ? session.quickResetAction_
                                              : session.reloadAction_;
            action->trigger();
            const auto &after = session.emulator_->gb;
            if (session.failed_ || session.paused_ || after.elapsed_cycles ||
                session.emulator_.get() == owner || !controlsUnchanged() ||
                after.joypad != core->joypad ||
                after.joyp_sample_pending != core->joyp_sample_pending ||
                std::memcmp(after.joypad_extra, core->joypad_extra, sizeof(after.joypad_extra)) ||
                after.mbc7.motion_x != core->mbc7.motion_x ||
                after.mbc7.motion_y != core->mbc7.motion_y || (after.interrupt_flags & 0x10))
                return smokeFailure(__FILE__, __LINE__);

            // A modifier added after a press must still allow the real release.
            keyEvent(false, session.keys_[4], Qt::ControlModifier);
            keyEvent(false, session.specialKeys_[4]);
            if (mouse)
                mouseEvent(QEvent::MouseButtonRelease);
            session.controllers_->outputChanged(cupid::input::ControllerOutput{});
            if (after.joypad != 0xff || after.joypad_extra[0] != 0xff ||
                after.joypad_extra[1] != 0xff || after.joypad_extra[2] != 0xff ||
                session.physicalJoypad_ != 0xff || session.keyboardSpecialHeld_[4] ||
                session.rapidHeld_[1] || session.controllerRapidHeld_[2][0] ||
                session.mouseButtonA_)
                return smokeFailure(__FILE__, __LINE__);

            keyEvent(true, session.keys_[4]);
            keyEvent(true, session.specialKeys_[4]);
            mouseEvent(QEvent::MouseMove);
            mouseEvent(QEvent::MouseButtonPress);
            QEvent deactivate(QEvent::WindowDeactivate);
            QApplication::sendEvent(&session, &deactivate);
            if (after.joypad != 0xff || after.mbc7.motion_x || after.mbc7.motion_y ||
                session.physicalJoypad_ != 0xff || session.keyboardSpecialHeld_[4] ||
                session.mouseButtonA_ || session.motionSource_ != MotionSource::None)
                return smokeFailure(__FILE__, __LINE__);

            // Reload has no temporary resume. Paused controller delivery remains
            // a host hold; it must not become a new guest press during the reload.
            QEvent activate(QEvent::WindowActivate);
            QApplication::sendEvent(&session, &activate);
            session.setPaused(true);
            session.controllers_->outputChanged(delivered);
            const auto pausedControls = session.controllerOutput_;
            session.reloadAction_->trigger();
            if (session.failed_ || !session.paused_ || session.emulator_->gb.elapsed_cycles ||
                session.emulator_->gb.joypad != 0xff ||
                !(session.controllerOutput_ == pausedControls) || session.rapidHeld_[1] ||
                session.controllerRapidHeld_[2][0])
                return smokeFailure(__FILE__, __LINE__);
        }
    }
    qInfo("CupidGB reset action input retention and release checks passed");
    return resetMenuInputSmokeTest(directory);
}

bool MainWindow::resetMenuInputSmokeTest(const QString &directory) {
    const QString path = QDir(directory).filePath("reset-input/held-input.gbc");
    const QPointer<QWidget> previousActive = QApplication::activeWindow();
    const auto restoreFocus =
        qScopeGuard([previousActive] { selectResetTestWindow(previousActive); });
    for (unsigned workflow = 0; workflow < 3; ++workflow) {
        for (const bool shortcut : {false, true}) {
            MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_CGB, 0);
            session.timer_.stop();
            auto configuration = session.controllers_->configuration();
            configuration.enabled = false;
            session.controllers_->configure(configuration);
            if (!session.openRom(path))
                return smokeFailure(__FILE__, __LINE__);
            session.setAttribute(Qt::WA_DontShowOnScreen);
            session.show();
            selectResetTestWindow(&session);
            session.display_->setFocus();
            session.motionFocusActive_ = true;
            session.specialKeys_[4] = Qt::Key_B;
            session.mouseMotionAction_->setChecked(true);
            const auto key = [](QWidget *target, bool down, int code,
                                Qt::KeyboardModifiers modifiers = {}) {
                QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, code, modifiers);
                QApplication::sendEvent(target, &event);
            };
            key(session.display_, true, session.keys_[4]);
            key(session.display_, true, session.specialKeys_[4]);
            const QRectF viewport =
                displayViewport(session.display_->image.size(), session.display_->size(),
                                session.display_->scaling, session.display_->devicePixelRatioF());
            const QPointF position = viewport.topLeft();
            const QPointF global = session.display_->mapToGlobal(position.toPoint());
            QMouseEvent move(QEvent::MouseMove, position, global, Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
            QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(session.display_, &move);
            QApplication::sendEvent(session.display_, &press);
            cupid_gb_tick(&session.emulator_->gb, 1);
            if (session.emulator_->gb.joypad != 0xcf || !session.mouseButtonA_ ||
                session.emulator_->gb.joyp_sample_pending ||
                session.emulator_->gb.mbc7.motion_x != 112)
                return smokeFailure(__FILE__, __LINE__);
            QAction *action = workflow == 0   ? session.resetAction_
                              : workflow == 1 ? session.quickResetAction_
                                              : session.reloadAction_;
            const uint64_t previousSession = session.sessionId_;
            uint8_t expectedButtons = 0xcf;
            uint8_t expectedPhysical = 0xef;
            bool expectedMouse = true;
            bool expectedRapid = true;
            bool expectedPending = false;
            unsigned activations = 0;
            connect(action, &QAction::triggered, &session, [&] { ++activations; });
            if (shortcut) {
                // Deliver real Qt key events through the shortcut map. Calling
                // QAction::trigger directly cannot verify key recognition.
                const Qt::KeyboardModifiers modifiers =
                    workflow == 0 ? Qt::ControlModifier : Qt::ControlModifier | Qt::AltModifier;
                const int actionKey = workflow == 2 ? Qt::Key_F10 : Qt::Key_R;
                if (workflow == 2)
                    action->setShortcut(QKeySequence("Ctrl+Alt+F10"));
                key(session.display_, true, Qt::Key_Control, Qt::ControlModifier);
                if (workflow != 0)
                    key(session.display_, true, Qt::Key_Alt, modifiers);
                key(session.display_, true, actionKey, modifiers);
                key(session.display_, false, actionKey, modifiers);
                if (workflow != 0)
                    key(session.display_, false, Qt::Key_Alt, Qt::ControlModifier);
                key(session.display_, false, Qt::Key_Control);
            } else {
                auto *menu = session.menuBar()->actions().value(workflow == 2 ? 0 : 1)->menu();
                menu->setAttribute(Qt::WA_DontShowOnScreen);
                menu->popup(session.mapToGlobal(QPoint(20, 20)));
                if (!session.menuDepth_ || !session.menuInputSuspended_ ||
                    session.emulator_->gb.joypad != expectedButtons || !session.mouseButtonA_ ||
                    session.emulator_->gb.joyp_sample_pending)
                    return smokeFailure(__FILE__, __LINE__);
                const uint64_t menuCycles = session.emulator_->gb.elapsed_cycles;
                session.tick();
                if (session.emulator_->gb.elapsed_cycles != menuCycles)
                    return smokeFailure(__FILE__, __LINE__);
                if (workflow == 0) {
                    key(menu, false, session.specialKeys_[4]);
                    expectedButtons = 0xef;
                    expectedRapid = false;
                    expectedPending = true;
                } else if (workflow == 1) {
                    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(-1, -1),
                                        menu->mapToGlobal(QPoint(-1, -1)), Qt::LeftButton,
                                        Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(menu, &release);
                    expectedMouse = false;
                } else {
                    key(menu, false, session.keys_[4], Qt::ControlModifier);
                    expectedPhysical = 0xff;
                }
                // Menu navigation must not create a new game-button press.
                key(menu, true, session.keys_[5]);
                if (session.physicalJoypad_ != expectedPhysical ||
                    session.emulator_->gb.joypad != expectedButtons ||
                    session.mouseButtonA_ != expectedMouse ||
                    session.emulator_->gb.mbc7.motion_x != 112) {
                    qCritical("Reset menu controls differ: workflow=%u physical=%02x/%02x "
                              "guest=%02x/%02x mouse=%d/%d motion=%d menuDepth=%u suspended=%d",
                              workflow, unsigned(session.physicalJoypad_),
                              unsigned(expectedPhysical), unsigned(session.emulator_->gb.joypad),
                              unsigned(expectedButtons), int(session.mouseButtonA_),
                              int(expectedMouse), int(session.emulator_->gb.mbc7.motion_x),
                              session.menuDepth_, int(session.menuInputSuspended_));
                    return smokeFailure(__FILE__, __LINE__);
                }
                menu->setActiveAction(action);
                key(menu, true, Qt::Key_Return);
                key(menu, false, Qt::Key_Return);
            }
            if (activations != 1 || session.sessionId_ != previousSession + 1 || session.failed_ ||
                session.paused_ || session.emulator_->gb.elapsed_cycles ||
                session.emulator_->gb.joypad != expectedButtons ||
                session.emulator_->gb.joyp_sample_pending != expectedPending ||
                session.physicalJoypad_ != expectedPhysical ||
                session.mouseButtonA_ != expectedMouse || session.rapidHeld_[1] != expectedRapid ||
                session.emulator_->gb.mbc7.motion_x != 112) {
                qCritical("Reset input result differs: workflow=%u shortcut=%d activations=%u "
                          "session=%llu/%llu cycles=%llu paused=%d guest=%02x/%02x "
                          "pending=%d/%d physical=%02x/%02x mouse=%d/%d rapid=%d/%d motion=%d",
                          workflow, int(shortcut), activations,
                          static_cast<unsigned long long>(session.sessionId_),
                          static_cast<unsigned long long>(previousSession + 1),
                          static_cast<unsigned long long>(session.emulator_->gb.elapsed_cycles),
                          int(session.paused_), unsigned(session.emulator_->gb.joypad),
                          unsigned(expectedButtons), int(session.emulator_->gb.joyp_sample_pending),
                          int(expectedPending), unsigned(session.physicalJoypad_),
                          unsigned(expectedPhysical), int(session.mouseButtonA_),
                          int(expectedMouse), int(session.rapidHeld_[1]), int(expectedRapid),
                          int(session.emulator_->gb.mbc7.motion_x));
                return smokeFailure(__FILE__, __LINE__);
            }
            if (!shortcut) {
                // Hiding the last menu queues physical-input resynchronization.
                // A nested event loop must not execute the guest in that interval.
                if (session.menuDepth_ || !session.menuInputSuspended_)
                    return smokeFailure(__FILE__, __LINE__);
                session.tick();
                if (session.emulator_->gb.elapsed_cycles)
                    return smokeFailure(__FILE__, __LINE__);
            }
            QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
            if (session.menuDepth_ || session.menuInputSuspended_ ||
                session.emulator_->gb.joypad != expectedButtons)
                return smokeFailure(__FILE__, __LINE__);
            key(session.display_, false, session.keys_[4]);
            key(session.display_, false, session.specialKeys_[4]);
            QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(session.display_, &release);
            if (session.emulator_->gb.joypad != 0xff || session.physicalJoypad_ != 0xff ||
                session.mouseButtonA_ || session.rapidHeld_[1])
                return smokeFailure(__FILE__, __LINE__);
            if (!shortcut) {
                key(session.display_, true, session.keys_[4]);
                QApplication::sendEvent(session.display_, &move);
                QApplication::sendEvent(session.display_, &press);
                auto *menu = session.menuBar()->actions().value(1)->menu();
                menu->setAttribute(Qt::WA_DontShowOnScreen);
                menu->popup(session.mapToGlobal(QPoint(20, 20)));
                if (!session.menuInputSuspended_ || !session.mouseButtonA_ ||
                    session.physicalJoypad_ != 0xef)
                    return smokeFailure(__FILE__, __LINE__);
                QWidget tool(&session, Qt::Tool);
                tool.setAttribute(Qt::WA_DontShowOnScreen);
                tool.show();
                selectResetTestWindow(&tool);
                if (session.menuInputSuspended_ || session.mouseButtonA_ ||
                    session.physicalJoypad_ != 0xff || session.emulator_->gb.joypad != 0xff ||
                    session.emulator_->gb.mbc7.motion_x || session.emulator_->gb.mbc7.motion_y)
                    return smokeFailure(__FILE__, __LINE__);
                menu->hide();
                QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
                if (session.mouseButtonA_ || session.physicalJoypad_ != 0xff)
                    return smokeFailure(__FILE__, __LINE__);
            }
        }
    }
    qInfo("CupidGB menu and keyboard reset input checks passed");
    return true;
}

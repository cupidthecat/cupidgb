/**
 * @file    main_window_sgb_tests.cpp
 * @brief   Adapter startup, ordered audio, state files, redraw, and debugger workflows.
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

#include "main_window_internal.h"
#include "main_window_state_test_helpers.h"

#include <QJsonArray>
#include <QScopeGuard>

extern "C" {
#include "cupid/gb/timer.h"
}

namespace {
bool writeSgbFixture(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readSgbFixture(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray pcmBytes(const int16_t *samples, size_t frames) {
    QByteArray bytes;
    bytes.reserve(qsizetype(frames * 4));
    for (size_t i = 0; i < frames * 2; ++i) {
        const uint16_t value = uint16_t(samples[i]);
        bytes.append(char(value));
        bytes.append(char(value >> 8));
    }
    return bytes;
}

bool drainSmallChunks(CupidGb &gb, QByteArray &bytes) {
    int16_t samples[34];
    CupidGbAudioOrigin origin;
    for (;;) {
        const auto count = cupid_gb_apu_drain_chunk(&gb, samples, 17, &origin);
        if (!count)
            break;
        bytes += pcmBytes(samples, count);
    }
    const auto pending = cupid_gb_apu_pending(&gb);
    return !pending.handheld_frames && !pending.startup_frames;
}
} // namespace

bool MainWindow::externalSgbSmokeTest(const QString &directory) {
    QTemporaryDir fixtures(QDir(directory).filePath("sgb-external-XXXXXX"));
    if (!fixtures.isValid())
        return smokeFailure(__FILE__, __LINE__);
    std::vector<std::pair<QString, QVariant>> preferences;
    for (const QString &key : settings_.allKeys())
        preferences.emplace_back(key, settings_.value(key));
    const auto restorePreferences = qScopeGuard([&] {
        settings_.clear();
        for (const auto &[key, value] : preferences)
            settings_.setValue(key, value);
    });
    settings_.setValue("video/sgbBorderMode", int(SgbBorderMode::SgbOnly));
    QByteArray cartridge(32768, char(0));
    cartridge[0x100] = char(0x18);
    cartridge[0x101] = char(0xfe);
    cartridge[0x146] = 3;
    cartridge[0x14b] = 0x33;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        cartridge[0x14d] = char(uint8_t(cartridge[0x14d]) - uint8_t(cartridge[address]) - 1);
    const QString cartridgePath = fixtures.filePath("adapter.gb");
    if (!writeSgbFixture(cartridgePath, cartridge) ||
        automaticModel(cartridge) != CUPID_GB_MODEL_SGB)
        return smokeFailure(__FILE__, __LINE__);
    const auto bootFixture = [](uint8_t marker) {
        QByteArray boot(256, char(0));
        boot[0] = char(0x3e);
        boot[1] = char(marker);
        boot[2] = char(0xea);
        boot[4] = char(0xc0);
        boot[5] = char(0x18);
        boot[6] = char(0xfe);
        return boot;
    };
    const auto firstFirmware = bootFixture(0x42);
    const auto secondFirmware = bootFixture(0x63);
    if (!writeSgbFixture(fixtures.filePath("sgb_boot.bin"), firstFirmware) ||
        !writeSgbFixture(fixtures.filePath("sgb2_boot.bin"), secondFirmware))
        return smokeFailure(__FILE__, __LINE__);
    const std::array profiles{CUPID_GB_MODEL_SGB_EXTERNAL, CUPID_GB_MODEL_SGB_PAL_EXTERNAL,
                              CUPID_GB_MODEL_SGB2_EXTERNAL};
    for (const auto model : profiles) {
        settings_.setValue("emulation/model", int(model));
        MainWindow session(true, true, false, fixtures.path(), std::nullopt, uint64_t(0));
        session.timer_.stop();
        bool selected = false;
        QTimer driver;
        driver.setInterval(1);
        connect(&driver, &QTimer::timeout, &session, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "settings")
                return;
            driver.stop();
            auto *choice = dialog->findChild<QComboBox *>("hardwareModel");
            selected = choice && choice->isEnabled() && choice->currentData().toInt() == model;
            for (const auto available : profiles) {
                const int index = choice ? choice->findData(int(available)) : -1;
                selected =
                    selected && index >= 0 && choice->itemText(index).contains("external host");
            }
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        driver.start();
        session.configure();
        if (!selected || settings_.value("emulation/model").toInt() != model ||
            !session.openRom(cartridgePath))
            return smokeFailure(__FILE__, __LINE__);
        auto &gb = session.emulator_->gb;
        const bool second = model == CUPID_GB_MODEL_SGB2_EXTERNAL;
        const auto expectedFirmware = second ? secondFirmware : firstFirmware;
        const uint8_t marker = second ? 0x63 : 0x42;
        const uint32_t expectedClock = second                                     ? 4194304
                                       : model == CUPID_GB_MODEL_SGB_PAL_EXTERNAL ? 4256274
                                                                                  : 4295454;
        if (gb.model != model || !gb.boot_rom_enabled || gb.cpu.pc || gb.elapsed_cycles ||
            gb.boot_rom_size != 256 || std::memcmp(gb.boot_rom, expectedFirmware.data(), 256) ||
            !cupid_gb_model_is_sgb(gb.model) || cupid_gb_sgb_host_present(&gb) ||
            cupid_gb_sgb_startup_active(&gb) || !cupid_gb_sgb_active(&gb) ||
            cupid_gb_sgb_commands_enabled(&gb) || session.image_.size() != QSize(160, 144) ||
            session.displayNativeSize_ != QSize(160, 144) || session.inputPlayerCount() != 1 ||
            cupid_gb_base_clock_hz(&gb) != expectedClock)
            return smokeFailure(__FILE__, __LINE__);
        gb.work_ram[0] = 0;
        for (unsigned step = 0; step < 8; ++step)
            if (!cupid_gb_step(&gb))
                return smokeFailure(__FILE__, __LINE__);
        if (gb.work_ram[0] != marker || !gb.elapsed_cycles ||
            cupid_gb_presentation_cycles(&gb) != gb.elapsed_cycles || !session.runFrame() ||
            session.startupAudioFrames_)
            return smokeFailure(__FILE__, __LINE__);
        for (const auto border : {SgbBorderMode::Never, SgbBorderMode::SgbOnly}) {
            session.sgbBorderActions_[size_t(border)]->trigger();
            if (session.image_.size() != QSize(160, 144))
                return smokeFailure(__FILE__, __LINE__);
        }
        // Ordinary controls retain host masks. External hardware owns electrical P1 lines.
        cupid_gb_write_u8(&gb, 0xff00, 0x10);
        gb.interrupt_flags &= uint8_t(~0x10);
        const uint8_t unconnected = cupid_gb_read_u8(&gb, 0xff00);
        QKeyEvent press(QEvent::KeyPress, session.keys_[4], Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, session.keys_[4], Qt::NoModifier);
        QCoreApplication::sendEvent(session.display_, &press);
        cupid_gb_tick(&gb, 1);
        if ((gb.joypad & 0x10) || cupid_gb_read_u8(&gb, 0xff00) != unconnected ||
            (gb.interrupt_flags & 0x10))
            return smokeFailure(__FILE__, __LINE__);
        QCoreApplication::sendEvent(session.display_, &release);
        auto controller = session.controllerOutput_;
        controller.buttons[0] = 0xef;
        session.receiveControllerOutput(controller);
        cupid_gb_tick(&gb, 1);
        if ((gb.joypad & 0x10) || cupid_gb_read_u8(&gb, 0xff00) != unconnected ||
            (gb.interrupt_flags & 0x10))
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_set_external_joyp(&gb, 0x0f);
        gb.interrupt_flags &= uint8_t(~0x10);
        cupid_gb_set_external_joyp(&gb, 0x0e);
        const uint8_t driven = cupid_gb_read_u8(&gb, 0xff00);
        controller.buttons[0] = 0xff;
        session.receiveControllerOutput(controller);
        cupid_gb_tick(&gb, 1);
        if ((driven & 0x0f) != 0x0e || !(gb.interrupt_flags & 0x10) ||
            cupid_gb_read_u8(&gb, 0xff00) != driven)
            return smokeFailure(__FILE__, __LINE__);
        unsigned hostWrites = 0;
        const auto hostWrite = [](CupidGb *, uint8_t, void *context) {
            ++*static_cast<unsigned *>(context);
        };
        cupid_gb_set_joyp_write_callback(&gb, hostWrite, &hostWrites);
        const auto registrationRetained = [&] {
            const auto &current = session.emulator_->gb;
            return current.joyp_write_callback == hostWrite &&
                   current.joyp_write_user_data == &hostWrites && !hostWrites;
        };
        if (!session.restart() || !registrationRetained() || !session.quickReset() ||
            session.emulator_->gb.model != model ||
            cupid_gb_sgb_startup_active(&session.emulator_->gb) || !registrationRetained() ||
            !session.reloadRom() || !registrationRetained() ||
            session.emulator_->gb.model != model ||
            cupid_gb_sgb_startup_active(&session.emulator_->gb) ||
            session.image_.size() != QSize(160, 144))
            return smokeFailure(__FILE__, __LINE__);
        settings_.setValue("emulation/model", int(cupid_gb_model_base(model)));
        if (!session.openRom(cartridgePath, false, ResetWorkflow::HotSwap) ||
            !registrationRetained() || !cupid_gb_sgb_host_present(&session.emulator_->gb))
            return smokeFailure(__FILE__, __LINE__);
        settings_.setValue("emulation/model", int(model));
        if (!session.openRom(cartridgePath, false, ResetWorkflow::HotSwap) ||
            !registrationRetained() || session.emulator_->gb.model != model ||
            cupid_gb_sgb_host_present(&session.emulator_->gb))
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_write_u8(&session.emulator_->gb, 0xff00, 0x20);
        if (hostWrites != 1)
            return smokeFailure(__FILE__, __LINE__);
        const auto retained = session.captureState();
        const QString required = fixtures.filePath(second ? "sgb2_boot.bin" : "sgb_boot.bin");
        const QString held = required + ".hold";
        if (retained.isEmpty() || !QFile::rename(required, held))
            return smokeFailure(__FILE__, __LINE__);
        const bool rejected = !session.reloadRom();
        const bool recovered = QFile::rename(held, required);
        if (!rejected || !recovered || session.captureState() != retained || hostWrites != 1 ||
            session.emulator_->gb.joyp_write_callback != hostWrite ||
            session.emulator_->gb.joyp_write_user_data != &hostWrites)
            return smokeFailure(__FILE__, __LINE__);
        session.failed_ = false;
        cupid_gb_set_joyp_write_callback(&session.emulator_->gb, nullptr, nullptr);
        if (!session.closeRom())
            return smokeFailure(__FILE__, __LINE__);

        MainWindow portable(true, true, false, QStringLiteral(""), model, uint64_t(0));
        portable.timer_.stop();
        if (!portable.openRom(cartridgePath) || !portable.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        portable.setPaused(true);
        auto &restored = portable.emulator_->gb;
        restored.work_ram[4] = 0x5a;
        const QString statePath = fixtures.filePath(QString("external-%1.bess").arg(int(model)));
        if (!portable.savePortableStateFile(statePath))
            return smokeFailure(__FILE__, __LINE__);
        restored.work_ram[4] = 0x81;
        if (!portable.loadStateFile(statePath) || restored.model != model ||
            restored.work_ram[4] != 0x5a || cupid_gb_sgb_host_present(&restored) ||
            cupid_gb_sgb_startup_active(&restored) || portable.inputPlayerCount() != 1 ||
            portable.image_.size() != QSize(160, 144) || !portable.paused_)
            return smokeFailure(__FILE__, __LINE__);
    }
    qInfo("CupidGB external adapter Qt model, firmware, input, and state workflows passed");
    return true;
}

bool MainWindow::sgbSmokeTest(const QString &directory) {
    if (!sgbBorderModesSmokeTest(directory) || !externalSgbSmokeTest(directory))
        return smokeFailure(__FILE__, __LINE__);
    std::vector<std::pair<QString, QVariant>> preferences;
    for (const QString &key : settings_.allKeys())
        preferences.emplace_back(key, settings_.value(key));
    const auto restorePreferences = qScopeGuard([&] {
        settings_.clear();
        for (const auto &[key, value] : preferences)
            settings_.setValue(key, value);
    });
    settings_.setValue("emulation/rewindSeconds", 120);
    settings_.setValue("emulation/speed", 100);
    settings_.setValue("video/sgbBorderMode", int(SgbBorderMode::SgbOnly));
    const QDir fixtures(QDir(directory).filePath("sgb"));
    const QDir firmware(fixtures.filePath("firmware"));
    const QString captures = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    const QDir output(captures.isEmpty() ? fixtures.path() : QDir(captures).filePath("sgb"));
    if (!QDir().mkpath(firmware.path()) || !QDir().mkpath(output.path()))
        return smokeFailure(__FILE__, __LINE__);
    QByteArray boot(256, char(0));
    // NOP, CALL $0008, then a stationary loop; the subroutine is NOP/RET.
    boot[1] = char(0xcd);
    boot[2] = 8;
    boot[4] = char(0x18);
    boot[5] = char(0xfe);
    boot[9] = char(0xc9);
    if (!writeSgbFixture(firmware.filePath("sgb_boot.bin"), boot) ||
        !writeSgbFixture(firmware.filePath("sgb2_boot.bin"), boot))
        return smokeFailure(__FILE__, __LINE__);
    QStringList cartridges;
    for (const bool enhanced : {false, true}) {
        QByteArray rom(32768, char(0));
        rom[0x100] = char(0x18);
        rom[0x101] = char(0xfe);
        std::memcpy(rom.data() + 0x134, "CUPID HOST", 10);
        rom[0x146] = enhanced ? 3 : 0;
        rom[0x14b] = enhanced ? 0x33 : 0;
        for (unsigned index = 0x134; index <= 0x14c; ++index)
            rom[0x14d] = char(uint8_t(rom[0x14d]) - uint8_t(rom[index]) - 1);
        const QString path = fixtures.filePath(enhanced ? "enhanced.gb" : "ordinary.gb");
        if (!writeSgbFixture(path, rom))
            return smokeFailure(__FILE__, __LINE__);
        cartridges.append(path);
    }
    QJsonArray cases;
    QJsonObject nativeAudio{{"requested", audioCheck_}, {"passed", false}};
    bool nativeChecked = false;
    for (const auto model : {CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_SGB_PAL}) {
        for (int enhanced = 0; enhanced < 2; ++enhanced) {
            const bool detailed = model == CUPID_GB_MODEL_SGB2 && !enhanced;
            const bool native = audioCheck_ && detailed;
            settings_.setValue("emulation/rewindSeconds", 120);
            MainWindow session(true, true, native, firmware.path(), model, uint64_t(0));
            session.timer_.stop();
            if (!session.openRom(cartridges[enhanced]))
                return smokeFailure(__FILE__, __LINE__);
            if (native)
                session.stopAudio();
            auto &gb = session.emulator_->gb;
            if (!cupid_gb_sgb_startup_held(&gb) || gb.sgb_startup.phase != -10 ||
                gb.elapsed_cycles || cupid_gb_presentation_cycles(&gb) ||
                cupid_gb_sgb_active(&gb) != bool(enhanced) ||
                !cupid_gb_apu_set_sample_rate(&gb, 192000))
                return smokeFailure(__FILE__, __LINE__);
            const uint32_t batchFrames =
                cupid_gb_sgb_startup_audio_frame_count(192000, cupid_gb_base_clock_hz(&gb));
            const QString recording =
                output.filePath(QString("startup-%1-%2.wav").arg(int(model)).arg(enhanced));
            if (QFileInfo::exists(recording) || !session.startRecording(recording))
                return smokeFailure(__FILE__, __LINE__);
            session.inputTrace_ = {{1, 0xef}};
            const size_t initialHistory = session.rewindStates_.size();
            PlaybackResult first;
            if (!session.runFrame(Playback::Forward, &first) || first.presentationCycles != 70224 ||
                first.guestCycles || gb.elapsed_cycles || gb.cpu.pc || gb.sgb_startup.phase != -9 ||
                session.startupAudioFrames_ != batchFrames || session.handheldAudioFrames_ ||
                session.recorder_->framesWritten() ||
                session.rewindStates_.size() != initialHistory || session.inputTraceIndex_)
                return smokeFailure(__FILE__, __LINE__);
            const qint64 deadline = session.clock_.nsecsElapsed();
            session.nextFrame_ = deadline;
            session.tick();
            if (gb.sgb_startup.phase != -8 || gb.elapsed_cycles || session.nextFrame_ <= deadline ||
                session.failed_ || session.recorder_->framesWritten())
                return smokeFailure(__FILE__, __LINE__);
            session.setPaused(true);
            const auto pausedPresentation = cupid_gb_presentation_cycles(&gb);
            session.tick();
            session.setPaused(false);
            session.menuDepth_ = 1;
            session.tick();
            session.menuDepth_ = 0;
            session.stateOperation_ = true;
            session.tick();
            session.stateOperation_ = false;
            if (cupid_gb_presentation_cycles(&gb) != pausedPresentation || gb.elapsed_cycles)
                return smokeFailure(__FILE__, __LINE__);
            if (native)
                session.stopAudio();
            bool captured = false;
            while (gb.sgb_startup.phase < 96) {
                if (!session.runFrame() || session.recorder_->framesWritten() ||
                    gb.elapsed_cycles || session.rewindStates_.size() != initialHistory ||
                    session.inputTraceIndex_)
                    return smokeFailure(__FILE__, __LINE__);
                if (detailed && !captured && gb.sgb_startup.phase >= 80) {
                    if (native) {
                        session.setupAudio();
                        if (!session.audio_)
                            return smokeFailure(__FILE__, __LINE__);
                        const auto sink = session.audio_;
                        const auto buffer = session.audioBuffer_;
                        const auto machine = session.emulator_.get();
                        const auto owner = session.sessionId_, timeline = session.timelineId_;
                        QElapsedTimer elapsed;
                        elapsed.start();
                        unsigned generated = 0;
                        while ((!session.audioDevice_ || session.audioPriming_) && generated < 8 &&
                               elapsed.elapsed() < 500) {
                            ++generated;
                            if (!session.runFrame())
                                return smokeFailure(__FILE__, __LINE__);
                        }
                        if (session.emulator_.get() != machine || session.sessionId_ != owner ||
                            session.timelineId_ != timeline || session.audio_ != sink ||
                            !session.audioDevice_ || session.audioPriming_ ||
                            sink->state() != QAudio::ActiveState ||
                            sink->error() != QAudio::NoError)
                            return smokeFailure(__FILE__, __LINE__);
                        const auto device = session.audioDevice_;
                        const auto primes = session.audioPrimeEvents_;
                        bool consumed = false;
                        QJsonObject proof;
                        while (!consumed && generated < 12 && elapsed.elapsed() < 500) {
                            if (buffer->bytesAvailable() < session.audioPrimeBytes_) {
                                ++generated;
                                if (!session.runFrame())
                                    return smokeFailure(__FILE__, __LINE__);
                            }
                            session.pumpAudio();
                            if (session.emulator_.get() != machine || session.sessionId_ != owner ||
                                session.timelineId_ != timeline || session.audio_ != sink ||
                                session.audioBuffer_ != buffer || session.audioDevice_ != device ||
                                session.audioPriming_ || session.audioPrimeEvents_ != primes ||
                                sink->state() != QAudio::ActiveState ||
                                sink->error() != QAudio::NoError)
                                return smokeFailure(__FILE__, __LINE__);
                            const auto beforeQueue = buffer->telemetry();
                            const auto beforeFree = sink->bytesFree();
                            const auto beforePresentation = cupid_gb_presentation_cycles(&gb);
                            const auto beforeFrames = session.generatedAudioFrames_;
                            QEventLoop wait;
                            QTimer::singleShot(1, Qt::PreciseTimer, &wait, &QEventLoop::quit);
                            wait.exec();
                            if (session.emulator_.get() != machine || session.sessionId_ != owner ||
                                session.timelineId_ != timeline || session.audio_ != sink ||
                                session.audioBuffer_ != buffer || session.audioDevice_ != device ||
                                session.audioPriming_ || session.audioPrimeEvents_ != primes ||
                                sink->state() != QAudio::ActiveState ||
                                sink->error() != QAudio::NoError)
                                return smokeFailure(__FILE__, __LINE__);
                            // An event-loop wait can retire a backend. Query its
                            // capacity only after proving the same stream is active.
                            const auto afterQueue = buffer->telemetry();
                            const auto gained = sink->bytesFree() - beforeFree;
                            if (beforeQueue != afterQueue ||
                                cupid_gb_presentation_cycles(&gb) != beforePresentation ||
                                session.generatedAudioFrames_ != beforeFrames ||
                                gb.elapsed_cycles || session.recorder_->framesWritten())
                                return smokeFailure(__FILE__, __LINE__);
                            consumed = gained >= sink->format().bytesPerFrame();
                            proof = {
                                {"gained_bytes", double(gained)},
                                {"written_host_frames", afterQueue["host_frames_written_to_sink"]},
                                {"no_write_interval", true}};
                        }
                        if (!consumed ||
                            buffer->telemetry()["host_frames_written_to_sink"].toDouble() <= 0)
                            return smokeFailure(__FILE__, __LINE__);
                        nativeAudio = {{"requested", true},
                                       {"passed", true},
                                       {"host_rate", sink->format().sampleRate()},
                                       {"host_channels", sink->format().channelCount()},
                                       {"host_sample_format", int(sink->format().sampleFormat())},
                                       {"generated_intervals", int(generated)},
                                       {"elapsed_ms", double(elapsed.elapsed())},
                                       {"guest_cycles", double(gb.elapsed_cycles)},
                                       {"recorded_handheld_frames",
                                        double(session.recorder_->framesWritten())},
                                       {"consumption", proof}};
                        nativeChecked = true;
                        session.stopAudio();
                    }
                    session.sgbBorderActions_[size_t(SgbBorderMode::SgbOnly)]->trigger();
                    session.present(Presentation::Reset);
                    const QByteArray state = session.captureState();
                    const QImage full = session.image_;
                    if (full.size() != QSize(256, 224))
                        return smokeFailure(__FILE__, __LINE__);
                    if (!captures.isEmpty()) {
                        session.show();
                        if (!session.grab().save(output.filePath("startup.png")))
                            return smokeFailure(__FILE__, __LINE__);
                    }
                    for (unsigned redraw = 0; redraw < 4; ++redraw)
                        session.present();
                    if (session.captureState() != state || session.image_ != full ||
                        (!captures.isEmpty() &&
                         !session.grab().save(output.filePath("redraw.png"))))
                        return smokeFailure(__FILE__, __LINE__);
                    session.sgbBorderActions_[size_t(SgbBorderMode::Never)]->trigger();
                    if (session.captureState() != state ||
                        session.image_ !=
                            full.copy(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y, 160, 144) ||
                        (!captures.isEmpty() &&
                         !session.grab().save(output.filePath("cropped.png"))))
                        return smokeFailure(__FILE__, __LINE__);
                    session.sgbBorderActions_[size_t(SgbBorderMode::SgbOnly)]->trigger();
                    captured = true;
                }
            }
            if (gb.sgb_startup.phase != 96 || gb.elapsed_cycles || gb.cpu.pc ||
                session.startupAudioFrames_ != uint64_t(106) * batchFrames)
                return smokeFailure(__FILE__, __LINE__);
            // The full executed introduction verifies that the recorder resumes
            // only when ordinary output returns. Rewind compression is covered below.
            settings_.setValue("emulation/rewindSeconds", 0);
            while (gb.sgb_startup.phase < 200)
                if (!session.runFrame())
                    return smokeFailure(__FILE__, __LINE__);
            if (detailed && !captures.isEmpty() &&
                !session.grab().save(output.filePath("complete.png")))
                return smokeFailure(__FILE__, __LINE__);
            if (!session.runFrame() || gb.sgb_startup.output.dropped_frames ||
                session.startupAudioFrames_ != uint64_t(210) * batchFrames ||
                session.generatedAudioFrames_ !=
                    session.startupAudioFrames_ + session.handheldAudioFrames_ ||
                !session.handheldAudioFrames_ || session.inputTraceIndex_ != 1 ||
                session.recorder_->framesWritten() != session.handheldAudioFrames_ ||
                !session.stopRecording())
                return smokeFailure(__FILE__, __LINE__);
            cases.append(
                QJsonObject{{"model", int(model)},
                            {"enhanced", bool(enhanced)},
                            {"intro_frames", double(session.startupAudioFrames_)},
                            {"handheld_frames", double(session.handheldAudioFrames_)},
                            {"guest_cycles", double(gb.elapsed_cycles)},
                            {"presentation_cycles", double(cupid_gb_presentation_cycles(&gb))}});
            if (!detailed)
                continue;

            settings_.setValue("emulation/rewindSeconds", 120);
            session.setPaused(true);
            session.resetRewind();
            session.rememberFrame();
            const auto previousAge = session.rewindAgeCycles_;
            if (!session.quickReset() || !cupid_gb_sgb_startup_held(&session.emulator_->gb) ||
                session.rewindAgeCycles_ < previousAge || session.rewindPresentationCycles_ ||
                cupid_gb_presentation_cycles(&session.emulator_->gb))
                return smokeFailure(__FILE__, __LINE__);
            auto &held = session.emulator_->gb;
            session.resetRewind();
            session.rememberFrame();
            for (unsigned frame = 0; frame < 2; ++frame) {
                if (!session.runFrame())
                    return smokeFailure(__FILE__, __LINE__);
                session.rememberFrame();
            }
            if (held.elapsed_cycles || session.rewindStates_.size() != 3 ||
                session.rewindStates_.back().cycles - session.rewindStates_.front().cycles !=
                    140448)
                return smokeFailure(__FILE__, __LINE__);
            PlaybackResult replay;
            if (!session.rewindFrame(&replay) || replay.guestCycles ||
                replay.presentationCycles != 70224 || session.replayedGuestCycles_)
                return smokeFailure(__FILE__, __LINE__);
            if (!session.rewindFrame(&replay) || !session.rewindExhausted_ || replay.guestCycles ||
                replay.presentationCycles != CUPID_GB_SGB_STARTUP_STEP_DOTS ||
                cupid_gb_presentation_cycles(&held) != CUPID_GB_SGB_STARTUP_STEP_DOTS)
                return smokeFailure(__FILE__, __LINE__);
            const auto saved = session.captureState();
            const auto savedHardware = hardwareSnapshotForTest(held);
            const QString statePath = fixtures.filePath("intro.state");
            if (saved.isEmpty() || !writeSgbFixture(statePath, saved))
                return smokeFailure(__FILE__, __LINE__);
            for (const int phase : {10, 11})
                if (cupid_gb_sgb_startup_audio_enqueue(
                        &held.sgb_startup.output, held.apu.output.buf_write, phase, 192000,
                        cupid_gb_base_clock_hz(&held)) != CUPID_GB_SGB_AUDIO_QUEUED)
                    return smokeFailure(__FILE__, __LINE__);
            if (!session.loadStateFile(statePath) || !matchesHardwareForTest(held, savedHardware) ||
                cupid_gb_apu_pending(&held).startup_frames ||
                cupid_gb_apu_pending(&held).handheld_frames)
                return smokeFailure(__FILE__, __LINE__);

            // Accepted presentation debt can publish twice within one real step.
            held.sgb_startup.phase = 198;
            held.display_cycles_since_frame = 140447;
            held.cpu.pc = 0;
            const auto debt = session.captureState();
            if (debt.isEmpty() ||
                cupid_gb_load_state(&held, reinterpret_cast<const uint8_t *>(debt.constData()),
                                    size_t(debt.size())) != CUPID_GB_STATE_OK)
                return smokeFailure(__FILE__, __LINE__);
            const auto beforeIntro = session.startupAudioFrames_;
            if (!session.runFrame() || held.sgb_startup.phase != 200 ||
                session.startupAudioFrames_ - beforeIntro != uint64_t(2) * batchFrames ||
                held.sgb_startup.output.dropped_frames ||
                cupid_gb_apu_pending(&held).startup_frames)
                return smokeFailure(__FILE__, __LINE__);

            session.ensureDebugger();
            session.resetDebuggerTimeline();
            if (!cupid_gb_debugger_breakpoint_add(session.debugger_.get(), -1, 0))
                return smokeFailure(__FILE__, __LINE__);
            held.sgb_startup.phase = 95;
            held.display_cycles_since_frame = 0;
            held.cpu.pc = 0;
            session.debuggerCommand("step");
            session.debuggerTimer_.stop();
            const auto beforeHeldSlice = cupid_gb_presentation_cycles(&held);
            session.debuggerSlice();
            if (!session.debuggerHistory_.empty() || session.debuggerCommandSteps_ ||
                !session.debugger_->skip_breakpoint || held.cpu.pc ||
                cupid_gb_presentation_cycles(&held) <= beforeHeldSlice ||
                held.sgb_startup.phase != 95)
                return smokeFailure(__FILE__, __LINE__);
            session.debuggerCommand("break");
            const auto cancelledClock = cupid_gb_presentation_cycles(&held);
            session.tick();
            if (cupid_gb_presentation_cycles(&held) != cancelledClock ||
                session.debuggerTimer_.isActive())
                return smokeFailure(__FILE__, __LINE__);
            for (const QString command : {"step", "next", "finish"}) {
                session.resetDebuggerTimeline();
                held.sgb_startup.phase = 95;
                held.display_cycles_since_frame = 70224 - CUPID_GB_SGB_STARTUP_STEP_DOTS;
                held.cpu.ime = false;
                held.interrupt_enable = 0;
                held.cpu.pc = command == "step" ? 0 : command == "next" ? 1 : 8;
                held.cpu.sp = 0xfffc;
                held.high_ram[0x7c] = 4;
                held.high_ram[0x7d] = 0;
                session.debuggerCommand(command);
                session.debuggerTimer_.stop();
                for (unsigned slice = 0;
                     slice < 16 && session.debugger_->mode != CUPID_GB_DEBUG_PAUSED; ++slice)
                    session.debuggerSlice();
                const uint64_t expected = command == "step" ? 1 : command == "next" ? 3 : 2;
                if (session.debugger_->mode != CUPID_GB_DEBUG_PAUSED ||
                    session.debuggerCommandSteps_ != expected ||
                    session.debuggerHistory_.size() != expected || held.sgb_startup.phase != 96 ||
                    held.cpu.pc != (command == "step" ? 1 : 4))
                    return smokeFailure(__FILE__, __LINE__);
            }
            held.sgb_startup.phase = 95;
            held.display_cycles_since_frame = 70224 - CUPID_GB_SGB_STARTUP_STEP_DOTS;
            held.cpu.pc = 0;
            const auto beforeBreakpoint = held.elapsed_cycles;
            session.setPaused(false);
            if (!session.runFrame() || session.paused_ || held.sgb_startup.phase != 96 ||
                held.elapsed_cycles != beforeBreakpoint || !session.debuggerHistory_.empty())
                return smokeFailure(__FILE__, __LINE__);
            if (!session.runFrame() || !session.paused_ || held.cpu.pc ||
                held.elapsed_cycles != beforeBreakpoint || !session.debuggerHistory_.empty())
                return smokeFailure(__FILE__, __LINE__);
            // A valid saved clock can be too close to its limit for one more
            // held quantum. That is an execution error, not a failed condition
            // or a retained watchpoint hit, and creates no guest history.
            held.sgb_startup.phase = 95;
            held.sgb_startup.elapsed_cycles =
                UINT64_MAX - held.elapsed_cycles - CUPID_GB_SGB_STARTUP_STEP_DOTS + 1;
            const auto overflow = session.captureState();
            if (overflow.isEmpty() ||
                cupid_gb_load_state(&held, reinterpret_cast<const uint8_t *>(overflow.constData()),
                                    size_t(overflow.size())) != CUPID_GB_STATE_OK)
                return smokeFailure(__FILE__, __LINE__);
            session.debuggerCommand("step");
            session.debuggerTimer_.stop();
            session.debugger_->last_watch_hit.id = 1;
            const auto beforeOverflow = cupid_gb_presentation_cycles(&held);
            session.debuggerSlice();
            const QString executionError =
                QString::fromUtf8(cupid_gb_debugger_stop_name(CUPID_GB_DEBUG_STOP_ERROR));
            if (!session.paused_ || !session.debuggerHistory_.empty() ||
                session.debuggerCommandSteps_ ||
                cupid_gb_presentation_cycles(&held) != beforeOverflow ||
                !session.debuggerWindow_->outputText().contains(executionError) ||
                session.debuggerWindow_->outputText().contains(
                    "Breakpoint condition evaluation failed."))
                return smokeFailure(__FILE__, __LINE__);
        }
    }

    {
        MainWindow portable(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_SGB2,
                            uint64_t(0));
        portable.timer_.stop();
        if (!portable.openRom(cartridges[1]))
            return smokeFailure(__FILE__, __LINE__);
        portable.setPaused(true);
        auto &gb = portable.emulator_->gb;
        gb.sgb.transfer.tiles[32] = 0x80;
        gb.sgb.transfer.map[0] = 1;
        gb.sgb.transfer.palettes[1] = 0x001f;
        gb.sgb.player_count = 4;
        gb.sgb.current_player = 2;
        const QString path = fixtures.filePath("adapter.bess");
        if (!portable.savePortableStateFile(path))
            return smokeFailure(__FILE__, __LINE__);
        gb.sgb.transfer.map[0] = 0;
        gb.sgb.player_count = 1;
        gb.sgb.current_player = 0;
        const QRgb red = (*portable.sgbColorTable_)[0x001f];
        if (!portable.loadStateFile(path) || !portable.paused_ ||
            portable.image_.size() != QSize(CUPID_SGB_SCREEN_WIDTH, CUPID_SGB_SCREEN_HEIGHT) ||
            portable.image_.pixel(0, 0) != red || gb.sgb.player_count != 4 ||
            gb.sgb.current_player != 2 || cupid_gb_sgb_startup_active(&gb))
            return smokeFailure(__FILE__, __LINE__);
    }

    // Compare the high-rate Qt interval against independent small drains after
    // every hardware call, including a completely full ordinary entry queue.
    MainWindow stream(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_CGB, uint64_t(0));
    stream.timer_.stop();
    if (!stream.openRom(cartridges[0]) ||
        !cupid_gb_apu_set_sample_rate(&stream.emulator_->gb, 192000))
        return smokeFailure(__FILE__, __LINE__);
    auto &streamGb = stream.emulator_->gb;
    streamGb.apu.output.buf_write = CUPID_GB_APU_BUF_FRAMES;
    for (unsigned i = 0; i < CUPID_GB_APU_BUF_FRAMES * 2; ++i)
        streamGb.apu.output.buf[i] = int16_t(i * 13u);
    auto frequent = std::make_unique<CupidGb>(streamGb);
    QByteArray expected;
    if (!drainSmallChunks(*frequent, expected))
        return smokeFailure(__FILE__, __LINE__);
    frequent->frame_ready = false;
    const auto start = cupid_gb_presentation_cycles(frequent.get());
    for (unsigned calls = 0; calls < 80000; ++calls) {
        if (!cupid_gb_step(frequent.get()) || !drainSmallChunks(*frequent, expected))
            return smokeFailure(__FILE__, __LINE__);
        if (frequent->frame_ready || cupid_gb_presentation_cycles(frequent.get()) - start >= 70224)
            break;
    }
    const QString highRate = output.filePath("high-rate.wav");
    if (!stream.startRecording(highRate) || !stream.runFrame() || !stream.stopRecording() ||
        readSgbFixture(highRate).mid(44) != expected ||
        stream.generatedAudioFrames_ != uint64_t(expected.size() / 4) ||
        !matchesHardwareForTest(streamGb, hardwareSnapshotForTest(*frequent)))
        return smokeFailure(__FILE__, __LINE__);

    auto ordered = std::make_unique<CupidGb>(streamGb);
    ordered->model = CUPID_GB_MODEL_SGB2;
    cupid_gb_sgb_startup_audio_reset(&ordered->sgb_startup.output, 1);
    ordered->apu.output.buf_write = 4;
    for (unsigned i = 0; i < 8; ++i)
        ordered->apu.output.buf[i] = int16_t(100 + i);
    for (const int phase : {198, 199})
        if (cupid_gb_sgb_startup_audio_enqueue(&ordered->sgb_startup.output, 4, phase, 192000,
                                               cupid_gb_base_clock_hz(ordered.get())) !=
            CUPID_GB_SGB_AUDIO_QUEUED)
            return smokeFailure(__FILE__, __LINE__);
    ordered->apu.output.buf_write = 7;
    for (unsigned i = 8; i < 14; ++i)
        ordered->apu.output.buf[i] = int16_t(300 + i);
    auto orderedControl = std::make_unique<CupidGb>(*ordered);
    QByteArray orderedBytes;
    CoreAudioCollector collector;
    const auto orderedClock = cupid_gb_presentation_cycles(ordered.get());
    if (!drainSmallChunks(*orderedControl, orderedBytes) || !collector.collect(*ordered) ||
        collector.frames() != 6435 || collector.spanCount() != 3 ||
        collector.spans()[0].origin != CUPID_GB_AUDIO_ORIGIN_HANDHELD ||
        collector.spans()[1].origin != CUPID_GB_AUDIO_ORIGIN_SGB_STARTUP ||
        collector.spans()[2].origin != CUPID_GB_AUDIO_ORIGIN_HANDHELD ||
        collector.frames(CUPID_GB_AUDIO_ORIGIN_HANDHELD) != 7 ||
        pcmBytes(collector.samples(), collector.frames()) != orderedBytes ||
        cupid_gb_presentation_cycles(ordered.get()) != orderedClock)
        return smokeFailure(__FILE__, __LINE__);

    CoreAudioCollector full;
    for (unsigned block = 0; block < 8; ++block) {
        ordered->apu.output.buf_write = 2048;
        if (!full.collect(*ordered))
            return smokeFailure(__FILE__, __LINE__);
    }
    ordered->apu.output.buf_write = 1;
    const auto beforeRejected = std::make_unique<CupidGb>(*ordered);
    if (full.frames() != CoreAudioCollector::maximumFrames || full.collect(*ordered) ||
        std::memcmp(beforeRejected.get(), ordered.get(), sizeof(*ordered)))
        return smokeFailure(__FILE__, __LINE__);
    ordered->apu.output.buf_write = 1535;
    if (CoreAudioCollector::needsDrain(*ordered))
        return smokeFailure(__FILE__, __LINE__);
    ordered->apu.output.buf_write = 1536;
    if (!CoreAudioCollector::needsDrain(*ordered) || !CoreAudioCollector::discard(*ordered) ||
        cupid_gb_apu_pending(ordered.get()).handheld_frames)
        return smokeFailure(__FILE__, __LINE__);
    if (audioCheck_ && !nativeChecked)
        return smokeFailure(__FILE__, __LINE__);
    const QJsonObject evidence{{"passed", true},
                               {"cases", cases},
                               {"collector_limit", int(CoreAudioCollector::maximumFrames)},
                               {"ordinary_watermark", int(CUPID_GB_AUDIO_DRAIN_WATERMARK)},
                               {"ordered_two_batch_frames", 6435},
                               {"recorder_excludes_startup", true},
                               {"pure_redraw", true},
                               {"held_debugger", true},
                               {"native_audio", nativeAudio}};
    if (!writeSgbFixture(output.filePath("sgb-workflows.json"),
                         QJsonDocument(evidence).toJson(QJsonDocument::Indented)))
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB SGB startup Qt workflows passed");
    return true;
}

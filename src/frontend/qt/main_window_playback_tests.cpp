/**
 * @file    main_window_playback_tests.cpp
 * @brief   Qt regressions for forward playback, rewind, and audio continuity.
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

#include <QScopeGuard>

bool MainWindow::playbackSmokeTest(const QString &directory) {
    const QVariant savedRewind = settings_.value("emulation/rewindSeconds", 120);
    const bool hadCap = settings_.contains("emulation/turboCap");
    const QVariant savedCap = settings_.value("emulation/turboCap");
    settings_.setValue("emulation/rewindSeconds", 120);
    const auto restoreSettings = qScopeGuard([&] {
        settings_.setValue("emulation/rewindSeconds", savedRewind);
        if (hadCap)
            settings_.setValue("emulation/turboCap", savedCap);
        else
            settings_.remove("emulation/turboCap");
    });
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0xc3);
    rom[0x101] = char(0x50);
    rom[0x102] = char(1);
    const uint8_t program[] = {
        0xf3, 0x31, 0xfe, 0xff, 0x21, 0x00, 0xc0, 0x36, 0x00, 0x3e, 0x10, 0xe0,
        0x00, 0xf0, 0x44, 0xfe, 0x90, 0x38, 0xfa, 0x34, 0xf0, 0x00, 0xea, 0x01,
        0xc0, 0xf0, 0x44, 0xfe, 0x90, 0x30, 0xfa, 0xc3, 0x5d, 0x01,
    };
    std::memcpy(rom.data() + 0x150, program, sizeof(program));
    std::memcpy(rom.data() + 0x134, "PLAYBACK TEST", 13);
    rom[0x147] = char(9);
    rom[0x149] = char(2);
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = char(uint8_t(rom[0x14d]) - uint8_t(rom[index]) - 1);
    const QString path = QDir(directory).filePath("playback.gb");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    file.close();
    MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC);
    session.timer_.stop();
    if (!session.openRom(path))
        return smokeFailure(__FILE__, __LINE__);
    for (unsigned index = 0; index < 10; ++index)
        if (!session.runFrame())
            return smokeFailure(__FILE__, __LINE__);
    const QByteArray warm = session.captureState();
    const uint8_t warmTag = cupid_gb_read_u8(&session.emulator_->gb, 0xc000);
    const auto prepare = [&](unsigned history) {
        session.setPaused(false);
        session.turbo_ = session.slowHeld_ = false;
        session.speedSteps_ = 16;
        if (!session.restoreState(warm, false))
            return false;
        session.resetRewind();
        session.present(Presentation::Reset);
        for (unsigned index = 0; index < history; ++index)
            if (!session.runFrame())
                return false;
        return true;
    };
    if (!prepare(8))
        return smokeFailure(__FILE__, __LINE__);
    const uint8_t before = cupid_gb_read_u8(&session.emulator_->gb, 0xc000);
    const uint64_t audio = session.generatedAudioFrames_;
    if (!session.rewindFrame() || cupid_gb_read_u8(&session.emulator_->gb, 0xc000) != before ||
        session.generatedAudioFrames_ <= audio || session.rewindStates_.size() != 7) {
        qCritical(
            "Rewind replay failed: tag=%u expected=%u generated=%llu previous=%llu history=%zu",
            cupid_gb_read_u8(&session.emulator_->gb, 0xc000), before,
            qulonglong(session.generatedAudioFrames_), qulonglong(audio),
            session.rewindStates_.size());
        return smokeFailure(__FILE__, __LINE__);
    }

    struct Trace {
        unsigned history;
        bool turbo;
        std::initializer_list<unsigned> tags;
    };

    const Trace traces[] = {
        {0, false, {0}},
        {0, true, {0}},
        {1, false, {1}},
        {1, true, {1}},
        {2, false, {2, 2}},
        {2, true, {1}},
        {3, false, {3, 2, 2}},
        {3, true, {2, 2}},
        {5, false, {5, 4, 3, 2, 2}},
        {5, true, {4, 2, 2}},
        {8, false, {8, 7, 6, 5, 4, 3, 2, 2}},
        {8, true, {7, 5, 3, 1}},
    };
    for (const auto &trace : traces) {
        if (!prepare(trace.history))
            return smokeFailure(__FILE__, __LINE__);
        session.turbo_ = trace.turbo;
        session.display_->messages.setEnabled(true);
        for (const unsigned tag : trace.tags) {
            const size_t count = session.rewindStates_.size();
            const unsigned consumed = trace.turbo ? 3 : 2;
            const bool frame = count >= consumed;
            const uint64_t framesBefore = session.replayedFrames_;
            const uint64_t audioBefore = session.generatedAudioFrames_;
            const uint64_t timeline = session.timelineId_;
            session.display_->messages.show("Replay interval");
            const unsigned messageAge = session.display_->messages.remainingFrames();
            session.currentDisplayFrame_.fill(qRgb(71, 33, 95));
            const QImage priorImage = session.currentDisplayFrame_;
            PlaybackResult result;
            if (!session.rewindFrame(&result) ||
                cupid_gb_read_u8(&session.emulator_->gb, 0xc000) != warmTag + tag ||
                session.rewindStates_.size() != (frame ? count - consumed + 1 : 0) ||
                session.rewindExhausted_ == frame ||
                session.replayedFrames_ != framesBefore + frame ||
                result.timeline != timeline + bool(count) || !result.presentationCycles ||
                result.guestCycles != result.presentationCycles ||
                (frame && (result.presentationCycles < 70000 || result.presentationCycles > 70248 ||
                           session.generatedAudioFrames_ <= audioBefore ||
                           session.previousDisplayFrame_ != priorImage)) ||
                (!frame && (result.presentationCycles > 24 ||
                            session.display_->messages.remainingFrames() != messageAge)))
                return smokeFailure(__FILE__, __LINE__);
            qsizetype bytes = 0;
            for (const auto &snapshot : session.rewindStates_)
                bytes += snapshot.compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
            if (bytes != session.rewindBytes_)
                return smokeFailure(__FILE__, __LINE__);
        }
        const QByteArray exhausted = session.captureState();
        if (!session.rewindExhausted_ || session.rewindFrame() ||
            session.captureState() != exhausted)
            return smokeFailure(__FILE__, __LINE__);
        session.rewind_ = true;
        session.nextFrame_ = 0;
        session.tick();
        if (session.captureState() != exhausted)
            return smokeFailure(__FILE__, __LINE__);
        session.reconcileControllerInput();
        if (session.rewind_ || session.rewindExhausted_)
            return smokeFailure(__FILE__, __LINE__);
        session.nextFrame_ = 0;
        session.tick();
        if (session.captureState() == exhausted)
            return smokeFailure(__FILE__, __LINE__);
    }
    session.display_->messages.setEnabled(false);

    // The replay sees the currently held button, not the button restored from history.
    if (!prepare(5))
        return smokeFailure(__FILE__, __LINE__);
    session.physicalJoypad_ = 0xef;
    session.slowHeld_ = true;
    session.speedSteps_ = 13;
    session.rapidHeld_[1] = true;
    session.rapidPhase_[1] = 1;
    const size_t traceIndex = session.inputTraceIndex_;
    session.inputTrace_ = {{0, 0xff}};
    if (!session.rewindFrame() || (cupid_gb_read_u8(&session.emulator_->gb, 0xc001) & 1) ||
        session.speedSteps_ != 12 || session.rapidPhase_[1] != 2 ||
        session.inputTraceIndex_ != traceIndex)
        return smokeFailure(__FILE__, __LINE__);
    session.inputTrace_.clear();
    session.inputTraceIndex_ = 0;

    // Fractional slow/Turbo factors apply to positive replay work at every cap.
    for (const unsigned cap : {6u, 7u, 16u, 0u}) {
        if (!prepare(8))
            return smokeFailure(__FILE__, __LINE__);
        session.setTurboCap(cap);
        session.turbo_ = session.rewind_ = session.slowHeld_ = true;
        session.speedSteps_ = 13;
        session.resetPacing();
        if (!cap)
            session.nextFrame_ = session.clock_.nsecsElapsed() + INT64_C(1000000000);
        const qint64 deadline = session.nextFrame_;
        const uint64_t cycles = session.replayedGuestCycles_;
        const uint64_t beforeGuest = session.emulator_->gb.elapsed_cycles;
        session.tick();
        const uint64_t replayCycles = session.replayedGuestCycles_ - cycles;
        const uint64_t denominator =
            uint64_t(cupid_gb_base_clock_hz(&session.emulator_->gb)) * 13 * cap;
        if (session.paused_ || replayCycles < 70000 || replayCycles > 70248 ||
            session.emulator_->gb.elapsed_cycles >= beforeGuest || session.speedSteps_ != 12 ||
            (cap && session.nextFrame_ !=
                        deadline + qint64(replayCycles * UINT64_C(64000000000) / denominator)) ||
            (!cap && session.nextFrame_ >= deadline))
            return smokeFailure(__FILE__, __LINE__);
        if (cap) {
            session.nextFrame_ = session.clock_.nsecsElapsed() + INT64_C(1000000000);
            const QByteArray waiting = session.captureState();
            session.tick();
            if (session.captureState() != waiting)
                return smokeFailure(__FILE__, __LINE__);
        }
    }
    if (!prepare(5))
        return smokeFailure(__FILE__, __LINE__);
    session.turbo_ = session.rewind_ = true;
    session.setTurboCap(0);
    const QByteArray gated = session.captureState();
    for (unsigned gate = 0; gate < 3; ++gate) {
        QScopedValueRollback<bool> pause(session.paused_, gate == 0);
        QScopedValueRollback<bool> operation(session.stateOperation_, gate == 1);
        QScopedValueRollback<unsigned> menu(session.menuDepth_, gate == 2 ? 1 : 0);
        session.tick();
        if (session.captureState() != gated)
            return smokeFailure(__FILE__, __LINE__);
    }

    // A rejected compressed state must leave the core, ring, battery and deadline intact.
    for (const bool wrongCartridge : {false, true}) {
        if (!prepare(5))
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_write_u8(&session.emulator_->gb, 0xa017, 0x5a);
        session.watchBattery();
        if (!session.flushBattery())
            return smokeFailure(__FILE__, __LINE__);
        session.saveTimer_.stop();
        QByteArray invalid("invalid state");
        if (wrongCartridge) {
            QByteArray otherRom = rom;
            otherRom[0x134] = 'X';
            const QString otherPath = QDir(directory).filePath("playback-other.gb");
            QFile otherFile(otherPath);
            if (!otherFile.open(QIODevice::WriteOnly) ||
                otherFile.write(otherRom) != otherRom.size())
                return smokeFailure(__FILE__, __LINE__);
            otherFile.close();
            MainWindow other(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC);
            other.timer_.stop();
            if (!other.openRom(otherPath))
                return smokeFailure(__FILE__, __LINE__);
            invalid = other.captureState();
        }
        auto &target = session.rewindStates_[session.rewindStates_.size() - 2].compressed;
        session.rewindBytes_ -= target.capacity();
        target = qCompress(invalid);
        session.rewindBytes_ += target.capacity();
        const QByteArray state = session.captureState();
        const auto history = session.rewindStates_;
        const qsizetype bytes = session.rewindBytes_;
        const qint64 deadline = session.nextFrame_;
        const uint64_t timeline = session.timelineId_;
        const uint64_t produced = session.generatedAudioFrames_;
        QFile battery(session.savePath_);
        if (!battery.open(QIODevice::ReadOnly))
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray saved = battery.readAll();
        battery.close();
        if (session.rewindFrame() || !session.rewindFailed_ || session.captureState() != state ||
            session.rewindStates_.size() != history.size() || session.rewindBytes_ != bytes ||
            session.timelineId_ != timeline || session.nextFrame_ != deadline ||
            session.generatedAudioFrames_ != produced || session.batteryDirty_ ||
            session.saveTimer_.isActive() || !battery.open(QIODevice::ReadOnly) ||
            battery.readAll() != saved)
            return smokeFailure(__FILE__, __LINE__);
        for (size_t index = 0; index < history.size(); ++index)
            if (session.rewindStates_[index].compressed != history[index].compressed)
                return smokeFailure(__FILE__, __LINE__);
    }

    if (!prepare(5))
        return smokeFailure(__FILE__, __LINE__);
    const QString recording = QDir(directory).filePath("playback-forward.wav");
    if (!session.startRecording(recording) || !session.runFrame())
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t recorded = session.recorder_->framesWritten();
    if (!recorded || !session.rewindFrame() || session.recorder_->framesWritten() != recorded ||
        !session.runFrame() || session.recorder_->framesWritten() <= recorded ||
        !session.stopRecording())
        return smokeFailure(__FILE__, __LINE__);

    if (!prepare(5))
        return smokeFailure(__FILE__, __LINE__);
    session.ensureDebugger();
    for (uint16_t address = 0x150; address < 0x190; ++address)
        if (!cupid_gb_debugger_breakpoint_add(session.debugger_.get(), -1, address))
            return smokeFailure(__FILE__, __LINE__);
    const uint64_t stoppedFrames = session.replayedFrames_;
    PlaybackResult stopped;
    if (!session.rewindFrame(&stopped) || !session.paused_ || stopped.guestCycles ||
        stopped.presentationCycles || session.replayedFrames_ != stoppedFrames ||
        session.rewindStates_.size() != 3)
        return smokeFailure(__FILE__, __LINE__);
    cupid_gb_debugger_cleanup(session.debugger_.get());
    session.debugger_.reset();

    // Exercise the actual conversion/filter boundary without requiring a device.
    // Whole-frame limits must work for both integer and float stereo formats.
    for (const bool floating : {false, true}) {
        QAudioFormat format;
        format.setSampleRate(floating ? 48000 : 44100);
        format.setChannelCount(2);
        format.setSampleFormat(floating ? QAudioFormat::Float : QAudioFormat::Int16);
        session.audioConverter_ = std::make_unique<AudioConverter>(format);
        session.audioConverter_->setInputRate(format.sampleRate());
        session.audioBuffer_ = std::make_shared<AudioBuffer>(format.bytesPerFrame());
        session.turbo_ = true;
        session.turboCapQuarters_ = 7;
        const size_t period = size_t(format.sampleRate()) / 8;
        std::vector<int16_t> samples((period * 3 + 123) * 2);
        for (size_t index = 0; index < samples.size(); ++index)
            samples[index] = int16_t(index * 37);
        const auto converted =
            session.convertPlaybackAudio(samples.data(), samples.size() / 2, format);
        if (converted.size() != qsizetype((period + 1) * format.bytesPerFrame()) ||
            !session.turboWindowDroppedFrames_ || !session.turboCapacityDroppedFrames_ ||
            session.turboAudioWindow_.sampleRate() != unsigned(format.sampleRate()))
            return smokeFailure(__FILE__, __LINE__);
        session.audioBuffer_->append(converted.constData(), converted.size());
        const auto phase = session.turboAudioWindow_.phase();
        if (!session.convertPlaybackAudio(samples.data(), 100, format).isEmpty() ||
            session.turboAudioWindow_.phase() != (phase + 100) % period)
            return smokeFailure(__FILE__, __LINE__);
        session.audioBuffer_->clear();
        const QByteArray threshold(qsizetype(period * format.bytesPerFrame()), char(0));
        session.audioBuffer_->append(threshold.constData(), threshold.size());
        const auto finalFrame = session.convertPlaybackAudio(samples.data(), period, format);
        if (finalFrame.size() != format.bytesPerFrame())
            return smokeFailure(__FILE__, __LINE__);
        session.audioBuffer_->append(finalFrame.constData(), finalFrame.size());
        if (!session.convertPlaybackAudio(samples.data(), period, format).isEmpty())
            return smokeFailure(__FILE__, __LINE__);
        session.turbo_ = false;
        const auto normalPhase = session.turboAudioWindow_.phase();
        const auto normal = session.convertPlaybackAudio(samples.data(), 100, format);
        if (normal.size() != 100 * format.bytesPerFrame() ||
            session.turboAudioWindow_.phase() != normalPhase)
            return smokeFailure(__FILE__, __LINE__);
        session.clearAudio();
        if (session.turboAudioWindow_.phase() != normalPhase ||
            !session.convertPlaybackAudio(samples.data(), 100, QAudioFormat()).isEmpty() ||
            session.turboAudioWindow_.phase() != normalPhase)
            return smokeFailure(__FILE__, __LINE__);

        session.setPaused(false);
        for (const unsigned mode : {0u, 1u}) {
            for (const bool duringCallback : {false, true}) {
                session.keyboardSpecialHeld_[mode] = true;
                session.reconcileControllerInput();
                session.audioBuffer_->append(normal.constData(), normal.size());
                const auto heldPhase = session.turboAudioWindow_.phase();
                const auto heldCycles = session.emulator_->gb.elapsed_cycles;
                QEvent inactive(QEvent::WindowDeactivate);
                if (duringCallback) {
                    {
                        QScopedValueRollback<bool> callback(session.stateOperation_, true);
                        QApplication::sendEvent(&session, &inactive);
                        if (!session.audioModeResetPending_ ||
                            !session.audioBuffer_->bytesAvailable())
                            return smokeFailure(__FILE__, __LINE__);
                    }
                    session.pumpAudio();
                } else {
                    QApplication::sendEvent(&session, &inactive);
                }
                if (session.turbo_ || session.rewind_ || session.audioModeResetPending_ ||
                    session.audioBuffer_->bytesAvailable() ||
                    session.turboAudioWindow_.phase() != heldPhase ||
                    session.emulator_->gb.elapsed_cycles != heldCycles)
                    return smokeFailure(__FILE__, __LINE__);
            }
        }
        // Keyboard focus loss preserves a controller mode still allowed by its
        // delivery policy; its later release owns the mode transition.
        session.controllerOutput_.held[0] = true;
        session.reconcileControllerInput();
        session.audioBuffer_->append(normal.constData(), normal.size());
        QEvent inactive(QEvent::WindowDeactivate);
        QApplication::sendEvent(&session, &inactive);
        if (!session.turbo_ || session.audioBuffer_->bytesAvailable() != normal.size())
            return smokeFailure(__FILE__, __LINE__);
        session.controllerOutput_.held[0] = false;
        session.reconcileControllerInput();
        if (session.turbo_ || session.audioBuffer_->bytesAvailable())
            return smokeFailure(__FILE__, __LINE__);
    }
    qInfo("CupidGB rewind replay, mode pacing, transactional history and Turbo audio workflows "
          "passed");
    return true;
}

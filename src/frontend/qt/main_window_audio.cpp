/**
 * @file    main_window_audio.cpp
 * @brief   Qt audio output, listening preferences, and recording lifecycle.
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

void MainWindow::applyAudioPolicy(CupidGb *gb) const {
    cupid_gb_apu_set_filter_mode(gb, configuredAudioFilter(settings_));
    cupid_gb_apu_set_interference_volume(gb, double(configuredAudioInterference(settings_)) / 100);
    for (size_t channel = 0; channel < audioChannelActions_.size(); ++channel)
        cupid_gb_apu_set_channel_muted(gb, unsigned(channel),
                                       !audioChannelActions_[channel]->isChecked());
}

bool MainWindow::collectAudio(CoreAudioCollector &collected) {
    if (!emulator_)
        return false;
    const auto frames = collected.frames();
    const auto handheld = collected.frames(CUPID_GB_AUDIO_ORIGIN_HANDHELD);
    const auto startup = collected.frames(CUPID_GB_AUDIO_ORIGIN_SGB_STARTUP);
    const bool complete = collected.collect(emulator_->gb);
    generatedAudioFrames_ += collected.frames() - frames;
    handheldAudioFrames_ += collected.frames(CUPID_GB_AUDIO_ORIGIN_HANDHELD) - handheld;
    startupAudioFrames_ += collected.frames(CUPID_GB_AUDIO_ORIGIN_SGB_STARTUP) - startup;
    return complete;
}

bool MainWindow::discardCoreAudio() {
    if (emulator_ && CoreAudioCollector::discard(emulator_->gb))
        return true;
    setPaused(true);
    reportError("Audio output could not be preserved. Emulation is paused.");
    return false;
}

void MainWindow::recordAudio(const CoreAudioCollector &collected) {
    if (!recorder_ || !recorder_->active())
        return;
    // Adapter introduction and its silent lead-in are playback only. Skipping
    // those spans must not write replacement silence into the handheld recording.
    for (size_t i = 0; i < collected.spanCount(); ++i) {
        const auto &span = collected.spans()[i];
        if (span.origin != CUPID_GB_AUDIO_ORIGIN_HANDHELD)
            continue;
        if (!recorder_->append(collected.samples() + span.firstFrame * 2u, span.frames)) {
            const QString error = recorder_->errorString();
            if (stopRecording())
                reportError("Audio recording stopped: " + error);
            return;
        }
    }
}

void MainWindow::setupAudio() {
    // Some audio backends dispatch Qt events while opening or closing a stream.
    // Guest execution must not reenter a pause, load, or rewind transition.
    QScopedValueRollback<bool> operation(stateOperation_, true);
    stopAudio();
    audioConverter_.reset();
    audioBuffer_ = std::make_shared<AudioBuffer>();
    const QByteArray requested = settings_.value("audio/outputDevice").toByteArray();
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    audioDeviceFallback_ = false;
    if (!requested.isEmpty()) {
        const auto available = QMediaDevices::audioOutputs();
        const auto match = std::find_if(
            available.cbegin(), available.cend(),
            [&requested](const QAudioDevice &candidate) { return candidate.id() == requested; });
        if (match == available.cend())
            audioDeviceFallback_ = true;
        else
            device = *match;
    }
    activeAudioDeviceId_ = device.id();
    activeAudioDeviceName_ = device.description();
    if (isolated_ && !audioCheck_)
        return;
    QAudioFormat format;
    format.setSampleRate(CUPID_GB_APU_SAMPLE_RATE);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isNull() && !device.isFormatSupported(format))
        format = device.preferredFormat();
    if (device.isNull() || !format.isValid() || !device.isFormatSupported(format)) {
        ++audioErrorEvents_;
        statusBar()->showMessage("No usable audio output device is available");
        return;
    }
    audioBuffer_ = std::make_shared<AudioBuffer>(format.bytesPerFrame());
    audioConverter_ = std::make_unique<AudioConverter>(format);
    turboAudioWindow_.setSampleRate(unsigned(format.sampleRate()));
    audioQueueLimit_ = format.bytesForDuration(50000);
    audioPrimeBytes_ = format.bytesForDuration(30000);
    audioPriming_ = true;
    audio_ = createAudioSink<QAudioSink>(device, format);
    connect(audio_.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::IdleState)
            audioPriming_ = true;
        if (state == QAudio::IdleState && telemetryClock_.isValid() &&
            telemetryClock_.elapsed() > 250)
            ++audioIdleEvents_;
        if (state == QAudio::StoppedState && audio_ && audio_->error() != QAudio::NoError)
            ++audioErrorEvents_;
    });
    audio_->setBufferSize(audioQueueLimit_);
    audio_->setVolume(
        muteAction_->isChecked() ? 0.0f : settings_.value("audio/volume", 100).toFloat() / 100);
    if (audioDeviceFallback_)
        statusBar()->showMessage("Selected audio output is unavailable; using System Default",
                                 5000);
}

void MainWindow::stopAudio() {
    QScopedValueRollback<bool> operation(stateOperation_, true);
    audioModeResetPending_ = false;
    audioDevice_ = nullptr;
    audioPriming_ = true;
    // Detach immediately, but retain the old backend while a reentrant start/write
    // still uses its device. The final lease resets the obsolete stream, not drains it.
    retireAudioSink(audio_, this);
}

void MainWindow::clearAudio() {
    audioModeResetPending_ = false;
    audioPriming_ = true;
    if (audio_) {
        // A new sink owns a fresh backend stream. Rapid reuse after several
        // pause/load/rewind resets can otherwise leave the Windows stream idle.
        setupAudio();
        return;
    }
    if (audioConverter_)
        audioConverter_->reset();
    if (audioBuffer_)
        audioBuffer_->clear();
}

void MainWindow::pumpAudio() {
    const auto finishModeChange = qScopeGuard([this] {
        if (audioModeResetPending_ && !stateOperation_) {
            clearAudio();
            applyHeldButtons();
        }
    });
    QScopedValueRollback<bool> operation(stateOperation_, true);
    // Backend event dispatch can replace the session queue while a write is active.
    const auto buffer = audioBuffer_;
    const auto sink = audio_;
    if (audio_ && !paused_ && !menuInputSuspended_) {
        // The event loop produces one guest frame at a time. Accumulate real samples
        // before starting the sink so frame-sized writes tolerate host scheduling jitter.
        // The 30 ms target stays below the 50 ms execution-backpressure limit.
        if (audioPriming_) {
            if (buffer->bytesAvailable() < audioPrimeBytes_)
                return;
            // Start only when generated audio is ready. Repeated reset/start calls
            // with an empty queue can leave the Windows backend idle after a load.
            if (!audioDevice_) {
                QIODevice *device = sink->start();
                if (sink != audio_ || buffer != audioBuffer_ || paused_)
                    return;
                audioDevice_ = device;
            }
            QIODevice *const device = audioDevice_;
            const auto state = sink->state();
            // A failed start can leave the backend without a native client.
            // Capacity is only valid for a started, writable stream.
            if (!device || (state != QAudio::ActiveState && state != QAudio::IdleState))
                return;
            const qint64 capacity = sink->bytesFree();
            if (sink != audio_ || buffer != audioBuffer_ || device != audioDevice_ || paused_ ||
                menuDepth_ || menuInputSuspended_)
                return;
            const qint64 written = buffer->writeTo(device, capacity);
            if (written > 0 && sink == audio_ && buffer == audioBuffer_ && !paused_) {
                audioPriming_ = false;
                ++audioPrimeEvents_;
            }
            return;
        }
        if (audioDevice_) {
            QIODevice *const device = audioDevice_;
            const auto state = sink->state();
            if (state != QAudio::ActiveState && state != QAudio::IdleState)
                return;
            const qint64 capacity = sink->bytesFree();
            if (sink != audio_ || buffer != audioBuffer_ || device != audioDevice_ || paused_ ||
                menuDepth_ || menuInputSuspended_)
                return;
            buffer->writeTo(device, capacity);
        }
    }
}

QByteArray MainWindow::convertPlaybackAudio(const int16_t *samples, size_t frames,
                                            const QAudioFormat &format) {
    if (!audioConverter_ || !audioBuffer_ || !format.isValid() || format.bytesPerFrame() <= 0 ||
        !turboAudioWindow_.setSampleRate(unsigned(format.sampleRate())))
        return {};
    QByteArray converted = audioConverter_->convert(samples, frames);
    const size_t frameBytes = size_t(format.bytesPerFrame());
    size_t capacity = TurboAudioWindow::unlimitedFrames;
    if (turbo_) {
        // The threshold frame is accepted too. This bounds the pending whole
        // host frames; the audio backend has its own separate buffering.
        const size_t maximum = size_t(format.sampleRate()) / 8 + 1;
        const size_t queued = size_t(audioBuffer_->bytesAvailable()) / frameBytes;
        capacity = queued < maximum ? maximum - queued : 0;
    }
    const auto filtered = turboAudioWindow_.filter(converted.data(), size_t(converted.size()),
                                                   frameBytes, turbo_, turboCapQuarters_, capacity);
    if (filtered.status != TurboAudioWindow::Status::Ok)
        return {};
    converted.truncate(qsizetype(filtered.outputFrames * frameBytes));
    turboWindowDroppedFrames_ += filtered.windowDroppedFrames;
    turboCapacityDroppedFrames_ += filtered.capacityDroppedFrames;
    return converted;
}

bool MainWindow::startRecording(const QString &path) {
    if (!emulator_)
        return false;
    const QFileInfo destination(path);
    if (destination.suffix().compare("wav", Qt::CaseInsensitive) != 0) {
        reportError("Choose a .wav file for the audio recording.");
        return false;
    }
    auto identity = [](const QString &name) {
        const QFileInfo info(name);
        return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    };
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    QStringList protectedPaths = {romPath_, savePath_};
    for (int slot = 0; slot < 10; ++slot)
        protectedPaths << slotPath(slot) << slotPath(slot, true);
    for (const auto &protectedPath : protectedPaths) {
        if (identity(path).compare(identity(protectedPath), pathCase) == 0) {
            reportError(
                "Record to a separate file. Cartridge, battery, and state files are protected.");
            return false;
        }
    }
    if (!recorder_)
        recorder_ = std::make_unique<WaveRecorder>();
    if (!recorder_->start(path, cupid_gb_apu_get_sample_rate(&emulator_->gb))) {
        reportError("Cannot start audio recording: " + recorder_->errorString());
        return false;
    }
    refreshActions();
    statusBar()->showMessage("Recording forward audio at the core sample rate", 3000);
    notifyOnScreen("Audio recording started");
    return true;
}

bool MainWindow::stopRecording() {
    if (!recorder_ || !recorder_->active())
        return true;
    const bool finished = recorder_->finish();
    refreshActions();
    if (!finished) {
        reportError("Cannot finalize audio recording: " + recorder_->errorString());
        return false;
    }
    statusBar()->showMessage("Audio recording saved", 3000);
    notifyOnScreen("Audio recording ended");
    return true;
}

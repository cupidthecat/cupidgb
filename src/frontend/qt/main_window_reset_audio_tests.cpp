/**
 * @file    main_window_reset_audio_tests.cpp
 * @brief   Native audio sink recovery, recording, and rewind checks across reset actions.
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

#include <QJsonArray>
#include <QScopeGuard>

extern "C" {
#include "../../gb/audio_state.h"
}

bool MainWindow::resetAudioSmokeTest(const QString &directory) {
    if (!audioCheck_ || !emulator_ || paused_ || !audio_ || recorder_->active())
        return smokeFailure(__FILE__, __LINE__);
    const bool running = timer_.isActive();
    timer_.stop();
    const auto cleanup = qScopeGuard([&] {
        if (recorder_->active())
            stopRecording();
        if (running && !failed_)
            timer_.start();
    });
    releaseKeys(false);
    if (turbo_ || rewind_ || slowHeld_ || menuDepth_)
        return smokeFailure(__FILE__, __LINE__);
    const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    const QDir output(captureDirectory.isEmpty() ? directory : captureDirectory);
    if (!QDir().mkpath(output.path()))
        return smokeFailure(__FILE__, __LINE__);
    const QString recording = output.filePath("reset-audio-workflows.wav");
    if (QFileInfo::exists(recording) || !startRecording(recording))
        return smokeFailure(__FILE__, __LINE__);
    QJsonArray observations;
    // Evidence output can block while the manually driven stream is draining.
    // Flush stored observations only after the consecutive playback proofs.
    const auto publishObservations = qScopeGuard([&] {
        for (const auto &row : observations)
            qInfo("CupidGB native reset audio observation: %s",
                  QJsonDocument(row.toObject()).toJson(QJsonDocument::Compact).constData());
    });
    const auto snapshot = [&] {
        const auto state = audio_ ? audio_->state() : QAudio::StoppedState;
        // Buffer capacity is only available after the backend opens its stream.
        // A reset creates a stopped sink before a writable device exists.
        const bool capacityAvailable =
            audio_ && audioDevice_ && (state == QAudio::ActiveState || state == QAudio::IdleState);
        return QJsonObject{
            {"sink", bool(audio_)},
            {"device", bool(audioDevice_)},
            {"state", audio_ ? int(state) : -1},
            {"error", audio_ ? int(audio_->error()) : -1},
            {"priming", audioPriming_},
            {"prime_events", double(audioPrimeEvents_)},
            {"processed_us", audio_ ? double(audio_->processedUSecs()) : 0},
            {"bytes_free", capacityAvailable ? double(audio_->bytesFree()) : 0},
            {"capacity_available", capacityAvailable},
            {"host_rate", audio_ ? audio_->format().sampleRate() : 0},
            {"host_frame_bytes", audio_ ? audio_->format().bytesPerFrame() : 0},
            {"queue", audioBuffer_ ? audioBuffer_->telemetry() : QJsonObject{}},
            {"recording", recorder_ && recorder_->active()},
            {"recorded_frames", recorder_ ? double(recorder_->framesWritten()) : 0},
            {"recording_rate", recorder_ ? int(recorder_->sampleRate()) : 0},
            {"core_rate", emulator_ ? int(cupid_gb_apu_get_sample_rate(&emulator_->gb)) : 0},
            {"guest_cycles", emulator_ ? double(emulator_->gb.elapsed_cycles) : 0},
            {"generated_frames", double(generatedAudioFrames_)},
            {"timer_active", timer_.isActive()},
            {"paused", paused_},
            {"failed", failed_},
            {"history", double(rewindStates_.size())},
            {"history_age", double(rewindAgeCycles_)},
        };
    };
    const auto record = [&](const QString &phase, const QJsonObject &details) {
        QJsonObject row{{"phase", phase}, {"details", details}};
        observations.append(row);
    };
    constexpr qint64 budgetMs = 500;
    constexpr unsigned maximumFrames = 48;
    constexpr unsigned maximumPrimeFrames = 8;
    const auto ready = [&](const QString &phase) {
        QElapsedTimer elapsed;
        elapsed.start();
        const QPointer<QAudioSink> sink = audio_.get();
        const auto buffer = audioBuffer_;
        const auto machine = emulator_.get();
        const uint64_t session = sessionId_, timeline = timelineId_;
        const uint32_t recordingRate = recorder_ ? recorder_->sampleRate() : 0;
        bool sawActive = sink && sink->state() == QAudio::ActiveState;
        bool activeUninterrupted = true;
        QMetaObject::Connection stateConnection;
        if (sink) {
            stateConnection =
                connect(sink.data(), &QAudioSink::stateChanged, this, [&](QAudio::State state) {
                    if (state == QAudio::ActiveState)
                        sawActive = true;
                    else if (sawActive)
                        activeUninterrupted = false;
                });
        }
        const auto disconnectObservation = qScopeGuard([&] { disconnect(stateConnection); });
        QString failure;
        const auto reject = [&](const char *reason) {
            if (failure.isEmpty())
                failure = QString::fromLatin1(reason);
            return false;
        };
        const auto sameSession = [&] {
            return sink && audio_.get() == sink.data() && buffer && audioBuffer_ == buffer &&
                   machine && emulator_.get() == machine && sessionId_ == session &&
                   timelineId_ == timeline && recorder_ && recorder_->active() &&
                   recorder_->sampleRate() == recordingRate && !timer_.isActive() && !failed_ &&
                   !paused_ && !turbo_ && !rewind_ && !menuDepth_ && !menuInputSuspended_;
        };
        const auto active = [&] {
            return sameSession() && audioDevice_ && !audioPriming_ && activeUninterrupted &&
                   sink->state() == QAudio::ActiveState && sink->error() == QAudio::NoError;
        };
        unsigned generated = 0, primeFrames = 0;
        while (!active() && failure.isEmpty()) {
            // A new stream may prime once. An already-started stream must not
            // hide starvation by entering another prime during this proof.
            if (!sameSession() || audioDevice_ || sawActive ||
                sink->state() != QAudio::StoppedState || sink->error() != QAudio::NoError ||
                !audioPriming_)
                reject("Native stream is not ready for its initial prime");
            else if (primeFrames >= maximumPrimeFrames || elapsed.elapsed() >= budgetMs)
                reject("Initial native prime exceeded its frame or time bound");
            else {
                ++primeFrames;
                ++generated;
                if (!runFrame())
                    reject("Guest execution failed while priming native audio");
            }
        }
        const auto before = snapshot();
        const double accepted =
            before["queue"].toObject()["host_frames_written_to_sink"].toDouble();
        const bool primed = failure.isEmpty() && active() &&
                            before["capacity_available"].toBool() && accepted > 0 &&
                            accepted == double(uint64_t(accepted));
        const QPointer<QIODevice> device = audioDevice_;
        const uint64_t primeEvents = audioPrimeEvents_;
        const int hostRate = before["host_rate"].toInt();
        const int frameBytes = before["host_frame_bytes"].toInt();
        const auto healthy = [&] {
            return active() && device && audioDevice_ == device.data() &&
                   audioPrimeEvents_ == primeEvents && sink->format().sampleRate() == hostRate &&
                   sink->format().bytesPerFrame() == frameBytes &&
                   cupid_gb_apu_get_sample_rate(&emulator_->gb) == recordingRate;
        };
        bool capacityProgress = false, acceptedProgress = false, processedProgress = false;
        bool guestRecordingProgress = false;
        QJsonObject consumptionProof;
        if (!primed || frameBytes <= 0 || hostRate <= 0 || audioPrimeBytes_ <= 0)
            reject("Native audio prime or format is invalid");
        while (failure.isEmpty() && elapsed.elapsed() < budgetMs) {
            // Keep the backend fed with guest PCM while its counters settle.
            // A reserve below the normal queue limit avoids an unbounded producer.
            while ((generated == primeFrames || buffer->bytesAvailable() < audioPrimeBytes_) &&
                   failure.isEmpty()) {
                if (!healthy())
                    reject("Native stream changed or underrun occurred before a refill");
                else if (generated >= maximumFrames || elapsed.elapsed() >= budgetMs)
                    reject("Native audio refill exceeded its frame or time bound");
                else {
                    ++generated;
                    if (!runFrame())
                        reject("Guest execution failed while refilling native audio");
                }
            }
            if (!failure.isEmpty())
                break;
            if (!healthy()) {
                reject("Native stream changed or underrun occurred after a refill");
                break;
            }
            pumpAudio();
            if (!healthy()) {
                reject("Native stream changed or underrun occurred during a write");
                break;
            }
            // No writes or guest steps occur in this interval. Capacity returning
            // by a whole host frame proves consumption, independently of whether
            // the backend reports accepted or played PCM in processedUSecs().
            const auto consumptionBefore = snapshot();
            const qint64 waitStarted = elapsed.elapsed();
            QEventLoop consumed;
            QTimer::singleShot(1, Qt::PreciseTimer, &consumed, &QEventLoop::quit);
            consumed.exec();
            const auto consumptionAfter = snapshot();
            if (!healthy()) {
                reject("Native stream changed or underrun occurred during consumption");
                break;
            }
            const auto queuedBefore = consumptionBefore["queue"].toObject();
            const auto queuedAfter = consumptionAfter["queue"].toObject();
            const bool noWrite =
                queuedBefore["host_frames_written_to_sink"] ==
                    queuedAfter["host_frames_written_to_sink"] &&
                queuedBefore["remaining_queued_bytes"] == queuedAfter["remaining_queued_bytes"] &&
                consumptionBefore["generated_frames"] == consumptionAfter["generated_frames"] &&
                consumptionBefore["guest_cycles"] == consumptionAfter["guest_cycles"] &&
                consumptionBefore["recorded_frames"] == consumptionAfter["recorded_frames"];
            if (!noWrite) {
                reject("Guest execution or an audio write entered the no-write interval");
                break;
            }
            const qint64 gained = qint64(consumptionAfter["bytes_free"].toDouble()) -
                                  qint64(consumptionBefore["bytes_free"].toDouble());
            if (!capacityProgress && gained >= frameBytes) {
                capacityProgress = true;
                consumptionProof = {{"before", consumptionBefore},
                                    {"after", consumptionAfter},
                                    {"wait_ms", double(elapsed.elapsed() - waitStarted)},
                                    {"unchanged_written_frames", noWrite},
                                    {"gained_bytes", double(gained)}};
            }
            acceptedProgress = queuedAfter["host_frames_written_to_sink"].toDouble() > accepted;
            processedProgress =
                consumptionAfter["processed_us"].toDouble() > before["processed_us"].toDouble();
            guestRecordingProgress =
                consumptionAfter["guest_cycles"].toDouble() > before["guest_cycles"].toDouble() &&
                consumptionAfter["generated_frames"].toDouble() >
                    before["generated_frames"].toDouble() &&
                consumptionAfter["recorded_frames"].toDouble() >
                    before["recorded_frames"].toDouble();
            if (capacityProgress && acceptedProgress && processedProgress && guestRecordingProgress)
                break;
        }
        const auto after = snapshot();
        const qint64 readinessMs = elapsed.elapsed();
        if (failure.isEmpty() && (!capacityProgress || !acceptedProgress || !processedProgress ||
                                  !guestRecordingProgress || readinessMs > budgetMs))
            reject("Native audio did not establish all progress checks within its bound");
        if (failure.isEmpty() && !healthy())
            reject("Native stream changed before its final observation");
        const bool sameOwners = sameSession() && device && audioDevice_ == device.data();
        const bool noReprime = activeUninterrupted && audioPrimeEvents_ == primeEvents;
        const bool passed = failure.isEmpty() && healthy() && sameOwners && noReprime;
        record(phase, {{"before_wait", before},
                       {"after_wait", after},
                       {"readiness_ms", double(readinessMs)},
                       {"readiness_budget_ms", double(budgetMs)},
                       {"generated_intervals", int(generated)},
                       {"maximum_generated_intervals", int(maximumFrames)},
                       {"prime_intervals", int(primeFrames)},
                       {"same_sink_device_queue", sameOwners},
                       {"active_without_reprime", noReprime},
                       {"accepted_input_progress", acceptedProgress},
                       {"processed_time_progress", processedProgress},
                       {"guest_recording_progress", guestRecordingProgress},
                       {"capacity_progress", capacityProgress},
                       {"consumption_proof", consumptionProof},
                       {"ready", passed},
                       {"failure", failure}});
        return passed;
    };
    for (const bool quick : {false, true}) {
        const QString name = quick ? "quick" : "normal";
        if (!ready(name + "-before"))
            return smokeFailure(__FILE__, __LINE__);
        // Leave real generated PCM waiting in the application queue. This tests
        // disposal of an old stream as well as recovery from an empty new one.
        for (unsigned frame = 0; frame < 8 && audioBuffer_->bytesAvailable() == 0; ++frame)
            if (!runFrame())
                return smokeFailure(__FILE__, __LINE__);
        if (audioBuffer_->bytesAvailable() <= 0 || rewindStates_.size() < 2)
            return smokeFailure(__FILE__, __LINE__);
        const QPointer<QAudioSink> priorSink = audio_.get();
        const auto priorQueue = audioBuffer_;
        const auto before = snapshot();
        const auto outputState = std::make_unique<CupidGbAudioOutput>(emulator_->gb.apu.output);
        const uint64_t recorded = recorder_->framesWritten();
        const uint32_t rate = recorder_->sampleRate();
        const uint64_t guestCycles = emulator_->gb.elapsed_cycles;
        const size_t history = rewindStates_.size();
        const uint64_t age = rewindStates_.back().cycles;
        const uint64_t primes = audioPrimeEvents_;
        (quick ? quickResetAction_ : resetAction_)->trigger();
        const auto immediate = snapshot();
        const bool reset =
            !failed_ && !paused_ && emulator_->gb.elapsed_cycles == 0 && priorSink.isNull() &&
            audio_ && !audioDevice_ && audioPriming_ && audio_->state() == QAudio::StoppedState &&
            !immediate["capacity_available"].toBool() && audio_->error() == QAudio::NoError &&
            audioBuffer_ != priorQueue && audioBuffer_->bytesAvailable() == 0 &&
            audioBuffer_->telemetry()["host_frames_written_to_sink"].toDouble() == 0 &&
            recorder_->active() && recorder_->sampleRate() == rate &&
            recorder_->framesWritten() == recorded &&
            cupid_gb_apu_get_sample_rate(&emulator_->gb) == rate &&
            !std::memcmp(&emulator_->gb.apu.output, outputState.get(), sizeof(*outputState)) &&
            cupid_gb_audio_state_valid(&emulator_->gb) &&
            recordingAction_->text().startsWith("Stop Audio") && !rewindFailed_;
        const bool historyRetained =
            quick ? rewindStates_.size() == history + 1 && rewindStates_.back().cycles == age
                  : rewindStates_.size() == 1 && rewindStates_.front().cycles == 0;
        record(name + "-immediate", {{"before", before},
                                     {"after", immediate},
                                     {"old_sink_released", priorSink.isNull()},
                                     {"old_queue_bytes", double(priorQueue->bytesAvailable())},
                                     {"reset_contract", reset},
                                     {"history_contract", historyRetained}});
        if (!reset || !historyRetained)
            return smokeFailure(__FILE__, __LINE__);
        if (!captureDirectory.isEmpty() && !grab().save(output.filePath("reset-" + name + ".png")))
            return smokeFailure(__FILE__, __LINE__);
        if (!ready(name + "-after") || recorder_->framesWritten() <= recorded ||
            audioPrimeEvents_ <= primes)
            return smokeFailure(__FILE__, __LINE__);
        if (quick) {
            const uint64_t beforeRewind = recorder_->framesWritten();
            QKeyEvent press(QEvent::KeyPress, specialKeys_[1], Qt::NoModifier);
            QApplication::sendEvent(display_, &press);
            if (!rewind_)
                return smokeFailure(__FILE__, __LINE__);
            // Readiness can produce several real frames after reset. Cross the
            // exact resulting history depth, with a bound tied to that producer.
            const size_t expectedIntervals =
                rewindStates_.size() > history ? rewindStates_.size() - history : 0;
            if (!expectedIntervals || expectedIntervals > maximumFrames + 1)
                return smokeFailure(__FILE__, __LINE__);
            unsigned intervals = 0;
            while (rewindStates_.size() > history && intervals < expectedIntervals) {
                if (!rewindFrame() || recorder_->framesWritten() != beforeRewind)
                    return smokeFailure(__FILE__, __LINE__);
                ++intervals;
            }
            const bool crossed =
                intervals == expectedIntervals && rewindStates_.size() == history &&
                emulator_->gb.elapsed_cycles > guestCycles && recorder_->active() &&
                recorder_->framesWritten() == beforeRewind && !rewindFailed_;
            record("quick-rewind", {{"state", snapshot()},
                                    {"intervals", int(intervals)},
                                    {"expected_intervals", double(expectedIntervals)},
                                    {"maximum_intervals", int(maximumFrames + 1)},
                                    {"crossed_reset", crossed},
                                    {"pre_reset_guest_cycles", double(guestCycles)},
                                    {"pre_rewind_recorded_frames", double(beforeRewind)}});
            if (!crossed)
                return smokeFailure(__FILE__, __LINE__);
            QKeyEvent release(QEvent::KeyRelease, specialKeys_[1], Qt::NoModifier);
            QApplication::sendEvent(display_, &release);
            if (rewind_ || !ready("quick-forward-after-rewind") ||
                recorder_->framesWritten() <= beforeRewind)
                return smokeFailure(__FILE__, __LINE__);
        }
    }
    const uint64_t frames = recorder_->framesWritten();
    const uint32_t rate = recorder_->sampleRate();
    if (!stopRecording() || QFileInfo(recording).size() != qint64(44 + frames * 4))
        return smokeFailure(__FILE__, __LINE__);
    const QJsonObject report{
        {"passed", true},
        {"scope", "native sink progress, queued PCM, recording and reset history"},
        {"observations", observations},
        {"recording_file", QFileInfo(recording).fileName()},
        {"recording_frames", double(frames)},
        {"recording_rate", int(rate)},
        {"recording_bytes", double(QFileInfo(recording).size())}};
    QFile evidence(output.filePath("reset-audio-workflows.json"));
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (!evidence.open(QIODevice::WriteOnly) || evidence.write(bytes) != bytes.size())
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB native reset audio, recording and rewind checks passed");
    return true;
}

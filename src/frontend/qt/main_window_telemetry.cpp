/**
 * @file    main_window_telemetry.cpp
 * @brief   Bounded playback reports, capture metrics, and scripted input traces.
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

#include "gbs_player.h"

#include <cmath>

void MainWindow::beginBoundedRun(double seconds, const QString &reportPath) {
    struct WarmupSample {
        bool captured = false;
        double elapsed = 0;
        uint64_t cycles = 0;
        uint64_t presentationCycles = 0;
        uint64_t images = 0;
        uint64_t paints = 0;
        uint64_t musicWaveforms = 0;
        uint64_t musicPaints = 0;
        uint64_t audioFrames = 0;
        uint64_t handheldAudioFrames = 0;
        uint64_t startupAudioFrames = 0;
        uint64_t audioIdle = 0;
        uint64_t audioErrors = 0;
        size_t firstPaintInterval = 0;
    };

    constexpr double warmupSeconds = 5;
    constexpr qint64 warmupNanoseconds = 5000000000LL;
    /* Qt schedules timer callbacks against its own clock. Sample deadlines
     * against the same host clock that measures pacing and reported duration. */
    const qint64 deadlineNanoseconds = qint64(std::ceil(seconds * 1e9));
    const auto warmup = std::make_shared<WarmupSample>();
    telemetryStartCycles_ = emulator_->gb.elapsed_cycles;
    telemetryStartPresentationCycles_ = cupid_gb_presentation_cycles(&emulator_->gb);
    presentedImages_ = 0;
    musicWaveformsPublished_ = 0;
    if (musicPlayer_)
        musicPlayer_->resetTelemetry();
    generatedAudioFrames_ = 0;
    handheldAudioFrames_ = 0;
    startupAudioFrames_ = 0;
    replayedGuestCycles_ = 0;
    replayedFrames_ = 0;
    turboWindowDroppedFrames_ = 0;
    turboCapacityDroppedFrames_ = 0;
    rewindSnapshotsCaptured_ = 0;
    rewindCaptureNanoseconds_ = 0;
    rewindCaptureMaximumNanoseconds_ = 0;
    audioIdleEvents_ = 0;
    audioPrimeEvents_ = 0;
    display_->paintCount = 0;
    display_->lastPaintAt = 0;
    display_->paintIntervals.clear();
    display_->collectPaintIntervals = true;
    if (audioBuffer_)
        audioBuffer_->resetTelemetry();
    telemetryClock_.start();
    const uint64_t session = sessionId_;
    const uint64_t timeline = timelineId_;
    const uint64_t measurement = ++boundedRunId_;
    const uint32_t clockHz = cupid_gb_base_clock_hz(&emulator_->gb);
    const auto unchanged = [this, session, timeline, measurement, clockHz] {
        return emulator_ && sessionId_ == session && timelineId_ == timeline &&
               boundedRunId_ == measurement && cupid_gb_base_clock_hz(&emulator_->gb) == clockHz &&
               emulator_->gb.elapsed_cycles >= telemetryStartCycles_ &&
               cupid_gb_presentation_cycles(&emulator_->gb) >= telemetryStartPresentationCycles_;
    };
    auto *deadlineTimer = new QTimer(this);
    deadlineTimer->setTimerType(Qt::PreciseTimer);
    deadlineTimer->setInterval(10);
    connect(
        deadlineTimer, &QTimer::timeout, this,
        [this, deadlineTimer, reportPath, seconds, deadlineNanoseconds, warmupSeconds, warmup,
         measurement, unchanged, clockHz] {
            if (boundedRunId_ != measurement) {
                deadlineTimer->stop();
                deadlineTimer->deleteLater();
                return;
            }
            if (!unchanged()) {
                deadlineTimer->stop();
                deadlineTimer->deleteLater();
                display_->collectPaintIntervals = false;
                qCritical(
                    "Bounded frontend run cancelled because the cartridge or timeline changed");
                QApplication::exit(1);
                return;
            }
            const qint64 elapsedNanoseconds = telemetryClock_.nsecsElapsed();
            /* Sample the warmup once, strictly before the final deadline. If
             * the event loop misses that window, report it as unavailable. */
            if (seconds > warmupSeconds && !warmup->captured &&
                elapsedNanoseconds >= warmupNanoseconds &&
                elapsedNanoseconds < deadlineNanoseconds) {
                warmup->elapsed = double(elapsedNanoseconds) / 1e9;
                warmup->cycles = emulator_->gb.elapsed_cycles;
                warmup->presentationCycles = cupid_gb_presentation_cycles(&emulator_->gb);
                warmup->images = presentedImages_;
                warmup->paints = display_->paintCount;
                warmup->musicWaveforms = musicWaveformsPublished_;
                warmup->musicPaints = musicPlayer_ ? musicPlayer_->waveformPaintCount() : 0;
                warmup->audioFrames = generatedAudioFrames_;
                warmup->handheldAudioFrames = handheldAudioFrames_;
                warmup->startupAudioFrames = startupAudioFrames_;
                warmup->audioIdle = audioIdleEvents_;
                warmup->audioErrors = audioErrorEvents_;
                // The next recorded interval starts before this sample. Exclude it
                // from the later window without changing the whole-run history.
                warmup->firstPaintInterval =
                    display_->paintIntervals.size() + (display_->lastPaintAt ? 1 : 0);
                warmup->captured = true;
            }
            if (elapsedNanoseconds < deadlineNanoseconds)
                return;
            deadlineTimer->stop();
            deadlineTimer->deleteLater();
            display_->collectPaintIntervals = false;
            const double elapsed = double(elapsedNanoseconds) / 1e9;
            const uint64_t cycles = emulator_->gb.elapsed_cycles - telemetryStartCycles_;
            const uint64_t presentationCycles =
                cupid_gb_presentation_cycles(&emulator_->gb) - telemetryStartPresentationCycles_;
            QJsonObject report{
                {"host_clock", QString::fromLatin1(HostElapsedTimer::sourceName())},
                {"requested_duration_seconds", seconds},
                {"elapsed_seconds", elapsed},
                {"guest_base_speed_t_cycles", double(cycles)},
                {"base_clock_hz", int(clockHz)},
                {"guest_cycles_per_second", double(cycles) / elapsed},
                {"guest_speed_percent", double(cycles) / elapsed / clockHz * 100},
                {"presentation_base_speed_t_cycles", double(presentationCycles)},
                {"presentation_cycles_per_second", double(presentationCycles) / elapsed},
                {"presentation_speed_percent",
                 double(presentationCycles) / elapsed / clockHz * 100},
                {"images_submitted_to_widget", double(presentedImages_)},
                {"widget_paint_events", double(display_->paintCount)},
                {"music_waveforms_published", double(musicWaveformsPublished_)},
                {"music_widget_paint_events",
                 double(musicPlayer_ ? musicPlayer_->waveformPaintCount() : 0)},
                {"audio_frames_generated", double(generatedAudioFrames_)},
                {"handheld_audio_frames_generated", double(handheldAudioFrames_)},
                {"startup_audio_frames_generated", double(startupAudioFrames_)},
                {"rewind_replay_guest_t_cycles", double(replayedGuestCycles_)},
                {"rewind_replay_presentation_events", double(replayedFrames_)},
                {"turbo_window_dropped_host_frames", double(turboWindowDroppedFrames_)},
                {"turbo_capacity_dropped_host_frames", double(turboCapacityDroppedFrames_)},
                {"audio_idle_events_after_startup", double(audioIdleEvents_)},
                {"audio_priming_target_microseconds", 30000},
                {"audio_priming_events", double(audioPrimeEvents_)},
                {"audio_priming_pending", audioPriming_},
                {"audio_error_events", double(audioErrorEvents_)},
                {"audio_sink_created", bool(audio_)},
                {"audio_muted", muteAction_->isChecked()},
                {"audio_volume_percent", settings_.value("audio/volume", 100).toInt()},
                {"audio_sink_volume", audio_ ? double(audio_->volume()) : 0.0},
                {"audio_requested_device_id_base64",
                 QString::fromLatin1(
                     settings_.value("audio/outputDevice").toByteArray().toBase64())},
                {"audio_active_device_id_base64",
                 QString::fromLatin1(activeAudioDeviceId_.toBase64())},
                {"audio_active_device_name", activeAudioDeviceName_},
                {"audio_device_fallback", audioDeviceFallback_},
                {"audio_sink_state", audio_ ? int(audio_->state()) : -1},
                {"audio_sink_error", audio_ ? int(audio_->error()) : -1},
                {"audio_processed_microseconds", audio_ ? double(audio_->processedUSecs()) : 0},
                {"audio_sample_rate", int(cupid_gb_apu_get_sample_rate(&emulator_->gb))},
                {"audio_channels", 2},
                {"host_audio_sample_rate", audio_ ? audio_->format().sampleRate() : 0},
                {"host_audio_channels", audio_ ? audio_->format().channelCount() : 0},
                {"host_audio_format", audio_ ? int(audio_->format().sampleFormat()) : 0},
                {"qt_version", qVersion()},
                {"platform", QGuiApplication::platformName()},
                {"execution_failed", failed_}};
            QJsonObject postWarmup{{"available", false},
                                   {"warmup_target_seconds", warmupSeconds},
                                   {"base_clock_hz", int(clockHz)}};
            if (seconds <= warmupSeconds) {
                postWarmup.insert("reason", "run_does_not_exceed_warmup");
            } else if (!warmup->captured || elapsed <= warmup->elapsed) {
                postWarmup.insert("reason", "warmup_sample_unavailable");
            } else if (emulator_->gb.elapsed_cycles < warmup->cycles ||
                       cupid_gb_presentation_cycles(&emulator_->gb) < warmup->presentationCycles ||
                       presentedImages_ < warmup->images || display_->paintCount < warmup->paints ||
                       generatedAudioFrames_ < warmup->audioFrames ||
                       handheldAudioFrames_ < warmup->handheldAudioFrames ||
                       startupAudioFrames_ < warmup->startupAudioFrames ||
                       audioIdleEvents_ < warmup->audioIdle ||
                       audioErrorEvents_ < warmup->audioErrors) {
                qCritical("Bounded frontend run cancelled because measurement counters changed");
                QApplication::exit(1);
                return;
            } else {
                const double windowElapsed = elapsed - warmup->elapsed;
                const double windowCycles = double(emulator_->gb.elapsed_cycles - warmup->cycles);
                const double windowPresentationCycles = double(
                    cupid_gb_presentation_cycles(&emulator_->gb) - warmup->presentationCycles);
                postWarmup.insert("available", true);
                postWarmup.insert("sample_start_elapsed_seconds", warmup->elapsed);
                postWarmup.insert("sample_end_elapsed_seconds", elapsed);
                postWarmup.insert("elapsed_seconds", windowElapsed);
                postWarmup.insert("guest_base_speed_t_cycles", windowCycles);
                postWarmup.insert("guest_cycles_per_second", windowCycles / windowElapsed);
                postWarmup.insert("guest_speed_percent",
                                  windowCycles / windowElapsed / clockHz * 100);
                postWarmup.insert("presentation_base_speed_t_cycles", windowPresentationCycles);
                postWarmup.insert("presentation_cycles_per_second",
                                  windowPresentationCycles / windowElapsed);
                postWarmup.insert("presentation_speed_percent",
                                  windowPresentationCycles / windowElapsed / clockHz * 100);
                postWarmup.insert("images_submitted_to_widget",
                                  double(presentedImages_ - warmup->images));
                postWarmup.insert("widget_paint_events",
                                  double(display_->paintCount - warmup->paints));
                postWarmup.insert("music_waveforms_published",
                                  double(musicWaveformsPublished_ - warmup->musicWaveforms));
                postWarmup.insert("music_widget_paint_events",
                                  double((musicPlayer_ ? musicPlayer_->waveformPaintCount() : 0) -
                                         warmup->musicPaints));
                postWarmup.insert("audio_frames_generated",
                                  double(generatedAudioFrames_ - warmup->audioFrames));
                postWarmup.insert("handheld_audio_frames_generated",
                                  double(handheldAudioFrames_ - warmup->handheldAudioFrames));
                postWarmup.insert("startup_audio_frames_generated",
                                  double(startupAudioFrames_ - warmup->startupAudioFrames));
                postWarmup.insert("audio_idle_events",
                                  double(audioIdleEvents_ - warmup->audioIdle));
                postWarmup.insert("audio_error_events",
                                  double(audioErrorEvents_ - warmup->audioErrors));
                postWarmup.insert("widget_paint_interval_ms",
                                  display_->paintTelemetry(warmup->firstPaintInterval));
                postWarmup.insert(
                    "baseline",
                    QJsonObject{
                        {"elapsed_seconds", warmup->elapsed},
                        {"guest_base_speed_t_cycles", double(warmup->cycles)},
                        {"presentation_base_speed_t_cycles", double(warmup->presentationCycles)},
                        {"images_submitted_to_widget", double(warmup->images)},
                        {"widget_paint_events", double(warmup->paints)},
                        {"music_waveforms_published", double(warmup->musicWaveforms)},
                        {"music_widget_paint_events", double(warmup->musicPaints)},
                        {"audio_frames_generated", double(warmup->audioFrames)},
                        {"handheld_audio_frames_generated", double(warmup->handheldAudioFrames)},
                        {"startup_audio_frames_generated", double(warmup->startupAudioFrames)},
                        {"audio_idle_events", double(warmup->audioIdle)},
                        {"audio_error_events", double(warmup->audioErrors)}});
            }
            report.insert("post_warmup", postWarmup);
            report.insert("input_events_applied", double(inputTraceIndex_));
            report.insert("rewind_snapshots_captured", double(rewindSnapshotsCaptured_));
            report.insert("rewind_capture_total_ms", double(rewindCaptureNanoseconds_) / 1e6);
            report.insert("rewind_capture_maximum_ms",
                          double(rewindCaptureMaximumNanoseconds_) / 1e6);
            report.insert("rewind_memory_bytes", double(rewindBytes_));
            report.insert("rewind_history_seconds",
                          rewindStates_.size() < 2
                              ? 0.0
                              : double(rewindStates_.back().cycles - rewindStates_.front().cycles) /
                                    clockHz);
            report.insert("rewind_capture_failed", rewindFailed_);
            report.insert("hardware_model", cupid_gb_model_name(emulator_->gb.model));
            QJsonValue requestedModel;
            if (modelOverride_)
                requestedModel =
                    *modelOverride_ < 0
                        ? QString("auto")
                        : QString::fromLatin1(cupid_gb_model_name(CupidGbModel(*modelOverride_)));
            report.insert("requested_model", requestedModel);
            report.insert("guest_double_speed", emulator_->gb.double_speed);
            report.insert("media_kind", musicInfo_ ? "gbs" : "cartridge");
            if (musicInfo_) {
                report.insert("music_declared_track_count", int(musicInfo_->track_count));
                report.insert("music_selected_track_index", int(musicTrack_));
            }
            report.insert("startup_mode", musicInfo_                    ? "music-program"
                                          : emulator_->gb.boot_rom_size ? "boot-rom"
                                                                        : "post-boot");
            report.insert(
                "power_on_seed_hex",
                QString::number(qulonglong(cupid_gb_get_power_on_seed(&emulator_->gb)), 16)
                    .rightJustified(16, QLatin1Char('0')));
            report.insert("power_on_seed_mode", powerOnSeedOverride_ ? "fixed" : "auto");
            report.insert("power_on_seed_scope", "realized cold generation");
            report.insert("power_on_seed_is_state_ram_provenance", false);
            report.insert("boot_rom_bytes", int(emulator_->gb.boot_rom_size));
            if (emulator_->gb.boot_rom_size)
                report.insert("boot_rom_sha256",
                              QString::fromLatin1(
                                  QCryptographicHash::hash(
                                      QByteArray::fromRawData(
                                          reinterpret_cast<const char *>(emulator_->gb.boot_rom),
                                          qsizetype(emulator_->gb.boot_rom_size)),
                                      QCryptographicHash::Sha256)
                                      .toHex()));
            report.insert(
                "rom_sha256",
                QString::fromLatin1(
                    QCryptographicHash::hash(romData_, QCryptographicHash::Sha256).toHex()));
            if (audioBuffer_)
                report.insert("audio_queue", audioBuffer_->telemetry());
            report.insert("widget_paint_interval_ms", display_->paintTelemetry());
            if (!image_.isNull()) {
                const QString screenshotPath = reportPath + ".png";
                report.insert("screenshot_saved", image_.save(screenshotPath, "PNG"));
            }
            // Capture the interface in-process: external compositor readback can
            // return an empty surface even while a native Qt window is visible.
            const QPixmap windowCapture = grab();
            report.insert("window_visible", isVisible());
            report.insert("window_capture_width", windowCapture.width());
            report.insert("window_capture_height", windowCapture.height());
            report.insert("window_capture_saved",
                          windowCapture.save(reportPath + ".window.png", "PNG"));
            report.insert("battery_flushed", flushBattery(true));
            QSaveFile file(reportPath);
            const QByteArray bytes = QJsonDocument(report).toJson();
            const bool written = file.open(QIODevice::WriteOnly) &&
                                 file.write(bytes) == bytes.size() && file.commit();
            if (!written)
                qCritical("Could not write frontend report: %s", qPrintable(file.errorString()));
            QApplication::exit(written && !failed_ ? 0 : 1);
        });
    deadlineTimer->start();
}

bool MainWindow::loadInputTrace(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        reportError("Cannot open input trace: " + file.errorString());
        return false;
    }
    std::vector<std::pair<uint64_t, uint8_t>> events;
    for (const auto &raw : QString::fromUtf8(file.readAll()).split('\n')) {
        const QString line = raw.section('#', 0, 0).simplified();
        if (line.isEmpty())
            continue;
        const auto fields = line.split(' ');
        bool frameValid = false;
        bool maskValid = false;
        const auto frame = fields[0].toULongLong(&frameValid);
        const auto mask = fields.size() == 2 ? fields[1].toUInt(&maskValid, 16) : 0;
        if (!frameValid || !maskValid || mask > 255 || frame > 10000000000ULL ||
            events.size() >= 4096 || (!events.empty() && frame < events.back().first)) {
            reportError("Invalid input trace. Use ascending decimal frames and hexadecimal masks.");
            return false;
        }
        events.emplace_back(frame, uint8_t(mask));
    }
    inputTrace_ = std::move(events);
    inputTraceIndex_ = 0;
    return true;
}

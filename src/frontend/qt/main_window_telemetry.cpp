#include "main_window_internal.h"

void MainWindow::beginBoundedRun(double seconds, const QString &reportPath) {
    struct WarmupSample {
        bool captured = false;
        double elapsed = 0;
        uint64_t cycles = 0;
        uint64_t images = 0;
        uint64_t paints = 0;
        uint64_t audioFrames = 0;
        uint64_t audioIdle = 0;
        uint64_t audioErrors = 0;
        size_t firstPaintInterval = 0;
    };

    constexpr double warmupSeconds = 5;
    const auto warmup = std::make_shared<WarmupSample>();
    telemetryStartCycles_ = emulator_->gb.elapsed_cycles;
    presentedImages_ = 0;
    generatedAudioFrames_ = 0;
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
               emulator_->gb.elapsed_cycles >= telemetryStartCycles_;
    };
    if (seconds > warmupSeconds) {
        QTimer::singleShot(5000, Qt::PreciseTimer, this, [this, warmup, unchanged] {
            if (!unchanged())
                return;
            warmup->elapsed = double(telemetryClock_.nsecsElapsed()) / 1e9;
            warmup->cycles = emulator_->gb.elapsed_cycles;
            warmup->images = presentedImages_;
            warmup->paints = display_->paintCount;
            warmup->audioFrames = generatedAudioFrames_;
            warmup->audioIdle = audioIdleEvents_;
            warmup->audioErrors = audioErrorEvents_;
            // The next recorded interval starts before this sample. Exclude it
            // from the later window without changing the whole-run history.
            warmup->firstPaintInterval =
                display_->paintIntervals.size() + (display_->lastPaintAt ? 1 : 0);
            warmup->captured = true;
        });
    }
    QTimer::singleShot(
        int(seconds * 1000), Qt::PreciseTimer, this,
        [this, reportPath, seconds, warmupSeconds, measurement, warmup, unchanged, clockHz] {
            if (boundedRunId_ != measurement)
                return;
            display_->collectPaintIntervals = false;
            if (!unchanged()) {
                qCritical(
                    "Bounded frontend run cancelled because the cartridge or timeline changed");
                QApplication::exit(1);
                return;
            }
            const double elapsed = double(telemetryClock_.nsecsElapsed()) / 1e9;
            const uint64_t cycles = emulator_->gb.elapsed_cycles - telemetryStartCycles_;
            QJsonObject report{
                {"elapsed_seconds", elapsed},
                {"guest_base_speed_t_cycles", double(cycles)},
                {"base_clock_hz", int(clockHz)},
                {"guest_cycles_per_second", double(cycles) / elapsed},
                {"guest_speed_percent", double(cycles) / elapsed / clockHz * 100},
                {"images_submitted_to_widget", double(presentedImages_)},
                {"widget_paint_events", double(display_->paintCount)},
                {"audio_frames_generated", double(generatedAudioFrames_)},
                {"audio_idle_events_after_startup", double(audioIdleEvents_)},
                {"audio_priming_target_microseconds", 30000},
                {"audio_priming_events", double(audioPrimeEvents_)},
                {"audio_priming_pending", audioPriming_},
                {"audio_error_events", double(audioErrorEvents_)},
                {"audio_sink_created", bool(audio_)},
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
                       presentedImages_ < warmup->images || display_->paintCount < warmup->paints ||
                       generatedAudioFrames_ < warmup->audioFrames ||
                       audioIdleEvents_ < warmup->audioIdle ||
                       audioErrorEvents_ < warmup->audioErrors) {
                qCritical("Bounded frontend run cancelled because measurement counters changed");
                QApplication::exit(1);
                return;
            } else {
                const double windowElapsed = elapsed - warmup->elapsed;
                const double windowCycles = double(emulator_->gb.elapsed_cycles - warmup->cycles);
                postWarmup.insert("available", true);
                postWarmup.insert("sample_start_elapsed_seconds", warmup->elapsed);
                postWarmup.insert("sample_end_elapsed_seconds", elapsed);
                postWarmup.insert("elapsed_seconds", windowElapsed);
                postWarmup.insert("guest_base_speed_t_cycles", windowCycles);
                postWarmup.insert("guest_cycles_per_second", windowCycles / windowElapsed);
                postWarmup.insert("guest_speed_percent",
                                  windowCycles / windowElapsed / clockHz * 100);
                postWarmup.insert("images_submitted_to_widget",
                                  double(presentedImages_ - warmup->images));
                postWarmup.insert("widget_paint_events",
                                  double(display_->paintCount - warmup->paints));
                postWarmup.insert("audio_frames_generated",
                                  double(generatedAudioFrames_ - warmup->audioFrames));
                postWarmup.insert("audio_idle_events",
                                  double(audioIdleEvents_ - warmup->audioIdle));
                postWarmup.insert("audio_error_events",
                                  double(audioErrorEvents_ - warmup->audioErrors));
                postWarmup.insert("widget_paint_interval_ms",
                                  display_->paintTelemetry(warmup->firstPaintInterval));
                postWarmup.insert(
                    "baseline", QJsonObject{{"elapsed_seconds", warmup->elapsed},
                                            {"guest_base_speed_t_cycles", double(warmup->cycles)},
                                            {"images_submitted_to_widget", double(warmup->images)},
                                            {"widget_paint_events", double(warmup->paints)},
                                            {"audio_frames_generated", double(warmup->audioFrames)},
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
            report.insert("startup_mode", emulator_->gb.boot_rom_size ? "boot-rom" : "post-boot");
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
            report.insert("battery_flushed", flushBattery());
            QSaveFile file(reportPath);
            const QByteArray bytes = QJsonDocument(report).toJson();
            const bool written = file.open(QIODevice::WriteOnly) &&
                                 file.write(bytes) == bytes.size() && file.commit();
            if (!written)
                qCritical("Could not write frontend report: %s", qPrintable(file.errorString()));
            QApplication::exit(written && !failed_ ? 0 : 1);
        });
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

#include "main_window_internal.h"

void MainWindow::setupAudio() {
    // Some audio backends dispatch Qt events while opening or closing a stream.
    // Guest execution must not reenter a pause, load, or rewind transition.
    QScopedValueRollback<bool> operation(stateOperation_, true);
    stopAudio();
    audioConverter_.reset();
    audioBuffer_ = std::make_unique<AudioBuffer>();
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
    audioBuffer_ = std::make_unique<AudioBuffer>(format.bytesPerFrame());
    audioConverter_ = std::make_unique<AudioConverter>(format);
    audioQueueLimit_ = format.bytesForDuration(50000);
    audioPrimeBytes_ = format.bytesForDuration(30000);
    audioPriming_ = true;
    audio_ = std::make_unique<QAudioSink>(device, format);
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
    audioDevice_ = nullptr;
    audioPriming_ = true;
    if (audio_) {
        // Disconnect callbacks before releasing the owning pointer: stopping the
        // backend may synchronously emit stateChanged during destruction.
        audio_->disconnect(this);
        // A pause or timeline change discards obsolete samples. Draining the old
        // stream can block the UI indefinitely when the host device stops consuming.
        audio_->reset();
        audio_.reset();
    }
}

void MainWindow::clearAudio() {
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
    QScopedValueRollback<bool> operation(stateOperation_, true);
    if (audio_ && !paused_) {
        // The event loop produces one guest frame at a time. Accumulate real samples
        // before starting the sink so frame-sized writes tolerate host scheduling jitter.
        // The 30 ms target stays below the 50 ms execution-backpressure limit.
        if (audioPriming_) {
            if (audioBuffer_->bytesAvailable() < audioPrimeBytes_)
                return;
            // Start only when generated audio is ready. Repeated reset/start calls
            // with an empty queue can leave the Windows backend idle after a load.
            if (!audioDevice_)
                audioDevice_ = audio_->start();
            if (audioDevice_ && audioBuffer_->writeTo(audioDevice_, audio_->bytesFree()) > 0) {
                audioPriming_ = false;
                ++audioPrimeEvents_;
            }
            return;
        }
        if (audioDevice_)
            audioBuffer_->writeTo(audioDevice_, audio_->bytesFree());
    }
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
    return true;
}

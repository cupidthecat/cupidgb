#include "main_window.h"
#include "audio_converter.h"

#include <QAction>
#include <QApplication>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>

extern "C" {
#include "cupid/gbc/cgb.h"
}

namespace {
constexpr std::array<int, 8> defaultKeys = {Qt::Key_Right,     Qt::Key_Left,  Qt::Key_Up,
                                            Qt::Key_Down,      Qt::Key_X,     Qt::Key_Z,
                                            Qt::Key_Backspace, Qt::Key_Return};
constexpr std::array<const char *, 8> buttonNames = {"Right", "Left", "Up",     "Down",
                                                     "A",     "B",    "Select", "Start"};
// A normal-speed LCD frame is 70,224 T-cycles at 4,194,304 Hz.
constexpr qint64 frameNanoseconds = 16742706;

QRgb rgb555(uint16_t value) {
    const int r = value & 31;
    const int g = (value >> 5) & 31;
    const int b = (value >> 10) & 31;
    return qRgb((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}
} // namespace

class DisplayWidget final : public QWidget {
  public:
    explicit DisplayWidget(QWidget *parent) : QWidget(parent) {
        setMinimumSize(160, 144);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        paintClock.start();
    }

    QImage image;
    bool integerScale = true;
    uint64_t paintCount = 0;
    QElapsedTimer paintClock;
    qint64 lastPaintAt = 0;
    std::vector<double> paintIntervals;

    QSize sizeHint() const override {
        return {320, 288};
    }

    QJsonObject paintTelemetry() const {
        if (paintIntervals.empty())
            return {};
        auto intervals = paintIntervals;
        std::sort(intervals.begin(), intervals.end());
        return {{"minimum", intervals.front()},
                {"median", intervals[intervals.size() / 2]},
                {"percentile_95", intervals[(intervals.size() - 1) * 95 / 100]},
                {"percentile_99", intervals[(intervals.size() - 1) * 99 / 100]},
                {"maximum", intervals.back()}};
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (image.isNull()) {
            painter.setPen(Qt::white);
            painter.drawText(rect(), Qt::AlignCenter,
                             "CupidGB\nOpen a Game Boy or Game Boy Color ROM");
            return;
        }
        ++paintCount;
        const qint64 now = paintClock.nsecsElapsed();
        if (lastPaintAt)
            paintIntervals.push_back(double(now - lastPaintAt) / 1e6);
        lastPaintAt = now;
        double scale = std::min(double(width()) / image.width(), double(height()) / image.height());
        if (integerScale && scale >= 1.0)
            scale = int(scale);
        const QSize size(int(image.width() * scale), int(image.height() * scale));
        const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2),
                           size);
        painter.drawImage(target, image);
    }
};

// Keep generated audio until the host accepts it. Backpressure pauses guest execution.
class AudioBuffer final {
  public:
    explicit AudioBuffer(int bytesPerFrame = 4) : bytesPerFrame_(bytesPerFrame) {
    }

    void append(const char *data, qsizetype size) {
        {
            QMutexLocker lock(&mutex_);
            queue_.append(data, size);
            maximumQueued_ = std::max(maximumQueued_, queue_.size());
        }
    }

    void clear() {
        QMutexLocker lock(&mutex_);
        queue_.clear();
    }

    qint64 bytesAvailable() const {
        QMutexLocker lock(&mutex_);
        return queue_.size();
    }

    void resetTelemetry() {
        QMutexLocker lock(&mutex_);
        bytesRead_ = 0;
        maximumQueued_ = queue_.size();
    }

    QJsonObject telemetry() const {
        QMutexLocker lock(&mutex_);
        return {{"host_frames_written_to_sink", double(bytesRead_) / bytesPerFrame_},
                {"maximum_queued_bytes", double(maximumQueued_)},
                {"remaining_queued_bytes", double(queue_.size())}};
    }

    void writeTo(QIODevice *device, qint64 maximum) {
        QMutexLocker lock(&mutex_);
        const qint64 available = std::min(maximum, qint64(queue_.size()));
        const qint64 aligned = available - available % bytesPerFrame_;
        if (aligned <= 0)
            return;
        const qint64 written = device->write(queue_.constData(), aligned);
        if (written > 0) {
            queue_.remove(0, qsizetype(written));
            bytesRead_ += uint64_t(written);
        }
    }

  private:
    mutable QMutex mutex_;
    QByteArray queue_;
    uint64_t bytesRead_ = 0;
    int bytesPerFrame_ = 4;
    qsizetype maximumQueued_ = 0;
};

MainWindow::MainWindow(bool isolated, bool automated) : isolated_(isolated), automated_(automated) {
    setWindowTitle("CupidGB");
    setAcceptDrops(true);
    display_ = new DisplayWidget(this);
    setCentralWidget(display_);
    for (size_t i = 0; i < keys_.size(); ++i)
        keys_[i] = settings_.value(QString("input/%1").arg(i), defaultKeys[i]).toInt();
    display_->integerScale = settings_.value("video/integerScale", true).toBool();
    buildMenus();
    resize(std::max(320, menuBar()->sizeHint().width()),
           288 + menuBar()->sizeHint().height() + statusBar()->sizeHint().height());
    if (!isolated_)
        restoreGeometry(settings_.value("window/geometry").toByteArray());
    qApp->installEventFilter(this);
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(1);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    connect(&saveTimer_, &QTimer::timeout, this, [this] { flushBattery(); });
    saveTimer_.start(15000);
    timer_.start();
    clock_.start();
    refreshActions();
}

MainWindow::~MainWindow() {
    timer_.stop();
    saveTimer_.stop();
    paused_ = true;
    qApp->removeEventFilter(this);
    stopAudio();
    if (emulator_) {
        flushBattery();
        cupid_emulator_shutdown(emulator_.get());
    }
}

void MainWindow::buildMenus() {
    auto *file = menuBar()->addMenu("&File");
    auto *open = file->addAction("&Open ROM…", QKeySequence("Ctrl+O"));
    connect(open, &QAction::triggered, this, [this] {
        const bool wasPaused = paused_;
        setPaused(true);
        const QString path = QFileDialog::getOpenFileName(
            this, "Open ROM", settings_.value("files/lastDirectory").toString(),
            "Game Boy cartridges (*.gb *.gbc *.sgb);;All files (*)");
        if (path.isEmpty() || !openRom(path))
            setPaused(wasPaused);
    });
    recentMenu_ = file->addMenu("Open &Recent");
    updateRecentMenu();
    closeAction_ = file->addAction("&Close ROM", QKeySequence("Ctrl+W"));
    connect(closeAction_, &QAction::triggered, this, [this] { closeRom(); });
    file->addSeparator();
    auto *quit = file->addAction("&Quit", QKeySequence("Ctrl+Q"));
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto *emulation = menuBar()->addMenu("&Emulation");
    resetAction_ = emulation->addAction("&Reset", QKeySequence("Ctrl+R"));
    connect(resetAction_, &QAction::triggered, this, [this] { restart(); });
    pauseAction_ = emulation->addAction("&Pause", QKeySequence("Ctrl+P"));
    pauseAction_->setCheckable(true);
    connect(pauseAction_, &QAction::toggled, this, [this](bool value) { setPaused(value); });
    frameAction_ = emulation->addAction("Advance &Frame", QKeySequence("Ctrl+N"));
    connect(frameAction_, &QAction::triggered, this, [this] {
        setPaused(true);
        runFrame();
        clearAudio();
    });
    auto *view = menuBar()->addMenu("&View");
    auto *fullscreen = view->addAction("&Fullscreen", QKeySequence("Ctrl+F"));
    fullscreen->setCheckable(true);
    connect(fullscreen, &QAction::toggled, this,
            [this](bool value) { value ? showFullScreen() : showNormal(); });
    screenshotAction_ = view->addAction("Save &Screenshot…", QKeySequence("F12"));
    connect(screenshotAction_, &QAction::triggered, this, [this] { screenshot(); });
    auto *audio = menuBar()->addMenu("&Audio");
    muteAction_ = audio->addAction("&Mute", QKeySequence("Ctrl+M"));
    muteAction_->setCheckable(true);
    muteAction_->setChecked(settings_.value("audio/mute", false).toBool());
    connect(muteAction_, &QAction::toggled, this, [this](bool muted) {
        settings_.setValue("audio/mute", muted);
        if (audio_)
            audio_->setVolume(muted ? 0.0f : settings_.value("audio/volume", 100).toFloat() / 100);
    });
    emulation->addSeparator();
    auto *settings = emulation->addAction("&Settings…", QKeySequence("Ctrl+,"));
    connect(settings, &QAction::triggered, this, [this] { configure(); });
    auto *help = menuBar()->addMenu("&Help");
    connect(help->addAction("&Controls"), &QAction::triggered, this, [this] {
        const bool wasPaused = paused_;
        setPaused(true);
        QMessageBox::information(
            this, "Controls",
            "Default controls: arrows, X (A), Z (B), Backspace (Select), Enter (Start).\n"
            "Hold Space for turbo. Change button mappings in Settings.\n\n"
            "Open: Ctrl+O   Reset: Ctrl+R   Pause: Ctrl+P\n"
            "Fullscreen: Ctrl+F   Mute: Ctrl+M   Screenshot: F12\n\n"
            "Rewind, save states, controllers and debugging tools are not available yet.");
        setPaused(wasPaused);
    });
    connect(help->addAction("&About CupidGB"), &QAction::triggered, this, [this] {
        const bool wasPaused = paused_;
        setPaused(true);
        QMessageBox::about(this, "CupidGB",
                           "CupidGB 0.2.0\nGame Boy and Game Boy Color emulator\n"
                           "See LICENSE for licensing terms.");
        setPaused(wasPaused);
    });
}

void MainWindow::updateRecentMenu() {
    recentMenu_->clear();
    const QStringList paths = settings_.value("files/recent").toStringList();
    for (const auto &path : paths) {
        auto *action = recentMenu_->addAction(path);
        connect(action, &QAction::triggered, this, [this, path] { openRom(path); });
    }
    recentMenu_->setEnabled(!paths.isEmpty());
}

void MainWindow::reportError(const QString &message) {
    failed_ = true;
    if (isolated_ || automated_)
        qCritical("%s", qPrintable(message));
    else
        QMessageBox::critical(this, "CupidGB", message);
}

bool MainWindow::openRom(const QString &path) {
    if (QFileInfo(path).suffix().compare("sav", Qt::CaseInsensitive) == 0) {
        reportError("Battery save files cannot be opened as cartridges.");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        reportError("Cannot open ROM: " + file.errorString());
        return false;
    }
    if (file.size() < 0x150 || file.size() > CUPID_GB_MAX_ROM_SIZE) {
        reportError("Cartridge size must be between 336 bytes and 8 MiB.");
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.size() != file.size()) {
        reportError("Could not read the complete cartridge: " + file.errorString());
        return false;
    }
    auto candidate = std::make_unique<CupidEmulator>();
    int model = settings_.value("emulation/model", -1).toInt();
    if (model < 0) {
        if ((uint8_t(data[0x143]) & 0x80) != 0)
            model = CUPID_GB_MODEL_CGB;
        else if (uint8_t(data[0x146]) == 3)
            model = CUPID_GB_MODEL_SGB;
        else
            model = CUPID_GB_MODEL_DMG_ABC;
    }
    if (model > CUPID_GB_MODEL_SGB2 || model < CUPID_GB_MODEL_DMG_ABC) {
        reportError("The configured hardware model is invalid.");
        return false;
    }
    if ((uint8_t(data[0x143]) & 0xc0) == 0xc0 && model != CUPID_GB_MODEL_CGB) {
        reportError("This cartridge requires Game Boy Color. Select Automatic or Game Boy Color.");
        return false;
    }
    cupid_emulator_init(candidate.get(),
                        model == CUPID_GB_MODEL_CGB ? CUPID_SYSTEM_GBC : CUPID_SYSTEM_GB);
    cupid_gb_set_model(&candidate->gb, CupidGbModel(model));
    if (!cupid_emulator_load_rom(candidate.get(),
                                 reinterpret_cast<const uint8_t *>(data.constData()),
                                 size_t(data.size()))) {
        cupid_emulator_shutdown(candidate.get());
        reportError("The cartridge could not be loaded.");
        return false;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QFileInfo info(absolute);
    const QString save = info.dir().filePath(info.completeBaseName() + ".sav");
    const QString canonicalSave = QFileInfo(save).canonicalFilePath();
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    if (!canonicalSave.isEmpty() &&
        canonicalSave.compare(info.canonicalFilePath(), pathCase) == 0) {
        cupid_emulator_shutdown(candidate.get());
        reportError("The battery save path points to the cartridge. Choose separate files.");
        return false;
    }
    // Flush the running cartridge before reading the same battery file during reset.
    if (!flushBattery()) {
        cupid_emulator_shutdown(candidate.get());
        return false;
    }
    QFile battery(save);
    if (candidate->gb.header.has_battery && candidate->gb.cartridge_ram_size && battery.exists()) {
        if (!battery.open(QIODevice::ReadOnly) ||
            battery.size() != qint64(candidate->gb.cartridge_ram_size)) {
            cupid_emulator_shutdown(candidate.get());
            reportError("Cannot load battery save or its size does not match cartridge RAM: " +
                        save);
            return false;
        }
        const QByteArray ram = battery.readAll();
        if (ram.size() != qint64(candidate->gb.cartridge_ram_size)) {
            cupid_emulator_shutdown(candidate.get());
            reportError("Battery save read failed: " + save);
            return false;
        }
        std::memcpy(candidate->gb.cartridge_ram, ram.constData(), size_t(ram.size()));
    }
    // Loading a cartridge initializes post-boot state. Apply the boot overlay afterwards.
    const QString bootFolder = settings_.value("emulation/bootDirectory").toString();
    if (!bootFolder.isEmpty()) {
        QString name = "dmg_boot.bin";
        if (model == CUPID_GB_MODEL_CGB)
            name = "cgb_boot.bin";
        else if (model == CUPID_GB_MODEL_MGB)
            name = "mgb_boot.bin";
        else if (model == CUPID_GB_MODEL_SGB)
            name = "sgb_boot.bin";
        else if (model == CUPID_GB_MODEL_SGB2)
            name = "sgb2_boot.bin";
        QFile boot(QDir(bootFolder).filePath(name));
        if (!boot.open(QIODevice::ReadOnly) ||
            boot.size() != (model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100)) {
            cupid_emulator_shutdown(candidate.get());
            reportError("Missing boot ROM or invalid size: " + boot.fileName());
            return false;
        }
        const QByteArray bytes = boot.readAll();
        if (bytes.size() != boot.size()) {
            cupid_emulator_shutdown(candidate.get());
            reportError("Boot ROM read failed.");
            return false;
        }
        std::memcpy(candidate->gb.boot_rom, bytes.constData(), size_t(bytes.size()));
        candidate->gb.boot_rom_size = size_t(bytes.size());
        cupid_gb_enter_boot_rom(&candidate->gb);
    }
    if (emulator_)
        cupid_emulator_shutdown(emulator_.get());
    emulator_ = std::move(candidate);
    ++sessionId_;
    romData_ = data;
    romPath_ = absolute;
    savePath_ = save;
    savedRam_ = QByteArray(reinterpret_cast<const char *>(emulator_->gb.cartridge_ram),
                           qsizetype(emulator_->gb.cartridge_ram_size));
    QStringList recent = settings_.value("files/recent").toStringList();
    recent.removeAll(absolute);
    recent.prepend(absolute);
    while (recent.size() > 10)
        recent.removeLast();
    settings_.setValue("files/recent", recent);
    settings_.setValue("files/lastDirectory", info.absolutePath());
    updateRecentMenu();
    setPaused(false);
    setWindowTitle(info.fileName() + " — CupidGB");
    statusBar()->showMessage(bootFolder.isEmpty() ? "Post-boot startup; no boot ROM selected"
                                                  : "Boot ROM loaded");
    setupAudio();
    present();
    refreshActions();
    return true;
}

bool MainWindow::flushBattery() {
    if (!emulator_ || !emulator_->gb.header.has_battery || !emulator_->gb.cartridge_ram_size)
        return true;
    const QByteArray ram(reinterpret_cast<const char *>(emulator_->gb.cartridge_ram),
                         qsizetype(emulator_->gb.cartridge_ram_size));
    if (ram == savedRam_)
        return true;
    QSaveFile file(savePath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(ram) != ram.size() || !file.commit()) {
        reportError("Cannot save cartridge RAM: " + file.errorString());
        return false;
    }
    savedRam_ = ram;
    return true;
}

bool MainWindow::closeRom() {
    if (!flushBattery())
        return false;
    ++sessionId_;
    releaseKeys();
    stopAudio();
    audioBuffer_.reset();
    if (emulator_)
        cupid_emulator_shutdown(emulator_.get());
    emulator_.reset();
    romPath_.clear();
    romData_.clear();
    image_ = {};
    display_->image = {};
    display_->update();
    setWindowTitle("CupidGB");
    refreshActions();
    return true;
}

bool MainWindow::restart() {
    if (!emulator_)
        return false;
    return openRom(romPath_);
}

void MainWindow::setupAudio() {
    stopAudio();
    audioConverter_.reset();
    audioBuffer_ = std::make_unique<AudioBuffer>();
    if (isolated_)
        return;
    QAudioFormat format;
    format.setSampleRate(CUPID_GB_APU_SAMPLE_RATE);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
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
    audio_ = std::make_unique<QAudioSink>(device, format);
    connect(audio_.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::IdleState && telemetryClock_.isValid() &&
            telemetryClock_.elapsed() > 250)
            ++audioIdleEvents_;
        if (state == QAudio::StoppedState && audio_ && audio_->error() != QAudio::NoError)
            ++audioErrorEvents_;
    });
    audio_->setBufferSize(audioQueueLimit_);
    audio_->setVolume(
        muteAction_->isChecked() ? 0.0f : settings_.value("audio/volume", 100).toFloat() / 100);
    audioDevice_ = audio_->start();
}

void MainWindow::stopAudio() {
    audioDevice_ = nullptr;
    if (audio_) {
        // Disconnect callbacks before releasing the owning pointer: stopping the
        // backend may synchronously emit stateChanged during destruction.
        audio_->disconnect(this);
        audio_->stop();
        audio_.reset();
    }
}

void MainWindow::clearAudio() {
    if (audioConverter_)
        audioConverter_->reset();
    if (audioBuffer_)
        audioBuffer_->clear();
    if (audio_) {
        audioDevice_ = nullptr;
        audio_->reset();
        if (!paused_)
            audioDevice_ = audio_->start();
    }
}

void MainWindow::pumpAudio() {
    if (audio_ && audioDevice_)
        audioBuffer_->writeTo(audioDevice_, audio_->bytesFree());
}

void MainWindow::setPaused(bool value) {
    paused_ = value;
    releaseKeys();
    QSignalBlocker blocker(pauseAction_);
    pauseAction_->setChecked(value);
    nextFrame_ = clock_.nsecsElapsed();
    clearAudio();
    if (value)
        flushBattery();
}

void MainWindow::releaseKeys() {
    turbo_ = false;
    if (emulator_)
        emulator_->gb.joypad = 0xff;
}

void MainWindow::setJoypad(uint8_t mask) {
    auto &gb = emulator_->gb;
    const uint8_t old = cupid_gb_read_u8(&gb, 0xff00);
    gb.joypad = mask;
    const uint8_t current = cupid_gb_read_u8(&gb, 0xff00);
    if ((old & uint8_t(~current) & 0xf) != 0)
        gb.interrupt_flags |= 0x10;
}

void MainWindow::tick() {
    if (!emulator_ || paused_)
        return;
    pumpAudio();
    const qint64 now = clock_.nsecsElapsed();
    if (!turbo_ && now < nextFrame_)
        return;
    // Bound each event-loop turn so dialogs, input, and close remain responsive.
    if (audio_ && !turbo_ && audioBuffer_->bytesAvailable() > audioQueueLimit_)
        return;
    if (!runFrame())
        return;
    nextFrame_ += frameNanoseconds;
    if (turbo_ || now - nextFrame_ > frameNanoseconds * 4)
        nextFrame_ = now;
}

bool MainWindow::runFrame() {
    if (!emulator_)
        return false;
    auto &gb = emulator_->gb;
    gb.frame_ready = false;
    const uint64_t startCycles = gb.elapsed_cycles;
    // With LCD disabled no vblank arrives. Bound the interval with the guest clock.
    for (unsigned steps = 0; steps < 80000; ++steps) {
        if (!inputTrace_.empty()) {
            const uint64_t frame = (gb.elapsed_cycles - telemetryStartCycles_) / 70224;
            while (inputTraceIndex_ < inputTrace_.size() &&
                   inputTrace_[inputTraceIndex_].first <= frame)
                setJoypad(inputTrace_[inputTraceIndex_++].second);
        }
        if (!cupid_emulator_step(emulator_.get())) {
            setPaused(true);
            reportError("CPU execution stopped. The cartridge remains open for inspection.");
            return false;
        }
        if (gb.frame_ready || gb.elapsed_cycles - startCycles >= 70224)
            break;
    }
    int16_t samples[CUPID_GB_APU_BUF_FRAMES * 2];
    const auto frames = cupid_gb_apu_drain(&gb, samples, CUPID_GB_APU_BUF_FRAMES);
    generatedAudioFrames_ += frames;
    if (audio_ && !turbo_ && !paused_) {
        const QByteArray converted = audioConverter_->convert(samples, frames);
        audioBuffer_->append(converted.constData(), converted.size());
        pumpAudio();
    }
    present();
    return true;
}

void MainWindow::present() {
    if (!emulator_)
        return;
    const auto &gb = emulator_->gb;
    if (cupid_gb_sgb_active(&gb)) {
        image_ = QImage(CUPID_SGB_SCREEN_WIDTH, CUPID_SGB_SCREEN_HEIGHT, QImage::Format_ARGB32);
        cupid_gb_sgb_render_argb(&gb, reinterpret_cast<uint32_t *>(image_.bits()),
                                 size_t(image_.width() * image_.height()));
    } else {
        image_ = QImage(160, 144, QImage::Format_RGB32);
        constexpr QRgb palette[] = {0xffc6de8c, 0xff84a563, 0xff396139, 0xff081810};
        auto *pixels = reinterpret_cast<QRgb *>(image_.bits());
        for (size_t i = 0; i < 160 * 144; ++i)
            pixels[i] = (gb.cgb_mode || cupid_cgb_compat_active(&gb))
                            ? rgb555(gb.frame_buffer_color[i])
                            : palette[gb.frame_buffer[i] & 3];
    }
    display_->image = image_;
    ++presentedImages_;
    display_->update();
}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::ApplicationDeactivate || event->type() == QEvent::WindowDeactivate)
        releaseKeys();
    if ((object == display_ || object == this) && emulator_ &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->isAutoRepeat())
            return true;
        const bool pressed = event->type() == QEvent::KeyPress;
        // A modifier pressed after a game button must not suppress that button's release.
        if (pressed &&
            (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
            return QMainWindow::eventFilter(object, event);
        if (key->key() == Qt::Key_Space) {
            turbo_ = pressed;
            clearAudio();
            nextFrame_ = clock_.nsecsElapsed();
            return true;
        }
        for (size_t i = 0; i < keys_.size(); ++i) {
            if (key->key() == keys_[i]) {
                const uint8_t mask = uint8_t(1u << i);
                auto &gb = emulator_->gb;
                if (pressed && !paused_) {
                    setJoypad(gb.joypad & uint8_t(~mask));
                } else {
                    gb.joypad |= mask;
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!closeRom()) {
        event->ignore();
        return;
    }
    settings_.setValue("window/geometry", saveGeometry());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 &&
        event->mimeData()->urls().first().isLocalFile())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1)
        if (openRom(event->mimeData()->urls().first().toLocalFile()))
            event->acceptProposedAction();
}

void MainWindow::refreshActions() {
    for (auto *action : {closeAction_, resetAction_, pauseAction_, frameAction_, screenshotAction_})
        action->setEnabled(bool(emulator_));
}

void MainWindow::screenshot() {
    if (image_.isNull())
        return;
    const bool wasPaused = paused_;
    setPaused(true);
    const QString path =
        QFileDialog::getSaveFileName(this, "Save Screenshot", "CupidGB.png", "PNG image (*.png)");
    if (!path.isEmpty() && !image_.save(path, "PNG"))
        reportError("Could not save screenshot: " + path);
    setPaused(wasPaused);
}

void MainWindow::configure() {
    const bool wasPaused = paused_;
    setPaused(true);
    QDialog dialog(this);
    dialog.setWindowTitle("CupidGB Settings");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *model = new QComboBox;
    model->addItem("Automatic", -1);
    model->addItem("Game Boy", CUPID_GB_MODEL_DMG_ABC);
    model->addItem("Game Boy (DMG0)", CUPID_GB_MODEL_DMG0);
    model->addItem("Game Boy Pocket / Light", CUPID_GB_MODEL_MGB);
    model->addItem("Game Boy Color", CUPID_GB_MODEL_CGB);
    model->addItem("Super Game Boy", CUPID_GB_MODEL_SGB);
    model->addItem("Super Game Boy 2", CUPID_GB_MODEL_SGB2);
    model->setCurrentIndex(model->findData(settings_.value("emulation/model", -1).toInt()));
    form->addRow("Hardware (applies on reset/open)", model);
    auto *bootRow = new QWidget;
    auto *bootLayout = new QHBoxLayout(bootRow);
    bootLayout->setContentsMargins(0, 0, 0, 0);
    auto *boot = new QLineEdit(settings_.value("emulation/bootDirectory").toString());
    boot->setPlaceholderText("Empty: start from post-boot state");
    auto *browse = new QPushButton("Browse…");
    bootLayout->addWidget(boot);
    bootLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString path =
            QFileDialog::getExistingDirectory(&dialog, "Boot ROM directory", boot->text());
        if (!path.isEmpty())
            boot->setText(path);
    });
    form->addRow("Boot ROM directory", bootRow);
    auto *scaling = new QComboBox;
    scaling->addItems({"Integer scale", "Retain aspect ratio"});
    scaling->setCurrentIndex(display_->integerScale ? 0 : 1);
    form->addRow("Display scaling", scaling);
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, 100);
    volume->setValue(settings_.value("audio/volume", 100).toInt());
    form->addRow("Volume", volume);
    std::array<QKeySequenceEdit *, 8> edits;
    for (size_t i = 0; i < edits.size(); ++i) {
        edits[i] = new QKeySequenceEdit(QKeySequence(keys_[i]));
        form->addRow(buttonNames[i], edits[i]);
    }
    layout->addLayout(form);
    auto *note =
        new QLabel("Choose one unmodified key per button. Space is reserved for turbo.\n"
                   "Rewind, save states, controllers, and debugger tools remain incomplete.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::array<int, 8> values{};
        for (size_t i = 0; i < edits.size(); ++i) {
            const auto sequence = edits[i]->keySequence();
            if (sequence.count() != 1 || sequence[0].keyboardModifiers() != Qt::NoModifier ||
                sequence[0].key() == Qt::Key_Space || sequence[0].key() == Qt::Key_unknown) {
                QMessageBox::warning(
                    &dialog, "Input",
                    "Choose one unmodified key for each button. Space is reserved.");
                return;
            }
            values[i] = sequence[0].key();
            for (size_t j = 0; j < i; ++j) {
                if (values[j] == values[i]) {
                    QMessageBox::warning(&dialog, "Input", "Each button needs a different key.");
                    return;
                }
            }
        }
        keys_ = values;
        for (size_t i = 0; i < values.size(); ++i)
            settings_.setValue(QString("input/%1").arg(i), values[i]);
        settings_.setValue("emulation/model", model->currentData());
        settings_.setValue("emulation/bootDirectory", boot->text());
        settings_.setValue("video/integerScale", scaling->currentIndex() == 0);
        settings_.setValue("audio/volume", volume->value());
        display_->integerScale = scaling->currentIndex() == 0;
        display_->update();
        if (audio_)
            audio_->setVolume(muteAction_->isChecked() ? 0.0f : float(volume->value()) / 100);
        dialog.accept();
    });
    dialog.exec();
    setPaused(wasPaused);
}

bool MainWindow::smokeTest() {
    QTemporaryDir directory;
    if (!directory.isValid())
        return false;
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom[0x147] = 0x09; // ROM + RAM + battery, used to check persistence.
    rom[0x149] = 0x02;
    const QString path = directory.filePath(QString::fromUtf8("cartridge-\xc3\xa9.gb"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
        return false;
    file.close();
    if (!openRom(path) || !runFrame() || image_.size() != QSize(160, 144))
        return false;
    pauseAction_->trigger();
    const uint16_t pc = emulator_->gb.cpu.pc;
    tick();
    if (emulator_->gb.cpu.pc != pc || !pauseAction_->isChecked())
        return false;
    setPaused(false);
    const auto beforeTimer = emulator_->gb.elapsed_cycles;
    QEventLoop loop;
    QTimer::singleShot(75, &loop, &QEventLoop::quit);
    loop.exec();
    if (emulator_->gb.elapsed_cycles <= beforeTimer)
        return false;
    emulator_->gb.io_registers[0] = 0x10;
    QKeyEvent down(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(display_, &down);
    if ((emulator_->gb.joypad & 0x10) != 0 || !(emulator_->gb.interrupt_flags & 0x10))
        return false;
    QKeyEvent upWithControl(QEvent::KeyRelease, Qt::Key_X, Qt::ControlModifier);
    QApplication::sendEvent(display_, &upWithControl);
    if ((emulator_->gb.joypad & 0x10) == 0)
        return false;
    QKeyEvent turboDown(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent turboUp(QEvent::KeyRelease, Qt::Key_Space, Qt::AltModifier);
    QApplication::sendEvent(display_, &turboDown);
    QApplication::sendEvent(display_, &turboUp);
    if (turbo_)
        return false;
    QApplication::sendEvent(display_, &down);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(this, &deactivate);
    if (emulator_->gb.joypad != 0xff)
        return false;
    auto *running = emulator_.get();
    if (openRom(directory.filePath("missing.gb")) || emulator_.get() != running)
        return false;
    failed_ = false; // The missing-file error above is intentional.
    QFile saveRom(directory.filePath("not-a-cartridge.sav"));
    if (!saveRom.open(QIODevice::WriteOnly) || saveRom.write(rom) != rom.size())
        return false;
    saveRom.close();
    if (openRom(saveRom.fileName()) || emulator_.get() != running ||
        !saveRom.open(QIODevice::ReadOnly) || saveRom.readAll() != rom)
        return false;
    failed_ = false; // Rejecting a save file must preserve the running cartridge and input bytes.
    const QString shot = directory.filePath("screen.png");
    emulator_->gb.cartridge_ram[23] = 0x5a;
    if (!image_.save(shot) || QImage(shot).size() != image_.size() || !restart() ||
        emulator_->gb.cartridge_ram[23] != 0x5a)
        return false;
    if (!closeRom() || closeAction_->isEnabled())
        return false;
    settings_.setValue("input/4", int(Qt::Key_A));
    settings_.setValue("video/integerScale", false);
    settings_.sync();
    {
        MainWindow restored(true);
        if (restored.keys_[4] != Qt::Key_A || restored.display_->integerScale)
            return false;
    }
    // Exercise model selection and boot overlay after cartridge initialization.
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    settings_.setValue("emulation/bootDirectory", directory.path());
    QFile boot(directory.filePath("cgb_boot.bin"));
    if (!boot.open(QIODevice::WriteOnly) || boot.write(QByteArray(0x900, char(0))) != 0x900)
        return false;
    boot.close();
    if (!openRom(path) || emulator_->gb.model != CUPID_GB_MODEL_CGB ||
        !emulator_->gb.boot_rom_enabled || emulator_->gb.cpu.pc != 0)
        return false;
    if (!closeRom())
        return false;
    qInfo("CupidGB Qt workflow smoke test passed");
    return !failed_;
}

void MainWindow::beginBoundedRun(double seconds, const QString &reportPath) {
    telemetryStartCycles_ = emulator_->gb.elapsed_cycles;
    presentedImages_ = 0;
    generatedAudioFrames_ = 0;
    audioIdleEvents_ = 0;
    display_->paintCount = 0;
    display_->lastPaintAt = 0;
    display_->paintIntervals.clear();
    if (audioBuffer_)
        audioBuffer_->resetTelemetry();
    telemetryClock_.start();
    const uint64_t session = sessionId_;
    QTimer::singleShot(int(seconds * 1000), Qt::PreciseTimer, this, [this, reportPath, session] {
        if (!emulator_ || sessionId_ != session ||
            emulator_->gb.elapsed_cycles < telemetryStartCycles_) {
            qCritical("Bounded frontend run cancelled because the cartridge changed");
            QApplication::exit(1);
            return;
        }
        const double elapsed = double(telemetryClock_.nsecsElapsed()) / 1e9;
        const uint64_t cycles = emulator_->gb.elapsed_cycles - telemetryStartCycles_;
        QJsonObject report{
            {"elapsed_seconds", elapsed},
            {"guest_base_speed_t_cycles", double(cycles)},
            {"guest_cycles_per_second", double(cycles) / elapsed},
            {"guest_speed_percent", double(cycles) / elapsed / 4194304.0 * 100},
            {"images_submitted_to_widget", double(presentedImages_)},
            {"widget_paint_events", double(display_->paintCount)},
            {"audio_frames_generated", double(generatedAudioFrames_)},
            {"audio_idle_events_after_startup", double(audioIdleEvents_)},
            {"audio_error_events", double(audioErrorEvents_)},
            {"audio_sink_created", bool(audio_)},
            {"audio_sink_state", audio_ ? int(audio_->state()) : -1},
            {"audio_sink_error", audio_ ? int(audio_->error()) : -1},
            {"audio_processed_microseconds", audio_ ? double(audio_->processedUSecs()) : 0},
            {"audio_sample_rate", 44100},
            {"audio_channels", 2},
            {"host_audio_sample_rate", audio_ ? audio_->format().sampleRate() : 0},
            {"host_audio_channels", audio_ ? audio_->format().channelCount() : 0},
            {"host_audio_format", audio_ ? int(audio_->format().sampleFormat()) : 0},
            {"qt_version", qVersion()},
            {"platform", QGuiApplication::platformName()},
            {"execution_failed", failed_}};
        report.insert("input_events_applied", double(inputTraceIndex_));
        report.insert("hardware_model", cupid_gb_model_name(emulator_->gb.model));
        report.insert("rom_sha256",
                      QString::fromLatin1(
                          QCryptographicHash::hash(romData_, QCryptographicHash::Sha256).toHex()));
        if (audioBuffer_)
            report.insert("audio_queue", audioBuffer_->telemetry());
        report.insert("widget_paint_interval_ms", display_->paintTelemetry());
        if (!image_.isNull()) {
            const QString screenshotPath = reportPath + ".png";
            report.insert("screenshot_saved", image_.save(screenshotPath, "PNG"));
        }
        report.insert("battery_flushed", flushBattery());
        QSaveFile file(reportPath);
        const QByteArray bytes = QJsonDocument(report).toJson();
        const bool written =
            file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
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

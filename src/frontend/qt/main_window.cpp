#include "main_window.h"
#include "audio_converter.h"
#include "cartridge_file.h"
#include "controller_dialog.h"
#include "cupid/compat/native_v1.h"
#include "cupid/gb/battery.h"
#include "debugger_window.h"
#include "inspectors_window.h"
#include "wave_recorder.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
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
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <limits>

extern "C" {
#include "cupid/gb/apu.h"
#include "cupid/gb/input.h"
#include "cupid/gb/state.h"
#include "cupid/gbc/cgb.h"
}

namespace {
constexpr std::array<int, 8> defaultKeys = {Qt::Key_Right,     Qt::Key_Left,  Qt::Key_Up,
                                            Qt::Key_Down,      Qt::Key_X,     Qt::Key_Z,
                                            Qt::Key_Backspace, Qt::Key_Return};
constexpr std::array<const char *, 8> buttonNames = {"Right", "Left", "Up",     "Down",
                                                     "A",     "B",    "Select", "Start"};
constexpr std::array<const char *, 5> specialNames = {"Turbo", "Rewind", "Slow motion", "Rapid A",
                                                      "Rapid B"};
constexpr std::array<int, 5> defaultSpecialKeys = {Qt::Key_Space, Qt::Key_Tab, Qt::Key_Shift, 0, 0};
constexpr std::array<const char *, 3> frameBlendingNames = {"Disabled", "Simple", "Accurate"};

FrameBlending configuredFrameBlending(const QSettings &settings) {
    bool valid = false;
    const int mode = settings.value("video/blendingMode").toInt(&valid);
    if (valid && mode >= 0 && mode < int(frameBlendingNames.size()))
        return FrameBlending(mode);
    if (settings.contains("video/blending"))
        return settings.value("video/blending").toBool() ? FrameBlending::Simple
                                                         : FrameBlending::Disabled;
    return FrameBlending::Accurate;
}

// LCD geometry stays at 70,224 base cycles; elapsed time follows the model oscillator.
qint64 nominalFrameNanoseconds(const CupidGb *gb, unsigned speedSteps = 16) {
    return qint64(uint64_t(70224) * 1000000000 * 16 /
                  (uint64_t(cupid_gb_base_clock_hz(gb)) * speedSteps));
}

bool smokeFailure(int line) {
    qCritical("CupidGB Qt workflow check failed at main_window.cpp:%d", line);
    return false;
}

Qt::KeyboardModifiers keyModifier(int key) {
    switch (key) {
        case Qt::Key_Shift:
            return Qt::ShiftModifier;
        case Qt::Key_Control:
            return Qt::ControlModifier;
        case Qt::Key_Alt:
            return Qt::AltModifier;
        case Qt::Key_Meta:
            return Qt::MetaModifier;
        default:
            return Qt::NoModifier;
    }
}

int inputKey(int key) {
    // Qt names Shift+Tab separately, although the same physical Tab key remains held.
    return key == Qt::Key_Backtab ? Qt::Key_Tab : key;
}

QRgb rgb555(uint16_t value) {
    const int r = value & 31;
    const int g = (value >> 5) & 31;
    const int b = (value >> 10) & 31;
    return qRgb((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}

bool isStatePath(const QString &path) {
    const QString extension = QFileInfo(path).suffix().toLower();
    const auto digit = [](QChar character) { return character >= '0' && character <= '9'; };
    return (extension.size() == 2 && extension[0] == 's' && digit(extension[1])) ||
           (extension.size() == 3 && extension.startsWith("sn") && digit(extension[2]));
}

CupidGbModel automaticModel(const QByteArray &rom) {
    if (rom.size() <= 0x14b)
        return CUPID_GB_MODEL_DMG_ABC;
    const bool color = (uint8_t(rom[0x143]) & 0x80) != 0;
    const bool super = uint8_t(rom[0x146]) == 3;
    const bool licensed = uint8_t(rom[0x14b]) == 1 ||
                          (uint8_t(rom[0x14b]) == 0x33 && rom[0x144] == '0' && rom[0x145] == '1');
    return color      ? CUPID_GB_MODEL_CGB
           : super    ? CUPID_GB_MODEL_SGB
           : licensed ? CUPID_GB_MODEL_CGB
                      : CUPID_GB_MODEL_DMG_ABC;
}
} // namespace

// Button bindings also accept standalone modifiers and Tab, which ordinary shortcut
// editors treat as unfinished combinations or focus traversal.
class InputKeyEdit final : public QKeySequenceEdit {
  public:
    explicit InputKeyEdit(int key) : QKeySequenceEdit(QKeySequence(key)) {
    }

  protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            const int value = inputKey(key->key());
            if (value == Qt::Key_Tab || keyModifier(value) != Qt::NoModifier) {
                setKeySequence(QKeySequence(value));
                event->accept();
                return true;
            }
        }
        return QKeySequenceEdit::event(event);
    }
};

class DisplayWidget final : public QWidget {
  public:
    explicit DisplayWidget(QWidget *parent) : QWidget(parent) {
        setMinimumSize(160, 144);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        paintClock.start();
    }

    QImage image;
    DisplayScaling scaling = DisplayScaling::Integer;
    uint64_t paintCount = 0;
    QElapsedTimer paintClock;
    qint64 lastPaintAt = 0;
    bool collectPaintIntervals = false;
    std::vector<double> paintIntervals;

    QSize sizeHint() const override {
        return {320, 288};
    }

    QJsonObject paintTelemetry(size_t first = 0) const {
        if (first >= paintIntervals.size())
            return {};
        std::vector<double> intervals(paintIntervals.begin() + first, paintIntervals.end());
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
        if (collectPaintIntervals) {
            const qint64 now = paintClock.nsecsElapsed();
            if (lastPaintAt)
                paintIntervals.push_back(double(now - lastPaintAt) / 1e6);
            lastPaintAt = now;
        }
        const QRectF target = displayViewport(image.size(), size(), scaling, devicePixelRatioF());
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

    qint64 writeTo(QIODevice *device, qint64 maximum) {
        QMutexLocker lock(&mutex_);
        const qint64 available = std::min(maximum, qint64(queue_.size()));
        const qint64 aligned = available - available % bytesPerFrame_;
        if (aligned <= 0)
            return 0;
        const qint64 written = device->write(queue_.constData(), aligned);
        if (written > 0) {
            queue_.remove(0, qsizetype(written));
            bytesRead_ += uint64_t(written);
        }
        return written;
    }

  private:
    mutable QMutex mutex_;
    QByteArray queue_;
    uint64_t bytesRead_ = 0;
    int bytesPerFrame_ = 4;
    qsizetype maximumQueued_ = 0;
};

MainWindow::MainWindow(bool isolated, bool automated, bool audioCheck,
                       const QString &bootDirectoryOverride, std::optional<int> modelOverride)
    : bootDirectoryOverride_(bootDirectoryOverride), modelOverride_(modelOverride),
      isolated_(isolated), automated_(automated), audioCheck_(audioCheck) {
    setWindowTitle("CupidGB");
    setAcceptDrops(true);
    display_ = new DisplayWidget(this);
    setCentralWidget(display_);
    for (size_t i = 0; i < keys_.size(); ++i)
        keys_[i] = settings_.value(QString("input/%1").arg(i), defaultKeys[i]).toInt();
    for (size_t i = 0; i < specialKeys_.size(); ++i)
        specialKeys_[i] =
            settings_.value(QString("input/special/%1").arg(i), defaultSpecialKeys[i]).toInt();
    if (!settings_.contains("video/scaling"))
        settings_.setValue("video/scaling", settings_.value("video/integerScale", true).toBool()
                                                ? int(DisplayScaling::Integer)
                                                : int(DisplayScaling::Aspect));
    buildMenus();
    applyDisplaySettings();
    resizeDisplay(std::clamp(settings_.value("video/defaultScale", 2).toInt(), 1, 8));
    if (!isolated_)
        restoreGeometry(settings_.value("window/geometry").toByteArray());
    qApp->installEventFilter(this);
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(1);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    connect(&saveTimer_, &QTimer::timeout, this, [this] { flushBattery(); });
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(250);
    mediaDevices_ = new QMediaDevices(this);
    audioRefreshTimer_.setSingleShot(true);
    connect(mediaDevices_, &QMediaDevices::audioOutputsChanged, this,
            [this] { audioRefreshTimer_.start(0); });
    connect(&audioRefreshTimer_, &QTimer::timeout, this, [this] {
        if (!emulator_ || (isolated_ && !audioCheck_))
            return;
        if (stateOperation_) {
            audioRefreshTimer_.start(10);
            return;
        }
        setupAudio();
        resetPacing();
    });
    timer_.start();
    clock_.start();
    debuggerTimer_.setInterval(0);
    connect(&debuggerTimer_, &QTimer::timeout, this, [this] { debuggerSlice(); });
    controllers_ = std::make_unique<cupid::input::ControllerManager>();
    bool validControllers = false;
    controllers_->configure(cupid::input::ControllerConfiguration::load(
        settings_.value("controllers/configuration").toByteArray(), &validControllers));
    controllers_->outputChanged = [this](const cupid::input::ControllerOutput &output) {
        controllerOutput_ = output;
        reconcileControllerInput();
    };
    controllers_->shortcutPressed = [this](const QString &id) {
        QTimer::singleShot(0, this, [this, id] {
            if (!controllers_->deliveryEnabled() || stateOperation_ ||
                QApplication::activeModalWidget())
                return;
            if (id == "menu") {
                auto *menu = menuBar()->actions().value(1)->menu();
                menu->popup(menuBar()->mapToGlobal(
                    menuBar()->actionGeometry(menu->menuAction()).bottomLeft()));
                return;
            }
            for (auto *action : shortcutActions_)
                if (action->objectName() == id && action->isEnabled()) {
                    action->trigger();
                    return;
                }
        });
    };
    controllers_->navigationPressed = [this](const QString &action, SDL_JoystickID instance) {
        navigateControllerMenu(action, instance);
    };
    QString controllerError;
    if (!controllers_->backend().start(&controllerError))
        statusBar()->showMessage("Controllers unavailable: " + controllerError);
    else if (!validControllers)
        statusBar()->showMessage(
            "Invalid controller settings; using defaults until settings are saved.");
    refreshControllerGate();
    for (auto *menu : findChildren<QMenu *>()) {
        connect(menu, &QMenu::aboutToShow, this, [this] {
            if (menuDepth_++ == 0) {
                releaseKeys();
                clearAudio();
                resetControllerRumble();
                resetPacing();
            }
            controllers_->setDeliveryEnabled(false);
        });
        connect(menu, &QMenu::aboutToHide, this, [this] {
            if (menuDepth_)
                --menuDepth_;
            if (!menuDepth_)
                QTimer::singleShot(0, this, [this] {
                    if (!menuDepth_) {
                        refreshControllerGate();
                        clearAudio();
                        resetPacing();
                    }
                });
        });
    }
    refreshActions();
}

MainWindow::~MainWindow() {
    if (controllers_) {
        controllers_->outputChanged = {};
        controllers_->shortcutPressed = {};
        controllers_->physicalEvent = {};
        controllers_->navigationPressed = {};
        controllers_->backend().stop();
    }
    timer_.stop();
    saveTimer_.stop();
    audioRefreshTimer_.stop();
    debuggerTimer_.stop();
    if (debuggerWindow_)
        debuggerWindow_->setHandlers({}, {}, {});
    if (inspectorsWindow_)
        inspectorsWindow_->setSession({}, {});
    if (debugger_)
        cupid_gb_debugger_cleanup(debugger_.get());
    paused_ = true;
    qApp->removeEventFilter(this);
    stopAudio();
    if (recorder_ && !recorder_->finish())
        qCritical("Cannot finalize audio recording: %s", qPrintable(recorder_->errorString()));
    setPrinterConnected(false);
    if (emulator_) {
        flushBattery();
        cupid_emulator_shutdown(emulator_.get());
    }
}

void MainWindow::buildMenus() {
    auto *file = menuBar()->addMenu("&File");
    auto *open = file->addAction("&Open ROM…", QKeySequence("Ctrl+O"));
    registerShortcut(open, "open");
    connect(open, &QAction::triggered, this, [this] {
        const bool wasPaused = paused_;
        setPaused(true);
        const QString path = QFileDialog::getOpenFileName(
            this, "Open ROM", settings_.value("files/lastDirectory").toString(),
            "Game Boy cartridges (*.gb *.gbc *.sgb *.zip *.7z *.rar);;All files (*)");
        if (path.isEmpty() || !openRom(path))
            setPaused(wasPaused);
    });
    recentMenu_ = file->addMenu("Open &Recent");
    updateRecentMenu();
    loadStateFileAction_ = file->addAction("Load State File…");
    connect(loadStateFileAction_, &QAction::triggered, this, [this] {
        QScopedValueRollback<bool> operation(stateOperation_, true);
        const QString path = QFileDialog::getOpenFileName(
            this, "Load State File", QFileInfo(romPath_).absolutePath(),
            "State files (*.s? *.sn?);;All files (*)");
        if (!path.isEmpty())
            loadStateFile(path);
        resetPacing();
    });
    auto *recover = file->addAction("Recover Battery from Older State…");
    connect(recover, &QAction::triggered, this, [this] {
        if (!emulator_) {
            reportError("Open the matching cartridge before recovering an older state.");
            return;
        }
        QScopedValueRollback<bool> operation(stateOperation_, true);
        const QString source = QFileDialog::getOpenFileName(
            this, "Choose Older State", {}, "State files (*.s? *.sn?);;All files (*)");
        if (source.isEmpty())
            return;
        const QString destination = QFileDialog::getSaveFileName(
            this, "Export Recovered Battery",
            QFileInfo(romPath_).dir().filePath(QFileInfo(romPath_).completeBaseName() +
                                               "-recovered.sav"),
            "Battery saves (*.sav)");
        if (!destination.isEmpty())
            recoverBattery(source, destination);
        resetPacing();
    });
    closeAction_ = file->addAction("&Close ROM", QKeySequence("Ctrl+W"));
    registerShortcut(closeAction_, "close");
    connect(closeAction_, &QAction::triggered, this, [this] { closeRom(); });
    file->addSeparator();
    auto *quit = file->addAction("&Quit", QKeySequence("Ctrl+Q"));
    registerShortcut(quit, "quit");
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto *emulation = menuBar()->addMenu("&Emulation");
    resetAction_ = emulation->addAction("&Reset", QKeySequence("Ctrl+R"));
    registerShortcut(resetAction_, "reset");
    connect(resetAction_, &QAction::triggered, this, [this] { restart(); });
    pauseAction_ = emulation->addAction("&Pause", QKeySequence("Ctrl+P"));
    registerShortcut(pauseAction_, "pause");
    pauseAction_->setCheckable(true);
    connect(pauseAction_, &QAction::toggled, this, [this](bool value) { setPaused(value); });
    frameAction_ = emulation->addAction("Advance &Frame", QKeySequence("Ctrl+N"));
    registerShortcut(frameAction_, "frame");
    connect(frameAction_, &QAction::triggered, this, [this] {
        const auto previousStop = debugger_ ? debugger_->last_stop : CUPID_GB_DEBUG_STOP_NONE;
        setPaused(true);
        resetDebuggerTimeline();
        if (debugger_ && emulator_) {
            if (previousStop == CUPID_GB_DEBUG_STOP_BREAKPOINT)
                debugger_->last_stop = previousStop;
            cupid_gb_debugger_begin(debugger_.get(), &emulator_->gb, CUPID_GB_DEBUG_RUN);
        }
        runFrame();
        clearAudio();
        refreshDebugger();
    });
    auto *saveStates = emulation->addMenu("Save State");
    auto *loadStates = emulation->addMenu("Load State");
    for (int ordinal = 1; ordinal <= 10; ++ordinal) {
        const int slot = ordinal % 10;
        // The 0 key selects the tenth slot; filenames retain their numeric suffix.
        const QString title = QString("Slot %1").arg(slot ? slot : 10);
        saveSlotActions_[size_t(slot)] =
            saveStates->addAction(title, QKeySequence(QString("Ctrl+%1").arg(slot)));
        loadSlotActions_[size_t(slot)] =
            loadStates->addAction(title, QKeySequence(QString("Ctrl+Shift+%1").arg(slot)));
        registerShortcut(saveSlotActions_[size_t(slot)], QString("save/%1").arg(slot));
        registerShortcut(loadSlotActions_[size_t(slot)], QString("load/%1").arg(slot));
        connect(saveSlotActions_[size_t(slot)], &QAction::triggered, this,
                [this, slot] { saveSlot(slot); });
        connect(loadSlotActions_[size_t(slot)], &QAction::triggered, this,
                [this, slot] { loadSlot(slot); });
    }
    auto *view = menuBar()->addMenu("&View");
    auto *fullscreen = view->addAction("&Fullscreen", QKeySequence("Ctrl+F"));
    registerShortcut(fullscreen, "fullscreen");
    fullscreen->setCheckable(true);
    connect(fullscreen, &QAction::toggled, this,
            [this](bool value) { value ? showFullScreen() : showNormal(); });
    screenshotAction_ = view->addAction("Save &Screenshot…", QKeySequence("F12"));
    registerShortcut(screenshotAction_, "screenshot");
    connect(screenshotAction_, &QAction::triggered, this, [this] { screenshot(); });
    auto *scaling = view->addMenu("Display &Scaling");
    auto *scalingGroup = new QActionGroup(this);
    const std::array<const char *, 3> scaleNames = {"Integer Scale", "Retain Aspect Ratio",
                                                    "Fill Window"};
    for (size_t i = 0; i < scalingActions_.size(); ++i) {
        auto *action = scaling->addAction(scaleNames[i]);
        action->setObjectName(QString("video/scaling/%1").arg(i));
        action->setCheckable(true);
        scalingGroup->addAction(action);
        scalingActions_[i] = action;
        connect(action, &QAction::triggered, this, [this, i] {
            settings_.setValue("video/scaling", int(i));
            applyDisplaySettings();
        });
    }
    auto *windowSize = view->addMenu("&Window Size");
    auto *sizeGroup = new QActionGroup(this);
    for (size_t i = 0; i < windowSizeActions_.size(); ++i) {
        auto *action = windowSize->addAction(QString("%1x").arg(i + 1));
        action->setObjectName(QString("video/size/%1").arg(i + 1));
        action->setCheckable(true);
        sizeGroup->addAction(action);
        windowSizeActions_[i] = action;
        connect(action, &QAction::triggered, this, [this, i] {
            settings_.setValue("video/defaultScale", int(i + 1));
            resizeDisplay(int(i + 1));
        });
    }
    auto *palette = view->addMenu("&Monochrome Palette");
    auto *paletteGroup = new QActionGroup(this);
    const std::array<const char *, 5> paletteNames = {"Lime (Game Boy)", "Grayscale",
                                                      "Olive (Pocket)", "Teal (Light)", "Custom"};
    for (size_t i = 0; i < paletteActions_.size(); ++i) {
        auto *action = palette->addAction(paletteNames[i]);
        action->setObjectName(QString("video/palette/%1").arg(i));
        action->setCheckable(true);
        paletteGroup->addAction(action);
        paletteActions_[i] = action;
        connect(action, &QAction::triggered, this, [this, i] {
            settings_.setValue("video/palette", int(i));
            applyDisplaySettings();
        });
    }
    auto *blending = view->addMenu("Frame &Blending");
    auto *blendingGroup = new QActionGroup(this);
    for (size_t i = 0; i < blendingActions_.size(); ++i) {
        auto *action = blending->addAction(frameBlendingNames[i]);
        action->setObjectName(QString("video/blendingMode/%1").arg(i));
        action->setCheckable(true);
        blendingGroup->addAction(action);
        blendingActions_[i] = action;
        connect(action, &QAction::triggered, this, [this, i] {
            settings_.setValue("video/blendingMode", int(i));
            setFrameBlending(FrameBlending(i));
            present();
        });
    }
    borderAction_ = view->addAction("Show &SGB Border");
    borderAction_->setCheckable(true);
    borderAction_->setObjectName("video/sgbBorder");
    connect(borderAction_, &QAction::triggered, this, [this](bool enabled) {
        settings_.setValue("video/sgbBorder", enabled);
        applyDisplaySettings();
    });
    auto *audio = menuBar()->addMenu("&Audio");
    muteAction_ = audio->addAction("&Mute", QKeySequence("Ctrl+M"));
    registerShortcut(muteAction_, "mute");
    muteAction_->setCheckable(true);
    muteAction_->setChecked(settings_.value("audio/mute", false).toBool());
    connect(muteAction_, &QAction::toggled, this, [this](bool muted) {
        settings_.setValue("audio/mute", muted);
        if (audio_)
            audio_->setVolume(muted ? 0.0f : settings_.value("audio/volume", 100).toFloat() / 100);
    });
    recordingAction_ = audio->addAction("Start Audio &Recording…");
    recordingAction_->setToolTip(
        "Record forward stereo audio before host mute, volume, and resampling. "
        "Pause and rewind add no audio. Reset or a sample-rate change ends the file.");
    registerShortcut(recordingAction_, "recording");
    connect(recordingAction_, &QAction::triggered, this, [this] {
        if (recorder_ && recorder_->active()) {
            stopRecording();
            return;
        }
        const bool wasPaused = paused_;
        setPaused(true);
        const QString path = QFileDialog::getSaveFileName(
            this, "Record Audio", QFileInfo(romPath_).completeBaseName() + ".wav",
            "WAVE audio (*.wav)");
        if (!path.isEmpty())
            startRecording(path);
        setPaused(wasPaused);
    });
    emulation->addSeparator();
    auto *settings = emulation->addAction("&Settings…", QKeySequence("Ctrl+,"));
    registerShortcut(settings, "settings");
    connect(settings, &QAction::triggered, this, [this] { configure(); });
    auto *controllers = emulation->addAction("&Controllers…");
    registerShortcut(controllers, "controllers");
    connect(controllers, &QAction::triggered, this, [this] { configureControllers(); });
    connectionsMenu_ = emulation->addMenu("&Connect");
    auto *accessories = new QActionGroup(connectionsMenu_);
    disconnectAction_ = connectionsMenu_->addAction("&None");
    printerAction_ = connectionsMenu_->addAction("Game Boy &Printer");
    for (auto *action : {disconnectAction_, printerAction_}) {
        action->setCheckable(true);
        accessories->addAction(action);
    }
    disconnectAction_->setChecked(true);
    connect(disconnectAction_, &QAction::triggered, this, [this] { setPrinterConnected(false); });
    connect(printerAction_, &QAction::triggered, this, [this] { setPrinterConnected(true); });
    auto *tools = menuBar()->addMenu("&Tools");
    debuggerAction_ = tools->addAction("&Debugger", QKeySequence("Ctrl+C"));
    registerShortcut(debuggerAction_, "debugger");
    connect(debuggerAction_, &QAction::triggered, this, [this] { openDebugger(); });
    inspectorsAction_ = tools->addAction("&Memory and Graphics");
    connect(inspectorsAction_, &QAction::triggered, this, [this] { openInspectors(); });
    auto *help = menuBar()->addMenu("&Help");
    connect(help->addAction("&Controls"), &QAction::triggered, this, [this] {
        const bool wasPaused = paused_;
        setPaused(true);
        QMessageBox::information(
            this, "Controls",
            "Default controls: arrows, X (A), Z (B), Backspace (Select), Enter (Start).\n"
            "Hold Space for turbo. Hold Tab to rewind. Change button mappings in Settings.\n\n"
            "Hold Shift for slow motion. Rapid A/B keys are configurable and start unassigned.\n\n"
            "Open: Ctrl+O   Reset: Ctrl+R   Pause: Ctrl+P\n"
            "Fullscreen: Ctrl+F   Mute: Ctrl+M   Screenshot: F12\n\n"
            "Save state: Ctrl+0–9   Load state: Ctrl+Shift+0–9\n\n"
            "Debugger: Ctrl+C. Use Tools for memory and graphics inspectors.\n"
            "Controller devices remain incomplete.");
        setPaused(wasPaused);
    });
    connect(help->addAction("&About CupidGB"), &QAction::triggered, this, [this] {
        const bool wasPaused = paused_;
        setPaused(true);
        QMessageBox::about(this, "CupidGB",
                           QString("CupidGB %1\nGame Boy and Game Boy Color emulator\n"
                                   "See LICENSE for licensing terms.")
                               .arg(QCoreApplication::applicationVersion()));
        setPaused(wasPaused);
    });
}

void MainWindow::applyDisplaySettings() {
    const int scaling = std::clamp(settings_.value("video/scaling", 0).toInt(), 0, 2);
    display_->scaling = DisplayScaling(scaling);
    const int palette = std::clamp(settings_.value("video/palette", 0).toInt(), 0, 4);
    for (size_t i = 0; i < scalingActions_.size(); ++i)
        scalingActions_[i]->setChecked(int(i) == scaling);
    for (size_t i = 0; i < paletteActions_.size(); ++i)
        paletteActions_[i]->setChecked(int(i) == palette);
    const int scale = std::clamp(settings_.value("video/defaultScale", 2).toInt(), 1, 8);
    for (size_t i = 0; i < windowSizeActions_.size(); ++i)
        windowSizeActions_[i]->setChecked(int(i) + 1 == scale);
    auto custom = monochromeColors(MonochromePalette::Lime, {});
    for (size_t i = 0; i < custom.size(); ++i) {
        const QColor value(
            settings_.value(QString("video/customPalette/%1").arg(i), QColor(custom[i]).name())
                .toString());
        if (value.isValid())
            custom[i] = value.rgb();
    }
    const auto colors = monochromeColors(MonochromePalette(palette), custom);
    const bool showBorder = settings_.value("video/sgbBorder", true).toBool();
    const bool borderChanged = emulator_ && cupid_gb_sgb_active(&emulator_->gb) &&
                               !image_.isNull() &&
                               (image_.width() == CUPID_SGB_SCREEN_WIDTH) != showBorder;
    if (displayPalette_ != colors || borderChanged) {
        previousDisplayFrame_ = {};
        currentDisplayFrame_ = {};
    }
    displayPalette_ = colors;
    setFrameBlending(configuredFrameBlending(settings_));
    borderAction_->setChecked(showBorder);
    if (emulator_)
        present();
    else
        display_->update();
}

void MainWindow::setFrameBlending(FrameBlending mode) {
    frameBlending_ = mode;
    blendingActions_[size_t(mode)]->setChecked(true);
}

void MainWindow::resizeDisplay(int scale) {
    scale = std::clamp(scale, 1, 8);
    if (isFullScreen()) {
        showNormal();
        if (auto *fullscreen = findChild<QAction *>("fullscreen"))
            fullscreen->setChecked(false);
    }
    const QSize source = image_.isNull() ? QSize(160, 144) : image_.size();
    const int chromeHeight = menuBar()->sizeHint().height() + statusBar()->sizeHint().height();
    resize(std::max(source.width() * scale, menuBar()->sizeHint().width()),
           source.height() * scale + chromeHeight);
    for (size_t i = 0; i < windowSizeActions_.size(); ++i)
        windowSizeActions_[i]->setChecked(int(i) + 1 == scale);
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
    else {
        QScopedValueRollback<bool> operation(stateOperation_, true);
        QMessageBox::critical(this, "CupidGB", message);
        resetPacing();
    }
}

bool MainWindow::openRom(const QString &path) {
    if (isStatePath(path)) {
        reportError(
            "State files cannot be opened as cartridges. Open a cartridge, then load its state.");
        return false;
    }
    if (QFileInfo(path).suffix().compare("sav", Qt::CaseInsensitive) == 0) {
        reportError("Battery save files cannot be opened as cartridges.");
        return false;
    }
    QString error;
    const QByteArray data = readCartridgeFile(path, error);
    if (data.isEmpty()) {
        reportError(error);
        return false;
    }
    auto candidate = std::make_unique<CupidEmulator>();
    int model = modelOverride_.value_or(settings_.value("emulation/model", -1).toInt());
    if (model < 0)
        model = automaticModel(data);
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
    const QString save = info.dir().filePath(cartridgeStorageName(info) + ".sav");
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
    if (cupid_gb_battery_size(&candidate->gb) && battery.exists()) {
        if (!battery.open(QIODevice::ReadOnly) || battery.size() > 128 * 1024 + 48) {
            cupid_emulator_shutdown(candidate.get());
            reportError("Cannot load battery save or its size does not match cartridge RAM: " +
                        save);
            return false;
        }
        const QByteArray ram = battery.readAll();
        if (ram.size() != battery.size()) {
            cupid_emulator_shutdown(candidate.get());
            reportError("Battery save read failed: " + save);
            return false;
        }
        const auto result = cupid_gb_load_battery(
            &candidate->gb, reinterpret_cast<const uint8_t *>(ram.constData()), size_t(ram.size()));
        if (result != CUPID_GB_BATTERY_OK) {
            cupid_emulator_shutdown(candidate.get());
            reportError("Cannot load battery save: " +
                        QString::fromLatin1(cupid_gb_battery_result_name(result)));
            return false;
        }
    }
    // Loading a cartridge initializes post-boot state. Apply the boot overlay afterwards.
    const QString bootFolder = bootDirectory();
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
    if (!stopRecording()) {
        cupid_emulator_shutdown(candidate.get());
        return false;
    }
    const bool sameCartridge = emulator_ && romData_ == data &&
                               romPath_.compare(absolute, pathCase) == 0 &&
                               emulator_->gb.model == candidate->gb.model;
    if (!sameCartridge)
        setPrinterConnected(false);
    if (emulator_)
        cupid_emulator_shutdown(emulator_.get());
    emulator_ = std::move(candidate);
    resetPrinter();
    cupid_gb_set_rumble_callback(
        &emulator_->gb,
        [](const CupidGb *gb, bool enabled, uint64_t cycles, void *user) {
            auto *window = static_cast<MainWindow *>(user);
            if (!window->emulator_ || &window->emulator_->gb != gb)
                return;
            if (cycles >= window->rumbleLastCycles_ && window->rumbleMotor_)
                window->rumbleOnCycles_ += cycles - window->rumbleLastCycles_;
            window->rumbleLastCycles_ = cycles;
            window->rumbleMotor_ = enabled;
        },
        this);
    resetControllerRumble();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    resetRewind();
    ++sessionId_;
    resetDebuggerTimeline(!sameCartridge);
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    romData_ = data;
    romPath_ = absolute;
    if (debugger_ && !sameCartridge) {
        const QString symbols = info.dir().filePath(info.completeBaseName() + ".sym");
        if (QFileInfo::exists(symbols))
            loadDebuggerSymbols(symbols);
    }
    savePath_ = save;
    saveTimer_.stop();
    observedBatteryGeneration_ = emulator_->gb.battery_write_generation;
    batteryDirty_ = false;
    savedRam_.clear();
    if (cupid_gb_battery_size(&emulator_->gb)) {
        savedRam_.resize(qsizetype(cupid_gb_battery_size(&emulator_->gb)));
        size_t written = 0;
        if (cupid_gb_save_battery(&emulator_->gb, reinterpret_cast<uint8_t *>(savedRam_.data()),
                                  size_t(savedRam_.size()), &written) != CUPID_GB_BATTERY_OK)
            savedRam_.clear();
    }
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
    rememberFrame();
    refreshActions();
    return true;
}

bool MainWindow::flushBattery() {
    if (!emulator_ || !cupid_gb_battery_size(&emulator_->gb))
        return true;
    // The generation excludes natural RTC advancement. Clock reads alone must not rewrite saves.
    if (!batteryDirty_ && observedBatteryGeneration_ == emulator_->gb.battery_write_generation)
        return true;
    QByteArray ram(qsizetype(cupid_gb_battery_size(&emulator_->gb)), Qt::Uninitialized);
    size_t written = 0;
    const auto result = cupid_gb_save_battery(
        &emulator_->gb, reinterpret_cast<uint8_t *>(ram.data()), size_t(ram.size()), &written);
    if (result != CUPID_GB_BATTERY_OK || written != size_t(ram.size())) {
        reportError("Cannot serialize battery save: " +
                    QString::fromLatin1(cupid_gb_battery_result_name(result)));
        return false;
    }
    if (ram != savedRam_) {
        QSaveFile file(savePath_);
        if (!file.open(QIODevice::WriteOnly) || file.write(ram) != ram.size() || !file.commit()) {
            reportError("Cannot save cartridge RAM: " + file.errorString());
            return false;
        }
        savedRam_ = ram;
    }
    observedBatteryGeneration_ = emulator_->gb.battery_write_generation;
    batteryDirty_ = false;
    saveTimer_.stop();
    return true;
}

void MainWindow::watchBattery() {
    if (emulator_ && observedBatteryGeneration_ != emulator_->gb.battery_write_generation) {
        observedBatteryGeneration_ = emulator_->gb.battery_write_generation;
        batteryDirty_ = true;
        saveTimer_.start();
    }
}

bool MainWindow::closeRom() {
    if (!flushBattery() || !stopRecording())
        return false;
    setPrinterConnected(false);
    ++sessionId_;
    resetControllerRumble();
    resetDebuggerTimeline(true);
    saveTimer_.stop();
    batteryDirty_ = false;
    releaseKeys();
    stopAudio();
    audioBuffer_.reset();
    if (emulator_)
        cupid_emulator_shutdown(emulator_.get());
    emulator_.reset();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    resetRewind();
    romPath_.clear();
    romData_.clear();
    image_ = {};
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    display_->image = {};
    display_->setMinimumSize(160, 144);
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
        audio_->stop();
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

void MainWindow::setPaused(bool value) {
    if (!value)
        debuggerInputPaused_ = false;
    if (debugger_) {
        const auto previousStop = debugger_->last_stop;
        debuggerTimer_.stop();
        if (!value) {
            resetDebuggerTimeline();
            if (previousStop == CUPID_GB_DEBUG_STOP_BREAKPOINT)
                debugger_->last_stop = previousStop;
            if (emulator_)
                cupid_gb_debugger_begin(debugger_.get(), &emulator_->gb, CUPID_GB_DEBUG_RUN);
        } else {
            cupid_gb_debugger_reset_execution(debugger_.get());
            if (previousStop == CUPID_GB_DEBUG_STOP_BREAKPOINT)
                debugger_->last_stop = previousStop;
        }
    }
    paused_ = value;
    resetControllerRumble();
    releaseKeys();
    QSignalBlocker blocker(pauseAction_);
    pauseAction_->setChecked(value);
    resetPacing();
    clearAudio();
    if (value)
        flushBattery();
    refreshDebugger();
}

void MainWindow::releaseKeys() {
    keyboardSpecialHeld_.fill(false);
    turbo_ = false;
    rewind_ = false;
    slowHeld_ = false;
    speedSteps_ = 16;
    if (audioConverter_)
        audioConverter_->setInputRate(
            cupid_gb_apu_get_sample_rate(emulator_ ? &emulator_->gb : nullptr));
    rapidHeld_.fill(false);
    rapidPhase_.fill(0);
    physicalJoypad_ = 0xff;
    reconcileControllerInput();
}

void MainWindow::registerShortcut(QAction *action, const QString &id) {
    action->setObjectName(id);
    const auto stored = settings_.value("shortcuts/" + id);
    if (stored.isValid())
        action->setShortcut(
            QKeySequence::fromString(stored.toString(), QKeySequence::PortableText));
    shortcutActions_.push_back(action);
}

void MainWindow::setJoypad(uint8_t mask) {
    cupid_gb_set_buttons(&emulator_->gb, mask);
}

void MainWindow::applyHeldButtons() {
    if (!emulator_)
        return;
    applyPlayerButtons(livePlayerButtons());
}

std::array<uint8_t, 4> MainWindow::livePlayerButtons() const {
    std::array<uint8_t, 4> buttons{0xff, 0xff, 0xff, 0xff};
    if (paused_)
        return buttons;
    buttons = controllerOutput_.buttons;
    buttons[0] &= physicalJoypad_;
    for (size_t action = 0; action < rapidHeld_.size(); ++action) {
        if (!rapidHeld_[action])
            continue;
        const uint8_t button = uint8_t(1u << (action + 4));
        if (rapidPhase_[action] < 2)
            buttons[0] &= uint8_t(~button);
    }
    for (size_t player = 0; player < 4; ++player)
        for (size_t action = 0; action < 2; ++action)
            if (controllerRapidHeld_[player][action] && controllerRapidPhase_[player][action] < 2)
                buttons[player] &= uint8_t(~(1u << (action + 4)));
    return buttons;
}

void MainWindow::applyPlayerButtons(const std::array<uint8_t, 4> &buttons) {
    controllerPlayerCount_ =
        cupid_gb_sgb_active(&emulator_->gb) ? emulator_->gb.sgb.player_count : 1;
    uint8_t first = buttons[0];
    if (controllerPlayerCount_ == 1)
        for (unsigned player = 1; player < 4; ++player)
            first &= buttons[player];
    setJoypad(first);
    for (unsigned player = 1; player < 4; ++player)
        cupid_gb_set_buttons_for_player(&emulator_->gb, player, buttons[player]);
}

void MainWindow::refreshControllerPlayerCount() {
    if (!emulator_)
        return;
    const unsigned count = cupid_gb_sgb_active(&emulator_->gb) ? emulator_->gb.sgb.player_count : 1;
    if (count == controllerPlayerCount_)
        return;
    // An SGB command can change routing inside one guest frame. Update before the next opcode.
    applyPlayerButtons(paused_ && debuggerInputPaused_ ? debuggerPlayerButtons_
                                                       : livePlayerButtons());
}

void MainWindow::reconcileControllerInput() {
    const bool oldTurbo = turbo_, oldRewind = rewind_;
    const auto held = [this](size_t action) {
        return !paused_ && (keyboardSpecialHeld_[action] || controllerOutput_.held[action]);
    };
    turbo_ = held(0);
    rewind_ = held(1);
    slowHeld_ = held(2);
    for (size_t i = 0; i < rapidHeld_.size(); ++i) {
        const bool next = !paused_ && keyboardSpecialHeld_[i + 3];
        if (rapidHeld_[i] != next)
            rapidPhase_[i] = 0;
        rapidHeld_[i] = next;
    }
    for (size_t player = 0; player < 4; ++player)
        for (size_t action = 0; action < 2; ++action) {
            const bool next = !paused_ && controllerOutput_.rapid[player][action];
            if (controllerRapidHeld_[player][action] != next)
                controllerRapidPhase_[player][action] = 0;
            controllerRapidHeld_[player][action] = next;
        }
    if (oldTurbo != turbo_ || oldRewind != rewind_) {
        clearAudio();
        resetPacing();
    }
    if (!(paused_ && debuggerInputPaused_) && !stateOperation_)
        applyHeldButtons();
}

void MainWindow::refreshControllerGate() {
    if (!controllers_)
        return;
    const bool focused = QApplication::applicationState() == Qt::ApplicationActive;
    controllers_->setFocused(focused || (isolated_ && !isVisible()));
    const auto *active = QApplication::activeWindow();
    const bool otherWindow = active && active != this;
    const bool blocked = (isolated_ && !controllerSmokeActive_) || menuDepth_ || stateOperation_ ||
                         QApplication::activeModalWidget() || QApplication::activePopupWidget() ||
                         otherWindow || !inputTrace_.empty();
    controllers_->setDeliveryEnabled(!blocked);
}

void MainWindow::navigateControllerMenu(const QString &action, SDL_JoystickID instance) {
    (void)instance;
    QWidget *target = QApplication::activePopupWidget();
    if (!target)
        target = QApplication::activeModalWidget();
    if (!target || QApplication::applicationState() != Qt::ApplicationActive ||
        target->property("controllerCaptureActive").toBool() ||
        target->property("controllerCaptureConsumed").toBool())
        return;
    const QStringList actions{"right", "left", "up", "down", "a", "b", "menu", "turbo", "rewind"};
    const std::array<int, 9> keys{Qt::Key_Right,  Qt::Key_Left,   Qt::Key_Up,
                                  Qt::Key_Down,   Qt::Key_Return, Qt::Key_Escape,
                                  Qt::Key_Escape, Qt::Key_Tab,    Qt::Key_Backtab};
    int value = 0;
    for (int i = 0; i < actions.size(); ++i)
        if (action == actions[i]) {
            value = keys[size_t(i)];
            break;
        }
    if (!value)
        return;
    QPointer<QWidget> guarded(target);
    QTimer::singleShot(0, this, [this, guarded, value] {
        if (!guarded || (QApplication::activePopupWidget() != guarded &&
                         QApplication::activeModalWidget() != guarded))
            return;
        QPointer<QWidget> receiver = guarded->focusWidget();
        // Menus receive keys directly; dialogs use their current visible focus child.
        if (qobject_cast<QMenu *>(guarded.data()) || !receiver || !receiver->isVisible() ||
            receiver->window() != guarded->window())
            receiver = guarded;
        int nativeKey = value;
        if (value == Qt::Key_Return && !qobject_cast<QMenu *>(guarded.data()) &&
            (qobject_cast<QAbstractButton *>(receiver.data()) ||
             qobject_cast<QComboBox *>(receiver.data())))
            nativeKey = Qt::Key_Space;
        QKeyEvent press(QEvent::KeyPress, nativeKey, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, nativeKey, Qt::NoModifier);
        QApplication::sendEvent(receiver, &press);
        if (guarded && receiver)
            QApplication::sendEvent(receiver, &release);
    });
}

void MainWindow::resetControllerRumble() {
    if (controllers_)
        controllers_->stopRumble();
    rumbleOnCycles_ = 0;
    rumbleWindowCycles_ = rumbleLastCycles_ = emulator_ ? emulator_->gb.elapsed_cycles : 0;
    rumbleMotor_ = emulator_ && cupid_gb_rumble_enabled(&emulator_->gb);
}

void MainWindow::finishControllerRumble() {
    if (!emulator_ || !controllers_)
        return;
    const uint64_t now = emulator_->gb.elapsed_cycles;
    if (now < rumbleLastCycles_ || now <= rumbleWindowCycles_) {
        resetControllerRumble();
        return;
    }
    if (rumbleMotor_)
        rumbleOnCycles_ += now - rumbleLastCycles_;
    controllers_->setRumble(!paused_ && !rewind_ && !stateOperation_
                                ? double(rumbleOnCycles_) / double(now - rumbleWindowCycles_)
                                : 0.0,
                            controllerPlayerCount_ == 1);
    rumbleWindowCycles_ = rumbleLastCycles_ = now;
    rumbleOnCycles_ = 0;
}

void MainWindow::configureControllers() {
    const bool wasPaused = paused_;
    setPaused(true);
    controllers_->setDeliveryEnabled(false);
    QList<QPair<QString, QString>> commands;
    for (auto *action : shortcutActions_)
        commands.append({action->objectName(), action->text()});
    {
        ControllerDialog dialog(*controllers_, commands, this);
        if (dialog.exec() == QDialog::Accepted) {
            const auto configuration = dialog.configuration();
            controllers_->configure(configuration, dialog.playerOverrides());
            controllers_->setPreferredInstance(dialog.preferredInstance());
            settings_.setValue("controllers/configuration", configuration.save());
        }
    }
    refreshControllerGate();
    setPaused(wasPaused);
}

void MainWindow::advanceControls() {
    if (slowHeld_)
        speedSteps_ = std::max(8u, speedSteps_ - 1);
    else
        speedSteps_ = std::min(16u, speedSteps_ + 1);
    for (size_t action = 0; action < rapidHeld_.size(); ++action)
        if (rapidHeld_[action])
            rapidPhase_[action] = (rapidPhase_[action] + 1) % 4;
    for (size_t player = 0; player < 4; ++player)
        for (size_t action = 0; action < 2; ++action)
            if (controllerRapidHeld_[player][action])
                controllerRapidPhase_[player][action] =
                    (controllerRapidPhase_[player][action] + 1) % 4;
    applyHeldButtons();
    if (audioConverter_)
        audioConverter_->setInputRate(double(cupid_gb_apu_get_sample_rate(&emulator_->gb)) *
                                      speedSteps_ / 16);
}

void MainWindow::resetPacing() {
    nextFrame_ = clock_.nsecsElapsed();
    clockPacer_.reset();
}

void MainWindow::tick() {
    refreshControllerGate();
    if (!emulator_ || paused_ || stateOperation_ || menuDepth_)
        return;
    const qint64 now = clock_.nsecsElapsed();
    if (rewind_) {
        if (now >= nextFrame_) {
            rewindFrame();
            if (turbo_)
                rewindFrame();
            nextFrame_ = now + nominalFrameNanoseconds(emulator_ ? &emulator_->gb : nullptr);
            clockPacer_.reset();
        }
        return;
    }
    pumpAudio();
    // Starting a native audio stream may dispatch window/session actions.
    if (!emulator_ || paused_ || stateOperation_)
        return;
    if (!turbo_ && now < nextFrame_)
        return;
    // Bound each event-loop turn so dialogs, input, and close remain responsive.
    if (audio_ && !turbo_ && audioBuffer_->bytesAvailable() > audioQueueLimit_)
        return;
    const uint64_t session = sessionId_;
    const uint64_t timeline = timelineId_;
    const uint64_t startCycles = emulator_->gb.elapsed_cycles;
    const unsigned speed = speedSteps_;
    if (!runFrame() || paused_ || !emulator_ || sessionId_ != session || timelineId_ != timeline)
        return;
    if (emulator_->gb.elapsed_cycles < startCycles) {
        resetPacing();
        return;
    }
    // LCD startup and re-enabling can finish a partial image. Charge only
    // consumed base-clock time, including the two-dot CGB machine cycles.
    const auto interval = clockPacer_.advance(emulator_->gb.elapsed_cycles - startCycles, speed,
                                              cupid_gb_base_clock_hz(&emulator_->gb));
    if (!interval || nextFrame_ > std::numeric_limits<qint64>::max() - *interval) {
        setPaused(true);
        reportError("Host pacing duration could not be computed.");
        return;
    }
    nextFrame_ += *interval;
    // A short LCD interval must not shrink the host-stall recovery window.
    const qint64 nominalInterval = nominalFrameNanoseconds(&emulator_->gb, speed);
    if (turbo_ || now - nextFrame_ > nominalInterval * 4) {
        nextFrame_ = now;
        clockPacer_.reset();
    }
}

bool MainWindow::runFrame() {
    if (!emulator_)
        return false;
    auto &gb = emulator_->gb;
    const uint32_t rate = cupid_gb_apu_get_sample_rate(&gb);
    if (recorder_ && recorder_->active() && recorder_->sampleRate() != rate && !stopRecording())
        return false;
    if (audioConverter_)
        audioConverter_->setInputRate(double(rate) * speedSteps_ / 16);
    gb.frame_ready = false;
    const uint64_t startCycles = gb.elapsed_cycles;
    // With LCD disabled no vblank arrives. Bound the interval with the guest clock.
    for (unsigned steps = 0; steps < 80000; ++steps) {
        if (!inputTrace_.empty()) {
            const uint64_t frame = (gb.elapsed_cycles - telemetryStartCycles_) / 70224;
            while (inputTraceIndex_ < inputTrace_.size() &&
                   inputTrace_[inputTraceIndex_].first <= frame) {
                physicalJoypad_ = inputTrace_[inputTraceIndex_++].second;
                setJoypad(physicalJoypad_);
            }
        }
        if (debugger_ && debugger_->bp_count &&
            cupid_gb_debugger_check(debugger_.get(), &gb) == CUPID_GB_DEBUG_STOP_BREAKPOINT) {
            stopDebugger("Breakpoint reached.");
            return true;
        }
        if (!cupid_emulator_step(emulator_.get())) {
            setPaused(true);
            reportError("CPU execution stopped. The cartridge remains open for inspection.");
            return false;
        }
        if (gb.sgb.player_count != controllerPlayerCount_)
            refreshControllerPlayerCount();
        if (gb.frame_ready || gb.elapsed_cycles - startCycles >= 70224)
            break;
    }
    int16_t samples[CUPID_GB_APU_BUF_FRAMES * 2];
    const auto frames = cupid_gb_apu_drain(&gb, samples, CUPID_GB_APU_BUF_FRAMES);
    generatedAudioFrames_ += frames;
    if (recorder_ && recorder_->active() && !recorder_->append(samples, frames)) {
        const QString error = recorder_->errorString();
        if (stopRecording())
            reportError("Audio recording stopped: " + error);
    }
    if (audio_ && !turbo_ && !paused_) {
        const QByteArray converted = audioConverter_->convert(samples, frames);
        audioBuffer_->append(converted.constData(), converted.size());
        pumpAudio();
    }
    present(gb.frame_ready);
    updatePrinter();
    finishControllerRumble();
    advanceControls();
    watchBattery();
    rememberFrame();
    return true;
}

QByteArray MainWindow::captureState(bool reportFailure, bool rewindSnapshot) {
    if (!emulator_)
        return {};
    const size_t size = cupid_gb_state_size(&emulator_->gb);
    if (!size || size > 16 * 1024 * 1024) {
        if (reportFailure)
            reportError("Cannot determine a valid emulator state size.");
        return {};
    }
    QByteArray bytes(qsizetype(size), Qt::Uninitialized);
    size_t written = 0;
    const auto result =
        rewindSnapshot
            ? cupid_gb_save_state_with_context(&emulator_->gb,
                                               reinterpret_cast<uint8_t *>(bytes.data()), size,
                                               &written, rewindContext_.get())
            : cupid_gb_save_state(&emulator_->gb, reinterpret_cast<uint8_t *>(bytes.data()), size,
                                  &written);
    if (result != CUPID_GB_STATE_OK || written > size) {
        if (reportFailure)
            reportError("Cannot capture emulator state: " +
                        QString::fromUtf8(cupid_gb_state_result_name(result)));
        return {};
    }
    bytes.resize(qsizetype(written));
    return bytes;
}

bool MainWindow::restoreState(const QByteArray &bytes, bool reportFailure, bool rewindSnapshot) {
    if (!emulator_)
        return false;
    if (cupid_native_v1_is_state(reinterpret_cast<const uint8_t *>(bytes.constData()),
                                 size_t(bytes.size()))) {
        if (reportFailure)
            reportError("This older state cannot resume with the current pixel pipeline. "
                        "Use File > Recover Battery from Older State to export its battery data.");
        return false;
    }
    const auto result =
        rewindSnapshot
            ? cupid_gb_load_state_with_context(&emulator_->gb,
                                               reinterpret_cast<const uint8_t *>(bytes.constData()),
                                               size_t(bytes.size()), rewindContext_.get())
            : cupid_gb_load_state(&emulator_->gb,
                                  reinterpret_cast<const uint8_t *>(bytes.constData()),
                                  size_t(bytes.size()));
    if (result != CUPID_GB_STATE_OK) {
        if (reportFailure)
            reportError("Cannot load emulator state: " +
                        QString::fromUtf8(cupid_gb_state_result_name(result)));
        return false;
    }
    ++timelineId_;
    resetPrinter();
    resetControllerRumble();
    controllerPlayerCount_ =
        cupid_gb_sgb_active(&emulator_->gb) ? emulator_->gb.sgb.player_count : 1;
    return true;
}

QString MainWindow::slotPath(int slot, bool legacy) const {
    const QFileInfo info(savePath_);
    return info.dir().filePath(info.completeBaseName() +
                               QString(legacy ? ".sn%1" : ".s%1").arg(slot));
}

bool MainWindow::saveSlot(int slot) {
    if (!emulator_ || slot < 0 || slot > 9)
        return false;
    QScopedValueRollback<bool> operation(stateOperation_, true);
    const QString path = slotPath(slot);
    const QString canonical = QFileInfo(path).canonicalFilePath();
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    if (!canonical.isEmpty() &&
        (canonical.compare(QFileInfo(romPath_).canonicalFilePath(), pathCase) == 0 ||
         canonical.compare(QFileInfo(savePath_).canonicalFilePath(), pathCase) == 0)) {
        reportError(
            "The state path points to the cartridge or battery save. Choose separate files.");
        return false;
    }
    const QByteArray bytes = captureState();
    if (bytes.isEmpty())
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        reportError("Cannot save state: " + file.errorString());
        return false;
    }
    statusBar()->showMessage(QString("State saved to slot %1").arg(slot ? slot : 10), 3000);
    resetPacing();
    return true;
}

bool MainWindow::loadSlot(int slot) {
    if (!emulator_ || slot < 0 || slot > 9)
        return false;
    QString path = slotPath(slot);
    // An existing but unreadable or invalid primary file must not select a different state.
    if (!QFileInfo::exists(path))
        path = slotPath(slot, true);
    if (!loadStateFile(path))
        return false;
    statusBar()->showMessage(QString("State loaded from slot %1").arg(slot ? slot : 10), 3000);
    return true;
}

bool MainWindow::loadStateFile(const QString &path) {
    if (!emulator_) {
        reportError("Open a cartridge before loading a state file.");
        return false;
    }
    QScopedValueRollback<bool> operation(stateOperation_, true);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 16 * 1024 * 1024) {
        reportError("Cannot read state file: " + path + "\n" + file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() != file.size()) {
        reportError("State file read failed: " + file.errorString());
        return false;
    }
    if (!restoreState(bytes, true))
        return false;
    resetDebuggerTimeline();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    if (recorder_ && recorder_->active() &&
        recorder_->sampleRate() != cupid_gb_apu_get_sample_rate(&emulator_->gb))
        stopRecording();
    // Pending samples belong to the saved timeline, before the next forward instruction.
    int16_t discarded[CUPID_GB_APU_BUF_FRAMES * 2];
    cupid_gb_apu_drain(&emulator_->gb, discarded, CUPID_GB_APU_BUF_FRAMES);
    // Host input remains current even when the saved guest frame held another button.
    applyHeldButtons();
    if (paused_ && debuggerInputPaused_)
        debuggerPlayerButtons_ = livePlayerButtons();
    batteryDirty_ = true;
    saveTimer_.start();
    rewind_ = false;
    turbo_ = false;
    clearAudio();
    resetPacing();
    resetRewind();
    rememberFrame();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    present();
    statusBar()->showMessage("State loaded", 3000);
    return true;
}

bool MainWindow::recoverBattery(const QString &statePath, const QString &outputPath) {
    if (!emulator_)
        return false;
    auto pathIdentity = [](const QString &path) {
        const QFileInfo file(path);
        return file.exists() ? file.canonicalFilePath() : file.absoluteFilePath();
    };
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    for (const QString &protectedPath : {romPath_, savePath_, statePath}) {
        if (pathIdentity(outputPath).compare(pathIdentity(protectedPath), pathCase) == 0) {
            reportError("Export recovery to a separate file. Cartridge, battery, and state inputs "
                        "are protected.");
            return false;
        }
    }
    QFile file(statePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        reportError("Cannot read the older state: " + file.errorString());
        return false;
    }
    const QByteArray state = file.readAll();
    if (state.size() != file.size())
        return false;
    auto recovered = std::make_unique<CupidNativeV1Battery>();
    auto result = cupid_native_v1_recover_battery(
        reinterpret_cast<const uint8_t *>(state.constData()), size_t(state.size()),
        reinterpret_cast<const uint8_t *>(romData_.constData()), size_t(romData_.size()),
        emulator_->gb.cartridge_ram_size, recovered.get());
    if (result != CUPID_NATIVE_V1_OK) {
        reportError("Cannot recover battery: " +
                    QString::fromLatin1(cupid_native_v1_result_name(result)));
        return false;
    }
    const auto size = cupid_native_v1_battery_size(recovered.get());
    if (!size || size != cupid_gb_battery_size(&emulator_->gb)) {
        reportError("The recovered battery layout does not match this cartridge.");
        return false;
    }
    QByteArray bytes(qsizetype(size), Qt::Uninitialized);
    size_t written = 0;
    result = cupid_native_v1_export_battery(
        recovered.get(), reinterpret_cast<uint8_t *>(bytes.data()), size, &written);
    QSaveFile output(outputPath);
    if (result != CUPID_NATIVE_V1_OK || written != size || !output.open(QIODevice::WriteOnly) ||
        output.write(bytes) != bytes.size() || !output.commit()) {
        reportError("Cannot export recovered battery: " + output.errorString());
        return false;
    }
    statusBar()->showMessage("Recovered battery exported; the running cartridge is unchanged",
                             5000);
    return true;
}

void MainWindow::resetRewind() {
    rewindStates_.clear();
    rewindContext_.reset();
    rewindBytes_ = 0;
    rewindFailed_ = false;
    rewind_ = false;
    rewindSnapshotsCaptured_ = 0;
    rewindCaptureNanoseconds_ = 0;
    rewindCaptureMaximumNanoseconds_ = 0;
}

void MainWindow::rememberFrame() {
    const int seconds = std::clamp(settings_.value("emulation/rewindSeconds", 120).toInt(), 0, 600);
    if (!emulator_ || !seconds || rewindFailed_)
        return;
    const uint32_t clockHz = cupid_gb_base_clock_hz(&emulator_->gb);
    if (!rewindContext_) {
        auto context = std::make_unique<CupidGbStateContext>();
        const auto result = cupid_gb_state_context_init(context.get(), &emulator_->gb);
        if (result != CUPID_GB_STATE_OK) {
            rewindFailed_ = true;
            statusBar()->showMessage(
                "Rewind disabled: cartridge identity could not be initialized");
            return;
        }
        // Cartridge bytes are immutable in this frontend. Reset this context whenever
        // opening/resetting a cartridge, loading a disk state, or adding ROM-editing support.
        rewindContext_ = std::move(context);
    }
    QElapsedTimer captureTimer;
    captureTimer.start();
    const QByteArray state = captureState(false, true);
    QByteArray compressed = state.isEmpty() ? QByteArray{} : qCompress(state, 1);
    if (compressed.isEmpty()) {
        rewindFailed_ = true;
        statusBar()->showMessage("Rewind disabled: cannot capture emulator state");
        return;
    }
    compressed.squeeze();
    const qint64 captureTime = captureTimer.nsecsElapsed();
    ++rewindSnapshotsCaptured_;
    rewindCaptureNanoseconds_ += captureTime;
    rewindCaptureMaximumNanoseconds_ = std::max(rewindCaptureMaximumNanoseconds_, captureTime);
    rewindBytes_ += compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
    rewindStates_.push_back({std::move(compressed), emulator_->gb.elapsed_cycles});
    const int budgetMiB = std::clamp(settings_.value("emulation/rewindMiB", 256).toInt(), 16, 256);
    const qsizetype budget = qsizetype(budgetMiB) * 1024 * 1024;
    if (rewindStates_.back().compressed.capacity() + qsizetype(sizeof(RewindSnapshot)) > budget) {
        rewindStates_.clear();
        rewindBytes_ = 0;
        rewindFailed_ = true;
        statusBar()->showMessage("Rewind disabled: one state exceeds the selected memory limit");
        return;
    }
    bool budgetLimited = false;
    while (rewindStates_.size() > 1 &&
           (rewindBytes_ > budget || rewindStates_.back().cycles - rewindStates_.front().cycles >
                                         uint64_t(seconds) * clockHz)) {
        budgetLimited |= rewindBytes_ > budget;
        rewindBytes_ -=
            rewindStates_.front().compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
        rewindStates_.pop_front();
    }
    if (budgetLimited && clock_.elapsed() - lastRewindBudgetNotice_ > 1000) {
        lastRewindBudgetNotice_ = clock_.elapsed();
        const double available =
            double(rewindStates_.back().cycles - rewindStates_.front().cycles) / clockHz;
        statusBar()->showMessage(QString("Rewind memory limit: %1 seconds retained within %2 MiB")
                                     .arg(available, 0, 'f', 1)
                                     .arg(budgetMiB),
                                 2000);
    }
}

bool MainWindow::rewindFrame() {
    if (!emulator_ || rewindFailed_ || rewindStates_.size() < 2) {
        statusBar()->showMessage(rewindFailed_ ? "Rewind unavailable: snapshot capture failed"
                                               : "No earlier rewind history");
        return false;
    }
    // The last entry describes the currently displayed frame. Restore its predecessor.
    const auto &previous = rewindStates_[rewindStates_.size() - 2];
    const QByteArray state = qUncompress(previous.compressed);
    if (state.isEmpty() || !restoreState(state, false, true)) {
        rewindFailed_ = true;
        statusBar()->showMessage("Rewind disabled: stored state could not be restored");
        return false;
    }
    resetDebuggerTimeline();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    applyHeldButtons();
    batteryDirty_ = true;
    saveTimer_.start();
    rewindBytes_ -= rewindStates_.back().compressed.capacity() + qsizetype(sizeof(RewindSnapshot));
    rewindStates_.pop_back();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    present();
    return true;
}

void MainWindow::present(bool completedFrame) {
    if (!emulator_)
        return;
    const auto &gb = emulator_->gb;
    const bool sgb = cupid_gb_sgb_active(&gb);
    if (sgb) {
        image_ = QImage(CUPID_SGB_SCREEN_WIDTH, CUPID_SGB_SCREEN_HEIGHT, QImage::Format_RGB32);
        cupid_gb_sgb_render_argb(&gb, reinterpret_cast<uint32_t *>(image_.bits()),
                                 size_t(image_.width() * image_.height()));
        if (!borderAction_->isChecked())
            image_ = image_.copy(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y, 160, 144);
    } else {
        image_ = QImage(160, 144, QImage::Format_RGB32);
        auto *pixels = reinterpret_cast<QRgb *>(image_.bits());
        // Hardware mode stays fixed while this framebuffer is converted.
        if (gb.cgb_mode || cupid_cgb_compat_active(&gb)) {
            for (size_t i = 0; i < 160 * 144; ++i)
                pixels[i] = rgb555(gb.frame_buffer_color[i]);
        } else {
            for (size_t i = 0; i < 160 * 144; ++i)
                pixels[i] = displayPalette_[gb.frame_buffer[i] & 3];
        }
    }
    // Only completed LCD frames advance blend history. A restored VBlank frame
    // may seed it once so the next guest frame has a valid predecessor.
    if (completedFrame) {
        previousDisplayFrame_ = currentDisplayFrame_;
        currentDisplayFrame_ = image_;
    } else if (currentDisplayFrame_.isNull() && gb.frame_ready) {
        currentDisplayFrame_ = image_;
    }
    // SGB sends the image to the TV without the handheld LCD's row polarity.
    const auto mode =
        sgb && frameBlending_ == FrameBlending::Accurate ? FrameBlending::Simple : frameBlending_;
    display_->image = blendDisplayFrames(image_, previousDisplayFrame_, mode, gb.lcd_frame_odd);
    display_->setMinimumSize(image_.size());
    ++presentedImages_;
    display_->update();
}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::ApplicationDeactivate ||
        (event->type() == QEvent::WindowDeactivate && object == this))
        releaseKeys();
    if (event->type() == QEvent::ApplicationDeactivate && controllers_) {
        controllers_->setFocused(false);
        resetControllerRumble();
    }
    if (event->type() == QEvent::ApplicationActivate && controllers_) {
        controllers_->setFocused(true);
        resetControllerRumble();
    }
    if ((object == display_ || object == this) && emulator_ &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->isAutoRepeat())
            return true;
        const bool pressed = event->type() == QEvent::KeyPress;
        const int value = inputKey(key->key());
        // A modifier pressed after a game button must not suppress that button's release.
        if (pressed && (key->modifiers() & ~keyModifier(value) &
                        (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
            return QMainWindow::eventFilter(object, event);
        for (size_t action = 0; action < specialKeys_.size(); ++action) {
            if (!specialKeys_[action] || specialKeys_[action] != value)
                continue;
            const bool held = pressed && !paused_;
            keyboardSpecialHeld_[action] = held;
            reconcileControllerInput();
            if (action == 1 && held && rewindStates_.size() < 2)
                statusBar()->showMessage("No earlier rewind history");
            resetPacing();
            return true;
        }
        for (size_t i = 0; i < keys_.size(); ++i) {
            if (value == keys_[i]) {
                const uint8_t mask = uint8_t(1u << i);
                if (pressed && !paused_) {
                    physicalJoypad_ &= uint8_t(~mask);
                } else {
                    physicalJoypad_ |= mask;
                }
                if (!(paused_ && debuggerInputPaused_))
                    applyHeldButtons();
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
    if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1) {
        const QString path = event->mimeData()->urls().first().toLocalFile();
        if (isStatePath(path) ? loadStateFile(path) : openRom(path))
            event->acceptProposedAction();
    }
}

void MainWindow::ensureDebugger() {
    const bool created = !debugger_;
    if (!debugger_) {
        debugger_ = std::make_unique<CupidGbDebugger>();
        cupid_gb_debugger_init(debugger_.get());
    }
    if (!debuggerWindow_) {
        debuggerWindow_ = new DebuggerWindow(this);
        debuggerWindow_->setHandlers(
            [this](const QString &command) { debuggerCommand(command); },
            [this](const QString &path) { return loadDebuggerSymbols(path); },
            [this] {
                if (debuggerTimer_.isActive())
                    stopDebugger("Debugger window closed; execution paused.");
                if (paused_) {
                    resetDebuggerTimeline();
                } else {
                    debuggerHistory_.clear();
                    debuggerHistoryBytes_ = 0;
                    debuggerContext_.reset();
                }
            });
    }
    if (created && emulator_) {
        const QFileInfo rom(romPath_);
        const QString symbols = rom.dir().filePath(rom.completeBaseName() + ".sym");
        if (QFileInfo::exists(symbols))
            loadDebuggerSymbols(symbols);
    }
}

void MainWindow::openDebugger() {
    ensureDebugger();
    pauseDebugger();
    refreshDebugger();
    debuggerWindow_->show();
    debuggerWindow_->raise();
    debuggerWindow_->activateWindow();
}

void MainWindow::openInspectors() {
    pauseDebugger();
    if (!inspectorsWindow_) {
        inspectorsWindow_ = new InspectorsWindow(this);
        inspectorsWindow_->setSession(
            [this] { return emulator_ ? &emulator_->gb : nullptr; },
            [this](uint16_t address, uint8_t value) { return debuggerWrite(address, value); });
    }
    inspectorsWindow_->refresh();
    inspectorsWindow_->show();
    inspectorsWindow_->raise();
    inspectorsWindow_->activateWindow();
}

void MainWindow::resetDebuggerTimeline(bool clearSession) {
    resetControllerRumble();
    debuggerTimer_.stop();
    debuggerHistory_.clear();
    debuggerHistoryBytes_ = 0;
    debuggerContext_.reset();
    debuggerCommandSteps_ = 0;
    if (debugger_) {
        if (clearSession) {
            cupid_gb_debugger_cleanup(debugger_.get());
            cupid_gb_debugger_init(debugger_.get());
        } else {
            cupid_gb_debugger_reset_execution(debugger_.get());
        }
    }
}

void MainWindow::pauseDebugger() {
    if (!debuggerInputPaused_)
        debuggerPlayerButtons_ = livePlayerButtons();
    debuggerInputPaused_ = true;
    debuggerTimer_.stop();
    const auto previousStop = debugger_ ? debugger_->last_stop : CUPID_GB_DEBUG_STOP_NONE;
    if (debugger_) {
        cupid_gb_debugger_reset_execution(debugger_.get());
        if (previousStop == CUPID_GB_DEBUG_STOP_BREAKPOINT)
            debugger_->last_stop = previousStop;
    }
    if (!paused_) {
        // Host focus/key releases continue to clear physical bindings, while
        // paused tools inspect the guest input at the stopped boundary.
        setPaused(true);
    }
}

void MainWindow::refreshDebugger() {
    if (inspectorsWindow_ && inspectorsWindow_->isVisible())
        inspectorsWindow_->refresh();
    if (!debuggerWindow_)
        return;
    if (!emulator_ || !debugger_) {
        debuggerWindow_->updateState({}, {}, {}, false, false, false);
        return;
    }
    const auto &gb = emulator_->gb;
    const auto &cpu = gb.cpu;
    const QString registers = QString::asprintf(
        "AF  %02X%02X\nBC  %02X%02X\nDE  %02X%02X\nHL  %02X%02X\n"
        "SP  %04X\nPC  %04X\nIME %s\nHALT %s  STOP %s\n"
        "Z %u  N %u  H %u  C %u\n\nBase T-cycles: %llu",
        cpu.a, cpu.f, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l, cpu.sp, cpu.pc,
        cpu.ime ? "on" : "off", cpu.halted ? "yes" : "no", cpu.stopped ? "yes" : "no",
        !!(cpu.f & 0x80), !!(cpu.f & 0x40), !!(cpu.f & 0x20), !!(cpu.f & 0x10),
        static_cast<unsigned long long>(gb.elapsed_cycles));
    QString listing;
    uint16_t address = cpu.pc;
    for (unsigned line = 0; line < 16; ++line) {
        CupidGbDebugInstruction instruction{};
        cupid_gb_debug_disassemble(&gb, address, &instruction);
        const int bank = cupid_gb_debugger_bank(&gb, address);
        const char *symbol =
            cupid_gb_debugger_symbol_for_address(debugger_.get(), bank, address, true);
        if (symbol)
            listing += QString::fromUtf8(symbol) + ":\n";
        QString bytes;
        for (unsigned byte = 0; byte < instruction.length; ++byte)
            bytes += QString("%1 ").arg(instruction.bytes[byte], 2, 16, QChar('0')).toUpper();
        listing += QString("$%1:$%2  %3 %4\n")
                       .arg(bank, 2, 16, QChar('0'))
                       .arg(address, 4, 16, QChar('0'))
                       .arg(bytes, -9)
                       .arg(QString::fromUtf8(instruction.text));
        address = uint16_t(address + std::max<unsigned>(1, instruction.length));
    }
    QString breakpoints = "Breakpoints (delete by number):\n";
    for (size_t index = 0; index < debugger_->bp_count; ++index) {
        const auto &point = debugger_->breakpoints[index];
        breakpoints += QString("%1  %2:$%3%4\n")
                           .arg(index + 1)
                           .arg(point.bank < 0 ? "*" : "$" + QString::number(point.bank, 16))
                           .arg(point.address, 4, 16, QChar('0'))
                           .arg(point.enabled ? "" : " [disabled]");
    }
    if (!debugger_->bp_count)
        breakpoints += "None\n";
    breakpoints += QString("\nSymbols: %1\nInstruction history: %2 / 128")
                       .arg(debugger_->symbol_count)
                       .arg(debuggerHistory_.size());
    debuggerWindow_->updateState(registers, listing, breakpoints, true,
                                 !paused_ || debuggerTimer_.isActive(), !debuggerHistory_.empty());
}

bool MainWindow::loadDebuggerSymbols(const QString &path) {
    ensureDebugger();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        debuggerWindow_->appendOutput("Cannot read symbols: " + path + "\n" + file.errorString());
        return false;
    }
    const QByteArray text = file.readAll();
    size_t errorLine = 0;
    if (text.size() != file.size() ||
        !cupid_gb_debugger_load_symbols(debugger_.get(), text.constData(), size_t(text.size()),
                                        &errorLine)) {
        debuggerWindow_->appendOutput(
            QString("Symbols rejected at line %1; existing symbols are unchanged.").arg(errorLine));
        return false;
    }
    debuggerWindow_->appendOutput(QString("Loaded %1 symbols from %2.")
                                      .arg(debugger_->symbol_count)
                                      .arg(QFileInfo(path).fileName()));
    refreshDebugger();
    return true;
}

bool MainWindow::rememberDebuggerStep() {
    if (!emulator_)
        return false;
    if (!debuggerContext_) {
        debuggerContext_ = std::make_unique<CupidGbStateContext>();
        if (cupid_gb_state_context_init(debuggerContext_.get(), &emulator_->gb) !=
            CUPID_GB_STATE_OK) {
            debuggerContext_.reset();
            return false;
        }
    }
    const size_t size = cupid_gb_state_size(&emulator_->gb);
    if (!size || size > 16 * 1024 * 1024)
        return false;
    QByteArray state(qsizetype(size), Qt::Uninitialized);
    size_t written = 0;
    if (cupid_gb_save_state_with_context(&emulator_->gb, reinterpret_cast<uint8_t *>(state.data()),
                                         size, &written,
                                         debuggerContext_.get()) != CUPID_GB_STATE_OK)
        return false;
    state.resize(qsizetype(written));
    QByteArray compressed = qCompress(state, 1);
    if (compressed.isEmpty())
        return false;
    // Compression can retain space for an incompressible input. Release that
    // spare capacity before accounting for the retained snapshot allocation.
    compressed.squeeze();
    debuggerHistoryBytes_ += compressed.capacity() + qsizetype(sizeof(QByteArray));
    debuggerHistory_.push_back(std::move(compressed));
    while (debuggerHistory_.size() > 128 || debuggerHistoryBytes_ > 32 * 1024 * 1024) {
        debuggerHistoryBytes_ -=
            debuggerHistory_.front().capacity() + qsizetype(sizeof(QByteArray));
        debuggerHistory_.pop_front();
    }
    return true;
}

void MainWindow::drainDebuggerAudio() {
    if (!emulator_)
        return;
    int16_t samples[CUPID_GB_APU_BUF_FRAMES * 2];
    const auto frames = cupid_gb_apu_drain(&emulator_->gb, samples, CUPID_GB_APU_BUF_FRAMES);
    generatedAudioFrames_ += frames;
    if (recorder_ && recorder_->active() && !recorder_->append(samples, frames)) {
        const QString error = recorder_->errorString();
        stopRecording();
        reportError("Audio recording stopped: " + error);
    }
}

void MainWindow::stopDebugger(const QString &message) {
    pauseDebugger();
    drainDebuggerAudio();
    if (emulator_) {
        present();
        watchBattery();
    }
    if (debuggerWindow_)
        debuggerWindow_->appendOutput(message);
    if (debuggerWindow_ && !debuggerWindow_->isVisible()) {
        debuggerWindow_->show();
        debuggerWindow_->raise();
    }
    statusBar()->showMessage(message, 5000);
    refreshDebugger();
}

void MainWindow::debuggerSlice() {
    if (!emulator_ || !debugger_ || sessionId_ != debuggerSessionId_) {
        resetDebuggerTimeline();
        refreshDebugger();
        return;
    }
    if (stateOperation_ || menuDepth_)
        return;
    QScopedValueRollback<bool> operation(stateOperation_, true);
    QElapsedTimer slice;
    slice.start();
    for (unsigned count = 0; count < 128 && slice.elapsed() < 4; ++count) {
        if (!rememberDebuggerStep()) {
            stopDebugger("Debugger stopped: instruction history could not be captured.");
            return;
        }
        const auto result = cupid_gb_debugger_step(debugger_.get(), &emulator_->gb);
        if (emulator_->gb.sgb.player_count != controllerPlayerCount_)
            refreshControllerPlayerCount();
        if (result == CUPID_GB_DEBUG_STOP_BREAKPOINT || result == CUPID_GB_DEBUG_STOP_NO_ROM) {
            // A breakpoint stops before execution. Do not offer a fictitious
            // backstep for an instruction that did not run.
            debuggerHistoryBytes_ -=
                debuggerHistory_.back().capacity() + qsizetype(sizeof(QByteArray));
            debuggerHistory_.pop_back();
        }
        ++debuggerCommandSteps_;
        drainDebuggerAudio();
        updatePrinter();
        if (result != CUPID_GB_DEBUG_STOP_NONE) {
            stopDebugger(QString::fromUtf8(cupid_gb_debugger_stop_name(result)));
            return;
        }
        if (debuggerCommandSteps_ >= 1000000) {
            stopDebugger("Debugger instruction budget exhausted before the target was reached.");
            return;
        }
    }
    present();
    refreshDebugger();
}

bool MainWindow::debuggerBackstep() {
    if (!emulator_ || debuggerHistory_.empty()) {
        if (debuggerWindow_)
            debuggerWindow_->appendOutput(
                "No instruction history. Only explicit debugger stepping is recorded.");
        return false;
    }
    pauseDebugger();
    const QByteArray state = qUncompress(debuggerHistory_.back());
    if (state.isEmpty() || cupid_gb_load_state_with_context(
                               &emulator_->gb, reinterpret_cast<const uint8_t *>(state.constData()),
                               size_t(state.size()), debuggerContext_.get()) != CUPID_GB_STATE_OK) {
        stopDebugger("Debugger history could not be restored.");
        return false;
    }
    ++timelineId_;
    resetPrinter();
    resetControllerRumble();
    controllerPlayerCount_ =
        cupid_gb_sgb_active(&emulator_->gb) ? emulator_->gb.sgb.player_count : 1;
    debuggerHistoryBytes_ -= debuggerHistory_.back().capacity() + qsizetype(sizeof(QByteArray));
    debuggerHistory_.pop_back();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    batteryDirty_ = true;
    saveTimer_.start();
    clearAudio();
    resetRewind();
    rememberFrame();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    present();
    refreshDebugger();
    return true;
}

bool MainWindow::debuggerWrite(uint16_t address, uint8_t value) {
    if (!emulator_)
        return false;
    pauseDebugger();
    if (!cupid_gb_debug_poke(&emulator_->gb, address, value))
        return false;
    ++timelineId_;
    resetDebuggerTimeline();
    resetRewind();
    rememberFrame();
    watchBattery();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    present();
    refreshDebugger();
    return true;
}

void MainWindow::debuggerCommand(const QString &input) {
    ensureDebugger();
    const QString command = input.trimmed();
    const QStringList words = command.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return;
    const QString verb = words.front().toLower();
    const auto output = [this](const QString &text) { debuggerWindow_->appendOutput(text); };
    if (verb == "help" || verb == "?") {
        output("break [bank:address|symbol]   delete NUMBER|all   breakpoints\n"
               "continue (c)   step (s)   next (n)   finish (f)   backstep (bs)\n"
               "registers (r)   disassemble [address]   x address [decimal count]\n"
               "write address byte   set register value   symbols path\n"
               "Numbers are decimal; use $ or 0x for hexadecimal. Symbols are supported.\n"
               "Backstep retains up to 128 explicit debugger steps within 32 MiB.\n"
               "Continuous playback does not record instruction history.");
        return;
    }
    if (verb == "symbols") {
        QString path = command.mid(words.front().size()).trimmed();
        if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
            path = path.mid(1, path.size() - 2);
        if (path.isEmpty()) {
            output("Usage: symbols path");
            return;
        }
        loadDebuggerSymbols(path);
        return;
    }
    if (!emulator_) {
        output("Open a cartridge before using debugger execution or memory commands.");
        return;
    }
    const auto number = [](QString text, unsigned maximum, unsigned *value) {
        int base = 10;
        if (text.startsWith('$')) {
            text.remove(0, 1);
            base = 16;
        } else if (text.startsWith("0x", Qt::CaseInsensitive)) {
            text.remove(0, 2);
            base = 16;
        }
        bool valid = false;
        *value = text.toUInt(&valid, base);
        return valid && *value <= maximum;
    };
    const auto address = [this, &number](const QString &text, int *bank, uint16_t *value) {
        const auto parts = text.split(':');
        unsigned raw = 0;
        *bank = -1;
        if (parts.size() == 2) {
            unsigned rawBank = 0;
            if (parts[0] != "*" && !number(parts[0], 0xffffu, &rawBank))
                return false;
            if (!number(parts[1], 0xffffu, &raw))
                return false;
            *bank = parts[0] == "*" ? -1 : int(rawBank);
            *value = uint16_t(raw);
            return true;
        }
        if (parts.size() != 1)
            return false;
        uint16_t symbolBank = 0;
        const QByteArray name = text.toUtf8();
        if (cupid_gb_debugger_lookup_symbol(debugger_.get(), name.constData(), &symbolBank,
                                            value)) {
            *bank = symbolBank;
            return true;
        }
        if (number(text, 0xffffu, &raw)) {
            *value = uint16_t(raw);
            return true;
        }
        return false;
    };
    if ((verb == "break" || verb == "b") && words.size() == 2) {
        int bank;
        uint16_t location;
        if (!address(words[1], &bank, &location) ||
            !cupid_gb_debugger_breakpoint_add(debugger_.get(), bank, location)) {
            output("Cannot add breakpoint: invalid address/bank or breakpoint limit reached.");
            return;
        }
        output(QString("Breakpoint added at %1.").arg(words[1]));
        refreshDebugger();
        return;
    }
    if (verb == "delete" && words.size() == 2) {
        if (words[1].compare("all", Qt::CaseInsensitive) == 0) {
            cupid_gb_debugger_breakpoints_clear(debugger_.get());
        } else {
            bool valid = false;
            const auto index = words[1].toULongLong(&valid, 10);
            if (!valid || !index ||
                !cupid_gb_debugger_breakpoint_remove(debugger_.get(), size_t(index - 1))) {
                output("No breakpoint with that number.");
                return;
            }
        }
        refreshDebugger();
        return;
    }
    if (verb == "breakpoints" && words.size() == 1) {
        output(QString("%1 breakpoints configured.").arg(debugger_->bp_count));
        refreshDebugger();
        return;
    }
    if ((verb == "continue" || verb == "c") && words.size() == 1) {
        setPaused(false);
        output("Continuing; instruction history cleared.");
        refreshDebugger();
        return;
    }
    if ((verb == "break" || verb == "b") && words.size() == 1) {
        stopDebugger("Break requested.");
        return;
    }
    if ((verb == "backstep" || verb == "bs") && words.size() == 1) {
        if (debuggerBackstep())
            output("Restored the preceding instruction boundary.");
        return;
    }
    CupidGbDebugMode mode = CUPID_GB_DEBUG_PAUSED;
    if (verb == "step" || verb == "s")
        mode = CUPID_GB_DEBUG_STEP;
    else if (verb == "next" || verb == "n")
        mode = CUPID_GB_DEBUG_NEXT;
    else if (verb == "finish" || verb == "f")
        mode = CUPID_GB_DEBUG_FINISH;
    if (mode != CUPID_GB_DEBUG_PAUSED && words.size() == 1) {
        pauseDebugger();
        debuggerCommandSteps_ = 0;
        debuggerSessionId_ = sessionId_;
        cupid_gb_debugger_begin(debugger_.get(), &emulator_->gb, mode);
        debuggerTimer_.start();
        refreshDebugger();
        return;
    }
    if ((verb == "registers" || verb == "r") && words.size() == 1) {
        pauseDebugger();
        refreshDebugger();
        output(debuggerWindow_->findChild<QPlainTextEdit *>("debuggerRegisters")->toPlainText());
        return;
    }
    if (verb == "set" && words.size() == 3) {
        unsigned value;
        if (!number(words[2], 0xffffu, &value)) {
            output("Register value must fit in sixteen bits.");
            return;
        }
        const QString name = words[1].toUpper();
        if (!QStringList{"AF", "BC", "DE", "HL", "SP", "PC", "A", "F", "B", "C", "D", "E", "H", "L",
                         "IME"}
                 .contains(name) ||
            (name.size() == 1 && value > 255) || (name == "IME" && value > 1)) {
            output("Unknown register or value outside its range.");
            return;
        }
        pauseDebugger();
        auto &cpu = emulator_->gb.cpu;
        if (name == "AF") {
            cpu.a = uint8_t(value >> 8);
            cpu.f = uint8_t(value & 0xf0u);
        } else if (name == "BC") {
            cpu.b = uint8_t(value >> 8);
            cpu.c = uint8_t(value);
        } else if (name == "DE") {
            cpu.d = uint8_t(value >> 8);
            cpu.e = uint8_t(value);
        } else if (name == "HL") {
            cpu.h = uint8_t(value >> 8);
            cpu.l = uint8_t(value);
        } else if (name == "PC")
            cpu.pc = uint16_t(value);
        else if (name == "SP")
            cpu.sp = uint16_t(value);
        else if (name == "IME") {
            cpu.ime = value != 0;
            cpu.ime_delay = 0;
        } else if (name == "A")
            cpu.a = uint8_t(value);
        else if (name == "F")
            cpu.f = uint8_t(value & 0xf0u);
        else if (name == "B")
            cpu.b = uint8_t(value);
        else if (name == "C")
            cpu.c = uint8_t(value);
        else if (name == "D")
            cpu.d = uint8_t(value);
        else if (name == "E")
            cpu.e = uint8_t(value);
        else if (name == "H")
            cpu.h = uint8_t(value);
        else if (name == "L")
            cpu.l = uint8_t(value);
        ++timelineId_;
        resetDebuggerTimeline();
        if (inspectorsWindow_)
            inspectorsWindow_->sessionChanged();
        resetRewind();
        rememberFrame();
        refreshDebugger();
        return;
    }
    int bank = -1;
    uint16_t location = emulator_->gb.cpu.pc;
    if (verb == "x" || verb == "memory" || verb == "write" || verb == "disassemble" ||
        verb == "dis") {
        const bool disassemble = verb == "disassemble" || verb == "dis";
        if (words.size() > (disassemble ? 2 : 3) || (!disassemble && words.size() < 2) ||
            (words.size() >= 2 && !address(words[1], &bank, &location))) {
            output("Invalid memory address or command arguments.");
            return;
        }
        if (bank >= 0 && bank != cupid_gb_debugger_bank(&emulator_->gb, location)) {
            output("That bank is not currently mapped at the requested address.");
            return;
        }
        if (verb == "write") {
            unsigned value;
            if (words.size() != 3 || !number(words[2], 255, &value) ||
                !debuggerWrite(location, uint8_t(value))) {
                output("Write rejected. ROM and unusable memory cannot be edited.");
                return;
            }
            output("Memory edited; instruction and rewind history cleared.");
            return;
        }
        unsigned count = 64;
        if (!disassemble && words.size() == 3) {
            bool valid = false;
            count = words[2].toUInt(&valid, 10);
            if (!valid || !count || count > 256) {
                output("Memory count must be between 1 and 256 decimal bytes.");
                return;
            }
        }
        pauseDebugger();
        QString result;
        if (disassemble) {
            for (unsigned line = 0; line < 16; ++line) {
                CupidGbDebugInstruction instruction{};
                cupid_gb_debug_disassemble(&emulator_->gb, location, &instruction);
                result += QString("$%1: %2\n")
                              .arg(location, 4, 16, QChar('0'))
                              .arg(QString::fromUtf8(instruction.text));
                location = uint16_t(location + std::max<unsigned>(1, instruction.length));
            }
        } else {
            for (unsigned byte = 0; byte < count; ++byte) {
                if (byte % 16 == 0)
                    result += QString("$%1: ").arg(uint16_t(location + byte), 4, 16, QChar('0'));
                result += QString("%1 ").arg(
                    cupid_gb_debug_peek(&emulator_->gb, uint16_t(location + byte)), 2, 16,
                    QChar('0'));
                if (byte % 16 == 15 || byte + 1 == count)
                    result += '\n';
            }
        }
        output(result);
        refreshDebugger();
        return;
    }
    output("Unknown command or arguments. Type help for supported commands.");
}

void MainWindow::refreshActions() {
    for (auto *action : {closeAction_, resetAction_, pauseAction_, frameAction_, screenshotAction_,
                         loadStateFileAction_, recordingAction_})
        action->setEnabled(bool(emulator_));
    recordingAction_->setText(recorder_ && recorder_->active() ? "Stop Audio &Recording"
                                                               : "Start Audio &Recording…");
    for (auto *action : saveSlotActions_)
        action->setEnabled(bool(emulator_));
    for (auto *action : loadSlotActions_)
        action->setEnabled(bool(emulator_));
    debuggerAction_->setEnabled(bool(emulator_));
    inspectorsAction_->setEnabled(bool(emulator_));
    connectionsMenu_->setEnabled(bool(emulator_));
    refreshDebugger();
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

QString MainWindow::bootDirectory() const {
    return bootDirectoryOverride_.isNull() ? settings_.value("emulation/bootDirectory").toString()
                                           : bootDirectoryOverride_;
}

void MainWindow::configure() {
    const bool wasPaused = paused_;
    setPaused(true);
    QDialog dialog(this);
    dialog.setObjectName("settings");
    dialog.setWindowTitle("CupidGB Settings");
    auto *layout = new QVBoxLayout(&dialog);
    auto *formWidget = new QWidget;
    auto *form = new QFormLayout(formWidget);
    auto *model = new QComboBox;
    model->setObjectName("hardwareModel");
    model->addItem("Automatic", -1);
    model->addItem("Game Boy", CUPID_GB_MODEL_DMG_ABC);
    model->addItem("Game Boy (DMG0)", CUPID_GB_MODEL_DMG0);
    model->addItem("Game Boy Pocket / Light", CUPID_GB_MODEL_MGB);
    model->addItem("Game Boy Color", CUPID_GB_MODEL_CGB);
    model->addItem("Super Game Boy", CUPID_GB_MODEL_SGB);
    model->addItem("Super Game Boy 2", CUPID_GB_MODEL_SGB2);
    model->setCurrentIndex(
        model->findData(modelOverride_.value_or(settings_.value("emulation/model", -1).toInt())));
    if (modelOverride_) {
        model->setEnabled(false);
        model->setToolTip("Hardware is fixed by --model for this session. "
                          "Your saved preference is unchanged.");
    }
    form->addRow(
        modelOverride_ ? "Hardware (session override)" : "Hardware (applies on reset/open)", model);
    auto *bootRow = new QWidget;
    auto *bootLayout = new QHBoxLayout(bootRow);
    bootLayout->setContentsMargins(0, 0, 0, 0);
    auto *boot = new QLineEdit(bootDirectory());
    boot->setObjectName("bootDirectory");
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
    scaling->setObjectName("displayScaling");
    scaling->addItems({"Integer Scale", "Retain Aspect Ratio", "Fill Window"});
    scaling->setCurrentIndex(int(display_->scaling));
    form->addRow("Display scaling", scaling);
    auto *windowScale = new QComboBox;
    windowScale->setObjectName("windowScale");
    for (int factor = 1; factor <= 8; ++factor)
        windowScale->addItem(QString("%1x").arg(factor), factor);
    windowScale->setCurrentIndex(
        std::clamp(settings_.value("video/defaultScale", 2).toInt(), 1, 8) - 1);
    form->addRow("Default window size", windowScale);
    auto *palette = new QComboBox;
    palette->setObjectName("monochromePalette");
    palette->addItems({"Lime (Game Boy)", "Grayscale", "Olive (Pocket)", "Teal (Light)", "Custom"});
    palette->setCurrentIndex(std::clamp(settings_.value("video/palette", 0).toInt(), 0, 4));
    form->addRow("Monochrome palette", palette);
    const auto defaultColors = monochromeColors(MonochromePalette::Lime, {});
    std::array<QLineEdit *, 4> customColors{};
    for (size_t i = 0; i < customColors.size(); ++i) {
        customColors[i] = new QLineEdit(
            settings_
                .value(QString("video/customPalette/%1").arg(i), QColor(defaultColors[i]).name())
                .toString());
        customColors[i]->setObjectName(QString("customPalette/%1").arg(i));
        customColors[i]->setMaxLength(7);
        customColors[i]->setPlaceholderText("#RRGGBB");
        customColors[i]->setEnabled(palette->currentIndex() == int(MonochromePalette::Custom));
        form->addRow(QString("Custom shade %1 (%2)")
                         .arg(i)
                         .arg(i == 0   ? "lightest"
                              : i == 3 ? "darkest"
                                       : "middle"),
                     customColors[i]);
    }
    connect(palette, &QComboBox::currentIndexChanged, &dialog, [customColors](int index) {
        for (auto *color : customColors)
            color->setEnabled(index == int(MonochromePalette::Custom));
    });
    auto *blending = new QComboBox;
    blending->setObjectName("frameBlending");
    for (const char *name : frameBlendingNames)
        blending->addItem(name);
    blending->setCurrentIndex(int(configuredFrameBlending(settings_)));
    form->addRow("Frame blending", blending);
    auto *border = new QCheckBox("Show the Super Game Boy border");
    border->setObjectName("sgbBorder");
    border->setChecked(settings_.value("video/sgbBorder", true).toBool());
    form->addRow("SGB display", border);
    auto *outputDevice = new QComboBox;
    outputDevice->setObjectName("audioOutputDevice");
    outputDevice->addItem("System Default", QByteArray());
    for (const auto &device : QMediaDevices::audioOutputs())
        outputDevice->addItem(device.description(), device.id());
    const QByteArray requestedOutput = settings_.value("audio/outputDevice").toByteArray();
    int outputIndex = outputDevice->findData(requestedOutput);
    if (outputIndex < 0) {
        outputDevice->addItem("Unavailable device (using System Default)", requestedOutput);
        outputIndex = outputDevice->count() - 1;
    }
    outputDevice->setCurrentIndex(outputIndex);
    form->addRow("Audio output", outputDevice);
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, 100);
    volume->setValue(settings_.value("audio/volume", 100).toInt());
    form->addRow("Volume", volume);
    auto *rewindLength = new QComboBox;
    for (const int seconds : {0, 10, 30, 60, 120, 300, 600})
        rewindLength->addItem(seconds ? QString("Up to %1 seconds").arg(seconds) : "Disabled",
                              seconds);
    rewindLength->setCurrentIndex(
        rewindLength->findData(settings_.value("emulation/rewindSeconds", 120).toInt()));
    form->addRow("Rewind history", rewindLength);
    auto *rewindBudget = new QComboBox;
    for (const int memory : {16, 32, 64, 128, 256})
        rewindBudget->addItem(QString("%1 MiB").arg(memory), memory);
    rewindBudget->setCurrentIndex(
        rewindBudget->findData(settings_.value("emulation/rewindMiB", 256).toInt()));
    form->addRow("Rewind memory limit", rewindBudget);
    const double available =
        rewindStates_.size() < 2
            ? 0.0
            : double(rewindStates_.back().cycles - rewindStates_.front().cycles) /
                  cupid_gb_base_clock_hz(emulator_ ? &emulator_->gb : nullptr);
    form->addRow("Current retained history",
                 new QLabel(QString("%1 seconds, %2 MiB")
                                .arg(available, 0, 'f', 1)
                                .arg(double(rewindBytes_) / (1024 * 1024), 0, 'f', 1)));
    std::array<QKeySequenceEdit *, 8> edits;
    for (size_t i = 0; i < edits.size(); ++i) {
        edits[i] = new InputKeyEdit(keys_[i]);
        edits[i]->setObjectName(QString("input/%1").arg(i));
        form->addRow(buttonNames[i], edits[i]);
    }
    std::array<QKeySequenceEdit *, 5> specialEdits;
    for (size_t i = 0; i < specialEdits.size(); ++i) {
        specialEdits[i] = new InputKeyEdit(specialKeys_[i]);
        specialEdits[i]->setObjectName(QString("input/special/%1").arg(i));
        form->addRow(specialNames[i], specialEdits[i]);
    }
    std::vector<QKeySequenceEdit *> commandEdits;
    for (auto *action : shortcutActions_) {
        auto *edit = new QKeySequenceEdit(action->shortcut());
        edit->setObjectName("shortcut/" + action->objectName());
        commandEdits.push_back(edit);
        QString label = action->text();
        label.remove('&');
        if (action->objectName().startsWith("save/"))
            label.prepend("Save ");
        if (action->objectName().startsWith("load/"))
            label.prepend("Load ");
        form->addRow(label, edit);
    }
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(formWidget);
    layout->addWidget(scroll);
    auto *note =
        new QLabel("Choose one key per button or mode. Empty mappings disable an input.\n"
                   "Command shortcuts may include modifiers. Each mapping must be unique.\n"
                   "Rewind duration is bounded by the selected memory limit.\n"
                   "Controllers and debugger tools remain incomplete.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::array<QString, 4> colorNames{};
        for (size_t i = 0; i < customColors.size(); ++i) {
            const QString text = customColors[i]->text().trimmed();
            const QColor color(text);
            if (text.size() != 7 || !text.startsWith('#') || !color.isValid()) {
                QMessageBox::warning(
                    &dialog, "Display",
                    "Enter each custom color as #RRGGBB using hexadecimal digits.");
                return;
            }
            colorNames[i] = color.name();
        }
        std::array<int, 8> values{};
        std::array<int, 5> specialValues{};
        std::vector<int> used;
        auto readInput = [&](QKeySequenceEdit *edit, int &value) {
            const auto sequence = edit->keySequence();
            if (sequence.isEmpty()) {
                value = 0;
                return true;
            }
            const auto key = sequence[0].key();
            const auto modifiers = sequence[0].keyboardModifiers();
            if (sequence.count() != 1 || key == Qt::Key_unknown ||
                (modifiers != Qt::NoModifier && modifiers != keyModifier(key)) ||
                std::find(used.begin(), used.end(), int(key)) != used.end())
                return false;
            value = int(key);
            used.push_back(value);
            return true;
        };
        for (size_t i = 0; i < edits.size(); ++i)
            if (!readInput(edits[i], values[i])) {
                QMessageBox::warning(&dialog, "Input",
                                     "Choose unique single keys for buttons and modes.");
                return;
            }
        for (size_t i = 0; i < specialEdits.size(); ++i)
            if (!readInput(specialEdits[i], specialValues[i])) {
                QMessageBox::warning(&dialog, "Input",
                                     "Choose unique single keys for buttons and modes.");
                return;
            }
        std::vector<QKeySequence> commands;
        for (auto *edit : commandEdits) {
            const auto sequence = edit->keySequence();
            if (!sequence.isEmpty() &&
                (sequence.count() != 1 ||
                 std::find(commands.begin(), commands.end(), sequence) != commands.end() ||
                 (sequence[0].keyboardModifiers() == Qt::NoModifier &&
                  std::find(used.begin(), used.end(), int(sequence[0].key())) != used.end()))) {
                QMessageBox::warning(
                    &dialog, "Input",
                    "Command shortcuts must be unique and cannot overlap button or mode keys.");
                return;
            }
            commands.push_back(sequence);
        }
        keys_ = values;
        specialKeys_ = specialValues;
        for (size_t i = 0; i < values.size(); ++i)
            settings_.setValue(QString("input/%1").arg(i), values[i]);
        for (size_t i = 0; i < specialValues.size(); ++i)
            settings_.setValue(QString("input/special/%1").arg(i), specialValues[i]);
        for (size_t i = 0; i < commands.size(); ++i) {
            shortcutActions_[i]->setShortcut(commands[i]);
            settings_.setValue("shortcuts/" + shortcutActions_[i]->objectName(),
                               commands[i].toString(QKeySequence::PortableText));
        }
        if (!modelOverride_)
            settings_.setValue("emulation/model", model->currentData());
        settings_.setValue("emulation/bootDirectory", boot->text());
        bootDirectoryOverride_ = QString();
        settings_.setValue("video/scaling", scaling->currentIndex());
        const int oldScale = settings_.value("video/defaultScale", 2).toInt();
        settings_.setValue("video/defaultScale", windowScale->currentData());
        settings_.setValue("video/palette", palette->currentIndex());
        settings_.setValue("video/blendingMode", blending->currentIndex());
        settings_.setValue("video/sgbBorder", border->isChecked());
        for (size_t i = 0; i < colorNames.size(); ++i)
            settings_.setValue(QString("video/customPalette/%1").arg(i), colorNames[i]);
        const bool outputChanged = requestedOutput != outputDevice->currentData().toByteArray();
        settings_.setValue("audio/outputDevice", outputDevice->currentData().toByteArray());
        settings_.setValue("audio/volume", volume->value());
        if (settings_.value("emulation/rewindSeconds", 120) != rewindLength->currentData() ||
            settings_.value("emulation/rewindMiB", 256) != rewindBudget->currentData()) {
            settings_.setValue("emulation/rewindSeconds", rewindLength->currentData());
            settings_.setValue("emulation/rewindMiB", rewindBudget->currentData());
            resetRewind();
            rememberFrame();
        }
        applyDisplaySettings();
        if (oldScale != windowScale->currentData().toInt())
            resizeDisplay(windowScale->currentData().toInt());
        if (outputChanged && emulator_)
            setupAudio();
        else if (audio_)
            audio_->setVolume(muteAction_->isChecked() ? 0.0f : float(volume->value()) / 100);
        dialog.accept();
    });
    dialog.resize(620, std::min(780, screen()->availableGeometry().height() - 80));
    dialog.exec();
    setPaused(wasPaused);
}

bool MainWindow::smokeTest() {
    QTemporaryDir directory;
    if (!directory.isValid())
        return smokeFailure(__LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom[0x147] = 0x09; // ROM + RAM + battery, used to check persistence.
    rom[0x149] = 0x02;
    if (automaticModel(rom) != CUPID_GB_MODEL_DMG_ABC)
        return smokeFailure(__LINE__);
    auto modelProbe = rom;
    modelProbe[0x14b] = 1;
    if (automaticModel(modelProbe) != CUPID_GB_MODEL_CGB)
        return smokeFailure(__LINE__);
    modelProbe[0x146] = 3;
    if (automaticModel(modelProbe) != CUPID_GB_MODEL_SGB)
        return smokeFailure(__LINE__);
    modelProbe[0x143] = char(0x80);
    if (automaticModel(modelProbe) != CUPID_GB_MODEL_CGB)
        return smokeFailure(__LINE__);
    modelProbe = rom;
    modelProbe[0x14b] = 0x33;
    modelProbe[0x144] = '0';
    modelProbe[0x145] = '1';
    if (automaticModel(modelProbe) != CUPID_GB_MODEL_CGB)
        return smokeFailure(__LINE__);
    const QString path = directory.filePath(QString::fromUtf8("cartridge-\xc3\xa9.gb"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
        return smokeFailure(__LINE__);
    file.close();
    auto dropFile = [this](const QString &path) {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter({1, 1}, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(this, &enter);
        QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(this, &drop);
        return drop.isAccepted();
    };
    if (!openRom(path) || !runFrame() || image_.size() != QSize(160, 144))
        return smokeFailure(__LINE__);
    if (audioCheck_ && (!audio_ || !audioPriming_ || audioBuffer_->bytesAvailable() <= 0 ||
                        audioPrimeEvents_ != 0))
        return smokeFailure(__LINE__);
    const auto beforeInvalidBoot = captureState();
    settings_.setValue("emulation/bootDirectory", directory.filePath("missing-boot-directory"));
    if (openRom(path) || captureState() != beforeInvalidBoot)
        return smokeFailure(__LINE__);
    settings_.remove("emulation/bootDirectory");
    failed_ = false;
    pauseAction_->trigger();
    const uint16_t pc = emulator_->gb.cpu.pc;
    tick();
    if (emulator_->gb.cpu.pc != pc || !pauseAction_->isChecked() ||
        (audioCheck_ && (!audioPriming_ || audioBuffer_->bytesAvailable() != 0)))
        return smokeFailure(__LINE__);
    setPaused(false);
    const auto beforeTimer = emulator_->gb.elapsed_cycles;
    QEventLoop loop;
    QTimer::singleShot(75, &loop, &QEventLoop::quit);
    loop.exec();
    if (emulator_->gb.elapsed_cycles <= beforeTimer)
        return smokeFailure(__LINE__);
    emulator_->gb.io_registers[0] = 0x10;
    QKeyEvent down(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(display_, &down);
    if ((emulator_->gb.joypad & 0x10) != 0 || !(emulator_->gb.interrupt_flags & 0x10))
        return smokeFailure(__LINE__);
    QKeyEvent upWithControl(QEvent::KeyRelease, Qt::Key_X, Qt::ControlModifier);
    QApplication::sendEvent(display_, &upWithControl);
    if ((emulator_->gb.joypad & 0x10) == 0)
        return smokeFailure(__LINE__);
    QKeyEvent turboDown(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent turboUp(QEvent::KeyRelease, Qt::Key_Space, Qt::AltModifier);
    QApplication::sendEvent(display_, &turboDown);
    QApplication::sendEvent(display_, &turboUp);
    if (turbo_)
        return smokeFailure(__LINE__);
    QKeyEvent slowDown(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    QKeyEvent slowUp(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
    QApplication::sendEvent(display_, &slowDown);
    for (unsigned frame = 0; frame < 8; ++frame)
        if (!runFrame())
            return smokeFailure(__LINE__);
    if (speedSteps_ != 8 || !slowHeld_)
        return smokeFailure(__LINE__);
    QApplication::sendEvent(display_, &slowUp);
    for (unsigned frame = 0; frame < 8; ++frame)
        if (!runFrame())
            return smokeFailure(__LINE__);
    if (speedSteps_ != 16 || slowHeld_)
        return smokeFailure(__LINE__);
    specialKeys_[3] = Qt::Key_C;
    QKeyEvent rapidDown(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier);
    QKeyEvent rapidUp(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(display_, &rapidDown);
    for (unsigned frame = 0; frame < 8; ++frame) {
        if (bool(emulator_->gb.joypad & 0x10) != (frame % 4 >= 2) || !runFrame())
            return smokeFailure(__LINE__);
    }
    QApplication::sendEvent(display_, &rapidUp);
    if (!(emulator_->gb.joypad & 0x10))
        return smokeFailure(__LINE__);
    specialKeys_[3] = 0;
    QApplication::sendEvent(display_, &down);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(this, &deactivate);
    if (emulator_->gb.joypad != 0xff)
        return smokeFailure(__LINE__);
    auto *running = emulator_.get();
    if (openRom(directory.filePath("missing.gb")) || emulator_.get() != running)
        return smokeFailure(__LINE__);
    failed_ = false; // The missing-file error above is intentional.
    QFile saveRom(directory.filePath("not-a-cartridge.sav"));
    if (!saveRom.open(QIODevice::WriteOnly) || saveRom.write(rom) != rom.size())
        return smokeFailure(__LINE__);
    saveRom.close();
    if (openRom(saveRom.fileName()) || emulator_.get() != running ||
        !saveRom.open(QIODevice::ReadOnly) || saveRom.readAll() != rom)
        return smokeFailure(__LINE__);
    failed_ = false; // Rejecting a save file must preserve the running cartridge and input bytes.
    cupid_gb_write_u8(&emulator_->gb, 0xa017, 0x39);
    watchBattery();
    QEventLoop saveLoop;
    QTimer::singleShot(320, &saveLoop, &QEventLoop::quit);
    saveLoop.exec();
    QFile automaticSave(savePath_);
    if (batteryDirty_ || !automaticSave.open(QIODevice::ReadOnly) ||
        automaticSave.readAll()[23] != char(0x39))
        return smokeFailure(__LINE__);
    automaticSave.close();
    cupid_gb_write_u8(&emulator_->gb, 0xa017, 0x40);
    watchBattery();
    QTimer::singleShot(120, this, [this] {
        cupid_gb_write_u8(&emulator_->gb, 0xa017, 0x41);
        watchBattery();
    });
    QEventLoop debounceLoop;
    QTimer::singleShot(320, &debounceLoop, &QEventLoop::quit);
    debounceLoop.exec();
    if (!batteryDirty_ || !automaticSave.open(QIODevice::ReadOnly) ||
        automaticSave.readAll()[23] != char(0x39))
        return smokeFailure(__LINE__);
    automaticSave.close();
    QEventLoop stableLoop;
    QTimer::singleShot(100, &stableLoop, &QEventLoop::quit);
    stableLoop.exec();
    if (batteryDirty_ || !automaticSave.open(QIODevice::ReadOnly) ||
        automaticSave.readAll()[23] != char(0x41))
        return smokeFailure(__LINE__);
    automaticSave.close();
    const QString shot = directory.filePath("screen.png");
    emulator_->gb.cartridge_ram[23] = 0x5a;
    ++emulator_->gb.battery_write_generation;
    if (!image_.save(shot) || QImage(shot).size() != image_.size() || !restart() ||
        emulator_->gb.cartridge_ram[23] != 0x5a)
        return smokeFailure(__LINE__);
    setPaused(true);
    const QByteArray checkpoint = captureState();
    // Version 2 retains the frozen payload prefix but has no new timing latches.
    // Loading its file must preserve that file and let the core initialize missing history.
    QByteArray version2 = checkpoint.first(300518);
    auto putState32 = [](QByteArray &bytes, qsizetype offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes[offset + i] = char(value >> (i * 8));
    };
    putState32(version2, 8, 2);
    putState32(version2, 12, uint32_t(version2.size()));
    const QByteArray version2Digest = QCryptographicHash::hash(
        version2.first(96) + version2.mid(128), QCryptographicHash::Sha256);
    std::memcpy(version2.data() + 96, version2Digest.constData(), 32);
    QFile version2File(directory.filePath("version2.s3"));
    if (checkpoint.size() <= version2.size() || !version2File.open(QIODevice::WriteOnly) ||
        version2File.write(version2) != version2.size())
        return smokeFailure(__LINE__);
    version2File.close();
    if (!loadStateFile(version2File.fileName()) || !paused_ ||
        captureState().mid(128, 300518 - 128) != version2.mid(128) ||
        !version2File.open(QIODevice::ReadOnly) || version2File.readAll() != version2 ||
        !restoreState(checkpoint, false) || captureState() != checkpoint)
        return smokeFailure(__LINE__);
    version2File.close();
    // Frozen legacy wire offsets are used only to exercise explicit battery recovery.
    QByteArray olderState(300349, char(0));
    std::memcpy(olderState.data(), "CUPIDGB", 8);
    auto putLegacy32 = [&](qsizetype offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            olderState[offset + i] = char(value >> (i * 8));
    };
    putLegacy32(8, 1);
    putLegacy32(12, uint32_t(olderState.size()));
    putLegacy32(20, uint32_t(rom.size()));
    const auto romDigest = QCryptographicHash::hash(rom, QCryptographicHash::Sha256);
    std::memcpy(olderState.data() + 32, romDigest.constData(), 32);
    olderState[16529 + 23] = char(0x72);
    const auto stateDigest = QCryptographicHash::hash(olderState.first(96) + olderState.mid(128),
                                                      QCryptographicHash::Sha256);
    std::memcpy(olderState.data() + 96, stateDigest.constData(), 32);
    QFile olderFile(directory.filePath("older.s1"));
    if (!olderFile.open(QIODevice::WriteOnly) || olderFile.write(olderState) != olderState.size())
        return smokeFailure(__LINE__);
    olderFile.close();
    const QString recoveredPath = directory.filePath(QString::fromUtf8("recovered-\xc3\xa9.sav"));
    if (restoreState(olderState, false) || captureState() != checkpoint ||
        !recoverBattery(olderFile.fileName(), recoveredPath) || captureState() != checkpoint)
        return smokeFailure(__LINE__);
    QFile recovered(recoveredPath);
    if (!recovered.open(QIODevice::ReadOnly) || recovered.size() != 8192 ||
        recovered.readAll()[23] != char(0x72))
        return smokeFailure(__LINE__);
    if (recoverBattery(olderFile.fileName(), savePath_) ||
        recoverBattery(olderFile.fileName(), romPath_) ||
        recoverBattery(olderFile.fileName(), olderFile.fileName()) || captureState() != checkpoint)
        return smokeFailure(__LINE__);
    failed_ = false;
    olderState[16529] ^= 1;
    if (!olderFile.open(QIODevice::WriteOnly) || olderFile.write(olderState) != olderState.size())
        return smokeFailure(__LINE__);
    olderFile.close();
    if (recoverBattery(olderFile.fileName(), directory.filePath("bad-export.sav")) ||
        QFileInfo::exists(directory.filePath("bad-export.sav")) || captureState() != checkpoint)
        return smokeFailure(__LINE__);
    failed_ = false;
    if (checkpoint.isEmpty() || !saveSlot(1) || !saveSlot(0) ||
        saveSlotActions_[1]->shortcut() != QKeySequence("Ctrl+1") ||
        loadSlotActions_[0]->shortcut() != QKeySequence("Ctrl+Shift+0"))
        return smokeFailure(__LINE__);
    const QString nativeStatePath = slotPath(1);
    if (openRom(nativeStatePath) || captureState() != checkpoint)
        return smokeFailure(__LINE__);
    failed_ = false; // State files must never be interpreted as new cartridges.
    if (!runFrame() || !dropFile(nativeStatePath) || !paused_ || captureState() != checkpoint)
        return smokeFailure(__LINE__);
    if (!runFrame() || !runFrame())
        return smokeFailure(__LINE__);
    const QByteArray expectedReplay = captureState();
    if (!loadSlot(1) || !paused_ || captureState() != checkpoint || !runFrame() || !runFrame() ||
        captureState() != expectedReplay)
        return smokeFailure(__LINE__);
    if (!saveSlot(2) || !QFile::rename(slotPath(2), slotPath(2, true)) || !runFrame() ||
        !loadSlot(2) || captureState() != expectedReplay)
        return smokeFailure(__LINE__);
    QFile corrupt(slotPath(2));
    if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("invalid") != 7)
        return smokeFailure(__LINE__);
    corrupt.close();
    const QByteArray beforeInvalid = captureState();
    if (loadSlot(2) || !paused_ || captureState() != beforeInvalid)
        return smokeFailure(__LINE__);
    failed_ = false; // A malformed primary must not load the valid legacy state.
    if (dropFile(slotPath(2)) || !paused_ || captureState() != beforeInvalid)
        return smokeFailure(__LINE__);
    failed_ = false; // A damaged dropped state must preserve the running cartridge.
    setPaused(false);
    QApplication::sendEvent(display_, &down);
    if (!saveSlot(3))
        return smokeFailure(__LINE__);
    QKeyEvent releasedA(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(display_, &releasedA);
    if (!loadSlot(3) || paused_ || (emulator_->gb.joypad & 0x10) == 0)
        return smokeFailure(__LINE__);
    const QByteArray runningBeforeInvalid = captureState();
    if (loadSlot(2) || paused_ || captureState() != runningBeforeInvalid)
        return smokeFailure(__LINE__);
    failed_ = false; // Rejected loads must also preserve an unpaused session.
    setPaused(true);
    const QString alternatePath = directory.filePath("different-cartridge.gb");
    QByteArray alternate = rom;
    alternate[0x150] = 1;
    QFile alternateFile(alternatePath);
    if (!alternateFile.open(QIODevice::WriteOnly) ||
        alternateFile.write(alternate) != alternate.size())
        return smokeFailure(__LINE__);
    alternateFile.close();
    QFile originalSlot(slotPath(1));
    if (!originalSlot.open(QIODevice::ReadOnly))
        return smokeFailure(__LINE__);
    const QByteArray originalBytes = originalSlot.readAll();
    originalSlot.close();
    if (!openRom(alternatePath))
        return smokeFailure(__LINE__);
    setPaused(true);
    QFile wrongSlot(slotPath(1));
    if (!wrongSlot.open(QIODevice::WriteOnly) ||
        wrongSlot.write(originalBytes) != originalBytes.size())
        return smokeFailure(__LINE__);
    wrongSlot.close();
    const QByteArray beforeWrongRom = captureState();
    if (loadSlot(1) || !paused_ || captureState() != beforeWrongRom)
        return smokeFailure(__LINE__);
    failed_ = false; // A state belonging to another cartridge must preserve this session.
    if (dropFile(nativeStatePath) || !paused_ || captureState() != beforeWrongRom)
        return smokeFailure(__LINE__);
    failed_ = false;
    if (!openRom(path))
        return smokeFailure(__LINE__);
    setPaused(true);
    resetRewind();
    rememberFrame();
    for (int i = 0; i < 6; ++i)
        if (!runFrame())
            return smokeFailure(__LINE__);
    const QByteArray forwardState = captureState();
    for (int i = 0; i < 3; ++i)
        if (!rewindFrame())
            return smokeFailure(__LINE__);
    for (int i = 0; i < 3; ++i)
        if (!runFrame())
            return smokeFailure(__LINE__);
    if (captureState() != forwardState) {
        qCritical("Rewind workflow failed to reproduce the forward state");
        return smokeFailure(__LINE__);
    }
    const auto beforeRewind = emulator_->gb.elapsed_cycles;
    setPaused(false);
    QKeyEvent rightDown(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QKeyEvent rightUp(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(display_, &rightDown);
    QKeyEvent rewindDown(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QKeyEvent rewindUp(QEvent::KeyRelease, Qt::Key_Tab, Qt::ControlModifier);
    QApplication::sendEvent(display_, &rewindDown);
    tick();
    QApplication::sendEvent(display_, &rewindUp);
    if (rewind_ || emulator_->gb.elapsed_cycles >= beforeRewind ||
        (emulator_->gb.joypad & 1) != 0) {
        qCritical("Rewind key workflow failed: held=%d cycles=%llu before=%llu joypad=%02x",
                  rewind_, qulonglong(emulator_->gb.elapsed_cycles), qulonglong(beforeRewind),
                  emulator_->gb.joypad);
        return smokeFailure(__LINE__);
    }
    // Shift+Tab arrives as Backtab on desktop platforms and must still operate rewind.
    QKeyEvent shiftedRewindDown(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QKeyEvent shiftedRewindUp(QEvent::KeyRelease, Qt::Key_Backtab, Qt::ShiftModifier);
    QApplication::sendEvent(display_, &shiftedRewindDown);
    if (!rewind_) {
        qCritical("Shift+Tab did not start rewind");
        return smokeFailure(__LINE__);
    }
    QApplication::sendEvent(display_, &shiftedRewindUp);
    if (rewind_) {
        qCritical("Shift+Tab release did not stop rewind");
        return smokeFailure(__LINE__);
    }
    QApplication::sendEvent(display_, &rightUp);
    if ((emulator_->gb.joypad & 1) == 0)
        return smokeFailure(__LINE__);
    setPaused(true);
    const QByteArray beforeBudget = captureState();
    settings_.setValue("emulation/rewindMiB", 16);
    resetRewind();
    uint32_t random = 0x619ad325;
    auto randomize = [&random](auto &bytes) {
        for (auto &byte : bytes) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            byte = uint8_t(random);
        }
    };
    for (int i = 0; i < 100; ++i) {
        randomize(emulator_->gb.cartridge_ram);
        randomize(emulator_->gb.work_ram);
        randomize(emulator_->gb.video_ram);
        rememberFrame();
    }
    if (rewindFailed_ || rewindBytes_ > 16 * 1024 * 1024 || rewindStates_.size() >= 100 ||
        rewindStates_.size() < 2) {
        qCritical("Rewind memory limit workflow failed");
        return smokeFailure(__LINE__);
    }
    settings_.setValue("emulation/rewindSeconds", 0);
    resetRewind();
    rememberFrame();
    if (!rewindStates_.empty())
        return smokeFailure(__LINE__);
    settings_.setValue("emulation/rewindMiB", 256);
    if (!restoreState(beforeBudget, true))
        return smokeFailure(__LINE__);
    settings_.setValue("emulation/rewindSeconds", 1);
    resetRewind();
    rememberFrame();
    const uint64_t durationStart = emulator_->gb.elapsed_cycles;
    for (int i = 0; i < 90; ++i)
        if (!runFrame())
            return smokeFailure(__LINE__);
    // Guest execution exceeds the duration limit while memory remains far below its limit.
    if (rewindFailed_ ||
        emulator_->gb.elapsed_cycles - durationStart <= cupid_gb_base_clock_hz(&emulator_->gb) ||
        rewindStates_.size() < 2 || rewindStates_.size() >= 91 ||
        rewindStates_.front().cycles <= durationStart ||
        rewindStates_.back().cycles - rewindStates_.front().cycles >
            cupid_gb_base_clock_hz(&emulator_->gb) ||
        rewindBytes_ >= 16 * 1024 * 1024) {
        qCritical("Rewind duration limit workflow failed");
        return smokeFailure(__LINE__);
    }
    settings_.setValue("emulation/rewindSeconds", 120);
    if (!restoreState(beforeBudget, true))
        return smokeFailure(__LINE__);
    resetRewind();
    rememberFrame();
    if (audioCheck_) {
        if (!audio_ || audioErrorEvents_) {
            qCritical("Audio workflow check requires a working audio output device");
            return smokeFailure(__LINE__);
        }
        auto waitForward = [this] {
            setPaused(false);
            const uint64_t before = generatedAudioFrames_;
            const uint64_t primedBefore = audioPrimeEvents_;
            QEventLoop loop;
            QTimer::singleShot(350, Qt::PreciseTimer, &loop, &QEventLoop::quit);
            loop.exec();
            const bool passed = audio_ && audio_->error() == QAudio::NoError &&
                                audio_->processedUSecs() > 0 && generatedAudioFrames_ > before &&
                                !audioPriming_ && audioPrimeEvents_ > primedBefore;
            if (!passed)
                qCritical("Audio resume check failed: sink=%d state=%d error=%d processed=%lld "
                          "generated=%llu priming=%d completions=%llu previous=%llu queued=%lld",
                          bool(audio_), audio_ ? int(audio_->state()) : -1,
                          audio_ ? int(audio_->error()) : -1,
                          audio_ ? qlonglong(audio_->processedUSecs()) : 0,
                          qulonglong(generatedAudioFrames_ - before), audioPriming_,
                          qulonglong(audioPrimeEvents_), qulonglong(primedBefore),
                          audioBuffer_ ? qlonglong(audioBuffer_->bytesAvailable()) : -1);
            return passed;
        };
        if (!waitForward() || !saveSlot(9) || !waitForward())
            return smokeFailure(__LINE__);
        if (!loadSlot(9) || audioBuffer_->bytesAvailable() != 0 || !audioPriming_ || !waitForward())
            return smokeFailure(__LINE__);
        const uint64_t beforeBackwards = emulator_->gb.elapsed_cycles;
        QApplication::sendEvent(display_, &rewindDown);
        QEventLoop backwardsLoop;
        QTimer::singleShot(75, Qt::PreciseTimer, &backwardsLoop, &QEventLoop::quit);
        backwardsLoop.exec();
        QApplication::sendEvent(display_, &rewindUp);
        if (rewind_ || emulator_->gb.elapsed_cycles >= beforeBackwards ||
            audioBuffer_->bytesAvailable() != 0 || !audioPriming_ || !waitForward() ||
            audioErrorEvents_)
            return smokeFailure(__LINE__);
        qInfo("CupidGB real-audio save/load and rewind transition checks passed");
    }
    const auto originalKeys = keys_;
    const auto originalSpecialKeys = specialKeys_;
    const auto originalPauseShortcut = pauseAction_->shortcut();
    auto settingsDialog = [this](const auto &edit) {
        bool checked = false;
        QTimer command;
        command.setInterval(1);
        connect(&command, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "settings")
                return;
            command.stop();
            checked = edit(*dialog);
            if (dialog->isVisible())
                dialog->reject();
        });
        command.start();
        configure();
        return checked;
    };
    setPaused(true);
    const QByteArray displayState = captureState();
    if (displayState.isEmpty())
        return smokeFailure(__LINE__);
    const uint64_t ordinaryPaints = display_->paintCount;
    display_->grab();
    display_->grab();
    if (display_->paintCount < ordinaryPaints + 2 || display_->collectPaintIntervals ||
        !display_->paintIntervals.empty() || display_->lastPaintAt)
        return smokeFailure(__LINE__);
    scalingActions_[2]->trigger();
    if (display_->scaling != DisplayScaling::Fill ||
        settings_.value("video/scaling").toInt() != int(DisplayScaling::Fill))
        return smokeFailure(__LINE__);
    scalingActions_[1]->trigger();
    if (display_->scaling != DisplayScaling::Aspect)
        return smokeFailure(__LINE__);
    scalingActions_[0]->trigger();
    windowSizeActions_[2]->trigger();
    if (settings_.value("video/defaultScale").toInt() != 3 || !windowSizeActions_[2]->isChecked())
        return smokeFailure(__LINE__);
    windowSizeActions_[1]->trigger();
    for (size_t i = 0; i < paletteActions_.size(); ++i) {
        paletteActions_[i]->trigger();
        if (!paletteActions_[i]->isChecked() || settings_.value("video/palette").toInt() != int(i))
            return smokeFailure(__LINE__);
    }
    if (captureState() != displayState)
        return smokeFailure(__LINE__);
    const auto accurateBlend = size_t(FrameBlending::Accurate);
    const auto simpleBlend = size_t(FrameBlending::Simple);
    const auto disabledBlend = size_t(FrameBlending::Disabled);
    if (!blendingActions_[accurateBlend]->isChecked())
        return smokeFailure(__LINE__);
    settings_.setValue("video/blending", false);
    applyDisplaySettings();
    if (!blendingActions_[disabledBlend]->isChecked())
        return smokeFailure(__LINE__);
    settings_.setValue("video/blending", true);
    applyDisplaySettings();
    if (!blendingActions_[simpleBlend]->isChecked())
        return smokeFailure(__LINE__);
    settings_.remove("video/blending");
    applyDisplaySettings();
    if (!settingsDialog([&](QDialog &dialog) {
            auto *scaling = dialog.findChild<QComboBox *>("displayScaling");
            auto *palette = dialog.findChild<QComboBox *>("monochromePalette");
            auto *color = dialog.findChild<QLineEdit *>("customPalette/0");
            auto *blend = dialog.findChild<QComboBox *>("frameBlending");
            if (!scaling || !palette || !color || !blend ||
                blend->currentIndex() != int(FrameBlending::Accurate))
                return smokeFailure(__LINE__);
            scaling->setCurrentIndex(int(DisplayScaling::Fill));
            palette->setCurrentIndex(int(MonochromePalette::Custom));
            color->setText("#123456");
            blend->setCurrentIndex(int(FrameBlending::Disabled));
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            return true;
        }) ||
        display_->scaling != DisplayScaling::Integer ||
        settings_.contains("video/customPalette/0") || settings_.contains("video/blendingMode") ||
        !blendingActions_[accurateBlend]->isChecked())
        return smokeFailure(__LINE__);
    if (!settingsDialog([&](QDialog &dialog) {
            auto *palette = dialog.findChild<QComboBox *>("monochromePalette");
            auto *color = dialog.findChild<QLineEdit *>("customPalette/0");
            auto *blend = dialog.findChild<QComboBox *>("frameBlending");
            if (!palette || !color || !blend)
                return smokeFailure(__LINE__);
            palette->setCurrentIndex(int(MonochromePalette::Custom));
            blend->setCurrentIndex(int(FrameBlending::Simple));
            color->setText("#123456");
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return !dialog.isVisible();
        }) ||
        settings_.value("video/customPalette/0").toString() != "#123456" ||
        settings_.value("video/blendingMode").toInt() != int(FrameBlending::Simple) ||
        !blendingActions_[simpleBlend]->isChecked() || captureState() != displayState)
        return smokeFailure(__LINE__);
    if (!settingsDialog([&](QDialog &dialog) {
            auto *color = dialog.findChild<QLineEdit *>("customPalette/0");
            if (!color)
                return smokeFailure(__LINE__);
            color->setText("#zzzzzz");
            bool warningShown = false;
            QTimer::singleShot(0, &dialog, [&] {
                if (auto *warning =
                        qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    warningShown = true;
                    warning->accept();
                }
            });
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return warningShown && dialog.isVisible();
        }) ||
        settings_.value("video/customPalette/0").toString() != "#123456")
        return smokeFailure(__LINE__);
    paletteActions_[0]->trigger();
    std::fill(std::begin(emulator_->gb.frame_buffer), std::end(emulator_->gb.frame_buffer), 0);
    present();
    if (image_.pixel(0, 0) != QColor("#c6de8c").rgb())
        return smokeFailure(__LINE__);
    paletteActions_[4]->trigger();
    if (image_.pixel(0, 0) != QColor("#123456").rgb())
        return smokeFailure(__LINE__);
    paletteActions_[1]->trigger();
    std::fill(std::begin(emulator_->gb.frame_buffer), std::end(emulator_->gb.frame_buffer), 3);
    present(true);
    std::fill(std::begin(emulator_->gb.frame_buffer), std::end(emulator_->gb.frame_buffer), 0);
    present(true);
    if (image_.pixel(0, 0) != qRgb(255, 255, 255) ||
        display_->image.pixel(0, 0) != qRgb(186, 186, 186))
        return smokeFailure(__LINE__);
    blendingActions_[accurateBlend]->trigger();
    const int darkerRow = emulator_->gb.lcd_frame_odd ? 0 : 1;
    if (!blendingActions_[accurateBlend]->isChecked() ||
        settings_.value("video/blendingMode").toInt() != int(FrameBlending::Accurate) ||
        display_->image.pixel(0, darkerRow) != qRgb(155, 155, 155) ||
        display_->image.pixel(0, darkerRow ^ 1) != qRgb(212, 212, 212))
        return smokeFailure(__LINE__);
    const QImage blendedFrame = display_->image;
    present();
    if (display_->image != blendedFrame || !saveSlot(7) || !loadSlot(7) ||
        !previousDisplayFrame_.isNull() || display_->image != image_)
        return smokeFailure(__LINE__);
    const QImage frameBeforeAdvance = currentDisplayFrame_;
    if (!runFrame() || previousDisplayFrame_ != frameBeforeAdvance ||
        display_->image != blendDisplayFrames(image_, frameBeforeAdvance, FrameBlending::Accurate,
                                              emulator_->gb.lcd_frame_odd))
        return smokeFailure(__LINE__);
    resetRewind();
    rememberFrame();
    if (!runFrame() || !runFrame() || !rewindFrame() || !previousDisplayFrame_.isNull() ||
        display_->image != image_ || !restoreState(displayState, false))
        return smokeFailure(__LINE__);
    blendingActions_[disabledBlend]->trigger();
    paletteActions_[0]->trigger();
    auto changeMappings = [](QDialog &dialog) {
        auto *button = dialog.findChild<QKeySequenceEdit *>("input/4");
        auto *mode = dialog.findChild<QKeySequenceEdit *>("input/special/0");
        auto *command = dialog.findChild<QKeySequenceEdit *>("shortcut/pause");
        if (!button || !mode || !command)
            return smokeFailure(__LINE__);
        button->setKeySequence(QKeySequence(Qt::Key_J));
        mode->setKeySequence(QKeySequence(Qt::Key_Control));
        command->setKeySequence(QKeySequence("Ctrl+F8"));
        return true;
    };
    setPaused(false);
    QApplication::sendEvent(display_, &down);
    if (!settingsDialog([&](QDialog &dialog) {
            if (!changeMappings(dialog) || !paused_ || emulator_->gb.joypad != 0xff)
                return smokeFailure(__LINE__);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            return true;
        }) ||
        paused_ || keys_ != originalKeys || specialKeys_ != originalSpecialKeys ||
        pauseAction_->shortcut() != originalPauseShortcut)
        return smokeFailure(__LINE__);
    if (!settingsDialog([&](QDialog &dialog) {
            if (!changeMappings(dialog))
                return smokeFailure(__LINE__);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return !dialog.isVisible();
        }) ||
        paused_ || keys_[4] != Qt::Key_J || specialKeys_[0] != Qt::Key_Control ||
        pauseAction_->shortcut() != QKeySequence("Ctrl+F8") ||
        settings_.value("input/4").toInt() != Qt::Key_J ||
        settings_.value("input/special/0").toInt() != Qt::Key_Control ||
        settings_.value("shortcuts/pause").toString() != "Ctrl+F8")
        return smokeFailure(__LINE__);
    QKeyEvent controlDown(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
    QKeyEvent controlUp(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
    QApplication::sendEvent(display_, &controlDown);
    if (!turbo_)
        return smokeFailure(__LINE__);
    QApplication::sendEvent(display_, &controlUp);
    if (turbo_)
        return smokeFailure(__LINE__);
    const auto appliedKeys = keys_;
    if (!settingsDialog([&](QDialog &dialog) {
            auto *conflict = dialog.findChild<QKeySequenceEdit *>("input/5");
            if (!conflict)
                return smokeFailure(__LINE__);
            conflict->setKeySequence(QKeySequence(Qt::Key_J));
            bool warningShown = false;
            QTimer::singleShot(0, &dialog, [&] {
                auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (warning) {
                    warningShown = true;
                    warning->accept();
                }
            });
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return warningShown && dialog.isVisible();
        }) ||
        paused_ || keys_ != appliedKeys)
        return smokeFailure(__LINE__);
    setPaused(true);
    const QString recordingPath = directory.filePath("forward-audio.wav");
    if (startRecording(romPath_) || startRecording(savePath_))
        return smokeFailure(__LINE__);
    failed_ = false;
    if (!startRecording(recordingPath) || !recorder_->active() ||
        recordingAction_->text() != "Stop Audio &Recording")
        return smokeFailure(__LINE__);
    const uint64_t audioBeforeRecording = generatedAudioFrames_;
    resetRewind();
    rememberFrame();
    if (!saveSlot(8) || !runFrame() || !runFrame())
        return smokeFailure(__LINE__);
    const uint64_t recordedFrames = recorder_->framesWritten();
    const QByteArray deviceState = captureState();
    const QByteArray missingOutput("cupidgb-missing-test-output");
    settings_.setValue("audio/outputDevice", missingOutput);
    setupAudio();
    if (!audioDeviceFallback_ || activeAudioDeviceId_ != QMediaDevices::defaultAudioOutput().id() ||
        !recorder_->active() || recorder_->framesWritten() != recordedFrames ||
        captureState() != deviceState || (audioCheck_ && !audio_))
        return smokeFailure(__LINE__);
    if (!settingsDialog([&](QDialog &dialog) {
            auto *output = dialog.findChild<QComboBox *>("audioOutputDevice");
            if (!output || output->currentData().toByteArray() != missingOutput ||
                !output->currentText().startsWith("Unavailable device"))
                return smokeFailure(__LINE__);
            output->setCurrentIndex(0);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            return true;
        }) ||
        settings_.value("audio/outputDevice").toByteArray() != missingOutput)
        return smokeFailure(__LINE__);
    const auto outputs = QMediaDevices::audioOutputs();
    const QByteArray selectedOutput = outputs.isEmpty() ? QByteArray() : outputs.front().id();
    if (!settingsDialog([&](QDialog &dialog) {
            auto *output = dialog.findChild<QComboBox *>("audioOutputDevice");
            if (!output)
                return smokeFailure(__LINE__);
            const int selectedIndex = output->findData(selectedOutput);
            if (selectedIndex < 0)
                return smokeFailure(__LINE__);
            output->setCurrentIndex(selectedIndex);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            return !dialog.isVisible();
        }) ||
        settings_.value("audio/outputDevice").toByteArray() != selectedOutput ||
        audioDeviceFallback_ || !recorder_->active() ||
        recorder_->framesWritten() != recordedFrames || captureState() != deviceState ||
        (!selectedOutput.isEmpty() && activeAudioDeviceId_ != selectedOutput))
        return smokeFailure(__LINE__);
    // A controlled device-list notification exercises deferred renegotiation. It
    // does not substitute for physical disconnection tests on different backends.
    QMetaObject::invokeMethod(mediaDevices_, "audioOutputsChanged", Qt::DirectConnection);
    QEventLoop deviceEvents;
    QTimer::singleShot(20, &deviceEvents, &QEventLoop::quit);
    deviceEvents.exec();
    if (!recorder_->active() || recorder_->framesWritten() != recordedFrames ||
        captureState() != deviceState)
        return smokeFailure(__LINE__);
    settings_.setValue("audio/outputDevice", QByteArray());
    setupAudio();
    if (!recordedFrames || recordedFrames != generatedAudioFrames_ - audioBeforeRecording ||
        !rewindFrame() || recorder_->framesWritten() != recordedFrames || !loadSlot(8) ||
        !recorder_->active() || recorder_->framesWritten() != recordedFrames)
        return smokeFailure(__LINE__);
    tick();
    if (recorder_->framesWritten() != recordedFrames || !runFrame() ||
        recorder_->framesWritten() <= recordedFrames)
        return smokeFailure(__LINE__);
    const uint64_t finalFrames = recorder_->framesWritten();
    recordingAction_->trigger();
    QFile recorded(recordingPath);
    if (recorder_->active() || !recorded.open(QIODevice::ReadOnly) ||
        recorded.size() != qint64(44 + finalFrames * 4))
        return smokeFailure(__LINE__);
    recorded.close();
    const QString rateChangePath = directory.filePath("rate-change.wav");
    if (!startRecording(rateChangePath) || !runFrame() ||
        !cupid_gb_apu_set_sample_rate(&emulator_->gb, 48000) || !runFrame() ||
        recorder_->active() || QFileInfo(rateChangePath).size() <= 44)
        return smokeFailure(__LINE__);
    const QString loadRatePath = directory.filePath("load-rate-change.wav");
    if (!startRecording(loadRatePath) || !loadSlot(8) || recorder_->active() ||
        QFileInfo(loadRatePath).size() != 44)
        return smokeFailure(__LINE__);
    const QString resetRecordingPath = directory.filePath("reset-audio.wav");
    if (!startRecording(resetRecordingPath) || !runFrame() || !restart() || recorder_->active() ||
        QFileInfo(resetRecordingPath).size() <= 44)
        return smokeFailure(__LINE__);
    const QString closingRecordingPath = directory.filePath("close-audio.wav");
    if (!startRecording(closingRecordingPath) || !runFrame())
        return smokeFailure(__LINE__);
    if (!closeRom() || closeAction_->isEnabled())
        return smokeFailure(__LINE__);
    if (recorder_->active() || recordingAction_->isEnabled() ||
        QFileInfo(closingRecordingPath).size() <= 44)
        return smokeFailure(__LINE__);
    const QString exitRecordingPath = directory.filePath("exit-audio.wav");
    {
        MainWindow closing(true);
        if (!closing.openRom(path) || !closing.startRecording(exitRecordingPath) ||
            !closing.runFrame())
            return smokeFailure(__LINE__);
    }
    if (QFileInfo(exitRecordingPath).size() <= 44)
        return smokeFailure(__LINE__);
    settings_.setValue("input/4", int(Qt::Key_A));
    settings_.setValue("input/special/0", int(Qt::Key_T));
    settings_.setValue("shortcuts/pause", "Ctrl+F9");
    settings_.setValue("video/scaling", int(DisplayScaling::Aspect));
    settings_.setValue("video/defaultScale", 3);
    settings_.setValue("video/palette", int(MonochromePalette::Teal));
    settings_.sync();
    {
        MainWindow restored(true);
        if (restored.keys_[4] != Qt::Key_A || restored.specialKeys_[0] != Qt::Key_T ||
            restored.pauseAction_->shortcut() != QKeySequence("Ctrl+F9") ||
            restored.display_->scaling != DisplayScaling::Aspect ||
            !restored.paletteActions_[3]->isChecked() ||
            !restored.windowSizeActions_[2]->isChecked())
            return smokeFailure(__LINE__);
    }
    InputKeyEdit capture(0);
    QKeyEvent tabCapture(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(&capture, &tabCapture);
    if (capture.keySequence() != QKeySequence(Qt::Key_Tab))
        return smokeFailure(__LINE__);
    QApplication::sendEvent(&capture, &slowDown);
    if (capture.keySequence() != QKeySequence(Qt::Key_Shift))
        return smokeFailure(__LINE__);
    QApplication::sendEvent(&capture, &controlDown);
    if (capture.keySequence() != QKeySequence(Qt::Key_Control))
        return smokeFailure(__LINE__);
    QApplication::sendEvent(&capture, &shiftedRewindDown);
    if (capture.keySequence() != QKeySequence(Qt::Key_Tab))
        return smokeFailure(__LINE__);
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_SGB));
    QFile superCartridge(directory.filePath("super.gb"));
    auto superData = rom;
    superData[0x146] = 3;
    if (!superCartridge.open(QIODevice::WriteOnly) ||
        superCartridge.write(superData) != superData.size())
        return smokeFailure(__LINE__);
    superCartridge.close();
    if (!openRom(superCartridge.fileName()) || image_.size() != QSize(256, 224) ||
        display_->minimumSize() != image_.size())
        return smokeFailure(__LINE__);
    setPaused(true);
    // Border tiles may overlap the central image. Hiding the surrounding frame
    // must retain those overlays, including their palette-selected colors.
    emulator_->gb.sgb.border_tiles[0] = 0xff;
    emulator_->gb.sgb.border_palettes[1] = 0x001f;
    present(true);
    if (image_.pixel(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y) != qRgb(255, 0, 0) ||
        !previousDisplayFrame_.isNull() || display_->image != image_)
        return smokeFailure(__LINE__);
    const QImage bordered = image_;
    blendingActions_[accurateBlend]->trigger();
    emulator_->gb.sgb.border_palettes[1] = 0x0000;
    present(true);
    if (display_->image != blendDisplayFrames(image_, bordered, FrameBlending::Simple, false) ||
        display_->image.pixel(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y) != qRgb(186, 0, 0))
        return smokeFailure(__LINE__);
    blendingActions_[disabledBlend]->trigger();
    emulator_->gb.sgb.border_palettes[1] = 0x001f;
    present();
    borderAction_->trigger();
    if (image_.size() != QSize(160, 144) || display_->minimumSize() != image_.size() ||
        image_ != bordered.copy(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y, 160, 144))
        return smokeFailure(__LINE__);
    borderAction_->trigger();
    if (image_.size() != bordered.size() || !closeRom())
        return smokeFailure(__LINE__);
    // Exercise model selection and boot overlay after cartridge initialization.
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    QFile boot(directory.filePath("cgb_boot.bin"));
    if (!boot.open(QIODevice::WriteOnly) || boot.write(QByteArray(0x900, char(0))) != 0x900)
        return smokeFailure(__LINE__);
    boot.close();
    const QString priorDirectory = directory.filePath("saved-boot-preference");
    settings_.setValue("emulation/bootDirectory", priorDirectory);
    {
        MainWindow session(true, false, false, directory.path());
        if (!session.openRom(path) || !session.emulator_->gb.boot_rom_enabled ||
            session.bootDirectory() != directory.path() ||
            settings_.value("emulation/bootDirectory").toString() != priorDirectory)
            return smokeFailure(__LINE__);
        for (const bool accept : {false, true}) {
            bool checked = false;
            QTimer command;
            command.setInterval(1);
            connect(&command, &QTimer::timeout, &session, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "settings")
                    return;
                command.stop();
                auto *edit = dialog ? dialog->findChild<QLineEdit *>("bootDirectory") : nullptr;
                checked = edit && edit->text() == directory.path();
                if (dialog)
                    dialog->findChild<QDialogButtonBox *>()
                        ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
                        ->click();
            });
            command.start();
            session.configure();
            if (!checked || session.bootDirectory() != directory.path() ||
                settings_.value("emulation/bootDirectory").toString() !=
                    (accept ? directory.path() : priorDirectory) ||
                session.bootDirectoryOverride_.isNull() != accept)
                return smokeFailure(__LINE__);
        }
    }
    if (!openRom(path) || emulator_->gb.model != CUPID_GB_MODEL_CGB ||
        !emulator_->gb.boot_rom_enabled || emulator_->gb.cpu.pc != 0)
        return smokeFailure(__LINE__);
    if (!closeRom())
        return smokeFailure(__LINE__);
    if (dropFile(nativeStatePath) || emulator_)
        return smokeFailure(__LINE__);
    failed_ = false; // Loading a dropped state requires an active cartridge.
    if (!debuggerSmokeTest(directory.path()))
        return smokeFailure(__LINE__);
    if (!pacingSmokeTest(path) || !modelSmokeTest(directory.path()) ||
        !printerSmokeTest(path, directory.path()) || !controllerSmokeTest(directory.path()) ||
        display_->collectPaintIntervals || !display_->paintIntervals.empty() ||
        display_->lastPaintAt)
        return smokeFailure(__LINE__);
    qInfo("CupidGB Qt workflow smoke test passed");
    return !failed_;
}

bool MainWindow::controllerSmokeTest(const QString &directory) {
    using namespace cupid::input;
    controllerSmokeActive_ = true;

    struct VirtualPad {
        SDL_JoystickID id = -1;
        SDL_Joystick *handle = nullptr;
        bool attached = false;
        uint16_t amplitude = 0;

        static int SDLCALL feedback(void *user, Uint16 low, Uint16 high) {
            static_cast<VirtualPad *>(user)->amplitude = std::max(low, high);
            return 0;
        }

        bool attach(uint16_t product) {
            SDL_VirtualJoystickDesc desc{};
            desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
            desc.naxes = 2;
            desc.nbuttons = 8;
            desc.nhats = 1;
            desc.vendor_id = 0xcafe;
            desc.product_id = product;
            desc.name = "CupidGB workflow virtual pad";
            desc.userdata = this;
            desc.Rumble = feedback;
            const int index = SDL_JoystickAttachVirtualEx(&desc);
            if (index < 0)
                return false;
            attached = true;
            id = SDL_JoystickGetDeviceInstanceID(index);
            handle = SDL_JoystickOpen(index);
            return handle != nullptr;
        }

        void detach() {
            if (!attached)
                return;
            for (int i = 0; i < SDL_NumJoysticks(); ++i)
                if (SDL_JoystickGetDeviceInstanceID(i) == id) {
                    SDL_JoystickDetachVirtual(i);
                    break;
                }
            attached = false;
        }

        ~VirtualPad() {
            detach();
            if (handle)
                SDL_JoystickClose(handle);
        }
    };

    const auto wait = [](const std::function<bool()> &condition) {
        if (condition())
            return true;
        QEventLoop loop;
        QTimer poll, deadline;
        poll.setInterval(1);
        deadline.setSingleShot(true);
        connect(&poll, &QTimer::timeout, &loop, [&] {
            if (condition())
                loop.quit();
        });
        connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        poll.start();
        deadline.start(1500);
        loop.exec();
        return condition();
    };
    const auto original = controllers_->configuration();
    const QVariant originalSettings = settings_.value("controllers/configuration");
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_SGB));
    settings_.setValue("emulation/bootDirectory", QString());
    bootDirectoryOverride_ = {};
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0xc3);
    rom[0x101] = char(0x50);
    rom[0x102] = char(0x01);
    rom[0x150] = char(0x18);
    rom[0x151] = char(0xfe);
    rom[0x146] = 3;
    rom[0x147] = char(0x1c); // MBC5 motor, with no RAM needed by this generated cartridge.
    QFile file(QDir(directory).filePath("controller-workflow.gb"));
    if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
        return smokeFailure(__LINE__);
    file.close();
    if (!openRom(file.fileName()))
        return smokeFailure(__LINE__);
    timer_.stop();
    const int baseline = SDL_NumJoysticks();
    VirtualPad first, second;
    if (!first.attach(0x81) || !second.attach(0x82))
        return smokeFailure(__LINE__);
    DeviceState a, b;
    if (!wait([&] {
            return controllers_->backend().state(first.id, &a) &&
                   controllers_->backend().state(second.id, &b);
        }))
        return smokeFailure(__LINE__);
    auto configuration = ControllerConfiguration{};
    for (const auto &device : controllers_->backend().devices())
        if (device.info.instance != first.id && device.info.instance != second.id) {
            auto profile = ControllerManager::defaults(device.info);
            profile.enabled = false;
            configuration.profiles[device.info.persistentKey] = profile;
        }
    configuration.profiles[a.info.persistentKey] = ControllerManager::defaults(a.info);
    configuration.profiles[b.info.persistentKey] = ControllerManager::defaults(b.info);
    configuration.profiles[b.info.persistentKey].player = 1;
    configuration.profiles[b.info.persistentKey].bindings["rapidB"] = {{Binding::Button, 2, 1}};
    configuration.profiles[a.info.persistentKey].bindings["rapidA"] = {{Binding::Button, 2, 1}};
    configuration.profiles[a.info.persistentKey].bindings["shortcut/mute"] = {
        {Binding::Button, 3, 1}};
    controllers_->configure(configuration);
    controllers_->setDeliveryEnabled(true);
    controllers_->setFocused(true);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    SDL_JoystickSetVirtualButton(second.handle, 1, 1);
    if (!wait(
            [&] { return emulator_->gb.joypad == 0xcf && emulator_->gb.joypad_extra[0] == 0xdf; }))
        return smokeFailure(__LINE__);
    const auto packetProgram = [](unsigned players) {
        QByteArray code;
        const auto write = [&](uint8_t value) {
            code.append(char(0x3e)); // LD A, value
            code.append(char(value));
            code.append(char(0xe0)); // LDH (JOYP), A
            code.append(char(0));
        };
        const std::array<uint8_t, 16> packet{0x89, uint8_t(players - 1)};
        write(0x30);
        write(0);
        for (unsigned bit = 0; bit < 128; ++bit) {
            write(0x30);
            write(packet[bit / 8] & (1u << (bit % 8)) ? 0x10 : 0x20);
        }
        write(0x30);
        write(0x20);
        write(0x30);
        return code;
    };
    const auto readButtons = [](unsigned index) {
        QByteArray code = QByteArray::fromHex("3e10e000f000ea00d0");
        code[7] = char(index);
        return code;
    };
    QByteArray transition = packetProgram(4) + readButtons(0);
    transition += QByteArray::fromHex("3e20e0003e30e000");
    transition += readButtons(1) + packetProgram(1) + readButtons(2);
    transition += QByteArray::fromHex("18fe");
    for (qsizetype i = 0; i < transition.size(); ++i)
        cupid_gb_write_u8(&emulator_->gb, uint16_t(0xc000 + i), uint8_t(transition[i]));
    emulator_->gb.cpu.pc = 0xc000;
    emulator_->gb.cpu.ime = false;
    emulator_->gb.interrupt_enable = 0;
    if (!runFrame() || emulator_->gb.sgb.player_count != 1 || emulator_->gb.joypad != 0xcf ||
        cupid_gb_read_u8(&emulator_->gb, 0xd000) != 0xde ||
        cupid_gb_read_u8(&emulator_->gb, 0xd001) != 0xdd ||
        cupid_gb_read_u8(&emulator_->gb, 0xd002) != 0xdc)
        return smokeFailure(__LINE__); // Guest reads immediately after both player-count changes.
    auto preferred = configuration;
    preferred.preferredDevice = a.info.persistentKey;
    controllers_->configure(preferred);
    controllers_->setPreferredInstance(first.id);
    if (emulator_->gb.joypad != 0xef || emulator_->gb.joypad_extra[0] != 0xff)
        return smokeFailure(__LINE__);
    auto disabled = configuration;
    disabled.profiles[b.info.persistentKey].enabled = false;
    controllers_->configure(disabled);
    if (emulator_->gb.joypad != 0xef)
        return smokeFailure(__LINE__);
    controllers_->configure(configuration);
    transition = packetProgram(4) + QByteArray::fromHex("18fe");
    for (qsizetype i = 0; i < transition.size(); ++i)
        cupid_gb_write_u8(&emulator_->gb, uint16_t(0xc000 + i), uint8_t(transition[i]));
    emulator_->gb.cpu.pc = 0xc000;
    if (!runFrame() || emulator_->gb.sgb.player_count != 4 || emulator_->gb.joypad != 0xef)
        return smokeFailure(__LINE__);
    emulator_->gb.sgb.current_player = 1;
    emulator_->gb.io_registers[0] = 0x10;
    if ((cupid_gb_read_u8(&emulator_->gb, 0xff00) & 0xf) != 0xd)
        return smokeFailure(__LINE__);
    QKeyEvent press(QEvent::KeyPress, keys_[4], Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, keys_[4], Qt::NoModifier);
    QApplication::sendEvent(this, &press);
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    if (!wait([&] { return controllers_->output().buttons[0] == 0xff; }) ||
        emulator_->gb.joypad != 0xef)
        return smokeFailure(__LINE__);
    QApplication::sendEvent(this, &release);
    if (emulator_->gb.joypad != 0xff)
        return smokeFailure(__LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    SDL_JoystickSetVirtualButton(first.handle, 2, 1);
    if (!wait([&] { return controllerRapidHeld_[0][0] && emulator_->gb.joypad == 0xef; }))
        return smokeFailure(__LINE__);
    for (unsigned phase = 0; phase < 8; ++phase) {
        advanceControls();
        if (emulator_->gb.joypad != 0xef)
            return smokeFailure(__LINE__); // Rapid release cannot release a separately held A.
    }
    SDL_JoystickSetVirtualButton(first.handle, 2, 0);
    if (!wait([&] { return !controllerRapidHeld_[0][0]; }))
        return smokeFailure(__LINE__);
    SDL_JoystickSetVirtualButton(second.handle, 2, 1);
    if (!wait([&] { return controllerRapidHeld_[1][1]; }))
        return smokeFailure(__LINE__);
    for (unsigned phase = 0; phase < 8; ++phase) {
        advanceControls();
        if (emulator_->gb.joypad != 0xef || emulator_->gb.joypad_extra[0] != 0xdf)
            return smokeFailure(__LINE__);
    }
    SDL_JoystickSetVirtualButton(second.handle, 2, 0);
    if (!wait([&] { return !controllerRapidHeld_[1][1]; }))
        return smokeFailure(__LINE__);
    const bool oldMute = muteAction_->isChecked();
    SDL_JoystickSetVirtualButton(first.handle, 3, 1);
    SDL_JoystickUpdate();
    SDL_JoystickSetVirtualButton(first.handle, 3, 0);
    SDL_JoystickUpdate();
    if (!wait([&] { return muteAction_->isChecked() != oldMute; }))
        return smokeFailure(__LINE__);
    muteAction_->setChecked(oldMute);
    pauseDebugger();
    const QByteArray stopped = captureState();
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    SDL_JoystickSetVirtualButton(second.handle, 1, 0);
    if (!wait([&] { return controllers_->output().buttons == ControllerOutput{}.buttons; }) ||
        captureState() != stopped)
        return smokeFailure(__LINE__);
    setPaused(false);
    if (emulator_->gb.joypad != 0xff || emulator_->gb.joypad_extra[0] != 0xff)
        return smokeFailure(__LINE__);
    controllers_->setFocused(true);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    if (!wait([&] { return emulator_->gb.joypad == 0xef; }))
        return smokeFailure(__LINE__);
    const QByteArray held = captureState();
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    if (!wait([&] { return emulator_->gb.joypad == 0xff; }) || !restoreState(held, false))
        return smokeFailure(__LINE__);
    applyHeldButtons();
    if (emulator_->gb.joypad != 0xff)
        return smokeFailure(__LINE__);
    for (const bool initialPaused : {false, true}) {
        for (const bool accept : {false, true}) {
            setPaused(initialPaused);
            timer_.start();
            const uint64_t beforeCycles = emulator_->gb.elapsed_cycles;
            const auto before = controllers_->configuration().save();
            bool checked = false;
            bool injected = false;
            unsigned activationStage = 0;
            bool backgroundBefore = false;
            QElapsedTimer dialogClock;
            dialogClock.start();
            QTimer command;
            command.setInterval(1);
            connect(&command, &QTimer::timeout, this, [&] {
                auto *dialog = QApplication::activeModalWidget();
                if (!dialog || dialog->objectName() != "controllerSettings")
                    return;
                if (!injected) {
                    SDL_JoystickSetVirtualButton(first.handle, 6, 1);
                    injected = true;
                }
                if (dialogClock.elapsed() < 40)
                    return;
                auto *background = dialog->findChild<QCheckBox *>("controllerBackground");
                if (!activationStage) {
                    backgroundBefore = background->isChecked();
                    background->setFocus();
                    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
                    activationStage = 1;
                    return;
                }
                if (activationStage == 1) {
                    if (background->isChecked() == backgroundBefore)
                        return;
                    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
                    controllers_->backend().pollNow();
                    activationStage = 2;
                    return;
                }
                command.stop();
                checked = controllers_->output().buttons == ControllerOutput{}.buttons &&
                          emulator_->gb.elapsed_cycles == beforeCycles &&
                          background->isChecked() != backgroundBefore;
                auto *device = dialog->findChild<QComboBox *>("controllerDevice");
                device->setCurrentIndex(device->findData(first.id));
                dialog->findChild<QSpinBox *>("controllerPressThreshold")->setValue(19000);
                if (accept) {
                    dialog->findChild<QDialogButtonBox *>("controllerDialogButtons")
                        ->button(QDialogButtonBox::Ok)
                        ->setFocus();
                    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
                } else {
                    SDL_JoystickSetVirtualButton(first.handle, 1, 1);
                }
            });
            command.start();
            QTimer dialogDeadline;
            dialogDeadline.setSingleShot(true);
            connect(&dialogDeadline, &QTimer::timeout, this, [&] {
                checked = false;
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (dialog && dialog->objectName() == "controllerSettings") {
                    qCritical("Controller navigation did not dismiss its settings dialog.");
                    dialog->reject();
                }
            });
            dialogDeadline.start(2000);
            configureControllers();
            dialogDeadline.stop();
            timer_.stop();
            if (!checked || paused_ != initialPaused ||
                (initialPaused && emulator_->gb.elapsed_cycles != beforeCycles) ||
                (!accept && controllers_->configuration().save() != before) ||
                (accept &&
                 controllers_->configuration().profiles[a.info.persistentKey].pressThreshold !=
                     19000))
                return smokeFailure(__LINE__);
            SDL_JoystickSetVirtualButton(first.handle, 0, 0);
            SDL_JoystickSetVirtualButton(first.handle, 1, 0);
            SDL_JoystickSetVirtualButton(first.handle, 6, 0);
            if (!wait([&] { return controllers_->output().buttons[0] == 0xff; }))
                return smokeFailure(__LINE__);
        }
    }
    setPaused(false);
    timer_.start();
    auto *emulationMenu = menuBar()->actions().value(1)->menu();
    emulationMenu->popup(mapToGlobal(QPoint(20, 20)));
    const uint64_t menuCycles = emulator_->gb.elapsed_cycles;
    QElapsedTimer menuClock;
    menuClock.start();
    if (!wait([&] { return menuClock.elapsed() >= 40; }) || !menuDepth_ || paused_ ||
        emulator_->gb.elapsed_cycles != menuCycles)
        return smokeFailure(__LINE__);
    QMenu *nested = nullptr;
    for (auto *action : emulationMenu->actions())
        if (action->menu()) {
            nested = action->menu();
            break;
        }
    if (!nested)
        return smokeFailure(__LINE__);
    const auto selectMenuAction = [&](QAction *target) {
        SDL_JoystickSetVirtualAxis(first.handle, 1, 0);
        controllers_->backend().pollNow();
        QApplication::processEvents();
        if (emulationMenu->activeAction() == target) {
            SDL_JoystickSetVirtualAxis(first.handle, 1, -24000);
            if (!wait([&] {
                    return emulationMenu->activeAction() && emulationMenu->activeAction() != target;
                }))
                return false;
        }
        for (qsizetype movement = 0; movement <= emulationMenu->actions().size(); ++movement) {
            SDL_JoystickSetVirtualAxis(first.handle, 1, 0);
            controllers_->backend().pollNow();
            QApplication::processEvents();
            QAction *previous = emulationMenu->activeAction();
            const auto items = emulationMenu->actions();
            const int direction = items.indexOf(previous) > items.indexOf(target) ? -1 : 1;
            SDL_JoystickSetVirtualAxis(first.handle, 1, int16_t(24000 * direction));
            if (!wait([&] {
                    return emulationMenu->activeAction() &&
                           emulationMenu->activeAction() != previous;
                }))
                return false;
            if (emulationMenu->activeAction() == target)
                return true;
        }
        return false;
    };
    controllers_->setFocused(true);
    if (!selectMenuAction(nested->menuAction()))
        return smokeFailure(__LINE__);
    SDL_JoystickSetVirtualAxis(first.handle, 0, 24000);
    if (!wait([&] { return nested->isVisible() && menuDepth_ >= 2; }))
        return smokeFailure(__LINE__);
    SDL_JoystickSetVirtualAxis(first.handle, 0, 0);
    controllers_->backend().pollNow();
    SDL_JoystickSetVirtualButton(first.handle, 1, 1);
    if (!wait(
            [&] { return !nested->isVisible() && emulationMenu->isVisible() && menuDepth_ == 1; }))
        return smokeFailure(__LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 1, 0);
    controllers_->backend().pollNow();
    menuClock.restart();
    if (!wait([&] { return menuClock.elapsed() >= 30; }) || !menuDepth_ ||
        emulator_->gb.elapsed_cycles != menuCycles)
        return smokeFailure(__LINE__);
    controllers_->setFocused(true);
    if (!selectMenuAction(pauseAction_)) {
        controllers_->backend().state(first.id, &a);
        qCritical("Controller menu selection failed: action=%s popup=%s appState=%d menuActive=%d "
                  "depth=%u axis=%d press=%d",
                  emulationMenu->activeAction() ? qPrintable(emulationMenu->activeAction()->text())
                                                : "none",
                  QApplication::activePopupWidget()
                      ? QApplication::activePopupWidget()->metaObject()->className()
                      : "none",
                  int(QApplication::applicationState()), int(emulationMenu->isActiveWindow()),
                  menuDepth_, a.physical.axes.size() > 1 ? int(a.physical.axes[1]) : 0,
                  controllers_->profile(a.info).pressThreshold);
        return smokeFailure(__LINE__);
    }
    const int menuDirection = SDL_JoystickGetAxis(first.handle, 1) < 0 ? -1 : 1;
    SDL_JoystickSetVirtualAxis(first.handle, 1, int16_t(24500 * menuDirection));
    controllers_->backend().pollNow();
    QApplication::processEvents();
    if (emulationMenu->activeAction() != pauseAction_)
        return smokeFailure(__LINE__); // Axis jitter cannot repeat one navigation press.
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    if (!wait([&] { return paused_ && !menuDepth_; }) || emulator_->gb.elapsed_cycles != menuCycles)
        return smokeFailure(__LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    SDL_JoystickSetVirtualAxis(first.handle, 1, 0);
    controllers_->backend().pollNow();
    emulationMenu->popup(mapToGlobal(QPoint(20, 20)));
    controllers_->setFocused(true);
    if (!selectMenuAction(pauseAction_))
        return smokeFailure(__LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    if (!wait([&] { return !paused_ && !menuDepth_ && emulator_->gb.elapsed_cycles > menuCycles; }))
        return smokeFailure(__LINE__);
    timer_.stop();
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    controllers_->backend().pollNow();
    controllers_->setFocused(true);
    controllers_->setDeliveryEnabled(true);
    cupid_gb_write_u8(&emulator_->gb, 0x4000, 8);
    resetControllerRumble();
    if (!runFrame() || !first.amplitude)
        return smokeFailure(__LINE__);
    setPaused(true);
    if (first.amplitude)
        return smokeFailure(__LINE__);
    setPaused(false);
    controllers_->setFocused(true);
    if (!runFrame() || !first.amplitude)
        return smokeFailure(__LINE__);
    const QByteArray motor = captureState();
    cupid_gb_write_u8(&emulator_->gb, 0x4000, 0);
    if (!restoreState(motor, false) || first.amplitude || !cupid_gb_rumble_enabled(&emulator_->gb))
        return smokeFailure(__LINE__);
    if (!closeRom() || first.amplitude)
        return smokeFailure(__LINE__);
    first.detach();
    second.detach();
    if (!wait([&] {
            return !controllers_->backend().state(first.id, &a) &&
                   !controllers_->backend().state(second.id, &b);
        }) ||
        SDL_NumJoysticks() != baseline)
        return smokeFailure(__LINE__);
    controllers_->configure(original);
    controllerSmokeActive_ = false;
    if (originalSettings.isValid())
        settings_.setValue("controllers/configuration", originalSettings);
    else
        settings_.remove("controllers/configuration");
    timer_.start();
    return true;
}

bool MainWindow::debuggerSmokeTest(const QString &directory) {
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
    settings_.setValue("emulation/bootDirectory", QString());
    bootDirectoryOverride_ = {};
    QByteArray rom(32768, char(0));
    const std::array<uint8_t, 3> entry = {0xc3, 0x50, 0x01};
    const std::array<uint8_t, 11> program = {0x31, 0xfe, 0xff, 0x3e, 0x11, 0xcd,
                                             0x60, 0x01, 0x3c, 0x18, 0xfe};
    std::memcpy(rom.data() + 0x100, entry.data(), entry.size());
    std::memcpy(rom.data() + 0x150, program.data(), program.size());
    rom[0x160] = char(0x3c);
    rom[0x161] = char(0xc9);
    rom.replace(0x134, 14, QByteArray("DEBUG WORKFLOW", 14));
    uint8_t checksum = 0;
    for (int i = 0x134; i <= 0x14c; ++i)
        checksum = uint8_t(checksum - uint8_t(rom[i]) - 1);
    rom[0x14d] = char(checksum);
    const QString path = QDir(directory).filePath(QString::fromUtf8("debugger-\xc3\xa9.gb"));
    QFile cartridge(path);
    if (!cartridge.open(QIODevice::WriteOnly) || cartridge.write(rom) != rom.size())
        return smokeFailure(__LINE__);
    cartridge.close();
    if (!openRom(path))
        return smokeFailure(__LINE__);
    physicalJoypad_ = 0xef;
    cupid_gb_set_buttons(&emulator_->gb, physicalJoypad_);
    const QByteArray beforeTools = captureState();
    openDebugger();
    if (!paused_ || !debuggerWindow_ || !debugger_ || !debuggerHistory_.empty())
        return smokeFailure(__LINE__);
    QEvent deactivateTools(QEvent::WindowDeactivate);
    QEvent deactivateApp(QEvent::ApplicationDeactivate);
    QApplication::sendEvent(this, &deactivateTools);
    QApplication::sendEvent(this, &deactivateApp);
    if (emulator_->gb.joypad != 0xef || physicalJoypad_ != 0xff || captureState() != beforeTools)
        return smokeFailure(__LINE__);
    QKeyEvent lateRelease(QEvent::KeyRelease, keys_[4], Qt::NoModifier);
    QKeyEvent latePress(QEvent::KeyPress, keys_[5], Qt::NoModifier);
    QApplication::sendEvent(this, &lateRelease);
    QApplication::sendEvent(display_, &latePress);
    const int oldRapidA = specialKeys_[3];
    specialKeys_[3] = Qt::Key_V;
    QKeyEvent rapidPress(QEvent::KeyPress, Qt::Key_V, Qt::NoModifier);
    QKeyEvent rapidRelease(QEvent::KeyRelease, Qt::Key_V, Qt::NoModifier);
    QApplication::sendEvent(this, &rapidPress);
    QApplication::sendEvent(display_, &rapidRelease);
    specialKeys_[3] = oldRapidA;
    if (captureState() != beforeTools || physicalJoypad_ != 0xff)
        return smokeFailure(__LINE__);
    openInspectors();
    QApplication::sendEvent(this, &deactivateTools);
    if (captureState() != beforeTools)
        return smokeFailure(__LINE__);
    inspectorsWindow_->close();
    setPaused(false);
    physicalJoypad_ = 0xef;
    cupid_gb_set_buttons(&emulator_->gb, physicalJoypad_);
    QApplication::sendEvent(this, &deactivateTools);
    if (emulator_->gb.joypad != 0xff || physicalJoypad_ != 0xff)
        return smokeFailure(__LINE__);
    pauseDebugger();
    const auto complete = [this] {
        QElapsedTimer deadline;
        deadline.start();
        while (debuggerTimer_.isActive() && deadline.elapsed() < 5000)
            debuggerSlice();
        return !debuggerTimer_.isActive();
    };
    debuggerWindow_->submitCommand("set pc 336");
    if (emulator_->gb.cpu.pc != 0x150)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("set pc 0x0153");
    if (emulator_->gb.cpu.pc != 0x153)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("set pc $0150");
    const QByteArray beforeStep = captureState();
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.pc != 0x153 || debuggerHistory_.size() != 1 ||
        !debuggerBackstep() || captureState() != beforeStep)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("set pc $0153");
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.a != 0x11 || emulator_->gb.cpu.pc != 0x155)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("next");
    if (!complete() || emulator_->gb.cpu.pc != 0x158 || emulator_->gb.cpu.a != 0x12 ||
        !debuggerBackstep() || emulator_->gb.cpu.pc != 0x161 || emulator_->gb.cpu.sp != 0xfffc)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.pc != 0x158)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("set pc $0155");
    debuggerWindow_->submitCommand("step");
    if (!complete() || emulator_->gb.cpu.pc != 0x160)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("finish");
    if (!complete() || emulator_->gb.cpu.pc != 0x158 || !debuggerBackstep() ||
        emulator_->gb.cpu.pc != 0x161)
        return smokeFailure(__LINE__);
    if (debuggerHistory_.empty())
        return smokeFailure(__LINE__);
    frameAction_->trigger();
    const QByteArray afterFrameAdvance = captureState();
    if (!paused_ || !debuggerHistory_.empty() || debuggerTimer_.isActive() || debuggerBackstep() ||
        captureState() != afterFrameAdvance)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("set sp $fffe");
    debuggerWindow_->submitCommand("set pc $0150");
    debuggerWindow_->submitCommand("break $00:$0155");
    debuggerWindow_->submitCommand("continue");
    if (paused_ || !runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x155 ||
        !debuggerHistory_.empty())
        return smokeFailure(__LINE__);
    if (!saveSlot(8))
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("step");
    if (!complete() || !loadSlot(8))
        return smokeFailure(__LINE__);
    const uint64_t loadedCycles = emulator_->gb.elapsed_cycles;
    debuggerWindow_->submitCommand("continue");
    if (!runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x155 ||
        emulator_->gb.elapsed_cycles != loadedCycles)
        return smokeFailure(__LINE__);
    frameAction_->trigger();
    if (!paused_ || emulator_->gb.cpu.pc == 0x155 || !debuggerHistory_.empty())
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("set pc $0150");
    debuggerWindow_->submitCommand("continue");
    if (!runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x155)
        return smokeFailure(__LINE__);
    pauseAction_->trigger();
    if (paused_ || !runFrame() || emulator_->gb.cpu.pc == 0x155)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("break");
    debuggerWindow_->submitCommand("set pc $0150");
    debuggerWindow_->submitCommand("continue");
    if (!runFrame() || !paused_ || emulator_->gb.cpu.pc != 0x155)
        return smokeFailure(__LINE__);
    // Resuming a hit skips it once; a later visit must stop at the same point.
    debuggerWindow_->submitCommand("continue");
    if (paused_ || !runFrame() || emulator_->gb.cpu.pc == 0x155)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("break");
    debuggerWindow_->submitCommand("delete all");
    if (!paused_ || debugger_->bp_count)
        return smokeFailure(__LINE__);
    const QString symbolsPath = QDir(directory).filePath("debugger.sym");
    QFile symbols(symbolsPath);
    if (!symbols.open(QIODevice::WriteOnly) ||
        symbols.write("00:0155 Subcall\n00:0160 Inner\n") <= 0)
        return smokeFailure(__LINE__);
    symbols.close();
    const QByteArray beforeSymbols = captureState();
    if (!loadDebuggerSymbols(symbolsPath) || debugger_->symbol_count != 2 ||
        captureState() != beforeSymbols)
        return smokeFailure(__LINE__);
    const QString invalidPath = QDir(directory).filePath("invalid.sym");
    QFile invalid(invalidPath);
    if (!invalid.open(QIODevice::WriteOnly) || invalid.write("invalid symbol file\n") <= 0)
        return smokeFailure(__LINE__);
    invalid.close();
    if (loadDebuggerSymbols(invalidPath) || debugger_->symbol_count != 2 ||
        captureState() != beforeSymbols)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("break Inner");
    if (debugger_->bp_count != 1 || debugger_->breakpoints[0].address != 0x160)
        return smokeFailure(__LINE__);
    if (!restart() || debugger_->bp_count != 1 || debugger_->symbol_count != 2 ||
        !debuggerHistory_.empty())
        return smokeFailure(__LINE__);
    pauseDebugger();
    debuggerWindow_->submitCommand("delete 1");
    debuggerWindow_->submitCommand("set af $123f");
    if (emulator_->gb.cpu.a != 0x12 || emulator_->gb.cpu.f != 0x30)
        return smokeFailure(__LINE__);
    const QByteArray beforeInvalid = captureState();
    debuggerWindow_->submitCommand("set f $1ff");
    debuggerWindow_->submitCommand("write $0100 $00");
    if (captureState() != beforeInvalid)
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("set pc $0159");
    for (unsigned instruction = 0; instruction < 132; ++instruction) {
        debuggerWindow_->submitCommand("step");
        if (!complete())
            return smokeFailure(__LINE__);
    }
    if (debuggerHistory_.size() != 128 || debuggerHistoryBytes_ > 32 * 1024 * 1024)
        return smokeFailure(__LINE__);
    const auto historyCount = debuggerHistory_.size();
    if (openRom(QDir(directory).filePath("absent-debugger.gb")) ||
        debuggerHistory_.size() != historyCount)
        return smokeFailure(__LINE__);
    failed_ = false;
    const QByteArray beforeNoise = captureState();
    resetDebuggerTimeline();
    uint32_t randomState = 0x17382fadu;
    const auto randomByte = [&randomState] {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return uint8_t(randomState >> 24);
    };
    const auto fillNoise = [&randomByte](void *data, size_t length) {
        auto *bytes = static_cast<uint8_t *>(data);
        for (size_t i = 0; i < length; ++i)
            bytes[i] = randomByte();
    };
    auto &noisy = emulator_->gb;
    fillNoise(noisy.video_ram, sizeof(noisy.video_ram));
    fillNoise(noisy.cartridge_ram, sizeof(noisy.cartridge_ram));
    fillNoise(noisy.work_ram, sizeof(noisy.work_ram));
    fillNoise(noisy.high_ram, sizeof(noisy.high_ram));
    fillNoise(noisy.object_attribute_memory, sizeof(noisy.object_attribute_memory));
    fillNoise(noisy.apu.buf, sizeof(noisy.apu.buf));
    fillNoise(noisy.sgb.border_tiles, sizeof(noisy.sgb.border_tiles));
    fillNoise(noisy.sgb.attribute_files, sizeof(noisy.sgb.attribute_files));
    fillNoise(noisy.sgb.ram_palettes, sizeof(noisy.sgb.ram_palettes));
    fillNoise(noisy.sgb.border_map, sizeof(noisy.sgb.border_map));
    fillNoise(noisy.sgb.border_palettes, sizeof(noisy.sgb.border_palettes));
    for (size_t i = 0; i < 160 * 144; ++i) {
        noisy.frame_buffer[i] = randomByte() & 3u;
        noisy.sgb.freeze_buffer[i] = randomByte() & 3u;
        noisy.frame_buffer_color[i] =
            uint16_t((uint16_t(randomByte()) * 256u + randomByte()) & 0x7fffu);
    }
    for (auto &attribute : noisy.sgb.attribute_map)
        attribute = randomByte() & 3u;
    for (unsigned instruction = 0; instruction < 132; ++instruction) {
        debuggerWindow_->submitCommand("step");
        if (!complete())
            return smokeFailure(__LINE__);
    }
    qsizetype retainedBytes = 0;
    for (const auto &snapshot : debuggerHistory_)
        retainedBytes += snapshot.capacity() + qsizetype(sizeof(QByteArray));
    if (debuggerHistory_.empty() || debuggerHistory_.size() >= 128 ||
        retainedBytes != debuggerHistoryBytes_ || retainedBytes > 32 * 1024 * 1024 ||
        !restoreState(beforeNoise, false))
        return smokeFailure(__LINE__);
    resetDebuggerTimeline();
    const QString debuggerAudio = QDir(directory).filePath("debugger-audio.wav");
    if (!startRecording(debuggerAudio))
        return smokeFailure(__LINE__);
    for (unsigned instruction = 0; instruction < 16; ++instruction) {
        debuggerWindow_->submitCommand("step");
        if (!complete())
            return smokeFailure(__LINE__);
    }
    const uint64_t debuggerAudioFrames = recorder_->framesWritten();
    if (!debuggerAudioFrames || !debuggerBackstep() || !recorder_->active() ||
        recorder_->framesWritten() != debuggerAudioFrames || !stopRecording())
        return smokeFailure(__LINE__);
    if (!saveSlot(9))
        return smokeFailure(__LINE__);
    debuggerWindow_->submitCommand("step");
    if (!complete() || !loadSlot(9) || !debuggerHistory_.empty() || debuggerTimer_.isActive())
        return smokeFailure(__LINE__);
    openInspectors();
    if (!inspectorsWindow_ || !paused_ || !inspectorsWindow_->writeByte(0xc000, 0x5a) ||
        cupid_gb_debug_peek(&emulator_->gb, 0xc000) != 0x5a ||
        inspectorsWindow_->writeByte(0x100, 0) || !debuggerHistory_.empty())
        return smokeFailure(__LINE__);
    for (int tab = 0; tab < 5; ++tab)
        inspectorsWindow_->selectTab(tab);
    debuggerWindow_->submitCommand("finish");
    if (!debuggerTimer_.isActive())
        return smokeFailure(__LINE__);
    QKeyEvent escapeDebugger(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(debuggerWindow_, &escapeDebugger);
    if (debuggerWindow_->isVisible() || debuggerTimer_.isActive() || !debuggerHistory_.empty() ||
        !paused_)
        return smokeFailure(__LINE__);
    openDebugger();
    debuggerWindow_->submitCommand("finish");
    if (!debuggerTimer_.isActive())
        return smokeFailure(__LINE__);
    if (!openRom(path) || debuggerTimer_.isActive() || !debuggerHistory_.empty())
        return smokeFailure(__LINE__);
    const QString replacementPath = QDir(directory).filePath("replacement-debugger.gb");
    QFile replacement(replacementPath);
    if (!replacement.open(QIODevice::WriteOnly) || replacement.write(rom) != rom.size())
        return smokeFailure(__LINE__);
    replacement.close();
    QFile companion(QDir(directory).filePath("replacement-debugger.sym"));
    if (!companion.open(QIODevice::WriteOnly) || companion.write("00:0159 Replacement\n") <= 0)
        return smokeFailure(__LINE__);
    companion.close();
    debuggerWindow_->submitCommand("break Inner");
    if (!openRom(replacementPath) || debugger_->bp_count || debugger_->symbol_count != 1 ||
        QString::fromUtf8(cupid_gb_debugger_symbol_for_address(debugger_.get(), 0, 0x159, true)) !=
            "Replacement")
        return smokeFailure(__LINE__);
    rom[0x150] = char(0xd3);
    const QString illegalPath = QDir(directory).filePath("illegal-debugger.gb");
    QFile illegal(illegalPath);
    if (!illegal.open(QIODevice::WriteOnly) || illegal.write(rom) != rom.size())
        return smokeFailure(__LINE__);
    illegal.close();
    if (!openRom(illegalPath))
        return smokeFailure(__LINE__);
    openDebugger();
    debuggerWindow_->submitCommand("set pc $0150");
    const QByteArray beforeIllegal = captureState();
    debuggerWindow_->submitCommand("step");
    if (!complete() || !emulator_->gb.cpu.halted || !debuggerBackstep() ||
        captureState() != beforeIllegal)
        return smokeFailure(__LINE__);
    const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QDir captures(captureDirectory);
        if (!captures.mkpath("."))
            return smokeFailure(__LINE__);
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
        rom[0x143] = char(0x80);
        rom[0x150] = char(0x31);
        checksum = 0;
        for (int i = 0x134; i <= 0x14c; ++i)
            checksum = uint8_t(checksum - uint8_t(rom[i]) - 1);
        rom[0x14d] = char(checksum);
        const QString capturePath = QDir(directory).filePath("capture-debugger.gbc");
        QFile captureRom(capturePath);
        if (!captureRom.open(QIODevice::WriteOnly) || captureRom.write(rom) != rom.size())
            return smokeFailure(__LINE__);
        captureRom.close();
        if (!openRom(capturePath))
            return smokeFailure(__LINE__);
        openDebugger();
        debuggerWindow_->submitCommand("set pc $0150");
        if (!loadDebuggerSymbols(symbolsPath))
            return smokeFailure(__LINE__);
        debuggerWindow_->submitCommand("break Inner");
        auto &fixture = emulator_->gb;
        for (size_t bank = 0; bank < 2; ++bank) {
            for (size_t tile = 0; tile < 384; ++tile) {
                for (size_t row = 0; row < 8; ++row) {
                    fixture.video_ram[bank * 0x2000 + tile * 16 + row * 2] =
                        uint8_t(0x81u >> ((tile + row) % 4));
                    fixture.video_ram[bank * 0x2000 + tile * 16 + row * 2 + 1] =
                        uint8_t((tile & 1u) ? 0x55u : 0xaau);
                }
            }
        }
        for (size_t cell = 0; cell < 1024; ++cell) {
            fixture.video_ram[0x1800 + cell] = uint8_t(cell);
            fixture.video_ram[0x1c00 + cell] = uint8_t(255 - cell);
            fixture.video_ram[0x3800 + cell] = uint8_t((cell / 32) & 7u);
            fixture.video_ram[0x3c00 + cell] = uint8_t((cell / 32) & 7u);
        }
        for (size_t object = 0; object < 40; ++object) {
            fixture.object_attribute_memory[object * 4] = uint8_t(16 + (object / 10) * 24);
            fixture.object_attribute_memory[object * 4 + 1] = uint8_t(8 + (object % 10) * 16);
            fixture.object_attribute_memory[object * 4 + 2] = uint8_t(object);
            fixture.object_attribute_memory[object * 4 + 3] = uint8_t(object & 7u);
        }
        for (size_t color = 0; color < 32; ++color) {
            const uint16_t rgb = uint16_t((color % 4) * 10 + (((color / 4) % 4) * 10 << 5) +
                                          ((color % 4) * 8 << 10));
            fixture.cgb_bg_palette_ram[color * 2] = uint8_t(rgb);
            fixture.cgb_bg_palette_ram[color * 2 + 1] = uint8_t(rgb >> 8);
            fixture.cgb_obj_palette_ram[color * 2] = uint8_t(rgb);
            fixture.cgb_obj_palette_ram[color * 2 + 1] = uint8_t(rgb >> 8);
        }
        refreshDebugger();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!debuggerWindow_->grab().save(captures.filePath("debugger.png")))
            return smokeFailure(__LINE__);
        openInspectors();
        const std::array<const char *, 5> names = {"memory.png", "tiles.png", "maps.png",
                                                   "objects.png", "palettes.png"};
        for (int tab = 0; tab < 5; ++tab) {
            inspectorsWindow_->selectTab(tab);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (!inspectorsWindow_->grab().save(captures.filePath(names[size_t(tab)])))
                return smokeFailure(__LINE__);
        }
    }
    if (!closeRom())
        return smokeFailure(__LINE__);
    inspectorsWindow_->refresh();
    const auto noRomOutput = debuggerWindow_->outputText();
    debuggerWindow_->submitCommand("step");
    if (emulator_ || debuggerTimer_.isActive() || debuggerWindow_->outputText() == noRomOutput ||
        !debuggerWindow_->findChild<QPlainTextEdit *>("debuggerRegisters")->toPlainText().isEmpty())
        return smokeFailure(__LINE__);
    debuggerWindow_->close();
    inspectorsWindow_->close();
    return true;
}

bool MainWindow::modelSmokeTest(const QString &directory) {
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    const auto writeFixture = [&](const QString &name, uint8_t color, uint8_t licensee) {
        rom[0x143] = char(color);
        rom[0x14b] = char(licensee);
        uint8_t checksum = 0;
        for (int i = 0x134; i <= 0x14c; ++i)
            checksum = uint8_t(checksum - uint8_t(rom[i]) - 1);
        rom[0x14d] = char(checksum);
        const QString path = QDir(directory).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
            return QString();
        return path;
    };
    const QString plain = writeFixture("model-plain.gb", 0, 0);
    const QString licensed = writeFixture("model-licensed.gb", 0, 1);
    const QString colorOnly = writeFixture("model-color-only.gbc", 0xc0, 1);
    if (plain.isEmpty() || licensed.isEmpty() || colorOnly.isEmpty())
        return smokeFailure(__LINE__);
    const bool hadModel = settings_.contains("emulation/model");
    const QVariant savedModel = settings_.value("emulation/model");
    settings_.setValue("emulation/bootDirectory", QString());
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    {
        MainWindow forced(true, false, false, QString(), CUPID_GB_MODEL_DMG_ABC);
        if (!forced.openRom(licensed) || forced.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC ||
            !forced.restart() || forced.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC)
            return smokeFailure(__LINE__);
        for (const bool accept : {false, true}) {
            bool checked = false;
            QTimer command;
            command.setInterval(1);
            connect(&command, &QTimer::timeout, &forced, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "settings")
                    return;
                command.stop();
                auto *model = dialog->findChild<QComboBox *>("hardwareModel");
                checked = model && !model->isEnabled() &&
                          model->currentData().toInt() == CUPID_GB_MODEL_DMG_ABC;
                // Disabled controls must not become a persisted override even
                // if another UI path changes the selection programmatically.
                if (model)
                    model->setCurrentIndex(model->findData(int(CUPID_GB_MODEL_CGB)));
                dialog->findChild<QDialogButtonBox *>()
                    ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
                    ->click();
            });
            command.start();
            forced.configure();
            if (!checked || settings_.value("emulation/model").toInt() != CUPID_GB_MODEL_CGB ||
                !forced.restart() || forced.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC)
                return smokeFailure(__LINE__);
        }
        const QByteArray beforeRejected = forced.captureState();
        if (forced.openRom(colorOnly) || forced.captureState() != beforeRejected ||
            !forced.closeRom() || !forced.openRom(plain) ||
            forced.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC)
            return smokeFailure(__LINE__);
    }
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
    {
        MainWindow automatic(true, false, false, QString(), -1);
        if (!automatic.openRom(licensed) || automatic.emulator_->gb.model != CUPID_GB_MODEL_CGB ||
            !automatic.restart() || automatic.emulator_->gb.model != CUPID_GB_MODEL_CGB ||
            settings_.value("emulation/model").toInt() != CUPID_GB_MODEL_DMG_ABC ||
            !automatic.openRom(plain) || automatic.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC ||
            !automatic.openRom(colorOnly) || automatic.emulator_->gb.model != CUPID_GB_MODEL_CGB)
            return smokeFailure(__LINE__);
    }
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    {
        MainWindow saved(true);
        if (!saved.openRom(plain) || saved.emulator_->gb.model != CUPID_GB_MODEL_CGB)
            return smokeFailure(__LINE__);
    }
    if (hadModel)
        settings_.setValue("emulation/model", savedModel);
    else
        settings_.remove("emulation/model");
    return true;
}

bool MainWindow::pacingSmokeTest(const QString &path) {
    for (const auto model : {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2}) {
        settings_.setValue("emulation/model", int(model));
        if (!openRom(path))
            return smokeFailure(__LINE__);
        auto &gb = emulator_->gb;
        const uint32_t clockHz = cupid_gb_base_clock_hz(&gb);
        // Approach VBlank using real hardware steps so the next image covers only
        // a scanline tail. A presentation-count deadline would overcharge it.
        unsigned steps = 0;
        while ((gb.pixels.line != 143 || gb.pixels.dot < 440) && steps++ < 70000) {
            if (!cupid_gb_step(&gb))
                return smokeFailure(__LINE__);
        }
        if (steps >= 70000)
            return smokeFailure(__LINE__);
        setPaused(false);
        const uint64_t partialStart = gb.elapsed_cycles;
        const qint64 partialDeadline = nextFrame_;
        tick();
        const uint64_t partialCycles = gb.elapsed_cycles - partialStart;
        if (paused_ || !partialCycles || partialCycles >= 70224 ||
            nextFrame_ != partialDeadline + qint64(partialCycles * 1000000000 / clockHz))
            return smokeFailure(__LINE__);
        cupid_gb_write_u8(&gb, 0xff40, 0);
        resetPacing();
        const uint64_t blankStart = gb.elapsed_cycles;
        const qint64 blankDeadline = nextFrame_;
        tick();
        const uint64_t blankCycles = gb.elapsed_cycles - blankStart;
        if (paused_ || blankCycles != 70224 ||
            nextFrame_ != blankDeadline + qint64(blankCycles * 1000000000 / clockHz))
            return smokeFailure(__LINE__);
        if (model == CUPID_GB_MODEL_SGB) {
            const auto priorDuration = settings_.value("emulation/rewindSeconds", 120);
            settings_.setValue("emulation/rewindSeconds", 1);
            resetRewind();
            rememberFrame();
            for (unsigned frame = 0; frame < 64; ++frame)
                if (!runFrame())
                    return smokeFailure(__LINE__);
            if (rewindFailed_ || rewindStates_.size() < 2)
                return smokeFailure(__LINE__);
            const uint64_t retained = rewindStates_.back().cycles - rewindStates_.front().cycles;
            if (retained > clockHz || retained <= clockHz - 70224)
                return smokeFailure(__LINE__);
            settings_.setValue("emulation/rewindSeconds", priorDuration);
        }
        if (!closeRom())
            return smokeFailure(__LINE__);
    }
    return true;
}

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

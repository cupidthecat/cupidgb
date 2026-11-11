#include "cheats_window.h"
#include "main_window_internal.h"

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
    buildColorMenus(view);
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
    cheatsAction_ = tools->addAction("&Cheats…");
    cheatSearchAction_ = tools->addAction("Cheat &Search…");
    enableCheatsAction_ = tools->addAction("&Enable Cheats");
    enableCheatsAction_->setCheckable(true);
    connect(cheatsAction_, &QAction::triggered, this, [this] { openCheats(); });
    connect(cheatSearchAction_, &QAction::triggered, this, [this] { openCheats(true); });
    connect(enableCheatsAction_, &QAction::toggled, this, [this](bool enabled) {
        mutateCheats([enabled](CupidGbCheats *collection) {
            cupid_gb_cheats_set_enabled(collection, enabled);
            return true;
        });
        refreshCheats();
    });
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
    if (QFileInfo(path).suffix().compare("cht", Qt::CaseInsensitive) == 0) {
        reportError("Open a cartridge, then import cheat files through Tools > Cheats.");
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
    const bool sameCheatSession =
        emulator_ && romData_ == data && romPath_.compare(absolute, pathCase) == 0;
    CheatCollection candidateCheats(sameCheatSession ? cupid_gb_cheats_clone(cheats_.get())
                                                     : cupid_gb_cheats_create(),
                                    cupid_gb_cheats_destroy);
    const QString cheatPath = info.dir().filePath(cartridgeStorageName(info) + ".cht");
    if (!candidateCheats) {
        cupid_emulator_shutdown(candidate.get());
        reportError("Cannot prepare the cheat collection.");
        return false;
    }
    QString cheatWarning;
    const bool protectedCheat = cheatDestinationProtected(cheatPath, absolute, save);
    QFile cheatFile(cheatPath);
    if (!sameCheatSession && protectedCheat) {
        cheatWarning = "Cheats skipped because the cheat file points to a protected file.";
    } else if (!sameCheatSession && cheatFile.exists()) {
        if (!cheatFile.open(QIODevice::ReadOnly) || cheatFile.size() > 8 * 1024 * 1024) {
            cheatWarning = "Cheats skipped because the cheat file could not be read.";
        } else {
            const QByteArray bytes = cheatFile.readAll();
            if (bytes.size() != cheatFile.size() ||
                cupid_gb_cheats_decode(candidateCheats.get(),
                                       reinterpret_cast<const uint8_t *>(bytes.constData()),
                                       size_t(bytes.size()), true) != CUPID_GB_CHEAT_OK)
                cheatWarning = "Cheats skipped because the cheat file is invalid.";
        }
    }
    cupid_gb_cheats_set_enabled(candidateCheats.get(), cupid_gb_cheats_enabled(cheats_.get()));
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
    cheats_ = std::move(candidateCheats);
    cheatPath_ = cheatPath;
    if (!sameCheatSession)
        cheatSaveWarning_.clear();
    cupid_gb_set_cheats(&emulator_->gb, cheats_.get());
    refreshCheats(true);
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
    if (!cheatWarning.isEmpty()) {
        qWarning("%s", qPrintable(cheatWarning));
        statusBar()->showMessage(cheatWarning);
    }
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
    cheats_.reset();
    cheatPath_.clear();
    cheatSaveWarning_.clear();
    refreshCheats(true);
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
    refreshCheats();
    refreshDebugger();
}

QString MainWindow::bootDirectory() const {
    return bootDirectoryOverride_.isNull() ? settings_.value("emulation/bootDirectory").toString()
                                           : bootDirectoryOverride_;
}

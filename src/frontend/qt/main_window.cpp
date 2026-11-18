/**
 * @file    main_window.cpp
 * @brief   Qt main window, cartridge lifecycle, menus, and application controls.
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

#include "cheats_window.h"
#include "main_window_internal.h"

#include "host_camera.h"

#include <QScopeGuard>

MainWindow::MainWindow(bool isolated, bool automated, bool audioCheck,
                       const QString &bootDirectoryOverride, std::optional<int> modelOverride,
                       std::optional<uint64_t> powerOnSeedOverride)
    : bootDirectoryOverride_(bootDirectoryOverride), modelOverride_(modelOverride),
      powerOnSeedOverride_(powerOnSeedOverride), isolated_(isolated), automated_(automated),
      audioCheck_(audioCheck) {
    setWindowTitle("CupidGB");
    setAcceptDrops(true);
    display_ = new DisplayWidget(this);
    setCentralWidget(display_);
    for (size_t i = 0; i < keys_.size(); ++i)
        keys_[i] = settings_.value(QString("input/%1").arg(i), defaultKeys[i]).toInt();
    for (size_t i = 0; i < linkedKeys_.size(); ++i)
        linkedKeys_[i] = settings_.value(QString("input/linkPlayer2/%1").arg(i), 0).toInt();
    for (size_t i = 0; i < specialKeys_.size(); ++i)
        specialKeys_[i] =
            settings_.value(QString("input/special/%1").arg(i), defaultSpecialKeys[i]).toInt();
    if (!settings_.contains("video/scaling"))
        settings_.setValue("video/scaling", settings_.value("video/integerScale", true).toBool()
                                                ? int(DisplayScaling::Integer)
                                                : int(DisplayScaling::Aspect));
    turboCapQuarters_ = configuredTurboCapQuarters(settings_);
    buildMenus();
    // Include the status bar before sizing the first guest canvas. Creating it
    // later takes height from the display and can drop integer scaling a step.
    statusBar();
    applyDisplaySettings();
    resizeDisplay(std::clamp(settings_.value("video/defaultScale", 2).toInt(), 1, 8));
    if (!isolated_)
        restoreGeometry(settings_.value("window/geometry").toByteArray());
    qApp->installEventFilter(this);
    timer_.setTimerType(Qt::PreciseTimer);
    refreshTimerInterval();
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
        const QPointer<MainWindow> self(this);
        setupAudio();
        if (self)
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
        receiveControllerOutput(output);
    };
    controllers_->shortcutPressed = [this](const QString &id) {
        QTimer::singleShot(0, this, [this, id] {
            if (!controllers_->deliveryEnabled() || stateOperation_ || menuInputSuspended_ ||
                QApplication::activeModalWidget() || QApplication::activePopupWidget())
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
    controllers_->backend().beforePoll = [this] {
        // Controller delivery must see current modal/focus gates even between idle ticks.
        // Manual fixtures stop the main timer and supply their own delivery gates.
        if (timer_.isActive())
            refreshControllerGate();
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
                // The guest stops while a menu owns input. Keep existing holds,
                // but allow their physical releases to reach the paused machine.
                menuInputSuspended_ = true;
                releaseLinkedInput();
                clearAudio();
                resetControllerRumble();
                resetPacing();
            }
            refreshControllerGate();
        });
        connect(menu, &QMenu::aboutToHide, this, [this] {
            if (menuDepth_)
                --menuDepth_;
            if (!menuDepth_)
                QTimer::singleShot(0, this, [this] {
                    if (!menuDepth_) {
                        menuInputSuspended_ = false;
                        refreshControllerGate();
                        receiveControllerOutput(controllers_->output());
                        clearAudio();
                        resetPacing();
                    }
                });
        });
    }
    refreshActions();
    registerLinkWindow();
}

MainWindow::~MainWindow() {
    unregisterLinkWindow();
    if (controllers_) {
        controllers_->outputChanged = {};
        controllers_->shortcutPressed = {};
        controllers_->physicalEvent = {};
        controllers_->navigationPressed = {};
        controllers_->backend().beforePoll = {};
        controllers_->backend().stop();
    }
    timer_.stop();
    cancelCameraCapture();
    hostCamera_.reset();
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
        flushBattery(true);
        cupid_emulator_shutdown(emulator_.get());
    }
}

void MainWindow::buildMenus() {
    auto *file = menuBar()->addMenu("&File");
    auto *newSession = file->addAction("New &Window", QKeySequence("Ctrl+Shift+N"));
    registerShortcut(newSession, "newWindow");
    connect(newSession, &QAction::triggered, this, [this] { newWindow(); });
    auto *open = file->addAction("&Open File…", QKeySequence("Ctrl+O"));
    registerShortcut(open, "open");
    connect(open, &QAction::triggered, this, [this] {
        const QPointer<MainWindow> self(this);
        const bool wasPaused = paused_;
        setPaused(true);
        if (!self)
            return;
        const QString path = QFileDialog::getOpenFileName(
            this, "Open File", settings_.value("files/lastDirectory").toString(),
            "Game Boy files (*.gb *.gbc *.sgb *.gbs *.zip *.7z *.rar);;"
            "Cartridges (*.gb *.gbc *.sgb);;GBS music (*.gbs);;All files (*)");
        if (!self)
            return;
        if ((path.isEmpty() || !openRom(path)) && self)
            setPaused(wasPaused);
    });
    recentMenu_ = file->addMenu("Open &Recent");
    updateRecentMenu();
    reloadAction_ = file->addAction("&Reload Cartridge");
    registerShortcut(reloadAction_, "reload");
    connect(reloadAction_, &QAction::triggered, this, [this] { reloadRom(); });
    loadStateFileAction_ = file->addAction("Load State File…");
    connect(loadStateFileAction_, &QAction::triggered, this, [this] {
        const QPointer<MainWindow> self(this);
        const bool previousOperation = stateOperation_;
        stateOperation_ = true;
        const auto operation = qScopeGuard([self, previousOperation] {
            if (self)
                self->stateOperation_ = previousOperation;
        });
        const QString path = QFileDialog::getOpenFileName(
            this, "Load State File", QFileInfo(romPath_).absolutePath(),
            "State files (*.s? *.sn?);;All files (*)");
        if (!self)
            return;
        if (!path.isEmpty())
            loadStateFile(path);
        if (self)
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
    closeAction_ = file->addAction("&Close File", QKeySequence("Ctrl+W"));
    registerShortcut(closeAction_, "close");
    connect(closeAction_, &QAction::triggered, this, [this] { closeRom(); });
    file->addSeparator();
    auto *quit = file->addAction("&Quit", QKeySequence("Ctrl+Q"));
    registerShortcut(quit, "quit");
    connect(quit, &QAction::triggered, this, [] { QApplication::closeAllWindows(); });
    auto *emulation = menuBar()->addMenu("&Emulation");
    resetAction_ = emulation->addAction("&Reset", QKeySequence("Ctrl+R"));
    registerShortcut(resetAction_, "reset");
    connect(resetAction_, &QAction::triggered, this, [this] { restart(); });
    quickResetAction_ = emulation->addAction("&Quick Reset", QKeySequence("Ctrl+Alt+R"));
    registerShortcut(quickResetAction_, "quickReset");
    connect(quickResetAction_, &QAction::triggered, this, [this] { quickReset(); });
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
    auto *turboCap = emulation->addMenu("Turbo Speed Cap");
    auto *turboGroup = new QActionGroup(this);
    for (size_t i = 0; i < turboCapChoices.size(); ++i) {
        const unsigned quarters = turboCapChoices[i];
        auto *action = turboCap->addAction(quarters ? QString("%1%").arg(quarters * 25)
                                                    : QStringLiteral("Uncapped"));
        action->setObjectName(QString("turboCap/%1").arg(quarters * 25));
        action->setCheckable(true);
        action->setChecked(quarters == turboCapQuarters_);
        turboGroup->addAction(action);
        turboCapActions_[i] = action;
        connect(action, &QAction::triggered, this, [this, quarters] { setTurboCap(quarters); });
    }
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
    screenshotAction_ = view->addAction("Save &Screenshot", QKeySequence("F12"));
    registerShortcut(screenshotAction_, "screenshot");
    connect(screenshotAction_, &QAction::triggered, this, [this] { screenshot(); });
    screenshotAsAction_ = view->addAction("Save Screenshot &As…", QKeySequence("Shift+F12"));
    registerShortcut(screenshotAsAction_, "screenshotAs");
    connect(screenshotAsAction_, &QAction::triggered, this, [this] { screenshotAs(); });
    copyScreenshotAction_ = view->addAction("&Copy Screenshot", QKeySequence("Ctrl+Shift+C"));
    registerShortcut(copyScreenshotAction_, "copyScreenshot");
    connect(copyScreenshotAction_, &QAction::triggered, this, [this] { copyScreenshot(); });
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
    auto *filters = view->addMenu("Scaling &Filter");
    auto *filterGroup = new QActionGroup(this);
    const std::array<const char *, 3> filterNames = {"Nearest Neighbor", "Bilinear",
                                                     "Smooth Bilinear"};
    for (size_t i = 0; i < scalingFilterActions_.size(); ++i) {
        auto *action = filters->addAction(filterNames[i]);
        action->setObjectName(QString("video/filter/%1").arg(i));
        action->setCheckable(true);
        filterGroup->addAction(action);
        scalingFilterActions_[i] = action;
        connect(action, &QAction::triggered, this, [this, i] {
            settings_.setValue("video/filter", int(i));
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
    buildPaletteFileMenu(palette);
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
    auto *messages = view->addAction("On-screen &Messages");
    messages->setObjectName("video/osd");
    messages->setCheckable(true);
    connect(messages, &QAction::triggered, this, [this](bool enabled) {
        settings_.setValue("video/osd", enabled);
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
    auto *channels = audio->addMenu("Audio Channels");
    channels->setObjectName("audioChannels");
    for (size_t channel = 0; channel < audioChannelActions_.size(); ++channel) {
        auto *action = channels->addAction(audioChannelNames[channel],
                                           QKeySequence(QString("Alt+%1").arg(channel + 1)));
        audioChannelActions_[channel] = action;
        action->setCheckable(true);
        action->setChecked(true);
        registerShortcut(action, QString("audioChannel/%1").arg(channel));
        connect(action, &QAction::toggled, this, [this, channel](bool audible) {
            if (emulator_)
                cupid_gb_apu_set_channel_muted(&emulator_->gb, unsigned(channel), !audible);
            notifyOnScreen(
                QString("Channel %1 %2").arg(channel + 1).arg(audible ? "unmuted" : "muted"));
        });
    }
    recordingAction_ = audio->addAction("Start Audio &Recording…");
    recordingAction_->setToolTip(
        "Record forward stereo audio before host mute, volume, and resampling. "
        "Pause and rewind add no audio. Resets preserve recording; opening or closing "
        "a cartridge or changing the sample rate ends the file.");
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
    auto *motion = emulation->addMenu("Motion-Controlled &Games");
    mouseMotionAction_ = motion->addAction("Allow &mouse control");
    mouseMotionAction_->setObjectName("input/allowMouseMotion");
    mouseMotionAction_->setCheckable(true);
    mouseMotionAction_->setChecked(settings_.value("input/allowMouseMotion", false).toBool());
    connect(mouseMotionAction_, &QAction::toggled, this, [this](bool enabled) {
        settings_.setValue("input/allowMouseMotion", enabled);
        releaseMouseInput();
    });
    connectionsMenu_ = emulation->addMenu("&Connect");
    auto *accessories = new QActionGroup(connectionsMenu_);
    disconnectAction_ = connectionsMenu_->addAction("&None");
    printerAction_ = connectionsMenu_->addAction("Game Boy &Printer");
    for (auto *action : {disconnectAction_, printerAction_}) {
        action->setCheckable(true);
        accessories->addAction(action);
    }
    disconnectAction_->setChecked(true);
    connect(disconnectAction_, &QAction::triggered, this, [this] {
        disconnectLink();
        setPrinterConnected(false);
    });
    connect(printerAction_, &QAction::triggered, this, [this] { setPrinterConnected(true); });
    linkMenu_ = connectionsMenu_->addMenu("Link &Cable");
    linkMenu_->setObjectName("linkCable");
    connect(linkMenu_, &QMenu::aboutToShow, this, [this] { refreshLinkMenu(); });
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
            "Open: Ctrl+O   Reset: Ctrl+R   Quick Reset: Ctrl+Alt+R   Pause: Ctrl+P\n"
            "Fullscreen: Ctrl+F   Mute: Ctrl+M   Screenshot: F12\n\n"
            "Audio channels: Alt+1–4. Change shortcuts and audio filters in Settings.\n\n"
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
        const QPointer<MainWindow> self(this);
        const bool previousOperation = stateOperation_;
        stateOperation_ = true;
        const auto operation = qScopeGuard([self, previousOperation] {
            if (self)
                self->stateOperation_ = previousOperation;
        });
        // A nested native event loop may close the owning window. Keep the
        // dialog independently guarded instead of parenting a stack object.
        const QPointer<QMessageBox> dialog =
            new QMessageBox(QMessageBox::Critical, "CupidGB", message, QMessageBox::Ok, this);
        const auto cleanup = qScopeGuard([dialog] { delete dialog.data(); });
        dialog->exec();
        if (self)
            resetPacing();
    }
}

bool MainWindow::openRom(const QString &path) {
    return openRom(path, false);
}

bool MainWindow::openRom(const QString &path, bool reload, ResetWorkflow workflow) {
    const QPointer<MainWindow> self(this);
    const bool previousOperation = stateOperation_;
    stateOperation_ = true;
    const auto operation = qScopeGuard([self, previousOperation] {
        if (self)
            self->stateOperation_ = previousOperation;
    });
    const uint64_t previousSession = sessionId_;
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
    const GameMedia media = readGameMedia(path, error);
    const QByteArray &data = media.data;
    const bool music = media.kind == GameMediaKind::Gbs;
    if (data.isEmpty()) {
        reportError(error);
        return false;
    }
    auto candidate = music ? std::unique_ptr<CupidEmulator>() : std::make_unique<CupidEmulator>();
    int model = reload && emulator_
                    ? int(emulator_->gb.model)
                    : modelOverride_.value_or(settings_.value("emulation/model", -1).toInt());
    if (!music && model < 0)
        model = automaticModel(data);
    if ((model >= 0 && !cupid_gb_model_is_valid(CupidGbModel(model))) || (!music && model < 0)) {
        reportError("The configured hardware model is invalid.");
        return false;
    }
    if (!music && (uint8_t(data[0x143]) & 0xc0) == 0xc0 &&
        !cupid_gb_model_is_cgb(CupidGbModel(model))) {
        reportError("This cartridge requires Game Boy Color. Select Automatic or Game Boy Color.");
        return false;
    }
    std::optional<CupidGbGbsInfo> candidateMusic;
    if (music) {
        CupidGbGbsInfo metadata{};
        if (!prepareMusicCandidate(candidate, data, model, metadata))
            return false;
        candidateMusic = metadata;
    } else {
        if (powerOnSeedOverride_)
            cupid_gb_set_power_on_seed(&candidate->gb, *powerOnSeedOverride_);
        else
            cupid_gb_clear_power_on_seed(&candidate->gb);
        // Select host policy before initialization and battery import establish clock history.
        cupid_gb_set_rtc_mode(&candidate->gb, configuredRtcMode(settings_));
        cupid_emulator_init(candidate.get(), cupid_gb_model_is_cgb(CupidGbModel(model))
                                                 ? CUPID_SYSTEM_GBC
                                                 : CUPID_SYSTEM_GB);
        cupid_gb_set_model(&candidate->gb, CupidGbModel(model));
        if (emulator_)
            candidate->gb.mbc3_extended_history = emulator_->gb.mbc3_extended_history;
        if (!cupid_emulator_load_rom(candidate.get(),
                                     reinterpret_cast<const uint8_t *>(data.constData()),
                                     size_t(data.size()))) {
            cupid_emulator_shutdown(candidate.get());
            reportError("The cartridge could not be loaded.");
            return false;
        }
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
    if (!storageAvailable(absolute, save)) {
        cupid_emulator_shutdown(candidate.get());
        return false;
    }
    // Reject unavailable firmware before exporting the current cartridge's clock.
    const QString bootFolder = music ? QString() : bootDirectory();
    if (!bootFolder.isEmpty()) {
        QString name = "dmg_boot.bin";
        if (model == CUPID_GB_MODEL_CGB_0)
            name = "cgb0_boot.bin";
        else if (cupid_gb_model_is_cgb(CupidGbModel(model))) {
            name = "cgb_boot.bin";
            // Revision E firmware is optional. Existing shared color firmware
            // remains the fallback for configurations created before revision selection.
            if (model == CUPID_GB_MODEL_CGB &&
                QFileInfo::exists(QDir(bootFolder).filePath("cgbE_boot.bin")))
                name = "cgbE_boot.bin";
        } else if (model == CUPID_GB_MODEL_MGB)
            name = "mgb_boot.bin";
        else if (model == CUPID_GB_MODEL_SGB || model == CUPID_GB_MODEL_SGB_PAL)
            name = "sgb_boot.bin";
        else if (model == CUPID_GB_MODEL_SGB2)
            name = "sgb2_boot.bin";
        QFile boot(QDir(bootFolder).filePath(name));
        if (!boot.open(QIODevice::ReadOnly) ||
            boot.size() != (cupid_gb_model_is_cgb(CupidGbModel(model)) ? 0x900 : 0x100)) {
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
    }
    const auto batteryIdentity = [](const QString &path) {
        const QFileInfo file(path);
        const QString canonical = file.canonicalFilePath();
        return canonical.isEmpty() ? file.absoluteFilePath() : canonical;
    };
    const bool sameBattery =
        emulator_ && batteryIdentity(save).compare(batteryIdentity(savePath_), pathCase) == 0;
    const bool liveBattery = sameBattery && romData_ == data;
    const auto loadBattery = [&] {
        QFile battery(save);
        size_t maximumSize = cupid_gb_battery_size(&candidate->gb);
        if (!maximumSize || !battery.exists())
            return true;
        // MBC7 also accepts older saves padded to the header's RAM population.
        // Other controllers fit their selected SRAM and largest supported clock trailer.
        if (candidate->gb.header.mbc_type == CUPID_GB_MBC7)
            maximumSize = std::max(maximumSize,
                                   cupid_gb_ram_size_from_code(candidate->gb.header.ram_size_code));
        if (!battery.open(QIODevice::ReadOnly) || battery.size() > qint64(maximumSize)) {
            reportError("Cannot load battery save or its size does not match cartridge RAM: " +
                        save);
            return false;
        }
        const QByteArray ram = battery.readAll();
        if (ram.size() != battery.size()) {
            reportError("Battery save read failed: " + save);
            return false;
        }
        const auto result = cupid_gb_load_battery(
            &candidate->gb, reinterpret_cast<const uint8_t *>(ram.constData()), size_t(ram.size()));
        if (result != CUPID_GB_BATTERY_OK) {
            reportError("Cannot load battery save: " +
                        QString::fromLatin1(cupid_gb_battery_result_name(result)));
            return false;
        }
        return true;
    };
    // A different candidate must validate its existing battery before the current
    // clock is polled. Resetting the same cartridge instead commits live history
    // first, replacing stale disk bytes before the new session imports them.
    if (!liveBattery && !loadBattery()) {
        cupid_emulator_shutdown(candidate.get());
        return false;
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
    if (!flushBattery(true) || (sameBattery && !loadBattery())) {
        cupid_emulator_shutdown(candidate.get());
        return false;
    }
    const QByteArray savedBeforeReset = savedRam_;
    if (workflow == ResetWorkflow::Open && !stopRecording()) {
        cupid_emulator_shutdown(candidate.get());
        return false;
    }
    // Initial firmware entry restores the same generation's cold memory after
    // cartridge loading and battery import have prepared persistent clock history.
    if (workflow != ResetWorkflow::Open) {
        if (!prepareResetCandidate(candidate.get(), workflow, liveBattery)) {
            cupid_emulator_shutdown(candidate.get());
            return false;
        }
    } else if (!bootFolder.isEmpty())
        cupid_gb_start_boot_rom(&candidate->gb);
    // Startup may rebuild the APU. Install host choices after its final reset,
    // before the candidate can execute or produce its first audio sample.
    applyAudioPolicy(&candidate->gb);
    const bool sameCartridge = emulator_ && romData_ == data &&
                               romPath_.compare(absolute, pathCase) == 0 &&
                               emulator_->gb.model == candidate->gb.model;
    const bool sameFirmware =
        emulator_ && emulator_->gb.boot_rom_size == candidate->gb.boot_rom_size &&
        !std::memcmp(emulator_->gb.boot_rom, candidate->gb.boot_rom, candidate->gb.boot_rom_size);
    const bool retainHistory =
        sameCartridge && sameFirmware &&
        (workflow == ResetWorkflow::Quick || workflow == ResetWorkflow::Reload);
    const uint64_t previousPresentationCycles =
        emulator_ ? cupid_gb_presentation_cycles(&emulator_->gb) : 0;
    const bool resetChangedBattery =
        workflow != ResetWorkflow::Open && sameBattery && emulator_ &&
        candidate->gb.battery_write_generation != emulator_->gb.battery_write_generation;
    if (!sameCartridge && workflow == ResetWorkflow::Open) {
        disconnectLink();
        if (!self || sessionId_ != previousSession) {
            cupid_emulator_shutdown(candidate.get());
            return false;
        }
        setPrinterConnected(false);
    }
    if (!replaceLinkEndpoint(&candidate->gb)) {
        cupid_emulator_shutdown(candidate.get());
        return false;
    }
    commitCameraSession(&candidate->gb, workflow);
    if (emulator_)
        cupid_emulator_shutdown(emulator_.get());
    emulator_ = std::move(candidate);
    setMusicSession(candidateMusic, candidateMusic ? candidateMusic->first_track : 0);
    // Device reclassification can emit output immediately. The outer operation
    // still gates callbacks; resetting a session does not release its controls.
    if (workflow == ResetWorkflow::Open)
        releaseKeys(false);
    controllers_->setMotionEnabled(emulator_->gb.header.mbc_type == CUPID_GB_MBC7);
    cheats_ = std::move(candidateCheats);
    cheatPath_ = cheatPath;
    if (!sameCheatSession)
        cheatSaveWarning_.clear();
    cupid_gb_set_cheats(&emulator_->gb, cheats_.get());
    refreshCheats(true);
    resetPrinter();
    const auto &gb = emulator_->gb;
    const bool motorCapable =
        gb.header.has_rumble &&
        (gb.header.mbc_type == CUPID_GB_MBC5 ||
         (gb.header.mbc_type == CUPID_GB_TPP1 && gb.rom_size > 0x153u && (gb.rom[0x153u] & 1u)));
    cupid_gb_set_rumble_strength_callback(&emulator_->gb, nullptr, nullptr);
    if (motorCapable)
        cupid_gb_set_rumble_sample_callback(
            &emulator_->gb,
            [](const CupidGb *gb, uint8_t strength, void *user) {
                auto *window = static_cast<MainWindow *>(user);
                if (!window->emulator_ || &window->emulator_->gb != gb)
                    return;
                window->rumbleSampleSum_ += strength;
                ++window->rumbleSampleCount_;
            },
            this);
    else
        cupid_gb_set_rumble_sample_callback(&emulator_->gb, nullptr, nullptr);
    if (motorCapable || music)
        cupid_gb_set_presentation_callback(
            &emulator_->gb,
            [](const CupidGb *gb, uint8_t type, void *user) {
                (void)type;
                auto *window = static_cast<MainWindow *>(user);
                if (!window->emulator_ || &window->emulator_->gb != gb)
                    return;
                if (window->musicInfo_)
                    window->musicFramePending_ = true;
                if (!window->rumbleSampleCount_)
                    return;
                window->rumblePublishedAmplitudes_.push_back(
                    double(window->rumbleSampleSum_) / (3.0 * double(window->rumbleSampleCount_)));
                window->rumbleSampleSum_ = window->rumbleSampleCount_ = 0;
            },
            this);
    else
        cupid_gb_set_presentation_callback(&emulator_->gb, nullptr, nullptr);
    resetControllerRumble();
    if (workflow == ResetWorkflow::Open)
        resetMotionInput();
    if (retainHistory)
        rebaseRewindAfterReset(previousPresentationCycles);
    else
        resetRewind();
    ++sessionId_;
    const uint64_t committedSession = sessionId_;
    resetDebuggerTimeline(!sameCartridge && !reload);
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    romData_ = data;
    romPath_ = absolute;
    if (debugger_ && !sameCartridge && !reload) {
        const QString symbols = info.dir().filePath(info.completeBaseName() + ".sym");
        if (QFileInfo::exists(symbols))
            loadDebuggerSymbols(symbols);
    }
    savePath_ = save;
    saveTimer_.stop();
    observedBatteryGeneration_ = emulator_->gb.battery_write_generation;
    batteryDirty_ = resetChangedBattery;
    savedRam_.clear();
    if (resetChangedBattery) {
        // HuC3 clock/alarm digits reset with its controller. Keep the last committed
        // bytes as the disk baseline so the ordinary autosave records that change.
        savedRam_ = savedBeforeReset;
        saveTimer_.start();
    } else if (cupid_gb_battery_size(&emulator_->gb)) {
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
    // Reload keeps its existing pause choice without a transient resume. The
    // reset transaction retains controls until an actual input event changes them.
    setPaused(workflow == ResetWorkflow::Reload && paused_, workflow != ResetWorkflow::Open);
    if (!self || sessionId_ != committedSession)
        return true;
    setWindowTitle(info.fileName() + " — CupidGB");
    statusBar()->showMessage(music                  ? "Music playback"
                             : bootFolder.isEmpty() ? "Post-boot startup; no boot ROM selected"
                                                    : "Boot ROM loaded");
    if (!cheatWarning.isEmpty()) {
        qWarning("%s", qPrintable(cheatWarning));
        statusBar()->showMessage(cheatWarning);
    }
    setupAudio();
    if (!self || sessionId_ != committedSession)
        return true;
    display_->messages.clear();
    if (!music)
        display_->messages.showCartridge(QCoreApplication::applicationVersion(), romData_);
    present(Presentation::Reset);
    // A new cartridge reclassifies held controller axes while callbacks are gated.
    // Commit those controls before its first snapshot; resets retain their native contacts.
    if (workflow == ResetWorkflow::Open)
        applyHeldButtons();
    rememberFrame();
    refreshActions();
    return true;
}

bool MainWindow::flushBattery(bool forceWrite) {
    if (!emulator_ || !cupid_gb_battery_size(&emulator_->gb))
        return true;
    // Natural RTC progress and status-only commands do not schedule autosaves.
    // Lifecycle boundaries still export them, including a new session with no file yet.
    if (!forceWrite && !batteryDirty_ &&
        observedBatteryGeneration_ == emulator_->gb.battery_write_generation)
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
    // savedRam_ also caches newly initialized bytes that may never have reached disk.
    if (forceWrite || ram != savedRam_) {
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
    const QPointer<MainWindow> self(this);
    const bool previousOperation = stateOperation_;
    stateOperation_ = true;
    const auto operation = qScopeGuard([self, previousOperation] {
        if (self)
            self->stateOperation_ = previousOperation;
    });
    const uint64_t previousSession = sessionId_;
    if (!flushBattery(true) || !stopRecording())
        return false;
    disconnectLink();
    if (!self || sessionId_ != previousSession)
        return true;
    cancelCameraCapture();
    hostCamera_.reset();
    cameraError_.clear();
    resetMotionInput();
    setPrinterConnected(false);
    ++sessionId_;
    const uint64_t closingSession = sessionId_;
    resetControllerRumble();
    resetDebuggerTimeline(true);
    saveTimer_.stop();
    batteryDirty_ = false;
    releaseKeys(false);
    stopAudio();
    if (!self || sessionId_ != closingSession)
        return true;
    audioBuffer_.reset();
    if (emulator_)
        cupid_emulator_shutdown(emulator_.get());
    emulator_.reset();
    refreshTimerInterval();
    if (controllers_)
        controllers_->setMotionEnabled(false);
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
    previousMessageFrame_ = {};
    currentMessageFrame_ = {};
    display_->image = {};
    display_->messages.clear();
    display_->setMinimumSize(160, 144);
    display_->update();
    setMusicSession(std::nullopt, 0);
    setWindowTitle("CupidGB");
    refreshActions();
    return true;
}

void MainWindow::refreshActions() {
    for (auto *action : {closeAction_, resetAction_, quickResetAction_, reloadAction_, pauseAction_,
                         frameAction_, screenshotAction_, screenshotAsAction_,
                         copyScreenshotAction_, loadStateFileAction_, recordingAction_})
        action->setEnabled(bool(emulator_));
    reloadAction_->setEnabled(bool(emulator_) && !musicInfo_);
    for (auto *action : {screenshotAction_, screenshotAsAction_, copyScreenshotAction_})
        action->setEnabled(bool(emulator_) && !musicInfo_);
    for (auto *action : windowSizeActions_)
        action->setEnabled(!musicInfo_);
    if (auto *fullscreen = findChild<QAction *>("fullscreen"))
        fullscreen->setEnabled(!musicInfo_);
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
    refreshMusicControls();
}

QString MainWindow::bootDirectory() const {
    return bootDirectoryOverride_.isNull() ? settings_.value("emulation/bootDirectory").toString()
                                           : bootDirectoryOverride_;
}

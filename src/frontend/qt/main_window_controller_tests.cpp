#include "main_window_internal.h"

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
        return smokeFailure(__FILE__, __LINE__);
    file.close();
    if (!openRom(file.fileName()))
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    const int baseline = SDL_NumJoysticks();
    VirtualPad first, second;
    if (!first.attach(0x81) || !second.attach(0x82))
        return smokeFailure(__FILE__, __LINE__);
    DeviceState a, b;
    if (!wait([&] {
            return controllers_->backend().state(first.id, &a) &&
                   controllers_->backend().state(second.id, &b);
        }))
        return smokeFailure(__FILE__, __LINE__);
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
        return smokeFailure(__FILE__, __LINE__);
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
        return smokeFailure(__FILE__,
                            __LINE__); // Guest reads immediately after both player-count changes.
    auto preferred = configuration;
    preferred.preferredDevice = a.info.persistentKey;
    controllers_->configure(preferred);
    controllers_->setPreferredInstance(first.id);
    if (emulator_->gb.joypad != 0xef || emulator_->gb.joypad_extra[0] != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    auto disabled = configuration;
    disabled.profiles[b.info.persistentKey].enabled = false;
    controllers_->configure(disabled);
    if (emulator_->gb.joypad != 0xef)
        return smokeFailure(__FILE__, __LINE__);
    controllers_->configure(configuration);
    transition = packetProgram(4) + QByteArray::fromHex("18fe");
    for (qsizetype i = 0; i < transition.size(); ++i)
        cupid_gb_write_u8(&emulator_->gb, uint16_t(0xc000 + i), uint8_t(transition[i]));
    emulator_->gb.cpu.pc = 0xc000;
    if (!runFrame() || emulator_->gb.sgb.player_count != 4 || emulator_->gb.joypad != 0xef)
        return smokeFailure(__FILE__, __LINE__);
    emulator_->gb.sgb.current_player = 1;
    emulator_->gb.io_registers[0] = 0x10;
    if ((cupid_gb_read_u8(&emulator_->gb, 0xff00) & 0xf) != 0xd)
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent press(QEvent::KeyPress, keys_[4], Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, keys_[4], Qt::NoModifier);
    QApplication::sendEvent(this, &press);
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    if (!wait([&] { return controllers_->output().buttons[0] == 0xff; }) ||
        emulator_->gb.joypad != 0xef)
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(this, &release);
    if (emulator_->gb.joypad != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    SDL_JoystickSetVirtualButton(first.handle, 2, 1);
    if (!wait([&] { return controllerRapidHeld_[0][0] && emulator_->gb.joypad == 0xef; }))
        return smokeFailure(__FILE__, __LINE__);
    for (unsigned phase = 0; phase < 8; ++phase) {
        advanceControls();
        if (emulator_->gb.joypad != 0xef)
            return smokeFailure(__FILE__,
                                __LINE__); // Rapid release cannot release a separately held A.
    }
    SDL_JoystickSetVirtualButton(first.handle, 2, 0);
    if (!wait([&] { return !controllerRapidHeld_[0][0]; }))
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualButton(second.handle, 2, 1);
    if (!wait([&] { return controllerRapidHeld_[1][1]; }))
        return smokeFailure(__FILE__, __LINE__);
    for (unsigned phase = 0; phase < 8; ++phase) {
        advanceControls();
        if (emulator_->gb.joypad != 0xef || emulator_->gb.joypad_extra[0] != 0xdf)
            return smokeFailure(__FILE__, __LINE__);
    }
    SDL_JoystickSetVirtualButton(second.handle, 2, 0);
    if (!wait([&] { return !controllerRapidHeld_[1][1]; }))
        return smokeFailure(__FILE__, __LINE__);
    const bool oldMute = muteAction_->isChecked();
    SDL_JoystickSetVirtualButton(first.handle, 3, 1);
    SDL_JoystickUpdate();
    SDL_JoystickSetVirtualButton(first.handle, 3, 0);
    SDL_JoystickUpdate();
    if (!wait([&] { return muteAction_->isChecked() != oldMute; }))
        return smokeFailure(__FILE__, __LINE__);
    muteAction_->setChecked(oldMute);
    pauseDebugger();
    const QByteArray stopped = captureState();
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    SDL_JoystickSetVirtualButton(second.handle, 1, 0);
    if (!wait([&] { return controllers_->output().buttons == ControllerOutput{}.buttons; }) ||
        captureState() != stopped)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(false);
    if (emulator_->gb.joypad != 0xff || emulator_->gb.joypad_extra[0] != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    controllers_->setFocused(true);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    if (!wait([&] { return emulator_->gb.joypad == 0xef; }))
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray held = captureState();
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    if (!wait([&] { return emulator_->gb.joypad == 0xff; }) || !restoreState(held, false))
        return smokeFailure(__FILE__, __LINE__);
    applyHeldButtons();
    if (emulator_->gb.joypad != 0xff)
        return smokeFailure(__FILE__, __LINE__);
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
                return smokeFailure(__FILE__, __LINE__);
            SDL_JoystickSetVirtualButton(first.handle, 0, 0);
            SDL_JoystickSetVirtualButton(first.handle, 1, 0);
            SDL_JoystickSetVirtualButton(first.handle, 6, 0);
            if (!wait([&] { return controllers_->output().buttons[0] == 0xff; }))
                return smokeFailure(__FILE__, __LINE__);
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
        return smokeFailure(__FILE__, __LINE__);
    QMenu *nested = nullptr;
    for (auto *action : emulationMenu->actions())
        if (action->menu()) {
            nested = action->menu();
            break;
        }
    if (!nested)
        return smokeFailure(__FILE__, __LINE__);
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
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualAxis(first.handle, 0, 24000);
    if (!wait([&] { return nested->isVisible() && menuDepth_ >= 2; }))
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualAxis(first.handle, 0, 0);
    controllers_->backend().pollNow();
    SDL_JoystickSetVirtualButton(first.handle, 1, 1);
    if (!wait(
            [&] { return !nested->isVisible() && emulationMenu->isVisible() && menuDepth_ == 1; }))
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 1, 0);
    controllers_->backend().pollNow();
    menuClock.restart();
    if (!wait([&] { return menuClock.elapsed() >= 30; }) || !menuDepth_ ||
        emulator_->gb.elapsed_cycles != menuCycles)
        return smokeFailure(__FILE__, __LINE__);
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
        return smokeFailure(__FILE__, __LINE__);
    }
    const int menuDirection = SDL_JoystickGetAxis(first.handle, 1) < 0 ? -1 : 1;
    SDL_JoystickSetVirtualAxis(first.handle, 1, int16_t(24500 * menuDirection));
    controllers_->backend().pollNow();
    QApplication::processEvents();
    if (emulationMenu->activeAction() != pauseAction_)
        return smokeFailure(__FILE__, __LINE__); // Axis jitter cannot repeat one navigation press.
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    if (!wait([&] { return paused_ && !menuDepth_; }) || emulator_->gb.elapsed_cycles != menuCycles)
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    SDL_JoystickSetVirtualAxis(first.handle, 1, 0);
    controllers_->backend().pollNow();
    emulationMenu->popup(mapToGlobal(QPoint(20, 20)));
    controllers_->setFocused(true);
    if (!selectMenuAction(pauseAction_))
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    if (!wait([&] { return !paused_ && !menuDepth_ && emulator_->gb.elapsed_cycles > menuCycles; }))
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    controllers_->backend().pollNow();
    controllers_->setFocused(true);
    controllers_->setDeliveryEnabled(true);
    cupid_gb_write_u8(&emulator_->gb, 0x4000, 8);
    resetControllerRumble();
    if (!runFrame() || !first.amplitude)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    if (first.amplitude)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(false);
    controllers_->setFocused(true);
    if (!runFrame() || !first.amplitude)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray motor = captureState();
    cupid_gb_write_u8(&emulator_->gb, 0x4000, 0);
    if (!restoreState(motor, false) || first.amplitude || !cupid_gb_rumble_enabled(&emulator_->gb))
        return smokeFailure(__FILE__, __LINE__);
    if (!closeRom() || first.amplitude)
        return smokeFailure(__FILE__, __LINE__);
    first.detach();
    second.detach();
    if (!wait([&] {
            return !controllers_->backend().state(first.id, &a) &&
                   !controllers_->backend().state(second.id, &b);
        }) ||
        SDL_NumJoysticks() != baseline)
        return smokeFailure(__FILE__, __LINE__);
    controllers_->configure(original);
    controllerSmokeActive_ = false;
    if (originalSettings.isValid())
        settings_.setValue("controllers/configuration", originalSettings);
    else
        settings_.remove("controllers/configuration");
    timer_.start();
    return true;
}

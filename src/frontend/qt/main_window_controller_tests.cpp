/**
 * @file    main_window_controller_tests.cpp
 * @brief   Controller routing, dialog workflows, and motor feedback regression checks.
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

#include <cmath>

extern "C" {
#include "cupid/gb/ppu.h"
}

bool MainWindow::controllerSmokeTest(const QString &directory) {
    using namespace cupid::input;
    controllerSmokeActive_ = true;

    struct VirtualPad {
        SDL_JoystickID id = -1;
        SDL_Joystick *handle = nullptr;
        bool attached = false;
        uint16_t amplitude = 0;
        unsigned rumbleCalls = 0;

        static int SDLCALL feedback(void *user, Uint16 low, Uint16 high) {
            auto *pad = static_cast<VirtualPad *>(user);
            pad->amplitude = std::max(low, high);
            ++pad->rumbleCalls;
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
    cupid_gb_write_u8(&emulator_->gb, 0xff00, 0x10);
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
                auto *device = dialog->findChild<QComboBox *>("controllerDevice");
                device->setCurrentIndex(device->findData(first.id));
                bool rumbleTestChecked = true;
                if (initialPaused && accept) {
                    auto *strength = dialog->findChild<QSlider *>("controllerStrength");
                    auto *test = dialog->findChild<QPushButton *>("controllerTestRumble");
                    auto *status = dialog->findChild<QLabel *>("controllerStatus");
                    if (!strength || !test || !status) {
                        rumbleTestChecked = false;
                    } else {
                        controllers_->setFocused(true);
                        strength->setValue(0);
                        const unsigned beforeTest = first.rumbleCalls;
                        test->click();
                        const bool zeroStrengthHonest =
                            first.rumbleCalls == beforeTest && !first.amplitude &&
                            status->text().contains("0%") &&
                            status->text().contains("increase", Qt::CaseInsensitive);
                        strength->setValue(50);
                        test->click();
                        const bool positiveTestSent =
                            first.rumbleCalls > beforeTest && first.amplitude > 0 &&
                            status->text().startsWith("Sent a 150 ms rumble test.");
                        const unsigned beforeStop = first.rumbleCalls;
                        strength->setValue(0);
                        test->click();
                        DeviceState stopped;
                        const bool zeroStrengthStopsActive =
                            first.rumbleCalls > beforeStop && !first.amplitude &&
                            controllers_->backend().state(first.id, &stopped) &&
                            !stopped.rumbleActive && status->text().contains("0%");
                        strength->setValue(50);
                        controllers_->setFocused(false);
                        const unsigned beforeBlocked = first.rumbleCalls;
                        test->click();
                        const bool focusBlocksTest =
                            first.rumbleCalls == beforeBlocked && !first.amplitude &&
                            status->text().contains("focus", Qt::CaseInsensitive);
                        controllers_->setFocused(true);
                        strength->setValue(controllers_->configuration().rumbleStrength);
                        rumbleTestChecked = zeroStrengthHonest && positiveTestSent &&
                                            zeroStrengthStopsActive && focusBlocksTest;
                    }
                }
                checked = rumbleTestChecked &&
                          controllers_->output().buttons == ControllerOutput{}.buttons &&
                          emulator_->gb.elapsed_cycles == beforeCycles &&
                          background->isChecked() != backgroundBefore;
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
    SDL_JoystickSetVirtualAxis(first.handle, 1, 0);
    controllers_->backend().pollNow();
    if (!wait([&] { return controllers_->output().buttons == ControllerOutput{}.buttons; }) ||
        emulator_->gb.joypad != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    // Physical controller shortcuts retain a simultaneously held gameplay
    // button through each reset workflow, including the new core instance.
    const auto resetConfiguration = controllers_->configuration();
    controllers_->setFocused(true);
    controllers_->setDeliveryEnabled(true);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    if (!wait([&] { return !(emulator_->gb.joypad & 0x10); }))
        return smokeFailure(__FILE__, __LINE__);
    {
        QScopedValueRollback<bool> operation(stateOperation_, true);
        refreshControllerGate();
        SDL_JoystickSetVirtualButton(first.handle, 0, 0);
        controllers_->backend().pollNow();
        if (!controllerInputDeferred_ || controllers_->output().buttons[0] != 0xff ||
            (controllerOutput_.buttons[0] & 0x10))
            return smokeFailure(__FILE__, __LINE__);
    }
    refreshControllerGate();
    if (controllerInputDeferred_ || emulator_->gb.joypad != 0xff ||
        controllerOutput_.buttons[0] != 0xff) {
        const auto output = controllers_->output();
        const auto live = livePlayerButtons();
        qCritical("Controller release after transaction failed: deferred=%d operation=%d "
                  "menu=%u/%d delivery=%d appState=%d paused=%d manager=%02x retained=%02x "
                  "live=%02x/%02x/%02x/%02x physical=%02x core=%02x players=%u navigationY=%d",
                  int(controllerInputDeferred_), int(stateOperation_), menuDepth_,
                  int(menuInputSuspended_), int(controllers_->deliveryEnabled()),
                  int(QApplication::applicationState()), int(paused_), unsigned(output.buttons[0]),
                  unsigned(controllerOutput_.buttons[0]), unsigned(live[0]), unsigned(live[1]),
                  unsigned(live[2]), unsigned(live[3]), unsigned(physicalJoypad_),
                  unsigned(emulator_->gb.joypad), controllerPlayerCount_,
                  int(SDL_JoystickGetAxis(first.handle, 1)));
        return smokeFailure(__FILE__, __LINE__);
    }
    for (const QString &command :
         {QStringLiteral("reset"), QStringLiteral("quickReset"), QStringLiteral("reload")}) {
        auto shortcutConfiguration = resetConfiguration;
        shortcutConfiguration.profiles[a.info.persistentKey].bindings["shortcut/" + command] = {
            {Binding::Button, 7, 1}};
        controllers_->configure(shortcutConfiguration);
        controllers_->setFocused(true);
        controllers_->setDeliveryEnabled(true);
        SDL_JoystickSetVirtualButton(first.handle, 0, 1);
        if (!wait([&] { return !(emulator_->gb.joypad & 0x10); }))
            return smokeFailure(__FILE__, __LINE__);
        const uint64_t beforeReset = sessionId_;
        SDL_JoystickSetVirtualButton(first.handle, 7, 1);
        if (!wait([&] { return sessionId_ != beforeReset; }) || sessionId_ != beforeReset + 1 ||
            failed_ || paused_ || emulator_->gb.elapsed_cycles || (emulator_->gb.joypad & 0x10) ||
            (controllerOutput_.buttons[0] & 0x10))
            return smokeFailure(__FILE__, __LINE__);
        SDL_JoystickSetVirtualButton(first.handle, 7, 0);
        SDL_JoystickSetVirtualButton(first.handle, 0, 0);
        if (!wait([&] { return emulator_->gb.joypad == 0xff; }))
            return smokeFailure(__FILE__, __LINE__);
    }
    controllers_->configure(resetConfiguration);
    controllers_->setFocused(true);
    controllers_->setDeliveryEnabled(true);
    SDL_JoystickSetVirtualButton(first.handle, 0, 1);
    if (!wait([&] { return !(emulator_->gb.joypad & 0x10); }))
        return smokeFailure(__FILE__, __LINE__);
    emulationMenu->popup(mapToGlobal(QPoint(20, 20)));
    controllers_->setFocused(true);
    if (!menuInputSuspended_ || (emulator_->gb.joypad & 0x10) ||
        !selectMenuAction(quickResetAction_))
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t beforeMenuReset = sessionId_;
    SDL_JoystickSetVirtualButton(second.handle, 0, 1);
    if (!wait([&] { return sessionId_ != beforeMenuReset; }) || sessionId_ != beforeMenuReset + 1 ||
        menuDepth_ || paused_ || emulator_->gb.elapsed_cycles || (emulator_->gb.joypad & 0x10))
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualButton(second.handle, 0, 0);
    SDL_JoystickSetVirtualAxis(first.handle, 1, 0);
    controllers_->backend().pollNow();
    emulationMenu->popup(mapToGlobal(QPoint(20, 20)));
    controllers_->setFocused(true);
    SDL_JoystickSetVirtualButton(first.handle, 0, 0);
    if (!wait([&] { return emulator_->gb.joypad == 0xff; }) || !menuInputSuspended_)
        return smokeFailure(__FILE__, __LINE__);
    const bool menuMute = muteAction_->isChecked();
    SDL_JoystickSetVirtualButton(first.handle, 2, 1);
    SDL_JoystickSetVirtualButton(first.handle, 3, 1);
    controllers_->backend().pollNow();
    QApplication::processEvents();
    if (emulator_->gb.joypad != 0xff || muteAction_->isChecked() != menuMute ||
        !menuInputSuspended_)
        return smokeFailure(__FILE__, __LINE__);
    SDL_JoystickSetVirtualButton(first.handle, 2, 0);
    SDL_JoystickSetVirtualButton(first.handle, 3, 0);
    controllers_->backend().pollNow();
    emulationMenu->hide();
    if (!wait([&] { return !menuInputSuspended_; }))
        return smokeFailure(__FILE__, __LINE__);
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

    // TPP1 exposes graded motor levels independently of its raw status register.
    // Keep all host observations on virtual SDL devices and measured guest spans.
    const auto motorConfiguration = controllers_->configuration();
    const auto openMotorCartridge = [&](unsigned features) {
        QByteArray fixture(32768, char(0));
        fixture[0x100] = char(0xc3);
        fixture[0x102] = 2;
        fixture[0x143] = char(0x80);
        fixture[0x147] = char(0xbc);
        fixture[0x149] = char(0xc1);
        fixture[0x14a] = 0x65;
        fixture[0x150] = 1;
        fixture[0x152] = 1;
        fixture[0x153] = char(features);
        // NEXT and FINISH should each traverse enough instructions for more than
        // one debugger slice, then return to the normal idle loop.
        fixture[0x200] = char(0xcd); // CALL $0220
        fixture[0x201] = char(0x20);
        fixture[0x202] = char(0x02);
        fixture[0x203] = char(0x18); // JR $0203
        fixture[0x204] = char(0xfe);
        fixture[0x3a0] = char(0xc9); // RET after 384 NOPs
        uint8_t checksum = 0;
        for (unsigned address = 0x134; address <= 0x14c; ++address)
            checksum = uint8_t(checksum - uint8_t(fixture[qsizetype(address)]) - 1u);
        fixture[0x14d] = char(checksum);
        QFile generated(
            QDir(directory).filePath(QStringLiteral("controller-motor-%1.gb").arg(features)));
        if (!generated.open(QIODevice::WriteOnly) || generated.write(fixture) != fixture.size())
            return false;
        generated.close();
        if (!openRom(generated.fileName()))
            return false;
        timer_.stop();
        controllers_->setFocused(true);
        controllers_->setDeliveryEnabled(true);
        return !first.amplitude;
    };
    const auto stepMotor = [this](unsigned instructions) {
        for (unsigned i = 0; i < instructions; ++i)
            if (!cupid_gb_step(&emulator_->gb))
                return false;
        return true;
    };
    const auto publishMotor = [this, &first] {
        // An incomplete guest interval must never reach the controller.
        const unsigned calls = first.rumbleCalls;
        finishControllerRumble();
        if (first.rumbleCalls != calls)
            return false;
        // A pending LCD-enable presentation closes this interval without
        // advancing the guest oscillator or inventing another motor sample.
        const bool hadSamples = rumbleSampleCount_ != 0;
        const size_t queued = rumblePublishedAmplitudes_.size();
        emulator_->gb.display_cycles_since_frame = 4561;
        cupid_gb_prepare_lcd_enable(&emulator_->gb);
        if (first.rumbleCalls != calls ||
            rumblePublishedAmplitudes_.size() != queued + size_t(hadSamples))
            return false;
        finishControllerRumble();
        return true;
    };
    const auto motorNear = [&first](int expected) {
        return std::abs(int(first.amplitude) - expected) <= 1;
    };
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    if (!openMotorCartridge(9) || emulator_->gb.model != CUPID_GB_MODEL_CGB ||
        !emulator_->gb.cgb_mode)
        return smokeFailure(__FILE__, __LINE__);
    for (unsigned strength = 1; strength <= 3; ++strength) {
        cupid_gb_write_u8(&emulator_->gb, 3, uint8_t(0x20u + strength));
        if (!stepMotor(32))
            return smokeFailure(__FILE__, __LINE__);
        if (!publishMotor())
            return smokeFailure(__FILE__, __LINE__);
        if (!motorNear(int(strength * 21845u)))
            return smokeFailure(__FILE__, __LINE__);
    }

    resetControllerRumble();
    unsigned levelInstructions = 0, totalInstructions = 0;
    for (const auto &[strength, instructions] :
         {std::pair<unsigned, unsigned>{1, 7}, {3, 11}, {2, 13}}) {
        cupid_gb_write_u8(&emulator_->gb, 3, uint8_t(0x20u + strength));
        if (!stepMotor(instructions))
            return smokeFailure(__FILE__, __LINE__);
        levelInstructions += instructions * strength;
        totalInstructions += instructions;
    }
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    const int mixedAmplitude =
        int(std::lround(65535.0 * levelInstructions / (3.0 * totalInstructions)));
    if (!motorNear(mixedAmplitude))
        return smokeFailure(__FILE__, __LINE__);

    resetControllerRumble();
    auto &motorGb = emulator_->gb;
    cupid_gb_write_u8(&motorGb, 3, 0x21);
    const uint64_t normalStart = motorGb.elapsed_cycles;
    if (!stepMotor(16))
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t normalCycles = motorGb.elapsed_cycles - normalStart;
    const uint64_t normalSamples = rumbleSampleCount_;
    motorGb.double_speed = true;
    motorGb.io_registers[0x4d] |= 0x80u;
    cupid_gb_write_u8(&motorGb, 3, 0x23);
    const uint64_t fastStart = motorGb.elapsed_cycles;
    if (!stepMotor(16))
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t fastCycles = motorGb.elapsed_cycles - fastStart;
    const uint64_t fastSamples = rumbleSampleCount_ - normalSamples;
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (!normalSamples || fastSamples != normalSamples || normalCycles <= fastCycles ||
        !motorNear(43690))
        return smokeFailure(__FILE__, __LINE__);
    motorGb.double_speed = false;
    motorGb.io_registers[0x4d] &= uint8_t(~0x80u);

    cupid_gb_write_u8(&motorGb, 3, 0x22);
    const QByteArray gradedMotor = captureState();
    cupid_gb_write_u8(&emulator_->gb, 3, 0x23);
    if (!stepMotor(32))
        return smokeFailure(__FILE__, __LINE__);
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (!motorNear(65535) || !restoreState(gradedMotor, false) || first.amplitude ||
        cupid_gb_rumble_strength(&emulator_->gb) != 2 || !stepMotor(32))
        return smokeFailure(__FILE__, __LINE__);
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (!motorNear(43690) || restoreState(QByteArray("invalid motor state"), false) ||
        !motorNear(43690))
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    if (first.amplitude)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(false);
    controllers_->setFocused(true);
    if (!stepMotor(32))
        return smokeFailure(__FILE__, __LINE__);
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (!motorNear(43690))
        return smokeFailure(__FILE__, __LINE__);

    auto halfMotor = motorConfiguration;
    halfMotor.rumbleStrength = 50;
    controllers_->configure(halfMotor);
    if (first.amplitude || !stepMotor(32))
        return smokeFailure(__FILE__, __LINE__);
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (!motorNear(26754))
        return smokeFailure(__FILE__, __LINE__);
    for (const auto &[level, expected] :
         {std::pair<unsigned, int>{1, 18918}, {3, 32767}, {2, 26754}}) {
        const unsigned calls = first.rumbleCalls;
        cupid_gb_write_u8(&emulator_->gb, 3, uint8_t(0x20u + level));
        if (!stepMotor(32))
            return smokeFailure(__FILE__, __LINE__);
        if (!publishMotor())
            return smokeFailure(__FILE__, __LINE__);
        if (!motorNear(expected) || first.rumbleCalls <= calls)
            return smokeFailure(__FILE__, __LINE__);
    }
    halfMotor.rumbleStrength = 0;
    controllers_->configure(halfMotor);
    if (first.amplitude || !stepMotor(32))
        return smokeFailure(__FILE__, __LINE__);
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (first.amplitude)
        return smokeFailure(__FILE__, __LINE__);
    halfMotor.rumbleStrength = 100;
    controllers_->configure(halfMotor);
    if (first.amplitude || !stepMotor(32))
        return smokeFailure(__FILE__, __LINE__);
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (!motorNear(43690))
        return smokeFailure(__FILE__, __LINE__);
    halfMotor.rumble = false;
    controllers_->configure(halfMotor);
    if (first.amplitude || !stepMotor(32))
        return smokeFailure(__FILE__, __LINE__);
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (first.amplitude)
        return smokeFailure(__FILE__, __LINE__);
    controllers_->configure(motorConfiguration);
    controllers_->setFocused(true);
    controllers_->setDeliveryEnabled(true);

    for (const auto kind : {CUPID_GB_RESET_QUICK, CUPID_GB_RESET_HARD}) {
        cupid_gb_write_u8(&emulator_->gb, 3, 0x22);
        if (!stepMotor(32))
            return smokeFailure(__FILE__, __LINE__);
        if (!publishMotor())
            return smokeFailure(__FILE__, __LINE__);
        if (!motorNear(43690) || !resetConsole(kind) || first.amplitude ||
            cupid_gb_rumble_strength(&emulator_->gb) || !stepMotor(32))
            return smokeFailure(__FILE__, __LINE__);
        if (!publishMotor())
            return smokeFailure(__FILE__, __LINE__);
        if (first.amplitude)
            return smokeFailure(__FILE__, __LINE__);
    }
    for (const auto workflow :
         {ResetWorkflow::Normal, ResetWorkflow::Quick, ResetWorkflow::Reload}) {
        cupid_gb_write_u8(&emulator_->gb, 3, 0x23);
        if (!stepMotor(32))
            return smokeFailure(__FILE__, __LINE__);
        if (!publishMotor())
            return smokeFailure(__FILE__, __LINE__);
        if (!motorNear(65535) || !resetCartridge(workflow) || first.amplitude ||
            cupid_gb_rumble_strength(&emulator_->gb))
            return smokeFailure(__FILE__, __LINE__);
        controllers_->setFocused(true);
        controllers_->setDeliveryEnabled(true);
        cupid_gb_write_u8(&emulator_->gb, 3, 0x21);
        if (!stepMotor(32))
            return smokeFailure(__FILE__, __LINE__);
        if (!publishMotor())
            return smokeFailure(__FILE__, __LINE__);
        if (!motorNear(21845))
            return smokeFailure(__FILE__, __LINE__);
    }

    // Paused debugger execution still clocks cartridge hardware. All completed
    // and partial feedback belongs to the debugger operation, never to a later
    // resumed frame or a controller request while the debugger is stopped.
    resetControllerRumble();
    pauseDebugger();
    cupid_gb_write_u8(&emulator_->gb, 3, 0x23);
    if (!stepMotor(2) || !rumbleSampleCount_)
        return smokeFailure(__FILE__, __LINE__);
    emulator_->gb.display_cycles_since_frame = 4561;
    cupid_gb_prepare_lcd_enable(&emulator_->gb);
    if (rumblePublishedAmplitudes_.empty() || rumbleSampleCount_)
        return smokeFailure(__FILE__, __LINE__);
    const unsigned debuggerRumbleCalls = first.rumbleCalls;
    const auto feedbackCleared = [this] {
        return !rumbleSampleSum_ && !rumbleSampleCount_ && rumblePublishedAmplitudes_.empty();
    };
    debuggerCommand("step");
    if (!debuggerTimer_.isActive())
        return smokeFailure(__FILE__, __LINE__);
    debuggerSlice();
    if (!paused_ || debuggerTimer_.isActive() || !feedbackCleared() ||
        first.rumbleCalls != debuggerRumbleCalls)
        return smokeFailure(__FILE__, __LINE__);

    const auto finishDebuggerCommand = [this, &feedbackCleared, &first,
                                        debuggerRumbleCalls](const char *command) {
        debuggerCommand(command);
        if (!debuggerTimer_.isActive())
            return false;
        unsigned slices = 0;
        do {
            debuggerSlice();
            ++slices;
            if (!feedbackCleared() || first.rumbleCalls != debuggerRumbleCalls)
                return false;
        } while (debuggerTimer_.isActive() && slices < 256);
        return !debuggerTimer_.isActive() && slices > 1 && paused_ && emulator_->gb.cpu.pc == 0x203;
    };
    debuggerCommand("set pc $0200");
    debuggerCommand("set sp $fffe");
    if (!finishDebuggerCommand("next"))
        return smokeFailure(__FILE__, __LINE__);
    debuggerCommand("set pc $0200");
    debuggerCommand("set sp $fffe");
    debuggerCommand("step");
    if (!debuggerTimer_.isActive())
        return smokeFailure(__FILE__, __LINE__);
    debuggerSlice();
    if (!feedbackCleared() || debuggerTimer_.isActive() || emulator_->gb.cpu.pc != 0x220 ||
        first.rumbleCalls != debuggerRumbleCalls || !finishDebuggerCommand("finish"))
        return smokeFailure(__FILE__, __LINE__);

    cupid_gb_write_u8(&emulator_->gb, 3, 0x20);
    debuggerCommand("continue");
    if (paused_ || !feedbackCleared() || first.rumbleCalls != debuggerRumbleCalls || !runFrame() ||
        first.amplitude)
        return smokeFailure(__FILE__, __LINE__);
    cupid_gb_write_u8(&emulator_->gb, 3, 0x22);
    // JR $0203 has three four-T-cycle deliveries at normal speed. The previous
    // off-motor presentation left two level-zero samples in the next interval.
    // The first level-two frame therefore publishes a mixed mean; only the
    // following complete interval can publish the steady level-two amplitude.
    constexpr uint64_t frameCycles = 70224;
    constexpr uint64_t frameDeliveries = frameCycles / 4u;
    constexpr uint64_t carriedZeroSamples = 2;
    constexpr uint64_t expectedCompletedCount = frameDeliveries;
    constexpr uint64_t expectedCompletedSum = 2u * (frameDeliveries - carriedZeroSamples);
    constexpr uint16_t mixedExpected =
        uint16_t((uint64_t(65535) * expectedCompletedSum) / (3u * expectedCompletedCount));
    static_assert(mixedExpected == 43685);
    if (emulator_->gb.double_speed || emulator_->gb.cpu.pc != 0x203 ||
        cupid_gb_rumble_strength(&emulator_->gb) != 2)
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t mixedCyclesBefore = emulator_->gb.elapsed_cycles;
    const uint64_t mixedCountBefore = rumbleSampleCount_;
    const uint64_t mixedSumBefore = rumbleSampleSum_;
    const unsigned mixedCallsBefore = first.rumbleCalls;
    const bool mixedFrameRan = runFrame();
    const uint64_t mixedCycles = emulator_->gb.elapsed_cycles - mixedCyclesBefore;
    const uint64_t newDeliveries = mixedCycles / 4u;
    const uint64_t completedCount = mixedCountBefore + newDeliveries - rumbleSampleCount_;
    const uint64_t completedSum = mixedSumBefore + 2u * newDeliveries - rumbleSampleSum_;
    if (!mixedFrameRan || mixedCycles != frameCycles || newDeliveries != frameDeliveries ||
        mixedCountBefore != carriedZeroSamples || mixedSumBefore ||
        rumbleSampleCount_ != carriedZeroSamples || rumbleSampleSum_ != 4 ||
        completedCount != expectedCompletedCount || completedSum != expectedCompletedSum ||
        first.amplitude != mixedExpected || first.rumbleCalls != mixedCallsBefore + 1 ||
        !rumblePublishedAmplitudes_.empty() || emulator_->gb.double_speed ||
        emulator_->gb.cpu.pc != 0x203) {
        qCritical("Controller mixed motor interval: frame=%d amplitude=%u expected=%u "
                  "cycles=%llu partial=%llu/%llu->%llu/%llu completed=%llu/%llu calls=%u->%u",
                  int(mixedFrameRan), unsigned(first.amplitude), unsigned(mixedExpected),
                  static_cast<unsigned long long>(mixedCycles),
                  static_cast<unsigned long long>(mixedCountBefore),
                  static_cast<unsigned long long>(mixedSumBefore),
                  static_cast<unsigned long long>(rumbleSampleCount_),
                  static_cast<unsigned long long>(rumbleSampleSum_),
                  static_cast<unsigned long long>(completedCount),
                  static_cast<unsigned long long>(completedSum), mixedCallsBefore,
                  first.rumbleCalls);
        return smokeFailure(__FILE__, __LINE__);
    }
    if (!runFrame() || !motorNear(43690))
        return smokeFailure(__FILE__, __LINE__);

    if (!openMotorCartridge(8))
        return smokeFailure(__FILE__, __LINE__);
    cupid_gb_write_u8(&emulator_->gb, 3, 0x23);
    if (!cupid_gb_rumble_enabled(&emulator_->gb) ||
        (cupid_gb_read_u8(&emulator_->gb, 0xa003) & 3u) != 3 ||
        cupid_gb_rumble_strength(&emulator_->gb) || !stepMotor(32))
        return smokeFailure(__FILE__, __LINE__);
    if (!publishMotor())
        return smokeFailure(__FILE__, __LINE__);
    if (first.amplitude || second.amplitude)
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

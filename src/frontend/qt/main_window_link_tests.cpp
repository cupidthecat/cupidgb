/**
 * @file    main_window_link_tests.cpp
 * @brief   Qt linked-game transport, pacing, storage, and lifecycle regression checks.
 * @author  cupidthecat
 * @date    2026-11-17
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

#include <QScopeGuard>
#include <QThread>

struct WaveRecorderTestAccess {
    static void reachCapacity(WaveRecorder &recorder) {
        // Exercise the real RIFF boundary without materializing four GiB of fixture data.
        recorder.frames_ = WaveRecorder::maximumFrames;
    }
};

namespace {
struct LinkVirtualPad {
    SDL_JoystickID id = -1;
    SDL_Joystick *handle = nullptr;
    uint16_t amplitude = 0;

    static int SDLCALL feedback(void *user, Uint16 low, Uint16 high) {
        static_cast<LinkVirtualPad *>(user)->amplitude = std::max(low, high);
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
        desc.name = "CupidGB link workflow pad";
        desc.userdata = this;
        desc.Rumble = feedback;
        const int index = SDL_JoystickAttachVirtualEx(&desc);
        if (index < 0)
            return false;
        id = SDL_JoystickGetDeviceInstanceID(index);
        handle = SDL_JoystickOpen(index);
        return handle != nullptr;
    }

    ~LinkVirtualPad() {
        for (int index = 0; index < SDL_NumJoysticks(); ++index)
            if (SDL_JoystickGetDeviceInstanceID(index) == id) {
                SDL_JoystickDetachVirtual(index);
                break;
            }
        if (handle)
            SDL_JoystickClose(handle);
    }
};

class LinkNavigationProbe final : public QObject {
  public:
    unsigned downPresses = 0;

  protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Down)
            ++downPresses;
        return false;
    }
};

bool waitLinkCondition(const std::function<bool()> &condition) {
    if (condition())
        return true;
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(1);
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (condition())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    deadline.start(2000);
    loop.exec();
    return condition();
}

QByteArray linkCartridge(uint8_t sent, bool master) {
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0xc3);
    rom[0x101] = char(0x50);
    rom[0x102] = char(0x01);
    rom[0x143] = char(0x80);
    rom[0x147] = char(0x03);
    rom[0x149] = char(0x02);
    QByteArray code;
    const auto append = [&code](std::initializer_list<uint8_t> bytes) {
        for (const uint8_t byte : bytes)
            code.append(char(byte));
    };
    append({0xf3, 0x31, 0xfe, 0xdf});
    for (const auto value : {std::pair<uint8_t, uint8_t>{0x26, 0x80},
                             {0x24, 0x77},
                             {0x25, 0x11},
                             {0x11, 0x80},
                             {0x12, 0xf0},
                             {0x13, 0x87},
                             {0x14, 0x83}})
        append({0x3e, value.second, 0xe0, value.first});
    append({0x3e, sent, 0xe0, 0x01, 0x3e, uint8_t(master ? 0x81 : 0x80), 0xe0, 0x02});
    append({0xf0, 0x02, 0xcb, 0x7f, 0x20, 0xfa});
    append({0xf0, 0x01, 0xea, 0x00, 0xc0, 0xf0, 0x0f, 0xea, 0x01, 0xc0});
    append({0x3e, 0x01, 0xea, 0x02, 0xc0, 0x76, 0x18, 0xfd});
    std::memcpy(rom.data() + 0x150, code.constData(), size_t(code.size()));
    uint8_t checksum = 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        checksum = uint8_t(checksum - uint8_t(rom[int(i)]) - 1);
    rom[0x14d] = char(checksum);
    return rom;
}

bool writeLinkCartridge(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool linkTransferFinished(const CupidGb &gb, uint8_t expected) {
    return cupid_gb_debug_peek(&gb, 0xc002) == 1 && cupid_gb_debug_peek(&gb, 0xc000) == expected &&
           (cupid_gb_debug_peek(&gb, 0xc001) & 8) != 0;
}
} // namespace

bool MainWindow::linkSmokeTest(const QString &directory) {
    QMap<QString, QVariant> previousPreferences;
    for (const auto &key : settings_.allKeys())
        previousPreferences[key] = settings_.value(key);
    const auto restorePreferences = qScopeGuard([&] {
        settings_.clear();
        for (auto it = previousPreferences.cbegin(); it != previousPreferences.cend(); ++it)
            settings_.setValue(it.key(), it.value());
        synchronizeInputBindings();
        applyControllerConfiguration(
            cupid::input::ControllerConfiguration::load(
                settings_.value("controllers/configuration").toByteArray()),
            {}, -1);
        if (!previousPreferences.contains("controllers/configuration"))
            settings_.remove("controllers/configuration");
    });
    // The full suite deliberately edits controls and firmware preferences. This
    // workflow starts with defined bindings and restores that caller's settings.
    for (const auto &key : settings_.allKeys())
        if (key.startsWith("input/") || key.startsWith("shortcuts/"))
            settings_.remove(key);
    const QString noBoot = QStringLiteral("");
    const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    const auto capture = [&](QWidget *widget, const QString &name) {
        return captureDirectory.isEmpty() ||
               (QDir().mkpath(captureDirectory) &&
                widget->grab().save(QDir(captureDirectory).filePath(name)));
    };
    const QDir fixtures(directory);
    const QString firstPath = fixtures.filePath("link-first.gb");
    const QString secondPath = fixtures.filePath("link-second.gbc");
    if (!writeLinkCartridge(firstPath, linkCartridge(0xa5, true)) ||
        !writeLinkCartridge(secondPath, linkCartridge(0x3c, false)))
        return smokeFailure(__FILE__, __LINE__);

    MainWindow first(true, false, audioCheck_, noBoot, CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
    MainWindow second(true, false, audioCheck_, noBoot, CUPID_GB_MODEL_CGB, uint64_t(1));
    first.timer_.stop();
    second.timer_.stop();
    first.show();
    second.show();
    if (!first.openRom(firstPath) || !second.openRom(secondPath))
        return smokeFailure(__FILE__, __LINE__);
    first.setPaused(true);
    second.setPaused(true);
    first.refreshLinkMenu();
    QAction *connectAction = nullptr;
    for (auto *action : first.linkMenu_->actions())
        if (action->toolTip() == second.romPath_)
            connectAction = action;
    if (!connectAction)
        return smokeFailure(__FILE__, __LINE__);
    connectAction->trigger();
    const auto cable = first.linkSession_;
    if (!cable || second.linkSession_ != cable || first.linkPartner() != &second ||
        second.linkPartner() != &first || first.linkOwner() != &first ||
        second.linkOwner() != &first || !first.paused_ || !second.paused_ ||
        first.disconnectAction_->isChecked() || second.disconnectAction_->isChecked())
        return smokeFailure(__FILE__, __LINE__);

    const uint64_t secondaryBefore = second.emulator_->gb.elapsed_cycles;
    second.tick();
    if (second.emulator_->gb.elapsed_cycles != secondaryBefore)
        return smokeFailure(__FILE__, __LINE__);
    second.setPaused(false);
    if (first.paused_ || second.paused_)
        return smokeFailure(__FILE__, __LINE__);
    for (unsigned frame = 0; frame < 3; ++frame)
        if (!first.runFrame())
            return smokeFailure(__FILE__, __LINE__);
    if (!linkTransferFinished(first.emulator_->gb, 0x3c) ||
        !linkTransferFinished(second.emulator_->gb, 0xa5))
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t firstCycles = first.emulator_->gb.elapsed_cycles;
    const uint64_t secondCycles = second.emulator_->gb.elapsed_cycles;
    if ((firstCycles > secondCycles ? firstCycles - secondCycles : secondCycles - firstCycles) > 64)
        return smokeFailure(__FILE__, __LINE__);
    if (!first.rewindStates_.empty() || !second.rewindStates_.empty())
        return smokeFailure(__FILE__, __LINE__);

    bool observedPartialPeer = false;
    for (unsigned quantum = 0; quantum < 8; ++quantum) {
        const std::array<MainWindow *, 2> windows{&first, &second};
        std::array<uint64_t, 2> cycles{}, submitted{};
        std::array<QImage, 2> current, previous, display;
        std::array<size_t, 2> history{};
        for (size_t side = 0; side < windows.size(); ++side) {
            const auto *window = windows[side];
            cycles[side] = window->emulator_->gb.elapsed_cycles;
            submitted[side] = window->presentedImages_;
            current[side] = window->currentDisplayFrame_;
            previous[side] = window->previousDisplayFrame_;
            display[side] = window->display_->image;
            history[side] = window->rewindStates_.size();
        }
        if (!first.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        for (size_t side = 0; side < windows.size(); ++side) {
            const auto *window = windows[side];
            if (window->emulator_->gb.elapsed_cycles == cycles[side] ||
                window->emulator_->gb.frame_ready)
                continue;
            observedPartialPeer = true;
            if (window->presentedImages_ != submitted[side] ||
                window->currentDisplayFrame_ != current[side] ||
                window->previousDisplayFrame_ != previous[side] ||
                window->display_->image != display[side] ||
                window->rewindStates_.size() != history[side])
                return smokeFailure(__FILE__, __LINE__);
        }
    }
    if (!observedPartialPeer || !capture(&first, "link-first-window.png") ||
        !capture(&second, "link-second-window.png"))
        return smokeFailure(__FILE__, __LINE__);

    QKeyEvent firstPress(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QKeyEvent secondPress(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
    QApplication::sendEvent(&first, &firstPress);
    if (first.physicalJoypad_ != 0xef || second.physicalJoypad_ != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(&second, &secondPress);
    if (first.physicalJoypad_ != 0xef || second.physicalJoypad_ != 0xdf)
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent firstRelease(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(&first, &firstRelease);
    if (first.physicalJoypad_ != 0xff || second.physicalJoypad_ != 0xdf)
        return smokeFailure(__FILE__, __LINE__);
    second.releaseKeys();

    const auto editLinkKeys = [&](bool conflict) {
        bool submitted = false, inspected = false, warned = false, timedOut = false;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer command;
        command.setInterval(1);
        connect(&command, &QTimer::timeout, &first, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (elapsed.elapsed() > 5000) {
                timedOut = true;
                if (dialog)
                    dialog->reject();
                if (auto *settings = first.findChild<QDialog *>("settings"))
                    settings->reject();
                return;
            }
            if (auto *warning = qobject_cast<QMessageBox *>(dialog)) {
                warned = true;
                warning->accept();
                return;
            }
            if (!dialog || dialog->objectName() != "settings")
                return;
            if (submitted) {
                if (warned)
                    dialog->reject();
                return;
            }
            submitted = true;
            auto *a = dialog->findChild<QKeySequenceEdit *>("input/linkPlayer2/4");
            auto *b = dialog->findChild<QKeySequenceEdit *>("input/linkPlayer2/5");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            inspected = a && b && buttons;
            if (!inspected) {
                dialog->reject();
                return;
            }
            a->setKeySequence(QKeySequence(conflict ? Qt::Key_X : Qt::Key_J));
            b->setKeySequence(QKeySequence(Qt::Key_K));
            if (!conflict) {
                if (auto *scroll = dialog->findChild<QScrollArea *>())
                    scroll->ensureWidgetVisible(a);
                if (!capture(dialog, "link-settings.png")) {
                    inspected = false;
                    dialog->reject();
                    return;
                }
            }
            auto *ok = buttons->button(QDialogButtonBox::Ok);
            QTimer::singleShot(0, ok, [ok] { ok->click(); });
        });
        command.start();
        first.configure();
        command.stop();
        return inspected && !timedOut && warned == conflict;
    };
    if (!editLinkKeys(true) || first.linkedKeys_[4] || second.linkedKeys_[4] ||
        !editLinkKeys(false) || first.linkedKeys_[4] != Qt::Key_J ||
        second.linkedKeys_[4] != Qt::Key_J || first.linkedKeys_[5] != Qt::Key_K ||
        second.linkedKeys_[5] != Qt::Key_K ||
        first.settings_.value("input/linkPlayer2/4").toInt() != Qt::Key_J)
        return smokeFailure(__FILE__, __LINE__);

    QKeyEvent partnerPress(QEvent::KeyPress, Qt::Key_J, Qt::NoModifier);
    QKeyEvent partnerRelease(QEvent::KeyRelease, Qt::Key_J, Qt::NoModifier);
    QApplication::sendEvent(&first, &firstPress);
    QApplication::sendEvent(&first, &partnerPress);
    if (first.emulator_->gb.joypad != 0xef || second.emulator_->gb.joypad != 0xef)
        return smokeFailure(__FILE__, __LINE__);
    second.receiveControllerOutput(cupid::input::ControllerOutput{});
    if (first.emulator_->gb.joypad != 0xef || second.emulator_->gb.joypad != 0xef)
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(&second, &secondPress);
    QApplication::sendEvent(&first, &partnerRelease);
    if (first.emulator_->gb.joypad != 0xef || second.emulator_->gb.joypad != 0xdf)
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(&first, &partnerPress);
    QEvent focusLost(QEvent::WindowDeactivate);
    QApplication::sendEvent(&first, &focusLost);
    if (first.emulator_->gb.joypad != 0xff || second.emulator_->gb.joypad != 0xdf)
        return smokeFailure(__FILE__, __LINE__);
    second.releaseKeys();
    QApplication::sendEvent(&second, &partnerPress);
    if (first.emulator_->gb.joypad != 0xef || second.emulator_->gb.joypad != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    second.releaseKeys();

    {
        using namespace cupid::input;
        LinkVirtualPad firstPad, secondPad;
        if (!firstPad.attach(0x91) || !secondPad.attach(0x92))
            return smokeFailure(__FILE__, __LINE__);
        DeviceState a, b, peerA, peerB;
        if (!waitLinkCondition([&] {
                return first.controllers_->backend().state(firstPad.id, &a) &&
                       first.controllers_->backend().state(secondPad.id, &b) &&
                       second.controllers_->backend().state(firstPad.id, &peerA) &&
                       second.controllers_->backend().state(secondPad.id, &peerB);
            }))
            return smokeFailure(__FILE__, __LINE__);
        ControllerConfiguration configuration;
        for (const auto &device : first.controllers_->backend().devices()) {
            auto profile = ControllerManager::defaults(device.info);
            profile.enabled =
                device.info.instance == firstPad.id || device.info.instance == secondPad.id;
            profile.player = 0;
            configuration.profiles[device.info.persistentKey] = profile;
        }
        first.applyControllerConfiguration(configuration, {}, -1);
        bool controllerSettingsChecked = false, controllerSettingsTimedOut = false;
        QElapsedTimer controllerDeadline;
        controllerDeadline.start();
        QTimer editController;
        editController.setInterval(1);
        connect(&editController, &QTimer::timeout, &first, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (controllerDeadline.elapsed() > 5000) {
                controllerSettingsTimedOut = true;
                if (dialog)
                    dialog->reject();
                return;
            }
            if (!dialog || dialog->objectName() != "controllerSettings")
                return;
            auto *device = dialog->findChild<QComboBox *>("controllerDevice");
            auto *player = dialog->findChild<QComboBox *>("controllerPlayer");
            auto *buttons = dialog->findChild<QDialogButtonBox *>("controllerDialogButtons");
            const int index = device ? device->findData(int(secondPad.id)) : -1;
            if (!player || !buttons || index < 0) {
                dialog->reject();
                return;
            }
            device->setCurrentIndex(index);
            player->setCurrentIndex(player->findData(1));
            controllerSettingsChecked =
                player->currentData().toInt() == 1 && capture(dialog, "link-controllers.png");
            editController.stop();
            buttons
                ->button(controllerSettingsChecked ? QDialogButtonBox::Ok
                                                   : QDialogButtonBox::Cancel)
                ->click();
        });
        editController.start();
        first.configureControllers();
        editController.stop();
        if (!controllerSettingsChecked || controllerSettingsTimedOut ||
            first.controllers_->profile(b.info).player != 1 ||
            second.controllers_->profile(b.info).player != 1 ||
            first.controllers_->playerOverrides().value(secondPad.id, -1) != 1 ||
            second.controllers_->playerOverrides().value(secondPad.id, -1) != 1)
            return smokeFailure(__FILE__, __LINE__);
        QScopedValueRollback<bool> firstTestInput(first.controllerSmokeActive_, true);
        QScopedValueRollback<bool> secondTestInput(second.controllerSmokeActive_, true);
        const auto focusForControllers = [&](MainWindow &owner) {
            owner.raise();
            owner.activateWindow();
            QElapsedTimer settled;
            // Native dialog teardown can deliver delayed focus changes. Require
            // the intended document to stay active across event-loop turns.
            if (!waitLinkCondition([&] {
                    if (QApplication::applicationState() != Qt::ApplicationActive ||
                        QApplication::activeWindow() != &owner ||
                        QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
                        settled.invalidate();
                        return false;
                    }
                    if (!settled.isValid()) {
                        settled.start();
                        return false;
                    }
                    return settled.elapsed() >= 20;
                })) {
                const auto *active = QApplication::activeWindow();
                qCritical(
                    "Linked controller activation failed: targetFirst=%d activeFirst=%d "
                    "activeSecond=%d activeNull=%d appState=%d modal=%s popup=%s visible=%d/%d",
                    int(&owner == &first), int(active == &first), int(active == &second),
                    int(!active), int(QApplication::applicationState()),
                    QApplication::activeModalWidget()
                        ? QApplication::activeModalWidget()->metaObject()->className()
                        : "none",
                    QApplication::activePopupWidget()
                        ? QApplication::activePopupWidget()->metaObject()->className()
                        : "none",
                    int(first.isVisible()), int(second.isVisible()));
                return false;
            }
            first.refreshControllerGate();
            second.refreshControllerGate();
            const bool routed = first.controllers_->deliveryEnabled() == (&owner == &first) &&
                                second.controllers_->deliveryEnabled() == (&owner == &second);
            if (!routed)
                qCritical("Linked controller ownership gate failed: targetFirst=%d delivery=%d/%d "
                          "operation=%d/%d testInput=%d/%d trace=%u/%u",
                          int(&owner == &first), int(first.controllers_->deliveryEnabled()),
                          int(second.controllers_->deliveryEnabled()), int(first.stateOperation_),
                          int(second.stateOperation_), int(first.controllerSmokeActive_),
                          int(second.controllerSmokeActive_), unsigned(first.inputTrace_.size()),
                          unsigned(second.inputTrace_.size()));
            return routed;
        };
        if (!focusForControllers(first))
            return smokeFailure(__FILE__, __LINE__);
        SDL_JoystickSetVirtualButton(firstPad.handle, 0, 1);
        SDL_JoystickSetVirtualButton(secondPad.handle, 1, 1);
        if (!waitLinkCondition([&] {
                return first.controllers_->output().buttons[0] == 0xef &&
                       first.controllers_->output().buttons[1] == 0xdf &&
                       first.emulator_->gb.joypad == 0xef && second.emulator_->gb.joypad == 0xdf;
            })) {
            qCritical(
                "Linked controller routing failed: activeFirst=%d activeSecond=%d "
                "appState=%d paused=%d/%d delivery=%d/%d allowed=%d/%d timers=%d/%d "
                "manager=%02x/%02x peerManager=%02x/%02x cached=%02x/%02x "
                "peerCached=%02x/%02x guest=%02x/%02x",
                int(QApplication::activeWindow() == &first),
                int(QApplication::activeWindow() == &second), int(QApplication::applicationState()),
                int(first.paused_), int(second.paused_), int(first.controllers_->deliveryEnabled()),
                int(second.controllers_->deliveryEnabled()),
                int(first.controllers_->backend().inputAllowed()),
                int(second.controllers_->backend().inputAllowed()), int(first.timer_.isActive()),
                int(second.timer_.isActive()), unsigned(first.controllers_->output().buttons[0]),
                unsigned(first.controllers_->output().buttons[1]),
                unsigned(second.controllers_->output().buttons[0]),
                unsigned(second.controllers_->output().buttons[1]),
                unsigned(first.controllerOutput_.buttons[0]),
                unsigned(first.controllerOutput_.buttons[1]),
                unsigned(second.controllerOutput_.buttons[0]),
                unsigned(second.controllerOutput_.buttons[1]), unsigned(first.emulator_->gb.joypad),
                unsigned(second.emulator_->gb.joypad));
            return smokeFailure(__FILE__, __LINE__);
        }
        first.rumblePublishedAmplitudes_ = {0.25};
        second.rumblePublishedAmplitudes_ = {0.75};
        first.finishControllerRumble();
        second.finishControllerRumble();
        if (!firstPad.amplitude || secondPad.amplitude <= firstPad.amplitude)
            return smokeFailure(__FILE__, __LINE__);
        const uint16_t peerMotor = secondPad.amplitude;
        first.resetControllerRumble();
        if (firstPad.amplitude || secondPad.amplitude != peerMotor)
            return smokeFailure(__FILE__, __LINE__);
        second.resetControllerRumble();
        if (firstPad.amplitude || secondPad.amplitude)
            return smokeFailure(__FILE__, __LINE__);
        second.receiveControllerOutput(ControllerOutput{});
        if (second.emulator_->gb.joypad != 0xdf || first.emulator_->gb.joypad != 0xef)
            return smokeFailure(__FILE__, __LINE__);
        first.controllers_->setDeliveryEnabled(false);
        if (first.emulator_->gb.joypad != 0xff || second.emulator_->gb.joypad != 0xff)
            return smokeFailure(__FILE__, __LINE__);
        if (!focusForControllers(second))
            return smokeFailure(__FILE__, __LINE__);
        if (!waitLinkCondition([&] {
                return first.emulator_->gb.joypad == 0xdf && second.emulator_->gb.joypad == 0xef;
            }))
            return smokeFailure(__FILE__, __LINE__);
        first.rumblePublishedAmplitudes_ = {0.75};
        second.rumblePublishedAmplitudes_ = {0.25};
        first.finishControllerRumble();
        second.finishControllerRumble();
        if (!firstPad.amplitude || secondPad.amplitude <= firstPad.amplitude)
            return smokeFailure(__FILE__, __LINE__);
        second.setPaused(true);
        if (firstPad.amplitude || secondPad.amplitude)
            return smokeFailure(__FILE__, __LINE__);
        second.setPaused(false);
        SDL_JoystickSetVirtualButton(firstPad.handle, 0, 0);
        SDL_JoystickSetVirtualButton(secondPad.handle, 1, 0);
        if (!waitLinkCondition([&] {
                return first.emulator_->gb.joypad == 0xff && second.emulator_->gb.joypad == 0xff;
            }))
            return smokeFailure(__FILE__, __LINE__);
        first.controllers_->setDeliveryEnabled(false);
        second.controllers_->setDeliveryEnabled(false);
        // Every manager receives physical edges, but only the target's window
        // may turn an edge into a menu or modal-dialog navigation key.
        const auto firstNavigation = first.controllers_->navigationPressed;
        const auto secondNavigation = second.controllers_->navigationPressed;
        std::array<unsigned, 2> navigationEvents{};
        first.controllers_->navigationPressed = [&](const QString &action, SDL_JoystickID id) {
            if (action == "down" && id == firstPad.id)
                ++navigationEvents[0];
            firstNavigation(action, id);
        };
        second.controllers_->navigationPressed = [&](const QString &action, SDL_JoystickID id) {
            if (action == "down" && id == firstPad.id)
                ++navigationEvents[1];
            secondNavigation(action, id);
        };
        const auto restoreNavigation = qScopeGuard([&] {
            first.controllers_->navigationPressed = firstNavigation;
            second.controllers_->navigationPressed = secondNavigation;
        });
        const auto pollNavigation = [&](int16_t axis) {
            if (SDL_JoystickSetVirtualAxis(firstPad.handle, 1, axis) != 0)
                return false;
            first.controllers_->backend().pollNow();
            second.controllers_->backend().pollNow();
            return true;
        };
        const auto drainNavigation = [] {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            QApplication::processEvents();
        };
        for (auto *owner : {&first, &second}) {
            owner->activateWindow();
            QMenu menu(owner);
            QMenu nested("Linked navigation", &menu);
            menu.addMenu(&nested);
            auto *firstItem = nested.addAction("First");
            auto *secondItem = nested.addAction("Second");
            nested.addAction("Third");
            LinkNavigationProbe menuProbe;
            nested.installEventFilter(&menuProbe);
            nested.popup(owner->mapToGlobal(QPoint(20, 20)));
            if (!waitLinkCondition([&] {
                    return QApplication::activePopupWidget() == &nested &&
                           QApplication::applicationState() == Qt::ApplicationActive;
                }))
                return smokeFailure(__FILE__, __LINE__);
            nested.setActiveAction(firstItem);
            first.controllers_->setFocused(true);
            second.controllers_->setFocused(true);
            auto previousNavigation = navigationEvents;
            if (!pollNavigation(24000))
                return smokeFailure(__FILE__, __LINE__);
            drainNavigation();
            if (navigationEvents[0] != previousNavigation[0] + 1 ||
                navigationEvents[1] != previousNavigation[1] + 1 || menuProbe.downPresses != 1 ||
                nested.activeAction() != secondItem || !pollNavigation(0))
                return smokeFailure(__FILE__, __LINE__);
            nested.hide();
            drainNavigation();

            QDialog dialog(owner);
            dialog.setModal(true);
            auto *layout = new QVBoxLayout(&dialog);
            auto *choices = new QComboBox(&dialog);
            choices->addItems({"First", "Second", "Third", "Fourth"});
            layout->addWidget(choices);
            LinkNavigationProbe dialogProbe;
            choices->installEventFilter(&dialogProbe);
            dialog.show();
            dialog.activateWindow();
            choices->setFocus();
            if (!waitLinkCondition([&] {
                    return QApplication::activeModalWidget() == &dialog &&
                           QApplication::applicationState() == Qt::ApplicationActive &&
                           choices->hasFocus();
                }))
                return smokeFailure(__FILE__, __LINE__);
            previousNavigation = navigationEvents;
            if (!pollNavigation(24000))
                return smokeFailure(__FILE__, __LINE__);
            drainNavigation();
            if (navigationEvents[0] != previousNavigation[0] + 1 ||
                navigationEvents[1] != previousNavigation[1] + 1 || dialogProbe.downPresses != 1 ||
                choices->currentIndex() != 1 || !pollNavigation(0))
                return smokeFailure(__FILE__, __LINE__);
            // Binding capture owns its edge even if capture starts after the
            // navigation key was queued by the physical controller callback.
            dialog.setProperty("controllerCaptureActive", true);
            if (!pollNavigation(24000))
                return smokeFailure(__FILE__, __LINE__);
            drainNavigation();
            if (dialogProbe.downPresses != 1 || choices->currentIndex() != 1 || !pollNavigation(0))
                return smokeFailure(__FILE__, __LINE__);
            dialog.setProperty("controllerCaptureActive", false);
            if (!pollNavigation(24000))
                return smokeFailure(__FILE__, __LINE__);
            dialog.setProperty("controllerCaptureConsumed", true);
            drainNavigation();
            if (dialogProbe.downPresses != 1 || choices->currentIndex() != 1 || !pollNavigation(0))
                return smokeFailure(__FILE__, __LINE__);
            dialog.hide();
            drainNavigation();
        }
    }

    first.activateWindow();
    QApplication::sendEvent(&first, &partnerPress);
    if (second.emulator_->gb.joypad != 0xef)
        return smokeFailure(__FILE__, __LINE__);
    auto *fileMenu = first.menuBar()->actions().front()->menu();
    fileMenu->popup(first.mapToGlobal(QPoint(20, 20)));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    const bool releasedForMenu = first.menuDepth_ && first.emulator_->gb.joypad == 0xff &&
                                 second.emulator_->gb.joypad == 0xff;
    fileMenu->hide();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    if (!releasedForMenu || first.menuDepth_ || first.menuInputSuspended_ ||
        second.emulator_->gb.joypad != 0xff)
        return smokeFailure(__FILE__, __LINE__);

    first.keyboardSpecialHeld_[1] = true;
    first.reconcileControllerInput();
    if (first.rewind_ || first.rewindFrame() || first.linkSession_ != cable)
        return smokeFailure(__FILE__, __LINE__);
    first.keyboardSpecialHeld_[1] = false;
    first.reconcileControllerInput();
    second.setTurboCap(8);
    if (first.playbackTurboCap() != 8 || second.playbackTurboCap() != 8)
        return smokeFailure(__FILE__, __LINE__);
    second.keyboardSpecialHeld_[0] = true;
    second.reconcileControllerInput();
    if (!first.playbackTurbo() || !second.playbackTurbo())
        return smokeFailure(__FILE__, __LINE__);
    second.releaseKeys();

    second.setPaused(true);
    const uint64_t pausedFirst = first.emulator_->gb.elapsed_cycles;
    const uint64_t pausedSecond = second.emulator_->gb.elapsed_cycles;
    first.tick();
    second.tick();
    if (!first.paused_ || !second.paused_ || first.emulator_->gb.elapsed_cycles != pausedFirst ||
        second.emulator_->gb.elapsed_cycles != pausedSecond)
        return smokeFailure(__FILE__, __LINE__);
    second.setPaused(false);
    first.refreshTimerInterval();
    second.refreshTimerInterval();
    if (first.timer_.interval() != 1 || second.timer_.interval() != 16)
        return smokeFailure(__FILE__, __LINE__);

    cupid_gb_write_u8(&first.emulator_->gb, 0x0000, 0x0a);
    cupid_gb_write_u8(&second.emulator_->gb, 0x0000, 0x0a);
    cupid_gb_write_u8(&first.emulator_->gb, 0xa123, 0x11);
    cupid_gb_write_u8(&second.emulator_->gb, 0xa123, 0x22);
    if (!first.flushBattery(true) || !second.flushBattery(true) ||
        first.savePath_ == second.savePath_)
        return smokeFailure(__FILE__, __LINE__);
    QFile firstBattery(first.savePath_);
    QFile secondBattery(second.savePath_);
    if (!firstBattery.open(QIODevice::ReadOnly) || !secondBattery.open(QIODevice::ReadOnly))
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray firstBatteryBytes = firstBattery.readAll();
    const QByteArray secondBatteryBytes = secondBattery.readAll();
    if (firstBatteryBytes.size() <= 0x123 || secondBatteryBytes.size() <= 0x123 ||
        uint8_t(firstBatteryBytes.at(0x123)) != 0x11 ||
        uint8_t(secondBatteryBytes.at(0x123)) != 0x22)
        return smokeFailure(__FILE__, __LINE__);
    firstBattery.close();
    secondBattery.close();

    const auto firstState = first.captureState(false);
    const auto secondState = second.captureState(false);
    if (firstState.isEmpty() || secondState.isEmpty() || !first.saveSlot(4))
        return smokeFailure(__FILE__, __LINE__);
    const uint8_t savedWorkByte = cupid_gb_debug_peek(&first.emulator_->gb, 0xc010);
    cupid_gb_debug_poke(&first.emulator_->gb, 0xc010, 0x51);
    cupid_gb_debug_poke(&second.emulator_->gb, 0xc010, 0x72);
    const uint64_t secondBeforeLoad = second.emulator_->gb.elapsed_cycles;
    if (!first.loadSlot(4) || first.linkSession_ != cable || second.linkSession_ != cable ||
        cupid_gb_debug_peek(&first.emulator_->gb, 0xc010) != savedWorkByte ||
        cupid_gb_debug_peek(&second.emulator_->gb, 0xc010) != 0x72 ||
        second.emulator_->gb.elapsed_cycles != secondBeforeLoad)
        return smokeFailure(__FILE__, __LINE__);
    const auto beforeRejectedState = first.captureState(false);
    if (first.restoreState(QByteArray("not a state"), false) || first.linkSession_ != cable ||
        first.captureState(false) != beforeRejectedState)
        return smokeFailure(__FILE__, __LINE__);

    auto *firstCore = first.emulator_.get();
    if (first.openRom(fixtures.filePath("missing-link-cartridge.gb")) ||
        first.emulator_.get() != firstCore || first.linkSession_ != cable ||
        second.linkSession_ != cable)
        return smokeFailure(__FILE__, __LINE__);
    first.failed_ = false;
    if (second.openRom(firstPath) || first.linkSession_ != cable || second.linkSession_ != cable ||
        second.romPath_ != QFileInfo(secondPath).absoluteFilePath())
        return smokeFailure(__FILE__, __LINE__);
    second.failed_ = false;

    for (const auto workflow :
         {ResetWorkflow::Normal, ResetWorkflow::Quick, ResetWorkflow::Reload}) {
        auto *oldCore = first.emulator_.get();
        auto *peerCore = second.emulator_.get();
        const uint64_t peerCycles = peerCore->gb.elapsed_cycles;
        if (!first.resetCartridge(workflow) || first.emulator_.get() == oldCore ||
            second.emulator_.get() != peerCore || first.linkSession_ != cable ||
            second.linkSession_ != cable || peerCore->gb.elapsed_cycles != peerCycles ||
            cupid_gb_debug_peek(&peerCore->gb, 0xc010) != 0x72 || !first.runFrame())
            return smokeFailure(__FILE__, __LINE__);
    }
    if (!first.resetConsole(CUPID_GB_RESET_QUICK) || first.linkSession_ != cable ||
        !first.runFrame())
        return smokeFailure(__FILE__, __LINE__);

    if (!second.restart())
        return smokeFailure(__FILE__, __LINE__);
    second.debuggerCommand("break $0150");
    second.setPaused(false);
    if (!first.runFrame() || !first.paused_ || !second.paused_ || !second.debugger_ ||
        second.debugger_->last_stop != CUPID_GB_DEBUG_STOP_BREAKPOINT)
        return smokeFailure(__FILE__, __LINE__);
    second.debuggerCommand("step");
    second.debuggerTimer_.stop();
    for (unsigned attempt = 0; attempt < 8 && second.emulator_->gb.cpu.pc == 0x150; ++attempt)
        second.linkedDebuggerSlice();
    if (second.emulator_->gb.cpu.pc != 0x151 || !first.paused_ || !second.paused_ ||
        first.linkSession_ != cable || second.debuggerBackstep())
        return smokeFailure(__FILE__, __LINE__);
    second.debuggerCommand("delete all");
    if (second.debuggerWindow_)
        second.debuggerWindow_->hide();
    second.setPaused(false);

    if (audioCheck_) {
        first.clearAudio();
        second.clearAudio();
        const uint64_t firstFrames = first.generatedAudioFrames_;
        const uint64_t secondFrames = second.generatedAudioFrames_;
        first.timer_.start();
        second.timer_.start();
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < 1500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        first.timer_.stop();
        second.timer_.stop();
        if (!first.audio_ || !second.audio_ || !first.audioDevice_ || !second.audioDevice_ ||
            first.audio_->state() != QAudio::ActiveState ||
            second.audio_->state() != QAudio::ActiveState ||
            first.generatedAudioFrames_ <= firstFrames ||
            second.generatedAudioFrames_ <= secondFrames || first.audioErrorEvents_ ||
            second.audioErrorEvents_ || first.paused_ || second.paused_)
            return smokeFailure(__FILE__, __LINE__);
        qInfo("Linked native audio: first=%llu second=%llu generated frames",
              static_cast<unsigned long long>(first.generatedAudioFrames_ - firstFrames),
              static_cast<unsigned long long>(second.generatedAudioFrames_ - secondFrames));
    }

    const bool launchedFirst = first.outgoingLinkBit_;
    second.setPrinterConnected(true);
    if (first.linkSession_ || second.linkSession_ || !second.printer_ ||
        !first.disconnectAction_->isChecked())
        return smokeFailure(__FILE__, __LINE__);
    second.setPrinterConnected(false);
    if (!first.connectLink(&second) || first.outgoingLinkBit_ != launchedFirst)
        return smokeFailure(__FILE__, __LINE__);
    const auto reconnected = first.linkSession_;
    const QString savedBattery = first.savePath_;
    first.savePath_ = directory;
    if (first.closeRom() || first.linkSession_ != reconnected ||
        second.linkSession_ != reconnected || !first.emulator_)
        return smokeFailure(__FILE__, __LINE__);
    first.savePath_ = savedBattery;
    first.failed_ = false;
    if (!first.closeRom() || first.linkSession_ || second.linkSession_ || first.emulator_ ||
        !second.emulator_)
        return smokeFailure(__FILE__, __LINE__);
    second.refreshLinkMenu();
    for (auto *action : second.linkMenu_->actions())
        if (action->isEnabled())
            return smokeFailure(__FILE__, __LINE__);

    QPointer<MainWindow> third = second.newWindow();
    third->timer_.stop();
    if (!third->openRom(firstPath) || !second.connectLink(third))
        return smokeFailure(__FILE__, __LINE__);
    third->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (third || second.linkSession_ || !second.emulator_)
        return smokeFailure(__FILE__, __LINE__);
    if (!second.closeRom())
        return smokeFailure(__FILE__, __LINE__);

    {
        using namespace cupid::input;
        MainWindow backgroundFirst(true, false, false, noBoot, CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
        MainWindow backgroundSecond(true, false, false, noBoot, CUPID_GB_MODEL_CGB, uint64_t(1));
        backgroundFirst.timer_.stop();
        backgroundSecond.timer_.stop();
        backgroundFirst.controllerSmokeActive_ = backgroundSecond.controllerSmokeActive_ = true;
        backgroundFirst.show();
        backgroundSecond.show();
        if (!backgroundFirst.openRom(firstPath) || !backgroundSecond.openRom(secondPath) ||
            !backgroundFirst.connectLink(&backgroundSecond))
            return smokeFailure(__FILE__, __LINE__);
        LinkVirtualPad localPad, partnerPad;
        if (!localPad.attach(0x93) || !partnerPad.attach(0x94))
            return smokeFailure(__FILE__, __LINE__);
        DeviceState local, partner, peerLocal, peerPartner;
        if (!waitLinkCondition([&] {
                return backgroundFirst.controllers_->backend().state(localPad.id, &local) &&
                       backgroundFirst.controllers_->backend().state(partnerPad.id, &partner) &&
                       backgroundSecond.controllers_->backend().state(localPad.id, &peerLocal) &&
                       backgroundSecond.controllers_->backend().state(partnerPad.id, &peerPartner);
            }))
            return smokeFailure(__FILE__, __LINE__);
        ControllerConfiguration configuration;
        configuration.background = true;
        for (const auto &device : backgroundFirst.controllers_->backend().devices()) {
            auto profile = ControllerManager::defaults(device.info);
            profile.enabled =
                device.info.instance == localPad.id || device.info.instance == partnerPad.id;
            profile.player = device.info.instance == partnerPad.id ? 1 : 0;
            configuration.profiles[device.info.persistentKey] = profile;
        }
        backgroundFirst.applyControllerConfiguration(configuration, {}, -1);
        const auto leaveFocusedWindow = [&](MainWindow &focused) {
            QApplication::setActiveWindow(&focused);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            focused.refreshControllerGate();
            QApplication::setActiveWindow(nullptr);
            backgroundFirst.refreshControllerGate();
            backgroundSecond.refreshControllerGate();
            // The platform-independent test drives the inactive delivery gate
            // directly; background-enabled physical input must still be routed.
            backgroundFirst.controllers_->setFocused(false);
            backgroundSecond.controllers_->setFocused(false);
            return QApplication::activeWindow() == nullptr;
        };
        if (!leaveFocusedWindow(backgroundFirst) ||
            !backgroundFirst.controllers_->deliveryEnabled() ||
            backgroundSecond.controllers_->deliveryEnabled())
            return smokeFailure(__FILE__, __LINE__);
        SDL_JoystickSetVirtualButton(localPad.handle, 0, 1);
        SDL_JoystickSetVirtualButton(partnerPad.handle, 1, 1);
        if (!waitLinkCondition([&] {
                return backgroundFirst.emulator_->gb.joypad == 0xef &&
                       backgroundSecond.emulator_->gb.joypad == 0xdf;
            }))
            return smokeFailure(__FILE__, __LINE__);
        if (!leaveFocusedWindow(backgroundSecond) ||
            backgroundFirst.controllers_->deliveryEnabled() ||
            !backgroundSecond.controllers_->deliveryEnabled() || !waitLinkCondition([&] {
                return backgroundFirst.emulator_->gb.joypad == 0xdf &&
                       backgroundSecond.emulator_->gb.joypad == 0xef;
            }))
            return smokeFailure(__FILE__, __LINE__);
        if (!backgroundSecond.close())
            return smokeFailure(__FILE__, __LINE__);
        QApplication::setActiveWindow(nullptr);
        backgroundFirst.refreshControllerGate();
        backgroundFirst.controllers_->setFocused(false);
        if (backgroundFirst.linkSession_ || backgroundSecond.emulator_ ||
            !backgroundFirst.controllers_->deliveryEnabled() ||
            !waitLinkCondition([&] { return backgroundFirst.emulator_->gb.joypad == 0xcf; }))
            return smokeFailure(__FILE__, __LINE__);
        SDL_JoystickSetVirtualButton(localPad.handle, 0, 0);
        SDL_JoystickSetVirtualButton(partnerPad.handle, 1, 0);
        if (!waitLinkCondition([&] { return backgroundFirst.emulator_->gb.joypad == 0xff; }) ||
            !backgroundFirst.close())
            return smokeFailure(__FILE__, __LINE__);
    }

    for (const bool debugger : {false, true}) {
        MainWindow recordingOwner(true, false, false, noBoot, CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
        MainWindow recordingPeer(true, false, false, noBoot, CUPID_GB_MODEL_CGB, uint64_t(1));
        recordingOwner.timer_.stop();
        recordingPeer.timer_.stop();
        const QString ownerWave =
            fixtures.filePath(debugger ? "link-debug-limit.wav" : "link-play-limit.wav");
        const QString peerWave =
            fixtures.filePath(debugger ? "link-debug-prefix.wav" : "link-play-prefix.wav");
        if (!recordingOwner.openRom(firstPath) || !recordingPeer.openRom(secondPath) ||
            !recordingOwner.connectLink(&recordingPeer) || !recordingOwner.runFrame() ||
            !recordingOwner.startRecording(ownerWave) || !recordingPeer.startRecording(peerWave))
            return smokeFailure(__FILE__, __LINE__);
        if (debugger) {
            recordingOwner.debuggerCommand("step");
            recordingOwner.debuggerTimer_.stop();
        }
        // A peer-only frame boundary may arrive before another host sample is
        // produced. Queue real APU output so both paths exercise a nonempty prefix.
        cupid_gb_tick_apu(&recordingOwner.emulator_->gb, 4096);
        cupid_gb_tick_apu(&recordingPeer.emulator_->gb, 4096);
        if (!cupid_gb_apu_pending(&recordingOwner.emulator_->gb).handheld_frames ||
            !cupid_gb_apu_pending(&recordingPeer.emulator_->gb).handheld_frames)
            return smokeFailure(__FILE__, __LINE__);
        WaveRecorderTestAccess::reachCapacity(*recordingOwner.recorder_);
        const uint64_t peerGenerated = recordingPeer.generatedAudioFrames_;
        const uint64_t peerRecorded = recordingPeer.recorder_->framesWritten();
        bool errorSeen = false, prefixCommitted = false, peerClosed = false, timedOut = false;
        uint64_t committedFrames = 0;
        QElapsedTimer deadline;
        deadline.start();
        QTimer callback;
        callback.setInterval(1);
        connect(&callback, &QTimer::timeout, &recordingOwner, [&] {
            auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (deadline.elapsed() > 5000) {
                timedOut = true;
                if (error)
                    error->reject();
                return;
            }
            if (!error)
                return;
            errorSeen = error->text().contains("RIFF size limit");
            committedFrames = recordingPeer.recorder_->framesWritten();
            prefixCommitted = committedFrames > peerRecorded &&
                              committedFrames - peerRecorded ==
                                  recordingPeer.generatedAudioFrames_ - peerGenerated;
            peerClosed = recordingPeer.closeRom();
            error->accept();
        });
        recordingOwner.isolated_ = false;
        callback.start();
        if (debugger)
            recordingOwner.linkedDebuggerSlice();
        else
            recordingOwner.runFrame();
        callback.stop();
        recordingOwner.isolated_ = true;
        recordingOwner.failed_ = false;
        if (!errorSeen || !prefixCommitted || !peerClosed || timedOut ||
            recordingOwner.linkSession_ || recordingPeer.emulator_ ||
            QFileInfo(peerWave).size() != qint64(44 + committedFrames * 4) ||
            !recordingOwner.closeRom())
            return smokeFailure(__FILE__, __LINE__);
        // The artificial boundary has no four-GiB payload; its header is only a
        // failure fixture. Keep the correctly recorded peer output for inspection.
        QFile::remove(ownerWave);
    }

    // An unlinked recorder reports immediately. Its error dialog can delete the
    // same window, so both playback and debugger callers must unwind safely.
    const QString recordingCase = qEnvironmentVariable("CUPIDGB_RECORDING_LIFETIME_CASE");
    bool selectedRecordingCase = false;
    for (const bool debugger : {false, true}) {
        const QString name = debugger ? QStringLiteral("debugger") : QStringLiteral("playback");
        if (!recordingCase.isEmpty() && recordingCase != name)
            continue;
        selectedRecordingCase = true;
        QPointer<MainWindow> victim =
            new MainWindow(true, true, false, noBoot, CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
        const auto cleanup = qScopeGuard([&] {
            if (victim)
                delete victim.data();
        });
        victim->timer_.stop();
        const QString wave = fixtures.filePath("recording-lifetime-" + name + ".wav");
        if (!victim->openRom(firstPath) || !victim->startRecording(wave))
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_tick_apu(&victim->emulator_->gb, 4096);
        if (!cupid_gb_apu_pending(&victim->emulator_->gb).handheld_frames)
            return smokeFailure(__FILE__, __LINE__);
        WaveRecorderTestAccess::reachCapacity(*victim->recorder_);
        victim->isolated_ = false;
        victim->automated_ = false;
        bool errorSeen = false, recordingStopped = false, timedOut = false;
        QElapsedTimer deadline;
        deadline.start();
        QTimer callback;
        callback.setInterval(1);
        connect(&callback, &QTimer::timeout, &callback, [&] {
            auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (deadline.elapsed() > 2000) {
                timedOut = true;
                callback.stop();
                if (error)
                    error->reject();
                return;
            }
            if (!error || error->parentWidget() != victim)
                return;
            errorSeen =
                error->icon() == QMessageBox::Critical && error->text().contains("RIFF size limit");
            recordingStopped = victim->recorder_ && !victim->recorder_->active();
            callback.stop();
            delete victim.data();
        });
        callback.start();
        auto *window = victim.data();
        qInfo("CupidGB recording lifetime %s starting", qPrintable(name));
        if (debugger)
            window->drainDebuggerAudio();
        else
            window->runFrame();
        if (victim || !errorSeen || !recordingStopped || timedOut)
            return smokeFailure(__FILE__, __LINE__);
        qInfo("CupidGB recording lifetime %s passed", qPrintable(name));
    }
    if (!selectedRecordingCase)
        return smokeFailure(__FILE__, __LINE__);

    qInfo("Linked frontend checks passed: guest transfer, two windows, independent input, "
          "player-two bindings/controllers, paired pause, state/reset retention, debugger, "
          "storage, recording failures, printer and close lifecycle.");
    return true;
}

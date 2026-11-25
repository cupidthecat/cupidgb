/**
 * @file    main_window_input.cpp
 * @brief   Keyboard and controller input routing, focus, pause, and window events.
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

namespace {
QList<QPointer<MainWindow>> inputWindowOrder;

void rememberInputWindow(MainWindow *window, bool focused) {
    inputWindowOrder.removeAll(nullptr);
    if (focused) {
        inputWindowOrder.removeAll(window);
        inputWindowOrder.prepend(window);
    } else if (!inputWindowOrder.contains(window)) {
        inputWindowOrder.append(window);
    }
}

bool objectBelongsTo(const QObject *object, const QObject *owner) {
    // Popup menus are separate windows. QWidget ancestry intentionally stops
    // at that boundary, while QObject ownership retains their parent menu/window.
    for (auto *ancestor = object; ancestor; ancestor = ancestor->parent())
        if (ancestor == owner)
            return true;
    return false;
}
} // namespace

void MainWindow::setPaused(bool value, bool retainInput) {
    if (linkSession_) {
        setLinkPaused(value, retainInput);
        return;
    }
    setPausedLocal(value, retainInput);
}

void MainWindow::setPausedLocal(bool value, bool retainInput) {
    const QPointer<MainWindow> self(this);
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
    if (retainInput)
        reconcileControllerInput(false);
    else
        releaseKeys(false);
    {
        const QSignalBlocker blocker(pauseAction_);
        pauseAction_->setChecked(value);
    }
    resetPacing();
    clearAudio();
    if (!self)
        return;
    if (value)
        flushBattery();
    if (!self)
        return;
    refreshDebugger();
    refreshMusicControls();
}

void MainWindow::releaseKeys(bool resetAudio) {
    releaseWorkBoyKeys();
    if (!motionFocusActive_ && motionSource_ == MotionSource::Controller && motionInputAllowed())
        releaseMouseInput();
    else
        resetMotionInput();
    keyboardSpecialHeld_.fill(false);
    rewindExhausted_ = false;
    slowHeld_ = false;
    speedSteps_ = 16;
    if (audioConverter_)
        audioConverter_->setInputRate(
            cupid_gb_apu_get_sample_rate(emulator_ ? &emulator_->gb : nullptr));
    rapidHeld_.fill(false);
    rapidPhase_.fill(0);
    physicalJoypad_ = 0xff;
    linkedKeyboardJoypad_ = 0xff;
    reconcileControllerInput(resetAudio);
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
    if (linkSession_)
        buttons[1] &= linkedKeyboardJoypad_;
    if (mouseButtonA_ && emulator_ && emulator_->gb.header.mbc_type == CUPID_GB_MBC7)
        buttons[0] &= 0xefu;
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
    controllerPlayerCount_ = inputPlayerCount();
    if (linkSession_) {
        applyLinkedPlayerButtons(buttons);
        return;
    }
    uint8_t first = buttons[0];
    if (controllerPlayerCount_ == 1)
        for (unsigned player = 1; player < 4; ++player)
            first &= buttons[player];
    // A UI resynchronization without a changed contact is not a new input
    // event. In particular, closing a menu must retain a settled reset sample.
    if (emulator_->gb.joypad != first)
        setJoypad(first);
    for (unsigned player = 1; player < 4; ++player)
        if (emulator_->gb.joypad_extra[player - 1] != buttons[player])
            cupid_gb_set_buttons_for_player(&emulator_->gb, player, buttons[player]);
}

unsigned MainWindow::inputPlayerCount() const {
    if (linkSession_)
        return 2;
    return emulator_ && cupid_gb_sgb_host_present(&emulator_->gb) ? emulator_->gb.sgb.player_count
                                                                  : 1;
}

void MainWindow::applyLinkedPlayerButtons(const std::array<uint8_t, 4> &buttons) {
    auto *partner = linkPartner();
    if (!partner || !emulator_ || !partner->emulator_)
        return;
    linkedInput_ = {buttons[0], buttons[1]};
    const uint8_t own = linkedInput_[0] & partner->linkedInput_[1];
    const uint8_t peer = partner->linkedInput_[0] & linkedInput_[1];
    // Each window owns its own contribution. A gated peer releases only that
    // contribution, so it cannot erase player two input routed by the focused window.
    if (!(paused_ && debuggerInputPaused_) && emulator_->gb.joypad != own)
        setJoypad(own);
    if (!(partner->paused_ && partner->debuggerInputPaused_) &&
        partner->emulator_->gb.joypad != peer)
        partner->setJoypad(peer);
}

void MainWindow::releaseLinkedInput() {
    if (!linkSession_)
        return;
    if (controllers_)
        controllers_->stopRumble();
    controllerOutput_.buttons.fill(0xff);
    controllerOutput_.held.fill(false);
    for (auto &rapid : controllerOutput_.rapid)
        rapid.fill(false);
    releaseKeys(false);
    applyLinkedPlayerButtons({0xff, 0xff, 0xff, 0xff});
}

void MainWindow::refreshControllerPlayerCount() {
    if (!emulator_)
        return;
    const unsigned count = inputPlayerCount();
    if (count == controllerPlayerCount_)
        return;
    // An SGB command can change routing inside one guest frame. Update before the next opcode.
    applyPlayerButtons(paused_ && debuggerInputPaused_ ? debuggerPlayerButtons_
                                                       : livePlayerButtons());
}

void MainWindow::receiveControllerOutput(const cupid::input::ControllerOutput &output) {
    // Delivery gates synthesize a released output. A reset transaction retains
    // its host controls until delivery reopens and reports the physical state.
    if (stateOperation_ && !controllers_->deliveryEnabled()) {
        controllerInputDeferred_ = true;
        return;
    }
    const auto previous = controllerOutput_;
    controllerOutput_ = output;
    if (menuInputSuspended_) {
        // Navigation can release an existing hold, but cannot introduce a new
        // game press. The full current output is reconciled when the menu closes.
        for (size_t player = 0; player < controllerOutput_.buttons.size(); ++player) {
            controllerOutput_.buttons[player] |= previous.buttons[player];
            for (size_t action = 0; action < controllerOutput_.rapid[player].size(); ++action)
                controllerOutput_.rapid[player][action] &= previous.rapid[player][action];
        }
        for (size_t action = 0; action < controllerOutput_.held.size(); ++action)
            controllerOutput_.held[action] &= previous.held[action];
        controllerOutput_.motionAvailable &= previous.motionAvailable;
        controllerOutput_.motionX = previous.motionX;
        controllerOutput_.motionY = previous.motionY;
    }
    controllerMotionChanged(previous);
    reconcileControllerInput();
}

void MainWindow::reconcileControllerInput(bool resetAudio) {
    const bool oldTurbo = turbo_, oldRewind = rewind_;
    const auto held = [this](size_t action) {
        return !paused_ && (keyboardSpecialHeld_[action] || controllerOutput_.held[action]);
    };
    turbo_ = held(0);
    rewind_ = !linkSession_ && held(1);
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
    // Session commits restore audio and pacing after their metadata is ready.
    // Opening a sink here could dispatch events against a partially committed ROM.
    if (oldTurbo != turbo_ || oldRewind != rewind_) {
        if (oldRewind != rewind_)
            rewindExhausted_ = false;
        resetPacing();
        if (resetAudio) {
            if (stateOperation_)
                audioModeResetPending_ = true;
            else
                clearAudio();
        }
    }
    if (!(paused_ && debuggerInputPaused_) && !stateOperation_)
        applyHeldButtons();
}

void MainWindow::refreshControllerGate() {
    refreshMotionGate();
    if (!controllers_)
        return;
    const bool focused = QApplication::applicationState() == Qt::ApplicationActive;
    controllers_->setFocused(focused || (isolated_ && !isVisible()));
    const auto *active = QApplication::activeWindow();
    rememberInputWindow(this, active == this);
    MainWindow *backgroundOwner = nullptr;
    for (const auto &window : inputWindowOrder)
        if (window && window->emulator_ && !window->linkWindowClosing_) {
            backgroundOwner = window;
            break;
        }
    const auto ownMenu = [this](const QWidget *widget) {
        return qobject_cast<const QMenu *>(widget) && objectBelongsTo(widget, this);
    };
    const auto *popup = QApplication::activePopupWidget();
    // Losing application focus must not make every window consume the same
    // player slots. Preserve the last focused live cartridge's routing roles.
    const bool otherWindow =
        active ? active != this && !ownMenu(active) : backgroundOwner && backgroundOwner != this;
    // Menus keep physical delivery so releases cannot leave controls stuck.
    // The receive path suppresses their new presses and command shortcuts.
    const bool blocked = (isolated_ && !controllerSmokeActive_) || stateOperation_ ||
                         QApplication::activeModalWidget() || (popup && !ownMenu(popup)) ||
                         otherWindow || !inputTrace_.empty();
    controllers_->setDeliveryEnabled(!blocked);
    // Classification belongs to the cartridge, independently of delivery gates.
    // A paused tilt axis must not become a digital direction on resume.
    controllers_->setMotionEnabled(emulator_ && emulator_->gb.header.mbc_type == CUPID_GB_MBC7);
    const bool backgroundResync =
        !active && backgroundOwner == this && controllers_->configuration().background &&
        controllers_->deliveryEnabled() && !(controllerOutput_ == controllers_->output());
    if (!stateOperation_ && (controllerInputDeferred_ || backgroundResync)) {
        // A real release during a blocked transaction may leave the manager at
        // its already neutral output, so reopening need not emit another change.
        // Likewise, focus loss releases routed input while a background-enabled
        // manager can retain an unchanged physical hold for the selected owner.
        controllerInputDeferred_ = false;
        receiveControllerOutput(controllers_->output());
    }
}

void MainWindow::navigateControllerMenu(const QString &action, SDL_JoystickID instance) {
    (void)instance;
    QWidget *target = QApplication::activePopupWidget();
    if (!target)
        target = QApplication::activeModalWidget();
    if (!target || !objectBelongsTo(target, this) ||
        QApplication::applicationState() != Qt::ApplicationActive ||
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
        if (!guarded || !objectBelongsTo(guarded, this) ||
            guarded->property("controllerCaptureActive").toBool() ||
            guarded->property("controllerCaptureConsumed").toBool() ||
            (QApplication::activePopupWidget() != guarded &&
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
    if (auto *partner = linkPartner()) {
        if (controllers_)
            controllers_->setPlayerRumble(0, 0.0);
        if (partner->controllers_)
            partner->controllers_->setPlayerRumble(1, 0.0);
    } else if (controllers_) {
        controllers_->stopRumble();
    }
    rumbleSampleSum_ = rumbleSampleCount_ = 0;
    rumblePublishedAmplitudes_.clear();
    rumbleLastCycles_ = emulator_ ? emulator_->gb.elapsed_cycles : 0;
}

void MainWindow::finishControllerRumble() {
    if (!emulator_ || !controllers_)
        return;
    const uint64_t now = emulator_->gb.elapsed_cycles;
    if (now < rumbleLastCycles_) {
        resetControllerRumble();
        return;
    }
    rumbleLastCycles_ = now;
    for (const double amplitude : rumblePublishedAmplitudes_) {
        const double active = !paused_ && !rewind_ && !stateOperation_ ? amplitude : 0.0;
        if (auto *partner = linkPartner()) {
            controllers_->setPlayerRumble(0, active);
            if (partner->controllers_)
                partner->controllers_->setPlayerRumble(1, active);
        } else {
            controllers_->setRumble(active, controllerPlayerCount_ == 1);
        }
    }
    rumblePublishedAmplitudes_.clear();
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
            applyControllerConfiguration(configuration, dialog.playerOverrides(),
                                         dialog.preferredInstance());
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
                                      playbackSpeed() / 16);
}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    const auto *widget = qobject_cast<QWidget *>(object);
    const bool ownMenu = qobject_cast<const QMenu *>(widget) && objectBelongsTo(widget, this);
    const bool otherWindowActivated = menuInputSuspended_ &&
                                      event->type() == QEvent::WindowActivate && widget &&
                                      widget->isWindow() && widget != this && !ownMenu;
    const bool deactivate =
        event->type() == QEvent::ApplicationDeactivate || otherWindowActivated ||
        (event->type() == QEvent::WindowDeactivate && object == this && !menuInputSuspended_);
    if (deactivate)
        motionFocusActive_ = false;
    if (event->type() == QEvent::ApplicationActivate ||
        (event->type() == QEvent::WindowActivate && object == this))
        motionFocusActive_ = true;
    if (event->type() == QEvent::WindowActivate && object == this)
        rememberInputWindow(this, true);
    if (handleMotionEvent(object, event))
        return true;
    if (deactivate) {
        menuInputSuspended_ = false;
        if (otherWindowActivated)
            refreshControllerGate();
        releaseLinkedInput();
        releaseKeys();
    }
    if (event->type() == QEvent::ApplicationDeactivate && controllers_) {
        controllers_->setFocused(false);
        resetControllerRumble();
    }
    if (event->type() == QEvent::ApplicationActivate && controllers_) {
        controllers_->setFocused(true);
        resetControllerRumble();
    }
    const bool menuRelease = menuInputSuspended_ && widget && objectBelongsTo(widget, this);
    if (!stateOperation_ && (!paused_ || event->type() == QEvent::KeyRelease) &&
        ((!menuInputSuspended_ && (object == display_ || object == this)) ||
         (menuRelease && event->type() == QEvent::KeyRelease)) &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ||
         event->type() == QEvent::KeyRelease) &&
        handleWorkBoyKey(static_cast<QKeyEvent *>(event), event->type() != QEvent::KeyRelease,
                         event->type() == QEvent::ShortcutOverride) &&
        event->type() != QEvent::KeyRelease)
        return true;
    if (menuRelease && event->type() == QEvent::MouseButtonRelease && mouseButtonA_) {
        mouseButtonA_ = false;
        if (!stateOperation_ && !(paused_ && debuggerInputPaused_))
            applyHeldButtons();
    }
    if (((!menuInputSuspended_ && (object == display_ || object == this)) ||
         (menuRelease && event->type() == QEvent::KeyRelease)) &&
        emulator_ && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->isAutoRepeat())
            return menuRelease ? QMainWindow::eventFilter(object, event) : true;
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
            if (action == 1 && held) {
                if (linkSession_)
                    statusBar()->showMessage(
                        "Rewind is unavailable while a link cable is connected.", 3000);
                else if (rewindStates_.size() < 2)
                    statusBar()->showMessage("No earlier rewind history");
            }
            resetPacing();
            return menuRelease ? QMainWindow::eventFilter(object, event) : true;
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
                return menuRelease ? QMainWindow::eventFilter(object, event) : true;
            }
        }
        if (linkSession_)
            for (size_t i = 0; i < linkedKeys_.size(); ++i) {
                if (!linkedKeys_[i] || value != linkedKeys_[i])
                    continue;
                const uint8_t mask = uint8_t(1u << i);
                if (pressed && !paused_)
                    linkedKeyboardJoypad_ &= uint8_t(~mask);
                else
                    linkedKeyboardJoypad_ |= mask;
                if (!(paused_ && debuggerInputPaused_))
                    applyHeldButtons();
                return menuRelease ? QMainWindow::eventFilter(object, event) : true;
            }
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    const QPointer<MainWindow> self(this);
    const bool closed = closeRom();
    if (!self) {
        event->accept();
        return;
    }
    if (!closed) {
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

#include "main_window_internal.h"

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

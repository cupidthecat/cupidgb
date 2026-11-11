#include "main_window_internal.h"

bool MainWindow::motionSmokeTest(const QString &directory) {
    struct GeometryCase {
        QPoint guest, sample;
    };

    const std::array<GeometryCase, 9> geometry = {{{{0, 0}, {112, 112}},
                                                   {{80, 72}, {0, 0}},
                                                   {{160, 144}, {-112, -112}},
                                                   {{81, 73}, {-2, -2}},
                                                   {{79, 71}, {1, 1}},
                                                   {{1, 1}, {110, 110}},
                                                   {{159, 143}, {-111, -111}},
                                                   {{40, 36}, {56, 56}},
                                                   {{120, 108}, {-56, -56}}}};
    for (const QSize source : {QSize(160, 144), QSize(256, 224)})
        for (const auto scaling :
             {DisplayScaling::Integer, DisplayScaling::Aspect, DisplayScaling::Fill})
            for (const qreal ratio : {1.0, 1.25, 1.5, 2.0}) {
                const QSizeF available(641, 517);
                const QRectF viewport = displayViewport(source, available, scaling, ratio);
                for (const auto &test : geometry) {
                    const QPointF position(
                        viewport.left() + (test.guest.x() + (source.width() - 160) / 2.0) *
                                              viewport.width() / source.width(),
                        viewport.top() + (test.guest.y() + (source.height() - 144) / 2.0) *
                                             viewport.height() / source.height());
                    const auto sample =
                        pointerMotionSample(position, source, available, scaling, ratio);
                    if (!sample || *sample != test.sample)
                        return smokeFailure(__FILE__, __LINE__);
                }
                const auto left = pointerMotionSample(viewport.topLeft() - QPointF(100, 100),
                                                      source, available, scaling, ratio);
                const auto right = pointerMotionSample(viewport.bottomRight() + QPointF(100, 100),
                                                       source, available, scaling, ratio);
                if (!left || !right || *left != QPoint(112, 112) || *right != QPoint(-112, -112))
                    return smokeFailure(__FILE__, __LINE__);
            }
    if (pointerMotionSample({}, {}, {640, 480}, DisplayScaling::Integer, 1) ||
        pointerMotionSample({}, {160, 144}, {}, DisplayScaling::Integer, 1) ||
        pointerMotionSample({}, {160, 144}, {640, 480}, DisplayScaling::Integer, 0))
        return smokeFailure(__FILE__, __LINE__);

    const QVariant originalChoice = settings_.value("input/allowMouseMotion");
    const QVariant originalModel = settings_.value("emulation/model");
    const QVariant originalBoot = settings_.value("emulation/bootDirectory");
    const auto originalControllers = controllers_->configuration();
    QScopedValueRollback<bool> testInput(controllerSmokeActive_, true);
    QScopedValueRollback<bool> testFocus(motionFocusActive_, true);
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    settings_.setValue("emulation/bootDirectory", QString());
    bootDirectoryOverride_ = {};
    auto configuration = originalControllers;
    configuration.enabled = false; // Pointer workflow isolation; manager tests use virtual devices.
    configuration.background = false;
    controllers_->configure(configuration);
    if (!originalChoice.isValid() && mouseMotionAction_->isChecked())
        return smokeFailure(__FILE__, __LINE__);
    mouseMotionAction_->setChecked(false);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom.replace(0x134, 12, QByteArray("MOTION INPUT", 12));
    rom[0x143] = char(0x80);
    rom[0x147] = char(0x22);
    const auto writeFixture = [&](const QString &name) {
        uint8_t checksum = 0;
        for (int i = 0x134; i <= 0x14c; ++i)
            checksum = uint8_t(checksum - uint8_t(rom[i]) - 1);
        rom[0x14d] = char(checksum);
        const QString path = QDir(directory).filePath(name);
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(rom) == rom.size() ? path : QString();
    };
    const QString path = writeFixture("motion-input.gbc");
    if (path.isEmpty() || !openRom(path) || emulator_->gb.header.mbc_type != CUPID_GB_MBC7)
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    motionFocusActive_ = true;
    refreshControllerGate();
    const auto move = [&](int x, int y) {
        const QSize source = display_->image.size();
        const QRectF viewport = displayViewport(source, display_->size(), display_->scaling,
                                                display_->devicePixelRatioF());
        const QPointF position(viewport.left() + x * viewport.width() / 160,
                               viewport.top() + y * viewport.height() / 144);
        QMouseEvent event(QEvent::MouseMove, position,
                          QPointF(display_->mapToGlobal(position.toPoint())), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(display_, &event);
    };
    const auto button = [&](bool down, Qt::MouseButton value = Qt::LeftButton) {
        const QPointF position(display_->rect().center());
        QMouseEvent event(down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease, position,
                          QPointF(display_->mapToGlobal(position.toPoint())), value,
                          down ? Qt::MouseButtons(value) : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(display_, &event);
    };
    const auto sampleIs = [&](int x, int y) {
        return emulator_->gb.mbc7.motion_x == x && emulator_->gb.mbc7.motion_y == y;
    };
    const auto latch = [&] {
        auto &gb = emulator_->gb;
        cupid_gb_write_u8(&gb, 0, 0x0a);
        cupid_gb_write_u8(&gb, 0x4000, 0x40);
        cupid_gb_write_u8(&gb, 0xa000, 0x55);
        cupid_gb_write_u8(&gb, 0xa010, 0xaa);
    };
    const auto deliver = [&](bool available, int16_t x, int16_t y, bool b = false) {
        auto output = controllerOutput_;
        output.motionAvailable = available;
        output.motionX = x;
        output.motionY = y;
        output.buttons[0] = b ? 0xdf : 0xff;
        controllers_->outputChanged(output);
    };
    move(0, 0);
    button(true);
    button(false);
    if (!sampleIs(0, 0) || emulator_->gb.joypad != 0xff)
        return smokeFailure(__FILE__, __LINE__);
    mouseMotionAction_->setChecked(true);
    settings_.sync();
    QSettings persisted;
    if (!persisted.value("input/allowMouseMotion", false).toBool())
        return smokeFailure(__FILE__, __LINE__);
    move(0, 0);
    latch();
    if (!sampleIs(112, 112) || emulator_->gb.mbc7_sensor_x != 0x8240 ||
        emulator_->gb.mbc7_sensor_y != 0x8240)
        return smokeFailure(__FILE__, __LINE__);
    move(81, 73);
    if (!sampleIs(-2, -2) || emulator_->gb.mbc7_sensor_x != 0x8240)
        return smokeFailure(__FILE__, __LINE__);
    latch();
    if (emulator_->gb.mbc7_sensor_x != 0x81ce || emulator_->gb.mbc7_sensor_y != 0x81ce)
        return smokeFailure(__FILE__, __LINE__);
    button(true);
    if (emulator_->gb.joypad & 0x10)
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent keyDown(QEvent::KeyPress, keys_[4], Qt::NoModifier);
    QApplication::sendEvent(display_, &keyDown);
    button(false);
    if (emulator_->gb.joypad & 0x10)
        return smokeFailure(__FILE__, __LINE__);
    QKeyEvent keyUp(QEvent::KeyRelease, keys_[4], Qt::NoModifier);
    QApplication::sendEvent(display_, &keyUp);
    if (!(emulator_->gb.joypad & 0x10))
        return smokeFailure(__FILE__, __LINE__);

    deliver(true, 24, -17);
    if (!sampleIs(24, -17))
        return smokeFailure(__FILE__, __LINE__);
    move(0, 0);
    deliver(true, 24, -17, true); // Unchanged tilt during button/poll output cannot replace mouse.
    deliver(true, 24, -17);
    if (!sampleIs(112, 112))
        return smokeFailure(__FILE__, __LINE__);
    deliver(false, 0, 0);
    deliver(true, 24, -17); // Same live coordinates after an unavailable/zero gate retain mouse.
    if (!sampleIs(112, 112))
        return smokeFailure(__FILE__, __LINE__);
    deliver(true, -56, 39); // Changed axes, including fallback, deliver a new controller sample.
    if (!sampleIs(-56, 39))
        return smokeFailure(__FILE__, __LINE__);
    deliver(false, 0, 0);
    if (!sampleIs(0, 0))
        return smokeFailure(__FILE__, __LINE__);

    move(0, 0);
    latch();
    if (!saveSlot(7))
        return smokeFailure(__FILE__, __LINE__);
    move(160, 144);
    button(true, Qt::RightButton);
    if (!loadSlot(7) || !sampleIs(-112, -112) || emulator_->gb.mbc7_sensor_x != 0x8240 ||
        !mouseButtonA_ || (emulator_->gb.joypad & 0x10))
        return smokeFailure(__FILE__, __LINE__);
    latch();
    if (emulator_->gb.mbc7_sensor_x != 0x8160 || emulator_->gb.mbc7_sensor_y != 0x8160)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray beforeInvalid = captureState();
    if (restoreState(QByteArray("invalid", 7), false) || !sampleIs(-112, -112) || !mouseButtonA_ ||
        captureState() != beforeInvalid)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(true);
    if (!sampleIs(0, 0) || mouseButtonA_ || !(emulator_->gb.joypad & 0x10) ||
        emulator_->gb.mbc7_sensor_x != 0x8160)
        return smokeFailure(__FILE__, __LINE__);
    move(0, 0);
    button(true);
    if (!sampleIs(0, 0) || mouseButtonA_)
        return smokeFailure(__FILE__, __LINE__);
    setPaused(false);
    move(0, 0);
    button(true);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(this, &deactivate);
    move(160, 144);
    if (!sampleIs(0, 0) || mouseButtonA_)
        return smokeFailure(__FILE__, __LINE__);
    QEvent activate(QEvent::WindowActivate);
    QApplication::sendEvent(this, &activate);
    deliver(true, 31, -11);
    QApplication::sendEvent(this, &deactivate);
    if (!sampleIs(0, 0))
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(this, &activate);
    configuration.background = true;
    controllers_->configure(configuration);
    deliver(true, 32, -12);
    QApplication::sendEvent(this, &deactivate);
    if (!sampleIs(32, -12) || !motionInputAllowed() || motionInputAllowed(true))
        return smokeFailure(__FILE__, __LINE__);
    move(0, 0);
    if (!sampleIs(32, -12))
        return smokeFailure(__FILE__, __LINE__);
    QApplication::sendEvent(this, &activate);
    move(0, 0);
    button(true);
    {
        QScopedValueRollback<bool> operation(stateOperation_, true);
        refreshControllerGate();
        move(160, 144);
        button(false);
        if (!sampleIs(112, 112) || mouseButtonA_ || motionInputAllowed())
            return smokeFailure(__FILE__, __LINE__);
    }
    refreshControllerGate();
    if (emulator_->gb.joypad != 0xff || !sampleIs(112, 112))
        return smokeFailure(__FILE__, __LINE__);
    move(0, 0);
    button(true);
    mouseMotionAction_->setChecked(false);
    if (!sampleIs(0, 0) || mouseButtonA_ || !(emulator_->gb.joypad & 0x10))
        return smokeFailure(__FILE__, __LINE__);
    mouseMotionAction_->setChecked(true);
    move(0, 0);
    button(true);
    if (!restart() || !sampleIs(0, 0) || mouseButtonA_)
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    if (!closeRom())
        return smokeFailure(__FILE__, __LINE__);
    rom[0x147] = 0;
    const QString ordinary = writeFixture("motion-ordinary.gbc");
    if (ordinary.isEmpty() || !openRom(ordinary))
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    const QByteArray beforeOrdinary = captureState();
    move(0, 0);
    button(true);
    button(false);
    if (captureState() != beforeOrdinary || mouseButtonA_ || !closeRom())
        return smokeFailure(__FILE__, __LINE__);

    // A stick already held across cartridge and pause transitions must change
    // classification before the new core sees any host-button reconciliation.
    struct HeldPad {
        SDL_Joystick *handle = nullptr;
        SDL_JoystickID id = -1;
        bool attached = false;

        bool attach() {
            SDL_VirtualJoystickDesc descriptor{};
            descriptor.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            descriptor.type = SDL_JOYSTICK_TYPE_UNKNOWN;
            descriptor.naxes = 2;
            descriptor.nbuttons = 3;
            descriptor.vendor_id = 0xc115;
            descriptor.product_id = 0x7041;
            descriptor.name = "CupidGB motion transition fixture";
            const int index = SDL_JoystickAttachVirtualEx(&descriptor);
            if (index < 0)
                return false;
            attached = true;
            id = SDL_JoystickGetDeviceInstanceID(index);
            handle = SDL_JoystickOpen(index);
            return handle && id >= 0;
        }

        void detach() {
            if (handle) {
                SDL_JoystickClose(handle);
                handle = nullptr;
            }
            if (attached)
                for (int index = 0; index < SDL_NumJoysticks(); ++index)
                    if (SDL_JoystickGetDeviceInstanceID(index) == id) {
                        SDL_JoystickDetachVirtual(index);
                        break;
                    }
            attached = false;
        }

        ~HeldPad() {
            detach();
        }
    } pad;

    const int deviceBaseline = SDL_NumJoysticks();
    if (!pad.attach())
        return smokeFailure(__FILE__, __LINE__);
    const auto waitForController = [&](const std::function<bool()> &condition) {
        QElapsedTimer timeout;
        timeout.start();
        while (timeout.elapsed() < 2000) {
            controllers_->backend().pollNow();
            if (condition())
                return true;
            QApplication::processEvents(QEventLoop::AllEvents, 2);
        }
        return condition();
    };
    cupid::input::DeviceState heldState;
    if (!waitForController([&] { return controllers_->backend().state(pad.id, &heldState); }))
        return smokeFailure(__FILE__, __LINE__);
    cupid::input::ControllerConfiguration transitions;
    transitions.rumble = false;
    for (const auto &device : controllers_->backend().devices()) {
        auto profile = cupid::input::ControllerManager::defaults(device.info);
        profile.enabled = device.info.instance == pad.id;
        if (profile.enabled) {
            profile.bindings["b"] = {};
            profile.bindings["turbo"] = {{cupid::input::Binding::Button, 1, 1}};
            profile.bindings["rewind"] = {{cupid::input::Binding::Button, 2, 1}};
        }
        transitions.profiles[device.info.persistentKey] = profile;
    }
    controllers_->configure(transitions);
    controllers_->setDeliveryEnabled(true);
    controllers_->setFocused(true);
    SDL_JoystickSetVirtualAxis(pad.handle, 0, 30000);
    SDL_JoystickSetVirtualButton(pad.handle, 1, 1);
    SDL_JoystickSetVirtualButton(pad.handle, 2, 1);
    if (!waitForController([&] {
            return !(controllerOutput_.buttons[0] & 1) && controllerOutput_.held[0] &&
                   controllerOutput_.held[1];
        }) ||
        !openRom(ordinary))
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    if (!turbo_ || !rewind_)
        return smokeFailure(__FILE__, __LINE__);
    const bool priorPriming = audioPriming_;
    audioPriming_ = false;
    {
        QScopedValueRollback<bool> operation(stateOperation_, true);
        releaseKeys();
        if (audioPriming_)
            return smokeFailure(__FILE__, __LINE__);
    }
    audioPriming_ = priorPriming;
    QApplication::sendEvent(display_, &keyDown);
    if ((emulator_->gb.joypad & 1) || (emulator_->gb.joypad & 0x10) ||
        controllers_->motionEnabled() || !openRom(path))
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    if (!controllers_->motionEnabled() || emulator_->gb.joypad != 0xff || !turbo_ || !rewind_ ||
        (emulator_->gb.interrupt_flags & 0x10) || physicalJoypad_ != 0xff || !sampleIs(0, 0) ||
        emulator_->gb.elapsed_cycles)
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t beforeHeldPause = emulator_->gb.elapsed_cycles;
    cupid_gb_write_u8(&emulator_->gb, 0xff0f, 0);
    setPaused(true);
    refreshControllerGate();
    controllers_->backend().pollNow();
    setPaused(false);
    if (!controllers_->motionEnabled() || emulator_->gb.joypad != 0xff ||
        (emulator_->gb.interrupt_flags & 0x10) || emulator_->gb.elapsed_cycles != beforeHeldPause)
        return smokeFailure(__FILE__, __LINE__);
    move(0, 0);
    button(true);
    QApplication::sendEvent(display_, &keyDown);
    if ((emulator_->gb.joypad & 0x10) || !mouseButtonA_ || !restart())
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    if (emulator_->gb.joypad != 0xff || (emulator_->gb.interrupt_flags & 0x10) || mouseButtonA_ ||
        physicalJoypad_ != 0xff || !sampleIs(0, 0) || emulator_->gb.elapsed_cycles)
        return smokeFailure(__FILE__, __LINE__);
    move(0, 0);
    button(true);
    QApplication::sendEvent(display_, &keyDown);
    if (!openRom(ordinary))
        return smokeFailure(__FILE__, __LINE__);
    timer_.stop();
    if (controllers_->motionEnabled() || (emulator_->gb.joypad & 1) ||
        !(emulator_->gb.joypad & 0x10) || mouseButtonA_ || physicalJoypad_ != 0xff ||
        emulator_->gb.elapsed_cycles || !closeRom())
        return smokeFailure(__FILE__, __LINE__);
    pad.detach();
    if (!waitForController([&] { return !controllers_->backend().state(pad.id, &heldState); }) ||
        SDL_NumJoysticks() != deviceBaseline)
        return smokeFailure(__FILE__, __LINE__);
    controllers_->configure(originalControllers);
    controllerOutput_ = controllers_->output();
    for (const auto &entry : {qMakePair(QString("input/allowMouseMotion"), originalChoice),
                              qMakePair(QString("emulation/model"), originalModel),
                              qMakePair(QString("emulation/bootDirectory"), originalBoot)}) {
        if (entry.second.isValid())
            settings_.setValue(entry.first, entry.second);
        else
            settings_.remove(entry.first);
    }
    mouseMotionAction_->setChecked(originalChoice.toBool());
    if (!originalChoice.isValid())
        settings_.remove("input/allowMouseMotion");
    timer_.start();
    return true;
}

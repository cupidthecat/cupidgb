#include "controller_backend.h"

#include <QCoreApplication>
#include <QEvent>
#include <QThread>

#include <algorithm>
#include <array>
#include <set>
#include <string>

namespace cupid::input {

#if SDL_VERSION_ATLEAST(2, 30, 0)
static constexpr Uint32 lastControllerEvent = SDL_CONTROLLERSTEAMHANDLEUPDATED;
#else
static constexpr Uint32 lastControllerEvent = SDL_CONTROLLERSENSORUPDATE;
#endif

bool InputState::operator==(const InputState &other) const {
    return axes == other.axes && buttons == other.buttons && hats == other.hats &&
           controllerAxes == other.controllerAxes && controllerButtons == other.controllerButtons &&
           allowed == other.allowed;
}

ControllerBackend::ControllerBackend(QObject *parent) : QObject(parent) {
    timer_.setInterval(8);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { pollNow(); });
}

ControllerBackend::~ControllerBackend() {
    stop();
}

void ControllerBackend::assertThread() const {
    Q_ASSERT(QThread::currentThread() == thread());
}

bool ControllerBackend::start(QString *error) {
    assertThread();
    if (initialized_)
        return true;
    SDL_SetMainReady();
    // Collect physical state even when the host blocks delivery. This avoids stale held buttons.
    // A future shared SDL host must agree on this process-wide hint and event-stream ownership.
    const char *previousHint = SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    hadBackgroundHint_ = previousHint != nullptr;
    previousBackgroundHint_ = QString::fromUtf8(previousHint ? previousHint : "");
    installedBackgroundHint_ = SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    constexpr Uint32 subsystems = SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC;
    if (SDL_InitSubSystem(subsystems) < 0) {
        if (error)
            *error = QString::fromUtf8(SDL_GetError());
        if (installedBackgroundHint_) {
            if (hadBackgroundHint_)
                SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                            previousBackgroundHint_.toUtf8().constData());
            else
                SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
        }
        return false;
    }
    initialized_ = true;
    clock_.start();
    discardPendingEdges();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
    pollNow();
    timer_.start();
    return true;
}

void ControllerBackend::stop() {
    assertThread();
    timer_.stop();
    if (!initialized_)
        return;
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeEventFilter(this);
    for (auto &[id, device] : devices_) {
        closeDevice(device);
        if (deviceRemoved)
            deviceRemoved(id);
    }
    devices_.clear();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
    const char *currentHint = SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    if (installedBackgroundHint_ && currentHint && std::string(currentHint) == "1") {
        if (hadBackgroundHint_)
            SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                        previousBackgroundHint_.toUtf8().constData());
        else
            SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    }
    initialized_ = false;
}

bool ControllerBackend::running() const {
    return initialized_;
}

std::vector<DeviceState> ControllerBackend::devices() const {
    assertThread();
    std::vector<DeviceState> result;
    for (const auto &[id, device] : devices_) {
        (void)id;
        result.push_back(device.state);
    }
    return result;
}

bool ControllerBackend::state(SDL_JoystickID id, DeviceState *result) const {
    assertThread();
    const auto it = devices_.find(id);
    if (it == devices_.end() || !result)
        return false;
    *result = it->second.state;
    return true;
}

bool ControllerBackend::inputAllowed() const {
    return enabled_ && (focused_ || background_);
}

void ControllerBackend::setFocused(bool focused) {
    assertThread();
    if (focused_ == focused)
        return;
    const bool wasAllowed = inputAllowed();
    focused_ = focused;
    if (!wasAllowed && inputAllowed())
        discardPendingEdges();
    updateGate();
}

void ControllerBackend::setInputEnabled(bool enabled) {
    assertThread();
    if (enabled_ == enabled)
        return;
    const bool wasAllowed = inputAllowed();
    enabled_ = enabled;
    if (!wasAllowed && inputAllowed())
        discardPendingEdges();
    updateGate();
}

void ControllerBackend::setAllowBackground(bool enabled) {
    assertThread();
    if (background_ == enabled)
        return;
    const bool wasAllowed = inputAllowed();
    background_ = enabled;
    if (!wasAllowed && inputAllowed())
        discardPendingEdges();
    updateGate();
}

void ControllerBackend::discardPendingEdges() {
    if (!initialized_)
        return;
    // Old queued presses must not become shortcut presses after delivery is enabled again.
    // Current held state is delivered separately as a resynchronization, without synthetic edges.
    // Keep device/remapping metadata queued so a delivery boundary cannot hide a capability change.
    SDL_PumpEvents();
    SDL_JoystickUpdate();
    SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_JOYBUTTONUP);
    SDL_FlushEvents(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERBUTTONUP);
}

void ControllerBackend::updateGate() {
    for (auto &[id, device] : devices_) {
        if (!enabled_ || !focused_)
            stopRumble(id);
        // A resynchronization is deliberately distinct from a new physical button press.
        update(device, true);
    }
}

bool ControllerBackend::eventFilter(QObject *watched, QEvent *event) {
    if (watched == QCoreApplication::instance()) {
        if (event->type() == QEvent::ApplicationDeactivate)
            setFocused(false);
        else if (event->type() == QEvent::ApplicationActivate)
            setFocused(true);
    }
    return QObject::eventFilter(watched, event);
}

void ControllerBackend::describe(Device &device) {
    auto &info = device.state.info;
    const auto text = [](const char *s) { return QString::fromUtf8(s ? s : ""); };
    info.name = text(SDL_JoystickName(device.joystick));
    char guid[33]{};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(device.joystick), guid, sizeof(guid));
    info.guid = QString::fromLatin1(guid);
    info.path = text(SDL_JoystickPath(device.joystick));
    info.serial = text(SDL_JoystickGetSerial(device.joystick));
    info.profileKey = "sdl2:" + info.guid;
    info.persistentKey = info.profileKey;
    info.identity = DeviceInfo::Identity::DeviceClass;
    if (!info.serial.isEmpty()) {
        info.persistentKey += ":serial:" + info.serial;
        info.identity = DeviceInfo::Identity::Serial;
    } else if (!info.path.isEmpty()) {
        info.persistentKey += ":path:" + info.path;
        info.identity = DeviceInfo::Identity::ConnectionPath;
    }
    info.vendor = SDL_JoystickGetVendor(device.joystick);
    info.product = SDL_JoystickGetProduct(device.joystick);
    info.version = SDL_JoystickGetProductVersion(device.joystick);
    info.axes = SDL_JoystickNumAxes(device.joystick);
    info.buttons = SDL_JoystickNumButtons(device.joystick);
    info.hats = SDL_JoystickNumHats(device.joystick);
    info.mapped = device.controller != nullptr;
    info.controllerAxisMask = 0;
    info.controllerButtonMask = 0;
    if (device.controller) {
        for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i)
            if (SDL_GameControllerHasAxis(device.controller,
                                          static_cast<SDL_GameControllerAxis>(i)))
                info.controllerAxisMask |= uint32_t{1} << i;
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
            if (SDL_GameControllerHasButton(device.controller,
                                            static_cast<SDL_GameControllerButton>(i)))
                info.controllerButtonMask |= uint32_t{1} << i;
    }
    info.rumble = SDL_JoystickHasRumble(device.joystick);
    info.accelerometer =
        device.controller && SDL_GameControllerHasSensor(device.controller, SDL_SENSOR_ACCEL);
    info.gyroscope =
        device.controller && SDL_GameControllerHasSensor(device.controller, SDL_SENSOR_GYRO);
}

void ControllerBackend::reconcileDevices() {
    std::set<SDL_JoystickID> present;
    const int count = SDL_NumJoysticks();
    for (int index = 0; index < count; ++index) {
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
        if (id < 0)
            continue;
        present.insert(id);
        auto it = devices_.find(id);
        if (it == devices_.end()) {
            Device device;
            device.joystick = SDL_JoystickOpen(index);
            if (!device.joystick) {
                if (errorOccurred)
                    errorOccurred(QString::fromUtf8(SDL_GetError()));
                continue;
            }
            device.state.info.instance = id;
            if (SDL_IsGameController(index))
                device.controller = SDL_GameControllerOpen(index);
            describe(device);
            it = devices_.emplace(id, std::move(device)).first;
            if (deviceAdded)
                deviceAdded(it->second.state.info);
            update(it->second, true);
        } else if (!it->second.controller && SDL_IsGameController(index)) {
            it->second.controller = SDL_GameControllerOpen(index);
            describe(it->second);
            if (deviceChanged)
                deviceChanged(it->second.state.info);
        }
    }
    for (auto it = devices_.begin(); it != devices_.end();) {
        if (!present.count(it->first) || !SDL_JoystickGetAttached(it->second.joystick)) {
            const SDL_JoystickID id = it->first;
            closeDevice(it->second);
            it = devices_.erase(it);
            if (deviceRemoved)
                deviceRemoved(id);
        } else {
            ++it;
        }
    }
}

void ControllerBackend::update(Device &device, bool resynchronized) {
    auto &physical = device.state.physical;
    const InputState previousPhysical = physical;
    const auto &info = device.state.info;
    physical.allowed = true;
    physical.axes.resize(std::max(info.axes, 0));
    physical.buttons.resize(std::max(info.buttons, 0));
    physical.hats.resize(std::max(info.hats, 0));
    for (int i = 0; i < info.axes; ++i)
        physical.axes[i] = SDL_JoystickGetAxis(device.joystick, i);
    for (int i = 0; i < info.buttons; ++i)
        physical.buttons[i] = SDL_JoystickGetButton(device.joystick, i);
    for (int i = 0; i < info.hats; ++i)
        physical.hats[i] = SDL_JoystickGetHat(device.joystick, i);
    physical.controllerAxes.resize(device.controller ? SDL_CONTROLLER_AXIS_MAX : 0);
    physical.controllerButtons.resize(device.controller ? SDL_CONTROLLER_BUTTON_MAX : 0);
    if (device.controller) {
        for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i)
            physical.controllerAxes[i] = SDL_GameControllerGetAxis(
                device.controller, static_cast<SDL_GameControllerAxis>(i));
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
            physical.controllerButtons[i] = SDL_GameControllerGetButton(
                device.controller, static_cast<SDL_GameControllerButton>(i));
    }
    InputState effective = physical;
    effective.allowed = inputAllowed();
    if (!effective.allowed) {
        std::fill(effective.axes.begin(), effective.axes.end(), 0);
        std::fill(effective.buttons.begin(), effective.buttons.end(), 0);
        std::fill(effective.hats.begin(), effective.hats.end(), SDL_HAT_CENTERED);
        std::fill(effective.controllerAxes.begin(), effective.controllerAxes.end(), 0);
        std::fill(effective.controllerButtons.begin(), effective.controllerButtons.end(), 0);
    }
    const bool released = device.state.effective.allowed && !effective.allowed;
    const bool changed = device.state.effective != effective;
    device.state.effective = std::move(effective);
    if (released && inputReleased)
        inputReleased(info.instance);
    if ((changed || previousPhysical != physical || resynchronized) && stateChanged)
        stateChanged(device.state, resynchronized);
}

void ControllerBackend::closeDevice(Device &device) {
    const SDL_JoystickID id = device.state.info.instance;
    stopRumble(id);
    auto &effective = device.state.effective;
    effective.allowed = false;
    std::fill(effective.axes.begin(), effective.axes.end(), 0);
    std::fill(effective.buttons.begin(), effective.buttons.end(), 0);
    std::fill(effective.hats.begin(), effective.hats.end(), SDL_HAT_CENTERED);
    std::fill(effective.controllerAxes.begin(), effective.controllerAxes.end(), 0);
    std::fill(effective.controllerButtons.begin(), effective.controllerButtons.end(), 0);
    if (inputReleased)
        inputReleased(id);
    if (stateChanged)
        stateChanged(device.state, true);
    // Close both references; GameControllerOpen holds its own joystick reference.
    if (device.controller)
        SDL_GameControllerClose(device.controller);
    if (device.joystick)
        SDL_JoystickClose(device.joystick);
    device.controller = nullptr;
    device.joystick = nullptr;
}

bool ControllerBackend::rumble(SDL_JoystickID id, uint16_t low, uint16_t high, unsigned durationMs,
                               bool explicitTest) {
    assertThread();
    const auto it = devices_.find(id);
    if (it == devices_.end() || (!enabled_ && !explicitTest) || !focused_)
        return false;
    auto &device = it->second;
    if (!device.state.info.rumble)
        return false;
    const unsigned duration = std::min(durationMs, 250u);
    if (!duration || (!low && !high)) {
        stopRumble(id);
        return true;
    }
    if (SDL_JoystickRumble(device.joystick, low, high, duration) < 0)
        return false;
    device.state.rumbleActive = true;
    device.rumbleUntil = clock_.elapsed() + duration;
    return true;
}

void ControllerBackend::stopRumble(SDL_JoystickID id) {
    assertThread();
    const auto it = devices_.find(id);
    if (it == devices_.end())
        return;
    auto &device = it->second;
    if (device.state.rumbleActive) {
        // Unplug can make this fail. Clear host intent regardless, and never replay on reconnect.
        SDL_JoystickRumble(device.joystick, 0, 0, 0);
        device.state.rumbleActive = false;
        device.rumbleUntil = 0;
    }
}

void ControllerBackend::dispatch(const SDL_Event &e) {
    InputEvent event{};
    switch (e.type) {
        case SDL_CONTROLLERDEVICEREMAPPED: {
            const auto it = devices_.find(e.cdevice.which);
            if (it != devices_.end()) {
                describe(it->second);
                if (deviceChanged)
                    deviceChanged(it->second.state.info);
            }
            return;
        }
        case SDL_JOYAXISMOTION:
            event = {InputEvent::Kind::Axis, e.jaxis.which, e.jaxis.axis, e.jaxis.value,
                     inputAllowed()};
            break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            event = {InputEvent::Kind::Button, e.jbutton.which, e.jbutton.button, e.jbutton.state,
                     inputAllowed()};
            break;
        case SDL_JOYHATMOTION:
            event = {InputEvent::Kind::Hat, e.jhat.which, e.jhat.hat, e.jhat.value, inputAllowed()};
            break;
        case SDL_CONTROLLERAXISMOTION:
            event = {InputEvent::Kind::ControllerAxis, e.caxis.which, e.caxis.axis, e.caxis.value,
                     inputAllowed()};
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            event = {InputEvent::Kind::ControllerButton, e.cbutton.which, e.cbutton.button,
                     e.cbutton.state, inputAllowed()};
            break;
        default:
            return;
    }
    if (devices_.count(event.instance) && inputEvent)
        inputEvent(event);
}

void ControllerBackend::drainEvents(Uint32 first, Uint32 last) {
    // Preserve unrelated SDL event streams. At most 512 input events per family per Qt turn.
    std::array<SDL_Event, 64> events{};
    for (unsigned budget = 0; budget < 512; budget += static_cast<unsigned>(events.size())) {
        const int count = SDL_PeepEvents(events.data(), static_cast<int>(events.size()),
                                         SDL_GETEVENT, first, last);
        if (count < 0) {
            if (errorOccurred)
                errorOccurred(QString::fromUtf8(SDL_GetError()));
            break;
        }
        for (int i = 0; i < count; ++i)
            dispatch(events[i]);
        if (count < static_cast<int>(events.size()))
            break;
    }
}

void ControllerBackend::pollNow() {
    assertThread();
    if (!initialized_ || polling_)
        return;
    polling_ = true;
    SDL_PumpEvents();
    SDL_JoystickUpdate();
    reconcileDevices();
    drainEvents(SDL_JOYAXISMOTION, SDL_JOYBATTERYUPDATED);
    drainEvents(SDL_CONTROLLERAXISMOTION, lastControllerEvent);
    for (auto &[id, device] : devices_) {
        update(device);
        if (device.state.rumbleActive && clock_.elapsed() >= device.rumbleUntil)
            stopRumble(id);
    }
    polling_ = false;
}

} // namespace cupid::input

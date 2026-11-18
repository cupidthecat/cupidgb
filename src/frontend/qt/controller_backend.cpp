/**
 * @file    controller_backend.cpp
 * @brief   Controller discovery, input events, motion samples, and motor output.
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

#include "controller_backend.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QScopedValueRollback>
#include <QThread>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>

namespace cupid::input {

#if SDL_VERSION_ATLEAST(2, 30, 0)
static constexpr Uint32 lastControllerEvent = SDL_CONTROLLERSTEAMHANDLEUPDATED;
#else
static constexpr Uint32 lastControllerEvent = SDL_CONTROLLERSENSORUPDATE;
#endif

struct ControllerBackend::SharedTransport {
    static constexpr Uint32 subsystems =
        SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC;
    std::vector<QPointer<ControllerBackend>> clients;
    std::map<SDL_JoystickID, ControllerBackend *> rumbleOwners;
    QThread *thread = nullptr;
    QString previousBackgroundHint;
    bool hadBackgroundHint = false, installedBackgroundHint = false, collecting = false;

    void restoreBackgroundHint() {
        const char *currentHint = SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
        if (!installedBackgroundHint || !currentHint || std::string(currentHint) != "1")
            return;
        if (hadBackgroundHint)
            SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                        previousBackgroundHint.toUtf8().constData());
        else
            SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    }

    bool acquire(ControllerBackend *client, QString *error) {
        if (!clients.empty() && thread != QThread::currentThread()) {
            if (error)
                *error =
                    QStringLiteral("Controller devices are in use on another application thread");
            return false;
        }
        if (clients.empty()) {
            SDL_SetMainReady();
            // One process-wide lease keeps physical input current for every window.
            // Delivery remains a per-client choice, independently of native focus.
            const char *previous = SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
            hadBackgroundHint = previous != nullptr;
            previousBackgroundHint = QString::fromUtf8(previous ? previous : "");
            installedBackgroundHint = SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
            if (SDL_InitSubSystem(subsystems) < 0) {
                if (error)
                    *error = QString::fromUtf8(SDL_GetError());
                restoreBackgroundHint();
                return false;
            }
            thread = QThread::currentThread();
        }
        clients.emplace_back(client);
        return true;
    }

    void release(ControllerBackend *client) {
        clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());
        if (!clients.empty())
            return;
        rumbleOwners.clear();
        SDL_QuitSubSystem(subsystems);
        restoreBackgroundHint();
        thread = nullptr;
    }

    QString collect(bool allPending = false) {
        if (clients.empty() || collecting)
            return {};
        Q_ASSERT(thread == QThread::currentThread());
        QScopedValueRollback<bool> active(collecting, true);
        SDL_PumpEvents();
        SDL_JoystickUpdate();
        // SDL has one event queue. Preserve each edge for every live client;
        // callbacks run later, only during that client's own guarded poll.
        // Normal ticks are bounded. A gate transition drains the pending snapshot
        // once so it can suppress its old presses without discarding a peer's edges.
        const std::array<std::pair<Uint32, Uint32>, 2> ranges{
            std::pair<Uint32, Uint32>{SDL_JOYAXISMOTION, SDL_JOYBATTERYUPDATED},
            {SDL_CONTROLLERAXISMOTION, lastControllerEvent}};
        std::array<SDL_Event, 64> events{};
        for (const auto &[first, last] : ranges) {
            size_t remaining = allPending ? std::numeric_limits<size_t>::max() : 512;
            while (remaining) {
                const int requested = int(std::min(remaining, events.size()));
                const int count =
                    SDL_PeepEvents(events.data(), requested, SDL_GETEVENT, first, last);
                if (count < 0)
                    return QString::fromUtf8(SDL_GetError());
                for (const auto &client : clients)
                    if (client && client->initialized_)
                        client->pendingEvents_.insert(client->pendingEvents_.end(), events.begin(),
                                                      events.begin() + count);
                if (count < requested)
                    break;
                remaining -= size_t(count);
            }
        }
        return {};
    }
};

ControllerBackend::SharedTransport &ControllerBackend::transport() {
    static SharedTransport shared;
    return shared;
}

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
    if (!transport().acquire(this, error))
        return false;
    initialized_ = true;
    clock_.start();
    discardPendingEdges();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
    pollNow();
    if (!initialized_) {
        if (error)
            *error = QStringLiteral("Controller polling stopped during startup");
        return false;
    }
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
    initialized_ = false;
    for (auto &[id, device] : devices_) {
        closeDevice(device);
        if (deviceRemoved)
            deviceRemoved(id);
    }
    devices_.clear();
    pendingEvents_.clear();
    transport().release(this);
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
    const QString error = transport().collect(true);
    // A resumed window receives held state without replaying old shortcut presses.
    // Peers retain their copies, and device/remapping metadata remains queued here.
    const auto inputEdge = [](const SDL_Event &event) {
        return (event.type >= SDL_JOYAXISMOTION && event.type <= SDL_JOYBUTTONUP) ||
               (event.type >= SDL_CONTROLLERAXISMOTION && event.type <= SDL_CONTROLLERBUTTONUP);
    };
    pendingEvents_.erase(std::remove_if(pendingEvents_.begin(), pendingEvents_.end(), inputEdge),
                         pendingEvents_.end());
    if (!error.isEmpty() && errorOccurred)
        errorOccurred(error);
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
    auto &owners = transport().rumbleOwners;
    const auto previous = owners.find(id);
    if (previous != owners.end() && previous->second != this) {
        const auto stale = previous->second->devices_.find(id);
        if (stale != previous->second->devices_.end()) {
            stale->second.state.rumbleActive = false;
            stale->second.rumbleUntil = 0;
        }
    }
    owners[id] = this;
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
        auto &owners = transport().rumbleOwners;
        const auto owner = owners.find(id);
        // A newer command from another window owns the physical motor. Expiring
        // or closing the previous window must not cancel that command.
        if (owner != owners.end() && owner->second == this) {
            SDL_JoystickRumble(device.joystick, 0, 0, 0);
            owners.erase(owner);
        }
        // Unplug may reject the native stop. Never replay intent on reconnect.
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

void ControllerBackend::pollNow() {
    assertThread();
    if (!initialized_ || polling_)
        return;
    QScopedValueRollback<bool> polling(polling_, true);
    if (beforePoll)
        beforePoll();
    if (!initialized_)
        return;
    const QString error = transport().collect();
    reconcileDevices();
    if (!error.isEmpty() && errorOccurred)
        errorOccurred(error);
    // Pop before callback delivery so a gate transition can remove the remaining
    // old edges even when it occurs inside an input callback.
    for (unsigned budget = 0; initialized_ && !pendingEvents_.empty() && budget < 1024; ++budget) {
        const SDL_Event event = pendingEvents_.front();
        pendingEvents_.pop_front();
        dispatch(event);
    }
    if (!initialized_)
        return;
    for (auto &[id, device] : devices_) {
        update(device);
        if (device.state.rumbleActive && clock_.elapsed() >= device.rumbleUntil)
            stopRumble(id);
    }
}

} // namespace cupid::input

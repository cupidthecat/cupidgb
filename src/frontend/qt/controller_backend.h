/**
 * @file    controller_backend.h
 * @brief   Controller device records and input/motor transport interface.
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

#pragma once

#define SDL_MAIN_HANDLED
#ifdef _MSC_VER
// Some Windows SDKs ship a MinGW configuration. MSVC does not provide strings.h.
#include <SDL_config.h>
#undef HAVE_STRINGS_H
#endif
#include <SDL.h>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

// Device transport stays independent of cartridge state and gameplay bindings.
namespace cupid::input {

struct DeviceInfo {
    SDL_JoystickID instance = -1;
    QString name, guid, path, serial;
    QString profileKey, persistentKey;
    enum class Identity { DeviceClass, ConnectionPath, Serial } identity = Identity::DeviceClass;
    uint16_t vendor = 0, product = 0, version = 0;
    uint32_t controllerAxisMask = 0, controllerButtonMask = 0;
    int axes = 0, buttons = 0, hats = 0;
    bool mapped = false, rumble = false, accelerometer = false, gyroscope = false;
};

struct InputState {
    std::vector<int16_t> axes;
    std::vector<uint8_t> buttons, hats;
    std::vector<int16_t> controllerAxes;
    std::vector<uint8_t> controllerButtons;
    bool allowed = false;
    bool operator==(const InputState &other) const;

    bool operator!=(const InputState &other) const {
        return !(*this == other);
    }
};

struct DeviceState {
    DeviceInfo info;
    InputState physical, effective;
    bool rumbleActive = false;
};

struct InputEvent {
    enum class Kind { Axis, Button, Hat, ControllerAxis, ControllerButton } kind;
    SDL_JoystickID instance = -1;
    int control = 0, value = 0;
    bool allowed = false;
};

class ControllerBackend final : public QObject {
  public:
    explicit ControllerBackend(QObject *parent = nullptr);
    ~ControllerBackend() override;
    bool start(QString *error = nullptr);
    void stop();
    void pollNow();
    bool running() const;
    std::vector<DeviceState> devices() const;
    bool state(SDL_JoystickID instance, DeviceState *result) const;
    void setFocused(bool focused);
    void setInputEnabled(bool enabled);
    void setAllowBackground(bool enabled);
    bool inputAllowed() const;
    bool rumble(SDL_JoystickID instance, uint16_t low, uint16_t high, unsigned durationMs,
                bool explicitTest = false);
    void stopRumble(SDL_JoystickID instance);

    // All callbacks run synchronously on this object's thread. Queue destructive/reentrant work.
    // The gate callback runs before event delivery and may stop or request another poll.
    std::function<void()> beforePoll;
    std::function<void(const DeviceInfo &)> deviceAdded, deviceChanged;
    std::function<void(SDL_JoystickID)> deviceRemoved, inputReleased;
    std::function<void(const DeviceState &, bool resynchronized)> stateChanged;
    std::function<void(const InputEvent &)> inputEvent;
    std::function<void(const QString &)> errorOccurred;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    struct Device {
        SDL_Joystick *joystick = nullptr;
        SDL_GameController *controller = nullptr;
        DeviceState state;
        qint64 rumbleUntil = 0;
    };

    void reconcileDevices();
    void describe(Device &device);
    void update(Device &device, bool resynchronized = false);
    void closeDevice(Device &device);
    void updateGate();
    void discardPendingEdges();
    void drainEvents(Uint32 first, Uint32 last);
    void dispatch(const SDL_Event &event);
    void assertThread() const;

    std::map<SDL_JoystickID, Device> devices_;
    QTimer timer_;
    QElapsedTimer clock_;
    QString previousBackgroundHint_;
    bool initialized_ = false, polling_ = false;
    bool hadBackgroundHint_ = false, installedBackgroundHint_ = false;
    bool focused_ = true, enabled_ = true, background_ = false;
};

} // namespace cupid::input

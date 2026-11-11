#pragma once

#include "controller_backend.h"

#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <array>

namespace cupid::input {

struct Binding {
    enum Kind { Button, Axis, Hat, ControllerButton, ControllerAxis } kind = Button;

    int control = 0;
    int direction = 1;
    bool operator==(const Binding &other) const;
};

struct ControllerProfile {
    int player = 0;
    int pressThreshold = 16384, releaseThreshold = 14336;
    bool enabled = true;
    QMap<QString, QList<Binding>> bindings;
    bool operator==(const ControllerProfile &other) const;
};

struct ControllerConfiguration {
    bool enabled = true, background = false, rumble = true;
    int rumbleStrength = 100;
    QString preferredDevice;
    QMap<QString, ControllerProfile> profiles;
    QByteArray save() const;
    static ControllerConfiguration load(const QByteArray &bytes, bool *valid = nullptr);
};

struct ControllerOutput {
    std::array<uint8_t, 4> buttons{0xff, 0xff, 0xff, 0xff};
    std::array<bool, 5> held{}; // Turbo, rewind, slow motion, rapid A, rapid B.
    std::array<std::array<bool, 2>, 4> rapid{};
    bool operator==(const ControllerOutput &other) const;
};

class ControllerManager final : public QObject {
  public:
    explicit ControllerManager(QObject *parent = nullptr);
    ~ControllerManager() override;
    ControllerBackend &backend();
    const ControllerConfiguration &configuration() const;
    void configure(const ControllerConfiguration &configuration,
                   const QMap<SDL_JoystickID, int> &players = {});
    ControllerProfile profile(const DeviceInfo &device) const;
    static ControllerProfile defaults(const DeviceInfo &device);
    static QStringList gameplayActions();
    static QString actionName(const QString &action);
    static QString bindingName(const Binding &binding);
    static bool bindingFromEvent(const InputEvent &event, int threshold, Binding *binding);
    static bool bindingDown(const Binding &binding, const InputState &state,
                            const ControllerProfile &profile, bool previous);
    ControllerOutput output() const;
    void setDeliveryEnabled(bool enabled);
    bool deliveryEnabled() const;
    void setFocused(bool focused);
    void setPreferredInstance(SDL_JoystickID instance);
    QMap<SDL_JoystickID, int> playerOverrides() const;
    void setRumble(double amplitude, bool singlePlayer = false);
    void stopRumble();

    std::function<void(const ControllerOutput &)> outputChanged;
    std::function<void(const QString &)> shortcutPressed;
    std::function<void()> devicesChanged;
    std::function<void(const InputEvent &)> captureEvent;
    std::function<void(const InputEvent &)> physicalEvent;
    std::function<void(const QString &, SDL_JoystickID)> navigationPressed;

  private:
    struct State {
        DeviceInfo info;
        ControllerProfile fallback;
        InputState physical;
        QMap<QString, QList<bool>> bindingDown;
        QMap<QString, QList<bool>> navigationBindings;
        QMap<QString, bool> actions;
        QMap<QString, bool> navigationActions;
    };

    void receiveEvent(const InputEvent &event);
    void receiveState(const DeviceState &state);
    void evaluate(SDL_JoystickID instance, bool shortcuts, bool navigation = false);
    void aggregate();
    bool eligible(const State &state) const;
    bool selected(const State &state) const;
    ControllerBackend backend_;
    ControllerConfiguration configuration_;
    std::map<SDL_JoystickID, State> states_;
    QMap<SDL_JoystickID, int> players_;
    SDL_JoystickID preferredInstance_ = -1;
    ControllerOutput output_;
    QElapsedTimer rumbleClock_;
    qint64 lastRumbleRefresh_ = -250;
    bool focused_ = true;
    bool delivery_ = true;
};

} // namespace cupid::input

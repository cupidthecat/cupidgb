#include "controller_manager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace cupid::input {

namespace {

bool validMotionAxis(const std::optional<MotionAxis> &axis) {
    return !axis ||
           (axis->kind >= MotionAxis::Unassigned && axis->kind <= MotionAxis::Controller &&
            axis->control >= 0 && axis->control <= 4095 &&
            (axis->kind != MotionAxis::Controller || axis->control < SDL_CONTROLLER_AXIS_MAX));
}

MotionAxis resolvedMotionAxis(const ControllerProfile &profile, const DeviceInfo &device, bool y) {
    const auto &selection = y ? profile.motionY : profile.motionX;
    if (selection)
        return *selection;
    const int control = y ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_LEFTX;
    if (device.mapped)
        return device.controllerAxisMask & (1u << control)
                   ? MotionAxis{MotionAxis::Controller, control}
                   : MotionAxis{};
    return control < device.axes ? MotionAxis{MotionAxis::Raw, control} : MotionAxis{};
}

bool sameMotionAxis(MotionAxis first, MotionAxis second, const DeviceInfo &device) {
    if (first.kind == MotionAxis::Unassigned || second.kind == MotionAxis::Unassigned)
        return false;
    if (first.kind == second.kind && first.control == second.control)
        return true;
    auto *controller = SDL_GameControllerFromInstanceID(device.instance);
    if (!controller)
        return false;
    const auto physical = [&](MotionAxis axis) {
        SDL_GameControllerButtonBind binding{};
        if (axis.kind == MotionAxis::Raw) {
            binding.bindType = SDL_CONTROLLER_BINDTYPE_AXIS;
            binding.value.axis = axis.control;
        } else if (axis.control >= 0 && axis.control < SDL_CONTROLLER_AXIS_MAX) {
            binding =
                SDL_GameControllerGetBindForAxis(controller, SDL_GameControllerAxis(axis.control));
        }
        return binding;
    };
    const auto a = physical(first), b = physical(second);
    if (a.bindType != b.bindType)
        return false;
    switch (a.bindType) {
        case SDL_CONTROLLER_BINDTYPE_AXIS:
            return a.value.axis == b.value.axis;
        case SDL_CONTROLLER_BINDTYPE_BUTTON:
            return a.value.button == b.value.button;
        case SDL_CONTROLLER_BINDTYPE_HAT:
            return a.value.hat.hat == b.value.hat.hat &&
                   a.value.hat.hat_mask == b.value.hat.hat_mask;
        default:
            return false;
    }
}

std::optional<int16_t> motionAxisValue(MotionAxis axis, const DeviceInfo &device,
                                       const InputState &state) {
    if (axis.kind == MotionAxis::Unassigned || axis.control < 0)
        return {};
    if (axis.kind == MotionAxis::Controller &&
        (!device.mapped || axis.control >= SDL_CONTROLLER_AXIS_MAX ||
         !(device.controllerAxisMask & (1u << axis.control))))
        return {};
    const auto &values = axis.kind == MotionAxis::Raw ? state.axes : state.controllerAxes;
    return size_t(axis.control) < values.size()
               ? std::optional<int16_t>(values[size_t(axis.control)])
               : std::nullopt;
}

bool motionAxisBinding(const Binding &binding, const ControllerProfile &profile,
                       const DeviceInfo &device) {
    if (binding.kind != Binding::Axis && binding.kind != Binding::ControllerAxis)
        return false;
    const MotionAxis axis{binding.kind == Binding::Axis ? MotionAxis::Raw : MotionAxis::Controller,
                          binding.control};
    return sameMotionAxis(axis, resolvedMotionAxis(profile, device, false), device) ||
           sameMotionAxis(axis, resolvedMotionAxis(profile, device, true), device);
}

} // namespace

bool Binding::operator==(const Binding &other) const {
    return kind == other.kind && control == other.control && direction == other.direction;
}

bool MotionAxis::operator==(const MotionAxis &other) const {
    return kind == other.kind && control == other.control;
}

bool ControllerProfile::operator==(const ControllerProfile &other) const {
    return player == other.player && pressThreshold == other.pressThreshold &&
           releaseThreshold == other.releaseThreshold && enabled == other.enabled &&
           bindings == other.bindings && motionX == other.motionX && motionY == other.motionY;
}

bool ControllerOutput::operator==(const ControllerOutput &other) const {
    return buttons == other.buttons && held == other.held && rapid == other.rapid &&
           motionAvailable == other.motionAvailable && motionX == other.motionX &&
           motionY == other.motionY;
}

QByteArray ControllerConfiguration::save() const {
    QJsonObject root{
        {"version", 1},     {"enabled", enabled},         {"background", background},
        {"rumble", rumble}, {"strength", rumbleStrength}, {"preferred", preferredDevice}};
    QJsonObject savedProfiles;
    for (auto it = profiles.cbegin(); it != profiles.cend(); ++it) {
        const auto &profile = it.value();
        QJsonObject actions;
        for (auto action = profile.bindings.cbegin(); action != profile.bindings.cend(); ++action) {
            QJsonArray bindings;
            for (const auto &binding : action.value())
                bindings.append(QJsonObject{{"kind", int(binding.kind)},
                                            {"control", binding.control},
                                            {"direction", binding.direction}});
            actions.insert(action.key(), bindings);
        }
        QJsonObject saved{{"player", profile.player},
                          {"press", profile.pressThreshold},
                          {"release", profile.releaseThreshold},
                          {"enabled", profile.enabled},
                          {"bindings", actions}};
        for (const auto &entry : {std::make_pair("motionX", profile.motionX),
                                  std::make_pair("motionY", profile.motionY)})
            if (entry.second)
                saved.insert(entry.first, QJsonObject{{"kind", int(entry.second->kind)},
                                                      {"control", entry.second->control}});
        savedProfiles.insert(it.key(), saved);
    }
    root.insert("profiles", savedProfiles);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

ControllerConfiguration ControllerConfiguration::load(const QByteArray &bytes, bool *valid) {
    if (valid)
        *valid = true;
    ControllerConfiguration result;
    if (bytes.isEmpty())
        return result;
    if (bytes.size() > 1024 * 1024) {
        if (valid)
            *valid = false;
        return result;
    }
    const auto document = QJsonDocument::fromJson(bytes);
    const auto root = document.object();
    const auto reject = [&] {
        if (valid)
            *valid = false;
        return ControllerConfiguration{};
    };
    if (!document.isObject() || root.value("version").toInt(-1) != 1 ||
        !root.value("enabled").isBool() || !root.value("background").isBool() ||
        !root.value("rumble").isBool() || !root.value("profiles").isObject())
        return reject();
    result.enabled = root.value("enabled").toBool();
    result.background = root.value("background").toBool();
    result.rumble = root.value("rumble").toBool();
    result.rumbleStrength = root.value("strength").toInt(-1);
    result.preferredDevice = root.value("preferred").toString();
    if (result.rumbleStrength < 0 || result.rumbleStrength > 100)
        return reject();
    const auto profiles = root.value("profiles").toObject();
    if (profiles.size() > 128)
        return reject();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        const auto object = it.value().toObject();
        ControllerProfile profile;
        profile.player = object.value("player").toInt(-1);
        profile.pressThreshold = object.value("press").toInt(-1);
        profile.releaseThreshold = object.value("release").toInt(-1);
        profile.enabled = object.value("enabled").toBool(true);
        for (const auto &entry : {std::make_pair("motionX", &profile.motionX),
                                  std::make_pair("motionY", &profile.motionY)}) {
            if (!object.contains(entry.first))
                continue;
            const auto value = object.value(entry.first);
            if (!value.isObject())
                return reject();
            const auto axis = value.toObject();
            const int kind = axis.value("kind").toInt(-1);
            const int control = axis.value("control").toInt(-1);
            if (kind < MotionAxis::Unassigned || kind > MotionAxis::Controller)
                return reject();
            *entry.second = MotionAxis{MotionAxis::Kind(kind), control};
            if (!validMotionAxis(*entry.second))
                return reject();
        }
        if (profile.motionX && profile.motionY && profile.motionX->kind != MotionAxis::Unassigned &&
            *profile.motionX == *profile.motionY)
            return reject();
        if (it.key().isEmpty() || it.key().size() > 2048 || profile.player < 0 ||
            profile.player > 3 || profile.pressThreshold < 1 || profile.pressThreshold > 32766 ||
            profile.releaseThreshold < 0 || profile.releaseThreshold > profile.pressThreshold ||
            !object.value("bindings").isObject() || !object.value("enabled").isBool())
            return reject();
        const auto actions = object.value("bindings").toObject();
        if (actions.size() > 128)
            return reject();
        for (auto action = actions.begin(); action != actions.end(); ++action) {
            if (!action.value().isArray() || action.value().toArray().size() > 8 ||
                (!ControllerManager::gameplayActions().contains(action.key()) &&
                 !action.key().startsWith("shortcut/")))
                return reject();
            QList<Binding> bindings;
            for (const auto &entry : action.value().toArray()) {
                const auto item = entry.toObject();
                Binding binding;
                const int kind = item.value("kind").toInt(-1);
                if (kind < Binding::Button || kind > Binding::ControllerAxis)
                    return reject();
                binding.kind = Binding::Kind(kind);
                binding.control = item.value("control").toInt(-1);
                binding.direction = item.value("direction").toInt();
                if (binding.kind < Binding::Button || binding.kind > Binding::ControllerAxis ||
                    binding.control < 0 || binding.control > 4095 ||
                    (binding.kind == Binding::ControllerButton &&
                     binding.control >= SDL_CONTROLLER_BUTTON_MAX) ||
                    (binding.kind == Binding::ControllerAxis &&
                     binding.control >= SDL_CONTROLLER_AXIS_MAX) ||
                    ((binding.kind == Binding::Axis || binding.kind == Binding::ControllerAxis) &&
                     binding.direction != -1 && binding.direction != 1) ||
                    (binding.kind == Binding::Hat && binding.direction != SDL_HAT_UP &&
                     binding.direction != SDL_HAT_RIGHT && binding.direction != SDL_HAT_DOWN &&
                     binding.direction != SDL_HAT_LEFT))
                    return reject();
                bindings.push_back(binding);
            }
            profile.bindings.insert(action.key(), bindings);
        }
        result.profiles.insert(it.key(), profile);
    }
    return result;
}

ControllerManager::ControllerManager(QObject *parent) : QObject(parent) {
    rumbleClock_.start();
    backend_.deviceAdded = [this](const DeviceInfo &info) {
        states_[info.instance].info = info;
        states_[info.instance].fallback = defaults(info);
        if (devicesChanged)
            devicesChanged();
    };
    backend_.deviceChanged = [this](const DeviceInfo &info) {
        states_[info.instance].info = info;
        states_[info.instance].fallback = defaults(info);
        states_[info.instance].bindingDown.clear();
        if (devicesChanged)
            devicesChanged();
    };
    backend_.deviceRemoved = [this](SDL_JoystickID id) {
        states_.erase(id);
        players_.remove(id);
        if (preferredInstance_ == id)
            preferredInstance_ = -1;
        aggregate();
        if (devicesChanged)
            devicesChanged();
    };
    backend_.stateChanged = [this](const DeviceState &state, bool) { receiveState(state); };
    backend_.inputEvent = [this](const InputEvent &event) {
        receiveEvent(event);
        if (captureEvent)
            captureEvent(event);
        if (physicalEvent)
            physicalEvent(event);
    };
}

ControllerManager::~ControllerManager() {
    backend_.deviceAdded = {};
    backend_.deviceChanged = {};
    backend_.deviceRemoved = {};
    backend_.stateChanged = {};
    backend_.inputEvent = {};
    backend_.stop();
}

ControllerBackend &ControllerManager::backend() {
    return backend_;
}

const ControllerConfiguration &ControllerManager::configuration() const {
    return configuration_;
}

void ControllerManager::configure(const ControllerConfiguration &configuration,
                                  const QMap<SDL_JoystickID, int> &players) {
    for (const auto &profile : configuration.profiles)
        if (!validMotionAxis(profile.motionX) || !validMotionAxis(profile.motionY) ||
            (profile.motionX && profile.motionY &&
             profile.motionX->kind != MotionAxis::Unassigned &&
             *profile.motionX == *profile.motionY))
            return;
    for (const auto &[id, state] : states_) {
        (void)id;
        const auto saved = configuration.profiles.constFind(state.info.persistentKey);
        if (saved != configuration.profiles.cend() && !motionAxesValid(saved.value(), state.info))
            return;
    }
    stopRumble();
    configuration_ = configuration;
    players_ = players;
    const auto previousPreferred = states_.find(preferredInstance_);
    if (previousPreferred == states_.end() ||
        previousPreferred->second.info.persistentKey != configuration_.preferredDevice)
        preferredInstance_ = -1;
    backend_.setAllowBackground(configuration_.background);
    backend_.setInputEnabled(delivery_ && configuration_.enabled);
    for (auto &[id, state] : states_) {
        state.bindingDown.clear();
        state.actions.clear();
        evaluate(id, false);
    }
    aggregate();
}

ControllerProfile ControllerManager::profile(const DeviceInfo &device) const {
    const auto saved = configuration_.profiles.constFind(device.persistentKey);
    const auto state = states_.find(device.instance);
    auto profile = saved != configuration_.profiles.cend() ? saved.value()
                   : state != states_.end()                ? state->second.fallback
                                                           : defaults(device);
    profile.player = players_.value(device.instance, profile.player);
    profile.motionX = resolvedMotionAxis(profile, device, false);
    profile.motionY = resolvedMotionAxis(profile, device, true);
    return profile;
}

bool ControllerManager::motionAxesValid(const ControllerProfile &profile,
                                        const DeviceInfo &device) {
    if (!validMotionAxis(profile.motionX) || !validMotionAxis(profile.motionY))
        return false;
    const auto x = resolvedMotionAxis(profile, device, false);
    const auto y = resolvedMotionAxis(profile, device, true);
    const auto available = [&](MotionAxis axis) {
        return axis.kind == MotionAxis::Unassigned ||
               (axis.kind == MotionAxis::Raw && axis.control < device.axes) ||
               (axis.kind == MotionAxis::Controller && device.mapped &&
                (device.controllerAxisMask & (1u << axis.control)));
    };
    return available(x) && available(y) && !sameMotionAxis(x, y, device);
}

int16_t ControllerManager::motionCounts(int16_t raw) {
    // The positive hardware center is quantized after adding the scaled offset.
    // Fractional negative offsets therefore round down, including a raw value of +1.
    const int32_t scaled = -int32_t(raw) * 112;
    return int16_t(scaled >= 0 ? scaled / 32768 : -((-scaled + 32767) / 32768));
}

QStringList ControllerManager::gameplayActions() {
    return {"right", "left",  "up",     "down", "a",      "b",      "select",
            "start", "turbo", "rewind", "slow", "rapidA", "rapidB", "menu"};
}

QString ControllerManager::actionName(const QString &action) {
    const QStringList names{"Right",       "Left",    "Up",      "Down",  "A",
                            "B",           "Select",  "Start",   "Turbo", "Rewind",
                            "Slow motion", "Rapid A", "Rapid B", "Menu"};
    const int index = gameplayActions().indexOf(action);
    return index < 0 ? action : names[index];
}

ControllerProfile ControllerManager::defaults(const DeviceInfo &device) {
    ControllerProfile profile;
    const auto actions = gameplayActions();
    for (const auto &action : actions)
        profile.bindings.insert(action, {});
    const bool mapped = device.mapped;
    profile.motionX = resolvedMotionAxis(profile, device, false);
    profile.motionY = resolvedMotionAxis(profile, device, true);
    const auto button = mapped ? Binding::ControllerButton : Binding::Button;
    const auto axis = mapped ? Binding::ControllerAxis : Binding::Axis;
    const int buttons[] = {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
                           SDL_CONTROLLER_BUTTON_DPAD_UP,    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                           SDL_CONTROLLER_BUTTON_A,          SDL_CONTROLLER_BUTTON_B,
                           SDL_CONTROLLER_BUTTON_BACK,       SDL_CONTROLLER_BUTTON_START};
    for (int i = 0; i < 8; ++i) {
        if (mapped)
            profile.bindings[actions[i]].append({button, buttons[i], 1});
        else if (i >= 4)
            profile.bindings[actions[i]].append({button, i == 6 ? 4 : i == 7 ? 6 : i - 4, 1});
    }
    for (int i = 0; i < 4; ++i) {
        profile.bindings[actions[i]].append({axis, i < 2 ? 0 : 1, i == 0 || i == 3 ? 1 : -1});
        if (!mapped)
            profile.bindings[actions[i]].append({Binding::Hat, 0,
                                                 i == 0   ? SDL_HAT_RIGHT
                                                 : i == 1 ? SDL_HAT_LEFT
                                                 : i == 2 ? SDL_HAT_UP
                                                          : SDL_HAT_DOWN});
    }
    if (mapped) {
        profile.bindings["turbo"].append({button, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1});
        profile.bindings["rewind"].append({button, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1});
        profile.bindings["menu"].append({button, SDL_CONTROLLER_BUTTON_GUIDE, 1});
    }
    return profile;
}

QString ControllerManager::bindingName(const Binding &binding) {
    if (binding.kind == Binding::ControllerButton) {
        if (binding.control < 0 || binding.control >= SDL_CONTROLLER_BUTTON_MAX)
            return "Unavailable controller button";
        const std::array<const char *, 21> names{"A",
                                                 "B",
                                                 "X",
                                                 "Y",
                                                 "Back",
                                                 "Guide",
                                                 "Start",
                                                 "Left stick press",
                                                 "Right stick press",
                                                 "Left shoulder",
                                                 "Right shoulder",
                                                 "D-pad up",
                                                 "D-pad down",
                                                 "D-pad left",
                                                 "D-pad right",
                                                 "Miscellaneous",
                                                 "Paddle 1",
                                                 "Paddle 2",
                                                 "Paddle 3",
                                                 "Paddle 4",
                                                 "Touchpad press"};
        return size_t(binding.control) < names.size()
                   ? QString::fromLatin1(names[size_t(binding.control)])
                   : QString("Controller button %1").arg(binding.control);
    }
    if (binding.kind == Binding::ControllerAxis) {
        if (binding.control < 0 || binding.control >= SDL_CONTROLLER_AXIS_MAX)
            return "Unavailable controller axis";
        const std::array<const char *, 6> names{"Left stick X",  "Left stick Y", "Right stick X",
                                                "Right stick Y", "Left trigger", "Right trigger"};
        return QString("%1 %2").arg(size_t(binding.control) < names.size()
                                        ? QString::fromLatin1(names[size_t(binding.control)])
                                        : QString("Controller axis %1").arg(binding.control),
                                    binding.direction > 0 ? "+" : "-");
    }
    if (binding.kind == Binding::Button)
        return QString("Button %1").arg(binding.control);
    if (binding.kind == Binding::Axis)
        return QString("Axis %1 %2").arg(binding.control).arg(binding.direction > 0 ? "+" : "-");
    return QString("Hat %1 %2")
        .arg(binding.control)
        .arg(binding.direction == SDL_HAT_UP     ? "up"
             : binding.direction == SDL_HAT_DOWN ? "down"
             : binding.direction == SDL_HAT_LEFT ? "left"
                                                 : "right");
}

bool ControllerManager::bindingFromEvent(const InputEvent &event, int threshold, Binding *binding) {
    if (!binding || event.control < 0 || event.control > 4095)
        return false;
    if (event.kind == InputEvent::Kind::Button ||
        event.kind == InputEvent::Kind::ControllerButton) {
        if (!event.value)
            return false;
        *binding = {event.kind == InputEvent::Kind::Button ? Binding::Button
                                                           : Binding::ControllerButton,
                    event.control, 1};
    } else if (event.kind == InputEvent::Kind::Axis ||
               event.kind == InputEvent::Kind::ControllerAxis) {
        if (std::abs(event.value) <= threshold)
            return false;
        *binding = {event.kind == InputEvent::Kind::Axis ? Binding::Axis : Binding::ControllerAxis,
                    event.control, event.value > 0 ? 1 : -1};
    } else {
        if (event.value != SDL_HAT_UP && event.value != SDL_HAT_DOWN &&
            event.value != SDL_HAT_LEFT && event.value != SDL_HAT_RIGHT)
            return false;
        *binding = {Binding::Hat, event.control, event.value};
    }
    return true;
}

bool ControllerManager::bindingDown(const Binding &binding, const InputState &state,
                                    const ControllerProfile &profile, bool previous) {
    const auto index = size_t(binding.control);
    if (binding.kind == Binding::Button)
        return index < state.buttons.size() && state.buttons[index];
    if (binding.kind == Binding::ControllerButton)
        return index < state.controllerButtons.size() && state.controllerButtons[index];
    if (binding.kind == Binding::Hat)
        return index < state.hats.size() && (state.hats[index] & binding.direction) != 0;
    const auto &axes = binding.kind == Binding::Axis ? state.axes : state.controllerAxes;
    if (index >= axes.size())
        return false;
    const int value = int(axes[index]) * binding.direction;
    return previous ? value > 0 && value >= profile.releaseThreshold
                    : value > profile.pressThreshold;
}

void ControllerManager::receiveEvent(const InputEvent &event) {
    auto it = states_.find(event.instance);
    if (it == states_.end())
        return;
    auto &state = it->second.physical;
    const auto put = [&](auto &values) {
        if (event.control >= 0 && size_t(event.control) < values.size())
            values[size_t(event.control)] =
                typename std::decay_t<decltype(values)>::value_type(event.value);
    };
    switch (event.kind) {
        case InputEvent::Kind::Axis:
            put(state.axes);
            break;
        case InputEvent::Kind::Button:
            put(state.buttons);
            break;
        case InputEvent::Kind::Hat:
            put(state.hats);
            break;
        case InputEvent::Kind::ControllerAxis:
            put(state.controllerAxes);
            break;
        case InputEvent::Kind::ControllerButton:
            put(state.controllerButtons);
            break;
    }
    evaluate(event.instance, event.allowed, true);
}

void ControllerManager::receiveState(const DeviceState &state) {
    auto &current = states_[state.info.instance];
    current.info = state.info;
    current.physical = state.physical;
    evaluate(state.info.instance, false);
}

bool ControllerManager::eligible(const State &state) const {
    if (!delivery_ || !configuration_.enabled || !backend_.inputAllowed() ||
        !profile(state.info).enabled)
        return false;
    return selected(state);
}

bool ControllerManager::motionReady(const State &state) const {
    const auto selectedProfile = profile(state.info);
    return eligible(state) && selectedProfile.player >= 0 && selectedProfile.player < 4 &&
           motionAxesValid(selectedProfile, state.info) &&
           (motionAxisValue(*selectedProfile.motionX, state.info, state.physical) ||
            motionAxisValue(*selectedProfile.motionY, state.info, state.physical));
}

bool ControllerManager::selected(const State &state) const {
    if (configuration_.preferredDevice.isEmpty())
        return true;
    if (preferredInstance_ >= 0 && states_.count(preferredInstance_))
        return state.info.instance == preferredInstance_;
    for (const auto &[id, candidate] : states_)
        if (candidate.info.persistentKey == configuration_.preferredDevice)
            return state.info.instance == id;
    return false;
}

void ControllerManager::evaluate(SDL_JoystickID id, bool shortcuts, bool navigation) {
    auto &state = states_.at(id);
    const auto selected = profile(state.info);
    for (auto action = selected.bindings.cbegin(); action != selected.bindings.cend(); ++action) {
        auto &down = state.bindingDown[action.key()];
        down.resize(action.value().size());
        bool pressed = false;
        auto &navigationDown = state.navigationBindings[action.key()];
        navigationDown.resize(action.value().size());
        bool navigationHeld = false;
        for (int i = 0; i < action.value().size(); ++i) {
            const bool direction = action.key() == "right" || action.key() == "left" ||
                                   action.key() == "up" || action.key() == "down";
            const bool motion = motionEnabled_ && direction &&
                                motionAxisBinding(action.value()[i], selected, state.info);
            down[i] = !motion && eligible(state) &&
                      bindingDown(action.value()[i], state.physical, selected, down[i]);
            pressed |= down[i];
            navigationDown[i] =
                bindingDown(action.value()[i], state.physical, selected, navigationDown[i]);
            navigationHeld |= navigationDown[i];
        }
        const bool navigationWasHeld = state.navigationActions.value(action.key());
        state.navigationActions[action.key()] = navigationHeld;
        if (navigation && navigationHeld && !navigationWasHeld && configuration_.enabled &&
            selected.enabled && this->selected(state) && navigationPressed)
            navigationPressed(action.key(), id);
        const bool previous = state.actions.value(action.key());
        state.actions[action.key()] = pressed;
        if (pressed && !previous && shortcuts && shortcutPressed &&
            (action.key() == "menu" || action.key().startsWith("shortcut/")))
            shortcutPressed(action.key() == "menu" ? "menu" : action.key().mid(9));
    }
    aggregate();
}

void ControllerManager::aggregate() {
    ControllerOutput result;
    if (motionEnabled_)
        for (const auto &[id, state] : states_)
            if (motionReady(state)) {
                (void)id;
                const auto selectedProfile = profile(state.info);
                result.motionAvailable = true;
                result.motionX = motionCounts(
                    motionAxisValue(*selectedProfile.motionX, state.info, state.physical)
                        .value_or(0));
                result.motionY = motionCounts(
                    motionAxisValue(*selectedProfile.motionY, state.info, state.physical)
                        .value_or(0));
                break;
            }
    const auto actions = gameplayActions();
    for (const auto &[id, state] : states_) {
        (void)id;
        if (!eligible(state))
            continue;
        const int player = profile(state.info).player;
        if (player < 0 || player > 3)
            continue;
        for (int i = 0; i < 8; ++i)
            if (state.actions.value(actions[i]))
                result.buttons[size_t(player)] &= uint8_t(~(1u << i));
        for (int i = 0; i < 5; ++i)
            result.held[size_t(i)] |= state.actions.value(actions[i + 8]);
        for (int i = 0; i < 2; ++i)
            result.rapid[size_t(player)][size_t(i)] |= state.actions.value(actions[i + 11]);
    }
    if (!(result == output_)) {
        output_ = result;
        if (outputChanged)
            outputChanged(output_);
    }
}

ControllerOutput ControllerManager::output() const {
    return output_;
}

void ControllerManager::setDeliveryEnabled(bool enabled) {
    if (delivery_ == enabled)
        return;
    delivery_ = enabled;
    backend_.setInputEnabled(enabled && configuration_.enabled);
    for (auto &[id, state] : states_) {
        state.actions.clear();
        state.bindingDown.clear();
        evaluate(id, false);
    }
    aggregate();
}

bool ControllerManager::deliveryEnabled() const {
    return delivery_ && configuration_.enabled && backend_.inputAllowed();
}

void ControllerManager::setMotionEnabled(bool enabled) {
    if (motionEnabled_ == enabled)
        return;
    motionEnabled_ = enabled;
    for (auto &[id, state] : states_) {
        state.bindingDown.clear();
        state.actions.clear();
        evaluate(id, false);
    }
    aggregate();
}

bool ControllerManager::motionEnabled() const {
    return motionEnabled_;
}

void ControllerManager::setFocused(bool focused) {
    backend_.setFocused(focused);
    if (focused_ == focused)
        return;
    focused_ = focused;
    if (!focused)
        stopRumble();
}

void ControllerManager::setPreferredInstance(SDL_JoystickID instance) {
    preferredInstance_ = instance;
    for (auto &[id, state] : states_) {
        (void)state;
        evaluate(id, false);
    }
}

QMap<SDL_JoystickID, int> ControllerManager::playerOverrides() const {
    return players_;
}

void ControllerManager::setRumble(double amplitude, bool singlePlayer) {
    if (!delivery_ || !configuration_.enabled || !configuration_.rumble ||
        !std::isfinite(amplitude)) {
        stopRumble();
        return;
    }
    const auto value = uint16_t(std::clamp(amplitude, 0.0, 1.0) *
                                std::clamp(configuration_.rumbleStrength, 0, 100) / 100.0 * 65535);
    const qint64 now = rumbleClock_.elapsed();
    // Turbo can complete many guest frames in one host refresh. Keep motor requests bounded.
    if (value && now - lastRumbleRefresh_ < 16)
        return;
    lastRumbleRefresh_ = now;
    for (const auto &[id, state] : states_) {
        if (eligible(state) && (singlePlayer || profile(state.info).player == 0) && value)
            backend_.rumble(id, value, value, 250);
        else
            backend_.stopRumble(id);
    }
}

void ControllerManager::stopRumble() {
    lastRumbleRefresh_ = -250;
    for (const auto &state : backend_.devices())
        backend_.stopRumble(state.info.instance);
}

} // namespace cupid::input

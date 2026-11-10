#include "controller_backend.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace cupid::input;

static unsigned checks = 0;

static void require(bool value, const char *name) {
    ++checks;
    std::printf("check=%s result=%s%s%s\n", name, value ? "pass" : "FAIL",
                value ? "" : " error=", value ? "" : SDL_GetError());
    if (!value)
        throw std::runtime_error(name);
}

static bool waitFor(const std::function<bool()> &predicate, int timeout = 1200) {
    if (predicate())
        return true;
    QEventLoop loop;
    QTimer tick, deadline;
    tick.setInterval(1);
    deadline.setSingleShot(true);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        if (predicate())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    tick.start();
    deadline.start(timeout);
    loop.exec();
    return predicate();
}

struct SdlOwner {
    bool initialized = false;

    ~SdlOwner() {
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
    }
};

struct VirtualDevice {
    SDL_JoystickID id = -1;
    SDL_Joystick *joystick = nullptr;
    uint16_t low = 0, high = 0;
    unsigned rumbleCalls = 0;
    bool attached = false;

    int index() const {
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_JoystickGetDeviceInstanceID(i) == id)
                return i;
        return -1;
    }

    static int SDLCALL onRumble(void *user, Uint16 low, Uint16 high) {
        auto &device = *static_cast<VirtualDevice *>(user);
        device.low = low;
        device.high = high;
        ++device.rumbleCalls;
        return 0;
    }

    void attach(bool mapped) {
        SDL_VirtualJoystickDesc desc{};
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = mapped ? SDL_JOYSTICK_TYPE_GAMECONTROLLER : SDL_JOYSTICK_TYPE_UNKNOWN;
        desc.naxes = 6;
        desc.nbuttons = 16;
        desc.nhats = 1;
        desc.vendor_id = 0xCAFE;
        desc.product_id = mapped ? 1 : 2;
        desc.name = mapped ? "CupidGB virtual controller" : "CupidGB raw virtual joystick";
        desc.button_mask = mapped ? 0x7ff : 0;
        desc.axis_mask = mapped ? 0x3f : 0;
        desc.userdata = this;
        desc.Rumble = onRumble;
        const int deviceIndex = SDL_JoystickAttachVirtualEx(&desc);
        require(deviceIndex >= 0, "virtual_attach");
        attached = true;
        id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
        joystick = SDL_JoystickOpen(deviceIndex);
        require(joystick != nullptr, "provider_open");
        if (mapped) {
            char guid[33]{}, mapping[1024]{};
            SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof(guid));
            SDL_snprintf(mapping, sizeof(mapping),
                         "%s,CupidGB virtual controller,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,"
                         "start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,"
                         "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,leftx:a0,lefty:a1,"
                         "rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,",
                         guid);
            require(SDL_GameControllerAddMapping(mapping) >= 0, "provider_mapping");
        }
    }

    void detach() {
        if (attached) {
            require(SDL_JoystickDetachVirtual(index()) == 0, "provider_detach");
            attached = false;
        }
    }

    ~VirtualDevice() {
        if (attached)
            SDL_JoystickDetachVirtual(index());
        if (joystick)
            SDL_JoystickClose(joystick);
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        SDL_version runtime{};
        SDL_GetVersion(&runtime);
        std::printf("qt=%s sdl=%u.%u.%u\n", qVersion(), runtime.major, runtime.minor,
                    runtime.patch);
#ifdef _WIN32
        char module[2048]{};
        GetModuleFileNameA(GetModuleHandleA("SDL2.dll"), module, sizeof(module));
        std::printf("loaded_sdl=%s\n", module);
        GetModuleFileNameA(GetModuleHandleA("Qt6Core.dll"), module, sizeof(module));
        std::printf("loaded_qt=%s\n", module);
#endif
        SDL_SetMainReady();
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SdlOwner provider;
        require(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) ==
                    0,
                "provider_sdl_init");
        provider.initialized = true;
        const int baseline = SDL_NumJoysticks();
        require(baseline >= 0, "initial_device_inventory");
        std::printf("initial_devices=%d physical_controller_qualified=0\n", baseline);
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
        unsigned added = 0, changed = 0, removed = 0, released = 0, resynchronized = 0, errors = 0;
        std::vector<InputEvent> events;
        ControllerBackend backend;
        backend.deviceAdded = [&](const DeviceInfo &) { ++added; };
        backend.deviceChanged = [&](const DeviceInfo &) { ++changed; };
        backend.deviceRemoved = [&](SDL_JoystickID) { ++removed; };
        backend.inputReleased = [&](SDL_JoystickID) { ++released; };
        backend.stateChanged = [&](const DeviceState &, bool resync) {
            if (resync)
                ++resynchronized;
        };
        backend.inputEvent = [&](const InputEvent &event) { events.push_back(event); };
        backend.errorOccurred = [&](const QString &error) {
            ++errors;
            std::printf("backend_error=%s\n", error.toUtf8().constData());
        };
        QString error;
        require(backend.start(&error), "backend_start");
        require(backend.running(), "backend_running");
        require(!(SDL_WasInit(0) & (SDL_INIT_AUDIO | SDL_INIT_VIDEO)), "no_sdl_audio_or_video");
        QTimer heartbeat;
        unsigned beats = 0;
        heartbeat.setInterval(2);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++beats; });
        heartbeat.start();

        auto first = std::make_unique<VirtualDevice>();
        auto second = std::make_unique<VirtualDevice>();
        auto raw = std::make_unique<VirtualDevice>();
        first->attach(true);
        second->attach(true);
        raw->attach(false);
        require(
            waitFor([&] { return backend.devices().size() == static_cast<size_t>(baseline + 3); }),
            "qt_timer_hotplug_three_devices");
        require(beats > 0 && added == static_cast<unsigned>(baseline + 3),
                "qt_event_loop_coexistence");
        DeviceState a, b, c;
        require(backend.state(first->id, &a) && backend.state(second->id, &b) &&
                    backend.state(raw->id, &c),
                "runtime_id_lookup");
        require(a.info.instance != b.info.instance && a.info.profileKey == b.info.profileKey,
                "identical_devices_distinct_instances_shared_profile");
        require(!a.info.profileKey.isEmpty() && !a.info.persistentKey.isEmpty(), "identity_keys");
        require(a.info.mapped && b.info.mapped && !c.info.mapped, "mapped_and_raw_fallback");
        require(a.info.axes == 6 && a.info.buttons == 16 && a.info.hats == 1 && a.info.rumble,
                "capability_inventory");
        require((a.info.controllerAxisMask & (1u << SDL_CONTROLLER_AXIS_LEFTX)) &&
                    (a.info.controllerButtonMask & (1u << SDL_CONTROLLER_BUTTON_DPAD_UP)) &&
                    !c.info.controllerButtonMask,
                "normalized_capability_masks");
        std::printf("identity_strength=%d key=%s path=%s serial=%s\n",
                    static_cast<int>(a.info.identity), a.info.persistentKey.toUtf8().constData(),
                    a.info.path.toUtf8().constData(), a.info.serial.toUtf8().constData());

        require(SDL_JoystickSetVirtualButton(first->joystick, 0, 1) == 0 &&
                    SDL_JoystickSetVirtualAxis(first->joystick, 0, -22000) == 0 &&
                    SDL_JoystickSetVirtualHat(first->joystick, 0, SDL_HAT_RIGHTUP) == 0 &&
                    SDL_JoystickSetVirtualButton(second->joystick, 1, 1) == 0 &&
                    SDL_JoystickSetVirtualAxis(raw->joystick, 2, 19000) == 0,
                "provider_multiple_device_input");
        require(waitFor([&] {
                    return backend.state(first->id, &a) && backend.state(second->id, &b) &&
                           backend.state(raw->id, &c) && a.effective.buttons[0] &&
                           b.effective.buttons[1] && c.effective.axes[2] == 19000;
                }),
                "live_raw_state");
        require(a.effective.axes[0] == -22000 && a.effective.hats[0] == SDL_HAT_RIGHTUP &&
                    a.effective.controllerButtons[SDL_CONTROLLER_BUTTON_A] &&
                    a.effective.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_UP] &&
                    a.effective.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] &&
                    a.effective.controllerAxes[SDL_CONTROLLER_AXIS_LEFTX] == -22000,
                "normalized_buttons_axes_diagonal_hat");
        require(!b.effective.buttons[0] && c.effective.controllerAxes.empty(), "device_isolation");

        // Two changes happen before the next Qt turn. Polling only the final state loses both.
        events.clear();
        require(SDL_JoystickSetVirtualButton(first->joystick, 3, 1) == 0, "short_press_set");
        SDL_JoystickUpdate();
        require(SDL_JoystickSetVirtualButton(first->joystick, 3, 0) == 0, "short_release_set");
        SDL_JoystickUpdate();
        require(waitFor([&] {
                    int edges = 0;
                    for (const auto &event : events)
                        if (event.instance == first->id && event.kind == InputEvent::Kind::Button &&
                            event.control == 3)
                            ++edges;
                    return edges == 2;
                }),
                "short_press_release_edges_survive_poll");
        std::vector<int> sequence;
        for (const auto &event : events)
            if (event.instance == first->id && event.kind == InputEvent::Kind::Button &&
                event.control == 3)
                sequence.push_back(event.value);
        require(sequence == std::vector<int>({1, 0}), "raw_edge_order");
        sequence.clear();
        for (const auto &event : events)
            if (event.instance == first->id && event.kind == InputEvent::Kind::ControllerButton &&
                event.control == SDL_CONTROLLER_BUTTON_Y)
                sequence.push_back(event.value);
        require(sequence == std::vector<int>({1, 0}), "normalized_short_edge_order");

        require(backend.rumble(first->id, 0x1357, 0x2468, 1000) && first->low == 0x1357 &&
                    first->high == 0x2468,
                "rumble_request_actual_callback");
        QEvent deactivate(QEvent::ApplicationDeactivate);
        QCoreApplication::sendEvent(&app, &deactivate);
        require(backend.state(first->id, &a) && !a.effective.allowed && !a.effective.buttons[0] &&
                    a.physical.buttons[0] && !a.effective.axes[0] &&
                    a.effective.hats[0] == SDL_HAT_CENTERED,
                "focus_releases_effective_preserves_physical");
        require(!first->low && !first->high && !a.rumbleActive && released >= 3,
                "focus_stops_rumble_and_releases_sources");
        require(!backend.rumble(first->id, 1, 1, 10), "unfocused_rumble_rejected");
        events.clear();
        require(SDL_JoystickSetVirtualButton(first->joystick, 4, 1) == 0, "inactive_press_set");
        require(waitFor([&] { return backend.state(first->id, &a) && a.physical.buttons[4]; }),
                "physical_updates_while_blocked");
        require(std::any_of(events.begin(), events.end(),
                            [&](const InputEvent &event) {
                                return event.instance == first->id &&
                                       event.kind == InputEvent::Kind::Button &&
                                       event.control == 4 && event.value && !event.allowed;
                            }),
                "inactive_event_tagged_blocked");
        QEvent activate(QEvent::ApplicationActivate);
        events.clear();
        const unsigned previousResync = resynchronized;
        QCoreApplication::sendEvent(&app, &activate);
        require(backend.state(first->id, &a) && a.effective.buttons[0] && a.effective.buttons[4] &&
                    resynchronized > previousResync && events.empty(),
                "focus_resync_without_synthetic_press_edges");

        backend.setAllowBackground(true);
        require(backend.rumble(first->id, 200, 300, 100), "rumble_before_background");
        QCoreApplication::sendEvent(&app, &deactivate);
        require(backend.state(first->id, &a) && a.effective.allowed && a.effective.buttons[0] &&
                    !a.rumbleActive && !first->low && !first->high,
                "background_input_opt_in_still_stops_rumble");
        backend.setInputEnabled(false);
        require(backend.state(first->id, &a) && !a.effective.allowed && !a.effective.buttons[0] &&
                    a.physical.buttons[0],
                "master_gate_overrides_background");
        require(!backend.rumble(first->id, 1, 1, 10), "master_gate_rejects_rumble");
        events.clear();
        require(SDL_JoystickSetVirtualButton(first->joystick, 5, 1) == 0,
                "queued_inactive_press_set");
        SDL_JoystickUpdate();
        backend.setInputEnabled(true);
        require(backend.state(first->id, &a) && a.effective.buttons[5] && events.empty(),
                "enable_resync_discards_stale_shortcut_edges");
        QCoreApplication::sendEvent(&app, &activate);
        backend.setAllowBackground(false);

        require(backend.rumble(first->id, 400, 500, 1000), "duration_clamp_request");
        require(waitFor([&] { return backend.state(first->id, &a) && !a.rumbleActive; }, 800) &&
                    !first->low && !first->high,
                "rumble_duration_clamped_and_expires");
        require(backend.rumble(first->id, 600, 700, 250), "rumble_before_held_detach");
        const unsigned previousReleased = released;
        const SDL_JoystickID oldId = first->id;
        const QString oldProfile = a.info.profileKey;
        first->detach();
        require(waitFor([&] { return !backend.state(oldId, &a); }),
                "held_active_rumble_detach_cleanup");
        require(released > previousReleased && removed == 1 && backend.state(second->id, &b) &&
                    b.effective.buttons[1] && !backend.rumble(oldId, 1, 1, 10),
                "detach_releases_only_removed_instance");
        std::printf("detached_virtual_callback_low=%u high=%u calls=%u\n", first->low, first->high,
                    first->rumbleCalls);
        first.reset();
        first = std::make_unique<VirtualDevice>();
        first->attach(true);
        require(waitFor([&] { return backend.state(first->id, &a); }) && first->id != oldId &&
                    a.info.profileKey == oldProfile && !a.rumbleActive && !a.effective.buttons[0],
                "reconnect_profile_new_instance_no_stale_input_or_rumble");

        char rawGuid[33]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(raw->joystick), rawGuid, sizeof(rawGuid));
        const std::string lateMapping =
            std::string(rawGuid) + ",CupidGB late mapped joystick,a:b0,leftx:a2,";
        const unsigned beforeLateMapping = changed;
        require(SDL_GameControllerAddMapping(lateMapping.c_str()) >= 0, "late_mapping_install");
        require(waitFor([&] {
                    return backend.state(raw->id, &c) && c.info.mapped &&
                           changed > beforeLateMapping &&
                           c.effective.controllerAxes[SDL_CONTROLLER_AXIS_LEFTX] == 19000;
                }),
                "raw_device_gains_live_normalized_mapping");
        const std::string changedMapping =
            std::string(rawGuid) + ",CupidGB remapped joystick,b:b0,rightx:a2,";
        const unsigned beforeRemapping = changed;
        require(SDL_GameControllerAddMapping(changedMapping.c_str()) >= 0,
                "live_remapping_install");
        require(waitFor([&] {
                    return backend.state(raw->id, &c) && changed > beforeRemapping &&
                           (c.info.controllerAxisMask & (1u << SDL_CONTROLLER_AXIS_RIGHTX)) &&
                           !(c.info.controllerAxisMask & (1u << SDL_CONTROLLER_AXIS_LEFTX)) &&
                           c.effective.controllerAxes[SDL_CONTROLLER_AXIS_RIGHTX] == 19000;
                }),
                "live_remapping_refreshes_capabilities_and_state");
        backend.setInputEnabled(false);
        const unsigned beforeBlockedRemapping = changed;
        const std::string blockedMapping =
            std::string(rawGuid) + ",CupidGB blocked remapping,x:b0,lefty:a2,";
        require(SDL_GameControllerAddMapping(blockedMapping.c_str()) >= 0,
                "blocked_remapping_install");
        backend.setInputEnabled(true);
        require(waitFor([&] {
                    return backend.state(raw->id, &c) && changed > beforeBlockedRemapping &&
                           (c.info.controllerAxisMask & (1u << SDL_CONTROLLER_AXIS_LEFTY)) &&
                           !(c.info.controllerAxisMask & (1u << SDL_CONTROLLER_AXIS_RIGHTX)) &&
                           c.effective.controllerAxes[SDL_CONTROLLER_AXIS_LEFTY] == 19000;
                }),
                "re_enable_preserves_pending_mapping_metadata");

        SDL_Event user{};
        user.type = SDL_USEREVENT;
        user.user.code = 0x43555049;
        require(SDL_PushEvent(&user) == 1, "unrelated_event_enqueue");
        const unsigned previousBeats = beats;
        require(waitFor([&] { return beats >= previousBeats + 10; }),
                "qt_heartbeat_with_input_polling");
        SDL_Event found{};
        require(SDL_PeepEvents(&found, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_USEREVENT) == 1 &&
                    found.user.code == user.user.code,
                "unrelated_sdl_event_preserved");

        require(backend.rumble(second->id, 800, 900, 250), "rumble_before_stop");
        backend.stop();
        require(!backend.running() && backend.devices().empty() && !second->low && !second->high,
                "stop_closes_devices_and_rumble");
        require(std::string(SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS)) == "0",
                "owned_background_hint_restored");
        require((SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) &
                 (SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC)) ==
                    (SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC),
                "external_sdl_subsystem_references_survive");
        require(backend.start(&error) && backend.state(second->id, &b) && b.effective.buttons[1],
                "restart_reopens_live_state");
        backend.stop();
        unsigned destructorNotifications = 0;
        auto scopedBackend = std::make_unique<ControllerBackend>();
        scopedBackend->deviceRemoved = [&](SDL_JoystickID) { ++destructorNotifications; };
        require(scopedBackend->start(&error) && scopedBackend->rumble(second->id, 300, 400, 250),
                "scoped_backend_rumble");
        scopedBackend.reset();
        require(!second->low && !second->high &&
                    destructorNotifications == static_cast<unsigned>(baseline + 3),
                "destructor_stops_rumble_and_closes_all_instances");
        const unsigned previousNotifications = destructorNotifications;
        const unsigned destructorBeats = beats;
        require(waitFor([&] { return beats >= destructorBeats + 5; }) &&
                    destructorNotifications == previousNotifications,
                "qt_loop_safe_after_backend_destruction");
        first->detach();
        second->detach();
        raw->detach();
        first.reset();
        second.reset();
        raw.reset();
        require(SDL_NumJoysticks() == baseline, "no_leftover_virtual_devices");
        require(errors == 0, "no_backend_errors");
        std::printf("RESULT PASS checks=%u qt_heartbeats=%u virtual_only=1\n", checks, beats);
        return 0;
    } catch (const std::exception &error) {
        std::printf("RESULT FAIL checks=%u reason=%s\n", checks, error.what());
        return 1;
    }
}

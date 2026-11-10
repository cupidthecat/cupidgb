#pragma once

#include "controller_backend.h"

#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace controller_test {

inline void check(bool value, const char *description) {
    if (!value) {
        std::fprintf(stderr, "Controller check failed: %s; SDL: %s\n", description, SDL_GetError());
        throw std::runtime_error(description);
    }
}

inline bool waitFor(const std::function<bool()> &condition, int timeoutMs = 1500) {
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
    deadline.start(timeoutMs);
    loop.exec();
    return condition();
}

struct Session {
    Session() {
        SDL_SetMainReady();
        check(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) == 0,
              "virtual device provider initialization");
    }

    ~Session() {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
    }
};

struct Device {
    SDL_JoystickID id = -1;
    SDL_Joystick *handle = nullptr;
    bool attached = false;
    uint16_t low = 0, high = 0;
    unsigned rumbleCalls = 0;

    static int SDLCALL rumble(void *user, Uint16 low, Uint16 high) {
        auto &device = *static_cast<Device *>(user);
        device.low = low;
        device.high = high;
        ++device.rumbleCalls;
        return 0;
    }

    explicit Device(uint16_t product = 1, bool mapped = true) {
        SDL_VirtualJoystickDesc desc{};
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = mapped ? SDL_JOYSTICK_TYPE_GAMECONTROLLER : SDL_JOYSTICK_TYPE_UNKNOWN;
        desc.naxes = 6;
        desc.nbuttons = 16;
        desc.nhats = 1;
        desc.vendor_id = 0xcafe;
        desc.product_id = product;
        desc.name = mapped ? "CupidGB test controller" : "CupidGB test joystick";
        desc.button_mask = mapped ? 0x7ff : 0;
        desc.axis_mask = mapped ? 0x3f : 0;
        desc.userdata = this;
        desc.Rumble = rumble;
        const int index = SDL_JoystickAttachVirtualEx(&desc);
        check(index >= 0, "attach virtual controller");
        attached = true;
        id = SDL_JoystickGetDeviceInstanceID(index);
        handle = SDL_JoystickOpen(index);
        check(handle != nullptr, "open virtual provider handle");
        if (mapped) {
            char guid[33]{}, mapping[1024]{};
            SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(handle), guid, sizeof(guid));
            SDL_snprintf(mapping, sizeof(mapping),
                         "%s,CupidGB test controller,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,"
                         "start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,"
                         "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,leftx:a0,lefty:a1,"
                         "rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,",
                         guid);
            check(SDL_GameControllerAddMapping(mapping) >= 0, "install test mapping");
        }
    }

    void button(int index, bool pressed) {
        check(SDL_JoystickSetVirtualButton(handle, index, Uint8(pressed)) == 0, "virtual button");
    }

    void axis(int index, int16_t value) {
        check(SDL_JoystickSetVirtualAxis(handle, index, value) == 0, "virtual axis");
    }

    void hat(uint8_t value) {
        check(SDL_JoystickSetVirtualHat(handle, 0, value) == 0, "virtual hat");
    }

    void detach() {
        if (!attached)
            return;
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_JoystickGetDeviceInstanceID(i) == id) {
                check(SDL_JoystickDetachVirtual(i) == 0, "detach virtual device");
                attached = false;
                return;
            }
    }

    ~Device() {
        if (attached) {
            for (int i = 0; i < SDL_NumJoysticks(); ++i)
                if (SDL_JoystickGetDeviceInstanceID(i) == id)
                    SDL_JoystickDetachVirtual(i);
        }
        if (handle)
            SDL_JoystickClose(handle);
    }
};

} // namespace controller_test

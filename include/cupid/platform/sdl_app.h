#ifndef CUPID_PLATFORM_SDL_APP_H
#define CUPID_PLATFORM_SDL_APP_H

#include <stdbool.h>

#include "cupid/core/emulator.h"

typedef struct CupidSdlAppConfig {
    const char *title;
    int width;
    int height;
} CupidSdlAppConfig;

typedef struct CupidSdlApp {
    void *window;
    void *renderer;
    void *texture;    /* SDL_Texture* for the 160x144 GB screen */
    uint32_t audio_dev; /* SDL_AudioDeviceID, 0 = not opened */
    CupidEmulator *emulator;
    bool running;
} CupidSdlApp;

bool cupid_sdl_app_init(CupidSdlApp *app,
                        const CupidSdlAppConfig *config,
                        CupidEmulator *emulator);
void cupid_sdl_app_run(CupidSdlApp *app);
void cupid_sdl_app_shutdown(CupidSdlApp *app);

#endif

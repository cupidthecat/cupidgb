#include "cupid/platform/sdl_app.h"

#include <SDL2/SDL.h>

#include "cupid/common/log.h"
#include "cupid/core/system.h"

static void cupid_sdl_log_system(const CupidEmulator *emulator)
{
    if (emulator == 0) {
        cupid_log_error("SDL app started without a valid emulator instance.");
        return;
    }

    cupid_log_infof("Target system: %s", cupid_system_name(emulator->target_system));
}

bool cupid_sdl_app_init(CupidSdlApp *app,
                        const CupidSdlAppConfig *config,
                        const CupidEmulator *emulator)
{
    if (app == 0 || config == 0) {
        cupid_log_error("Cannot initialize SDL app: invalid arguments.");
        return false;
    }

    app->window = 0;
    app->renderer = 0;
    app->running = false;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        cupid_log_errorf("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    app->window = SDL_CreateWindow(config->title,
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   config->width,
                                   config->height,
                                   SDL_WINDOW_SHOWN);
    if (app->window == 0) {
        cupid_log_errorf("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    app->renderer = SDL_CreateRenderer((SDL_Window *)app->window, -1,
                                       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (app->renderer == 0) {
        cupid_log_errorf("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow((SDL_Window *)app->window);
        app->window = 0;
        SDL_Quit();
        return false;
    }

    app->running = true;

    cupid_log_info("SDL2 video initialized.");
    cupid_sdl_log_system(emulator);
    cupid_log_infof("Display window created: %s (%dx%d)",
                    config->title,
                    config->width,
                    config->height);
    cupid_log_info("Console logging remains in the terminal running cupidgb.");

    return true;
}

void cupid_sdl_app_run(CupidSdlApp *app)
{
    SDL_Event event;
    Uint32 frame_start;
    Uint8 color = 0;

    if (app == 0 || !app->running) {
        return;
    }

    cupid_log_info("Entering SDL event loop. Close the window to exit.");

    while (app->running) {
        frame_start = SDL_GetTicks();

        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                cupid_log_info("Received SDL quit event.");
                app->running = false;
            }
        }

        SDL_SetRenderDrawColor((SDL_Renderer *)app->renderer, color, 90U, 140U, 255U);
        SDL_RenderClear((SDL_Renderer *)app->renderer);
        SDL_RenderPresent((SDL_Renderer *)app->renderer);

        color = (Uint8)(color + 1U);

        if (SDL_GetTicks() - frame_start < 16U) {
            SDL_Delay(16U - (SDL_GetTicks() - frame_start));
        }
    }
}

void cupid_sdl_app_shutdown(CupidSdlApp *app)
{
    if (app == 0) {
        return;
    }

    if (app->renderer != 0) {
        SDL_DestroyRenderer((SDL_Renderer *)app->renderer);
        app->renderer = 0;
    }

    if (app->window != 0) {
        SDL_DestroyWindow((SDL_Window *)app->window);
        app->window = 0;
    }

    SDL_Quit();
    app->running = false;
    cupid_log_info("SDL2 shut down cleanly.");
}

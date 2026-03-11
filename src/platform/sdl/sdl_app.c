#include "cupid/platform/sdl_app.h"

#include <SDL2/SDL.h>
#include <stdlib.h>

#include "cupid/common/log.h"
#include "cupid/core/system.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/sgb.h"
#include "cupid/gbc/cgb.h"

/* DMG 4-shade palette (ARGB8888) */
static const Uint32 cupid_dmg_palette[4] = {
    0xFFE0F8D0u, /* color 0: lightest (off-white green) */
    0xFF88C070u, /* color 1: light green */
    0xFF346856u, /* color 2: dark green */
    0xFF081820u  /* color 3: darkest (near-black) */
};

static Uint32 cupid_rgb555_to_argb(uint16_t color)
{
    Uint32 r = (Uint32)(color & 0x1fu);
    Uint32 g = (Uint32)((color >> 5u) & 0x1fu);
    Uint32 b = (Uint32)((color >> 10u) & 0x1fu);

    r = (r << 3u) | (r >> 2u);
    g = (g << 3u) | (g >> 2u);
    b = (b << 3u) | (b >> 2u);
    return 0xff000000u | (r << 16u) | (g << 8u) | b;
}

/*
 * Joypad bit layout stored in CupidGb.joypad (0=pressed, 1=released):
 *   bit 0: Right   bit 4: A
 *   bit 1: Left    bit 5: B
 *   bit 2: Up      bit 6: Select
 *   bit 3: Down    bit 7: Start
 */
static void cupid_sdl_handle_key(CupidGb *gb, SDL_Scancode sc, bool pressed)
{
    uint8_t bit;

    switch (sc) {
    case SDL_SCANCODE_RIGHT:     bit = 0u; break;
    case SDL_SCANCODE_LEFT:      bit = 1u; break;
    case SDL_SCANCODE_UP:        bit = 2u; break;
    case SDL_SCANCODE_DOWN:      bit = 3u; break;
    case SDL_SCANCODE_Z:         bit = 4u; break; /* A */
    case SDL_SCANCODE_X:         bit = 5u; break; /* B */
    case SDL_SCANCODE_BACKSPACE: bit = 6u; break; /* Select */
    case SDL_SCANCODE_RETURN:    bit = 7u; break; /* Start */
    default:                     return;
    }

    if (pressed) {
        /* Only fire the joypad interrupt on the transition released -> pressed */
        if ((gb->joypad & (uint8_t)(1u << bit)) != 0u) {
            gb->joypad = (uint8_t)(gb->joypad & (uint8_t)~(1u << bit));
            gb->interrupt_flags = (uint8_t)(gb->interrupt_flags | 0x10u); /* bit 4: joypad IRQ */
        }
    } else {
        gb->joypad = (uint8_t)(gb->joypad | (uint8_t)(1u << bit));
    }
}

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
                        CupidEmulator *emulator)
{
    int window_width;
    int window_height;
    int window_scale;

    if (app == 0 || config == 0) {
        cupid_log_error("Cannot initialize SDL app: invalid arguments.");
        return false;
    }

    app->window = 0;
    app->renderer = 0;
    app->texture = 0;
    app->audio_dev = 0u;
    app->texture_width = CUPID_GB_SCREEN_WIDTH;
    app->texture_height = CUPID_GB_SCREEN_HEIGHT;
    app->frame_pixels = 0;
    app->emulator = emulator;
    app->running = false;

    if (emulator != 0 &&
        cupid_gb_sgb_active(&emulator->gb)) {
        app->texture_width = CUPID_SGB_SCREEN_WIDTH;
        app->texture_height = CUPID_SGB_SCREEN_HEIGHT;
    }

    window_scale = config->width / (int)app->texture_width;
    if (config->height / (int)app->texture_height < window_scale) {
        window_scale = config->height / (int)app->texture_height;
    }
    if (window_scale < 1) {
        window_scale = 1;
    }
    window_width = (int)app->texture_width * window_scale;
    window_height = (int)app->texture_height * window_scale;

    app->frame_pixels = malloc((size_t)app->texture_width * (size_t)app->texture_height * sizeof(*app->frame_pixels));
    if (app->frame_pixels == 0) {
        cupid_log_error("Failed to allocate SDL frame buffer.");
        return false;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        cupid_log_errorf("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    app->window = SDL_CreateWindow(config->title,
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   window_width,
                                   window_height,
                                   SDL_WINDOW_SHOWN);
    if (app->window == 0) {
        cupid_log_errorf("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    app->renderer = SDL_CreateRenderer((SDL_Window *)app->window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (app->renderer == 0) {
        cupid_log_errorf("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow((SDL_Window *)app->window);
        app->window = 0;
        SDL_Quit();
        return false;
    }

    /* Create a streaming texture for the active framebuffer */
    app->texture = SDL_CreateTexture((SDL_Renderer *)app->renderer,
                                     SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     (int)app->texture_width,
                                     (int)app->texture_height);
    if (app->texture == 0) {
        cupid_log_errorf("SDL_CreateTexture failed: %s", SDL_GetError());
        SDL_DestroyRenderer((SDL_Renderer *)app->renderer);
        SDL_DestroyWindow((SDL_Window *)app->window);
        app->renderer = 0;
        app->window = 0;
        free(app->frame_pixels);
        app->frame_pixels = 0;
        SDL_Quit();
        return false;
    }

    /* Scale framebuffer to fill the entire window */
    SDL_RenderSetLogicalSize((SDL_Renderer *)app->renderer,
                             (int)app->texture_width,
                             (int)app->texture_height);

    /* Open audio device for GB APU output */
    if (emulator != 0) {
        SDL_AudioSpec want;
        SDL_AudioSpec got;
        SDL_memset(&want, 0, sizeof(want));
        want.freq     = (int)CUPID_GB_APU_SAMPLE_RATE;
        want.format   = AUDIO_S16SYS;
        want.channels = 2;
        want.samples  = 512; /* low latency */
        want.callback = NULL; /* push API */
        app->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
        if (app->audio_dev == 0u) {
            cupid_log_errorf("SDL_OpenAudioDevice failed: %s", SDL_GetError());
            /* Non-fatal: run without audio */
        } else {
            SDL_PauseAudioDevice((SDL_AudioDeviceID)app->audio_dev, 0);
            cupid_log_info("SDL2 audio opened: 44100 Hz stereo int16");
        }
    }

    app->running = true;

    cupid_log_info("SDL2 video initialized.");
    cupid_sdl_log_system(emulator);
    cupid_log_infof("Display window created: %s (%dx%d)",
                    config->title,
                    window_width,
                    window_height);
    cupid_log_info("Controls: Arrow keys=DPad  Z=A  X=B  Enter=Start  Backspace=Select");
    cupid_log_info("Console logging remains in the terminal running cupidgb.");

    return true;
}

void cupid_sdl_app_run(CupidSdlApp *app)
{
    /* Safety cap: a bit over one full DMG frame (70224 T-cycles / 2 per step) */
    enum { CUPID_GB_MAX_STEPS_PER_FRAME = 80000 };
    SDL_Event event;
    Uint32 frame_start;

    if (app == 0 || !app->running) {
        return;
    }

    cupid_log_info("Entering SDL event loop. Close the window to exit.");

    while (app->running) {
        unsigned int steps;

        frame_start = SDL_GetTicks();

        /* Process SDL events */
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                cupid_log_info("Received SDL quit event.");
                app->running = false;
            } else if (event.type == SDL_KEYDOWN && app->emulator != 0) {
                cupid_sdl_handle_key(&app->emulator->gb,
                                     event.key.keysym.scancode,
                                     true);
            } else if (event.type == SDL_KEYUP && app->emulator != 0) {
                cupid_sdl_handle_key(&app->emulator->gb,
                                     event.key.keysym.scancode,
                                     false);
            }
        }

        /* Run the emulator until a full frame is produced (vblank) */
        if (app->emulator != 0 && app->emulator->initialized &&
            app->emulator->gb.loaded) {
            for (steps = 0u;
                 steps < (unsigned int)CUPID_GB_MAX_STEPS_PER_FRAME;
                 ++steps) {
                if (!cupid_emulator_step(app->emulator)) {
                    cupid_log_error("Emulation stopped after a CPU execution failure.");
                    app->running = false;
                    break;
                }
                if (app->emulator->gb.frame_ready) {
                    app->emulator->gb.frame_ready = false;
                    break;
                }
            }
        }

        /* Convert active framebuffer to ARGB and upload to texture */
        if (app->texture != 0 && app->emulator != 0) {
            size_t i;

            if (cupid_gb_sgb_active(&app->emulator->gb)) {
                cupid_gb_sgb_render_argb(&app->emulator->gb,
                                         app->frame_pixels,
                                         (size_t)app->texture_width * (size_t)app->texture_height);
            } else if (app->emulator->gb.cgb_mode || cupid_cgb_compat_active(&app->emulator->gb)) {
                for (i = 0u;
                     i < (size_t)app->texture_width * (size_t)app->texture_height;
                     ++i) {
                    app->frame_pixels[i] = cupid_rgb555_to_argb(
                        app->emulator->gb.frame_buffer_color[i]);
                }
            } else {
                for (i = 0u;
                     i < (size_t)app->texture_width * (size_t)app->texture_height;
                     ++i) {
                    app->frame_pixels[i] = cupid_dmg_palette[
                        app->emulator->gb.frame_buffer[i] & 0x03u];
                }
            }

            SDL_UpdateTexture((SDL_Texture *)app->texture,
                              NULL,
                              app->frame_pixels,
                              (int)(app->texture_width * sizeof(Uint32)));
            SDL_RenderClear((SDL_Renderer *)app->renderer);
            SDL_RenderCopy((SDL_Renderer *)app->renderer,
                           (SDL_Texture *)app->texture,
                           NULL,
                           NULL);
            SDL_RenderPresent((SDL_Renderer *)app->renderer);
        }

        /* Queue APU samples to SDL audio device */
        if (app->audio_dev != 0u && app->emulator != 0) {
            int16_t audio_buf[CUPID_GB_APU_BUF_FRAMES * 2u];
            uint32_t frames = cupid_gb_apu_drain(&app->emulator->gb,
                                                 audio_buf,
                                                 CUPID_GB_APU_BUF_FRAMES);
            if (frames > 0u) {
                SDL_QueueAudio((SDL_AudioDeviceID)app->audio_dev,
                               audio_buf,
                               (Uint32)(frames * 2u * sizeof(int16_t)));
            }
        }

        /* Sync emulation speed to audio playback rate.
         * Wait until the queued audio drops below ~2 frames' worth of
         * samples so we neither starve the device nor pile up latency.
         * Fall back to a simple timer-based 60 fps cap when there is
         * no audio device. */
        if (app->audio_dev != 0u) {
            enum { CUPID_AUDIO_QUEUE_LIMIT = 8192 }; /* ~46 ms of stereo int16 */
            while (SDL_GetQueuedAudioSize(
                       (SDL_AudioDeviceID)app->audio_dev) > CUPID_AUDIO_QUEUE_LIMIT) {
                SDL_Delay(1u);
                while (SDL_PollEvent(&event) != 0) {
                    if (event.type == SDL_QUIT) {
                        app->running = false;
                    } else if (event.type == SDL_KEYDOWN && app->emulator != 0) {
                        cupid_sdl_handle_key(&app->emulator->gb,
                                             event.key.keysym.scancode,
                                             true);
                    } else if (event.type == SDL_KEYUP && app->emulator != 0) {
                        cupid_sdl_handle_key(&app->emulator->gb,
                                             event.key.keysym.scancode,
                                             false);
                    }
                }
                if (!app->running) { break; }
            }
        } else {
            Uint32 elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < 17U) {
                SDL_Delay(17U - elapsed);
            }
        }
    }
}

void cupid_sdl_app_shutdown(CupidSdlApp *app)
{
    if (app == 0) {
        return;
    }

    if (app->audio_dev != 0u) {
        SDL_CloseAudioDevice((SDL_AudioDeviceID)app->audio_dev);
        app->audio_dev = 0u;
    }

    if (app->texture != 0) {
        SDL_DestroyTexture((SDL_Texture *)app->texture);
        app->texture = 0;
    }

    free(app->frame_pixels);
    app->frame_pixels = 0;

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

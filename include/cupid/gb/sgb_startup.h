/**
 * @file    sgb_startup.h
 * @brief   Adapter startup timing, presentation state, and host output ownership.
 * @author  cupidthecat
 * @date    2026-11-17
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

#ifndef CUPID_GB_SGB_STARTUP_H
#define CUPID_GB_SGB_STARTUP_H

#include "cupid/gb/sgb_startup_audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CUPID_GB_SGB_STARTUP_FIRST_PHASE (-10)
#define CUPID_GB_SGB_STARTUP_CPU_PHASE 96
#define CUPID_GB_SGB_STARTUP_COMPLETE_PHASE 200
#define CUPID_GB_SGB_STARTUP_STEP_DOTS 114u

struct CupidGb;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CupidGbSgbStartup {
    int16_t phase;
    /* Host-only base clocks. Guest clocks retain their existing meaning. */
    uint64_t elapsed_cycles;
    CupidGbSgbStartupAudio output;
} CupidGbSgbStartup;

/* Physical host presence is independent of cartridge enhancement flags. */
bool cupid_gb_sgb_host_present(const struct CupidGb *gb);
bool cupid_gb_sgb_startup_active(const struct CupidGb *gb);
bool cupid_gb_sgb_startup_held(const struct CupidGb *gb);

/* Total presentation time includes host startup holds and guest hardware time. */
uint64_t cupid_gb_presentation_cycles(const struct CupidGb *gb);

/* Only explicit executed-firmware entry restarts the adapter introduction.
 * Approximate post-boot setup and non-adapter models use the completed state. */
void cupid_gb_sgb_startup_reset(struct CupidGb *gb, bool firmware);

/* A held call advances only display delivery, before ordinary CPU dispatch. */
bool cupid_gb_sgb_startup_step(struct CupidGb *gb);

/* Called once at an actual presentation, before transfer/render/callback work. */
void cupid_gb_sgb_startup_present(struct CupidGb *gb);

/* Pure redraw of the 160x144 startup viewport in an existing adapter surface. */
void cupid_gb_sgb_startup_render(const struct CupidGb *gb, uint32_t *pixels, size_t pixel_count);

#ifdef __cplusplus
}
#endif

#endif

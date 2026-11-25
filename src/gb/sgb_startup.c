/**
 * @file    sgb_startup.c
 * @brief   Adapter startup hold, presentation timing, and output generation.
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

#include "cupid/gb/sgb_startup.h"

#include "cupid/gb/apu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "display_clock.h"

#include <string.h>

bool cupid_gb_sgb_host_present(const CupidGb *gb) {
    return gb && cupid_gb_model_is_sgb(gb->model) && !cupid_gb_model_is_sgb_external(gb->model);
}

bool cupid_gb_sgb_startup_active(const CupidGb *gb) {
    return cupid_gb_sgb_host_present(gb) &&
           gb->sgb_startup.phase < CUPID_GB_SGB_STARTUP_COMPLETE_PHASE;
}

bool cupid_gb_sgb_startup_held(const CupidGb *gb) {
    return cupid_gb_sgb_host_present(gb) && gb->sgb_startup.phase < CUPID_GB_SGB_STARTUP_CPU_PHASE;
}

uint64_t cupid_gb_presentation_cycles(const CupidGb *gb) {
    if (!gb)
        return 0;
    const uint64_t host = cupid_gb_sgb_host_present(gb) ? gb->sgb_startup.elapsed_cycles : 0;
    return UINT64_MAX - gb->elapsed_cycles < host ? UINT64_MAX : gb->elapsed_cycles + host;
}

void cupid_gb_sgb_startup_reset(CupidGb *gb, bool firmware) {
    if (!gb)
        return;
    CupidGbSgbStartup *startup = &gb->sgb_startup;
    memset(startup, 0, sizeof(*startup));
    const bool active = firmware && cupid_gb_sgb_host_present(gb);
    startup->phase =
        active ? CUPID_GB_SGB_STARTUP_FIRST_PHASE : CUPID_GB_SGB_STARTUP_COMPLETE_PHASE;
    /* Domain separation keeps host noise independent of power-on RAM draws. */
    const uint64_t seed =
        active ? gb->power_on.generation_seed ^ UINT64_C(0x4355504944534742) : UINT64_C(1);
    cupid_gb_sgb_startup_audio_reset(&startup->output, seed);
    if (active) {
        gb->display_cycles_since_frame = 0;
        gb->lcd_repeat_countdown = 0;
        gb->display_frame_type = CUPID_GB_FRAME_LCD_OFF;
        gb->sgb.transfer.frame_dots = 0;
    }
}

bool cupid_gb_sgb_startup_step(CupidGb *gb) {
    if (!cupid_gb_sgb_startup_held(gb))
        return false;
    const uint64_t host = gb->sgb_startup.elapsed_cycles;
    if (host > UINT64_MAX - CUPID_GB_SGB_STARTUP_STEP_DOTS ||
        gb->elapsed_cycles > UINT64_MAX - host - CUPID_GB_SGB_STARTUP_STEP_DOTS)
        return false;
    gb->sgb_startup.elapsed_cycles = host + CUPID_GB_SGB_STARTUP_STEP_DOTS;
    /* This path has no CPU delivery: no DIV, APU, DMA, input, or RTC clocks.
     * Pixel delivery still publishes idle or live LCD frames at its own cadence. */
    cupid_gb_tick_ppu_dots(gb, CUPID_GB_SGB_STARTUP_STEP_DOTS);
    cupid_gb_display_clock_advance_host(gb, 2u * CUPID_GB_SGB_STARTUP_STEP_DOTS);
    return true;
}

void cupid_gb_sgb_startup_present(CupidGb *gb) {
    if (!cupid_gb_sgb_startup_active(gb))
        return;
    CupidGbSgbStartup *startup = &gb->sgb_startup;
    /* Audio belongs to the old phase; the callback observes the new picture.
     * A caller which does not drain can lose new output, never guest clocks. */
    (void)cupid_gb_sgb_startup_audio_enqueue(&startup->output, gb->apu.output.buf_write,
                                             startup->phase, cupid_gb_apu_get_sample_rate(gb),
                                             cupid_gb_base_clock_hz(gb));
    ++startup->phase;
}

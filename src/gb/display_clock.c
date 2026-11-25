/**
 * @file    display_clock.c
 * @brief   LCD synchronization history for VRAM reads and infrared receiver thresholds.
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

#include "display_clock.h"

#include "cupid/gb/ppu.h"
#include "vram_bus.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

enum { FRAME_DOTS = 70224, ROW_HALF_DOTS = 912 };

bool cupid_gb_display_clock_capable(const CupidGb *gb) {
    return gb && cupid_gb_model_is_valid(gb->model);
}

static bool lcd_on(const CupidGb *gb) {
    return (gb->io_registers[0x40] & 0x80u) != 0;
}

void cupid_gb_display_clock_reset(CupidGb *gb) {
    if (gb)
        memset(&gb->display_clock, 0, sizeof(gb->display_clock));
}

void cupid_gb_display_clock_power(CupidGb *gb, bool enabled) {
    if (!cupid_gb_display_clock_capable(gb)) {
        cupid_gb_display_clock_reset(gb);
        return;
    }
    const uint32_t age = gb->display_clock.frame_age_dots;
    const uint8_t alignment = gb->display_clock.window_alignment;
    cupid_gb_display_clock_reset(gb);
    /* A late enable publishes the preceding off interval before starting the
     * first row. The WY check retains its oscillator origin until RESET runs. */
    gb->display_clock.frame_age_dots = enabled && age > 4560u ? 0u : age;
    gb->display_clock.window_alignment = alignment;
    if (enabled)
        gb->display_clock.flags = CUPID_GB_DISPLAY_CLOCK_FIRST_ROW;
}

void cupid_gb_display_clock_window_write(CupidGb *gb) {
    if (cupid_gb_display_clock_capable(gb))
        gb->display_clock.flags |= CUPID_GB_DISPLAY_CLOCK_WINDOW_PENDING;
}

void cupid_gb_display_clock_abort_object(CupidGb *gb) {
    if (!cupid_gb_display_clock_capable(gb))
        return;
    CupidGbDisplayClock *clock = &gb->display_clock;
    if (clock->phase == CUPID_GB_DISPLAY_CLOCK_TRANSFER_FINE) {
        /* OBJ_EN cancels the remaining fetch wait at the write edge. */
        clock->row_half_dots = (uint16_t)(clock->row_half_dots + clock->credit_half_dots);
        clock->credit_half_dots = 0;
        clock->fine_wait_half_dots = 0;
    }
}

bool cupid_gb_display_clock_sync_address(uint16_t address, bool write) {
    if (address >= 0x8000u && address < 0xa000u)
        return true;
    if (address >= 0xfe00u && address < 0xff00u)
        return true;
    if (address == 0xffffu)
        return write;
    if (address == 0xff0fu || (address >= 0xff40u && address <= 0xff4bu) ||
        (address >= 0xff51u && address <= 0xff55u) || (address >= 0xff68u && address <= 0xff6cu))
        return true;
    return false;
}

static unsigned window_write_row(const CupidGb *gb) {
    const CupidGbDisplayClock *clock = &gb->display_clock;
    /* The LY comparison still sees the previous line at the OAM prefix on
     * monochrome hardware and in double speed. */
    if ((!cupid_gb_model_is_cgb(gb->model) || gb->double_speed) &&
        clock->phase == CUPID_GB_DISPLAY_CLOCK_OAM_PREFIX && clock->cursor <= 1 && clock->row > 0 &&
        clock->row < 144)
        return clock->row - 1u;
    return clock->row;
}

static bool window_y_matches(const CupidGb *gb, unsigned row) {
    return (gb->io_registers[0x40] & 0xa0u) == 0xa0u && gb->io_registers[0x4a] == row;
}

static void window_compare(CupidGb *gb, unsigned row) {
    CupidGbDisplayClock *clock = &gb->display_clock;
    if (window_y_matches(gb, row)) {
        clock->flags |= CUPID_GB_DISPLAY_CLOCK_WINDOW_Y;
        if (row < 144)
            gb->pixels.window_armed = true;
    }
}

static void wait_dots(CupidGbDisplayClock *clock, unsigned dots, unsigned phase, unsigned cursor) {
    clock->credit_half_dots -= (int32_t)(dots * 2u);
    clock->phase = (uint8_t)phase;
    clock->cursor = (uint8_t)cursor;
}

static void begin_row(CupidGb *gb) {
    CupidGbDisplayClock *clock = &gb->display_clock;
    clock->row_half_dots = 0;
    clock->batch_half_dots = 0;
    clock->cursor = 0;
    clock->flags &=
        (uint8_t)~(CUPID_GB_DISPLAY_CLOCK_FIRST_ROW | CUPID_GB_DISPLAY_CLOCK_FORCED_FINE);
    if (++clock->row == 154) {
        clock->row = 0;
        clock->flags &=
            (uint8_t)~(CUPID_GB_DISPLAY_CLOCK_WINDOW_Y | CUPID_GB_DISPLAY_CLOCK_WINDOW_ACTIVE);
    }
    clock->phase = clock->row < 144   ? CUPID_GB_DISPLAY_CLOCK_OAM_PREFIX
                   : clock->row < 153 ? CUPID_GB_DISPLAY_CLOCK_VBLANK_EDGES
                                      : CUPID_GB_DISPLAY_CLOCK_VBLANK_LAST;
    window_compare(gb, clock->row);
}

static unsigned transfer_budget(const CupidGb *gb) {
    const CupidGbDisplayClock *clock = &gb->display_clock;
    const uint8_t lcdc = gb->io_registers[0x40];
    const uint8_t wx = gb->io_registers[0x4b];
    const bool window = (clock->flags & CUPID_GB_DISPLAY_CLOCK_WINDOW_Y) != 0;
    if (gb->cpu.stopped || gb->dma_active || gb->dma_start_delay || gb->hdma_pending ||
        gb->hdma_running || (clock->flags & CUPID_GB_DISPLAY_CLOCK_WINDOW_ACTIVE))
        return 0;
    if (window && ((lcdc & 0x20u) ? (wx < 7u || wx == 166u || wx == 167u)
                                  : (wx < 167u && !cupid_gb_model_is_cgb(gb->model))))
        return 0;
    if (!gb->pixels.object_count && !(window && (lcdc & 0x20u)))
        return 2u * (167u + (gb->io_registers[0x43] & 7u));
    if (gb->hdma_active)
        return 0;
    return !(gb->io_registers[0x41] & 8u) || !(gb->interrupt_enable & 2u) ? 600u : 0;
}

/* Fine processing follows fetch events rather than a fixed offset between LY
 * coordinate conventions. Object word and low-byte reads each hold two dots;
 * the remaining fetch waits hold one. A half dot can enter the next wait before
 * the eager pixel engine reaches its whole-dot edge. */
static void fine_transfer(CupidGb *gb) {
    cupid_gb_vram_bus_commit_fetch(gb);
    CupidGbDisplayClock *clock = &gb->display_clock;
    const CupidGbPixels *pixels = &gb->pixels;
    const int32_t position = clock->row_half_dots + clock->credit_half_dots;
    if (!pixels->active && pixels->x == CUPID_GB_SCREEN_WIDTH && gb->ppu_transfer_end_dot) {
        const unsigned elapsed =
            (pixels->dot - gb->ppu_transfer_end_dot) * 2u + ((unsigned)position & 1u);
        /* The final emitted pixel enters HBlank's first one-dot wait. Retain
         * the event's age so a late forced batch consumes every following wait. */
        clock->credit_half_dots = (int32_t)elapsed + 2;
        clock->row_half_dots = (uint16_t)(position - clock->credit_half_dots);
        clock->phase = CUPID_GB_DISPLAY_CLOCK_HBLANK_EDGES;
        clock->cursor = 0;
        clock->fine_wait_half_dots = 0;
        return;
    }
    unsigned remaining = (unsigned)position & 1u;
    if (remaining) {
        if (gb->object_fetch_stage == CUPID_GB_OBJECT_READ_WORD || pixels->object_delay == 4)
            remaining = 3;
    } else if (pixels->object_delay == 5 || pixels->object_delay == 3) {
        remaining = 2;
    }
    clock->credit_half_dots = -(int32_t)remaining;
    clock->row_half_dots = (uint16_t)(position + (int32_t)remaining);
    clock->fine_wait_half_dots = (uint8_t)remaining;
}

static void run_schedule(CupidGb *gb, bool force) {
    CupidGbDisplayClock *clock = &gb->display_clock;
    while (clock->credit_half_dots > 0) {
        switch (clock->phase) {
            case CUPID_GB_DISPLAY_CLOCK_RESET:
                if (!lcd_on(gb)) {
                    if (clock->frame_age_dots < FRAME_DOTS) {
                        wait_dots(clock, FRAME_DOTS - clock->frame_age_dots,
                                  CUPID_GB_DISPLAY_CLOCK_IDLE_FRAME, 0);
                        break;
                    }
                    clock->frame_age_dots = 0;
                    break;
                }
                clock->row = 0;
                clock->flags |= CUPID_GB_DISPLAY_CLOCK_FIRST_ROW;
                clock->flags &= (uint8_t)~(CUPID_GB_DISPLAY_CLOCK_WINDOW_Y |
                                           CUPID_GB_DISPLAY_CLOCK_WINDOW_ACTIVE);
                clock->phase = CUPID_GB_DISPLAY_CLOCK_STARTUP;
                clock->cursor = 0;
                if (!cupid_gb_model_is_cgb(gb->model))
                    clock->credit_half_dots -= 2;
                break;
            case CUPID_GB_DISPLAY_CLOCK_IDLE_FRAME:
                clock->frame_age_dots = 0;
                wait_dots(clock, FRAME_DOTS, CUPID_GB_DISPLAY_CLOCK_IDLE_FRAME, 0);
                break;
            case CUPID_GB_DISPLAY_CLOCK_STARTUP:
                switch (clock->cursor) {
                    case 0:
                        clock->row_half_dots = 152;
                        wait_dots(clock, 76, CUPID_GB_DISPLAY_CLOCK_STARTUP, 1);
                        break;
                    case 1:
                        clock->row_half_dots += 4;
                        wait_dots(clock, 2, CUPID_GB_DISPLAY_CLOCK_STARTUP, 2);
                        break;
                    case 2:
                        clock->row_half_dots += 20;
                        wait_dots(clock, 2, CUPID_GB_DISPLAY_CLOCK_STARTUP, 3);
                        break;
                    default:
                        clock->row_half_dots += 6;
                        wait_dots(clock, 3, CUPID_GB_DISPLAY_CLOCK_TRANSFER_PREFIX, 2);
                        break;
                }
                break;
            case CUPID_GB_DISPLAY_CLOCK_OAM_PREFIX:
                if (clock->cursor < 3) {
                    const unsigned delay = clock->cursor ? 1u : 2u;
                    wait_dots(clock, delay, CUPID_GB_DISPLAY_CLOCK_OAM_PREFIX, clock->cursor + 1u);
                } else {
                    clock->cursor = 0;
                    clock->batch_half_dots = 160;
                    clock->phase = CUPID_GB_DISPLAY_CLOCK_OAM_QUIET;
                    window_compare(gb, clock->row);
                }
                break;
            case CUPID_GB_DISPLAY_CLOCK_OAM_QUIET:
                if (!force && !gb->dma_active && !gb->dma_start_delay &&
                    !gb->stop_ppu_oam_blocked && clock->credit_half_dots < 160)
                    return;
                clock->batch_half_dots = 0;
                clock->phase = CUPID_GB_DISPLAY_CLOCK_OAM_FINE;
                clock->cursor = 0;
                break;
            case CUPID_GB_DISPLAY_CLOCK_OAM_FINE:
                if (clock->cursor < 40) {
                    wait_dots(clock, 2, CUPID_GB_DISPLAY_CLOCK_OAM_FINE, clock->cursor + 1u);
                } else {
                    clock->row_half_dots = 168;
                    clock->phase = CUPID_GB_DISPLAY_CLOCK_TRANSFER_PREFIX;
                    clock->cursor = 0;
                }
                break;
            case CUPID_GB_DISPLAY_CLOCK_TRANSFER_PREFIX:
                if (clock->cursor < 2) {
                    const unsigned delay = clock->cursor ? 2u : 3u;
                    clock->row_half_dots = (uint16_t)(clock->row_half_dots + delay * 2u);
                    wait_dots(clock, delay, CUPID_GB_DISPLAY_CLOCK_TRANSFER_PREFIX,
                              clock->cursor + 1u);
                } else {
                    clock->batch_half_dots = (uint16_t)transfer_budget(gb);
                    clock->phase = CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET;
                    clock->cursor = 0;
                    clock->flags &= (uint8_t)~CUPID_GB_DISPLAY_CLOCK_FORCED_FINE;
                }
                break;
            case CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET:
                if (clock->batch_half_dots && clock->credit_half_dots >= clock->batch_half_dots) {
                    clock->row_half_dots += clock->batch_half_dots;
                    clock->credit_half_dots -= clock->batch_half_dots;
                    clock->batch_half_dots = 0;
                    clock->phase = CUPID_GB_DISPLAY_CLOCK_HBLANK_EDGES;
                    break;
                }
                if (!force && clock->batch_half_dots)
                    return;
                clock->batch_half_dots = 0;
                clock->flags |= CUPID_GB_DISPLAY_CLOCK_FORCED_FINE;
                clock->phase = CUPID_GB_DISPLAY_CLOCK_TRANSFER_FINE;
                break;
            case CUPID_GB_DISPLAY_CLOCK_TRANSFER_FINE:
                fine_transfer(gb);
                break;
            case CUPID_GB_DISPLAY_CLOCK_HBLANK_EDGES:
                if (clock->cursor < 3) {
                    const unsigned delay = clock->cursor ? 2u : 1u;
                    clock->row_half_dots = (uint16_t)(clock->row_half_dots + delay * 2u);
                    wait_dots(clock, delay, CUPID_GB_DISPLAY_CLOCK_HBLANK_EDGES,
                              clock->cursor + 1u);
                } else {
                    if (clock->row_half_dots > ROW_HALF_DOTS - 4) {
                        clock->row_half_dots = 0;
                        wait_dots(clock, 456, CUPID_GB_DISPLAY_CLOCK_ROW_ABORT, 0);
                        break;
                    }
                    const unsigned delay = (ROW_HALF_DOTS - clock->row_half_dots) / 2u - 2u;
                    clock->row_half_dots = 0;
                    wait_dots(clock, delay, CUPID_GB_DISPLAY_CLOCK_HBLANK_GAP, 0);
                }
                break;
            case CUPID_GB_DISPLAY_CLOCK_HBLANK_GAP:
                wait_dots(clock, 2, CUPID_GB_DISPLAY_CLOCK_LINE_TAIL, 0);
                break;
            case CUPID_GB_DISPLAY_CLOCK_LINE_TAIL:
                begin_row(gb);
                break;
            case CUPID_GB_DISPLAY_CLOCK_VBLANK_EDGES:
                if (clock->cursor < 3) {
                    const unsigned delay = clock->cursor == 2 ? 1u : 2u;
                    wait_dots(clock, delay, CUPID_GB_DISPLAY_CLOCK_VBLANK_EDGES,
                              clock->cursor + 1u);
                } else {
                    if (clock->row == 144)
                        clock->frame_age_dots = 0;
                    wait_dots(clock, 451, CUPID_GB_DISPLAY_CLOCK_VBLANK_GAP, 0);
                }
                break;
            case CUPID_GB_DISPLAY_CLOCK_VBLANK_GAP:
                begin_row(gb);
                break;
            case CUPID_GB_DISPLAY_CLOCK_VBLANK_LAST: {
                const unsigned delays[] = {2,
                                           cupid_gb_model_is_cgb(gb->model) ? 2u : 4u,
                                           cupid_gb_model_is_cgb(gb->model) ? 4u : 2u,
                                           4,
                                           12,
                                           432};
                if (clock->cursor < sizeof(delays) / sizeof(delays[0])) {
                    wait_dots(clock, delays[clock->cursor], CUPID_GB_DISPLAY_CLOCK_VBLANK_LAST,
                              clock->cursor + 1u);
                } else {
                    begin_row(gb);
                }
                break;
            }
            case CUPID_GB_DISPLAY_CLOCK_ROW_ABORT:
                begin_row(gb);
                break;
            default:
                assert(false);
                return;
        }
    }
}

static void advance_clock(CupidGb *gb, unsigned half_dots, bool force) {
    CupidGbDisplayClock *clock = &gb->display_clock;
    clock->frame_age_dots += half_dots / 2u;
    clock->window_alignment = (uint8_t)((clock->window_alignment + half_dots) & 7u);
    if (gb->cpu.stopped && !cupid_gb_model_is_cgb(gb->model)) {
        if (clock->frame_age_dots >= FRAME_DOTS)
            clock->frame_age_dots = 0;
        return;
    }
    if (clock->phase == CUPID_GB_DISPLAY_CLOCK_RESET &&
        clock->credit_half_dots + (int32_t)half_dots > 0)
        clock->window_alignment = (uint8_t)(half_dots & 7u);
    clock->credit_half_dots += (int32_t)half_dots;
    run_schedule(gb, force);
}

static unsigned window_deadline(const CupidGb *gb) {
    const unsigned offset = gb->double_speed ? 6u : cupid_gb_model_is_cgb(gb->model) ? 0u : 2u;
    return 8u - ((gb->display_clock.window_alignment + offset) & 7u);
}

void cupid_gb_display_clock_sample_window(CupidGb *gb, unsigned before, unsigned after) {
    const unsigned deadline = window_deadline(gb);
    if (before >= deadline || after < deadline || (gb->io_registers[0x40] & 0xa0u) != 0xa0u)
        return;

    /* Locate the scheduled comparison between eager dots. Keep the observer's
     * VRAM delivery and window history on their regular CPU-span boundary. */
    const CupidGbDisplayClock saved_clock = gb->display_clock;
    const CupidGbVramBus saved_bus = gb->vram_bus;
    const bool was_armed = gb->pixels.window_armed;
    advance_clock(gb, deadline, true);
    const unsigned row = window_write_row(gb);
    gb->display_clock = saved_clock;
    gb->vram_bus = saved_bus;
    gb->pixels.window_armed = was_armed;
    if (row < 144 && window_y_matches(gb, row)) {
        gb->pixels.window_armed = true;
        if (!was_armed && gb->pixels.active)
            gb->pixel_timing.flags |= CUPID_GB_PIXEL_WINDOW_Y_EDGE;
    }
}

static void deliver_clock(CupidGb *gb, unsigned half_dots, bool force) {
    CupidGbDisplayClock *clock = &gb->display_clock;
    if (clock->flags & CUPID_GB_DISPLAY_CLOCK_WINDOW_Y)
        clock->flags &= (uint8_t)~CUPID_GB_DISPLAY_CLOCK_WINDOW_PENDING;
    if (clock->flags & CUPID_GB_DISPLAY_CLOCK_WINDOW_PENDING) {
        force = true;
        const unsigned until_check = window_deadline(gb);
        if (half_dots >= until_check) {
            clock->flags &= (uint8_t)~CUPID_GB_DISPLAY_CLOCK_WINDOW_PENDING;
            advance_clock(gb, until_check, true);
            const bool was_armed = gb->pixels.window_armed;
            window_compare(gb, window_write_row(gb));
            if (!was_armed && gb->pixels.window_armed && gb->pixels.active)
                gb->pixel_timing.flags |= CUPID_GB_PIXEL_WINDOW_Y_EDGE;
            half_dots -= until_check;
        }
    }
    advance_clock(gb, half_dots, force);
}

void cupid_gb_display_clock_advance(CupidGb *gb, unsigned half_dots) {
    if (!cupid_gb_display_clock_capable(gb))
        return;
    deliver_clock(gb, half_dots, false);
}

void cupid_gb_display_clock_advance_host(CupidGb *gb, unsigned half_dots) {
    if (cupid_gb_display_clock_capable(gb))
        deliver_clock(gb, half_dots, true);
}

void cupid_gb_display_clock_sync(CupidGb *gb) {
    if (cupid_gb_display_clock_capable(gb))
        deliver_clock(gb, 0, true);
}

static bool valid_clock(const CupidGbDisplayClock *clock) {
    if (clock->credit_half_dots < -140448 || clock->credit_half_dots > 599 ||
        clock->frame_age_dots >= 140448 || clock->row_half_dots > ROW_HALF_DOTS ||
        clock->row > 153 || clock->phase > CUPID_GB_DISPLAY_CLOCK_ROW_ABORT || clock->cursor > 40 ||
        clock->window_alignment > 7 || (clock->flags & (uint8_t)~CUPID_GB_DISPLAY_CLOCK_FLAGS) ||
        clock->fine_wait_half_dots > 4)
        return false;
    const unsigned budget = clock->batch_half_dots;
    if (budget && budget != 160 && budget != 600 && (budget < 334 || budget > 348 || (budget & 1u)))
        return false;
    if (budget && clock->phase != CUPID_GB_DISPLAY_CLOCK_OAM_QUIET &&
        clock->phase != CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET)
        return false;
    if (clock->phase == CUPID_GB_DISPLAY_CLOCK_OAM_QUIET && budget != 160)
        return false;
    if (clock->phase == CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET && budget == 160)
        return false;
    const unsigned maximum_cursor = clock->phase == CUPID_GB_DISPLAY_CLOCK_OAM_FINE      ? 40u
                                    : clock->phase == CUPID_GB_DISPLAY_CLOCK_VBLANK_LAST ? 6u
                                    : clock->phase == CUPID_GB_DISPLAY_CLOCK_STARTUP ||
                                            clock->phase == CUPID_GB_DISPLAY_CLOCK_OAM_PREFIX ||
                                            clock->phase == CUPID_GB_DISPLAY_CLOCK_HBLANK_EDGES ||
                                            clock->phase == CUPID_GB_DISPLAY_CLOCK_VBLANK_EDGES
                                        ? 3u
                                    : clock->phase == CUPID_GB_DISPLAY_CLOCK_TRANSFER_PREFIX ? 2u
                                                                                             : 0u;
    return clock->cursor <= maximum_cursor;
}

bool cupid_gb_display_clock_state_valid(const CupidGb *gb) {
    return cupid_gb_display_clock_capable(gb) && valid_clock(&gb->display_clock);
}

static void store_u32(uint8_t *data, uint32_t value) {
    for (unsigned index = 0; index < 4; ++index)
        data[index] = (uint8_t)(value >> (index * 8u));
}

static uint32_t load_u32(const uint8_t *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 |
           (uint32_t)data[3] << 24;
}

bool cupid_gb_display_clock_state_encode(const CupidGb *gb, uint8_t *data, size_t size) {
    if (!data || size != CUPID_GB_DISPLAY_CLOCK_STATE_SIZE ||
        !cupid_gb_display_clock_state_valid(gb))
        return false;
    const CupidGbDisplayClock *clock = &gb->display_clock;
    store_u32(data, (uint32_t)clock->credit_half_dots);
    store_u32(data + 4, clock->frame_age_dots);
    data[8] = (uint8_t)clock->row_half_dots;
    data[9] = (uint8_t)(clock->row_half_dots >> 8);
    data[10] = (uint8_t)clock->batch_half_dots;
    data[11] = (uint8_t)(clock->batch_half_dots >> 8);
    data[12] = clock->row;
    data[13] = clock->phase;
    data[14] = clock->cursor;
    data[15] = clock->window_alignment;
    data[16] = clock->flags;
    data[17] = clock->fine_wait_half_dots;
    data[18] = data[19] = 0;
    return true;
}

bool cupid_gb_display_clock_state_decode(CupidGb *gb, const uint8_t *data, size_t size) {
    if (!gb || !data || size != CUPID_GB_DISPLAY_CLOCK_STATE_SIZE || data[18] || data[19])
        return false;
    const uint32_t credit = load_u32(data);
    CupidGbDisplayClock clock = {0};
    clock.credit_half_dots =
        (int32_t)(credit <= INT32_MAX ? (int64_t)credit : (int64_t)credit - INT64_C(4294967296));
    clock.frame_age_dots = load_u32(data + 4);
    clock.row_half_dots = (uint16_t)((uint16_t)data[8] | (uint16_t)data[9] << 8);
    clock.batch_half_dots = (uint16_t)((uint16_t)data[10] | (uint16_t)data[11] << 8);
    clock.row = data[12];
    clock.phase = data[13];
    clock.cursor = data[14];
    clock.window_alignment = data[15];
    clock.flags = data[16];
    clock.fine_wait_half_dots = data[17];
    if (!cupid_gb_display_clock_capable(gb) || !valid_clock(&clock))
        return false;
    gb->display_clock = clock;
    return true;
}

void cupid_gb_display_clock_import_legacy(CupidGb *gb) {
    if (!gb)
        return;
    cupid_gb_display_clock_reset(gb);
    if (!cupid_gb_display_clock_capable(gb))
        return;
    CupidGbDisplayClock *clock = &gb->display_clock;
    clock->frame_age_dots = gb->display_cycles_since_frame % FRAME_DOTS;
    if (!lcd_on(gb)) {
        clock->phase = CUPID_GB_DISPLAY_CLOCK_IDLE_FRAME;
        clock->credit_half_dots = -(int32_t)((FRAME_DOTS - clock->frame_age_dots) * 2u);
        return;
    }
    const unsigned dot = gb->pixels.initialized ? gb->pixels.dot : gb->ppu_counter * 4u;
    clock->row = gb->pixels.initialized ? gb->pixels.line : gb->io_registers[0x44];
    clock->window_alignment = gb->speed_switch_alignment & 7u;
    clock->flags = CUPID_GB_DISPLAY_CLOCK_FORCED_FINE;
    if (gb->pixels.window_armed)
        clock->flags |= CUPID_GB_DISPLAY_CLOCK_WINDOW_Y;
    if (gb->pixels.window_active)
        clock->flags |= CUPID_GB_DISPLAY_CLOCK_WINDOW_ACTIVE;
    if (gb->ppu_lcd_startup)
        clock->flags |= CUPID_GB_DISPLAY_CLOCK_FIRST_ROW;
    clock->row_half_dots = (uint16_t)(dot * 2u);
    if (clock->row >= 144) {
        clock->phase = CUPID_GB_DISPLAY_CLOCK_VBLANK_GAP;
        clock->credit_half_dots = -(int32_t)((456u - dot) * 2u);
        clock->row_half_dots = 0;
    } else if (gb->pixels.active || (gb->io_registers[0x41] & 3u) == 3u) {
        clock->phase = CUPID_GB_DISPLAY_CLOCK_TRANSFER_FINE;
    } else if (dot < 80) {
        clock->phase = CUPID_GB_DISPLAY_CLOCK_OAM_FINE;
        clock->cursor = (uint8_t)(dot / 2u);
        clock->credit_half_dots = -(int32_t)((2u - (dot & 1u)) * 2u);
        clock->row_half_dots = 0;
    } else {
        clock->phase = CUPID_GB_DISPLAY_CLOCK_HBLANK_GAP;
        clock->credit_half_dots = dot < 454 ? -(int32_t)((454u - dot) * 2u) : 0;
        clock->row_half_dots = 0;
    }
}

/**
 * @file    display_clock.h
 * @brief   Retained LCD scheduling phase, synchronization events, and native-state codec.
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

#ifndef CUPID_GB_DISPLAY_CLOCK_INTERNAL_H
#define CUPID_GB_DISPLAY_CLOCK_INTERNAL_H

#include "cupid/gb/gb.h"

#define CUPID_GB_DISPLAY_CLOCK_STATE_SIZE 20u

typedef enum CupidGbDisplayClockPhase {
    CUPID_GB_DISPLAY_CLOCK_RESET = 0,
    CUPID_GB_DISPLAY_CLOCK_IDLE_FRAME,
    CUPID_GB_DISPLAY_CLOCK_STARTUP,
    CUPID_GB_DISPLAY_CLOCK_OAM_PREFIX,
    CUPID_GB_DISPLAY_CLOCK_OAM_QUIET,
    CUPID_GB_DISPLAY_CLOCK_OAM_FINE,
    CUPID_GB_DISPLAY_CLOCK_TRANSFER_PREFIX,
    CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET,
    CUPID_GB_DISPLAY_CLOCK_TRANSFER_FINE,
    CUPID_GB_DISPLAY_CLOCK_HBLANK_EDGES,
    CUPID_GB_DISPLAY_CLOCK_HBLANK_GAP,
    CUPID_GB_DISPLAY_CLOCK_LINE_TAIL,
    CUPID_GB_DISPLAY_CLOCK_VBLANK_EDGES,
    CUPID_GB_DISPLAY_CLOCK_VBLANK_GAP,
    CUPID_GB_DISPLAY_CLOCK_VBLANK_LAST,
    CUPID_GB_DISPLAY_CLOCK_ROW_ABORT
} CupidGbDisplayClockPhase;

enum {
    CUPID_GB_DISPLAY_CLOCK_FIRST_ROW = 1u << 0,
    CUPID_GB_DISPLAY_CLOCK_WINDOW_PENDING = 1u << 1,
    CUPID_GB_DISPLAY_CLOCK_WINDOW_Y = 1u << 2,
    CUPID_GB_DISPLAY_CLOCK_WINDOW_ACTIVE = 1u << 3,
    CUPID_GB_DISPLAY_CLOCK_WINDOW_JUST_CHECKED = 1u << 4,
    CUPID_GB_DISPLAY_CLOCK_FORCED_FINE = 1u << 5,
    CUPID_GB_DISPLAY_CLOCK_FLAGS = 0x3fu
};

bool cupid_gb_display_clock_capable(const CupidGb *gb);
void cupid_gb_display_clock_reset(CupidGb *gb);
void cupid_gb_display_clock_power(CupidGb *gb, bool enabled);
void cupid_gb_display_clock_window_write(CupidGb *gb);
void cupid_gb_display_clock_abort_object(CupidGb *gb);
void cupid_gb_display_clock_advance(CupidGb *gb, unsigned half_dots);
void cupid_gb_display_clock_advance_host(CupidGb *gb, unsigned half_dots);
void cupid_gb_display_clock_sync(CupidGb *gb);
bool cupid_gb_display_clock_sync_address(uint16_t address, bool write);

bool cupid_gb_display_clock_state_valid(const CupidGb *gb);
bool cupid_gb_display_clock_state_encode(const CupidGb *gb, uint8_t *data, size_t size);
bool cupid_gb_display_clock_state_decode(CupidGb *gb, const uint8_t *data, size_t size);
void cupid_gb_display_clock_import_legacy(CupidGb *gb);

#endif

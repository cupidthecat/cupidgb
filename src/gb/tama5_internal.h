/**
 * @file    tama5_internal.h
 * @brief   Internal TAMA5 bus, calendar and snapshot interfaces.
 * @author  cupidthecat
 * @date    2026-11-18
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#ifndef CUPID_GB_TAMA5_INTERNAL_H
#define CUPID_GB_TAMA5_INTERNAL_H

#include "cupid/gb/gb.h"

struct StateIo;

enum {
    CUPID_GB_TAMA5_CLOCK_BYTES = 8 + 1 + CUPID_GB_TAMA5_RTC_PAGES * CUPID_GB_TAMA5_RTC_DIGITS,
    CUPID_GB_TAMA5_STATE_SIZE = CUPID_GB_TAMA5_CLOCK_BYTES + 7
};

void cupid_gb_tama5_init(CupidGb *gb);
void cupid_gb_tama5_advance(CupidGbTama5 *device, int64_t now);
void cupid_gb_tama5_refresh_clock_cache(CupidGbTama5 *device);
bool cupid_gb_tama5_read(const CupidGb *gb, uint16_t address, uint8_t *nibble);
void cupid_gb_tama5_write(CupidGb *gb, uint16_t address, uint8_t value);
bool cupid_gb_tama5_clock_valid(const CupidGbTama5Clock *clock);
bool cupid_gb_tama5_state_valid(const CupidGb *gb);
void cupid_gb_tama5_clock_fields(struct StateIo *io, CupidGbTama5Clock *clock);
void cupid_gb_tama5_state(struct StateIo *io, CupidGb *gb);

#endif

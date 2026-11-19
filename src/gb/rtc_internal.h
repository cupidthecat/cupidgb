/**
 * @file    rtc_internal.h
 * @brief   Internal cartridge-clock scheduling and host-time access interfaces.
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

#ifndef CUPID_GB_RTC_INTERNAL_H
#define CUPID_GB_RTC_INTERNAL_H

#include "cupid/gb/gb.h"

int64_t cupid_gb_rtc_now(const CupidGb *gb);
/* Deliver one CPU/bus clock span after speed-switch freeze. Pixel/DMA
 * subdivisions must not create extra scheduler polls or consume extra debt. */
void cupid_gb_rtc_tick(CupidGb *gb, uint32_t half_cycles);
void cupid_gb_rtc_bind_cartridge(CupidGb *gb);
void cupid_gb_rtc_update_at(CupidGb *gb, int64_t now);

#endif

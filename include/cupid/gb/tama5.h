/**
 * @file    tama5.h
 * @brief   TAMA5 command latches and battery-backed calendar registers.
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

#ifndef CUPID_GB_TAMA5_H
#define CUPID_GB_TAMA5_H

#include <stdbool.h>
#include <stdint.h>

enum { CUPID_GB_TAMA5_RAM_SIZE = 32, CUPID_GB_TAMA5_RTC_PAGES = 4, CUPID_GB_TAMA5_RTC_DIGITS = 13 };

typedef struct CupidGbTama5Clock {
    int64_t base_time;
    uint8_t pages[CUPID_GB_TAMA5_RTC_PAGES][CUPID_GB_TAMA5_RTC_DIGITS];
    uint8_t control;
} CupidGbTama5Clock;

typedef struct CupidGbTama5 {
    CupidGbTama5Clock clock;
    uint8_t selected;
    uint8_t parameter;
    uint8_t address_high;
    uint8_t response;
    uint8_t cached_minute;
    uint8_t cached_hour;
    bool ready;
} CupidGbTama5;

#endif

/**
 * @file    workboy.h
 * @brief   Caller-owned serial keyboard and clock accessory.
 * @author  cupidthecat
 * @date    2026-11-24
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

#ifndef CUPID_GB_WORKBOY_H
#define CUPID_GB_WORKBOY_H

#include "cupid/gb/gb.h"

typedef struct CupidGbWorkBoy CupidGbWorkBoy;

/* Seconds since the Unix epoch. The device exchanges local calendar fields.
 * Callbacks must not reenter the core. NULL reads use epoch zero; NULL writes
 * discard the new value. No operation changes the host's system clock. */
typedef int64_t (*CupidGbWorkBoyReadClockCallback)(void *user_data);
typedef void (*CupidGbWorkBoyWriteClockCallback)(int64_t seconds, void *user_data);

enum {
    CUPID_GB_WORKBOY_REQUIRE_SHIFT = 0x40,
    CUPID_GB_WORKBOY_FORBID_SHIFT = 0x80,
    CUPID_GB_WORKBOY_KEY_NONE = 0xff,
    CUPID_GB_WORKBOY_KEY_CLOCK = 1,
    CUPID_GB_WORKBOY_KEY_TEMPERATURE = 2,
    CUPID_GB_WORKBOY_KEY_MONEY = 3,
    CUPID_GB_WORKBOY_KEY_CALCULATOR = 4,
    CUPID_GB_WORKBOY_KEY_DATE = 5,
    CUPID_GB_WORKBOY_KEY_CONVERSION = 6,
    CUPID_GB_WORKBOY_KEY_RECORD = 7,
    CUPID_GB_WORKBOY_KEY_WORLD = 8,
    CUPID_GB_WORKBOY_KEY_PHONE = 9,
    CUPID_GB_WORKBOY_KEY_ESCAPE = 10,
    CUPID_GB_WORKBOY_KEY_BACKSPACE = 11,
    CUPID_GB_WORKBOY_KEY_UNKNOWN = 12,
    CUPID_GB_WORKBOY_KEY_LEFT = 13,
    CUPID_GB_WORKBOY_KEY_Q = 17,
    CUPID_GB_WORKBOY_KEY_W = 18,
    CUPID_GB_WORKBOY_KEY_E = 19,
    CUPID_GB_WORKBOY_KEY_R = 20,
    CUPID_GB_WORKBOY_KEY_T = 21,
    CUPID_GB_WORKBOY_KEY_Y = 22,
    CUPID_GB_WORKBOY_KEY_U = 23,
    CUPID_GB_WORKBOY_KEY_I = 24,
    CUPID_GB_WORKBOY_KEY_O = 25,
    CUPID_GB_WORKBOY_KEY_P = 26,
    CUPID_GB_WORKBOY_KEY_A = 28,
    CUPID_GB_WORKBOY_KEY_S = 29,
    CUPID_GB_WORKBOY_KEY_D = 30,
    CUPID_GB_WORKBOY_KEY_F = 31,
    CUPID_GB_WORKBOY_KEY_G = 32,
    CUPID_GB_WORKBOY_KEY_H = 33,
    CUPID_GB_WORKBOY_KEY_J = 34,
    CUPID_GB_WORKBOY_KEY_K = 35,
    CUPID_GB_WORKBOY_KEY_L = 36,
    CUPID_GB_WORKBOY_KEY_COLON = 37,
    CUPID_GB_WORKBOY_KEY_ENTER = 38,
    CUPID_GB_WORKBOY_KEY_SHIFT_DOWN = 39,
    CUPID_GB_WORKBOY_KEY_Z = 40,
    CUPID_GB_WORKBOY_KEY_X = 41,
    CUPID_GB_WORKBOY_KEY_C = 42,
    CUPID_GB_WORKBOY_KEY_V = 43,
    CUPID_GB_WORKBOY_KEY_B = 44,
    CUPID_GB_WORKBOY_KEY_N = 45,
    CUPID_GB_WORKBOY_KEY_M = 46,
    CUPID_GB_WORKBOY_KEY_COMMA = 47 | CUPID_GB_WORKBOY_FORBID_SHIFT,
    CUPID_GB_WORKBOY_KEY_DOT = 48 | CUPID_GB_WORKBOY_FORBID_SHIFT,
    CUPID_GB_WORKBOY_KEY_SLASH = 49 | CUPID_GB_WORKBOY_FORBID_SHIFT,
    CUPID_GB_WORKBOY_KEY_SHIFT_UP = 50,
    CUPID_GB_WORKBOY_KEY_UMLAUT = 51,
    CUPID_GB_WORKBOY_KEY_SPACE = 52,
    CUPID_GB_WORKBOY_KEY_QUOTE = 53 | CUPID_GB_WORKBOY_FORBID_SHIFT,
    CUPID_GB_WORKBOY_KEY_UP = 54,
    CUPID_GB_WORKBOY_KEY_DOWN = 55,
    CUPID_GB_WORKBOY_KEY_RIGHT = 56,
};

CupidGbWorkBoy *cupid_gb_workboy_create(CupidGbWorkBoyReadClockCallback read_clock,
                                        CupidGbWorkBoyWriteClockCallback write_clock,
                                        void *user_data);
/* Detach the endpoint before destroying its caller-owned allocation. */
void cupid_gb_workboy_destroy(CupidGbWorkBoy *workboy);
/* Clear device history while retaining clock callbacks. */
void cupid_gb_workboy_reset(CupidGbWorkBoy *workboy);
/* Keep the factory's fields together. Rebinding this link does not reset it. */
CupidGbSerialLink cupid_gb_workboy_link(CupidGbWorkBoy *workboy);
bool cupid_gb_workboy_enabled(const CupidGbWorkBoy *workboy);
/* Replace the single pending key. Shift flags can be combined with a key code. */
void cupid_gb_workboy_set_key(CupidGbWorkBoy *workboy, uint8_t key);

#endif

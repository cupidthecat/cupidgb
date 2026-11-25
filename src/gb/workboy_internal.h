/**
 * @file    workboy_internal.h
 * @brief   Serial keyboard history and explicit native state codec.
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

#ifndef CUPID_GB_WORKBOY_INTERNAL_H
#define CUPID_GB_WORKBOY_INTERNAL_H

#include "cupid/gb/workboy.h"

enum {
    WORKBOY_STATE_CODEC_VERSION = 1,
    WORKBOY_STATE_WIRE_SIZE = 30,
    WORKBOY_CLOCK_BYTES = 21,
};

typedef struct CupidGbWorkBoyState {
    uint8_t transmit;
    bool output;
    uint8_t receive;
    uint8_t receive_bits;
    uint8_t mode;
    uint8_t key;
    bool shift;
    bool user_shift;
    uint8_t clock_data[WORKBOY_CLOCK_BYTES];
    uint8_t clock_index;
} CupidGbWorkBoyState;

struct CupidGbWorkBoy {
    CupidGbWorkBoyReadClockCallback read_clock;
    CupidGbWorkBoyWriteClockCallback write_clock;
    void *user_data;
    CupidGbWorkBoyState state;
};

CupidGbWorkBoy *cupid_gb_workboy_attached(const CupidGb *gb);
void cupid_gb_workboy_state_clear(CupidGbWorkBoyState *state);
bool cupid_gb_workboy_state_valid(const CupidGbWorkBoyState *state);
bool cupid_gb_workboy_state_encode(const CupidGbWorkBoyState *state, uint8_t *data, size_t length);
bool cupid_gb_workboy_state_decode(CupidGbWorkBoyState *state, const uint8_t *data, size_t length);

#endif

/**
 * @file    workboy.c
 * @brief   Serial keyboard polling, clock exchange, and device history codec.
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

#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "workboy_internal.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WORKBOY_POLL = 'O', WORKBOY_READ = 'R', WORKBOY_WRITE = 'W' };

static uint8_t clock_bcd(unsigned value) {
    return (uint8_t)((value / 10u) * 16u + value % 10u);
}

static int clock_decimal(uint8_t value) {
    return (int)(value >> 4) * 10 + (int)(value & 15u);
}

static void read_clock(CupidGbWorkBoy *device) {
    CupidGbWorkBoyState *state = &device->state;
    const int64_t seconds = device->read_clock ? device->read_clock(device->user_data) : 0;
    const time_t timestamp = (time_t)seconds;
    /* A failed host conversion has a deterministic calendar, without consulting
     * the wall clock or retaining bytes from an earlier transaction. */
    struct tm calendar = {.tm_year = 70, .tm_mday = 1, .tm_wday = 4};
    if ((int64_t)timestamp == seconds) {
        struct tm converted;
#ifdef _WIN32
        if (localtime_s(&converted, &timestamp) == 0)
#else
        if (localtime_r(&timestamp, &converted))
#endif
            calendar = converted;
    }
    memset(state->clock_data, 0, sizeof(state->clock_data));
    state->clock_data[0] = 4;
    state->clock_data[2] = clock_bcd((unsigned)calendar.tm_sec);
    state->clock_data[3] = clock_bcd((unsigned)calendar.tm_min);
    state->clock_data[4] = clock_bcd((unsigned)calendar.tm_hour);
    state->clock_data[5] = clock_bcd((unsigned)calendar.tm_mday);
    state->clock_data[6] =
        (uint8_t)(clock_bcd((unsigned)calendar.tm_mon + 1u) | (unsigned)calendar.tm_wday << 5);
    state->clock_data[15] = (uint8_t)calendar.tm_year;
    state->clock_index = 1;
}

static void write_clock(CupidGbWorkBoy *device) {
    const uint8_t *bytes = device->state.clock_data;
    struct tm calendar = {0};
    calendar.tm_sec = clock_decimal(bytes[7]);
    calendar.tm_min = clock_decimal(bytes[8]);
    calendar.tm_hour = clock_decimal(bytes[9]);
    calendar.tm_mday = clock_decimal(bytes[10]);
    calendar.tm_mon = clock_decimal(bytes[11] & 0x3fu) - 1;
    calendar.tm_year = (uint8_t)(bytes[20] + (bytes[10] >> 6));
    calendar.tm_isdst = -1;
    const time_t seconds = mktime(&calendar);
    if (device->write_clock)
        device->write_clock((int64_t)seconds, device->user_data);
}

static uint8_t poll_key(CupidGbWorkBoyState *state) {
    uint8_t key = state->key;
    if (key == CUPID_GB_WORKBOY_KEY_NONE)
        return key;
    const unsigned flag = key & CUPID_GB_WORKBOY_REQUIRE_SHIFT  ? CUPID_GB_WORKBOY_REQUIRE_SHIFT
                          : key & CUPID_GB_WORKBOY_FORBID_SHIFT ? CUPID_GB_WORKBOY_FORBID_SHIFT
                                                                : 0;
    if (flag) {
        state->key = (uint8_t)(key & ~flag);
        const bool wanted = flag == CUPID_GB_WORKBOY_REQUIRE_SHIFT;
        if (state->shift != wanted) {
            state->shift = wanted;
            return wanted ? CUPID_GB_WORKBOY_KEY_SHIFT_DOWN : CUPID_GB_WORKBOY_KEY_SHIFT_UP;
        }
        key = state->key;
    } else if (key == CUPID_GB_WORKBOY_KEY_SHIFT_DOWN || key == CUPID_GB_WORKBOY_KEY_SHIFT_UP) {
        state->shift = key == CUPID_GB_WORKBOY_KEY_SHIFT_DOWN;
        state->user_shift = state->shift;
    }
    state->key = CUPID_GB_WORKBOY_KEY_NONE;
    return key;
}

static void receive_byte(CupidGbWorkBoy *device, uint8_t incoming) {
    CupidGbWorkBoyState *state = &device->state;
    state->transmit = 0;
    if (state->mode == WORKBOY_WRITE) {
        state->transmit = 'D';
        const unsigned position = state->clock_index++;
        if (position >= 2 && position - 2 < WORKBOY_CLOCK_BYTES) {
            state->clock_data[position - 2] = incoming;
            if (position == WORKBOY_CLOCK_BYTES + 1) {
                write_clock(device);
                state->mode = WORKBOY_POLL;
            }
        }
        return;
    }
    if (incoming == WORKBOY_READ || incoming == WORKBOY_WRITE) {
        state->mode = incoming;
        state->key = CUPID_GB_WORKBOY_KEY_NONE;
        state->transmit = 'D';
        if (incoming == WORKBOY_READ)
            read_clock(device);
        else
            state->clock_index = 0;
        return;
    }
    if (incoming == WORKBOY_POLL || state->mode == WORKBOY_POLL) {
        state->mode = WORKBOY_POLL;
        state->transmit = poll_key(state);
        return;
    }
    if (state->mode == WORKBOY_READ && state->clock_index < WORKBOY_CLOCK_BYTES * 2) {
        const unsigned index = state->clock_index++;
        const uint8_t value = state->clock_data[index / 2];
        const unsigned nibble = index & 1u ? value & 15u : value >> 4;
        state->transmit = (uint8_t)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}

static void launch(const CupidGb *gb, bool outgoing, void *user_data) {
    (void)gb;
    CupidGbWorkBoy *device = user_data;
    CupidGbWorkBoyState *state = &device->state;
    state->receive = (uint8_t)((state->receive << 1) | outgoing);
    if (++state->receive_bits == 8) {
        const uint8_t incoming = state->receive;
        state->receive = state->receive_bits = 0;
        receive_byte(device, incoming);
    }
}

static bool sample(const CupidGb *gb, void *user_data) {
    (void)gb;
    CupidGbWorkBoyState *state = &((CupidGbWorkBoy *)user_data)->state;
    const bool bit = state->output;
    state->output = (state->transmit & 0x80u) != 0;
    state->transmit <<= 1;
    return bit;
}

CupidGbWorkBoy *cupid_gb_workboy_create(CupidGbWorkBoyReadClockCallback read_clock_callback,
                                        CupidGbWorkBoyWriteClockCallback write_clock_callback,
                                        void *user_data) {
    CupidGbWorkBoy *device = calloc(1, sizeof(*device));
    if (device) {
        device->read_clock = read_clock_callback;
        device->write_clock = write_clock_callback;
        device->user_data = user_data;
    }
    return device;
}

void cupid_gb_workboy_destroy(CupidGbWorkBoy *workboy) {
    free(workboy);
}

void cupid_gb_workboy_state_clear(CupidGbWorkBoyState *state) {
    if (state)
        memset(state, 0, sizeof(*state));
}

void cupid_gb_workboy_reset(CupidGbWorkBoy *workboy) {
    if (workboy)
        cupid_gb_workboy_state_clear(&workboy->state);
}

CupidGbSerialLink cupid_gb_workboy_link(CupidGbWorkBoy *workboy) {
    return workboy ? (CupidGbSerialLink){.launch = launch,
                                         .sample = sample,
                                         .user_data = workboy,
                                         .device = CUPID_GB_SERIAL_DEVICE_WORKBOY}
                   : (CupidGbSerialLink){0};
}

CupidGbWorkBoy *cupid_gb_workboy_attached(const CupidGb *gb) {
    if (!gb || gb->serial_link.device != CUPID_GB_SERIAL_DEVICE_WORKBOY ||
        gb->serial_link.launch != launch || gb->serial_link.sample != sample)
        return NULL;
    return gb->serial_link.user_data;
}

bool cupid_gb_workboy_enabled(const CupidGbWorkBoy *workboy) {
    return workboy && workboy->state.mode != 0;
}

void cupid_gb_workboy_set_key(CupidGbWorkBoy *workboy, uint8_t key) {
    if (!workboy)
        return;
    CupidGbWorkBoyState *state = &workboy->state;
    if (state->shift != state->user_shift &&
        !(key & (CUPID_GB_WORKBOY_REQUIRE_SHIFT | CUPID_GB_WORKBOY_FORBID_SHIFT)))
        key |= state->user_shift ? CUPID_GB_WORKBOY_REQUIRE_SHIFT : CUPID_GB_WORKBOY_FORBID_SHIFT;
    state->key = key;
}

bool cupid_gb_workboy_state_valid(const CupidGbWorkBoyState *state) {
    return state && state->receive_bits < 8 && state->receive < (1u << state->receive_bits) &&
           (state->mode == 0 || state->mode == WORKBOY_POLL || state->mode == WORKBOY_READ ||
            state->mode == WORKBOY_WRITE) &&
           state->clock_index <= WORKBOY_CLOCK_BYTES * 2 &&
           (state->mode != WORKBOY_WRITE || state->clock_index < WORKBOY_CLOCK_BYTES + 2);
}

bool cupid_gb_workboy_state_encode(const CupidGbWorkBoyState *state, uint8_t *data, size_t length) {
    if (!data || length != WORKBOY_STATE_WIRE_SIZE || !cupid_gb_workboy_state_valid(state))
        return false;
    data[0] = state->transmit;
    data[1] = state->output;
    data[2] = state->receive;
    data[3] = state->receive_bits;
    data[4] = state->mode;
    data[5] = state->key;
    data[6] = state->shift;
    data[7] = state->user_shift;
    memcpy(data + 8, state->clock_data, WORKBOY_CLOCK_BYTES);
    data[29] = state->clock_index;
    return true;
}

bool cupid_gb_workboy_state_decode(CupidGbWorkBoyState *state, const uint8_t *data, size_t length) {
    if (!state || !data || length != WORKBOY_STATE_WIRE_SIZE || data[1] > 1 || data[6] > 1 ||
        data[7] > 1)
        return false;
    CupidGbWorkBoyState candidate = {0};
    candidate.transmit = data[0];
    candidate.output = data[1] != 0;
    candidate.receive = data[2];
    candidate.receive_bits = data[3];
    candidate.mode = data[4];
    candidate.key = data[5];
    candidate.shift = data[6] != 0;
    candidate.user_shift = data[7] != 0;
    memcpy(candidate.clock_data, data + 8, WORKBOY_CLOCK_BYTES);
    candidate.clock_index = data[29];
    if (!cupid_gb_workboy_state_valid(&candidate))
        return false;
    *state = candidate;
    return true;
}

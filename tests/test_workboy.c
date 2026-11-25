/**
 * @file    test_workboy.c
 * @brief   Serial keyboard polling, clocks, and partial-transfer regressions.
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

#include "../src/gb/workboy_internal.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct Clock {
    int64_t seconds;
    unsigned reads;
    unsigned writes;
} Clock;

typedef struct Rig {
    CupidGb *gb;
    CupidGbWorkBoy *device;
    Clock clock;
    bool fast;
} Rig;

static int64_t read_clock(void *context) {
    Clock *clock = context;
    ++clock->reads;
    return clock->seconds;
}

static void write_clock(int64_t seconds, void *context) {
    Clock *clock = context;
    ++clock->writes;
    clock->seconds = seconds;
}

static void create_rig(Rig *rig, CupidGbModel model, bool fast) {
    memset(rig, 0, sizeof(*rig));
    rig->gb = calloc(1, sizeof(*rig->gb));
    assert(rig->gb);
    rig->gb->model = model;
    cupid_gb_init(rig->gb);
    rig->fast = fast;
    rig->clock.seconds = INT64_C(1704164645);
    rig->device = cupid_gb_workboy_create(read_clock, write_clock, &rig->clock);
    assert(rig->device);
    cupid_gb_set_serial_link(rig->gb, cupid_gb_workboy_link(rig->device));
    assert(cupid_gb_workboy_attached(rig->gb) == rig->device);
}

static void destroy_rig(Rig *rig) {
    cupid_gb_set_serial_link(rig->gb, (CupidGbSerialLink){0});
    cupid_gb_workboy_destroy(rig->device);
    cupid_gb_cleanup(rig->gb);
    free(rig->gb);
}

static uint8_t send_byte(Rig *rig, uint8_t value) {
    cupid_gb_write_u8(rig->gb, 0xff01, value);
    cupid_gb_write_u8(rig->gb, 0xff02, rig->fast ? 0x83 : 0x81);
    unsigned clocks = 0;
    while (cupid_gb_read_u8(rig->gb, 0xff02) & 0x80u) {
        assert(++clocks <= 1100);
        cupid_gb_tick(rig->gb, 1);
    }
    return cupid_gb_read_u8(rig->gb, 0xff01);
}

static unsigned hex_digit(char value) {
    return (unsigned)(value <= '9' ? value - '0' : value - 'a' + 10);
}

static void exchange(Rig *rig, const uint8_t *input, size_t size, const char *expected) {
    assert(strlen(expected) == size * 2);
    for (size_t index = 0; index < size; ++index) {
        const unsigned wanted =
            hex_digit(expected[index * 2]) * 16 + hex_digit(expected[index * 2 + 1]);
        const uint8_t result = send_byte(rig, input[index]);
        if (result != wanted)
            fprintf(stderr, "Keyboard model %u fast %u byte %zu: %02x, expected %02x\n",
                    rig->gb->model, rig->fast, index, result, wanted);
        assert(result == wanted);
    }
}

static void test_poll_and_clock(CupidGbModel model, bool fast) {
    Rig rig;
    create_rig(&rig, model, fast);
    const uint8_t enter[] = {'O', 0, 0};
    const uint8_t idle[5] = {0};
    assert(!cupid_gb_workboy_enabled(rig.device));
    exchange(&rig, enter, sizeof(enter), "0000ff");
    assert(cupid_gb_workboy_enabled(rig.device));

    const struct {
        uint8_t key;
        const char *reply;
    } keys[] = {{28, "ff1cffffff"},
                {28 | CUPID_GB_WORKBOY_REQUIRE_SHIFT, "ff271cffff"},
                {27 | CUPID_GB_WORKBOY_FORBID_SHIFT, "ff321bffff"},
                {CUPID_GB_WORKBOY_KEY_SHIFT_DOWN, "ff27ffffff"},
                {CUPID_GB_WORKBOY_KEY_SHIFT_UP, "ff32ffffff"}};

    for (size_t index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        cupid_gb_workboy_set_key(rig.device, keys[index].key);
        exchange(&rig, idle, sizeof(idle), keys[index].reply);
    }
    uint8_t read[46] = {'R'};
    exchange(&rig, read, sizeof(read),
             "ff44343030303530343033303234313030303030303030303030303030303037433030303030303030303"
             "0000000");
    assert(rig.clock.reads == 1 && rig.clock.writes == 0);
    uint8_t write[27] = {'W'};
    const uint8_t calendar[] = {5, 4, 3, 2, 1};
    memcpy(write + 10, calendar, sizeof(calendar));
    write[23] = 124;
    for (size_t index = 0; index < sizeof(write); ++index)
        assert(send_byte(&rig, write[index]) == (index == 0 ? 0 : index <= 24 ? 'D' : 0xff));
    assert(rig.clock.reads == 1 && rig.clock.writes == 1);
    assert(rig.clock.seconds == INT64_C(1704164645));

    /* Synthetic shift affects the guest latch, while the user's shift remains
     * up. A subsequent ordinary key first repairs that difference. */
    cupid_gb_workboy_set_key(rig.device, 28 | CUPID_GB_WORKBOY_REQUIRE_SHIFT);
    exchange(&rig, idle, sizeof(idle), "ff271cffff");
    cupid_gb_workboy_set_key(rig.device, CUPID_GB_WORKBOY_KEY_S);
    exchange(&rig, idle, sizeof(idle), "ff321dffff");
    cupid_gb_workboy_set_key(rig.device, CUPID_GB_WORKBOY_KEY_A);
    cupid_gb_workboy_set_key(rig.device, CUPID_GB_WORKBOY_KEY_D);
    exchange(&rig, idle, sizeof(idle), "ff1effffff");
    cupid_gb_workboy_set_key(rig.device, CUPID_GB_WORKBOY_KEY_SHIFT_DOWN);
    exchange(&rig, idle, sizeof(idle), "ff27ffffff");
    cupid_gb_workboy_set_key(rig.device, 27 | CUPID_GB_WORKBOY_FORBID_SHIFT);
    exchange(&rig, idle, sizeof(idle), "ff321bffff");
    cupid_gb_workboy_set_key(rig.device, CUPID_GB_WORKBOY_KEY_A);
    exchange(&rig, idle, sizeof(idle), "ff271cffff");
    cupid_gb_workboy_set_key(rig.device, CUPID_GB_WORKBOY_KEY_SHIFT_UP);
    exchange(&rig, idle, sizeof(idle), "ff32ffffff");

    const unsigned reads = rig.clock.reads, writes = rig.clock.writes;
    cupid_gb_workboy_reset(rig.device);
    assert(!cupid_gb_workboy_enabled(rig.device));
    const uint8_t restart[] = {'O', 0, 0, 0};
    exchange(&rig, restart, sizeof(restart), "0000ffff");
    assert(rig.clock.reads == reads && rig.clock.writes == writes);
    cupid_gb_workboy_set_key(rig.device, CUPID_GB_WORKBOY_KEY_A);
    cupid_gb_set_serial_link(rig.gb, cupid_gb_workboy_link(rig.device));
    exchange(&rig, idle, 4, "ff1cffff");
    cupid_gb_set_serial_link(rig.gb, (CupidGbSerialLink){0});
    assert(!cupid_gb_workboy_attached(rig.gb));
    exchange(&rig, idle, 4, "ffffffff");
    cupid_gb_workboy_reset(rig.device);
    cupid_gb_set_serial_link(rig.gb, cupid_gb_workboy_link(rig.device));
    exchange(&rig, restart, sizeof(restart), "0000ffff");
    destroy_rig(&rig);
}

static bool direct_bit(CupidGbSerialLink link, bool outgoing) {
    link.launch(NULL, outgoing, link.user_data);
    return link.sample(NULL, link.user_data);
}

static void test_codec_continuation(void) {
    uint8_t input[80] = {'O', 0, 0, 'R'};
    input[49] = 'W';
    input[59] = 5;
    input[60] = 4;
    input[61] = 3;
    input[62] = 2;
    input[63] = 1;
    input[72] = 124;
    /* Cut both sides of every byte boundary, including the buffered clock read
     * and final clock-write callback. Only explicit decode restores the device. */
    for (size_t cut = 0; cut <= sizeof(input) * 8; ++cut) {
        Clock source_clock = {INT64_C(1704164645), 0, 0};
        Clock target_clock = {0, 0, 0};
        CupidGbWorkBoy *source = cupid_gb_workboy_create(read_clock, write_clock, &source_clock);
        CupidGbWorkBoy *target = cupid_gb_workboy_create(read_clock, write_clock, &target_clock);
        assert(source && target);
        CupidGbSerialLink left = cupid_gb_workboy_link(source);
        CupidGbSerialLink right = cupid_gb_workboy_link(target);
        for (size_t bit = 0; bit < cut; ++bit)
            direct_bit(left, (input[bit / 8] & (0x80u >> (bit % 8))) != 0);
        uint8_t wire[WORKBOY_STATE_WIRE_SIZE];
        const unsigned reads = source_clock.reads, writes = source_clock.writes;
        assert(cupid_gb_workboy_state_encode(&source->state, wire, sizeof(wire)));
        assert(cupid_gb_workboy_state_decode(&target->state, wire, sizeof(wire)));
        assert(source_clock.reads == reads && source_clock.writes == writes);
        assert(target_clock.reads == 0 && target_clock.writes == 0);
        target_clock.seconds = source_clock.seconds;
        if (source->state.mode == 'R')
            target_clock.seconds += 86400;
        for (size_t bit = cut; bit < sizeof(input) * 8; ++bit) {
            const bool outgoing = (input[bit / 8] & (0x80u >> (bit % 8))) != 0;
            assert(direct_bit(left, outgoing) == direct_bit(right, outgoing));
        }
        uint8_t restored[WORKBOY_STATE_WIRE_SIZE];
        assert(cupid_gb_workboy_state_encode(&source->state, wire, sizeof(wire)));
        assert(cupid_gb_workboy_state_encode(&target->state, restored, sizeof(restored)));
        assert(memcmp(wire, restored, sizeof(wire)) == 0);
        assert(source_clock.seconds == target_clock.seconds);
        assert(target_clock.reads == source_clock.reads - reads);
        assert(target_clock.writes == source_clock.writes - writes);
        cupid_gb_workboy_destroy(source);
        cupid_gb_workboy_destroy(target);
    }
}

static void test_codec_rejection(void) {
    CupidGbWorkBoyState state = {0};
    uint8_t wire[WORKBOY_STATE_WIRE_SIZE] = {0};
    assert(cupid_gb_workboy_state_encode(&state, wire, sizeof(wire)));

    const struct {
        unsigned offset;
        uint8_t value;
    } invalid[] = {
        {1, 2}, {6, 2}, {7, 2}, {3, 8}, {2, 1}, {4, 'X'}, {29, 43},
    };

    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        uint8_t bad[WORKBOY_STATE_WIRE_SIZE];
        memcpy(bad, wire, sizeof(bad));
        bad[invalid[index].offset] = invalid[index].value;
        const CupidGbWorkBoyState before = state;
        assert(!cupid_gb_workboy_state_decode(&state, bad, sizeof(bad)));
        assert(memcmp(&state, &before, sizeof(state)) == 0);
    }
    wire[4] = 'W';
    wire[29] = 23;
    assert(!cupid_gb_workboy_state_decode(&state, wire, sizeof(wire)));
    wire[4] = 'O';
    wire[29] = 42;
    memset(wire + 8, 0xff, WORKBOY_CLOCK_BYTES);
    assert(cupid_gb_workboy_state_decode(&state, wire, sizeof(wire)));
    assert(!cupid_gb_workboy_state_decode(&state, wire, sizeof(wire) - 1));
    assert(!cupid_gb_workboy_state_encode(&state, wire, sizeof(wire) - 1));
}

static void foreign_launch(const CupidGb *gb, bool outgoing, void *context) {
    (void)gb;
    (void)outgoing;
    (void)context;
}

static void test_registration(void) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    CupidGbWorkBoy *device = cupid_gb_workboy_create(NULL, NULL, NULL);
    assert(device);
    CupidGbSerialLink link = cupid_gb_workboy_link(device);
    cupid_gb_set_serial_link(gb, link);
    assert(cupid_gb_workboy_attached(gb) == device);
    link.launch = foreign_launch;
    cupid_gb_set_serial_link(gb, link);
    assert(!cupid_gb_workboy_attached(gb));
    link = cupid_gb_workboy_link(NULL);
    assert(!link.launch && !link.sample && !link.user_data && !link.device);
    cupid_gb_set_serial_link(gb, link);
    assert(!cupid_gb_workboy_enabled(NULL));
    cupid_gb_workboy_reset(NULL);
    cupid_gb_workboy_set_key(NULL, 28);
    cupid_gb_workboy_destroy(device);
    free(gb);
}

int main(void) {
#ifdef _WIN32
    assert(_putenv_s("TZ", "UTC0") == 0);
    _tzset();
#else
    assert(setenv("TZ", "UTC0", 1) == 0);
    tzset();
#endif
    test_poll_and_clock(CUPID_GB_MODEL_DMG_ABC, false);
    test_poll_and_clock(CUPID_GB_MODEL_CGB, false);
    test_poll_and_clock(CUPID_GB_MODEL_CGB, true);
    test_codec_continuation();
    test_codec_rejection();
    test_registration();
    return 0;
}

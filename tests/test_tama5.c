/**
 * @file    test_tama5.c
 * @brief   Generated TAMA5 nibble protocol, banking, battery, and continuation regressions.
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

#include "cupid/common/sha256.h"
#include "cupid/gb/battery.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum { ROM_BYTES = 512 * 1024, SAVE_BYTES = 32 };

static CupidGb *machine(CupidGbModel model) {
    uint8_t *rom = calloc(1, ROM_BYTES);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(rom && gb);
    for (unsigned bank = 0; bank < 32; ++bank) {
        memset(rom + bank * 0x4000u, (int)(bank ^ 0xa5u), 0x4000u);
    }
    rom[0x143] = 0x80;
    rom[0x147] = 0xfd;
    rom[0x148] = 4;
    rom[0x149] = 0;
    rom[0x14d] = 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, ROM_BYTES));
    free(rom);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void latch(CupidGb *gb, uint8_t selected, uint8_t value) {
    cupid_gb_write_u8(gb, 0xa001, selected);
    cupid_gb_write_u8(gb, 0xa000, value);
}

static uint8_t output(CupidGb *gb, uint8_t selected) {
    cupid_gb_write_u8(gb, 0xa001, selected);
    return cupid_gb_read_u8(gb, 0xa000) & 15u;
}

static void ready(CupidGb *gb) {
    cupid_gb_write_u8(gb, 0xa001, 0x0a);
    /* Physical status checks use only the two low bits. Upper bits float. */
    for (unsigned count = 0; count < 8192; ++count) {
        if ((cupid_gb_read_u8(gb, 0xa000) & 3u) == 1)
            return;
        cupid_gb_step(gb);
    }
    assert(!"TAMA5 readiness deadline expired");
}

static void write_save(CupidGb *gb, unsigned address, uint8_t value) {
    latch(gb, 4, value & 15u);
    latch(gb, 5, value >> 4u);
    /* Writing command-low commits the prepared data and high address. */
    latch(gb, 6, (uint8_t)(address >> 4u));
    latch(gb, 7, (uint8_t)(address & 15u));
    ready(gb);
    assert(cupid_gb_read_u8(gb, 0xa000) == 0xf1);
}

static uint8_t read_save(CupidGb *gb, unsigned address) {
    latch(gb, 6, (uint8_t)(2u | (address >> 4u)));
    latch(gb, 7, (uint8_t)(address & 15u));
    ready(gb);
    const uint8_t high = output(gb, 0x0d);
    return (uint8_t)((high << 4u) | output(gb, 0x0c));
}

static void clock_write(CupidGb *gb, unsigned page, unsigned digit, unsigned value) {
    latch(gb, 4, (uint8_t)digit);
    latch(gb, 5, (uint8_t)value);
    latch(gb, 6, 8);
    latch(gb, 7, (uint8_t)(page * 2u));
}

static uint8_t clock_read(CupidGb *gb, unsigned page, unsigned digit) {
    latch(gb, 4, (uint8_t)digit);
    latch(gb, 6, 8);
    latch(gb, 7, (uint8_t)(page * 2u + 1u));
    return output(gb, 0xc);
}

static void mcu(CupidGb *gb, uint8_t command, uint8_t parameter) {
    latch(gb, 4, parameter & 15u);
    latch(gb, 5, parameter >> 4u);
    latch(gb, 6, command >> 4u);
    latch(gb, 7, command & 15u);
}

static int64_t clock_now(void *context) {
    return *(int64_t *)context;
}

static uint8_t pattern(unsigned address) {
    return (uint8_t)((address * 91u) ^ (address >> 1u) ^ 0xd6u);
}

static uint8_t *snapshot(CupidGb *gb, size_t *size) {
    *size = cupid_gb_state_size(gb);
    uint8_t *bytes = malloc(*size);
    size_t written = 0;
    assert(bytes);
    assert(cupid_gb_save_state(gb, bytes, *size, &written) == CUPID_GB_STATE_OK);
    assert(written == *size && bytes[8] == CUPID_GB_STATE_VERSION);
    return bytes;
}

static void assert_snapshot(CupidGb *gb, const uint8_t *bytes, size_t size) {
    size_t actual_size;
    uint8_t *actual = snapshot(gb, &actual_size);
    assert(size == actual_size && !memcmp(bytes, actual, size));
    free(actual);
}

static void test_banks(CupidGbModel model) {
    CupidGb *gb = machine(model);
    ready(gb);
    const uint64_t generation = gb->battery_write_generation;
    for (unsigned bank = 0; bank < 32; ++bank) {
        latch(gb, 0, (uint8_t)(0xf0u | (bank & 15u)));
        latch(gb, 1, (uint8_t)(0xfeu | (bank >> 4u)));
        assert(cupid_gb_read_u8(gb, 0x200) == 0xa5);
        assert(cupid_gb_read_u8(gb, 0x4200) == (bank ^ 0xa5u));
        assert(cupid_gb_read_bus_u8(gb, 0x7fff) == (bank ^ 0xa5u));
    }
    assert(gb->battery_write_generation == generation);
    destroy(gb);
}

static void test_storage(CupidGbModel model) {
    CupidGb *gb = machine(model);
    ready(gb);
    const uint64_t fresh = gb->battery_write_generation;
    for (unsigned address = 0; address < SAVE_BYTES; ++address)
        assert(read_save(gb, address) == 0xff);
    assert(gb->battery_write_generation == fresh);
    for (unsigned address = 0; address < SAVE_BYTES; ++address)
        write_save(gb, address, pattern(address));
    const uint64_t dirty = gb->battery_write_generation;
    assert(dirty > fresh);
    for (unsigned address = SAVE_BYTES; address-- > 0;)
        assert(read_save(gb, address) == pattern(address));
    assert(gb->battery_write_generation == dirty);
    for (unsigned address = 0; address < SAVE_BYTES; ++address)
        write_save(gb, address, pattern(address));
    assert(gb->battery_write_generation == dirty);
    /* A data latch is not a storage write before command-low is written. */
    latch(gb, 4, 7);
    latch(gb, 5, 6);
    latch(gb, 6, 0);
    assert(gb->battery_write_generation == dirty);
    assert(read_save(gb, 3) == pattern(3));
    destroy(gb);
}

static void test_battery(CupidGbModel model) {
    CupidGb *first = machine(model), *second = machine(model);
    ready(first);
    ready(second);
    for (unsigned address = 0; address < SAVE_BYTES; ++address)
        write_save(first, address, pattern(address));
    const size_t size = cupid_gb_battery_size(first);
    assert(size > SAVE_BYTES);
    uint8_t *battery = malloc(size + 1);
    size_t written = 0;
    assert(battery);
    assert(cupid_gb_save_battery_at(first, battery, size, &written, 0) == CUPID_GB_BATTERY_OK);
    assert(written == size);
    size_t before_size;
    uint8_t *before = snapshot(second, &before_size);
    assert(cupid_gb_load_battery_at(second, battery, size - 1, 0) == CUPID_GB_BATTERY_INVALID_SIZE);
    assert_snapshot(second, before, before_size);
    assert(cupid_gb_load_battery_at(second, battery, size + 1, 0) == CUPID_GB_BATTERY_INVALID_SIZE);
    assert_snapshot(second, before, before_size);
    assert(cupid_gb_save_battery_at(second, battery, size - 1, NULL, 0) ==
           CUPID_GB_BATTERY_BUFFER_TOO_SMALL);
    assert_snapshot(second, before, before_size);
    free(before);
    assert(cupid_gb_load_battery_at(second, battery, size, 0) == CUPID_GB_BATTERY_OK);
    for (unsigned address = 0; address < SAVE_BYTES; ++address)
        assert(read_save(second, address) == pattern(address));
    uint8_t raw[SAVE_BYTES];
    for (unsigned address = 0; address < SAVE_BYTES; ++address)
        raw[address] = (uint8_t)(pattern(address) ^ 0xffu);
    assert(cupid_gb_load_battery_at(second, raw, sizeof(raw), 0) == CUPID_GB_BATTERY_OK);
    for (unsigned address = 0; address < SAVE_BYTES; ++address)
        assert(read_save(second, address) == raw[address]);
    free(battery);
    destroy(first);
    destroy(second);
}

static void test_reset(CupidGbModel model) {
    CupidGb *gb = machine(model);
    ready(gb);
    for (unsigned address = 0; address < SAVE_BYTES; ++address)
        write_save(gb, address, pattern(address));
    for (unsigned kind = CUPID_GB_RESET_HARD; kind <= CUPID_GB_RESET_QUICK; ++kind) {
        latch(gb, 0, 9);
        latch(gb, 1, 1);
        latch(gb, 4, 0);
        latch(gb, 5, 0);
        assert(cupid_gb_reset(gb, (CupidGbResetKind)kind));
        ready(gb);
        for (unsigned address = 0; address < SAVE_BYTES; ++address)
            assert(read_save(gb, address) == pattern(address));
        /* A pre-reset partial write cannot become the first post-reset command. */
        write_save(gb, 7, 0x96);
        assert(read_save(gb, 7) == 0x96);
        write_save(gb, 7, pattern(7));
    }
    destroy(gb);
}

static void test_continuation(CupidGbModel model) {
    CupidGb *first = machine(model), *second = machine(model);
    ready(first);
    ready(second);
    for (unsigned prefix = 0; prefix < 4; ++prefix) {
        const uint8_t selectors[] = {4, 5, 6, 7};
        const uint8_t values[] = {0x0b, 6, 1, 0x0e};
        ready(first);
        for (unsigned i = 0; i <= prefix; ++i) {
            cupid_gb_write_u8(first, 0xa001, selectors[i]);
            if (i < prefix)
                cupid_gb_write_u8(first, 0xa000, values[i]);
        }
        size_t size;
        uint8_t *saved = snapshot(first, &size);
        assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_OK);
        assert_snapshot(second, saved, size);
        cupid_gb_write_u8(first, 0xa000, values[prefix]);
        cupid_gb_write_u8(second, 0xa000, values[prefix]);
        for (unsigned i = prefix + 1; i < 4; ++i) {
            latch(first, selectors[i], values[i]);
            latch(second, selectors[i], values[i]);
        }
        ready(first);
        ready(second);
        assert(read_save(first, 30) == 0x6b);
        assert(read_save(second, 30) == 0x6b);
        free(saved);
        saved = snapshot(first, &size);
        assert_snapshot(second, saved, size);
        saved[size - 1] ^= 1u;
        assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_INVALID);
        saved[size - 1] ^= 1u;
        assert_snapshot(second, saved, size);
        assert(cupid_gb_load_state_live(second, saved, size) == CUPID_GB_STATE_OK);
        assert(read_save(second, 30) == 0x6b);
        free(saved);
        write_save(first, 30, 0xff);
        write_save(second, 30, 0xff);
    }
    destroy(first);
    destroy(second);
}

static void test_clock_digits(CupidGbModel model) {
    static const uint8_t masks[][13] = {{15, 7, 15, 7, 15, 3, 7, 15, 3, 15, 1, 15, 15},
                                        {0, 0, 15, 7, 15, 3, 7, 15, 3, 0, 1, 3, 0}};
    CupidGb *gb = machine(model);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_read_u8(gb, 0xa000) == 0xff);
    ready(gb);
    cupid_gb_write_u8(gb, 0xa000, 0x51);
    assert(cupid_gb_read_bus_u8(gb, 0xa000) == 0x51);
    for (unsigned page = 0; page < 4; ++page) {
        for (unsigned digit = 0; digit < 13; ++digit) {
            clock_write(gb, page, digit, 15);
            assert(clock_read(gb, page, digit) == (page < 2 ? masks[page][digit] : 15));
        }
        clock_write(gb, page, 13, 15);
        clock_write(gb, page, 14, 15);
        clock_write(gb, page, 15, 15);
        assert(clock_read(gb, page, 13) == page);
        assert(clock_read(gb, page, 14) == 0);
        assert(clock_read(gb, page, 15) == 0);
    }
    mcu(gb, 0x51, 0);
    assert(clock_read(gb, 2, 13) == 6);
    mcu(gb, 0x50, 0);
    assert(clock_read(gb, 2, 13) == 2);
    destroy(gb);
}

static void test_calendar(CupidGbModel model) {
    CupidGb *gb = machine(model);
    int64_t now = 10000;
    cupid_gb_set_rtc_time_source(gb, clock_now, &now);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_SYNC_TO_HOST));
    ready(gb);
    mcu(gb, 0x41, 0);
    /* 1999-12-31 23:59:59, with the year counter at the end of a leap cycle. */
    static const uint8_t midnight[] = {9, 5, 9, 5, 3, 2, 6, 1, 3, 2, 1, 9, 9};
    for (unsigned i = 0; i < sizeof(midnight); ++i)
        clock_write(gb, 0, i, midnight[i]);
    clock_write(gb, 1, 11, 3);
    ++now;
    static const uint8_t new_year[] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0};
    for (unsigned i = 0; i < sizeof(new_year); ++i)
        assert(clock_read(gb, 0, i) == new_year[i]);
    assert(clock_read(gb, 1, 11) == 0);
    clock_write(gb, 0, 7, 8);
    clock_write(gb, 0, 8, 2);
    clock_write(gb, 0, 9, 2);
    now += 86400;
    assert(clock_read(gb, 0, 7) == 9);
    assert(clock_read(gb, 0, 8) == 2);
    now += 86400;
    assert(clock_read(gb, 0, 7) == 1);
    assert(clock_read(gb, 0, 8) == 0);
    assert(clock_read(gb, 0, 9) == 3);
    clock_write(gb, 1, 10, 0);
    mcu(gb, 0x45, 0x11);
    assert(clock_read(gb, 0, 4) == 1);
    assert(clock_read(gb, 0, 5) == 1);
    now += 3600;
    assert(clock_read(gb, 0, 4) == 2);
    assert(clock_read(gb, 0, 5) == 3);
    mcu(gb, 0x47, 0);
    assert(output(gb, 0xc) == 2 && output(gb, 0xd) == 1);
    mcu(gb, 0x44, 0x12);
    clock_write(gb, 0, 3, 3);
    mcu(gb, 0x46, 0);
    assert(output(gb, 0xc) == 2 && output(gb, 0xd) == 1);
    now += 60;
    mcu(gb, 0x46, 0);
    assert(output(gb, 0xc) == 3 && output(gb, 0xd) == 3);
    mcu(gb, 0x40, 0);
    now += 4;
    assert(clock_read(gb, 0, 0) == 0);
    mcu(gb, 0x41, 0);
    ++now;
    assert(clock_read(gb, 0, 0) == 1);

    const size_t size = cupid_gb_battery_size(gb);
    uint8_t *bytes = malloc(size);
    assert(bytes);
    assert(cupid_gb_save_battery_at(gb, bytes, size, NULL, now) == CUPID_GB_BATTERY_OK);
    assert(cupid_gb_reset(gb, CUPID_GB_RESET_HARD));
    ready(gb);
    assert(clock_read(gb, 0, 0) == 1);
    now += 125;
    assert(cupid_gb_load_battery_at(gb, bytes, size, now) == CUPID_GB_BATTERY_OK);
    assert(clock_read(gb, 0, 0) == 6);
    assert(clock_read(gb, 0, 2) == 5);
    size_t before_size;
    uint8_t *before = snapshot(gb, &before_size);
    bytes[SAVE_BYTES + 16] |= 0x80;
    assert(cupid_gb_load_battery_at(gb, bytes, size, now) == CUPID_GB_BATTERY_INVALID_FORMAT);
    assert_snapshot(gb, before, before_size);
    free(before);
    free(bytes);
    destroy(gb);
}

static void test_clock_continuation(CupidGbModel model) {
    CupidGb *first = machine(model), *second = machine(model);
    int64_t now = 500;
    cupid_gb_set_rtc_time_source(first, clock_now, &now);
    cupid_gb_set_rtc_time_source(second, clock_now, &now);
    assert(cupid_gb_set_rtc_mode(first, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_set_rtc_mode(second, CUPID_GB_RTC_ACCURATE));
    const double period = 32.0 / (2.0 * cupid_gb_base_clock_hz(first));
    assert(cupid_gb_set_rtc_multiplier(first, period));
    assert(cupid_gb_set_rtc_multiplier(second, period));
    ready(first);
    mcu(first, 0x41, 0);
    cupid_gb_tick(first, 3);
    assert(clock_read(first, 0, 0) == 0);
    size_t size;
    uint8_t *saved = snapshot(first, &size);
    assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_OK);
    assert_snapshot(second, saved, size);
    now += 100;
    cupid_gb_tick(first, 1);
    cupid_gb_tick(second, 1);
    assert(clock_read(first, 0, 0) == 1);
    assert(clock_read(second, 0, 0) == 1);
    free(saved);
    saved = snapshot(first, &size);
    assert_snapshot(second, saved, size);
    /* Corrupt the calendar control field and renew integrity so the rejection
     * exercises the mapper decoder itself. There is no accessory payload. */
    const size_t control = size - 8u - 68u + 8u;
    saved[control] |= 0x80;
    cupid_sha256_parts(saved, 96, saved + 128, size - 128, saved + 96);
    assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_INVALID);
    saved[control] &= 0x7f;
    cupid_sha256_parts(saved, 96, saved + 128, size - 128, saved + 96);
    assert_snapshot(second, saved, size);
    free(saved);
    destroy(first);
    destroy(second);
}

static void test_invalid_minute_digits(CupidGbModel model) {
    static const uint8_t transitions[][3] = {
        {0x09, 0x10, 0}, {0x0a, 0x0b, 0}, {0x0b, 0x16, 0}, {0x0c, 0x0d, 0}, {0x0d, 0x14, 0},
        {0x0e, 0x0f, 0}, {0x0f, 0x12, 0}, {0x59, 0x00, 1}, {0x69, 0x70, 0}, {0x79, 0x20, 1}};
    CupidGb *gb = machine(model);
    int64_t now = 700;
    cupid_gb_set_rtc_time_source(gb, clock_now, &now);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_SYNC_TO_HOST));
    ready(gb);
    mcu(gb, 0x41, 0);
    for (unsigned i = 0; i < sizeof(transitions) / sizeof(transitions[0]); ++i) {
        mcu(gb, 0x45, 0);
        clock_write(gb, 0, 2, transitions[i][0] & 15u);
        clock_write(gb, 0, 3, transitions[i][0] >> 4u);
        now += 60;
        const unsigned units = clock_read(gb, 0, 2);
        const unsigned tens = clock_read(gb, 0, 3);
        assert((units | tens << 4u) == transitions[i][1]);
        assert(clock_read(gb, 0, 4) == transitions[i][2]);
    }
    destroy(gb);
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        test_banks(models[i]);
        test_storage(models[i]);
        test_battery(models[i]);
        test_reset(models[i]);
        test_continuation(models[i]);
        test_clock_digits(models[i]);
        test_calendar(models[i]);
        test_clock_continuation(models[i]);
        test_invalid_minute_digits(models[i]);
    }
    return 0;
}

/**
 * @file    test_battery.c
 * @brief   Regression tests for SRAM and cartridge-clock battery-file persistence.
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

#include "cupid/gb/battery.h"
#include "cupid/gb/cartridge.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const int64_t test_time = INT64_C(1700000000);

typedef struct BatteryClock {
    int64_t now;
    unsigned calls;
} BatteryClock;

static int64_t battery_time(void *context) {
    BatteryClock *clock = context;
    ++clock->calls;
    return clock->now;
}

static CupidGb *cartridge(uint8_t type, uint8_t ram_size) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t rom[32768] = {0};
    assert(gb);
    memcpy(rom + 0x134, "BATTERY TEST", 12);
    rom[0x147] = type;
    rom[0x149] = ram_size;
    for (unsigned i = 0x134; i < 0x14d; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    cupid_gb_init(gb);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    for (size_t i = 0; i < gb->cartridge_ram_size; ++i)
        gb->cartridge_ram[i] = (uint8_t)(i * 13u + 7u);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static uint8_t *saved(CupidGb *gb, size_t *size) {
    *size = cupid_gb_battery_size(gb);
    uint8_t *bytes = malloc(*size);
    size_t written = 0;
    assert(bytes);
    assert(cupid_gb_save_battery_at(gb, bytes, *size, &written, test_time) == CUPID_GB_BATTERY_OK);
    assert(written == *size);
    return bytes;
}

static void rejected(CupidGb *gb, const uint8_t *data, size_t size, CupidGbBatteryResult result) {
    CupidGb *before = malloc(sizeof(*before));
    uint8_t *rom_before = malloc(gb->rom_capacity);
    uint8_t *ram_before = gb->cartridge_ram_heap ? malloc(gb->cartridge_ram_capacity) : NULL;
    assert(before && rom_before && (!gb->cartridge_ram_heap || ram_before));
    memcpy(before, gb, sizeof(*before));
    memcpy(rom_before, gb->rom, gb->rom_capacity);
    if (ram_before)
        memcpy(ram_before, gb->cartridge_ram_heap, gb->cartridge_ram_capacity);
    assert(cupid_gb_load_battery_at(gb, data, size, test_time) == result);
    assert(memcmp(gb, before, sizeof(*gb)) == 0);
    assert(!memcmp(rom_before, gb->rom, gb->rom_capacity));
    if (ram_before)
        assert(!memcmp(ram_before, gb->cartridge_ram_heap, gb->cartridge_ram_capacity));
    free(ram_before);
    free(rom_before);
    free(before);
}

static void test_raw_ram(void) {
    const uint8_t types[] = {0x03, 0x06, 0x09, 0x13, 0x1b};
    for (size_t i = 0; i < sizeof(types); ++i) {
        CupidGb *gb = cartridge(types[i], 2);
        size_t size;
        uint8_t *bytes = saved(gb, &size);
        assert(size == gb->cartridge_ram_size);
        assert(memcmp(bytes, gb->cartridge_ram, size) == 0);
        rejected(gb, bytes, size - 1, CUPID_GB_BATTERY_INVALID_SIZE);
        rejected(gb, bytes, size + 1, CUPID_GB_BATTERY_INVALID_SIZE);
        memset(gb->cartridge_ram, 0, size);
        assert(cupid_gb_load_battery_at(gb, bytes, size, test_time) == CUPID_GB_BATTERY_OK);
        assert(memcmp(bytes, gb->cartridge_ram, size) == 0);
        assert(gb->battery_write_generation == 0);
        free(bytes);
        destroy(gb);
    }
}

static void test_rom_capacity_output_overlap(void) {
    CupidGb *gb = cartridge(0x03u, 2u);
    uint8_t tiny[0x154] = {0};
    memcpy(tiny + 0x134, "BATTERY TAIL", 12);
    tiny[0x147] = 0x03;
    tiny[0x149] = 2;
    for (unsigned index = 0x134; index < 0x14d; ++index)
        tiny[0x14d] = (uint8_t)(tiny[0x14d] - tiny[index] - 1u);
    assert(cupid_gb_load_rom(gb, tiny, sizeof(tiny)));
    assert(gb->rom_capacity == CUPID_GB_MIN_ROM_CAPACITY && gb->rom_size == sizeof(tiny));
    const size_t battery_size = cupid_gb_battery_size(gb);
    assert(battery_size && battery_size <= gb->rom_capacity - gb->rom_size);
    uint8_t *const output = gb->rom + gb->rom_size;
    size_t written = 123;
    assert(cupid_gb_save_battery_at(gb, output, battery_size, &written, test_time) ==
           CUPID_GB_BATTERY_INVALID_ARGUMENT);
    assert(written == 0 && !memcmp(gb->rom, tiny, sizeof(tiny)));
    destroy(gb);
}

static void test_owned_source_bounds(void) {
    CupidGb *gb = cartridge(0x03u, 2u);
    const size_t size = gb->cartridge_ram_size;
    uint8_t *expected = malloc(size);
    assert(expected && size && size < sizeof(gb->cartridge_ram));

    /* A valid input may overlap the destination or occupy an unused owned tail.
     * Importing it must retain the cartridge's allocation and dirty generation. */
    uint8_t *const sources[] = {
        gb->cartridge_ram,
        gb->cartridge_ram + 1,
        gb->cartridge_ram + sizeof(gb->cartridge_ram) - size,
        gb->rom + gb->rom_capacity - size,
        gb->work_ram + sizeof(gb->work_ram) - size,
    };
    for (unsigned index = 0; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        for (size_t offset = 0; offset < size; ++offset)
            expected[offset] = (uint8_t)(offset * 37u + index * 23u);
        memcpy(sources[index], expected, size);
        uint8_t *const rom = gb->rom;
        assert(cupid_gb_load_battery_at(gb, sources[index], size, test_time) ==
               CUPID_GB_BATTERY_OK);
        assert(!memcmp(gb->cartridge_ram, expected, size));
        assert(gb->rom == rom && !gb->cartridge_ram_heap && !gb->battery_write_generation);
    }

    /* A supported raw size still fails when an input starts inside owned
     * storage and extends beyond it. Exact end addresses can instead belong
     * to a distinct caller buffer, so they cannot establish an alias. */
    const uint8_t *const truncated[] = {
        gb->cartridge_ram + sizeof(gb->cartridge_ram) - size + 1,
        gb->cartridge_ram + sizeof(gb->cartridge_ram) - 1,
        gb->rom + gb->rom_capacity - size + 1,
        gb->rom + gb->rom_capacity - 1,
        (const uint8_t *)gb + sizeof(*gb) - size + 1,
        (const uint8_t *)gb + sizeof(*gb) - 1,
    };
    for (unsigned index = 0; index < sizeof(truncated) / sizeof(truncated[0]); ++index)
        rejected(gb, truncated[index], size, CUPID_GB_BATTERY_INVALID_ARGUMENT);
    rejected(gb, gb->cartridge_ram + 1, SIZE_MAX, CUPID_GB_BATTERY_INVALID_ARGUMENT);
    rejected(gb, NULL, size, CUPID_GB_BATTERY_INVALID_ARGUMENT);
    free(expected);
    destroy(gb);
}

static void test_written_pointer_overlap(void) {
    CupidGb *gb = cartridge(0x10u, 2u);
    assert(cupid_gb_load_rom(gb, gb->rom, 0x150u));
    assert(gb->rom_size < gb->rom_capacity);
    memset(gb->rom + gb->rom_size, 0x6d, gb->rom_capacity - gb->rom_size);
    gb->cartridge_ram[0] = 0x5a;
    BatteryClock host = {test_time, 0};
    cupid_gb_set_rtc_time_source(gb, battery_time, &host);
    const size_t size = cupid_gb_battery_size(gb);
    const size_t capacity = size + 2u * sizeof(size_t);
    uint8_t *output = malloc(capacity);
    uint8_t *output_before = malloc(capacity);
    uint8_t *rom_before = malloc(gb->rom_capacity);
    CupidGb *before = malloc(sizeof(*before));
    assert(size && output && output_before && rom_before && before);
    memset(output, 0xa5, capacity);
    memcpy(output_before, output, capacity);
    memcpy(rom_before, gb->rom, gb->rom_capacity);
    memcpy(before, gb, sizeof(*before));

    /* Include the unused ROM/output tails and both partial-overlap directions.
     * Result pointers are aligned and backed by real writable storage. */
    const struct {
        uint8_t *data;
        size_t capacity;
        size_t *written;
    } cases[] = {
        {output, capacity, &gb->rom_size},
        {output, capacity, &gb->rom_capacity},
        {output, capacity, (size_t *)gb->rom},
        {output, capacity, (size_t *)(gb->rom + gb->rom_capacity - sizeof(size_t))},
        {output, capacity, (size_t *)output},
        {output, capacity, (size_t *)(output + size)},
        {output, capacity, (size_t *)(output + capacity - sizeof(size_t))},
        {output + 1, capacity - 1u, (size_t *)output},
        {output, size + 1u, (size_t *)(output + size)},
        {output, sizeof(size_t) - 1u, (size_t *)output},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        for (unsigned use_wrapper = 0; use_wrapper < 2; ++use_wrapper) {
            const unsigned calls = host.calls;
            const CupidGbBatteryResult result =
                use_wrapper
                    ? cupid_gb_save_battery(gb, cases[i].data, cases[i].capacity, cases[i].written)
                    : cupid_gb_save_battery_at(gb, cases[i].data, cases[i].capacity,
                                               cases[i].written, test_time);
            assert(result == CUPID_GB_BATTERY_INVALID_ARGUMENT);
            assert(!memcmp(before, gb, sizeof(*gb)));
            assert(!memcmp(rom_before, gb->rom, gb->rom_capacity));
            assert(!memcmp(output_before, output, capacity));
            assert(host.calls == calls);
        }
    }

    /* Ordinary errors still clear a separate result count without writing the
     * payload. A count immediately after the declared buffer is valid. */
    size_t written = 123;
    assert(cupid_gb_save_battery_at(NULL, output, capacity, &written, test_time) ==
           CUPID_GB_BATTERY_INVALID_ARGUMENT);
    assert(written == 0 && !memcmp(output_before, output, capacity));
    written = 123;
    assert(cupid_gb_save_battery_at(gb, output, size - 1u, &written, test_time) ==
           CUPID_GB_BATTERY_BUFFER_TOO_SMALL);
    assert(written == 0 && !memcmp(output_before, output, capacity));
    size_t *const adjacent = (size_t *)(output + size);
    assert(cupid_gb_save_battery_at(gb, output, size, adjacent, test_time) == CUPID_GB_BATTERY_OK);
    assert(*adjacent == size && !memcmp(output, gb->cartridge_ram, gb->cartridge_ram_size));
    assert(!memcmp(before, gb, sizeof(*gb)));
    assert(!memcmp(rom_before, gb->rom, gb->rom_capacity));
    for (size_t i = size + sizeof(size_t); i < capacity; ++i)
        assert(output[i] == 0xa5);

    free(before);
    free(rom_before);
    free(output_before);
    free(output);
    destroy(gb);
}

static void test_rtc_format(void) {
    CupidGb *gb = cartridge(0x10, 2);
    gb->rtc = (CupidGbRtc){.s = 57,
                           .m = 34,
                           .h = 12,
                           .dl = 52,
                           .dh = 64,
                           .ls = 56,
                           .lm = 34,
                           .lh = 12,
                           .ldl = 52,
                           .ldh = 64,
                           .base_time = test_time - 900};
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, gb, sizeof(*before));
    size_t size;
    uint8_t *bytes = saved(gb, &size);
    assert(memcmp(before, gb, sizeof(*gb)) == 0);
    assert(size == 8192 + 48);
    const uint8_t values[] = {57, 34, 12, 52, 64, 56, 34, 12, 52, 64};
    for (unsigned i = 0; i < 10; ++i) {
        assert(bytes[8192 + i * 4] == values[i]);
        assert(!bytes[8192 + i * 4 + 1] && !bytes[8192 + i * 4 + 2] && !bytes[8192 + i * 4 + 3]);
    }
    for (unsigned i = 0; i < 8; ++i)
        assert(bytes[8192 + 40 + i] == (uint8_t)((uint64_t)test_time >> (i * 8)));
    memset(&gb->rtc, 0, sizeof(gb->rtc));
    gb->rtc.latch_prev = 0x5a;
    memset(gb->cartridge_ram, 0, 8192);
    assert(cupid_gb_load_battery_at(gb, bytes, size, test_time) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.s == 57 && gb->rtc.ls == 56 && gb->rtc.m == 34 && gb->rtc.lm == 34);
    assert(gb->rtc.h == 12 && gb->rtc.dl == 52 && gb->rtc.dh == 64 && gb->rtc.ldh == 64);
    assert(gb->rtc.base_time == test_time && gb->rtc.latch_prev == 0x5a);
    assert(memcmp(gb->cartridge_ram, before->cartridge_ram, 8192) == 0);
    assert(cupid_gb_load_battery_at(gb, bytes, size - 4, test_time) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.s == 57 && gb->rtc.ls == 56);
    for (size_t cut = 0; cut < size; ++cut)
        if (cut != 8192 && cut != size - 4)
            rejected(gb, bytes, cut, CUPID_GB_BATTERY_INVALID_SIZE);
    uint8_t *bad = malloc(size + 1);
    assert(bad);
    memcpy(bad, bytes, size);
    for (unsigned field = 0; field < 10; ++field) {
        bad[8192 + field * 4 + 1] = 1;
        rejected(gb, bad, size, CUPID_GB_BATTERY_INVALID_RTC);
        bad[8192 + field * 4 + 1] = 0;
    }
    rejected(gb, bad, size + 1, CUPID_GB_BATTERY_INVALID_SIZE);
    memset(bad, 0xa5, size + 1);
    size_t written = 99;
    assert(cupid_gb_save_battery_at(gb, bad, size - 1, &written, test_time) ==
           CUPID_GB_BATTERY_BUFFER_TOO_SMALL);
    assert(written == 0);
    for (size_t i = 0; i < size + 1; ++i)
        assert(bad[i] == 0xa5);
    assert(cupid_gb_save_battery_at(gb, gb->cartridge_ram, size, &written, test_time) ==
           CUPID_GB_BATTERY_INVALID_ARGUMENT);
    free(before);
    free(bad);
    free(bytes);
    destroy(gb);
}

static void test_clock_advance(void) {
    CupidGbRtc rtc = {.s = 59,
                      .m = 59,
                      .h = 23,
                      .dl = 255,
                      .dh = 1,
                      .ls = 42,
                      .lm = 43,
                      .lh = 44,
                      .ldl = 45,
                      .ldh = 46,
                      .base_time = test_time};
    cupid_gb_rtc_advance(&rtc, test_time + 1);
    assert(rtc.s == 0 && rtc.m == 0 && rtc.h == 0 && rtc.dl == 0 && rtc.dh == 0x80);
    assert(rtc.ls == 42 && rtc.lm == 43 && rtc.lh == 44 && rtc.ldl == 45 && rtc.ldh == 46);
    rtc.dh |= 0x40;
    rtc.s = 7;
    cupid_gb_rtc_advance(&rtc, test_time + 100000);
    assert(rtc.s == 7 && rtc.dh == 0xc0 && rtc.base_time == test_time + 1);
    rtc.dh &= 0xbfu;
    cupid_gb_rtc_advance(&rtc, test_time);
    assert(rtc.s == 7 && rtc.dh == 0x80 && rtc.base_time == test_time + 1);
    rtc = (CupidGbRtc){.s = 63, .m = 63, .h = 31, .base_time = test_time};
    cupid_gb_rtc_advance(&rtc, test_time + 192);
    assert(rtc.s == 255 && rtc.m == 63 && rtc.h == 31);
    cupid_gb_rtc_advance(&rtc, test_time + 252);
    assert(rtc.s == 59 && rtc.m == 63 && rtc.h == 31);
    cupid_gb_rtc_advance(&rtc, test_time + 253);
    assert(rtc.s == 0 && rtc.m == 64 && rtc.h == 31);
    rtc = (CupidGbRtc){.base_time = INT64_MIN};
    cupid_gb_rtc_advance(&rtc, INT64_MAX);
    assert(rtc.base_time == INT64_MIN && !rtc.s && !rtc.m && !rtc.h && !rtc.dl && !rtc.dh);
}

static void test_running_and_legacy_clock(void) {
    CupidGb *gb = cartridge(0x10, 2);
    gb->rtc = (CupidGbRtc){.s = 59, .ls = 17, .base_time = test_time - 1};
    size_t size;
    uint8_t *bytes = saved(gb, &size);
    assert(gb->rtc.s == 59 && gb->rtc.ls == 17);
    assert(bytes[8192] == 59 && bytes[8196] == 0 && bytes[8212] == 17);
    assert(cupid_gb_load_battery_at(gb, bytes, size, test_time + 125) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.s == 4 && gb->rtc.m == 3 && gb->rtc.ls == 17);
    for (unsigned i = 0; i < 8; ++i)
        bytes[8232 + i] = 0xff;
    assert(cupid_gb_load_battery_at(gb, bytes, size, test_time) == CUPID_GB_BATTERY_OK);
    assert((gb->rtc.dh & 0x80) && gb->rtc.base_time == test_time && gb->rtc.ls == 17);
    gb->rtc.s = 29;
    gb->rtc.ls = 31;
    gb->rtc.dh = 0x40;
    assert(cupid_gb_load_battery_at(gb, bytes, 8192, test_time + 1) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.s == 29 && gb->rtc.ls == 31 && gb->rtc.dh == 0xc0);
    free(bytes);
    destroy(gb);
    gb = cartridge(0x0f, 0);
    assert(cupid_gb_battery_size(gb) == 48);
    assert(cupid_gb_load_battery_at(gb, NULL, 0, test_time) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.dh & 0x80);
    destroy(gb);
    gb = cartridge(0, 0);
    assert(!cupid_gb_battery_size(gb));
    assert(cupid_gb_save_battery_at(gb, NULL, 0, NULL, test_time) == CUPID_GB_BATTERY_NO_DATA);
    destroy(gb);
}

static void test_write_generation(void) {
    CupidGb *gb = cartridge(0x10, 2);
    cupid_gb_write_u8(gb, 0xa000, 0x55);
    assert(gb->battery_write_generation == 0);
    cupid_gb_write_u8(gb, 0x0000, 0x0a);
    cupid_gb_write_u8(gb, 0xa000, 0x55);
    assert(gb->battery_write_generation == 1);
    cupid_gb_write_u8(gb, 0xa000, 0x55);
    assert(gb->battery_write_generation == 1);
    gb->rtc = (CupidGbRtc){.s = 3, .ls = 42, .dh = 0x40, .base_time = (int64_t)time(NULL)};
    cupid_gb_write_u8(gb, 0x4000, 8);
    cupid_gb_write_u8(gb, 0xa000, 63);
    assert(gb->rtc.s == 63 && gb->rtc.ls == 42 && gb->battery_write_generation == 2);
    cupid_gb_write_u8(gb, 0xa000, 63);
    assert(gb->battery_write_generation == 2);
    gb->rtc.latch_prev = 1;
    cupid_gb_write_u8(gb, 0x6000, 0);
    cupid_gb_write_u8(gb, 0x6000, 1);
    assert(gb->rtc.ls == 63 && gb->battery_write_generation == 2);
    gb->rtc.ls = gb->rtc.lm = gb->rtc.lh = gb->rtc.ldh = 0xff;
    assert(cupid_gb_read_u8(gb, 0xa000) == 63);
    cupid_gb_write_u8(gb, 0x4000, 9);
    assert(cupid_gb_read_u8(gb, 0xa000) == 63);
    cupid_gb_write_u8(gb, 0x4000, 10);
    assert(cupid_gb_read_u8(gb, 0xa000) == 31);
    cupid_gb_write_u8(gb, 0x4000, 12);
    assert(cupid_gb_read_u8(gb, 0xa000) == 0xc1);
    assert(gb->battery_write_generation == 2);
    cupid_gb_rtc_advance(&gb->rtc, gb->rtc.base_time + 100);
    assert(gb->battery_write_generation == 2);
    destroy(gb);
    gb = cartridge(0x13, 3);
    cupid_gb_write_u8(gb, 0, 0x0a);
    cupid_gb_write_u8(gb, 0x4000, 0x0a);
    cupid_gb_write_u8(gb, 0xa000, 0x77);
    assert(gb->cartridge_ram[0x4000] == 0x77 && cupid_gb_read_u8(gb, 0xa000) == 0x77);
    assert(gb->battery_write_generation == 1);
    destroy(gb);
    gb = cartridge(0x06, 0);
    cupid_gb_write_u8(gb, 0, 0x0a);
    cupid_gb_write_u8(gb, 0xa000, 0x3a);
    assert(gb->battery_write_generation == 1 && gb->cartridge_ram[0] == 10);
    cupid_gb_write_u8(gb, 0xa000, 0xfa);
    assert(gb->battery_write_generation == 1);
    destroy(gb);
}

static void test_clock_policy_roundtrip(void) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        CupidGb *gb = cartridge(0x10, 2);
        BatteryClock host = {test_time, 0};
        cupid_gb_set_rtc_time_source(gb, battery_time, &host);
        assert(cupid_gb_set_rtc_mode(gb, (CupidGbRtcMode)mode));
        assert(cupid_gb_set_rtc_multiplier(gb, 2));
        gb->rtc = (CupidGbRtc){.s = 59, .m = 1, .h = 2, .ls = 7, .base_time = test_time - 900};
        gb->rtc_cycles = UINT32_MAX - 123;
        const CupidGbRtc live = gb->rtc;
        const uint64_t generation = gb->battery_write_generation;
        const unsigned calls = host.calls;
        const size_t size = cupid_gb_battery_size(gb);
        uint8_t *bytes = malloc(size);
        assert(bytes);
        assert(cupid_gb_save_battery(gb, bytes, size, NULL) == CUPID_GB_BATTERY_OK);
        assert(host.calls == calls + 1 && memcmp(&gb->rtc, &live, sizeof(live)) == 0);
        assert(bytes[8192] == 59 && bytes[8196] == 1 && bytes[8200] == 2);
        host.now += 61;
        assert(cupid_gb_load_battery(gb, bytes, size) == CUPID_GB_BATTERY_OK);
        assert(host.calls == calls + 2 && gb->rtc.base_time == host.now);
        assert(gb->rtc.s == 0 && gb->rtc.m == 3 && gb->rtc.h == 2 && gb->rtc.ls == 7);
        assert(gb->rtc_cycles == UINT32_MAX - 123 && gb->battery_write_generation == generation);
        assert(gb->rtc_policy.mode == (CupidGbRtcMode)mode &&
               gb->rtc_policy.second_length == 16777216u &&
               gb->rtc_policy.time_source == battery_time &&
               gb->rtc_policy.time_user_data == &host);
        free(bytes);
        destroy(gb);
    }
}

static void test_legacy_files(void) {
    CupidGb *gb = cartridge(0x10, 2);
    struct timespec now;
    assert(timespec_get(&now, TIME_UTC));
    FILE *file = NULL;
    for (unsigned i = 0; i < 128 && !file; ++i) {
        snprintf(gb->save_path, sizeof(gb->save_path), "cupidgb-battery-%llu-%lu-%u.sav",
                 (unsigned long long)now.tv_sec, (unsigned long)now.tv_nsec, i);
        file = fopen(gb->save_path, "wbx");
        assert(file || errno == EEXIST);
    }
    assert(file && fclose(file) == 0);
    gb->rtc = (CupidGbRtc){.s = 9, .ls = 7, .dh = 0x40, .base_time = test_time};
    cupid_gb_save(gb);
    file = fopen(gb->save_path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0 && ftell(file) == 8192 + 48);
    assert(fclose(file) == 0);
    gb->cartridge_ram[0] = 0x5a;
    gb->rtc.s = 11;
    cupid_gb_save(gb);
    memset(gb->cartridge_ram, 0, gb->cartridge_ram_size);
    memset(&gb->rtc, 0, sizeof(gb->rtc));
    cupid_gb_load_save(gb);
    assert(gb->cartridge_ram[0] == 0x5a && gb->rtc.s == 11 && gb->rtc.ls == 7);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, gb, sizeof(*before));
    file = fopen(gb->save_path, "wb");
    assert(file && fwrite("bad", 1, 3, file) == 3 && fclose(file) == 0);
    cupid_gb_load_save(gb);
    assert(memcmp(before, gb, sizeof(*gb)) == 0);
    assert(remove(gb->save_path) == 0);
    free(before);
    destroy(gb);
}

static void test_observed_fixture(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    uint8_t bytes[8192 + 48];
    assert(fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fgetc(file) == EOF && fclose(file) == 0);
    uint64_t encoded_time = 0;
    for (unsigned i = 0; i < 8; ++i)
        encoded_time |= (uint64_t)bytes[8232 + i] << (i * 8);
    assert(encoded_time <= INT64_MAX);
    int64_t timestamp = (int64_t)encoded_time;
    CupidGb *gb = cartridge(0x10, 2);
    assert(cupid_gb_load_battery_at(gb, bytes, sizeof(bytes), timestamp) == CUPID_GB_BATTERY_OK);
    assert(gb->cartridge_ram[0] == 0x5a);
    assert(gb->rtc.s == 57 && gb->rtc.ls == 56 && gb->rtc.m == 34 && gb->rtc.h == 12 &&
           gb->rtc.dl == 52 && gb->rtc.dh == 64 && gb->rtc.ldh == 64);
    uint8_t output[sizeof(bytes)];
    size_t written = 0;
    assert(cupid_gb_save_battery_at(gb, output, sizeof(output), &written, timestamp) ==
           CUPID_GB_BATTERY_OK);
    assert(written == sizeof(bytes) && memcmp(bytes, output, sizeof(bytes)) == 0);
    destroy(gb);
}

static void test_tpp1_sram_battery(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned code = 0; code <= 9; ++code) {
            CupidGb *gb = calloc(1, sizeof(*gb));
            uint8_t rom[0x8000] = {0};
            assert(gb);
            memcpy(rom + 0x134, "CUPID SRAM", 10);
            rom[0x143] = models[model] == CUPID_GB_MODEL_CGB ? 0x80 : 0;
            rom[0x147] = 0xbc;
            rom[0x149] = 0xc1;
            rom[0x14a] = 0x65;
            rom[0x152] = (uint8_t)code;
            rom[0x153] = 8;
            for (unsigned address = 0x134; address < 0x14d; ++address)
                rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);
            cupid_gb_set_model(gb, models[model]);
            cupid_gb_set_power_on_seed(gb, 0);
            assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
            uint8_t *const ram = cupid_gb_cartridge_ram(gb);
            const size_t ram_size = code ? (size_t)0x2000 << (code - 1u) : 0;
            assert(gb->cartridge_ram_size == ram_size);
            uint8_t *expected = malloc(ram_size ? ram_size : 1);
            assert(expected);
            for (size_t offset = 0; offset < ram_size; ++offset)
                expected[offset] = ram[offset] =
                    (uint8_t)(offset * 29u + (offset >> 13u) * 73u + 0x17u);
            gb->tpp1.status = 4;
            gb->tpp1.live[0] = 1;
            gb->tpp1.live[1] = 2;
            gb->tpp1.live[2] = 3;
            gb->tpp1.live[3] = 4;
            gb->battery_write_generation = 43;
            size_t size;
            uint8_t *bytes = saved(gb, &size);
            assert(size == ram_size + 20 && !memcmp(bytes, expected, ram_size));
            static const uint8_t trailer_header[] = {'T', 'P', 'P', '1', 1, 0};
            assert(!memcmp(bytes + ram_size, trailer_header, sizeof(trailer_header)));
            memset(ram, 0, ram_size);
            assert(cupid_gb_load_battery_at(gb, bytes, size, test_time) == CUPID_GB_BATTERY_OK);
            assert(!memcmp(ram, expected, ram_size) && gb->battery_write_generation == 43);
            assert(gb->tpp1.status == 4 && gb->tpp1.live[3] == 4);
            rejected(gb, bytes, size - 1, CUPID_GB_BATTERY_INVALID_SIZE);
            rejected(gb, bytes, size + 1, CUPID_GB_BATTERY_INVALID_SIZE);
            bytes[ram_size] = 0;
            rejected(gb, bytes, size, CUPID_GB_BATTERY_INVALID_RTC);
            bytes[ram_size] = 'T';
            bytes[ram_size + 6] = 0x10;
            rejected(gb, bytes, size, CUPID_GB_BATTERY_INVALID_RTC);
            bytes[ram_size + 6] = 4;

            if (size < sizeof(gb->cartridge_ram)) {
                /* Decode the clock trailer before an overlapping SRAM import
                 * overwrites any of its input. The whole span fits inline. */
                memcpy(ram + 1, bytes, size);
                assert(cupid_gb_load_battery_at(gb, ram + 1, size, test_time) ==
                       CUPID_GB_BATTERY_OK);
                assert(!memcmp(ram, expected, ram_size));
                assert(gb->tpp1.status == 4 && gb->tpp1.live[3] == 4);
            } else {
                /* Full-capacity SRAM cannot also hold a battery trailer. Reject
                 * these aliases before either payload copies or trailer reads. */
                rejected(gb, ram + 1, ram_size, CUPID_GB_BATTERY_INVALID_ARGUMENT);
                rejected(gb, ram, size, CUPID_GB_BATTERY_INVALID_ARGUMENT);
                rejected(gb, ram + ram_size - 1, ram_size, CUPID_GB_BATTERY_INVALID_ARGUMENT);
            }

            /* A raw image may be the live span itself; imports validate clock
             * bytes first and use overlap-safe copies without changing ownership. */
            assert(cupid_gb_load_battery_at(gb, ram, ram_size, test_time) == CUPID_GB_BATTERY_OK);
            assert(!memcmp(ram, expected, ram_size) && (gb->rtc.dh & 0x80));
            assert(gb->battery_write_generation == 43);
            if (gb->cartridge_ram_heap) {
                const size_t offsets[] = {0, CUPID_GB_INLINE_RAM_SIZE - sizeof(size_t),
                                          CUPID_GB_INLINE_RAM_SIZE, ram_size / 2,
                                          ram_size - sizeof(size_t)};
                CupidGb *before = malloc(sizeof(*before));
                uint8_t *output = malloc(size);
                uint8_t *output_before = malloc(size);
                assert(before && output && output_before);
                memcpy(before, gb, sizeof(*gb));
                memset(output, 0x6f, size);
                memcpy(output_before, output, size);
                for (unsigned index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
                    for (unsigned wrapper = 0; wrapper < 2; ++wrapper) {
                        size_t written = 123;
                        const CupidGbBatteryResult payload_result =
                            wrapper
                                ? cupid_gb_save_battery(gb, ram + offsets[index], size, &written)
                                : cupid_gb_save_battery_at(gb, ram + offsets[index], size, &written,
                                                           test_time);
                        assert(payload_result == CUPID_GB_BATTERY_INVALID_ARGUMENT && !written);
                        size_t *const aliased_count = (size_t *)(void *)(ram + offsets[index]);
                        const CupidGbBatteryResult count_result =
                            wrapper ? cupid_gb_save_battery(gb, output, size, aliased_count)
                                    : cupid_gb_save_battery_at(gb, output, size, aliased_count,
                                                               test_time);
                        assert(count_result == CUPID_GB_BATTERY_INVALID_ARGUMENT);
                        assert(!memcmp(gb, before, sizeof(*gb)));
                        assert(!memcmp(ram, expected, ram_size));
                        assert(!memcmp(output, output_before, size));
                    }
                }
                free(output_before);
                free(output);
                free(before);
            }
            free(expected);
            free(bytes);
            destroy(gb);
        }
    }
}

int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    test_raw_ram();
    test_tpp1_sram_battery();
    test_rom_capacity_output_overlap();
    test_owned_source_bounds();
    test_written_pointer_overlap();
    test_write_generation();
    test_clock_policy_roundtrip();
    test_legacy_files();
    test_rtc_format();
    test_clock_advance();
    test_running_and_legacy_clock();
    if (argc == 2)
        test_observed_fixture(argv[1]);
    puts("Portable battery and RTC checks passed.");
    return 0;
}

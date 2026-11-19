/**
 * @file    test_mbc3_bus.c
 * @brief   MBC3 cartridge-window response, held-bus decay, and native-state regressions.
 * @author  cupidthecat
 * @date    2026-11-17
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

#include "cupid/gb/cartridge.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(values) (sizeof(values) / sizeof((values)[0]))

enum { FLOATING = -1, SRAM_BYTE = 0x52, MARKER_BASE = 0x200, HOLD_HALF_DOTS = 12 };

typedef struct Profile {
    const char *name;
    CupidGbModel model;
    bool color;
    bool fast;
} Profile;

typedef struct Fixture {
    const char *name;
    uint8_t type;
    size_t rom_bytes;
    uint8_t ram_code;
    size_t ram_bytes;
    bool extended;
    bool timer;
    const int16_t *responses;
} Fixture;

typedef struct Gate {
    uint8_t value;
    bool enabled;
} Gate;

typedef struct BusCase {
    CupidGb *gb;
    const Profile *profile;
    const Fixture *fixture;
    unsigned phase;
    uint8_t gate;
    uint8_t selector;
    bool enabled;
    uint8_t held;
    uint64_t expiry;
} BusCase;

static const Profile profiles[] = {
    {"dmg", CUPID_GB_MODEL_DMG_ABC, false, false},
    {"dmg0", CUPID_GB_MODEL_DMG0, false, false},
    {"mgb", CUPID_GB_MODEL_MGB, false, false},
    {"sgb", CUPID_GB_MODEL_SGB, false, false},
    {"sgb2", CUPID_GB_MODEL_SGB2, false, false},
    {"cgb_compat", CUPID_GB_MODEL_CGB, false, false},
    {"cgb_native", CUPID_GB_MODEL_CGB, true, false},
    {"cgb_double", CUPID_GB_MODEL_CGB, true, true},
};

/* These response tables are fixture expectations, independent of the mapper's
 * decoded registers. SRAM contains one byte throughout, so physical bank aliases
 * cannot hide an incorrect choice between a driven and undriven window. */
static const int16_t timer_absent[16] = {
    FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING,
    0x25,     0x2a,     0x15,     0x93,     0x41,     FLOATING, FLOATING, FLOATING,
};
static const int16_t timer_ordinary[16] = {
    SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, 0xff, 0xff,     0xff,     0xff,
    0x25,      0x2a,      0x15,      0x93,      0x41, FLOATING, FLOATING, FLOATING,
};
static const int16_t timer_extended[16] = {
    SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE,
    0x25,      0x2a,      0x15,      0x93,      0x41,      FLOATING,  FLOATING,  FLOATING,
};
static const int16_t plain_absent[16] = {
    FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING,
    FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING, FLOATING,
};
static const int16_t plain_present[16] = {
    SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE,
    SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE, SRAM_BYTE,
};

static const Fixture fixtures[] = {
    {"timer_only_zero", 0x0f, 0x8000, 0, 0, false, true, timer_absent},
    {"timer_ram_zero", 0x10, 0x8000, 0, 0, false, true, timer_absent},
    {"timer_only_rom_edge", 0x0f, 0x200000, 0, 0, false, true, timer_absent},
    {"timer_only_rom_extended", 0x0f, 0x200001, 0, 0, true, true, timer_absent},
    {"timer_ram_rom_extended", 0x10, 0x200001, 0, 0, true, true, timer_absent},
    {"timer_ram_invalid_6", 0x10, 0x8000, 6, 0, false, true, timer_absent},
    {"timer_ram_invalid_ff", 0x10, 0x8000, 0xff, 0, false, true, timer_absent},
    {"timer_only_invalid_6", 0x0f, 0x8000, 6, 0, false, true, timer_absent},
    {"timer_ram_2k", 0x10, 0x8000, 1, 0x800, false, true, timer_ordinary},
    {"timer_ram_8k", 0x10, 0x8000, 2, 0x2000, false, true, timer_ordinary},
    {"timer_ram_32k", 0x10, 0x8000, 3, 0x8000, false, true, timer_ordinary},
    {"timer_only_promoted_32k", 0x0f, 0x8000, 3, 0x8000, false, true, timer_ordinary},
    {"timer_ram_64k", 0x10, 0x8000, 5, 0x10000, true, true, timer_extended},
    {"timer_ram_128k", 0x10, 0x8000, 4, 0x20000, true, true, timer_extended},
    {"timer_ram_rom_extended_8k", 0x10, 0x200001, 2, 0x2000, true, true, timer_extended},
    {"plain_no_ram", 0x11, 0x8000, 0, 0, false, false, plain_absent},
    {"plain_32k", 0x13, 0x8000, 3, 0x8000, false, false, plain_present},
    {"plain_rom_extended_32k", 0x13, 0x200001, 3, 0x8000, true, false, plain_present},
};

static const Gate gates[] = {{0x00, false}, {0x0a, true}, {0xfa, true}};
static const uint8_t markers[] = {0x00, 0x04, 0x6d, 0xa5, 0xff};
static const uint8_t clock_values[] = {0x25, 0x2a, 0x15, 0x93, 0x41};
static const uint16_t addresses[] = {0xa000, 0xa7ff, 0xa800, 0xbfff};
static const unsigned delays[] = {0, 1, 5, 6, 7};

static BusCase make_case(const Profile *profile, const Fixture *fixture);
static void release_case(BusCase *test);

static void test_minimal_absent_sram(void) {
    BusCase test = make_case(&profiles[0], &fixtures[0]);
    CupidGb *gb = test.gb;
    cupid_gb_write_u8(gb, 0x0000, 0x0a);
    cupid_gb_write_u8(gb, 0x4000, 0x04);
    assert(cupid_gb_read_bus_u8(gb, 0xa000) == 0x04);
    assert(gb->cartridge_bus_expiry == gb->elapsed_cycles * 2u + HOLD_HALF_DOTS);
    release_case(&test);
}

static int64_t fixed_time(void *unused) {
    (void)unused;
    return 0;
}

static BusCase make_case(const Profile *profile, const Fixture *fixture) {
    uint8_t *rom = calloc(fixture->rom_bytes, 1);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(rom && gb);
    rom[0x143] = profile->color ? 0x80 : 0;
    rom[0x147] = fixture->type;
    rom[0x148] = fixture->rom_bytes > 0x200000 ? 7 : fixture->rom_bytes > 0x8000 ? 6 : 0;
    rom[0x149] = fixture->ram_code;
    memcpy(rom + MARKER_BASE, markers, sizeof(markers));
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);

    cupid_gb_set_power_on_seed(gb, 0);
    cupid_gb_set_rtc_time_source(gb, fixed_time, NULL);
    cupid_gb_init(gb);
    cupid_gb_set_model(gb, profile->model);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_load_rom(gb, rom, fixture->rom_bytes));
    free(rom);
    assert(gb->header.mbc_type == CUPID_GB_MBC3);
    assert(gb->header.has_timer == fixture->timer);
    assert(gb->cartridge_ram_size == fixture->ram_bytes);
    assert(cupid_gb_has_extended_mbc3(gb) == fixture->extended);
    assert(gb->cgb_mode == profile->color);
    cupid_gb_write_u8(gb, 0xff40, 0);
    gb->double_speed = profile->fast;
    gb->speed_phase = false;
    gb->cpu.ime = false;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    memset(cupid_gb_cartridge_ram(gb), SRAM_BYTE, fixture->ram_bytes);
    if (fixture->timer) {
        cupid_gb_write_u8(gb, 0x0000, 0x0a);
        for (unsigned index = 0; index < COUNT(clock_values); ++index) {
            cupid_gb_write_u8(gb, 0x4000, (uint8_t)(8u + index));
            cupid_gb_write_u8(gb, 0xa000, clock_values[index]);
        }
        cupid_gb_write_u8(gb, 0x6000, 0);
        cupid_gb_write_u8(gb, 0x6000, 1);
    }
    /* A known final drive starts the independent history oracle. */
    cupid_gb_write_u8(gb, 0x0000, 0);
    return (BusCase){.gb = gb,
                     .profile = profile,
                     .fixture = fixture,
                     .held = 0,
                     .expiry = gb->elapsed_cycles * 2u + HOLD_HALF_DOTS};
}

static void release_case(BusCase *test) {
    cupid_gb_cleanup(test->gb);
    free(test->gb);
}

static uint8_t expected_held(const BusCase *test) {
    return test->gb->elapsed_cycles * 2u < test->expiry ? test->held : 0xff;
}

static void check_history(const BusCase *test) {
    if (test->gb->cartridge_bus_expiry != test->expiry ||
        cupid_gb_sample_cartridge_bus(test->gb) != expected_held(test)) {
        fprintf(stderr,
                "%s/%s gate=%02x select=%02x clock=%" PRIu64 " expiry=%" PRIu64
                " expected_expiry=%" PRIu64 "\n",
                test->profile->name, test->fixture->name, test->gate, test->selector,
                test->gb->elapsed_cycles, test->gb->cartridge_bus_expiry, test->expiry);
    }
    assert(test->gb->cartridge_bus_expiry == test->expiry);
    assert(cupid_gb_sample_cartridge_bus(test->gb) == expected_held(test));
}

static void expect_drive(BusCase *test, uint8_t value) {
    test->held = value;
    test->expiry = test->gb->elapsed_cycles * 2u + HOLD_HALF_DOTS;
    check_history(test);
}

static void write_bus(BusCase *test, uint16_t address, uint8_t value) {
    cupid_gb_write_u8(test->gb, address, value);
    expect_drive(test, value);
}

static void select_window(BusCase *test, unsigned gate, uint8_t selector) {
    test->gate = gates[gate].value;
    test->enabled = gates[gate].enabled;
    test->selector = selector;
    write_bus(test, 0x0000, test->gate);
    write_bus(test, 0x4000, selector);
}

static void prime_bus(BusCase *test, unsigned marker) {
    assert(marker < COUNT(markers));
    assert(cupid_gb_read_bus_u8(test->gb, (uint16_t)(MARKER_BASE + marker)) == markers[marker]);
    expect_drive(test, markers[marker]);
}

static void advance_dots(BusCase *test, unsigned dots) {
    const uint64_t before = test->gb->elapsed_cycles;
    const unsigned cycles = dots * (test->profile->fast ? 2u : 1u);
    /* Advance the actual peripheral clocks. Stable double speed uses two CPU
     * T-cycles per base dot; every observation remains on a whole-dot boundary. */
    test->phase = cupid_gb_tick_color_dma_slot(test->gb, cycles, test->phase);
    assert(test->gb->elapsed_cycles == before + dots);
    assert(!test->profile->fast || !(test->phase & 1u));
    check_history(test);
}

static int response(const BusCase *test) {
    return test->enabled ? test->fixture->responses[test->selector & 15u] : FLOATING;
}

static void read_window(BusCase *test, uint16_t address) {
    const int driven = response(test);
    const uint8_t expected = driven == FLOATING ? expected_held(test) : (uint8_t)driven;
    const uint8_t actual = cupid_gb_read_bus_u8(test->gb, address);
    if (actual != expected)
        fprintf(stderr, "%s/%s gate=%02x select=%02x address=%04x actual=%02x expected=%02x\n",
                test->profile->name, test->fixture->name, test->gate, test->selector, address,
                actual, expected);
    assert(actual == expected);
    if (driven != FLOATING)
        expect_drive(test, expected);
    else
        check_history(test);
}

static void test_all_selectors(BusCase *test) {
    for (unsigned gate = 0; gate < COUNT(gates); ++gate)
        for (unsigned selector = 0; selector < 256; ++selector)
            for (unsigned address = 0; address < COUNT(addresses); ++address) {
                select_window(test, gate, (uint8_t)selector);
                prime_bus(test, 2);
                advance_dots(test, 1);
                read_window(test, addresses[address]);
            }
}

static void test_decay(BusCase *test) {
    const uint16_t endpoints[] = {0xa000, 0xbfff};
    /* The full selector sweep covers high-bit aliases and both enable bytes.
     * Here the low-nibble truth table is crossed with values and decay edges. */
    for (unsigned gate = 0; gate < 2; ++gate)
        for (unsigned selector = 0; selector < 16; ++selector)
            for (unsigned marker = 0; marker < COUNT(markers); ++marker)
                for (unsigned delay = 0; delay < COUNT(delays); ++delay)
                    for (unsigned address = 0; address < COUNT(endpoints); ++address) {
                        select_window(test, gate, (uint8_t)selector);
                        prime_bus(test, marker);
                        advance_dots(test, delays[delay]);
                        read_window(test, endpoints[address]);
                        advance_dots(test, 1);
                        read_window(test, endpoints[address]);
                    }
}

static void test_ignored_writes_and_passive_reads(BusCase *test) {
    CupidGb *gb = test->gb;
    const uint16_t endpoints[] = {0xa000, 0xbfff};
    uint8_t *storage = malloc(CUPID_GB_INLINE_RAM_SIZE);
    assert(storage);
    /* These fixtures all fit the inline allocation. Include unused storage in
     * the snapshot so an absent-chip write cannot hide outside declared SRAM. */
    assert(test->fixture->ram_bytes <= CUPID_GB_INLINE_RAM_SIZE);
    memcpy(storage, cupid_gb_cartridge_ram_const(gb), CUPID_GB_INLINE_RAM_SIZE);
    CupidGbRtc rtc;
    memcpy(&rtc, &gb->rtc, sizeof(rtc));
    const uint32_t rtc_cycles = gb->rtc_cycles;
    const uint64_t generation = gb->battery_write_generation;
    for (unsigned gate = 0; gate < COUNT(gates); ++gate)
        for (unsigned selector = 0; selector < 16; ++selector) {
            select_window(test, gate, (uint8_t)selector);
            const int driven = response(test);
            if (driven != FLOATING && driven != 0xff)
                continue;
            for (unsigned address = 0; address < COUNT(endpoints); ++address)
                for (unsigned marker = 0; marker < COUNT(markers); ++marker) {
                    prime_bus(test, 2);
                    const uint64_t expiry = gb->cartridge_bus_expiry;
                    const uint64_t clock = gb->elapsed_cycles;
                    const uint8_t held = gb->cartridge_bus_data;
                    /* Passive inspection has its own FF contract. It supplies
                     * no expected value to any active-bus assertion. */
                    assert(cupid_gb_read_u8(gb, endpoints[address]) == 0xff);
                    assert(gb->cartridge_bus_expiry == expiry);
                    assert(gb->cartridge_bus_data == held);
                    assert(gb->elapsed_cycles == clock);
                    write_bus(test, endpoints[address], markers[marker]);
                    advance_dots(test, 1);
                    read_window(test, endpoints[address]);
                }
        }
    assert(memcmp(storage, cupid_gb_cartridge_ram_const(gb), CUPID_GB_INLINE_RAM_SIZE) == 0);
    assert(memcmp(&rtc, &gb->rtc, sizeof(rtc)) == 0);
    assert(gb->rtc_cycles == rtc_cycles);
    assert(gb->battery_write_generation == generation);
    free(storage);
}

static void test_native_continuation(const Profile *profile) {
    BusCase original = make_case(profile, &fixtures[0]);
    BusCase restored = make_case(profile, &fixtures[0]);
    select_window(&original, 1, 4);
    prime_bus(&original, 3);
    advance_dots(&original, 4);
    assert(original.phase == 0);
    assert(original.expiry - original.gb->elapsed_cycles * 2u == 4);
    const size_t size = cupid_gb_state_size(original.gb);
    size_t written = 0;
    uint8_t *state = malloc(size);
    assert(state);
    assert(cupid_gb_save_state(original.gb, state, size, &written) == CUPID_GB_STATE_OK);
    assert(written == size);

    /* Give the receiver a different gate, selected bank, value and elapsed time.
     * A successful import must replace that history, not recreate a fresh hold. */
    select_window(&restored, 0, 0);
    prime_bus(&restored, 0);
    advance_dots(&restored, 8);
    assert(restored.gb->elapsed_cycles != original.gb->elapsed_cycles);
    assert(cupid_gb_load_state(restored.gb, state, written) == CUPID_GB_STATE_OK);
    restored.gate = original.gate;
    restored.enabled = original.enabled;
    restored.selector = original.selector;
    restored.held = original.held;
    restored.expiry = original.expiry;
    restored.phase = original.phase;
    assert(restored.gb->elapsed_cycles == original.gb->elapsed_cycles);
    check_history(&restored);
    for (unsigned dot = 4; dot <= 7; ++dot) {
        read_window(&original, 0xa000);
        read_window(&restored, 0xa000);
        assert(cupid_gb_sample_cartridge_bus(original.gb) == (dot < 6 ? 0xa5 : 0xff));
        assert(cupid_gb_sample_cartridge_bus(restored.gb) == (dot < 6 ? 0xa5 : 0xff));
        if (dot < 7) {
            advance_dots(&original, 1);
            advance_dots(&restored, 1);
        }
    }
    free(state);
    release_case(&original);
    release_case(&restored);
}

int main(void) {
    test_minimal_absent_sram();
    for (unsigned profile = 0; profile < COUNT(profiles); ++profile) {
        for (unsigned fixture = 0; fixture < COUNT(fixtures); ++fixture) {
            BusCase test = make_case(&profiles[profile], &fixtures[fixture]);
            test_all_selectors(&test);
            test_decay(&test);
            test_ignored_writes_and_passive_reads(&test);
            release_case(&test);
        }
        test_native_continuation(&profiles[profile]);
    }
    puts("MBC3 bus tests passed.");
    return 0;
}

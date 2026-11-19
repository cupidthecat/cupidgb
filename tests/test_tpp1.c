/**
 * @file    test_tpp1.c
 * @brief   Regression tests for TPP1 banking, clock, motor, and supported memory geometry.
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
#include "cupid/gb/bess.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"
#include "cupid/gb/tpp1.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROM_BANK_SIZE = 0x4000, ROM_BANKS = 8, RAM_BANK_SIZE = 0x2000 };

static void update_checksum(uint8_t *rom) {
    uint8_t checksum = 0;
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        checksum = (uint8_t)(checksum - rom[offset] - 1);
    rom[0x14d] = checksum;
}

static void make_rom(uint8_t *rom, bool color) {
    memset(rom, 0, ROM_BANK_SIZE * ROM_BANKS);
    for (unsigned bank = 0; bank < ROM_BANKS; ++bank)
        rom[bank * ROM_BANK_SIZE] = (uint8_t)bank;
    memcpy(rom + 0x134, "TPP1 TEST", 9);
    rom[0x143] = color ? 0x80 : 0;
    rom[0x147] = 0xbc;
    rom[0x148] = 2;
    rom[0x149] = 0xc1;
    rom[0x14a] = 0x65;
    rom[0x152] = 3;
    rom[0x153] = 8;
    update_checksum(rom);
}

static void command(CupidGb *gb, uint8_t value) {
    cupid_gb_write_u8(gb, 0x3fff, value);
}

static void test_write_decode(CupidGb *gb) {
    const CupidGbRtcPolicy original_policy = gb->rtc_policy;
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xffff, 0);
    gb->cpu.ime = false;
    cupid_gb_write_u8(gb, 0x0000, 3);
    cupid_gb_write_u8(gb, 0x0001, 0);
    cupid_gb_write_u8(gb, 0x0002, 2);
    command(gb, 0x18);
    command(gb, 3);
    cupid_gb_write_u8(gb, 0xa010, 0x6d);
    command(gb, 5);
    static const uint8_t clock[] = {0xa5, 0x31, 0x37, 0x39};
    for (unsigned port = 0; port < sizeof(clock); ++port)
        cupid_gb_write_u8(gb, (uint16_t)(0xa000u + port), clock[port]);
    command(gb, 0x11);
    command(gb, 0x22);
    command(gb, 0);
    uint8_t before[sizeof(gb->tpp1)];
    memcpy(before, &gb->tpp1, sizeof(before));
    const uint64_t generation = gb->battery_write_generation;

    /* Only the fixed ROM window decodes mapper registers. A store to any
     * banked-ROM address must leave the latches, SRAM, clock and motor alone. */
    for (unsigned address = 0x4000; address < 0x8000; ++address) {
        const uint8_t program[] = {
            0x3e,
            (uint8_t)(address >> 2),
            0xea,
            (uint8_t)address,
            (uint8_t)(address >> 8),
            0xfa,
            0x00,
            0xa0,
            0xea,
            0x00,
            0xc8,
            0xfa,
            0x01,
            0xa0,
            0xea,
            0x01,
            0xc8,
            0xfa,
            0x02,
            0xa0,
            0xea,
            0x02,
            0xc8,
            0xfa,
            0x03,
            0xa0,
            0xea,
            0x03,
            0xc8,
        };
        for (unsigned byte = 0; byte < sizeof(program); ++byte)
            cupid_gb_write_u8(gb, (uint16_t)(0xc100u + byte), program[byte]);
        gb->cpu.pc = 0xc100;
        for (unsigned steps = 0; gb->cpu.pc != 0xc100u + sizeof(program); ++steps) {
            assert(steps < 16);
            assert(cupid_gb_step(gb));
        }
        assert(cupid_gb_read_u8(gb, 0xc800) == 3);
        assert(cupid_gb_read_u8(gb, 0xc801) == 0);
        assert(cupid_gb_read_u8(gb, 0xc802) == 2);
        assert(cupid_gb_read_u8(gb, 0xc803) == 2);
        assert(!memcmp(&gb->tpp1, before, sizeof(before)));
        assert(gb->current_rom_bank == 3 && gb->current_ram_bank == 2 && !gb->ram_enabled);
        assert(cupid_gb_read_u8(gb, 0x4000) == 3);
        assert(cupid_gb_cartridge_ram_const(gb)[2u * RAM_BANK_SIZE + 0x10u] == 0x6d);
        assert(gb->battery_write_generation == generation);
    }
    gb->rtc_policy = original_policy;
}

typedef struct RumbleProbe {
    unsigned transitions;
    bool enabled;
} RumbleProbe;

typedef struct ClockProbe {
    int64_t now;
    unsigned calls;
} ClockProbe;

static int64_t clock_time(void *context) {
    ClockProbe *probe = context;
    ++probe->calls;
    return probe->now;
}

static void observe_motor(const CupidGb *gb, bool enabled, uint64_t cycles, void *user_data) {
    RumbleProbe *probe = user_data;
    assert(cupid_gb_rumble_enabled(gb) == enabled);
    assert(cycles == gb->elapsed_cycles);
    ++probe->transitions;
    probe->enabled = enabled;
}

static void test_bank_modes_and_rumble(CupidGb *gb) {
    assert(gb->header.mbc_type == CUPID_GB_TPP1 && gb->header.has_battery);
    assert(gb->ram_bank_count == 4 && gb->cartridge_ram_size == 4 * RAM_BANK_SIZE);
    assert(cupid_gb_read_u8(gb, 0x4000) == 1 && cupid_gb_read_u8(gb, 0xa004) == 1);
    cupid_gb_write_u8(gb, 0x001c, 0x37);
    cupid_gb_write_u8(gb, 0x11fd, 0x89);
    cupid_gb_write_u8(gb, 0x2ffe, 0xfd);
    assert(cupid_gb_read_u8(gb, 0x4000) == 7 && cupid_gb_read_u8(gb, 0x0000) == 0);
    assert(cupid_gb_read_u8(gb, 0xbffc) == 0x37);
    assert(cupid_gb_read_u8(gb, 0xbffd) == 0x89);
    assert(cupid_gb_read_u8(gb, 0xbffe) == 0xfd);

    command(gb, 3);
    for (unsigned bank = 0; bank < 4; ++bank) {
        cupid_gb_write_u8(gb, 0x0002, (uint8_t)bank);
        assert(cupid_gb_read_u8(gb, 0xa123) == 0xff);
        cupid_gb_write_u8(gb, 0xa123, (uint8_t)(0x30 + bank));
    }
    command(gb, 2);
    command(gb, 4); /* Undefined commands leave the current mode selected. */
    for (unsigned bank = 0; bank < 256; ++bank) {
        cupid_gb_write_u8(gb, 0x0002, (uint8_t)bank);
        cupid_gb_write_u8(gb, 0xa123, 0x70);
        assert(cupid_gb_read_u8(gb, 0xa123) == 0x30 + (bank & 3));
    }
    command(gb, 0);
    RumbleProbe motor = {0};
    cupid_gb_set_rumble_callback(gb, observe_motor, &motor);
    for (unsigned strength = 1; strength <= 3; ++strength) {
        command(gb, (uint8_t)(0x20 + strength));
        assert(cupid_gb_read_u8(gb, 0xa003) == strength);
        assert(motor.enabled && motor.transitions == 1);
    }
    command(gb, 0x20);
    assert(!motor.enabled && motor.transitions == 2);
    cupid_gb_set_rumble_callback(gb, NULL, NULL);
}

static void test_clock_and_battery(CupidGb *gb) {
    static const uint8_t end_of_week[] = {255, 0xd7, 59, 59};
    command(gb, 5);
    for (unsigned port = 0; port < sizeof(end_of_week); ++port)
        cupid_gb_write_u8(gb, (uint16_t)(0xa004 + port), end_of_week[port]);
    command(gb, 0x11);
    cupid_gb_write_u8(gb, 0xa003, 0);
    command(gb, 0x10);
    assert(cupid_gb_read_u8(gb, 0xbfff) == 59);
    command(gb, 0x19);
    cupid_gb_tpp1_advance(&gb->tpp1, gb->tpp1.rtc_base_time + 86401);
    assert(gb->tpp1.status == 12);
    assert(gb->tpp1.live[0] == 0 && gb->tpp1.live[1] == 0x20);
    assert(gb->tpp1.live[2] == 0 && gb->tpp1.live[3] == 0);
    command(gb, 0x18);
    command(gb, 0);
    assert(cupid_gb_read_u8(gb, 0xa003) == 8);
    command(gb, 0x14);
    assert(cupid_gb_read_u8(gb, 0xa003) == 0);

    const size_t length = cupid_gb_battery_size(gb);
    assert(length == 4 * RAM_BANK_SIZE + 20);
    uint8_t *battery = malloc(length);
    assert(battery);
    command(gb, 3);
    cupid_gb_write_u8(gb, 0x0002, 0);
    cupid_gb_write_u8(gb, 0xa123, 0x29);
    assert(cupid_gb_save_battery_at(gb, battery, length, NULL, INT64_C(1800000000)) ==
           CUPID_GB_BATTERY_OK);
    assert(memcmp(battery + length - 20, "TPP1\x01\x00", 6) == 0);
    cupid_gb_write_u8(gb, 0xa123, 0x53);
    battery[length - 20] = 0;
    assert(cupid_gb_load_battery_at(gb, battery, length, INT64_C(1800000000)) ==
           CUPID_GB_BATTERY_INVALID_RTC);
    assert(cupid_gb_read_u8(gb, 0xa123) == 0x53);
    battery[length - 20] = 'T';
    battery[length - 14] = 2;
    RumbleProbe motor = {0};
    cupid_gb_set_rumble_callback(gb, observe_motor, &motor);
    assert(cupid_gb_load_battery_at(gb, battery, length, INT64_C(1800000000)) ==
           CUPID_GB_BATTERY_OK);
    assert(cupid_gb_read_u8(gb, 0xa123) == 0x29);
    assert(!cupid_gb_rumble_enabled(gb) && motor.transitions == 0);
    command(gb, 0);
    assert(cupid_gb_read_u8(gb, 0xa003) == 2);
    assert(cupid_gb_save_battery_at(gb, battery, length, NULL, INT64_C(1800000000)) ==
           CUPID_GB_BATTERY_OK);
    assert(battery[length - 14] == 2);
    cupid_gb_set_rumble_callback(gb, NULL, NULL);
    free(battery);
}

static void test_state_restore(CupidGb *gb) {
    command(gb, 0x22);
    command(gb, 5);
    cupid_gb_write_u8(gb, 0xa003, 0x58);
    const size_t size = cupid_gb_state_size(gb);
    assert(size);
    uint8_t *state = malloc(size);
    assert(state && cupid_gb_save_state(gb, state, size, NULL) == CUPID_GB_STATE_OK);
    assert(state[8] == CUPID_GB_STATE_VERSION);
    command(gb, 0x20);
    command(gb, 0);
    cupid_gb_write_u8(gb, 0, 0);
    cupid_gb_write_u8(gb, 1, 0);
    assert(cupid_gb_load_state(gb, state, size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_rumble_enabled(gb));
    assert(cupid_gb_read_u8(gb, 0xa003) == 0x58);
    assert(cupid_gb_read_u8(gb, 0x4000) == 7);
    free(state);
}

static void test_clock_commands_and_motor_storage(CupidGb *gb) {
    const CupidGbTpp1 original = gb->tpp1;
    const CupidGbRtc original_rtc = gb->rtc;
    const CupidGbRtcPolicy original_policy = gb->rtc_policy;
    const uint32_t original_phase = gb->rtc_cycles;
    const uint64_t original_generation = gb->battery_write_generation;
    ClockProbe clock = {5000, 0};
    cupid_gb_set_rtc_time_source(gb, clock_time, &clock);
    const size_t size = cupid_gb_battery_size(gb);
    uint8_t *battery = malloc(size);
    assert(battery);
    for (unsigned mode = 0; mode < 2; ++mode) {
        assert(cupid_gb_set_rtc_mode(gb, (CupidGbRtcMode)mode));
        for (unsigned mr4 = 0; mr4 < 16; ++mr4) {
            for (unsigned strength = 0; strength < 4; ++strength) {
                gb->tpp1 = original;
                gb->tpp1.status = (uint8_t)mr4;
                gb->tpp1.motor_strength = 0;
                gb->tpp1.rtc_base_time = 1000;
                gb->rtc_cycles = 1234;
                RumbleProbe motor = {0};
                cupid_gb_set_rumble_callback(gb, observe_motor, &motor);
                const unsigned calls = clock.calls;
                command(gb, (uint8_t)(0x20 + strength));
                command(gb, 0);
                assert(gb->tpp1.status == mr4 && gb->tpp1.motor_strength == strength);
                assert(cupid_gb_read_u8(gb, 0xa003) == (mr4 | strength));
                assert(cupid_gb_rumble_enabled(gb) == (strength != 0));
                assert(motor.transitions == (strength != 0));
                assert(cupid_gb_save_battery_at(gb, battery, size, NULL, 6000) ==
                       CUPID_GB_BATTERY_OK);
                assert(battery[size - 14] == mr4 && gb->tpp1.rtc_base_time == 1000);
                assert(cupid_gb_load_battery_at(gb, battery, size, 9000) == CUPID_GB_BATTERY_OK);
                assert(gb->tpp1.status == mr4 && gb->tpp1.motor_strength == strength &&
                       gb->tpp1.rtc_base_time == 6000 && gb->rtc_cycles == 1234);
                assert(clock.calls == calls && motor.transitions == (strength != 0));
                cupid_gb_set_rumble_callback(gb, NULL, NULL);
            }
        }
        gb->tpp1 = original;
        gb->tpp1.status = 4;
        gb->tpp1.live[0] = 3;
        gb->tpp1.live[1] = 4;
        gb->tpp1.live[2] = 5;
        gb->tpp1.live[3] = 6;
        gb->tpp1.rtc_base_time = 1000;
        gb->rtc.dh = 0x40;
        gb->rtc.ldh = 0;
        gb->rtc_cycles = 4321;
        clock.now = 1061;
        const unsigned calls = clock.calls;
        const uint64_t generation = gb->battery_write_generation;
        command(gb, 0x10);
        command(gb, 0x10);
        assert(gb->rtc.ldh == 0x40 && gb->battery_write_generation == generation + 2);
        gb->rtc.dh = 0;
        command(gb, 0x11);
        command(gb, 0x11);
        assert(gb->rtc.dh == 0x40 && gb->rtc_cycles == 4321 && gb->tpp1.rtc_base_time == 1000 &&
               gb->tpp1.live[3] == 6);
        assert(gb->battery_write_generation == generation + 4 && clock.calls == calls);
        gb->tpp1.status = 12;
        command(gb, 0x14);
        assert(gb->tpp1.status == 4 && gb->battery_write_generation == generation + 4);
        command(gb, 0x18);
        assert(gb->tpp1.status == 0 && gb->rtc_cycles == 4321);
        assert(gb->tpp1.rtc_base_time == (mode ? 1000 : 1061));
        assert(gb->tpp1.live[3] == (mode ? 6 : 7));
        assert(clock.calls == calls + (mode ? 0 : 1));
        clock.now = 2000;
        command(gb, 0x19);
        command(gb, 0x19);
        assert(gb->tpp1.rtc_base_time == (mode ? 1000 : 2000) && gb->rtc_cycles == 4321);
        assert(clock.calls == calls + (mode ? 0 : 2));
        assert(gb->battery_write_generation == generation + 4);
    }
    free(battery);
    gb->tpp1 = original;
    gb->rtc = original_rtc;
    gb->rtc_policy = original_policy;
    gb->rtc_cycles = original_phase;
    gb->battery_write_generation = original_generation;
}

static void test_absent_sram_bus(CupidGb *gb) {
    assert(!gb->cartridge_ram_size && !gb->ram_bank_count);
    const uint64_t writes = gb->battery_write_generation;
    for (unsigned mode = 2; mode <= 3; ++mode) {
        command(gb, (uint8_t)mode);
        for (unsigned bank = 0; bank < 256; ++bank) {
            cupid_gb_write_u8(gb, 0x0002, (uint8_t)bank);
            cupid_gb_write_u8(gb, 0xa000, 0x25);
            cupid_gb_write_u8(gb, 0xbfff, 0x73);
            /* An ignored SRAM write still drives the CPU's value onto the bus. */
            assert(cupid_gb_read_bus_u8(gb, 0xa000) == 0x73);
            assert(cupid_gb_read_bus_u8(gb, 0x0200) == 0x6d);
            const uint64_t expiry = gb->cartridge_bus_expiry;
            assert(cupid_gb_read_u8(gb, 0xa000) == 0xff);
            assert(cupid_gb_read_u8(gb, 0xbfff) == 0xff);
            assert(cupid_gb_read_bus_u8(gb, 0xa000) == 0x6d);
            assert(cupid_gb_read_bus_u8(gb, 0xbfff) == 0x6d);
            assert(gb->cartridge_bus_expiry == expiry);
            /* Floating reads cannot renew the six-T-cycle cartridge-bus hold. */
            gb->elapsed_cycles += 5;
            assert(cupid_gb_read_bus_u8(gb, 0xa123) == 0x6d);
            assert(gb->cartridge_bus_expiry == expiry);
            ++gb->elapsed_cycles;
            assert(cupid_gb_read_bus_u8(gb, 0xbfff) == 0xff);
            assert(gb->cartridge_bus_expiry == expiry);
        }
    }
    assert(gb->battery_write_generation == writes);
}

static void test_supported_sram_populations(CupidGb *gb, uint8_t *rom, size_t length) {
    static const unsigned populations[] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 256};
    static const uint16_t addresses[] = {0xa000, 0xa001, 0xa123, 0xafff, 0xb000, 0xbfff};

    enum { SLOTS = sizeof(addresses) / sizeof(addresses[0]) };

    for (unsigned code = 0; code < sizeof(populations) / sizeof(populations[0]); ++code) {
        const unsigned banks = populations[code];
        rom[0x152] = (uint8_t)code;
        rom[0x153] = 8;
        rom[0x200] = 0x6d;
        assert(cupid_gb_load_rom(gb, rom, length));
        assert(gb->header.mbc_type == CUPID_GB_TPP1 && gb->header.ram_bank_count == banks);
        assert(gb->ram_bank_count == banks);
        assert(gb->cartridge_ram_size == (size_t)banks * RAM_BANK_SIZE);
        assert(cupid_gb_battery_size(gb) == (size_t)banks * RAM_BANK_SIZE + 20);
        const bool large = gb->cartridge_ram_size > CUPID_GB_INLINE_RAM_SIZE;
        assert((gb->cartridge_ram_heap != NULL) == large);
        assert(gb->cartridge_ram_capacity == (large ? gb->cartridge_ram_size : 0));
        assert(cupid_gb_cartridge_ram(gb) == (large ? gb->cartridge_ram_heap : gb->cartridge_ram));
        for (size_t offset = 0; offset < gb->cartridge_ram_size; ++offset)
            assert(cupid_gb_cartridge_ram_const(gb)[offset] == 0xff);
        if (!banks) {
            test_absent_sram_bus(gb);
            continue;
        }

        uint8_t expected[256][SLOTS] = {{0}};
        command(gb, 3);
        for (unsigned bank = 0; bank < banks; ++bank) {
            cupid_gb_write_u8(gb, 0x2002, (uint8_t)bank);
            for (unsigned slot = 0; slot < SLOTS; ++slot) {
                assert(cupid_gb_read_bus_u8(gb, addresses[slot]) == 0xff);
                expected[bank][slot] = (uint8_t)(bank * 73u + slot * 11u + 0x23u);
                cupid_gb_write_u8(gb, addresses[slot], expected[bank][slot]);
            }
        }

        const uint64_t writes = gb->battery_write_generation;
        for (unsigned bank = 0; bank < 256; ++bank) {
            cupid_gb_write_u8(gb, 0x1002, (uint8_t)bank);
            command(gb, 0);
            assert(cupid_gb_read_u8(gb, 0xbffe) == bank);
            command(gb, 2);
            for (unsigned slot = 0; slot < SLOTS; ++slot) {
                const uint8_t value = expected[bank % banks][slot];
                assert(cupid_gb_read_bus_u8(gb, addresses[slot]) == value);
                cupid_gb_write_u8(gb, addresses[slot], (uint8_t)~value);
                assert(cupid_gb_read_bus_u8(gb, addresses[slot]) == value);
            }
        }
        assert(gb->battery_write_generation == writes);

        /* Writes through every raw latch value must change only its physical bank. */
        for (unsigned bank = 0; bank < 256; ++bank) {
            const unsigned slot = bank % SLOTS;
            const uint8_t value = (uint8_t)(0x5au ^ bank);
            command(gb, 3);
            cupid_gb_write_u8(gb, 0x3002, (uint8_t)bank);
            const uint64_t generation = gb->battery_write_generation;
            const bool changed = expected[bank % banks][slot] != value;
            cupid_gb_write_u8(gb, addresses[slot], value);
            cupid_gb_write_u8(gb, addresses[slot], value);
            assert(gb->battery_write_generation == generation + changed);
            expected[bank % banks][slot] = value;
            assert(cupid_gb_read_bus_u8(gb, addresses[slot]) == value);
            command(gb, 0);
            assert(cupid_gb_read_u8(gb, 0xbffe) == bank);
            command(gb, 2);
            for (unsigned physical = 0; physical < banks; ++physical) {
                cupid_gb_write_u8(gb, 0x0002, (uint8_t)physical);
                for (unsigned check = 0; check < SLOTS; ++check)
                    assert(cupid_gb_read_bus_u8(gb, addresses[check]) == expected[physical][check]);
            }
        }
    }
    static const uint8_t absent_codes[] = {10, 255};
    for (unsigned entry = 0; entry < sizeof(absent_codes); ++entry) {
        rom[0x152] = absent_codes[entry];
        assert(cupid_gb_load_rom(gb, rom, length));
        assert(!gb->cartridge_ram_heap && !gb->cartridge_ram_capacity);
        test_absent_sram_bus(gb);
    }
}

static void assert_large_rom_bank(const CupidGb *gb, unsigned bank) {
    /* Two-byte markers distinguish the upper 256 banks from the lower half. */
    assert(cupid_gb_read_u8(gb, 0x4200) == (uint8_t)bank);
    assert(cupid_gb_read_u8(gb, 0x4201) == ((bank >> 8u) & 1u));
    assert(cupid_gb_read_u8(gb, 0x0200) == 0);
    assert(cupid_gb_read_u8(gb, 0x0201) == 0);
}

static void test_full_rom_bank_addressing(CupidGb *gb, bool color) {
    const unsigned banks = 512;
    const size_t length = (size_t)banks * ROM_BANK_SIZE;
    uint8_t *rom = calloc(length, 1);
    assert(rom);
    make_rom(rom, color);
    rom[0x148] = 8;
    rom[0x152] = 0;
    update_checksum(rom);
    for (unsigned bank = 0; bank < banks; ++bank) {
        const size_t offset = (size_t)bank * ROM_BANK_SIZE + 0x200;
        rom[offset] = (uint8_t)bank;
        rom[offset + 1] = (uint8_t)(bank >> 8u);
    }
    assert(cupid_gb_load_rom(gb, rom, length));
    assert(gb->rom_size == length && gb->rom_bank_count == banks);
    unsigned previous = 1;
    for (unsigned bank = 0; bank <= 0xffff; ++bank) {
        const uint16_t port = (uint16_t)((bank & 0x0fffu) << 2u);
        if (bank & 1u) {
            cupid_gb_write_u8(gb, (uint16_t)(port | 1u), (uint8_t)(bank >> 8u));
            assert_large_rom_bank(gb, (bank & 0xff00u) | (previous & 0xffu));
            assert(cupid_gb_read_u8(gb, 0xbffc) == (uint8_t)previous);
            cupid_gb_write_u8(gb, port, (uint8_t)bank);
        } else {
            cupid_gb_write_u8(gb, port, (uint8_t)bank);
            assert_large_rom_bank(gb, (previous & 0xff00u) | (bank & 0xffu));
            assert(cupid_gb_read_u8(gb, 0xbffd) == (uint8_t)(previous >> 8u));
            cupid_gb_write_u8(gb, (uint16_t)(port | 1u), (uint8_t)(bank >> 8u));
        }
        assert(cupid_gb_read_u8(gb, 0xbffc) == (uint8_t)bank);
        assert(cupid_gb_read_u8(gb, 0xbffd) == (uint8_t)(bank >> 8u));
        assert_large_rom_bank(gb, bank);
        previous = bank;
    }
    static const uint8_t modes[] = {0, 2, 3, 5};
    static const unsigned selections[] = {0, 0xff, 0x100, 0x1ff, 0x8000, 0xffff};
    for (unsigned mode = 0; mode < sizeof(modes) / sizeof(modes[0]); ++mode) {
        command(gb, modes[mode]);
        for (unsigned selection = 0; selection < sizeof(selections) / sizeof(selections[0]);
             ++selection) {
            const unsigned bank = selections[selection];
            cupid_gb_write_u8(gb, 0x0001, (uint8_t)(bank >> 8u));
            cupid_gb_write_u8(gb, 0x0000, (uint8_t)bank);
            assert_large_rom_bank(gb, bank);
        }
    }
    free(rom);
}

static unsigned little_u32(const uint8_t *data) {
    return (unsigned)data[0] | (unsigned)data[1] << 8 | (unsigned)data[2] << 16 |
           (unsigned)data[3] << 24;
}

static void test_portable_state(CupidGb *source, uint8_t *rom, size_t length, bool color) {
    make_rom(rom, color);
    rom[0x152] = 9;
    rom[0x153] = 9;
    assert(cupid_gb_load_rom(source, rom, length));
    assert(cupid_gb_reset(source, CUPID_GB_RESET_HARD));
    CupidGb *target = calloc(1, sizeof(*target));
    assert(target);
    cupid_gb_set_model(target, source->model);
    assert(cupid_gb_load_rom(target, rom, length));
    assert(target->cartridge_ram_size == 0x200000);
    ClockProbe clock = {.now = 123450};
    RumbleProbe motor = {0};
    cupid_gb_set_rtc_time_source(target, clock_time, &clock);
    cupid_gb_set_rumble_callback(target, observe_motor, &motor);

    assert(cupid_gb_read_u8(source, 0x4000) == 1 && cupid_gb_read_u8(source, 0xa000) == 0);
    size_t size = cupid_gb_bess_size(source);
    uint8_t *data = malloc(size);
    assert(size && data && cupid_gb_bess_save(source, data, size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_bess_load(target, data, size) == CUPID_GB_STATE_OK && clock.calls == 1);
    assert(cupid_gb_read_u8(target, 0x4000) == 1 && cupid_gb_read_u8(target, 0xa000) == 0);
    command(target, 0);
    assert(cupid_gb_read_u8(target, 0x4000) == 0);
    free(data);

    cupid_gb_write_u8(source, 0x2000, 7);
    cupid_gb_write_u8(source, 0x2001, 0xf1);
    cupid_gb_write_u8(source, 0x2002, 255);
    command(source, 3);
    cupid_gb_write_u8(source, 0xbfff, 0x97);
    command(source, 0x22);
    command(source, 5);
    const uint8_t live[] = {0x51, 0xcb, 0x75, 0x96};
    const uint8_t latched[] = {0x86, 0xde, 0x47, 0x2a};
    memcpy(source->tpp1.live, live, sizeof(live));
    memcpy(source->tpp1.latched, latched, sizeof(latched));
    source->tpp1.status = 0x0d;
    source->tpp1.rtc_base_time = 123400;

    size = cupid_gb_bess_size(source);
    data = malloc(size);
    assert(size && data && cupid_gb_bess_save(source, data, size, NULL) == CUPID_GB_STATE_OK);
    uint8_t *block = data + little_u32(data + size - 8u);
    while (block + 8u <= data + size - 8u && memcmp(block, "TPP1", 4))
        block += 8u + little_u32(block + 4);
    assert(block + 25u <= data + size - 8u && little_u32(block + 4) == 17);
    uint8_t *payload = block + 8;
    assert(cupid_gb_bess_load(target, data, size) == CUPID_GB_STATE_OK && clock.calls == 2);
    assert(!motor.transitions && target->tpp1.motor_strength == 2 && target->tpp1.mode == 5);
    assert(target->tpp1.rtc_base_time == 123400 && target->tpp1.status == 0x0d);
    assert(!memcmp(target->tpp1.live, live, sizeof(live)));
    assert(!memcmp(target->tpp1.latched, latched, sizeof(latched)));
    assert(cupid_gb_read_u8(target, 0xa000) == latched[0]);
    assert(cupid_gb_read_u8(target, 0x4000) == 7);
    command(target, 0);
    assert(cupid_gb_read_u8(target, 0xa000) == 7 && cupid_gb_read_u8(target, 0xa001) == 0xf1);
    assert(cupid_gb_read_u8(target, 0xa002) == 255 && cupid_gb_read_u8(target, 0xa003) == 0x0f);
    command(target, 2);
    assert(cupid_gb_read_u8(target, 0xbfff) == 0x97);

    payload[16] = 0x8f;
    assert(cupid_gb_bess_load(target, data, size) == CUPID_GB_STATE_INVALID && clock.calls == 2);
    assert(target->tpp1.status == 0x0d && cupid_gb_read_u8(target, 0xbfff) == 0x97);
    payload[16] = 0x0d;
    memset(payload, 0xff, 8);
    assert(cupid_gb_bess_load(target, data, size) == CUPID_GB_STATE_OK && clock.calls == 3);
    assert(target->tpp1.rtc_base_time == clock.now && target->tpp1.status == 0x0d);
    memcpy(block, "TEST", 4);
    assert(cupid_gb_bess_load(target, data, size) == CUPID_GB_STATE_OK && clock.calls == 4);
    assert(target->tpp1.mode == 5 && target->tpp1.motor_strength == 2 && !target->tpp1.status);
    assert(!cupid_gb_read_u8(target, 0xa000) && target->tpp1.rtc_base_time == clock.now);
    memcpy(block, "TPP1", 4);
    memset(payload, 0, 8);
    assert(cupid_gb_set_rtc_mode(target, CUPID_GB_RTC_ACCURATE) && clock.calls == 5);
    assert(cupid_gb_bess_load(target, data, size) == CUPID_GB_STATE_OK && clock.calls == 6);
    assert(target->tpp1.rtc_base_time == clock.now && target->tpp1.status == 0x0d);
    const size_t native_size = cupid_gb_state_size(target);
    uint8_t *native = malloc(native_size);
    assert(native_size && native);
    assert(cupid_gb_save_state(target, native, native_size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_load_state(target, native, native_size) == CUPID_GB_STATE_OK);
    assert(target->tpp1.motor_strength == 2 && target->tpp1.ram_bank == 255 && !motor.transitions);
    free(native);
    free(data);
    cupid_gb_cleanup(target);
    free(target);
}

int main(void) {
    const size_t length = ROM_BANK_SIZE * ROM_BANKS;
    uint8_t *rom = malloc(length);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(rom && gb);
    for (unsigned color = 0; color < 2; ++color) {
        make_rom(rom, color != 0);
        cupid_gb_set_model(gb, color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
        assert(cupid_gb_load_rom(gb, rom, length));
        test_write_decode(gb);
        assert(!memcmp(gb->rom, rom, length));
        assert(cupid_gb_load_rom(gb, rom, length));
        test_bank_modes_and_rumble(gb);
        test_clock_and_battery(gb);
        test_state_restore(gb);
        test_clock_commands_and_motor_storage(gb);
        rom[0x152] = 0;
        rom[0x153] = 0;
        assert(cupid_gb_load_rom(gb, rom, length));
        assert(!gb->cartridge_ram_size && !cupid_gb_battery_size(gb));
        test_supported_sram_populations(gb, rom, length);
        test_full_rom_bank_addressing(gb, color != 0);
        test_portable_state(gb, rom, length, color != 0);
    }
    cupid_gb_cleanup(gb);
    free(gb);
    free(rom);
    puts("TPP1 cartridge regressions passed.");
    return 0;
}

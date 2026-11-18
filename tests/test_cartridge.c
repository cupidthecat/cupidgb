/**
 * @file    test_cartridge.c
 * @brief   Regression tests for cartridge headers, storage geometry, and mapper banking.
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
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"

#include "../src/gb/cartridge_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void update_header_checksum(uint8_t *rom) {
    uint8_t checksum = 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        checksum = (uint8_t)(checksum - rom[i] - 1);
    rom[0x14d] = checksum;
}

static uint8_t *make_rom(size_t size, uint8_t type, uint8_t rom_code, uint8_t ram_code) {
    uint8_t *rom = malloc(size);
    assert(rom && size >= 0x150);
    for (size_t offset = 0; offset < size; ++offset) {
        rom[offset] = (uint8_t)(offset / 0x4000);
    }
    memcpy(rom + 0x134, "CARTRIDGE TEST", 14);
    rom[0x143] = 0;
    rom[0x147] = type;
    rom[0x148] = rom_code;
    rom[0x149] = ram_code;
    update_header_checksum(rom);
    return rom;
}

static uint8_t *make_model_rom(CupidGbModel model, size_t size, uint8_t type, uint8_t rom_code,
                               uint8_t ram_code) {
    uint8_t *rom = make_rom(size, type, rom_code, ram_code);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    update_header_checksum(rom);
    return rom;
}

static void test_adjacent_caller_sources(CupidGbModel model) {
    typedef struct AdjacentSource {
        CupidGb core;
        uint8_t input[];
    } AdjacentSource;

    const size_t rom_size = 0x8000u;
    uint8_t *rom = make_model_rom(model, rom_size, 0x03, 0, 2);
    CupidGb *original = calloc(1, sizeof(*original));
    assert(original);
    cupid_gb_set_model(original, model);
    assert(cupid_gb_load_rom(original, rom, rom_size));
    original->cpu.a = 0x53;
    original->work_ram[0x123] = 0x97;
    for (size_t offset = 0; offset < original->cartridge_ram_size; ++offset)
        original->cartridge_ram[offset] = (uint8_t)(offset * 37u + (offset >> 8u) + 0x23u);

    const size_t state_size = cupid_gb_state_size(original);
    const size_t capacity = state_size > rom_size ? state_size : rom_size;
    uint8_t *state = malloc(state_size);
    AdjacentSource *fixture = calloc(1, sizeof(*fixture) + capacity);
    assert(state_size && state && fixture);
    assert(cupid_gb_save_state(original, state, state_size, NULL) == CUPID_GB_STATE_OK);
    CupidGb *gb = &fixture->core;
    /* Force the layout that optimized stack allocation can produce. This
     * input is a separate member, even though its first address is core's end. */
    assert((uintptr_t)fixture->input - (uintptr_t)gb == sizeof(*gb));
    cupid_gb_set_model(gb, model);
    memcpy(fixture->input, rom, rom_size);
    assert(cupid_gb_load_rom(gb, fixture->input, rom_size));
    assert(gb->model == model && gb->rom_size == rom_size);
    assert(!memcmp(gb->rom, rom, rom_size) && !memcmp(fixture->input, rom, rom_size));

    /* Battery and native-state imports share the same owned-source guard. */
    const size_t battery_size = cupid_gb_battery_size(original);
    size_t written = 0;
    assert(battery_size == original->cartridge_ram_size && battery_size <= capacity);
    assert(cupid_gb_save_battery_at(original, fixture->input, capacity, &written, 0) ==
           CUPID_GB_BATTERY_OK);
    assert(written == battery_size);
    uint8_t *const owned_rom = gb->rom;
    assert(cupid_gb_load_battery_at(gb, fixture->input, battery_size, 0) == CUPID_GB_BATTERY_OK);
    assert(gb->rom == owned_rom && !gb->battery_write_generation);
    assert(!memcmp(gb->cartridge_ram, original->cartridge_ram, battery_size));
    assert(!memcmp(fixture->input, original->cartridge_ram, battery_size));

    gb->cpu.a = 0x11;
    gb->work_ram[0x123] = 0x22;
    memset(gb->cartridge_ram, 0, battery_size);
    memcpy(fixture->input, state, state_size);
    assert(cupid_gb_load_state(gb, fixture->input, state_size) == CUPID_GB_STATE_OK);
    assert(gb->rom == owned_rom && gb->cpu.a == 0x53 && gb->work_ram[0x123] == 0x97);
    assert(!memcmp(gb->cartridge_ram, original->cartridge_ram, battery_size));
    assert(!memcmp(fixture->input, state, state_size));

    cupid_gb_cleanup(gb);
    cupid_gb_cleanup(original);
    free(fixture);
    free(original);
    free(state);
    free(rom);
}

static size_t expected_header_storage(unsigned type, unsigned ram_code) {
    static const bool external_ram[256] = {
        [0x01] = true, [0x02] = true, [0x03] = true, [0x08] = true, [0x09] = true, [0x0b] = true,
        [0x0c] = true, [0x0d] = true, [0x0f] = true, [0x10] = true, [0x11] = true, [0x12] = true,
        [0x13] = true, [0x19] = true, [0x1a] = true, [0x1b] = true, [0x1c] = true, [0x1d] = true,
        [0x1e] = true, [0xfc] = true, [0xfe] = true, [0xff] = true};
    static const size_t sizes[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
    if (type == 0x05 || type == 0x06)
        return 512;
    if (type == 0x22)
        return 256;
    if (type == 0x20)
        return 0x8000;
    if (type == 0xfd)
        return 32;
    return external_ram[type] && ram_code < sizeof(sizes) / sizeof(sizes[0]) ? sizes[ram_code] : 0;
}

static void check_storage_window(CupidGb *gb, size_t expected_size) {
    assert(gb->cartridge_ram_size == expected_size);
    assert(gb->ram_bank_count == (expected_size + 0x1fff) / 0x2000);
    if (gb->header.mbc_type == CUPID_GB_TAMA5) {
        /* The 32 bytes are accessed through a nibble protocol. */
        assert(gb->cartridge_ram[31] == 0xff);
        return;
    }
    if (gb->header.mbc_type == CUPID_GB_MBC7) {
        /* EEPROM uses its serial protocol; it never becomes ordinary SRAM. */
        assert(gb->cartridge_ram[0x31] == 0xff);
        return;
    }
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0, 0x0a);
    cupid_gb_write_u8(gb, 0xa031, 0x25);
    const size_t rom_offset = cupid_gb_effective_rom_bank(gb, true) * 0x4000 + 0x200;
    assert(rom_offset < gb->rom_size);
    gb->rom[rom_offset] = 0x6d;
    assert(cupid_gb_read_bus_u8(gb, 0x200) == 0x6d);
    if (expected_size) {
        const uint8_t expected = gb->header.mbc_type == CUPID_GB_MBC2 ? 0xf5 : 0x25;
        assert(cupid_gb_read_bus_u8(gb, 0xa031) == expected);
    } else {
        /* A missing chip leaves the held bus undriven, even after a RAM write.
         * Passive inspection must neither reveal storage nor refresh its hold. */
        const uint64_t expiry = gb->cartridge_bus_expiry;
        assert(cupid_gb_read_u8(gb, 0xa031) == 0xff);
        assert(cupid_gb_read_bus_u8(gb, 0xa031) == 0x6d);
        assert(gb->cartridge_bus_expiry == expiry);
        assert(gb->cartridge_ram[0x31] == 0);
        gb->elapsed_cycles += 6;
        assert(cupid_gb_read_bus_u8(gb, 0xa031) == 0xff);
        assert(gb->cartridge_bus_expiry == expiry);
    }
}

static void test_header_storage_capabilities(void) {
    const uint8_t ram_codes[] = {0, 1, 2, 3, 4, 5, 6, 0xff};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned color = 0; color < 2; ++color) {
        cupid_gb_set_model(gb, color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
        for (unsigned type = 0; type < 256; ++type) {
            for (unsigned code = 0; code < sizeof(ram_codes); ++code) {
                uint8_t *rom = make_rom(0x8000, (uint8_t)type, 0, ram_codes[code]);
                rom[0x143] = color ? 0x80 : 0;
                update_header_checksum(rom);
                assert(cupid_gb_load_rom(gb, rom, 0x8000));
                check_storage_window(gb, expected_header_storage(type, ram_codes[code]));
                free(rom);
            }
        }
        /* Oversized type-00 images select banking and may carry RAM. An
         * unsupported type still has no connected storage after fallback. */
        const uint8_t oversized_types[] = {0, 0x7f};
        for (unsigned variant = 0; variant < sizeof(oversized_types); ++variant) {
            uint8_t *rom = make_rom(0x10000, oversized_types[variant], 1, 2);
            rom[0x143] = color ? 0x80 : 0;
            update_header_checksum(rom);
            assert(cupid_gb_load_rom(gb, rom, 0x10000));
            assert(gb->header.mbc_type == (variant ? CUPID_GB_MBC_NONE : CUPID_GB_MBC3));
            check_storage_window(gb, variant ? 0 : 0x2000);
            free(rom);
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_mmm01_header_storage(void) {
    const uint8_t menu_types[] = {0x0b, 0x0c, 0x0d, 0x11};
    const uint8_t game_types[] = {0, 0x03, 0x0f, 0x1b, 0x1e};
    const size_t sizes[] = {0x10000, 0xc001};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned color = 0; color < 2; ++color) {
        cupid_gb_set_model(gb, color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
        for (unsigned size = 0; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
            for (unsigned layout = 0; layout < 2; ++layout) {
                for (unsigned menu = 0; menu < sizeof(menu_types); ++menu) {
                    if (layout && menu_types[menu] == 0x11)
                        continue;
                    for (unsigned game = 0; game < sizeof(game_types); ++game) {
                        for (unsigned menu_ram = 0; menu_ram < 4; ++menu_ram) {
                            for (unsigned game_ram = 0; game_ram < 4; ++game_ram) {
                                uint8_t *rom = make_rom(sizes[size], 0, 1, 0);
                                for (unsigned part = 0; part < 2; ++part) {
                                    uint8_t *header = rom + part * 0x8000;
                                    const bool is_menu = part != layout;
                                    for (unsigned i = 0; i < 48; ++i)
                                        header[0x104 + i] = (uint8_t)(i + 1);
                                    header[0x143] = color ? 0x80 : 0;
                                    header[0x147] = is_menu ? menu_types[menu] : game_types[game];
                                    header[0x149] = (uint8_t)(is_menu ? menu_ram : game_ram);
                                    update_header_checksum(header);
                                }
                                assert(cupid_gb_load_rom(gb, rom, sizes[size]));
                                assert(gb->header.mbc_type == CUPID_GB_MMM01);
                                assert(gb->mmm01_menu_first == (layout != 0));
                                assert(gb->header.has_battery == (menu_types[menu] == 0x0d));
                                assert(!gb->header.has_timer && !gb->header.has_rumble);
                                assert(gb->rom_size == sizes[size]);
                                assert(!memcmp(gb->rom, rom, sizes[size]));
                                const bool has_ram = menu_types[menu] == 0x0c ||
                                                     menu_types[menu] == 0x0d || game_ram != 0;
                                check_storage_window(
                                    gb, has_ram ? expected_header_storage(0x0c, menu_ram) : 0);
                                free(rom);
                            }
                        }
                    }
                }
            }
        }
        /* A truncated menu header uses erased bytes after the file's end.
         * Its type is present, but the absent size byte cannot allocate RAM. */
        uint8_t *rom = make_rom(0x10000, 0x1b, 1, 2);
        memcpy(rom + 0x8104, rom + 0x104, 0x30);
        rom[0x8147] = 0x0d;
        assert(cupid_gb_load_rom(gb, rom, 0x8148));
        assert(gb->header.mbc_type == CUPID_GB_MMM01 && gb->header.has_battery);
        assert(gb->cartridge_ram_size == 0 && gb->ram_bank_count == 0);
        /* A menu-like header at file-size minus 32 KiB is not at the chip's
         * tail when the file is partially populated. It must not select MMM01. */
        rom[0x8147] = 0;
        memcpy(rom + 0x4105, rom + 0x104, 0x30);
        rom[0x4148] = 0x0d;
        assert(cupid_gb_load_rom(gb, rom, 0xc001));
        assert(gb->header.mbc_type == CUPID_GB_MBC5);
        assert(gb->cartridge_ram_size == 0x2000);
        free(rom);
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_address_line_aliases(CupidGbModel model) {
    const uint8_t types[] = {0x08, 0x09, 0x03, 0x06, 0x10, 0x13, 0x1b, 0x1e,
                             0x20, 0x22, 0xfc, 0xfe, 0xff, 0x0d, 0xbc};
    const uint8_t ram_codes[] = {0, 1, 2, 3, 4, 5};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_effective_rom_bank(gb, false) == 0);
    assert(cupid_gb_effective_ram_bank(gb) == 0);
    for (unsigned bits = 1; bits <= 9; ++bits) {
        const size_t banks = (size_t)1u << bits;
        const size_t populated = banks * 0x4000 - 0x123;
        for (unsigned type = 0; type < sizeof(types); ++type) {
            uint8_t *rom = make_model_rom(model, populated, types[type], 0, ram_codes[bits % 6]);
            if (types[type] == 0xbc) {
                rom[0x149] = 0xc1;
                rom[0x14a] = 0x65;
                rom[0x152] = (uint8_t)(bits % 6);
            }
            update_header_checksum(rom);
            assert(cupid_gb_load_rom(gb, rom, populated));
            assert(gb->rom_bank_count == banks);
            assert(!gb->ram_bank_count || !(gb->ram_bank_count & (gb->ram_bank_count - 1)));
            for (unsigned selection = 0; selection < 512; ++selection) {
                cupid_gb_write_u8(gb, 0x0000, (uint8_t)(selection | 0x0a));
                cupid_gb_write_u8(gb, 0x2000, (uint8_t)selection);
                cupid_gb_write_u8(gb, 0x2100, (uint8_t)selection);
                cupid_gb_write_u8(gb, 0x3000, (uint8_t)(selection >> 8));
                cupid_gb_write_u8(gb, 0x4000, (uint8_t)(selection >> 1));
                cupid_gb_write_u8(gb, 0x6000, (uint8_t)(selection >> 2));
                if (types[type] == 0xbc) {
                    cupid_gb_write_u8(gb, 1, (uint8_t)selection);
                    cupid_gb_write_u8(gb, 2, (uint8_t)(selection >> 1));
                }
                for (unsigned lower = 0; lower < 2; ++lower) {
                    if (!lower && types[type] == 0x20) {
                        const unsigned offsets[] = {0x31f, 0x1ffe};
                        for (unsigned window = 0; window < 2; ++window) {
                            const unsigned bank = (selection >> (8u * window)) & 255u;
                            const size_t offset = (bank % (banks * 2u)) * 0x2000u + offsets[window];
                            const uint8_t expected = offset < populated ? rom[offset] : 0xff;
                            const uint16_t address =
                                (uint16_t)(0x4000u + window * 0x2000u + offsets[window]);
                            assert(cupid_gb_read_u8(gb, address) == expected);
                        }
                        continue;
                    }
                    size_t selected = cupid_gb_selected_rom_bank(gb, lower != 0);
                    if (gb->header.mbc_type == CUPID_GB_MMM01 && gb->mmm01_menu_first)
                        selected += 2;
                    const size_t physical = selected % banks;
                    assert(cupid_gb_effective_rom_bank(gb, lower != 0) == physical);
                    const unsigned offsets[] = {0x31f, 0x3ffe};
                    for (unsigned n = 0; n < sizeof(offsets) / sizeof(offsets[0]); ++n) {
                        const size_t offset = physical * 0x4000 + offsets[n];
                        const uint8_t expected = offset < populated ? rom[offset] : 0xff;
                        const uint16_t address = (uint16_t)((lower ? 0 : 0x4000) + offsets[n]);
                        assert(cupid_gb_read_u8(gb, address) == expected);
                    }
                }
                size_t selected = cupid_gb_selected_ram_bank(gb);
                if (gb->header.mbc_type == CUPID_GB_MBC3 && !cupid_gb_has_extended_mbc3(gb))
                    selected &= 3;
                const size_t physical = gb->ram_bank_count ? selected % gb->ram_bank_count : 0;
                assert(cupid_gb_effective_ram_bank(gb) == physical);
            }
            free(rom);
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void assert_fresh_storage(const CupidGb *gb, size_t expected_size) {
    assert(gb->cartridge_ram_size == expected_size && gb->battery_write_generation == 0);
    for (size_t offset = 0; offset < expected_size; ++offset)
        assert(cupid_gb_cartridge_ram_const(gb)[offset] == 0xff);
    if (expected_size < CUPID_GB_INLINE_RAM_SIZE)
        assert(gb->cartridge_ram[expected_size] == 0);
}

static void test_fresh_storage_and_restore(CupidGbModel model) {
    const struct {
        uint8_t type, ram_code;
        size_t size;
    } variants[] = {{0x00, 0, 0},      {0x08, 1, 0x800},    {0x09, 4, 0x20000}, {0x02, 2, 0x2000},
                    {0x03, 3, 0x8000}, {0x05, 0, 512},      {0x06, 4, 512},     {0x0c, 2, 0x2000},
                    {0x0d, 3, 0x8000}, {0x10, 2, 0x2000},   {0x12, 2, 0x2000},  {0x13, 3, 0x8000},
                    {0x1a, 2, 0x2000}, {0x1b, 4, 0x20000},  {0x1d, 2, 0x2000},  {0x1e, 3, 0x8000},
                    {0x20, 2, 0x8000}, {0x22, 4, 256},      {0xfc, 4, 0x20000}, {0xfe, 2, 0x2000},
                    {0xff, 2, 0x2000}, {0xbc, 0xc1, 0x2000}};

    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    for (size_t variant = 0; variant < sizeof(variants) / sizeof(*variants); ++variant) {
        uint8_t *rom =
            make_model_rom(model, 0x8000, variants[variant].type, 0, variants[variant].ram_code);
        if (variants[variant].type == 0xbc) {
            rom[0x14a] = 0x65;
            rom[0x152] = 1;
            rom[0x153] = 8;
            uint8_t checksum = 0;
            for (unsigned i = 0x134; i <= 0x14c; ++i)
                checksum = (uint8_t)(checksum - rom[i] - 1);
            rom[0x14d] = checksum;
        }
        assert(cupid_gb_load_rom(gb, rom, 0x8000));
        assert_fresh_storage(gb, variants[variant].size);
        const size_t ram_size = gb->cartridge_ram_size;
        uint8_t *expected = malloc(ram_size ? ram_size : 1);
        assert(expected);
        for (size_t offset = 0; offset < ram_size; ++offset) {
            const uint8_t pattern = (uint8_t)(offset * 13u + 7u);
            expected[offset] = gb->header.mbc_type == CUPID_GB_MBC2 ? pattern & 15u : pattern;
        }
        memcpy(gb->cartridge_ram, expected, ram_size);
        const size_t state_size = cupid_gb_state_size(gb);
        uint8_t *state = malloc(state_size);
        assert(state && cupid_gb_save_state(gb, state, state_size, NULL) == CUPID_GB_STATE_OK);
        const size_t battery_size = cupid_gb_battery_size(gb);
        uint8_t *battery = malloc(battery_size ? battery_size : 1);
        assert(battery);
        if (battery_size)
            assert(cupid_gb_save_battery_at(gb, battery, battery_size, NULL, 1700000000) ==
                   CUPID_GB_BATTERY_OK);

        /* Frontend reset/reopen restores the battery after loading the ROM.
         * The cold fill must leave that explicit persistent image intact. */
        assert(cupid_gb_load_rom(gb, rom, 0x8000));
        assert_fresh_storage(gb, ram_size);
        if (battery_size) {
            assert(cupid_gb_load_battery_at(gb, battery, battery_size, 1700000000) ==
                   CUPID_GB_BATTERY_OK);
            assert(memcmp(gb->cartridge_ram, expected, ram_size) == 0);
        }
        assert(cupid_gb_load_rom(gb, rom, 0x8000));
        assert_fresh_storage(gb, ram_size);
        assert(cupid_gb_load_state(gb, state, state_size) == CUPID_GB_STATE_OK);
        assert(memcmp(gb->cartridge_ram, expected, ram_size) == 0);
        gb->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100;
        cupid_gb_enter_boot_rom(gb);
        assert(memcmp(gb->cartridge_ram, expected, ram_size) == 0);
        free(battery);
        free(state);
        free(expected);
        free(rom);
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_physical_rom_address_lines(CupidGbModel model) {
    CupidGb gb = {0};
    cupid_gb_set_model(&gb, model);
    /* File population determines connected address lines; the size byte is
     * metadata. Empty tails of a partially populated chip read as erased ROM. */
    const size_t populated = 0xc001;
    uint8_t *rom = make_model_rom(model, populated, 0x19, 0, 0);
    assert(cupid_gb_load_rom(&gb, rom, populated));
    assert(gb.rom_bank_count == 4 && gb.rom_size == populated);
    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x2000, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0x4000) == (bank & 3));
        assert(cupid_gb_read_u8(&gb, 0x4001) == ((bank & 3) == 3 ? 0xff : bank & 3));
    }
    free(rom);
    rom = make_model_rom(model, 0x8000, 0x19, 8, 0);
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    assert(gb.rom_bank_count == 2 && gb.header.rom_bank_count == 512);
    cupid_gb_write_u8(&gb, 0x2000, 31);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
    /* Unknown size metadata must not hide valid, populated ROM bytes. */
    rom[0x148] = 0xff;
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    assert(gb.rom_bank_count == 2 && gb.header.rom_bank_count == 0);
    free(rom);
    cupid_gb_cleanup(&gb);
}

static uint8_t *make_extended_rom(size_t bytes) {
    uint8_t *rom = make_rom(bytes, 0xbcu, 8u, 0xc1u);
    rom[0x143u] = 0x80u;
    rom[0x14au] = 0x65u;
    rom[0x152u] = 0;
    rom[0x153u] = 1;
    update_header_checksum(rom);
    return rom;
}

static void select_tpp1_rom_bank(CupidGb *gb, uint16_t bank) {
    cupid_gb_write_u8(gb, 0x2000u, (uint8_t)bank);
    cupid_gb_write_u8(gb, 0x2001u, (uint8_t)(bank >> 8u));
    assert(gb->tpp1.rom_bank == bank && gb->current_rom_bank == bank);
}

static unsigned failed_rom_allocations;

static void *fail_rom_allocation(size_t bytes) {
    (void)bytes;
    ++failed_rom_allocations;
    return NULL;
}

static void test_extended_rom_capacity(CupidGbModel model) {
    enum {
        FIRST_EXTENDED_BYTE = 0x800000u,
        LARGER_ROM_SIZE = 0x1000000u,
        LAST_ROM_BANK = 0xffc000u
    };

    assert(CUPID_GB_MAX_ROM_SIZE / 0x4000u == 65536u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_init(gb);
    assert(gb->rom && gb->rom_capacity == CUPID_GB_MIN_ROM_CAPACITY && !gb->rom_size);
    const size_t length = FIRST_EXTENDED_BYTE + 1u;
    uint8_t *rom = make_extended_rom(length);
    rom[0x7fc000u] = 0x5cu;
    rom[FIRST_EXTENDED_BYTE] = 0x7bu;
    assert(cupid_gb_load_rom(gb, rom, length));
    assert(gb->rom_size == length && gb->rom_capacity == length && gb->rom_bank_count == 1024u &&
           gb->header.mbc_type == CUPID_GB_TPP1);
    assert(!memcmp(gb->rom, rom, length));
    select_tpp1_rom_bank(gb, 511u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == 0x5cu);
    select_tpp1_rom_bank(gb, 512u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == 0x7bu);
    assert(cupid_gb_read_u8(gb, 0x4001u) == 0xffu);
    select_tpp1_rom_bank(gb, 1023u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == 0xffu);
    select_tpp1_rom_bank(gb, 1024u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == rom[0]);
    select_tpp1_rom_bank(gb, 0x8001u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == rom[0x4000u]);

    select_tpp1_rom_bank(gb, 512u);
    const size_t state_size = cupid_gb_state_size(gb);
    uint8_t *state = malloc(state_size);
    assert(state && cupid_gb_save_state(gb, state, state_size, NULL) == CUPID_GB_STATE_OK);
    uint8_t *const retained = gb->rom;
    const size_t retained_capacity = gb->rom_capacity;
    const uint64_t retained_generation = gb->rom_generation;
    assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
    assert(gb->rom == retained && gb->rom_capacity == retained_capacity &&
           gb->rom_generation == retained_generation && gb->rom_size == length);
    assert(cupid_gb_reset(gb, CUPID_GB_RESET_HARD));
    assert(gb->rom == retained && gb->rom_capacity == retained_capacity &&
           gb->rom_generation == retained_generation && gb->rom_size == length);
    assert(cupid_gb_load_state(gb, state, state_size) == CUPID_GB_STATE_OK);
    assert(gb->rom == retained && gb->rom_capacity == retained_capacity &&
           gb->rom_generation == retained_generation && gb->current_rom_bank == 512u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == 0x7bu);
    free(state);

    /* The host-only cartridge generation binds cached state contexts across
     * ordinary reset and state import, without entering the native wire bytes. */
    CupidGbStateContext reset_context = {0};
    assert(cupid_gb_state_context_init(&reset_context, gb) == CUPID_GB_STATE_OK);
    assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
    state = malloc(cupid_gb_state_size(gb));
    assert(state);
    assert(cupid_gb_save_state_with_context(gb, state, cupid_gb_state_size(gb), NULL,
                                            &reset_context) == CUPID_GB_STATE_OK);
    free(state);

    CupidGbStateContext old_context = {0};
    assert(cupid_gb_state_context_init(&old_context, gb) == CUPID_GB_STATE_OK);
    assert(state_size == cupid_gb_state_size(gb));
    state = malloc(state_size);
    assert(state && cupid_gb_save_state(gb, state, state_size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_load_rom(gb, gb->rom, gb->rom_size));
    assert(gb->rom == retained && gb->rom_capacity == length &&
           gb->rom_generation == retained_generation + 1u);
    uint8_t *after = malloc(state_size);
    uint8_t *before = malloc(state_size);
    assert(after && before &&
           cupid_gb_save_state(gb, before, state_size, NULL) == CUPID_GB_STATE_OK);
    memset(after, 0xa5, state_size);
    size_t written = 17u;
    assert(cupid_gb_save_state_with_context(gb, after, state_size, &written, &old_context) ==
           CUPID_GB_STATE_WRONG_ROM);
    assert(written == 0);
    for (size_t offset = 0; offset < state_size; ++offset)
        assert(after[offset] == 0xa5u);
    assert(cupid_gb_load_state_with_context(gb, state, state_size, &old_context) ==
           CUPID_GB_STATE_WRONG_ROM);
    assert(cupid_gb_save_state(gb, after, state_size, NULL) == CUPID_GB_STATE_OK &&
           memcmp(before, after, state_size) == 0);
    assert(cupid_gb_load_state(gb, state, state_size) == CUPID_GB_STATE_OK &&
           gb->rom_generation == retained_generation + 1u);
    assert(cupid_gb_save_state(gb, after, state_size, NULL) == CUPID_GB_STATE_OK &&
           memcmp(state, after, state_size) == 0);
    CupidGbStateContext fresh_context = {0};
    assert(cupid_gb_state_context_init(&fresh_context, gb) == CUPID_GB_STATE_OK);
    assert(cupid_gb_save_state_with_context(gb, after, state_size, NULL, &fresh_context) ==
           CUPID_GB_STATE_OK);
    free(before);
    free(after);
    free(state);

    /* Failed validation or a failed capacity growth cannot discard an active
     * cartridge, its RAM, its CPU history, or its owned ROM bytes. */
    CupidGb *snapshot = malloc(sizeof(*snapshot));
    uint8_t *saved_bytes = malloc(gb->rom_size);
    assert(snapshot && saved_bytes);
    memcpy(snapshot, gb, sizeof(*gb));
    memcpy(saved_bytes, gb->rom, gb->rom_size);
    assert(!cupid_gb_load_rom(gb, rom, (size_t)CUPID_GB_MAX_ROM_SIZE + 1u));
    assert(!cupid_gb_load_rom(gb, gb->rom + gb->rom_capacity - 8u, 0x150u));
    assert(!memcmp(snapshot, gb, sizeof(*gb)) && !memcmp(saved_bytes, gb->rom, gb->rom_size));

    CupidGbStateContext still_valid = {0};
    assert(cupid_gb_state_context_init(&still_valid, gb) == CUPID_GB_STATE_OK);
    uint8_t *large = make_extended_rom(LARGER_ROM_SIZE);
    large[LAST_ROM_BANK] = 0xa6u;
    failed_rom_allocations = 0;
    assert(!cupid_gb_load_rom_with_allocator(gb, large, LARGER_ROM_SIZE, fail_rom_allocation));
    assert(failed_rom_allocations == 1u && !memcmp(snapshot, gb, sizeof(*gb)) &&
           !memcmp(saved_bytes, gb->rom, gb->rom_size));
    state = malloc(cupid_gb_state_size(gb));
    assert(state);
    assert(cupid_gb_save_state_with_context(gb, state, cupid_gb_state_size(gb), NULL,
                                            &still_valid) == CUPID_GB_STATE_OK);
    free(state);
    free(saved_bytes);
    free(snapshot);

    assert(cupid_gb_load_rom(gb, large, LARGER_ROM_SIZE));
    assert(gb->rom_capacity == LARGER_ROM_SIZE && gb->rom_bank_count == 1024u);
    select_tpp1_rom_bank(gb, 1023u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == 0xa6u);
    select_tpp1_rom_bank(gb, 0xffffu);
    assert(cupid_gb_read_u8(gb, 0x4000u) == 0xa6u);
    select_tpp1_rom_bank(gb, 1024u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == large[0]);
    free(large);

    /* Controllers with narrower bank latches accept the same physical input,
     * but still retain their existing hardware address-line selection widths. */
    rom[0x147u] = 0x19u;
    rom[0x149u] = 0;
    rom[0x14au] = 0;
    update_header_checksum(rom);
    assert(cupid_gb_load_rom(gb, rom, length));
    assert(gb->header.mbc_type == CUPID_GB_MBC5 && gb->rom_capacity == length);
    cupid_gb_write_u8(gb, 0x2000u, 0xffu);
    cupid_gb_write_u8(gb, 0x3000u, 1u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == 0x5cu);
    free(rom);

    /* Shrinking from the current ROM bytes reallocates the destination.
     * A direct copy from the old pointer must finish before that pointer is freed. */
    uint8_t *original_prefix = malloc(CUPID_GB_MIN_ROM_CAPACITY);
    assert(original_prefix);
    memcpy(original_prefix, gb->rom, CUPID_GB_MIN_ROM_CAPACITY);
    uint8_t *const larger_storage = gb->rom;
    assert(cupid_gb_load_rom(gb, gb->rom, CUPID_GB_MIN_ROM_CAPACITY));
    assert(gb->rom != larger_storage && gb->rom_capacity == CUPID_GB_MIN_ROM_CAPACITY &&
           gb->rom_size == CUPID_GB_MIN_ROM_CAPACITY &&
           memcmp(gb->rom, original_prefix, CUPID_GB_MIN_ROM_CAPACITY) == 0);
    free(original_prefix);

    uint8_t *small = make_rom(0x8000u, 0, 0, 0);
    assert(cupid_gb_load_rom(gb, small, 0x8000u));
    assert(gb->rom_size == 0x8000u && gb->rom_capacity == CUPID_GB_MIN_ROM_CAPACITY);
    free(small);
    uint8_t *inside = make_rom(0x7f00u, 0, 0, 0);
    uint8_t *const small_storage = gb->rom;
    memcpy(gb->rom + 0x100u, inside, 0x7f00u);
    assert(cupid_gb_load_rom(gb, gb->rom + 0x100u, 0x7f00u));
    assert(gb->rom == small_storage && gb->rom_capacity == CUPID_GB_MIN_ROM_CAPACITY &&
           gb->rom_size == 0x7f00u && !memcmp(gb->rom, inside, 0x7f00u));
    free(inside);
    cupid_gb_cleanup(gb);
    assert(!gb->rom && !gb->rom_capacity);
    free(gb);
}

static unsigned storage_allocation_calls, storage_fail_at;

static void *storage_allocator(size_t bytes) {
    ++storage_allocation_calls;
    return storage_allocation_calls == storage_fail_at ? NULL : malloc(bytes);
}

typedef struct StorageSnapshot {
    CupidGb machine;
    uint8_t *rom_bytes;
    uint8_t *ram_bytes;
} StorageSnapshot;

static StorageSnapshot *storage_snapshot(const CupidGb *gb) {
    StorageSnapshot *snapshot = malloc(sizeof(*snapshot));
    assert(snapshot);
    snapshot->machine = *gb;
    snapshot->rom_bytes = malloc(gb->rom_capacity);
    snapshot->ram_bytes = gb->cartridge_ram_heap ? malloc(gb->cartridge_ram_capacity) : NULL;
    assert(snapshot->rom_bytes && (!gb->cartridge_ram_heap || snapshot->ram_bytes));
    memcpy(snapshot->rom_bytes, gb->rom, gb->rom_capacity);
    if (snapshot->ram_bytes)
        memcpy(snapshot->ram_bytes, gb->cartridge_ram_heap, gb->cartridge_ram_capacity);
    return snapshot;
}

static void assert_storage_unchanged(const StorageSnapshot *snapshot, const CupidGb *gb) {
    assert(!memcmp(&snapshot->machine, gb, sizeof(*gb)));
    assert(!memcmp(snapshot->rom_bytes, gb->rom, gb->rom_capacity));
    if (snapshot->ram_bytes)
        assert(!memcmp(snapshot->ram_bytes, gb->cartridge_ram_heap, gb->cartridge_ram_capacity));
}

static void destroy_storage_snapshot(StorageSnapshot *snapshot) {
    /* The structure copy borrows the live owner's allocations. */
    free(snapshot->rom_bytes);
    free(snapshot->ram_bytes);
    free(snapshot);
}

static uint8_t *make_tpp1_storage_rom(CupidGbModel model, size_t length, uint8_t code) {
    uint8_t *rom = make_model_rom(model, length, 0xbc, length == 0x8000 ? 0 : 1, 0xc1);
    rom[0x14a] = 0x65;
    rom[0x150] = 0;
    rom[0x151] = 0;
    rom[0x152] = code;
    rom[0x153] = 8;
    update_header_checksum(rom);
    return rom;
}

static void assert_failed_storage_load(CupidGb *gb, const uint8_t *rom, size_t length,
                                       unsigned fail_at) {
    StorageSnapshot *snapshot = storage_snapshot(gb);
    CupidGbStateContext context = {0};
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    const CupidGbStateContext saved_context = context;
    const size_t state_size = cupid_gb_state_size(gb);
    uint8_t *before = malloc(state_size);
    uint8_t *after = malloc(state_size);
    assert(before && after);
    assert(cupid_gb_save_state_with_context(gb, before, state_size, NULL, &context) ==
           CUPID_GB_STATE_OK);
    storage_allocation_calls = 0;
    storage_fail_at = fail_at;
    assert(!cupid_gb_load_rom_with_allocator(gb, rom, length, storage_allocator));
    assert(storage_allocation_calls == fail_at);
    assert_storage_unchanged(snapshot, gb);
    assert(!memcmp(&context, &saved_context, sizeof(context)));
    assert(cupid_gb_save_state_with_context(gb, after, state_size, NULL, &context) ==
           CUPID_GB_STATE_OK);
    assert(!memcmp(before, after, state_size));
    free(before);
    free(after);
    destroy_storage_snapshot(snapshot);
}

static void test_extended_ram_ownership(CupidGbModel model) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    CupidGb *other = calloc(1, sizeof(*other));
    assert(gb && other && sizeof(gb->cartridge_ram) == CUPID_GB_INLINE_RAM_SIZE);
    assert(!cupid_gb_cartridge_ram(NULL) && !cupid_gb_cartridge_ram_const(NULL));
    assert(!cupid_gb_cartridge_storage_valid(NULL));
    cupid_gb_set_model(gb, model);
    cupid_gb_set_model(other, model);
    cupid_gb_set_power_on_seed(gb, 0);
    cupid_gb_set_power_on_seed(other, 0);
    uint8_t *small = make_tpp1_storage_rom(model, 0x8000, 5);
    uint8_t *large = make_tpp1_storage_rom(model, 0x8000, 9);
    uint8_t *replacement = make_tpp1_storage_rom(model, 0x10000, 8);
    assert(cupid_gb_load_rom(gb, small, 0x8000));
    memset(gb->cartridge_ram, 0x39, sizeof(gb->cartridge_ram));
    assert(cupid_gb_cartridge_storage_valid(gb));

    /* First ROM allocation and second SRAM allocation are separate failure gates.
     * The same-size ROM case must also remain untouched when only SRAM fails. */
    assert_failed_storage_load(gb, replacement, 0x10000, 1);
    assert_failed_storage_load(gb, replacement, 0x10000, 2);
    assert_failed_storage_load(gb, large, 0x8000, 1);
    uint8_t *const old_rom = gb->rom;
    const uint64_t generation = gb->rom_generation;
    assert(cupid_gb_load_rom(gb, large, 0x8000));
    assert(gb->rom == old_rom && gb->rom_generation == generation + 1);
    assert(gb->cartridge_ram_heap && gb->cartridge_ram_capacity == CUPID_GB_MAX_RAM_SIZE);
    assert(cupid_gb_cartridge_storage_valid(gb));
    assert_fresh_storage(gb, CUPID_GB_MAX_RAM_SIZE);
    for (size_t offset = 0; offset < gb->cartridge_ram_size; ++offset)
        gb->cartridge_ram_heap[offset] = (uint8_t)(offset * 29u + (offset >> 13u) * 73u);

    /* A deep snapshot must catch writes to old heap bytes after either failure. */
    assert_failed_storage_load(gb, replacement, 0x10000, 1);
    assert_failed_storage_load(gb, replacement, 0x10000, 2);
    StorageSnapshot *snapshot = storage_snapshot(gb);
    assert(!cupid_gb_load_rom(gb, gb->cartridge_ram_heap + gb->cartridge_ram_capacity - 8, 0x150));
    assert(!cupid_gb_load_rom(gb, gb->cartridge_ram + sizeof(gb->cartridge_ram) - 8, 0x150));
    assert_storage_unchanged(snapshot, gb);
    destroy_storage_snapshot(snapshot);

    const size_t capacity = gb->cartridge_ram_capacity;
    gb->cartridge_ram_capacity = capacity - 1;
    assert(!cupid_gb_cartridge_storage_valid(gb));
    gb->cartridge_ram_capacity = capacity;
    gb->header.mbc_type = CUPID_GB_MBC5;
    assert(!cupid_gb_cartridge_storage_valid(gb));
    gb->header.mbc_type = CUPID_GB_TPP1;

    /* Reuse both allocations while the complete input lives in the SRAM that
     * initialization is about to erase. The source must be copied first. */
    uint8_t *const reused_heap = gb->cartridge_ram_heap;
    memcpy(reused_heap + 0x20000, large, 0x8000);
    storage_allocation_calls = 0;
    storage_fail_at = 1;
    assert(cupid_gb_load_rom_with_allocator(gb, reused_heap + 0x20000, 0x8000, storage_allocator));
    assert(!storage_allocation_calls && gb->cartridge_ram_heap == reused_heap);
    assert(!memcmp(gb->rom, large, 0x8000));
    assert_fresh_storage(gb, CUPID_GB_MAX_RAM_SIZE);

    assert(cupid_gb_load_rom(other, large, 0x8000));
    assert(other->rom != gb->rom && other->cartridge_ram_heap != gb->cartridge_ram_heap);
    cupid_gb_write_u8(gb, 0x7fff, 3);
    cupid_gb_write_u8(gb, 0x0002, 255);
    cupid_gb_write_u8(gb, 0xbfff, 0x58);
    assert(gb->cartridge_ram_heap[CUPID_GB_MAX_RAM_SIZE - 1] == 0x58);
    assert(other->cartridge_ram_heap[CUPID_GB_MAX_RAM_SIZE - 1] == 0xff);

    /* Shrinking releases the heap only after a source inside its last bank has
     * reached ROM. Growing from the inline SRAM has the same source lifetime. */
    memcpy(gb->cartridge_ram_heap + capacity - 0x8000, small, 0x8000);
    assert(cupid_gb_load_rom(gb, gb->cartridge_ram_heap + capacity - 0x8000, 0x8000));
    assert(!gb->cartridge_ram_heap && !gb->cartridge_ram_capacity);
    assert(!memcmp(gb->rom, small, 0x8000));
    assert_fresh_storage(gb, CUPID_GB_INLINE_RAM_SIZE);
    large[0x152] = 6;
    memcpy(gb->cartridge_ram + 0x10000, large, 0x8000);
    assert(cupid_gb_load_rom(gb, gb->cartridge_ram + 0x10000, 0x8000));
    assert(!memcmp(gb->rom, large, 0x8000));
    assert_fresh_storage(gb, 0x40000);

    /* Init resets guest bytes but keeps host ownership, even without a loaded
     * cartridge. A later cleanup must release both retained buffers exactly once. */
    uint8_t *const retained_heap = gb->cartridge_ram_heap;
    const size_t retained_capacity = gb->cartridge_ram_capacity;
    memset(retained_heap, 0x7b, retained_capacity);
    cupid_gb_init(gb);
    assert(gb->cartridge_ram_heap == retained_heap &&
           gb->cartridge_ram_capacity == retained_capacity && !gb->loaded);
    for (size_t offset = 0; offset < retained_capacity; ++offset)
        assert(retained_heap[offset] == 0);
    assert(cupid_gb_load_rom(gb, replacement, 0x10000));
    assert_fresh_storage(gb, 0x100000);
    assert(cupid_gb_cartridge_storage_valid(gb));
    cupid_gb_cleanup(gb);
    cupid_gb_cleanup(gb);
    assert(!gb->rom && !gb->rom_capacity && !gb->cartridge_ram_heap && !gb->cartridge_ram_capacity);
    cupid_gb_cleanup(other);
    free(other);
    free(gb);
    free(replacement);
    free(large);
    free(small);
}

static void test_rom_capacity_output_overlap(void) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    uint8_t *rom = make_rom(0x8000u, 0u, 0u, 0u);
    assert(cupid_gb_load_rom(gb, rom, 0x8000u));
    const size_t size = cupid_gb_state_size(gb);
    assert(size > 0);
    const size_t enlarged_capacity = gb->rom_size + size + sizeof(size_t);
    uint8_t *const larger = malloc(enlarged_capacity);
    assert(larger);
    memcpy(larger, gb->rom, gb->rom_size);
    free(gb->rom);
    gb->rom = larger;
    gb->rom_capacity = enlarged_capacity;
    size_t written = 19;
    assert(cupid_gb_save_state(gb, gb->rom + gb->rom_size, size, &written) ==
           CUPID_GB_STATE_INVALID);
    assert(written == 0 && memcmp(gb->rom, rom, gb->rom_size) == 0);
    uint8_t *state = malloc(size);
    assert(state && cupid_gb_save_state(gb, state, size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_load_state(gb, state, size) == CUPID_GB_STATE_OK);
    free(state);
    cupid_gb_cleanup(gb);
    free(gb);
    free(rom);
}

static void test_extended_rom_file(void) {
    const size_t length = 0x800000u + 1u;
    uint8_t *rom = make_extended_rom(length);
    rom[0x800000u] = 0xb7u;
    char path[112] = {0};
    struct timespec timestamp = {0};
    assert(timespec_get(&timestamp, TIME_UTC) == TIME_UTC);
    FILE *file = NULL;
    for (unsigned attempt = 0; attempt < 64u && !file; ++attempt) {
        snprintf(path, sizeof(path), "cupidgb-rom-cap-%lld-%lu-%u.gbc", (long long)timestamp.tv_sec,
                 (unsigned long)timestamp.tv_nsec, attempt);
        file = fopen(path, "wbx");
        assert(file || errno == EEXIST);
    }
    assert(file);
    assert(fwrite(rom, 1u, length, file) == length && fclose(file) == 0);

    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, CUPID_GB_MODEL_CGB);
    assert(cupid_gb_load_rom_file(gb, path));
    assert(gb->rom_size == length && gb->rom_capacity == length && gb->rom_bank_count == 1024u &&
           !memcmp(gb->rom, rom, length));
    select_tpp1_rom_bank(gb, 512u);
    assert(cupid_gb_read_u8(gb, 0x4000u) == 0xb7u && cupid_gb_read_u8(gb, 0x4001u) == 0xffu);
    assert(remove(path) == 0);
    cupid_gb_cleanup(gb);
    free(gb);
    free(rom);
}

static void test_reject_advance_cartridge(void) {
    CupidGb gb = {0};
    uint8_t *rom = make_rom(0x8000, 0, 0, 0);
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    const uint8_t logo_prefix[] = {0x24, 0xff, 0xae, 0x51};
    memcpy(rom + 4, logo_prefix, sizeof(logo_prefix));
    rom[0xb2] = 0x96;
    rom[0xbd] = 0x51;
    assert(!cupid_gb_load_rom(&gb, rom, 0x8000));
    assert(gb.loaded && cupid_gb_read_u8(&gb, 4) == 0);
    cupid_gb_cleanup(&gb);
    free(rom);
}

static void test_multicart_wiring(CupidGbModel model) {
    CupidGb gb = {0};
    cupid_gb_set_model(&gb, model);
    const size_t size = 0x80000;
    uint8_t *rom = make_model_rom(model, size, 3, 4, 3);
    /* A second identical logo region identifies 16-bank subcartridges. Two
     * populated headers are enough; checksums of other entries are unrelated. */
    memcpy(rom + 0x40104, rom + 0x104, 48);
    assert(cupid_gb_load_rom(&gb, rom, size));
    assert(gb.mbc1_multicart);
    cupid_gb_write_u8(&gb, 0, 0x0a);
    cupid_gb_write_u8(&gb, 0xa000, 0x5a);
    for (unsigned mode = 0; mode < 2; ++mode) {
        cupid_gb_write_u8(&gb, 0x6000, (uint8_t)mode);
        for (unsigned high = 0; high < 4; ++high) {
            cupid_gb_write_u8(&gb, 0x4000, (uint8_t)high);
            assert(cupid_gb_read_u8(&gb, 0xa000) == 0x5a);
            for (unsigned low = 0; low < 32; ++low) {
                cupid_gb_write_u8(&gb, 0x2000, (uint8_t)low);
                const unsigned upper = ((high << 4) | (low ? low & 15 : 1)) & 31;
                assert(cupid_gb_read_u8(&gb, 0x4000) == upper);
                assert(cupid_gb_read_u8(&gb, 0) == (mode ? (high << 4) & 31 : 0));
            }
        }
    }
    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_small_ram_mirrors(CupidGbModel model) {
    const uint8_t types[] = {0x09, 0x03, 0x13, 0x1b};
    for (unsigned controller = 0; controller < sizeof(types); ++controller) {
        CupidGb gb = {0};
        cupid_gb_set_model(&gb, model);
        uint8_t *rom = make_model_rom(model, 0x8000, types[controller], 0, 1);
        assert(cupid_gb_load_rom(&gb, rom, 0x8000));
        assert(gb.cartridge_ram_size == 0x800);
        cupid_gb_write_u8(&gb, 0, 0x0a);
        const uint64_t generation = gb.battery_write_generation;
        cupid_gb_write_u8(&gb, 0xa123, 0x6b);
        for (unsigned mirror = 0; mirror < 4; ++mirror) {
            assert(cupid_gb_read_u8(&gb, (uint16_t)(0xa123 + mirror * 0x800)) == 0x6b);
            cupid_gb_write_u8(&gb, (uint16_t)(0xa123 + mirror * 0x800), 0x6b);
        }
        assert(gb.battery_write_generation == generation + 1);
        cupid_gb_write_u8(&gb, 0xb923, 0x71);
        assert(cupid_gb_read_u8(&gb, 0xa123) == 0x71);
        assert(gb.battery_write_generation == generation + 2);
        free(rom);
        cupid_gb_cleanup(&gb);
    }
}

static void test_extended_mbc3(CupidGbModel model) {
    /* Either a larger ROM or additional RAM selects the wider controller. */
    const size_t sizes[] = {0x400000, 0x8000, 0x400000, 0x8000};
    const uint8_t rom_codes[] = {7, 0, 7, 0};
    const uint8_t ram_codes[] = {3, 5, 3, 5};
    const uint8_t types[] = {0x10, 0x10, 0x13, 0x13};
    for (unsigned variant = 0; variant < sizeof(types); ++variant) {
        CupidGb gb = {0};
        cupid_gb_set_model(&gb, model);
        uint8_t *rom = make_model_rom(model, sizes[variant], types[variant], rom_codes[variant],
                                      ram_codes[variant]);
        assert(cupid_gb_load_rom(&gb, rom, sizes[variant]));
        cupid_gb_write_u8(&gb, 0, 0x0a);
        for (unsigned bank = 0; bank < 256; ++bank) {
            cupid_gb_write_u8(&gb, 0x2000, (uint8_t)bank);
            assert(gb.current_rom_bank == (bank ? bank : 1));
            assert(cupid_gb_read_u8(&gb, 0x4000) == ((bank ? bank : 1) % gb.rom_bank_count));
        }
        for (unsigned bank = 0; bank < 8; ++bank) {
            cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
            cupid_gb_write_u8(&gb, 0xa000, (uint8_t)(0x60 + bank));
        }
        for (unsigned bank = 0; bank < 8; ++bank) {
            cupid_gb_write_u8(&gb, 0x4000, (uint8_t)(0x10 + bank));
            const unsigned physical = bank % gb.ram_bank_count;
            const unsigned expected = physical + (gb.ram_bank_count < 8 ? 4 : 0);
            assert(cupid_gb_read_u8(&gb, 0xa000) == 0x60 + expected);
        }
        gb.rtc.ls = 0x25;
        cupid_gb_write_u8(&gb, 0x4000, 0x18);
        assert(cupid_gb_read_u8(&gb, 0xa000) == (gb.header.has_timer     ? 0x25u
                                                 : gb.ram_bank_count < 8 ? 0x64u
                                                                         : 0x60u));
        /* Disk state restores the wider banks without extra host state. */
        cupid_gb_write_u8(&gb, 0x4000, 7);
        cupid_gb_write_u8(&gb, 0x2000, 0x81);
        const size_t length = cupid_gb_state_size(&gb);
        uint8_t *state = malloc(length);
        assert(state && cupid_gb_save_state(&gb, state, length, NULL) == CUPID_GB_STATE_OK);
        cupid_gb_write_u8(&gb, 0x4000, 0);
        cupid_gb_write_u8(&gb, 0x2000, 1);
        assert(cupid_gb_load_state(&gb, state, length) == CUPID_GB_STATE_OK);
        assert(gb.current_rom_bank == 0x81 && gb.current_ram_bank == 7);
        free(state);
        free(rom);
        cupid_gb_cleanup(&gb);
    }
}

static void test_ordinary_mbc3_ram_aliases(CupidGbModel model) {
    CupidGb gb = {0};
    cupid_gb_set_model(&gb, model);
    uint8_t *rom = make_model_rom(model, 0x8000, 0x13, 0, 3);
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    cupid_gb_write_u8(&gb, 0, 10);
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        cupid_gb_write_u8(&gb, 0xa000, (uint8_t)(0x40 + bank));
    }
    for (unsigned selection = 0; selection < 256; ++selection) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)selection);
        assert(gb.rtc_register == 0);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0x44u + (selection & 3u));
    }
    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_mbc5_ram_enable(CupidGbModel model) {
    CupidGb gb = {0};
    cupid_gb_set_model(&gb, model);
    uint8_t *rom = make_model_rom(model, 0x8000, 0x1b, 0, 2);
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    cupid_gb_write_u8(&gb, 0, 0x0a);
    cupid_gb_write_u8(&gb, 0xa000, 0x5a);
    for (unsigned value = 0; value < 256; ++value) {
        cupid_gb_write_u8(&gb, 0x1fff, (uint8_t)value);
        assert(gb.ram_enabled == (value == 0x0a));
        assert(cupid_gb_read_u8(&gb, 0xa000) == (value == 0x0a ? 0x5a : 0xff));
    }
    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_huc1_banks_and_ir(CupidGbModel model) {
    CupidGb gb = {0};
    cupid_gb_set_model(&gb, model);
    uint8_t *rom = make_model_rom(model, 0x100000, 0xff, 5, 5);
    for (unsigned region = 0; region < 8; ++region) {
        assert(cupid_gb_load_rom(&gb, rom, 0x100000));
        assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
        const size_t length = cupid_gb_state_size(&gb);
        uint8_t *state = malloc(length);
        assert(state && cupid_gb_save_state(&gb, state, length, NULL) == CUPID_GB_STATE_OK);
        const uint16_t address = (uint16_t)(region * 0x1000);
        cupid_gb_write_u8(&gb, address, 0);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 0);
        /* Restore the initial mapping and its still-unapplied raw register. */
        assert(cupid_gb_load_state(&gb, state, length) == CUPID_GB_STATE_OK);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
        cupid_gb_write_u8(&gb, address, 0);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 0);
        free(state);
    }
    assert(cupid_gb_load_rom(&gb, rom, 0x100000));
    /* HuC1 RAM does not require MBC1's enable sequence. */
    cupid_gb_write_u8(&gb, 0xa000, 0x42);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 0x42);
    for (unsigned bank = 0; bank < 64; ++bank) {
        cupid_gb_write_u8(&gb, 0x2000, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0x4000) == bank);
        assert(cupid_gb_read_u8(&gb, 0) == 0);
    }
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        cupid_gb_write_u8(&gb, 0xa000, (uint8_t)(0x50 + bank));
    }
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        cupid_gb_write_u8(&gb, 0x6000, 1);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0x50 + bank);
        cupid_gb_write_u8(&gb, 0, 0x1e);
        assert(gb.huc1_ir_mode && cupid_gb_read_u8(&gb, 0xa000) == 0xc0);
        cupid_gb_write_u8(&gb, 0xa000, 0x13);
        cupid_gb_write_u8(&gb, 0, 0);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0x50 + bank);
    }
    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_huc3_banks_and_sram_modes(CupidGbModel model) {
    CupidGb gb = {0};
    cupid_gb_set_model(&gb, model);
    const size_t rom_size = 0x400000;
    uint8_t *rom = make_model_rom(model, rom_size, 0xfe, 7, 4);
    assert(cupid_gb_load_rom(&gb, rom, rom_size));
    assert(gb.header.has_battery && cupid_gb_battery_size(&gb) == 0x20000 + 17);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
    const size_t state_size = cupid_gb_state_size(&gb);
    uint8_t *state = malloc(state_size);
    assert(state && cupid_gb_save_state(&gb, state, state_size, NULL) == CUPID_GB_STATE_OK);
    cupid_gb_write_u8(&gb, 0x7fff, 0);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 0);
    assert(cupid_gb_load_state(&gb, state, state_size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
    cupid_gb_write_u8(&gb, 0x1fff, 0x1a);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 0);
    const uint8_t banks[] = {0, 1, 31, 63, 64, 127, 128, 129, 255};
    for (unsigned n = 0; n < sizeof(banks); ++n) {
        cupid_gb_write_u8(&gb, 0x3fff, banks[n]);
        assert(cupid_gb_read_u8(&gb, 0x4000) == (banks[n] & 0x7f));
        assert(cupid_gb_read_u8(&gb, 0x0000) == 0);
    }

    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)(0x80u | bank));
        cupid_gb_write_u8(&gb, 0xa031, (uint8_t)(0x40u + bank));
    }
    const uint64_t generation = gb.battery_write_generation;
    cupid_gb_write_u8(&gb, 0x0000, 0x10);
    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x5fff, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0xa031) == 0x40u + bank);
        cupid_gb_write_u8(&gb, 0xa031, 0x99);
        assert(cupid_gb_read_u8(&gb, 0xa031) == 0x40u + bank);
    }
    assert(gb.battery_write_generation == generation);

    const size_t battery_size = cupid_gb_battery_size(&gb);
    uint8_t *battery = malloc(battery_size);
    assert(battery &&
           cupid_gb_save_battery(&gb, battery, battery_size, NULL) == CUPID_GB_BATTERY_OK);
    assert(cupid_gb_load_rom(&gb, rom, rom_size));
    assert(cupid_gb_load_battery(&gb, battery, battery_size) == CUPID_GB_BATTERY_OK);
    cupid_gb_write_u8(&gb, 0x4000, 0x1f);
    assert(cupid_gb_read_u8(&gb, 0xa031) == 0x4f);
    free(battery);
    free(state);
    free(rom);
    cupid_gb_cleanup(&gb);
}

typedef struct RumbleEvents {
    unsigned count;
    bool enabled;
    uint64_t cycles;
} RumbleEvents;

static void observe_rumble(const CupidGb *gb, bool enabled, uint64_t cycles, void *user_data) {
    RumbleEvents *events = user_data;
    assert(cupid_gb_rumble_enabled(gb) == enabled);
    ++events->count;
    events->enabled = enabled;
    events->cycles = cycles;
}

static void test_mbc5_motor_and_ram(CupidGbModel model) {
    CupidGb gb = {0};
    cupid_gb_set_model(&gb, model);
    RumbleEvents events = {0};
    cupid_gb_set_rumble_callback(&gb, observe_rumble, &events);
    uint8_t *rom = make_model_rom(model, 0x8000, 0x1e, 0, 4);
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    assert(!cupid_gb_rumble_enabled(&gb) && !events.count);
    cupid_gb_write_u8(&gb, 0, 0x0a);
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        cupid_gb_write_u8(&gb, 0xa000, (uint8_t)(bank + 0x40));
    }
    for (unsigned motor = 0; motor < 2; ++motor) {
        for (unsigned bank = 0; bank < 8; ++bank) {
            gb.elapsed_cycles = 100 + motor * 8 + bank;
            cupid_gb_write_u8(&gb, 0x5fff, (uint8_t)(0xf0 | motor * 8 | bank));
            assert(gb.current_ram_bank == bank && cupid_gb_rumble_enabled(&gb) == (motor != 0));
            assert(cupid_gb_read_u8(&gb, 0xa000) == 0x40 + bank);
        }
    }
    assert(events.count == 1 && events.enabled && events.cycles == 108);
    const size_t length = cupid_gb_state_size(&gb);
    uint8_t *state = malloc(length);
    assert(state && cupid_gb_save_state(&gb, state, length, NULL) == CUPID_GB_STATE_OK);
    cupid_gb_write_u8(&gb, 0x4000, 3);
    assert(events.count == 2 && !events.enabled && !cupid_gb_rumble_enabled(&gb));
    assert(cupid_gb_load_state(&gb, state, length) == CUPID_GB_STATE_OK);
    assert(events.count == 2 && cupid_gb_rumble_enabled(&gb) && gb.current_ram_bank == 7);
    cupid_gb_write_u8(&gb, 0x4000, 0);
    assert(events.count == 3 && !events.enabled);
    cupid_gb_write_u8(&gb, 0x4000, 0);
    assert(events.count == 3);
    cupid_gb_write_u8(&gb, 0, 0);
    cupid_gb_write_u8(&gb, 0x4000, 8);
    assert(events.count == 4 && events.enabled && cupid_gb_read_u8(&gb, 0xa000) == 0xff);
    rom[0x147] = 0x1b;
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    cupid_gb_write_u8(&gb, 0x4000, 15);
    assert(gb.current_ram_bank == 15 && !cupid_gb_rumble_enabled(&gb) && events.count == 4);
    cupid_gb_set_rumble_callback(&gb, NULL, &events);
    assert(!gb.rumble_callback && !gb.rumble_user_data);
    free(state);
    free(rom);
    cupid_gb_cleanup(&gb);
    assert(!gb.rumble_callback && !gb.rumble_user_data);
    assert(!cupid_gb_rumble_enabled(NULL));
    cupid_gb_set_rumble_callback(NULL, observe_rumble, &events);
}

static void test_mmm01_menu_and_lock(CupidGbModel model) {
    const size_t size = 128u * 0x4000u;
    CupidGb gb = {0};
    cupid_gb_set_model(&gb, model);
    for (unsigned layout = 0; layout < 2; ++layout) {
        uint8_t *rom = make_model_rom(model, size, 0x0d, 6, 4);
        const unsigned menu_offset = layout ? 2u : 0u;
        if (!layout) {
            memcpy(rom + size - 0x8000u + 0x104u, rom + 0x104u, 0x4au);
            rom[0x147] = 0;
        }
        assert(cupid_gb_load_rom(&gb, rom, size));
        assert(gb.header.mbc_type == CUPID_GB_MMM01 && gb.header.has_battery);
        assert(gb.rom_size == size && !memcmp(gb.rom, rom, size));
        assert(cupid_gb_read_u8(&gb, 0) == (layout ? 0 : 126));
        assert(cupid_gb_read_u8(&gb, 0x4000) == (layout ? 1 : 127));
        cupid_gb_write_u8(&gb, 0x3fff, 0x24);
        cupid_gb_write_u8(&gb, 0x7fff, 0x08);
        cupid_gb_write_u8(&gb, 0x5fff, 0x0b);
        assert(cupid_gb_read_u8(&gb, 0x4000) == (layout ? 1 : 127));
        cupid_gb_write_u8(&gb, 0x1fff, 0x7a);
        assert(cupid_gb_read_u8(&gb, 0) == 36 + menu_offset);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 37 + menu_offset);
        cupid_gb_write_u8(&gb, 0xa000, 0x91);
        cupid_gb_write_u8(&gb, 0x2000, 0x02);
        cupid_gb_write_u8(&gb, 0x4000, 0);
        assert(cupid_gb_read_u8(&gb, 0) == 36 + menu_offset);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 38 + menu_offset);
        assert(cupid_gb_effective_ram_bank(&gb) == 8);
        cupid_gb_write_u8(&gb, 0xa000, 0x82);
        cupid_gb_write_u8(&gb, 0x4000, 3);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0x91);
        cupid_gb_write_u8(&gb, 0x0000, 0);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0xff);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 38 + menu_offset);

        assert(cupid_gb_load_rom(&gb, rom, size));
        cupid_gb_write_u8(&gb, 0x6000, 0x40);
        cupid_gb_write_u8(&gb, 0x2000, 0x42);
        cupid_gb_write_u8(&gb, 0x4000, 0x09);
        cupid_gb_write_u8(&gb, 0x0000, 0x7a);
        assert(cupid_gb_read_u8(&gb, 0) == 32 + menu_offset);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 34 + menu_offset);
        assert(cupid_gb_effective_ram_bank(&gb) == 10);
        cupid_gb_write_u8(&gb, 0x6000, 1);
        cupid_gb_write_u8(&gb, 0x4000, 3);
        assert(cupid_gb_read_u8(&gb, 0) == menu_offset);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 98 + menu_offset);
        assert(cupid_gb_effective_ram_bank(&gb) == 10);
        const size_t length = cupid_gb_state_size(&gb);
        uint8_t *state = malloc(length);
        assert(state && cupid_gb_save_state(&gb, state, length, NULL) == CUPID_GB_STATE_OK);
        assert(state[8] == CUPID_GB_STATE_VERSION);
        assert(cupid_gb_load_rom(&gb, rom, size));
        assert(cupid_gb_load_state(&gb, state, length) == CUPID_GB_STATE_OK);
        assert(cupid_gb_read_u8(&gb, 0) == menu_offset);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 98 + menu_offset);
        assert(cupid_gb_effective_ram_bank(&gb) == 10);
        cupid_gb_write_u8(&gb, 0x2000, 3);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 99 + menu_offset);
        free(state);
        free(rom);
    }
    cupid_gb_cleanup(&gb);
}

static void test_mbc3_snapshot_writes(void) {
    const uint16_t addresses[] = {0x6000, 0x6fff, 0x7000, 0x7fff};
    const uint8_t masks[] = {0x3f, 0x3f, 0x1f, 0xff, 0xc1};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned model = 0; model < 2; ++model) {
        for (unsigned type = 0x0f; type <= 0x10; ++type) {
            uint8_t *rom = make_rom(0x8000, type, 0, type == 0x10 ? 3 : 0);
            cupid_gb_init(gb);
            gb->model = model ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
            assert(cupid_gb_load_rom(gb, rom, 0x8000));
            cupid_gb_write_u8(gb, 0xff40, 0);
            cupid_gb_write_u8(gb, 0, 0x0a);
            for (size_t address = 0; address < sizeof(addresses) / sizeof(addresses[0]);
                 ++address) {
                for (unsigned value = 0; value < 256; ++value) {
                    // Hold the clock to isolate mapper writes from host time.
                    gb->rtc.dh = 0xc1;
                    gb->rtc.s = 37;
                    cupid_gb_write_u8(gb, 0x6000, 1);
                    const uint8_t values[] = {(uint8_t)value, (uint8_t)(value ^ 0x35),
                                              (uint8_t)(value ^ 0x72), (uint8_t)(value ^ 0xa9),
                                              0xc1};
                    gb->rtc.s = values[0];
                    gb->rtc.m = values[1];
                    gb->rtc.h = values[2];
                    gb->rtc.dl = values[3];
                    cupid_gb_write_u8(gb, addresses[address], (uint8_t)value);
                    for (unsigned reg = 0; reg < 5; ++reg) {
                        cupid_gb_write_u8(gb, 0x4000, (uint8_t)(8 + reg));
                        assert(cupid_gb_read_u8(gb, 0xa000) == (values[reg] & masks[reg]));
                        const uint64_t generation = gb->battery_write_generation;
                        assert(cupid_gb_read_bus_u8(gb, 0xa000) == (values[reg] & masks[reg]));
                        assert(gb->rtc.ls == (values[0] & masks[0]) &&
                               gb->rtc.lm == (values[1] & masks[1]) &&
                               gb->rtc.lh == (values[2] & masks[2]) && gb->rtc.ldl == values[3] &&
                               gb->rtc.ldh == (values[4] & masks[4]));
                        assert(gb->battery_write_generation == generation);
                        // Live writes cannot alter the snapshot until another latch write.
                        cupid_gb_write_u8(gb, 0xa000, 0);
                        assert(cupid_gb_read_u8(gb, 0xa000) == (values[reg] & masks[reg]));
                    }
                }
            }
            cupid_gb_cleanup(gb);
            free(rom);
        }
    }
    free(gb);
}

int main(void) {
    test_header_storage_capabilities();
    test_mmm01_header_storage();
    test_mbc3_snapshot_writes();
    test_extended_rom_capacity(CUPID_GB_MODEL_DMG_ABC);
    test_extended_rom_capacity(CUPID_GB_MODEL_CGB);
    test_rom_capacity_output_overlap();
    test_extended_rom_file();
    test_reject_advance_cartridge();
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (size_t model = 0; model < sizeof(models) / sizeof(*models); ++model) {
        test_adjacent_caller_sources(models[model]);
        test_extended_ram_ownership(models[model]);
        test_address_line_aliases(models[model]);
        test_fresh_storage_and_restore(models[model]);
        test_physical_rom_address_lines(models[model]);
        test_multicart_wiring(models[model]);
        test_small_ram_mirrors(models[model]);
        test_extended_mbc3(models[model]);
        test_ordinary_mbc3_ram_aliases(models[model]);
        test_mbc5_ram_enable(models[model]);
        test_huc1_banks_and_ir(models[model]);
        test_huc3_banks_and_sram_modes(models[model]);
        test_mbc5_motor_and_ram(models[model]);
        test_mmm01_menu_and_lock(models[model]);
    }
    puts("Cartridge regressions passed.");
    return 0;
}

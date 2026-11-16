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
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static size_t expected_header_storage(unsigned type, unsigned ram_code) {
    static const bool external_ram[256] = {
        [0x01] = true, [0x02] = true, [0x03] = true, [0x08] = true, [0x09] = true, [0x0b] = true,
        [0x0c] = true, [0x0d] = true, [0x0f] = true, [0x10] = true, [0x11] = true, [0x12] = true,
        [0x13] = true, [0x19] = true, [0x1a] = true, [0x1b] = true, [0x1c] = true, [0x1d] = true,
        [0x1e] = true, [0x20] = true, [0xfc] = true, [0xfe] = true, [0xff] = true};
    static const size_t sizes[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
    if (type == 0x05 || type == 0x06)
        return 512;
    if (type == 0x22)
        return 256;
    return external_ram[type] && ram_code < sizeof(sizes) / sizeof(sizes[0]) ? sizes[ram_code] : 0;
}

static void check_storage_window(CupidGb *gb, size_t expected_size) {
    assert(gb->cartridge_ram_size == expected_size);
    assert(gb->ram_bank_count == (expected_size + 0x1fff) / 0x2000);
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

static void test_address_line_aliases(void) {
    const uint8_t types[] = {0x08, 0x09, 0x03, 0x06, 0x10, 0x13, 0x1b, 0x1e,
                             0x20, 0x22, 0xfc, 0xfe, 0xff, 0x0d, 0xbc};
    const uint8_t ram_codes[] = {0, 1, 2, 3, 4, 5};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    assert(cupid_gb_effective_rom_bank(gb, false) == 0);
    assert(cupid_gb_effective_ram_bank(gb) == 0);
    for (unsigned bits = 1; bits <= 9; ++bits) {
        const size_t banks = (size_t)1u << bits;
        const size_t populated = banks * 0x4000 - 0x123;
        for (unsigned type = 0; type < sizeof(types); ++type) {
            uint8_t *rom = make_rom(populated, types[type], 0, ram_codes[bits % 6]);
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
        assert(gb->cartridge_ram[offset] == 0xff);
    if (expected_size < CUPID_GB_MAX_RAM_SIZE)
        assert(gb->cartridge_ram[expected_size] == 0);
}

static void test_fresh_storage_and_restore(void) {
    const struct {
        uint8_t type, ram_code;
        size_t size;
    } variants[] = {{0x00, 0, 0},      {0x08, 1, 0x800},    {0x09, 4, 0x20000}, {0x02, 2, 0x2000},
                    {0x03, 3, 0x8000}, {0x05, 0, 512},      {0x06, 4, 512},     {0x0c, 2, 0x2000},
                    {0x0d, 3, 0x8000}, {0x10, 2, 0x2000},   {0x12, 2, 0x2000},  {0x13, 3, 0x8000},
                    {0x1a, 2, 0x2000}, {0x1b, 4, 0x20000},  {0x1d, 2, 0x2000},  {0x1e, 3, 0x8000},
                    {0x20, 2, 0x2000}, {0x22, 4, 256},      {0xfc, 4, 0x20000}, {0xfe, 2, 0x2000},
                    {0xff, 2, 0x2000}, {0xbc, 0xc1, 0x2000}};

    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (size_t variant = 0; variant < sizeof(variants) / sizeof(*variants); ++variant) {
        uint8_t *rom = make_rom(0x8000, variants[variant].type, 0, variants[variant].ram_code);
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
        gb->boot_rom_size = 0x100;
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

static void test_physical_rom_address_lines(void) {
    CupidGb gb = {0};
    /* File population determines connected address lines; the size byte is
     * metadata. Empty tails of a partially populated chip read as erased ROM. */
    const size_t populated = 0xc001;
    uint8_t *rom = make_rom(populated, 0x19, 0, 0);
    assert(cupid_gb_load_rom(&gb, rom, populated));
    assert(gb.rom_bank_count == 4 && gb.rom_size == populated);
    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x2000, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0x4000) == (bank & 3));
        assert(cupid_gb_read_u8(&gb, 0x4001) == ((bank & 3) == 3 ? 0xff : bank & 3));
    }
    free(rom);
    rom = make_rom(0x8000, 0x19, 8, 0);
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

static void test_multicart_wiring(void) {
    CupidGb gb = {0};
    const size_t size = 0x80000;
    uint8_t *rom = make_rom(size, 3, 4, 3);
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

static void test_small_ram_mirrors(void) {
    const uint8_t types[] = {0x09, 0x03, 0x13, 0x1b};
    for (unsigned controller = 0; controller < sizeof(types); ++controller) {
        CupidGb gb = {0};
        uint8_t *rom = make_rom(0x8000, types[controller], 0, 1);
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

static void test_extended_mbc3(void) {
    /* Either a larger ROM or additional RAM selects the wider controller. */
    const size_t sizes[] = {0x400000, 0x8000, 0x400000, 0x8000};
    const uint8_t rom_codes[] = {7, 0, 7, 0};
    const uint8_t ram_codes[] = {3, 5, 3, 5};
    const uint8_t types[] = {0x10, 0x10, 0x13, 0x13};
    for (unsigned variant = 0; variant < sizeof(types); ++variant) {
        CupidGb gb = {0};
        uint8_t *rom =
            make_rom(sizes[variant], types[variant], rom_codes[variant], ram_codes[variant]);
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

static void test_ordinary_mbc3_ram_aliases(void) {
    CupidGb gb = {0};
    uint8_t *rom = make_rom(0x8000, 0x13, 0, 3);
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

static void test_mbc5_ram_enable(void) {
    CupidGb gb = {0};
    uint8_t *rom = make_rom(0x8000, 0x1b, 0, 2);
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

static void test_huc1_banks_and_ir(void) {
    CupidGb gb = {0};
    uint8_t *rom = make_rom(0x100000, 0xff, 5, 5);
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

static void test_huc3_banks_and_sram_modes(void) {
    CupidGb gb = {0};
    const size_t rom_size = 0x400000;
    uint8_t *rom = make_rom(rom_size, 0xfe, 7, 4);
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

static void test_mbc5_motor_and_ram(void) {
    CupidGb gb = {0};
    RumbleEvents events = {0};
    cupid_gb_set_rumble_callback(&gb, observe_rumble, &events);
    uint8_t *rom = make_rom(0x8000, 0x1e, 0, 4);
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

static void test_mmm01_menu_and_lock(void) {
    const size_t size = 128u * 0x4000u;
    CupidGb gb = {0};
    for (unsigned layout = 0; layout < 2; ++layout) {
        uint8_t *rom = make_rom(size, 0x0d, 6, 4);
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
        assert(state[8] == 14);
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
    test_address_line_aliases();
    test_mbc3_snapshot_writes();
    test_fresh_storage_and_restore();
    test_physical_rom_address_lines();
    test_reject_advance_cartridge();
    test_multicart_wiring();
    test_small_ram_mirrors();
    test_extended_mbc3();
    test_ordinary_mbc3_ram_aliases();
    test_mbc5_ram_enable();
    test_huc1_banks_and_ir();
    test_huc3_banks_and_sram_modes();
    test_mbc5_motor_and_ram();
    test_mmm01_menu_and_lock();
    puts("Cartridge regressions passed.");
    return 0;
}

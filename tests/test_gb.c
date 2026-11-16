/**
 * @file    test_gb.c
 * @brief   Core integration tests for GB/GBC execution, memory, and peripherals.
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

#include "gb_test_helpers.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Create an exclusive temporary file without assuming a Unix temporary directory. */
static FILE *open_test_file(char *path, size_t capacity) {
#ifdef _WIN32
    char directory[MAX_PATH];
    DWORD length = GetTempPathA(sizeof(directory), directory);
    assert(length > 0 && length < sizeof(directory));
    assert(capacity >= MAX_PATH);
    assert(GetTempFileNameA(directory, "cgb", 0, path));
    FILE *file = fopen(path, "wb");
#else
    const char pattern[] = "/tmp/cupidgb-XXXXXX";
    assert(capacity >= sizeof(pattern));
    memcpy(path, pattern, sizeof(pattern));
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor, "wb");
#endif
    assert(file != NULL);
    return file;
}

#include "cupid/core/emulator.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"
#include "cupid/gbc/cgb.h"

static const uint8_t nintendo_logo[] = {
    0xceu, 0xedu, 0x66u, 0x66u, 0xccu, 0x0du, 0x00u, 0x0bu, 0x03u, 0x73u, 0x00u, 0x83u,
    0x00u, 0x0cu, 0x00u, 0x0du, 0x00u, 0x08u, 0x11u, 0x1fu, 0x88u, 0x89u, 0x00u, 0x0eu,
    0xdcu, 0xccu, 0x6eu, 0xe6u, 0xddu, 0xddu, 0xd9u, 0x99u, 0xbbu, 0xbbu, 0x67u, 0x63u,
    0x6eu, 0x0eu, 0xecu, 0xccu, 0xddu, 0xdcu, 0x99u, 0x9fu, 0xbbu, 0xb9u, 0x33u, 0x3eu};

static uint8_t compute_header_checksum_at(const uint8_t *rom, size_t base_offset) {
    size_t index;
    uint8_t checksum = 0u;

    for (index = base_offset + 0x0134u; index <= base_offset + 0x014cu; ++index) {
        checksum = (uint8_t)(checksum - rom[index] - 1u);
    }

    return checksum;
}

static uint8_t popcount8(uint8_t value) {
    uint8_t count = 0u;

    while (value != 0u) {
        count = (uint8_t)(count + (value & 0x01u));
        value >>= 1u;
    }

    return count;
}

static unsigned sgb_header_stream_popcount(const uint8_t *rom, size_t rom_size) {
    size_t offset = 0x0104u;
    unsigned total = 0u;

    while (offset < 0x0158u) {
        uint8_t checksum = 0u;
        size_t block_index;

        for (block_index = 0u; block_index < 14u; ++block_index) {
            uint8_t value = 0u;

            if (offset <= 0x014fu && offset < rom_size) {
                value = rom[offset];
            }

            checksum = (uint8_t)(checksum + value);
            total += popcount8(value);
            offset += 1u;
        }

        total += popcount8(checksum);
    }

    return total;
}

static void build_mbc1_test_rom(uint8_t *rom, size_t rom_size) {
    size_t bank;
    const char title[] = "MBC1 TEST";

    memset(rom, 0, rom_size);
    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x01u;
    rom[0x0148] = 0x02u;
    rom[0x0149] = 0x03u;
    rom[0x014a] = 0x01u;

    for (bank = 0u; bank < 8u; ++bank) {
        size_t bank_offset = bank * 0x4000u;
        if (bank_offset < rom_size) {
            memset(&rom[bank_offset], (int)bank, 0x4000u);
        }
    }

    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x01u;
    rom[0x0148] = 0x02u;
    rom[0x0149] = 0x03u;
    rom[0x014a] = 0x01u;
    rom[0x014d] = compute_header_checksum(rom);
}

static void build_mbc1_ram_test_rom(uint8_t *rom, size_t rom_size) {
    const char title[] = "MBC1 RAM";

    memset(rom, 0, rom_size);
    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x03u;
    rom[0x0148] = 0x01u;
    rom[0x0149] = 0x03u;
    rom[0x014a] = 0x01u;
    rom[0x014d] = compute_header_checksum(rom);
}

static void build_mbc2_test_rom(uint8_t *rom, size_t rom_size) {
    size_t bank;
    const char title[] = "MBC2 TEST";

    memset(rom, 0, rom_size);
    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x06u;
    rom[0x0148] = 0x01u;
    rom[0x0149] = 0x00u;
    rom[0x014a] = 0x01u;

    for (bank = 0u; bank < 4u; ++bank) {
        size_t bank_offset = bank * 0x4000u;
        if (bank_offset < rom_size) {
            memset(&rom[bank_offset], (int)bank, 0x4000u);
        }
    }

    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x06u;
    rom[0x0148] = 0x01u;
    rom[0x0149] = 0x00u;
    rom[0x014a] = 0x01u;
    rom[0x014d] = compute_header_checksum(rom);
}

static void build_mbc1_multicart_test_rom(uint8_t *rom, size_t rom_size) {
    size_t bank;
    size_t image;
    static const char *const titles[4] = {"MULTI-A", "MULTI-B", "MULTI-C", "MULTI-D"};

    memset(rom, 0, rom_size);

    for (bank = 0u; bank < 64u; ++bank) {
        memset(&rom[bank * 0x4000u], (int)bank, 0x4000u);
    }

    for (image = 0u; image < 4u; ++image) {
        size_t base_offset = image * 0x40000u;

        memcpy(&rom[base_offset + 0x0104u], nintendo_logo, sizeof(nintendo_logo));
        memset(&rom[base_offset + 0x0134u], 0, 16u);
        memcpy(&rom[base_offset + 0x0134u], titles[image], strlen(titles[image]));
        rom[base_offset + 0x0143u] = 0x00u;
        rom[base_offset + 0x0147u] = 0x01u;
        rom[base_offset + 0x0148u] = 0x03u;
        rom[base_offset + 0x0149u] = 0x00u;
        rom[base_offset + 0x014au] = 0x01u;
        rom[base_offset + 0x014du] = compute_header_checksum_at(rom, base_offset);
    }

    rom[0x0148u] = 0x05u;
    rom[0x014du] = compute_header_checksum(rom);
}

static bool is_illegal_unprefixed(uint8_t opcode) {
    switch (opcode) {
        case 0xd3u:
        case 0xdbu:
        case 0xddu:
        case 0xe3u:
        case 0xe4u:
        case 0xebu:
        case 0xecu:
        case 0xedu:
        case 0xf4u:
        case 0xfcu:
        case 0xfdu:
            return true;
        default:
            return false;
    }
}

static void test_parse_header(void) {
    uint8_t rom[32u * 1024u];
    CupidGbCartridgeHeader header = {0};

    build_test_rom(rom, sizeof(rom));

    assert(cupid_gb_parse_header(rom, sizeof(rom), &header));
    assert(strcmp(header.title, "CPU TEST") == 0);
    assert(header.cartridge_type == 0x00u);
    assert(header.rom_size_code == 0x00u);
    assert(header.destination_code == 0x00u);
    assert(header.rom_bank_count == 2u);
    assert(header.ram_bank_count == 0u);
    assert(header.mbc_type == CUPID_GB_MBC_NONE);
    assert(header.header_checksum_valid);
}

static void test_header_title_byte_boundaries(void) {
    uint8_t rom[32u * 1024u];
    CupidGbCartridgeHeader header;
    build_test_rom(rom, sizeof(rom));
    memcpy(rom + 0x134, "FULLTITLE123456Z", 16);
    rom[0x14d] = compute_header_checksum(rom);
    assert(cupid_gb_parse_header(rom, sizeof(rom), &header));
    assert(strcmp(header.title, "FULLTITLE123456Z") == 0);
    assert(header.cgb_flag == 'Z' && header.header_checksum_valid);

    const uint8_t color_flags[] = {0x80, 0xc0};
    for (size_t i = 0; i < sizeof(color_flags); ++i) {
        /* All fifteen text bytes may be populated. The four bytes overlapping
         * the manufacturer field remain visible when the title has no NUL. */
        memcpy(rom + 0x134, "TITLE123456ABCD", 15);
        rom[0x143] = color_flags[i];
        rom[0x14d] = compute_header_checksum(rom);
        memset(&header, 0xa5, sizeof(header));
        assert(cupid_gb_parse_header(rom, sizeof(rom), &header));
        assert(strcmp(header.title, "TITLE123456ABCD") == 0);
        assert(header.cgb_flag == color_flags[i] && header.header_checksum_valid);

        const uint8_t terminators[] = {0, 0x1f, 0x80, 0xc0, 0xff};
        for (size_t j = 0; j < sizeof(terminators); ++j) {
            memcpy(rom + 0x134, "TITLE123456ABCD", 15);
            rom[0x13f] = terminators[j];
            rom[0x14d] = compute_header_checksum(rom);
            assert(cupid_gb_parse_header(rom, sizeof(rom), &header));
            assert(strcmp(header.title, "TITLE123456") == 0);
            assert(header.cgb_flag == color_flags[i] && header.header_checksum_valid);
        }
    }
}

static void test_parse_mbc1_header(void) {
    uint8_t rom[128u * 1024u];
    CupidGbCartridgeHeader header = {0};

    build_mbc1_test_rom(rom, sizeof(rom));

    assert(cupid_gb_parse_header(rom, sizeof(rom), &header));
    assert(strcmp(header.title, "MBC1 TEST") == 0);
    assert(header.cartridge_type == 0x01u);
    assert(header.destination_code == 0x01u);
    assert(header.rom_bank_count == 8u);
    assert(header.ram_bank_count == 4u);
    assert(header.mbc_type == CUPID_GB_MBC1);
    assert(header.header_checksum_valid);
}

static void test_boot_model_profiles(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG0);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_DMG0);
    assert(gb.cpu.a == 0x01u);
    assert(gb.cpu.f == 0x00u);
    assert(gb.cpu.b == 0xffu);
    assert(gb.cpu.c == 0x13u);
    assert(gb.cpu.e == 0xc1u);
    assert(gb.cpu.h == 0x84u);
    assert(gb.cpu.l == 0x03u);
    assert(gb.io_registers[0x04u] == 0x18u);
    assert(gb.io_registers[CUPID_GB_IO_STAT] == 0x83u);
    assert(gb.io_registers[CUPID_GB_IO_DMA] == 0x01u);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x91u);
    assert(gb.div_counter == 12u);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(gb.model == CUPID_GB_MODEL_DMG0);
    assert(gb.cpu.b == 0xffu);
    assert(gb.io_registers[0x04u] == 0x18u);
    assert(gb.io_registers[CUPID_GB_IO_STAT] == 0x83u);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x91u);
    assert(gb.div_counter == 12u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG_ABC);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_DMG_ABC);
    assert(gb.cpu.a == 0x01u);
    assert(gb.cpu.f == 0xb0u);
    assert(gb.cpu.b == 0x00u);
    assert(gb.cpu.c == 0x13u);
    assert(gb.cpu.e == 0xd8u);
    assert(gb.cpu.h == 0x01u);
    assert(gb.cpu.l == 0x4du);
    assert(gb.io_registers[0x04u] == 0xabu);
    assert(gb.io_registers[CUPID_GB_IO_STAT] == 0x80u);
    assert(gb.io_registers[CUPID_GB_IO_DMA] == 0x0au);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x00u);
    assert(gb.div_counter == 51u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_CGB);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_CGB);
    assert(gb.cpu.a == 0x11u);
    assert(gb.cpu.f == 0x80u);
    assert(gb.cpu.d == 0xffu);
    assert(gb.cpu.e == 0x56u);
    assert(gb.io_registers[0x04u] == 0x1eu);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x00u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_MGB);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_MGB);
    assert(gb.cpu.a == 0xffu);
    assert(gb.cpu.f == 0xb0u);
    assert(gb.cpu.b == 0x00u);
    assert(gb.cpu.c == 0x13u);
    assert(gb.cpu.e == 0xd8u);
    assert(gb.cpu.h == 0x01u);
    assert(gb.cpu.l == 0x4du);
    assert(gb.io_registers[0x04u] == 0xabu);
    assert(gb.io_registers[CUPID_GB_IO_STAT] == 0x80u);
    assert(gb.io_registers[CUPID_GB_IO_DMA] == 0x0au);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x00u);
    assert(gb.div_counter == 51u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_SGB);
    assert(gb.cpu.a == 0x01u);
    assert(gb.cpu.f == 0x00u);
    assert(gb.cpu.b == 0x00u);
    assert(gb.cpu.c == 0x14u);
    assert(gb.cpu.d == 0x00u);
    assert(gb.cpu.e == 0x00u);
    assert(gb.cpu.h == 0xc0u);
    assert(gb.cpu.l == 0x60u);
    assert(gb.io_registers[0x04u] == 0xd8u);
    assert(gb.div_counter == 24u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB2);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_SGB2);
    assert(gb.cpu.a == 0xffu);
    assert(gb.cpu.f == 0x00u);
    assert(gb.cpu.b == 0x00u);
    assert(gb.cpu.c == 0x14u);
    assert(gb.cpu.d == 0x00u);
    assert(gb.cpu.e == 0x00u);
    assert(gb.cpu.h == 0xc0u);
    assert(gb.cpu.l == 0x60u);
    assert(gb.io_registers[0x04u] == 0xd8u);
    assert(gb.div_counter == 24u);
    cupid_gb_cleanup(&gb);
}

static void test_boot_io_defaults(void) {
    CupidGb gb = {0};

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG0);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff02u) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff03u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff07u) == 0xf8u);
    assert(cupid_gb_read_u8(&gb, 0xff08u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff41u) == 0x83u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 0x91u);
    assert(cupid_gb_read_u8(&gb, 0xff46u) == 0x01u);
    assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xffu);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG_ABC);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff02u) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff07u) == 0xf8u);
    assert(cupid_gb_read_u8(&gb, 0xff41u) == 0x80u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xff46u) == 0x0au);
    assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xffu);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_CGB);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff4du) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff4fu) == 0xfeu);
    assert(cupid_gb_read_u8(&gb, 0xff68u) == 0x40u);
    assert(cupid_gb_read_u8(&gb, 0xff70u) == 0xf9u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_MGB);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff02u) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff07u) == 0xf8u);
    assert(cupid_gb_read_u8(&gb, 0xff41u) == 0x80u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xff46u) == 0x0au);
    assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xffu);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff00u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff02u) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff07u) == 0xf8u);
    assert(cupid_gb_read_u8(&gb, 0xff26u) == 0xf0u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB2);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff00u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff26u) == 0xf0u);
    cupid_gb_cleanup(&gb);
}

static void test_sgb_boot_div_phase_depends_on_header_stream(void) {
    uint8_t rom1[32u * 1024u];
    uint8_t rom2[32u * 1024u];
    CupidGb gb = {0};
    uint16_t counter1;
    unsigned popcount1;
    unsigned popcount2;

    build_idle_rom(rom1, sizeof(rom1));
    memcpy(rom2, rom1, sizeof(rom2));

    rom1[0x0146u] = 0x03u;
    rom2[0x0146u] = 0x03u;
    rom1[0x014eu] = 0x34u;
    rom1[0x014fu] = 0x12u;
    rom2[0x014eu] = 0x96u;
    rom2[0x014fu] = 0xa7u;
    popcount1 = sgb_header_stream_popcount(rom1, sizeof(rom1));
    popcount2 = sgb_header_stream_popcount(rom2, sizeof(rom2));

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    assert(cupid_gb_load_rom(&gb, rom1, sizeof(rom1)));
    assert(gb.io_registers[0x04u] == 0xd8u);
    counter1 = gb.div_counter;

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    assert(cupid_gb_load_rom(&gb, rom2, sizeof(rom2)));
    assert(gb.io_registers[0x04u] == 0xd8u);
    assert(gb.div_counter ==
           (uint16_t)((counter1 + 64u - ((popcount2 - popcount1) & 0x3fu)) & 0x3fu));
    cupid_gb_cleanup(&gb);
}

static void test_load_rom_file(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    char path[1024];
    FILE *file;

    build_test_rom(rom, sizeof(rom));
    file = open_test_file(path, sizeof(path));
    assert(file != NULL);
    assert(fwrite(rom, 1u, sizeof(rom), file) == sizeof(rom));
    assert(fclose(file) == 0);

    assert(cupid_gb_load_rom_file(&gb, path));
    assert(strcmp(gb.header.title, "CPU TEST") == 0);
    assert(gb.rom_bank_count == 2u);
    assert(gb.ram_bank_count == 0u);

    assert(remove(path) == 0);
    cupid_gb_cleanup(&gb);
}

static void test_boot_rom_file_maps_until_ff50_disable(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    uint8_t boot_rom[0x900] = {0};
    boot_rom[0xffu] = 0xaau;
    boot_rom[0x200u] = 0xbbu;
    boot_rom[0x8ffu] = 0xccu;

    uint8_t rom[32u * 1024u];
    build_test_rom(rom, sizeof(rom));
    rom[0x00u] = 0xffu;

    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        CupidGb gb = {0};
        const bool color_hardware = models[i] == CUPID_GB_MODEL_CGB;
        const size_t boot_size = color_hardware ? 0x900u : 0x100u;
        char path[1024];
        FILE *file = open_test_file(path, sizeof(path));
        assert(fwrite(boot_rom, 1, boot_size, file) == boot_size);
        assert(fclose(file) == 0);

        cupid_gb_set_model(&gb, models[i]);
        assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
        assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xffu);
        assert(cupid_gb_load_boot_rom_file(&gb, path));
        assert(remove(path) == 0);
        cupid_gb_enter_boot_rom(&gb);

        assert(gb.cpu.pc == 0);
        assert(cupid_gb_read_u8(&gb, 0x0000u) == boot_rom[0]);
        assert(cupid_gb_read_u8(&gb, 0x00ffu) == boot_rom[0xffu]);
        assert(cupid_gb_read_u8(&gb, 0x0100u) == rom[0x0100u]);
        assert(cupid_gb_read_u8(&gb, 0x01ffu) == rom[0x01ffu]);
        assert(cupid_gb_read_u8(&gb, 0x0200u) == (color_hardware ? boot_rom[0x200u] : rom[0x200u]));
        assert(cupid_gb_read_u8(&gb, 0x08ffu) == (color_hardware ? boot_rom[0x8ffu] : rom[0x8ffu]));
        assert(cupid_gb_read_u8(&gb, 0x0900u) == rom[0x0900u]);

        const uint8_t reset_value = color_hardware ? 0x00u : 0xffu;
        assert(cupid_gb_read_u8(&gb, 0xff46u) == reset_value);
        assert(cupid_gb_read_u8(&gb, 0xff48u) == reset_value);
        assert(cupid_gb_read_u8(&gb, 0xff49u) == reset_value);

        /* The first DIV increment is visible 248 T-cycles into firmware execution. */
        for (unsigned step = 0; step < 62; ++step) {
            assert(cupid_gb_read_u8(&gb, 0xff04u) == 0);
            assert(cupid_gb_step(&gb));
        }
        assert(gb.elapsed_cycles == 248);
        assert(cupid_gb_read_u8(&gb, 0xff04u) == 1);

        assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xfeu);
        cupid_gb_write_u8(&gb, 0xff50u, 0xfeu);
        assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xfeu);
        cupid_gb_write_u8(&gb, 0xff50u, 0x01u);
        cupid_gb_write_u8(&gb, 0xff50u, 0x00u);
        assert(!gb.boot_rom_enabled);
        assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xffu);
        assert(cupid_gb_read_u8(&gb, 0x0000u) == rom[0x0000u]);
        assert(cupid_gb_read_u8(&gb, 0x0200u) == rom[0x0200u]);

        cupid_gb_cleanup(&gb);
    }
}

static void test_save_path_does_not_overwrite_rom(void) {
    CupidGb gb = {0};
    cupid_gb_set_save_path(&gb, "games.v1/cartridge");
    assert(strcmp(gb.save_path, "games.v1/cartridge.sav") == 0);
    cupid_gb_set_save_path(&gb, "games.v1\\cartridge.gbc");
    assert(strcmp(gb.save_path, "games.v1\\cartridge.sav") == 0);
    cupid_gb_set_save_path(&gb, ".cartridge");
    assert(strcmp(gb.save_path, ".cartridge.sav") == 0);
    char long_path[300];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = 0;
    cupid_gb_set_save_path(&gb, long_path);
    assert(gb.save_path[0] == 0);
    cupid_gb_cleanup(&gb);
}

static void test_battery_save_loads_for_valid_rom(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    char rom_path[1024];
    uint8_t expected[8192];
    FILE *file;

    build_test_rom(rom, sizeof(rom));
    rom[0x0147] = 0x03u; /* MBC1+RAM+BATTERY */
    rom[0x0149] = 0x02u; /* 8 KiB RAM */
    rom[0x014d] = compute_header_checksum(rom);

    for (size_t i = 0; i < sizeof(expected); ++i) {
        expected[i] = (uint8_t)(i * 13u + i / 256u);
    }

    file = open_test_file(rom_path, sizeof(rom_path));
    assert(file != NULL);
    assert(fwrite(rom, 1u, sizeof(rom), file) == sizeof(rom));
    assert(fclose(file) == 0);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    cupid_gb_set_save_path(&gb, rom_path);

    file = fopen(gb.save_path, "wb");
    assert(file != NULL);
    assert(fwrite(expected, 1u, sizeof(expected), file) == sizeof(expected));
    assert(fclose(file) == 0);

    cupid_gb_load_save(&gb);

    assert(memcmp(gb.cartridge_ram, expected, sizeof(expected)) == 0);

    assert(remove(gb.save_path) == 0);
    assert(remove(rom_path) == 0);
    cupid_gb_cleanup(&gb);
}

static void test_battery_save_ignored_for_invalid_untitled_rom(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    char rom_path[1024];
    uint8_t persisted[8192];
    uint8_t erased[8192];
    FILE *file;

    build_test_rom(rom, sizeof(rom));
    memset(&rom[0x0134], 0, 16u);
    rom[0x0147] = 0x03u; /* MBC1+RAM+BATTERY */
    rom[0x0149] = 0x02u; /* 8 KiB RAM */
    rom[0x014d] = 0x00u; /* intentionally invalid */
    memset(persisted, 0x5a, sizeof(persisted));
    memset(erased, 0xff, sizeof(erased));

    file = open_test_file(rom_path, sizeof(rom_path));
    assert(file != NULL);
    assert(fwrite(rom, 1u, sizeof(rom), file) == sizeof(rom));
    assert(fclose(file) == 0);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    cupid_gb_set_save_path(&gb, rom_path);

    file = fopen(gb.save_path, "wb");
    assert(file != NULL);
    assert(fwrite(persisted, 1u, sizeof(persisted), file) == sizeof(persisted));
    assert(fclose(file) == 0);

    cupid_gb_load_save(&gb);

    assert(memcmp(gb.cartridge_ram, erased, sizeof(erased)) == 0);

    assert(remove(gb.save_path) == 0);
    assert(remove(rom_path) == 0);
    cupid_gb_cleanup(&gb);
}

static void test_mbc3_selection_aliases_and_unmapped_ram(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    build_test_rom(rom, sizeof(rom));
    rom[0x147] = 0x10; /* MBC3, RTC, RAM, and battery. */
    rom[0x149] = 3;
    rom[0x14d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    cupid_gb_write_u8(&gb, 0x0000, 0x0a);
    for (unsigned bank = 0; bank < 4; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        cupid_gb_write_u8(&gb, 0xa000, (uint8_t)(0x50u + bank));
    }
    gb.rtc = (CupidGbRtc){.s = 1, .ls = 37, .dh = 0x40};
    const uint64_t generation = gb.battery_write_generation;
    for (unsigned upper = 0; upper < 16; ++upper) {
        for (unsigned bank = 0; bank < 8; ++bank) {
            cupid_gb_write_u8(&gb, 0x4000, (uint8_t)(upper * 16u + bank));
            assert(gb.rtc_register == 0 && gb.current_ram_bank == bank);
            assert(cupid_gb_read_u8(&gb, 0xa000) == (bank < 4 ? 0x50u + bank : 0xffu));
            if (bank >= 4)
                cupid_gb_write_u8(&gb, 0xa000, 0xaa);
        }
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)(upper * 16u + 8u));
        assert(gb.rtc_register == 8 && cupid_gb_read_u8(&gb, 0xa000) == 37);
        /* An unconnected RTC selection must replace the previous valid one,
         * and writes through it must not reach that RTC register or RAM. */
        const CupidGbRtc before = gb.rtc;
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)(upper * 16u + 15u));
        assert(gb.rtc_register == 15);
        cupid_gb_write_u8(&gb, 0xa000, 0xaa);
        assert(memcmp(&gb.rtc, &before, sizeof(before)) == 0);
    }
    assert(gb.battery_write_generation == generation);
    for (unsigned bank = 0; bank < 4; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0x50u + bank);
    }
    cupid_gb_write_u8(&gb, 0x4000, 0x38);
    cupid_gb_write_u8(&gb, 0xa000, 12);
    assert(gb.rtc.s == 12 && gb.rtc.ls == 37);
    assert(gb.battery_write_generation == generation + 1);
    cupid_gb_cleanup(&gb);
}

static void test_mbc1_bank_switching(void) {
    uint8_t rom[128u * 1024u];
    CupidGb gb = {0};

    build_mbc1_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(!gb.mbc1_multicart);

    assert(cupid_gb_read_u8(&gb, 0x0150u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x01u);

    cupid_gb_write_u8(&gb, 0x2000u, 0x02u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x02u);

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);
    cupid_gb_write_u8(&gb, 0xa000u, 0x5au);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x5au);
    cupid_gb_cleanup(&gb);
}

static void test_mbc1_multicart_bank_switching(void) {
    uint8_t rom[1024u * 1024u];
    CupidGb gb = {0};

    build_mbc1_multicart_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(gb.mbc1_multicart);

    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x01u);

    cupid_gb_write_u8(&gb, 0x2000u, 0x10u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x00u);

    cupid_gb_write_u8(&gb, 0x4000u, 0x02u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x20u);

    cupid_gb_write_u8(&gb, 0x6000u, 0x01u);
    assert(cupid_gb_read_u8(&gb, 0x0000u) == 0x20u);
    cupid_gb_cleanup(&gb);
}

static void test_mbc1_ram_banking_mode_switching(void) {
    uint8_t rom[64u * 1024u];
    CupidGb gb = {0};

    build_mbc1_ram_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);

    cupid_gb_write_u8(&gb, 0x6000u, 0x01u);
    cupid_gb_write_u8(&gb, 0x4000u, 0x03u);
    cupid_gb_write_u8(&gb, 0xa000u, 0x5au);
    assert(gb.current_ram_bank == 3u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x5au);

    cupid_gb_write_u8(&gb, 0x6000u, 0x00u);
    assert(gb.current_ram_bank == 0u);
    cupid_gb_write_u8(&gb, 0xa000u, 0x11u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x11u);

    cupid_gb_write_u8(&gb, 0x6000u, 0x01u);
    cupid_gb_write_u8(&gb, 0x4000u, 0x03u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x5au);

    cupid_gb_write_u8(&gb, 0x4000u, 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x11u);
    cupid_gb_cleanup(&gb);
}

static void test_mbc2_address_decoding(void) {
    uint8_t rom[64u * 1024u];
    CupidGb gb = {0};

    build_mbc2_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);
    cupid_gb_write_u8(&gb, 0xa000u, 0x05u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0xf5u);

    cupid_gb_write_u8(&gb, 0x3effu, 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0xffu);

    cupid_gb_write_u8(&gb, 0x3fffu, 0x0au);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0xffu);

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0xf5u);

    cupid_gb_write_u8(&gb, 0x0100u, 0x02u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x02u);
    cupid_gb_cleanup(&gb);
}

static void test_load_32mb_mbc5_rom(void) {
    uint8_t *rom;
    CupidGb gb = {0};

    rom = calloc(1u, 4u * 1024u * 1024u);
    assert(rom != NULL);

    memcpy(&rom[0x0134], "MBC5 32MB", 9u);
    rom[0x0147] = 0x19u;
    rom[0x0148] = 0x07u;
    rom[0x0149] = 0x00u;
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, 4u * 1024u * 1024u));
    assert(gb.rom_bank_count == 256u);
    assert(gb.header.mbc_type == CUPID_GB_MBC5);

    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_load_64mb_mbc5_rom(void) {
    uint8_t *rom;
    CupidGb gb = {0};

    rom = calloc(1u, 8u * 1024u * 1024u);
    assert(rom != NULL);

    memcpy(&rom[0x0134], "MBC5 64MB", 9u);
    rom[0x0147] = 0x19u;
    rom[0x0148] = 0x08u;
    rom[0x0149] = 0x00u;
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, 8u * 1024u * 1024u));
    assert(gb.rom_bank_count == 512u);
    assert(gb.header.mbc_type == CUPID_GB_MBC5);

    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_emulator_executes_program(void) {
    uint8_t rom[32u * 1024u];
    CupidEmulator emulator = {0};
    unsigned int steps = 0u;

    build_test_rom(rom, sizeof(rom));
    cupid_emulator_init(&emulator, CUPID_SYSTEM_GB);

    assert(cupid_emulator_load_rom(&emulator, rom, sizeof(rom)));
    assert(emulator.gb.cpu.pc == 0x0100u);

    while (!emulator.gb.cpu.halted && steps < 16u) {
        assert(cupid_emulator_step(&emulator));
        ++steps;
    }

    assert(emulator.gb.cpu.halted);
    assert(steps == 5u);
    assert(emulator.gb.cpu.a == 0x12u);
    assert(cupid_gb_read_u8(&emulator.gb, 0xc000u) == 0x12u);

    cupid_emulator_shutdown(&emulator);
}

static void test_unprefixed_opcode_coverage(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int opcode;

    for (opcode = 0u; opcode <= 0xffu; ++opcode) {
        prepare_gb(&gb, rom, (uint8_t)opcode, 0x00u, 0x00u);
        CupidGbStepTrace trace;
        assert(cupid_gb_step_observed(&gb, &trace));
        assert(trace.address == 0x100u && trace.opcode == opcode);
        if (is_illegal_unprefixed((uint8_t)opcode)) {
            assert(trace.event == CUPID_GB_STEP_ILLEGAL_OPCODE);
            assert(gb.cpu.halted && !gb.interrupt_enable);
        } else {
            assert(trace.event == CUPID_GB_STEP_INSTRUCTION);
        }
        assert(cupid_gb_opcode_is_illegal((uint8_t)opcode) ==
               is_illegal_unprefixed((uint8_t)opcode));
    }
    cupid_gb_cleanup(&gb);
}

static void test_cb_opcode_coverage(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int opcode;

    for (opcode = 0u; opcode <= 0xffu; ++opcode) {
        prepare_gb(&gb, rom, 0xcbu, (uint8_t)opcode, 0x00u);
        assert(cupid_gb_step(&gb));
    }
    cupid_gb_cleanup(&gb);
}

static void test_call_and_ret(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    prepare_gb(&gb, rom, 0xcdu, 0x00u, 0x02u);
    assert(cupid_gb_step(&gb));
    assert(gb.cpu.pc == 0x0200u);
    assert(gb.cpu.sp == 0xff80u);
    assert(cupid_gb_read_u8(&gb, 0xff80u) == 0x03u);
    assert(cupid_gb_read_u8(&gb, 0xff81u) == 0x01u);

    gb.rom[0x0200] = 0xc9u;
    assert(cupid_gb_step(&gb));
    assert(gb.cpu.pc == 0x0103u);
    assert(gb.cpu.sp == 0xff82u);
    cupid_gb_cleanup(&gb);
}

static void test_cb_bit_res_set(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    prepare_gb(&gb, rom, 0xcbu, 0x46u, 0x00u);
    cupid_gb_write_u8(&gb, 0xc030u, 0x01u);
    assert(cupid_gb_step(&gb));
    assert((gb.cpu.f & 0x80u) == 0u);
    assert((gb.cpu.f & 0x20u) != 0u);

    prepare_gb(&gb, rom, 0xcbu, 0x86u, 0x00u);
    cupid_gb_write_u8(&gb, 0xc030u, 0xffu);
    assert(cupid_gb_step(&gb));
    assert(cupid_gb_read_u8(&gb, 0xc030u) == 0xfeu);

    prepare_gb(&gb, rom, 0xcbu, 0xfeu, 0x00u);
    cupid_gb_write_u8(&gb, 0xc030u, 0x00u);
    assert(cupid_gb_step(&gb));
    assert(cupid_gb_read_u8(&gb, 0xc030u) == 0x80u);
    cupid_gb_cleanup(&gb);
}

static void test_jr_negative(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int steps = 0u;
    static const uint8_t program[] = {0x3eu, 0x00u, 0x3cu, 0xfeu, 0x02u,
                                      0x28u, 0x02u, 0x18u, 0xf9u, 0x76u};

    build_test_rom(rom, sizeof(rom));
    memcpy(&rom[0x0100], program, sizeof(program));
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    while (!gb.cpu.halted && steps < 64u) {
        assert(cupid_gb_step(&gb));
        ++steps;
    }

    assert(gb.cpu.halted);
    assert(gb.cpu.a == 0x02u);
    cupid_gb_cleanup(&gb);
}

static void test_jr_positive(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int steps = 0u;
    static const uint8_t program[] = {0x3eu, 0x00u, 0x18u, 0x01u, 0x3cu, 0x3cu, 0x3cu, 0x76u};

    build_test_rom(rom, sizeof(rom));
    memcpy(&rom[0x0100], program, sizeof(program));
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    while (!gb.cpu.halted && steps < 16u) {
        assert(cupid_gb_step(&gb));
        ++steps;
    }

    assert(gb.cpu.halted);
    assert(gb.cpu.a == 0x02u);
    cupid_gb_cleanup(&gb);
}

static void test_jp_hl(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int steps = 0u;
    static const uint8_t program[] = {0x21u, 0x06u, 0x01u, 0x3eu, 0x00u,
                                      0xe9u, 0x3cu, 0x3cu, 0x76u};

    build_test_rom(rom, sizeof(rom));
    memcpy(&rom[0x0100], program, sizeof(program));
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    while (!gb.cpu.halted && steps < 16u) {
        assert(cupid_gb_step(&gb));
        ++steps;
    }

    assert(gb.cpu.halted);
    assert(gb.cpu.a == 0x02u);
    cupid_gb_cleanup(&gb);
}

static void test_pop_af_masks_low_nibble(void) {
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int value;

    for (value = 0u; value <= 0xffu; ++value) {
        prepare_gb(&gb, rom, 0xc5u, 0xf1u, 0xf5u);
        gb.cpu.b = 0x12u;
        gb.cpu.c = (uint8_t)value;
        gb.rom[0x0103] = 0xd1u;
        gb.rom[0x0104] = 0x76u;

        assert(cupid_gb_step(&gb));
        assert(cupid_gb_step(&gb));
        assert(cupid_gb_step(&gb));
        assert(cupid_gb_step(&gb));

        assert(gb.cpu.d == 0x12u);
        assert(gb.cpu.e == ((uint8_t)value & 0xf0u));
    }
    cupid_gb_cleanup(&gb);
}

int main(void) {
    test_parse_header();
    test_header_title_byte_boundaries();
    test_parse_mbc1_header();
    test_boot_model_profiles();
    test_boot_io_defaults();
    test_sgb_boot_div_phase_depends_on_header_stream();
    test_load_rom_file();
    test_boot_rom_file_maps_until_ff50_disable();
    test_save_path_does_not_overwrite_rom();
    test_battery_save_loads_for_valid_rom();
    test_battery_save_ignored_for_invalid_untitled_rom();
    test_mbc3_selection_aliases_and_unmapped_ram();
    test_mbc1_bank_switching();
    test_mbc1_multicart_bank_switching();
    test_mbc1_ram_banking_mode_switching();
    test_mbc2_address_decoding();
    test_load_32mb_mbc5_rom();
    test_load_64mb_mbc5_rom();
    test_emulator_executes_program();
    test_unprefixed_opcode_coverage();
    test_cb_opcode_coverage();
    test_call_and_ret();
    test_cb_bit_res_set();
    test_jr_negative();
    test_jr_positive();
    test_jp_hl();
    test_pop_af_masks_low_nibble();
    test_gb_hardware();
    return 0;
}

/**
 * @file    test_mbc6.c
 * @brief   Generated MBC6 split-window, flash command, storage, and continuation regressions.
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
#include "cupid/gb/debugger.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum { ROM_BYTES = 1024 * 1024, PAGE_BYTES = 128, SECTOR_BYTES = 128 * 1024 };

static CupidGb *machine(CupidGbModel model) {
    static const uint8_t program[] = {
        0x3e, 0x25, 0xea, 0x00, 0x20, /* Select 8 KiB ROM bank 37 in window A. */
        0x3e, 0x4c, 0xea, 0x00, 0x30, /* Select bank 76 in window B. */
        0xfa, 0x00, 0x42, 0xea, 0x00, 0xc0, 0xfa, 0x00, 0x62, 0xea, 0x01,
        0xc0, 0x3e, 0x0a, 0xea, 0x00, 0x00, /* Enable SRAM and select separate 4 KiB banks. */
        0x3e, 0x04, 0xea, 0x00, 0x04, 0x3e, 0x07, 0xea, 0x00, 0x08, 0x3e,
        0xaf, 0xea, 0xdc, 0xaf, 0x3e, 0x61, 0xea, 0xdb, 0xbf, 0x76};
    uint8_t *rom = malloc(ROM_BYTES);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(rom && gb);
    for (unsigned bank = 0; bank < 128; ++bank)
        memset(rom + bank * 8192u, (int)bank, 8192u);
    rom[0x143] = 0x80;
    rom[0x147] = 0x20;
    rom[0x148] = 5;
    rom[0x149] = 3;
    memcpy(rom + 0x150, program, sizeof(program));
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

static void enable_flash(CupidGb *gb) {
    cupid_gb_write_u8(gb, 0x0c00, 1);
    cupid_gb_write_u8(gb, 0x2800, 8);
}

static void flash_write(CupidGb *gb, unsigned address, uint8_t value) {
    cupid_gb_write_u8(gb, 0x2000, (uint8_t)(address >> 13));
    cupid_gb_write_u8(gb, (uint16_t)(0x4000u | (address & 0x1fffu)), value);
}

static uint8_t flash_read(CupidGb *gb, unsigned address) {
    cupid_gb_write_u8(gb, 0x2000, (uint8_t)(address >> 13));
    return cupid_gb_read_u8(gb, (uint16_t)(0x4000u | (address & 0x1fffu)));
}

static void command(CupidGb *gb, uint8_t value) {
    flash_write(gb, 0x5555, 0xaa);
    flash_write(gb, 0x2aaa, 0x55);
    flash_write(gb, 0x5555, value);
}

static void extended_command(CupidGb *gb, uint8_t value) {
    command(gb, 0x60);
    command(gb, value);
}

static void leave_flash_mode(CupidGb *gb) {
    flash_write(gb, 0, 0xf0);
}

static void program_page(CupidGb *gb, unsigned address, uint8_t pattern) {
    command(gb, 0xa0);
    for (unsigned i = 0; i < PAGE_BYTES; ++i)
        flash_write(gb, address + i, pattern);
    flash_write(gb, address + PAGE_BYTES - 1u, 0);
    assert(flash_read(gb, address) & 0x90u);
    leave_flash_mode(gb);
}

static void assert_page(CupidGb *gb, unsigned address, uint8_t pattern) {
    for (unsigned i = 0; i < PAGE_BYTES; ++i)
        assert(flash_read(gb, address + i) == pattern);
}

static void erase_sector(CupidGb *gb, unsigned address) {
    /* The final address, unlike the unlock addresses, selects the sector. */
    command(gb, 0x80);
    flash_write(gb, 0x5555, 0xaa);
    flash_write(gb, 0x2aaa, 0x55);
    flash_write(gb, address, 0x30);
    assert(flash_read(gb, address) & 0x90u);
    leave_flash_mode(gb);
}

static void test_windows(CupidGbModel model) {
    CupidGb *gb = machine(model);
    assert(gb->header.mbc_type == CUPID_GB_MBC6);
    assert(cupid_gb_read_u8(gb, 0x4200) == 0);
    assert(cupid_gb_read_u8(gb, 0x6200) == 0);
    for (unsigned bank = 0; bank < 128; ++bank) {
        cupid_gb_write_u8(gb, 0x27ff, (uint8_t)bank);
        cupid_gb_write_u8(gb, 0x37ff, (uint8_t)(127u - bank));
        assert(cupid_gb_read_u8(gb, 0x0200) == 0);
        assert(cupid_gb_read_u8(gb, 0x2200) == 1);
        assert(cupid_gb_read_u8(gb, 0x4200) == bank);
        assert(cupid_gb_read_u8(gb, 0x6200) == 127u - bank);
    }
    cupid_gb_write_u8(gb, 0x03ff, 0x0a);
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(gb, 0x07ff, (uint8_t)bank);
        cupid_gb_write_u8(gb, 0xa000, (uint8_t)(0x31u + bank));
        cupid_gb_write_u8(gb, 0xafff, (uint8_t)(0x51u + bank));
    }
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(gb, 0x0400, (uint8_t)bank);
        cupid_gb_write_u8(gb, 0x0bff, (uint8_t)(7u - bank));
        assert(cupid_gb_read_u8(gb, 0xa000) == 0x31u + bank);
        assert(cupid_gb_read_u8(gb, 0xb000) == 0x38u - bank);
        assert(cupid_gb_read_u8(gb, 0xbfff) == 0x58u - bank);
    }
    cupid_gb_write_u8(gb, 0, 0);
    cupid_gb_write_u8(gb, 0xa000, 0);
    cupid_gb_write_u8(gb, 0xb000, 0);
    cupid_gb_write_u8(gb, 0, 0x0a);
    assert(cupid_gb_read_u8(gb, 0xa000) == 0x38);
    assert(cupid_gb_read_u8(gb, 0xb000) == 0x31);
    assert(cupid_gb_debugger_bank(gb, 0x4000) == 127);
    assert(cupid_gb_debugger_bank(gb, 0x6000) == 0);
    assert(cupid_gb_debugger_bank(gb, 0xa000) == 7);
    assert(cupid_gb_debugger_bank(gb, 0xb000) == 0);
    cupid_gb_write_u8(gb, 0, 0);
    assert(cupid_gb_debug_peek(gb, 0xa000) == 0x38);
    assert(cupid_gb_debug_poke(gb, 0xb000, 0x70));
    cupid_gb_write_u8(gb, 0, 0x0a);
    assert(cupid_gb_read_u8(gb, 0xb000) == 0x70);
    enable_flash(gb);
    assert(flash_read(gb, 0x12345) == 0xff);
    /* B stays on ROM while A addresses flash, then selects the same physical flash. */
    assert(cupid_gb_read_u8(gb, 0x6200) == 0);
    cupid_gb_write_u8(gb, 0x3800, 8);
    assert(cupid_gb_read_u8(gb, 0x6200) == 0xff);
    cupid_gb_write_u8(gb, 0x0fff, 0);
    cupid_gb_write_u8(gb, 0x2fff, 0);
    assert(cupid_gb_read_u8(gb, 0x4200) == (0x12345u >> 13));
    destroy(gb);
}

static void test_guest_windows(CupidGbModel model) {
    CupidGb *gb = machine(model);
    gb->cpu.pc = 0x150;
    for (unsigned steps = 0; steps < 32 && !gb->cpu.halted; ++steps)
        cupid_gb_step(gb);
    assert(gb->cpu.halted);
    assert(gb->work_ram[0] == 0x25 && gb->work_ram[1] == 0x4c);
    assert(gb->cartridge_ram[0x4fdc] == 0xaf);
    assert(gb->cartridge_ram[0x7fdb] == 0x61);
    destroy(gb);
}

static void test_identification_and_program(CupidGbModel model) {
    CupidGb *gb = machine(model);
    enable_flash(gb);
    command(gb, 0x90);
    assert(flash_read(gb, 0) == 0xc2);
    assert(flash_read(gb, 1) == 0x81);
    leave_flash_mode(gb);
    /* Sector 1 remains writable while /WP protects sector 0. */
    program_page(gb, SECTOR_BYTES + 0x180, 0xa5);
    assert_page(gb, SECTOR_BYTES + 0x180, 0xa5);
    assert(flash_read(gb, SECTOR_BYTES + 0x17f) == 0xff);
    assert(flash_read(gb, SECTOR_BYTES + 0x200) == 0xff);
    cupid_gb_write_u8(gb, 0x3000, 16);
    cupid_gb_write_u8(gb, 0x3800, 8);
    assert(cupid_gb_read_u8(gb, 0x6180) == 0xa5);
    command(gb, 0xa0);
    for (unsigned i = 0; i < 37; ++i)
        flash_write(gb, SECTOR_BYTES + 0x280 + i, 0x41);
    leave_flash_mode(gb);
    assert_page(gb, SECTOR_BYTES + 0x280, 0xff);
    /* A complete page also remains pending until the extra confirm write. */
    command(gb, 0xa0);
    for (unsigned i = 0; i < PAGE_BYTES; ++i)
        flash_write(gb, SECTOR_BYTES + 0x280 + i, 0x41);
    leave_flash_mode(gb);
    assert_page(gb, SECTOR_BYTES + 0x280, 0xff);
    /* Main flash cannot restore cleared bits without an erase. */
    program_page(gb, SECTOR_BYTES + 0x180, 0xff);
    assert_page(gb, SECTOR_BYTES + 0x180, 0xa5);
    /* F0 is a literal byte within the aligned page, then a reset at confirmation. */
    program_page(gb, ROM_BYTES - PAGE_BYTES, 0xf0);
    assert_page(gb, ROM_BYTES - PAGE_BYTES, 0xf0);
    command(gb, 0xa0);
    flash_write(gb, SECTOR_BYTES + 0x301, 0x42);
    leave_flash_mode(gb);
    assert_page(gb, SECTOR_BYTES + 0x300, 0xff);
    cupid_gb_write_u8(gb, 0x0c00, 0);
    command(gb, 0x90);
    cupid_gb_write_u8(gb, 0x0c00, 1);
    assert(flash_read(gb, 0) == 0xff);
    assert_page(gb, SECTOR_BYTES + 0x180, 0xa5);
    destroy(gb);
}

static void test_protection_and_hidden(CupidGbModel model) {
    CupidGb *gb = machine(model);
    enable_flash(gb);
    program_page(gb, 0x100, 0x21);
    assert_page(gb, 0x100, 0xff);
    cupid_gb_write_u8(gb, 0x1000, 1);
    extended_command(gb, 0x40);
    leave_flash_mode(gb);
    program_page(gb, 0x100, 0x21);
    assert_page(gb, 0x100, 0x21);
    extended_command(gb, 0x20);
    assert(flash_read(gb, 0) & 2u);
    leave_flash_mode(gb);
    erase_sector(gb, 0);
    assert_page(gb, 0x100, 0x21);
    /* Hidden storage has its own program command and ignores sector 0 protection. */
    extended_command(gb, 0xe0);
    for (unsigned i = 0; i < PAGE_BYTES; ++i)
        flash_write(gb, 0x80 + i, 0x69);
    flash_write(gb, 0xff, 0);
    assert(flash_read(gb, 0) & 0x80u);
    leave_flash_mode(gb);
    command(gb, 0x77);
    command(gb, 0x77);
    assert_page(gb, 0x80, 0x69);
    assert_page(gb, 0, 0xff);
    leave_flash_mode(gb);
    /* Chip erase skips a protected first sector and always preserves hidden storage. */
    program_page(gb, SECTOR_BYTES, 0x35);
    command(gb, 0x80);
    command(gb, 0x10);
    assert(flash_read(gb, SECTOR_BYTES) & 0x80u);
    leave_flash_mode(gb);
    assert_page(gb, 0x100, 0x21);
    assert_page(gb, SECTOR_BYTES, 0xff);
    command(gb, 0x77);
    command(gb, 0x77);
    assert_page(gb, 0x80, 0x69);
    leave_flash_mode(gb);
    extended_command(gb, 0x40);
    leave_flash_mode(gb);
    erase_sector(gb, 0);
    assert_page(gb, 0x100, 0xff);
    extended_command(gb, 0x04);
    leave_flash_mode(gb);
    command(gb, 0x77);
    command(gb, 0x77);
    assert_page(gb, 0x80, 0xff);
    leave_flash_mode(gb);
    destroy(gb);
}

static uint8_t *snapshot(CupidGb *gb, size_t *size) {
    *size = cupid_gb_state_size(gb);
    uint8_t *bytes = malloc(*size);
    size_t written = 0;
    assert(bytes);
    assert(cupid_gb_save_state(gb, bytes, *size, &written) == CUPID_GB_STATE_OK);
    assert(written == *size);
    return bytes;
}

static void assert_snapshot(CupidGb *gb, const uint8_t *bytes, size_t size) {
    size_t actual_size;
    uint8_t *actual = snapshot(gb, &actual_size);
    assert(actual_size == size && !memcmp(bytes, actual, size));
    free(actual);
}

static void test_persistence(CupidGbModel model) {
    CupidGb *first = machine(model), *second = machine(model);
    enable_flash(first);
    program_page(first, SECTOR_BYTES, 0x51);
    cupid_gb_write_u8(first, 0x1000, 1);
    extended_command(first, 0x20);
    leave_flash_mode(first);
    extended_command(first, 0xe0);
    for (unsigned i = 0; i < PAGE_BYTES; ++i)
        flash_write(first, i, 0x44);
    flash_write(first, PAGE_BYTES - 1u, 0);
    leave_flash_mode(first);
    cupid_gb_write_u8(first, 0, 0x0a);
    cupid_gb_write_u8(first, 0x0400, 7);
    cupid_gb_write_u8(first, 0xa012, 0x37);
    const size_t size = cupid_gb_battery_size(first);
    uint8_t *battery = malloc(size);
    size_t written = 0;
    assert(battery && size > ROM_BYTES);
    assert(cupid_gb_save_battery(first, battery, size, &written) == CUPID_GB_BATTERY_OK);
    assert(written == size);
    size_t before_size;
    uint8_t *before = snapshot(second, &before_size);
    assert(cupid_gb_load_battery(second, battery, size - 1u) == CUPID_GB_BATTERY_INVALID_SIZE);
    assert_snapshot(second, before, before_size);
    const size_t trailer = first->cartridge_ram_size;
    const unsigned invalid_offsets[] = {0, 4, 5, 6, 7};
    for (unsigned i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i) {
        const size_t offset = trailer + invalid_offsets[i];
        const uint8_t original = battery[offset];
        battery[offset] = 0xff;
        assert(cupid_gb_load_battery(second, battery, size) == CUPID_GB_BATTERY_INVALID_FORMAT);
        assert_snapshot(second, before, before_size);
        battery[offset] = original;
    }
    free(before);
    assert(cupid_gb_load_battery(second, battery, size) == CUPID_GB_BATTERY_OK);
    enable_flash(second);
    assert_page(second, SECTOR_BYTES, 0x51);
    cupid_gb_write_u8(second, 0x1000, 1);
    program_page(second, 0, 0x23);
    assert_page(second, 0, 0xff);
    command(second, 0x77);
    command(second, 0x77);
    assert_page(second, 0, 0x44);
    leave_flash_mode(second);
    cupid_gb_write_u8(second, 0, 0x0a);
    cupid_gb_write_u8(second, 0x0800, 7);
    assert(cupid_gb_read_u8(second, 0xb012) == 0x37);
    for (unsigned kind = CUPID_GB_RESET_HARD; kind <= CUPID_GB_RESET_QUICK; ++kind) {
        assert(cupid_gb_reset(second, (CupidGbResetKind)kind));
        assert(cupid_gb_read_u8(second, 0x4200) == 0);
        enable_flash(second);
        assert_page(second, SECTOR_BYTES, 0x51);
        cupid_gb_write_u8(second, 0x1000, 1);
        program_page(second, 0, 0x23);
        assert_page(second, 0, 0xff);
        command(second, 0x77);
        command(second, 0x77);
        assert_page(second, 0, 0x44);
        leave_flash_mode(second);
        cupid_gb_write_u8(second, 0, 0x0a);
        cupid_gb_write_u8(second, 0x0400, 7);
        assert(cupid_gb_read_u8(second, 0xa012) == 0x37);
    }
    free(battery);
    destroy(first);
    destroy(second);
}

static void test_pending_continuation(CupidGbModel model) {
    CupidGb *first = machine(model), *second = machine(model);
    enable_flash(first);
    command(first, 0xa0);
    for (unsigned i = 0; i < 63; ++i)
        flash_write(first, SECTOR_BYTES + 0x80 + i, 0x29);
    size_t size;
    uint8_t *saved = snapshot(first, &size);
    assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_OK);
    assert_snapshot(second, saved, size);
    for (unsigned i = 63; i < PAGE_BYTES; ++i) {
        flash_write(first, SECTOR_BYTES + 0x80 + i, 0x29);
        flash_write(second, SECTOR_BYTES + 0x80 + i, 0x29);
    }
    flash_write(first, SECTOR_BYTES + 0xff, 0);
    flash_write(second, SECTOR_BYTES + 0xff, 0);
    leave_flash_mode(first);
    leave_flash_mode(second);
    assert_page(first, SECTOR_BYTES + 0x80, 0x29);
    assert_page(second, SECTOR_BYTES + 0x80, 0x29);
    free(saved);
    saved = snapshot(first, &size);
    assert_snapshot(second, saved, size);
    /* Integrity rejection must retain already programmed flash and controller history. */
    saved[size - 1u] ^= 1u;
    assert(cupid_gb_load_state(second, saved, size) != CUPID_GB_STATE_OK);
    saved[size - 1u] ^= 1u;
    assert_snapshot(second, saved, size);
    assert(cupid_gb_load_state_live(second, saved, size) == CUPID_GB_STATE_OK);
    assert_page(second, SECTOR_BYTES + 0x80, 0x29);
    free(saved);
    destroy(first);
    destroy(second);
}

static void test_legacy_state(CupidGbModel model) {
    enum {
        OLD_ROM_BANK = 249906,
        OLD_RAM_BANK = OLD_ROM_BANK + 8,
        SRAM_ENABLED = OLD_RAM_BANK + 11,
        ACCESSORY_BYTES = 8,
        FLASH_PAYLOAD = CUPID_GB_MBC6_STORAGE_SIZE + PAGE_BYTES + 18
    };

    CupidGb *gb = machine(model);
    enable_flash(gb);
    program_page(gb, SECTOR_BYTES, 0x25);
    cupid_gb_write_u8(gb, 0, 0x0a);
    cupid_gb_write_u8(gb, 0x0400, 6);
    cupid_gb_write_u8(gb, 0x0800, 7);
    cupid_gb_write_u8(gb, 0xa012, 0x13);
    cupid_gb_write_u8(gb, 0xb012, 0xd7);
    size_t size;
    uint8_t *legacy = snapshot(gb, &size);
    size -= FLASH_PAYLOAD;
    memcpy(legacy + size - ACCESSORY_BYTES, legacy + size - ACCESSORY_BYTES + FLASH_PAYLOAD,
           ACCESSORY_BYTES);
    legacy[8] = 24;
    for (unsigned i = 0; i < 4; ++i)
        legacy[12 + i] = (uint8_t)(size >> (i * 8u));
    legacy[OLD_ROM_BANK] = 7;
    legacy[OLD_RAM_BANK] = 3;
    assert(legacy[SRAM_ENABLED] == 1);
    cupid_sha256_parts(legacy, 96, legacy + 128, size - 128, legacy + 96);
    assert(cupid_gb_load_state(gb, legacy, size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_read_u8(gb, 0x4200) == 14);
    assert(cupid_gb_read_u8(gb, 0x6200) == 15);
    assert(cupid_gb_read_u8(gb, 0xa012) == 0x13);
    assert(cupid_gb_read_u8(gb, 0xb012) == 0xd7);
    enable_flash(gb);
    assert_page(gb, SECTOR_BYTES, 0xff);
    free(legacy);
    destroy(gb);
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        test_windows(models[i]);
        test_guest_windows(models[i]);
        test_identification_and_program(models[i]);
        test_protection_and_hidden(models[i]);
        test_persistence(models[i]);
        test_pending_continuation(models[i]);
        test_legacy_state(models[i]);
    }
    return 0;
}

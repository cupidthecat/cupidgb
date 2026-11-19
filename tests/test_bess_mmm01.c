/**
 * @file    test_bess_mmm01.c
 * @brief   Portable menu identity and guest MMM01 lock/mask continuation.
 * @author  cupidthecat
 * @date    2026-11-19
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

#include "cupid/gb/bess.h"
#include "cupid/gb/cartridge.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BANK_BYTES = 0x4000, BANKS = 512, ROM_BYTES = BANK_BYTES * BANKS };

static void header(uint8_t *rom, size_t offset, const char *title, uint8_t type,
                   uint16_t checksum) {
    memcpy(rom + offset + 0x134, title, strlen(title));
    rom[offset + 0x143] = 0x80;
    rom[offset + 0x147] = type;
    rom[offset + 0x148] = 8;
    rom[offset + 0x149] = 4;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[offset + 0x14d] = (uint8_t)(rom[offset + 0x14d] - rom[offset + i] - 1u);
    rom[offset + 0x14e] = (uint8_t)(checksum >> 8);
    rom[offset + 0x14f] = (uint8_t)checksum;
}

static CupidGb *machine(CupidGbModel model, bool menu_first) {
    uint8_t *rom = calloc(1, ROM_BYTES);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(rom && gb);
    for (unsigned bank = 0; bank < BANKS; ++bank) {
        const unsigned file_bank = menu_first ? (bank + 2u) % BANKS : bank;
        rom[file_bank * BANK_BYTES + 0x1000] = (uint8_t)bank;
        rom[file_bank * BANK_BYTES + 0x1001] = (uint8_t)(bank >> 8);
    }
    header(rom, menu_first ? 0x8000 : 0, "PORTABLE GAME", 3, 0x1a2b);
    header(rom, menu_first ? 0 : ROM_BYTES - 0x8000, "PORTABLE MENU", 0x0d, 0x9c3d);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 7);
    assert(cupid_gb_load_rom(gb, rom, ROM_BYTES));
    assert(gb->header.mbc_type == CUPID_GB_MMM01 && gb->mmm01_menu_first == menu_first);
    assert(!memcmp(gb->rom, rom, ROM_BYTES));
    for (size_t i = 0; i < gb->cartridge_ram_size; ++i)
        cupid_gb_cartridge_ram(gb)[i] = (uint8_t)(i / 0x2000u);
    free(rom);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void execute(CupidGb *gb, const uint8_t *program, size_t size) {
    for (size_t i = 0; i < size; ++i)
        cupid_gb_write_u8(gb, (uint16_t)(0xc100u + i), program[i]);
    gb->cpu.pc = 0xc100;
    assert(cupid_gb_step(gb) && cupid_gb_step(gb));
    assert(gb->cpu.pc == 0xc100u + size);
}

static void store(CupidGb *gb, uint16_t address, uint8_t value) {
    const uint8_t program[] = {0x3e, value, 0xea, (uint8_t)address, (uint8_t)(address >> 8)};
    execute(gb, program, sizeof(program));
}

static uint8_t sample(CupidGb *gb, uint16_t address) {
    const uint8_t program[] = {0xfa, (uint8_t)address, (uint8_t)(address >> 8), 0xea, 0, 0xc8};
    execute(gb, program, sizeof(program));
    return cupid_gb_read_u8(gb, 0xc800);
}

static unsigned bank(CupidGb *gb, uint16_t address) {
    const unsigned low = sample(gb, address);
    return low | (unsigned)sample(gb, (uint16_t)(address + 1u)) << 8;
}

static void configure(CupidGb *gb, unsigned profile) {
    store(gb, 0xffff, 0);
    store(gb, 0xff40, 0);
    if (!profile)
        return;
    store(gb, 0x0000, 0x3a);
    store(gb, 0x2000, 0x55);
    /* Hold ROM bit four, keep mode one, then disable future mode writes. */
    store(gb, 0x6000, 0x61);
    store(gb, 0x4000, 0x6e);
    /* RAM low bits were set while writable. Their mask now holds both. */
    store(gb, 0x0000, profile == 1 ? 0x0a : profile == 2 ? 0x4a : 0x40);
}

static void same_mapper(const CupidGb *a, const CupidGb *b) {
    assert(a->mmm01_locked == b->mmm01_locked);
    assert(a->mmm01_rom_select == b->mmm01_rom_select);
    assert(a->mmm01_bank_select == b->mmm01_bank_select);
    assert(a->mmm01_mode == b->mmm01_mode);
    assert(a->mmm01_ram_mask == b->mmm01_ram_mask);
    assert(a->ram_enabled == b->ram_enabled);
}

static unsigned read_long(const uint8_t *bytes) {
    return (unsigned)bytes[0] | (unsigned)bytes[1] << 8 | (unsigned)bytes[2] << 16 |
           (unsigned)bytes[3] << 24;
}

static uint8_t *info_block(uint8_t *bytes, size_t size) {
    size_t cursor = read_long(bytes + size - 8);
    while (cursor < size - 8) {
        const unsigned count = read_long(bytes + cursor + 4);
        if (!memcmp(bytes + cursor, "INFO", 4)) {
            assert(count == 18);
            return bytes + cursor + 8;
        }
        cursor += 8 + count;
    }
    assert(!"Missing portable cartridge identity");
    return NULL;
}

static uint8_t *native(CupidGb *gb, size_t *size) {
    *size = cupid_gb_state_size(gb);
    uint8_t *bytes = malloc(*size);
    size_t written;
    assert(bytes && cupid_gb_save_state(gb, bytes, *size, &written) == CUPID_GB_STATE_OK);
    assert(written == *size);
    return bytes;
}

static void continuation(CupidGb *gb) {
    store(gb, 0x0000, 0x4a);
    store(gb, 0x0000, 0x3a); /* A locked mapper cannot be unlocked. */
    store(gb, 0x6000, 0x3d);
    store(gb, 0x4000, 0x11);
    store(gb, 0x2000, 2);
}

static void exchange(CupidGbModel model, bool menu_first, unsigned profile) {
    CupidGb *source = machine(model, menu_first);
    CupidGb *target = machine(model, !menu_first);
    configure(source, profile);
    const size_t logical_menu = menu_first ? 0 : ROM_BYTES - 0x8000;
    uint8_t expected[18];
    memcpy(expected, source->rom + logical_menu + 0x134, 16);
    memcpy(expected + 16, source->rom + logical_menu + 0x14e, 2);
    const size_t size = cupid_gb_bess_size(source);
    uint8_t *bytes = malloc(size + 2);
    size_t written;
    assert(size && bytes);
    bytes[0] = bytes[size + 1] = 0xa5;
    assert(cupid_gb_bess_save(source, bytes + 1, size, &written) == CUPID_GB_STATE_OK);
    assert(written == size && bytes[0] == 0xa5 && bytes[size + 1] == 0xa5);
    uint8_t *info = info_block(bytes + 1, size);
    assert(!memcmp(info, expected, sizeof(expected)));
    assert(!memcmp(source->rom + logical_menu + 0x134, expected, 16));
    assert(cupid_gb_bess_load(target, bytes + 1, size) == CUPID_GB_STATE_OK);
    same_mapper(source, target);
    assert(target->mmm01_menu_first != source->mmm01_menu_first);
    assert(bank(source, 0x1000) == (profile < 2 ? 510u : 272u));
    assert(bank(target, 0x1000) == (profile < 2 ? 510u : 272u));
    assert(bank(source, 0x5000) == (profile < 2 ? 511u : 341u));
    assert(bank(target, 0x5000) == (profile < 2 ? 511u : 341u));
    assert(sample(source, 0xa000) == sample(target, 0xa000));

    size_t native_size;
    uint8_t *before = native(target, &native_size);
    info[0] ^= 1;
    assert(cupid_gb_bess_load(target, bytes + 1, size) == CUPID_GB_STATE_WRONG_ROM);
    size_t after_size;
    uint8_t *after = native(target, &after_size);
    assert(native_size == after_size && !memcmp(before, after, native_size));
    assert(cupid_gb_load_state(target, before, native_size) == CUPID_GB_STATE_OK);
    info[0] ^= 1;
    free(before);
    free(after);

    continuation(source);
    continuation(target);
    same_mapper(source, target);
    assert(source->mmm01_locked && target->mmm01_locked);
    assert(bank(source, 0x1000) == bank(target, 0x1000));
    assert(bank(source, 0x5000) == bank(target, 0x5000));
    assert(sample(source, 0xa000) == sample(target, 0xa000));
    if (profile) {
        assert(source->mmm01_mode == 0x61 && source->mmm01_rom_select == 0x52);
        assert(source->mmm01_bank_select == 0x6f && source->mmm01_ram_mask == 0);
        assert(bank(target, 0x1000) == 272 && bank(target, 0x5000) == 370);
    }
    before = native(target, &native_size);
    assert(cupid_gb_load_state(target, before, native_size) == CUPID_GB_STATE_OK);
    free(before);
    free(bytes);
    destroy(source);
    destroy(target);
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB_E};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model)
        for (unsigned layout = 0; layout < 2; ++layout)
            for (unsigned profile = 0; profile < 4; ++profile)
                exchange(models[model], layout != 0, profile);
    puts("MMM01 portable identity, raw state and locked guest continuation: 16 exchanges passed.");
    return 0;
}

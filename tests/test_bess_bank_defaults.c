/**
 * @file    test_bess_bank_defaults.c
 * @brief   Portable bank defaults and exact native mapping-latch continuation.
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
#include "cupid/gb/reset.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t size;
} Blob;

static CupidGb *machine(CupidGbModel model, uint8_t type) {
    const unsigned banks = type == 0x22 ? 256 : 512;
    uint8_t *rom = calloc(banks, 0x4000);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(rom && gb);
    for (unsigned bank = 0; bank < banks; ++bank) {
        rom[bank * 0x4000u + 0x200] = (uint8_t)bank;
        rom[bank * 0x4000u + 0x201] = (uint8_t)(bank >> 8);
        rom[bank * 0x4000u + 0x1300] = (uint8_t)(bank ^ 0xa55a);
        rom[bank * 0x4000u + 0x1301] = (uint8_t)((bank ^ 0xa55a) >> 8);
    }
    memcpy(rom + 0x134, "BANK DEFAULTS", 13);
    rom[0x143] = 0x80;
    rom[0x147] = type;
    rom[0x148] = type == 0x22 ? 7 : 8;
    rom[0x149] = type == 0x22 ? 0 : 3;
    for (unsigned i = 0x134; i < 0x14d; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 7);
    assert(cupid_gb_load_rom(gb, rom, banks * 0x4000u));
    cupid_gb_write_u8(gb, 0xffff, 0);
    cupid_gb_write_u8(gb, 0xff40, 0);
    free(rom);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void execute(CupidGb *gb, const uint8_t *code, size_t size) {
    for (size_t i = 0; i < size; ++i)
        cupid_gb_write_u8(gb, (uint16_t)(0xc100 + i), code[i]);
    gb->cpu.pc = 0xc100;
    assert(cupid_gb_step(gb) && cupid_gb_step(gb));
    assert(gb->cpu.pc == 0xc100 + size);
}

static void store(CupidGb *gb, uint16_t address, uint8_t value) {
    const uint8_t code[] = {0x3e, value, 0xea, (uint8_t)address, (uint8_t)(address >> 8)};
    execute(gb, code, sizeof(code));
}

static uint8_t sample(CupidGb *gb, uint16_t address) {
    const uint8_t code[] = {0xfa, (uint8_t)address, (uint8_t)(address >> 8), 0xea, 0, 0xc8};
    execute(gb, code, sizeof(code));
    return cupid_gb_read_u8(gb, 0xc800);
}

static unsigned word(CupidGb *gb, uint16_t address) {
    const unsigned low = sample(gb, address);
    return low | (unsigned)sample(gb, (uint16_t)(address + 1)) << 8;
}

static void expect_bank(CupidGb *gb, unsigned bank) {
    assert(word(gb, 0x4200) == bank);
    assert(word(gb, 0x5300) == (bank ^ 0xa55a));
}

static unsigned read_long(const uint8_t *p) {
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void put_long(uint8_t *p, unsigned value) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (i * 8u));
}

static size_t mapper_block(const Blob *blob) {
    for (size_t at = read_long(blob->data + blob->size - 8); at + 8 <= blob->size - 8;) {
        if (!memcmp(blob->data + at, "MBC ", 4))
            return at;
        at += 8 + read_long(blob->data + at + 4);
    }
    return 0;
}

static Blob portable(CupidGb *gb) {
    Blob result = {NULL, cupid_gb_bess_size(gb)};
    size_t written = 0;
    result.data = malloc(result.size);
    assert(result.data && result.size);
    assert(cupid_gb_bess_save(gb, result.data, result.size, &written) == CUPID_GB_STATE_OK);
    assert(written == result.size);
    return result;
}

static Blob controls(const Blob *source, bool omitted, bool pair, unsigned address,
                     unsigned value) {
    const size_t at = mapper_block(source);
    assert(at);
    const size_t old_size = 8 + read_long(source->data + at + 4);
    const size_t new_size = omitted ? 0 : pair ? 11 : 8;
    Blob result = {malloc(source->size - old_size + new_size), source->size - old_size + new_size};
    assert(result.data);
    memcpy(result.data, source->data, at);
    memcpy(result.data + at + new_size, source->data + at + old_size, source->size - at - old_size);
    if (!omitted) {
        memcpy(result.data + at, "MBC ", 4);
        put_long(result.data + at + 4, pair ? 3 : 0);
        if (pair) {
            result.data[at + 8] = (uint8_t)address;
            result.data[at + 9] = (uint8_t)(address >> 8);
            result.data[at + 10] = (uint8_t)value;
        }
    }
    return result;
}

static CupidGb *native_clone(CupidGb *gb, CupidGbModel model, uint8_t type) {
    const size_t size = cupid_gb_state_size(gb);
    uint8_t *data = malloc(size), *again = malloc(size);
    size_t used = 0;
    assert(data && again && CUPID_GB_STATE_VERSION == 30);
    assert(cupid_gb_save_state(gb, data, size, &used) == CUPID_GB_STATE_OK && used == size);
    CupidGb *copy = machine(model, type);
    assert(cupid_gb_load_state(copy, data, size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_state_size(copy) == size);
    assert(cupid_gb_save_state(copy, again, size, &used) == CUPID_GB_STATE_OK && used == size);
    assert(!memcmp(data, again, size));
    free(data);
    free(again);
    return copy;
}

static void fresh_defaults(CupidGbModel model, uint8_t type) {
    CupidGb *gb = machine(model, type);
    const unsigned initial = type == 0x22 ? 0 : 1;
    expect_bank(gb, initial);
    assert(gb->mbc5_rom_bank == 1);
    const CupidGbResetKind resets[] = {CUPID_GB_RESET_HARD, CUPID_GB_RESET_QUICK};
    for (size_t i = 0; i < sizeof(resets) / sizeof(resets[0]); ++i) {
        store(gb, 0x2000, 0xa5);
        assert(cupid_gb_reset(gb, resets[i]));
        expect_bank(gb, initial);
        assert(gb->mbc5_rom_bank == 1);
    }
    destroy(gb);
}

static void partial_imports(CupidGbModel model, uint8_t type) {
    const struct {
        bool omitted, pair, pending;
        uint16_t address;
        uint8_t value;
        unsigned bank;
    } cases[] = {{true, false, true, 0, 0, 1},
                 {false, false, true, 0, 0, 1},
                 {false, true, true, 0xa080, 0x82, 1},
                 {false, true, false, 0, 0x0a, 0},
                 {false, true, false, 0x3000, 1, type == 0x22 ? 1u : 256u},
                 {false, true, false, 0x4000, 1, 0},
                 {false, true, false, 0x4000, 0x40, 0},
                 {false, true, false, 0x6000, 0xa5, 0},
                 {false, true, false, 0x2000, 0, 0},
                 {false, true, false, 0x2000, 1, 1}};

    CupidGb *source = machine(model, type);
    Blob original = portable(source);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Blob fixture =
            controls(&original, cases[i].omitted, cases[i].pair, cases[i].address, cases[i].value);
        CupidGb *gb = machine(model, type);
        assert(cupid_gb_bess_load(gb, fixture.data, fixture.size) == CUPID_GB_STATE_OK);
        expect_bank(gb, cases[i].bank);
        if (cases[i].pending)
            assert(gb->mbc5_rom_bank == 0 && gb->current_rom_bank == 1);
        CupidGb *copy = native_clone(gb, model, type);
        expect_bank(copy, cases[i].bank);
        if (cases[i].pending) {
            Blob exported = portable(copy);
            assert(!mapper_block(&exported));
            assert(cupid_gb_bess_load(copy, exported.data, exported.size) == CUPID_GB_STATE_OK);
            const uint16_t unrelated[] = {0x8000, 0xa080, 0xc300, 0xff01};
            for (size_t j = 0; j < sizeof(unrelated) / sizeof(unrelated[0]); ++j) {
                store(copy, unrelated[j], 0x82);
                expect_bank(copy, 1);
            }
            store(gb, 0x6000, 0xa5);
            store(copy, 0, 0x0a);
            expect_bank(gb, 0);
            expect_bank(copy, 0);
            free(exported.data);
        } else {
            store(gb, 0, 0x0a);
            store(copy, 0x6000, 0xa5);
            expect_bank(gb, cases[i].bank);
            expect_bank(copy, cases[i].bank);
        }
        destroy(copy);
        destroy(gb);
        free(fixture.data);
    }
    free(original.data);
    destroy(source);
}

static void legacy_mbc7(CupidGbModel model) {
    const unsigned banks[] = {0, 1, 3, 255};
    for (size_t i = 0; i < sizeof(banks) / sizeof(banks[0]); ++i) {
        CupidGb *gb = machine(model, 0x22);
        store(gb, 0x2000, (uint8_t)banks[i]);
        gb->mbc5_rom_bank = 1; /* The otherwise unused field in older version-30 files. */
        CupidGb *copy = native_clone(gb, model, 0x22);
        assert(copy->mbc5_rom_bank == 1);
        expect_bank(copy, banks[i]);
        store(copy, 0, 0x0a);
        store(copy, 0x6000, 0xa5);
        expect_bank(copy, banks[i]);
        destroy(copy);
        destroy(gb);
    }
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB_E};
    const uint8_t types[] = {0x1b, 0x1e, 0x22};
    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        for (size_t j = 0; j < sizeof(types) / sizeof(types[0]); ++j) {
            fresh_defaults(models[i], types[j]);
            partial_imports(models[i], types[j]);
        }
        legacy_mbc7(models[i]);
    }
    puts("Portable bank defaults: 60 imports, fresh/reset markers and exact v30 continuations "
         "passed.");
    return 0;
}

/**
 * @file    test_mmm01.c
 * @brief   Executed guest regressions for MMM01 upper address lines and lock wiring.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "cupid/gb/gb.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BANK_SIZE = 0x4000, BANK_COUNT = 512, PROGRAM_ADDRESS = 0xc100 };

static void store_instruction(uint8_t *program, size_t *length, uint16_t address, uint8_t value) {
    const uint8_t bytes[] = {0x3e, value, 0xea, (uint8_t)address, (uint8_t)(address >> 8)};
    memcpy(program + *length, bytes, sizeof(bytes));
    *length += sizeof(bytes);
}

static void sample_instruction(uint8_t *program, size_t *length, uint16_t address,
                               uint16_t destination) {
    const uint8_t bytes[] = {0xfa, (uint8_t)address,     (uint8_t)(address >> 8),
                             0xea, (uint8_t)destination, (uint8_t)(destination >> 8)};
    memcpy(program + *length, bytes, sizeof(bytes));
    *length += sizeof(bytes);
}

static void execute(CupidGb *gb, const uint8_t *program, size_t length) {
    assert(length < 0x600);
    for (size_t i = 0; i < length; ++i)
        cupid_gb_write_u8(gb, (uint16_t)(PROGRAM_ADDRESS + i), program[i]);
    gb->cpu.pc = PROGRAM_ADDRESS;
    for (size_t steps = 0; steps <= length; ++steps) {
        if (gb->cpu.pc == PROGRAM_ADDRESS + length)
            return;
        assert(cupid_gb_step(gb));
    }
    assert(!"Guest program exceeded instruction bound");
}

static uint8_t *make_rom(CupidGbModel model, bool menu_first) {
    const size_t size = BANK_COUNT * BANK_SIZE;
    uint8_t *rom = calloc(1, size);
    assert(rom);
    for (unsigned bank = 0; bank < BANK_COUNT; ++bank) {
        /* Two independent bytes expose the ninth ROM address bit. Menu-first
         * files rotate the two menu banks while retaining physical identities. */
        const unsigned file_bank = menu_first ? (bank + 2u) % BANK_COUNT : bank;
        rom[file_bank * BANK_SIZE + 0x1000] = (uint8_t)bank;
        rom[file_bank * BANK_SIZE + 0x1001] = (uint8_t)(bank >> 8);
    }
    const size_t menu = menu_first ? 0 : size - 2 * BANK_SIZE;
    memcpy(rom + menu + 0x134, "MMM01 WIRING", 12);
    rom[menu + 0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[menu + 0x147] = 0x0d;
    rom[menu + 0x148] = 8;
    rom[menu + 0x149] = 4;
    uint8_t checksum = 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        checksum = (uint8_t)(checksum - rom[menu + i] - 1);
    rom[menu + 0x14d] = checksum;
    return rom;
}

static void check_bank(const CupidGb *gb, unsigned slot, unsigned expected) {
    const unsigned address = 0xc800u + slot * 2u;
    const unsigned actual = cupid_gb_read_u8(gb, (uint16_t)address) |
                            ((unsigned)cupid_gb_read_u8(gb, (uint16_t)(address + 1)) << 8);
    if (actual != expected)
        fprintf(stderr, "MMM01 bank slot %u expected %u, observed %u\n", slot, expected, actual);
    assert(actual == expected);
}

static void sample_banks(uint8_t *program, size_t *length, unsigned slot) {
    sample_instruction(program, length, 0x1000, (uint16_t)(0xc800 + slot * 4));
    sample_instruction(program, length, 0x1001, (uint16_t)(0xc801 + slot * 4));
    sample_instruction(program, length, 0x5000, (uint16_t)(0xc802 + slot * 4));
    sample_instruction(program, length, 0x5001, (uint16_t)(0xc803 + slot * 4));
}

static void test_wiring(CupidGbModel model, bool menu_first) {
    uint8_t *rom = make_rom(model, menu_first);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, BANK_COUNT * BANK_SIZE));
    assert(gb->header.mbc_type == CUPID_GB_MMM01);
    const unsigned masks[] = {0, 2, 6, 14, 30};
    const uint8_t lows[] = {0, 1, 2, 7, 16, 31};
    unsigned scenarios = 0;
    for (unsigned upper = 0; upper < 4; ++upper) {
        for (unsigned middle = 0; middle < 4; ++middle) {
            for (unsigned mask_index = 0; mask_index < 5; ++mask_index) {
                for (unsigned multiplex = 0; multiplex < 2; ++multiplex) {
                    for (unsigned disable = 0; disable < 2; ++disable) {
                        assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
                        cupid_gb_write_u8(gb, 0xff40, 0);
                        cupid_gb_write_u8(gb, 0xffff, 0);
                        const unsigned held = masks[mask_index];
                        const uint8_t config = (uint8_t)(held * 2u | multiplex * 0x40u);
                        uint8_t program[0x600];
                        size_t length = 0;
                        sample_banks(program, &length, 0);
                        /* The menu programs masks and upper address lines before
                         * its irreversible lock. Subsequent writes must retain them. */
                        store_instruction(program, &length, 0x0000, 0x3a);
                        store_instruction(program, &length, 0x2000, (uint8_t)(middle * 32u | 21u));
                        store_instruction(program, &length, 0x4000,
                                          (uint8_t)(upper * 16u | disable * 0x40u | 14u));
                        store_instruction(program, &length, 0x6000, config);
                        store_instruction(program, &length, 0x0000, 0x7a);
                        for (unsigned mode = 0; mode < 2; ++mode) {
                            /* Attempt to replace all locked configuration bits. */
                            store_instruction(program, &length, 0x6000, (uint8_t)(0x3eu | mode));
                            store_instruction(program, &length, 0x4000, 0x33);
                            for (unsigned n = 0; n < sizeof(lows); ++n) {
                                store_instruction(program, &length, 0x2000, lows[n]);
                                sample_banks(program, &length, 1u + mode * 6u + n);
                            }
                        }
                        execute(gb, program, length);
                        check_bank(gb, 0, 510);
                        check_bank(gb, 1, 511);
                        for (unsigned mode = 0; mode < 2; ++mode) {
                            for (unsigned n = 0; n < sizeof(lows); ++n) {
                                const unsigned low = (21u & held) | (lows[n] & (31u ^ held));
                                const unsigned fixed_middle =
                                    multiplex ? ((mode && !disable) ? 0u : 96u) : middle * 32u;
                                const unsigned switched_middle = multiplex ? 96u : middle * 32u;
                                const unsigned fixed = upper * 128u | fixed_middle | (low & held);
                                unsigned switched = upper * 128u | switched_middle | low;
                                if (switched == fixed)
                                    ++switched;
                                check_bank(gb, (1u + mode * 6u + n) * 2u, fixed);
                                check_bank(gb, (1u + mode * 6u + n) * 2u + 1u, switched);
                            }
                        }
                        ++scenarios;
                    }
                }
            }
        }
    }
    /* Restore the final locked, multiplexed upper-bank configuration into a
     * reset receiver, then change its writable selector through guest stores. */
    const size_t state_size = cupid_gb_state_size(gb);
    uint8_t *state = malloc(state_size);
    assert(state && cupid_gb_save_state(gb, state, state_size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
    assert(cupid_gb_load_state(gb, state, state_size) == CUPID_GB_STATE_OK);
    uint8_t suffix[64];
    size_t suffix_length = 0;
    store_instruction(suffix, &suffix_length, 0x0000, 0x3a);
    store_instruction(suffix, &suffix_length, 0x6000, 1);
    store_instruction(suffix, &suffix_length, 0x2000, 0);
    store_instruction(suffix, &suffix_length, 0x4000, 2);
    sample_banks(suffix, &suffix_length, 0);
    execute(gb, suffix, suffix_length);
    check_bank(gb, 0, 468);
    check_bank(gb, 1, 469);
    free(state);
    printf("MMM01 %s layout %u: %u executed guest scenarios passed.\n",
           model == CUPID_GB_MODEL_CGB ? "CGB" : "DMG", menu_first, scenarios);
    cupid_gb_cleanup(gb);
    free(gb);
    free(rom);
}

int main(void) {
    for (unsigned model = 0; model < 2; ++model)
        for (unsigned layout = 0; layout < 2; ++layout)
            test_wiring(model ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC, layout != 0);
    return 0;
}

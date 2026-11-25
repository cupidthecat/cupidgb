/**
 * @file    test_oam_bus.c
 * @brief   Guest OAM read ownership, passive inspection, and color-hardware controls.
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

#include "../src/gb/display_clock.h"
#include "gb_test_helpers.h"
#include <stdio.h>

static CupidGb *prepare(CupidGbModel model, unsigned dot, uint8_t opcode) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t rom[32768];
    assert(gb);
    build_idle_rom(rom, sizeof(rom));
    rom[0x14d] = compute_header_checksum(rom);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->cpu.ime = false;
    gb->cpu.pc = 0xff80;
    gb->cpu.h = 0xfe;
    gb->cpu.l = 0x20;
    gb->high_ram[0] = opcode;
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    /* Advance through the startup line using the ordinary peripheral clocks. */
    do {
        cupid_gb_tick(gb, 1);
        cupid_gb_display_clock_sync(gb);
    } while (gb->pixels.line != 1 || gb->pixels.dot < dot);
    assert(gb->pixels.dot >= dot && gb->pixels.dot < dot + 4);
    for (unsigned i = 0; i < 160; ++i)
        gb->object_attribute_memory[i] = (uint8_t)((i * 37u) ^ (i >> 1u) ^ 0x5au);
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_passive_and_ordinary(void) {
    CupidGb *gb = prepare(CUPID_GB_MODEL_DMG_ABC, 11, 0x7e);
    uint8_t original[160];
    memcpy(original, gb->object_attribute_memory, 160);
    assert(cupid_gb_read_u8(gb, 0xfe20) == 0xff);
    assert(memcmp(original, gb->object_attribute_memory, 160) == 0);
    /* Dot 11 selects row 3. Both row 2 and row 3 receive the coupled word. */
    uint8_t expected[160];
    memcpy(expected, original, 160);
    for (unsigned i = 0; i < 2; ++i)
        expected[16 + i] = (uint8_t)(original[16 + i] | (original[24 + i] & original[20 + i]));
    memcpy(expected + 24, expected + 16, 8);
    assert(cupid_gb_read_bus_u8(gb, 0xfe20) == 0xff);
    assert(memcmp(expected, gb->object_attribute_memory, 160) == 0);
    release(gb);
}

static void test_instruction_ownership(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB, CUPID_GB_MODEL_SGB2,
                                   CUPID_GB_MODEL_CGB};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned dot = 3; dot <= 67; dot += 4) {
            uint8_t images[3][160];
            const uint8_t opcodes[] = {0x7e, 0x2a, 0x3a};
            for (unsigned op = 0; op < 3; ++op) {
                CupidGb *gb = prepare(models[m], dot, opcodes[op]);
                uint8_t original[160];
                memcpy(original, gb->object_attribute_memory, 160);
                assert(cupid_gb_step(gb));
                memcpy(images[op], gb->object_attribute_memory, 160);
                assert(gb->cpu.a == 0xff);
                assert(gb->cpu.h == 0xfe);
                assert(gb->cpu.l == (op == 0 ? 0x20 : op == 1 ? 0x21 : 0x1f));
                if (models[m] == CUPID_GB_MODEL_CGB)
                    assert(memcmp(original, images[op], 160) == 0);
                release(gb);
            }
            assert(memcmp(images[0], images[1], 160) == 0);
            assert(memcmp(images[0], images[2], 160) == 0);
        }
    }
}

static void test_edge_rows(void) {
    for (unsigned column = 0; column < 8; ++column) {
        CupidGb *gb = prepare(CUPID_GB_MODEL_DMG_ABC, 3, 0x7e);
        uint8_t original[160], expected[160];
        memcpy(original, gb->object_attribute_memory, 160);
        memcpy(expected, original, 160);
        /* A read at the first slot couples row zero with the address-selected
         * row. Keep the passive peek separate from the mutating guest read. */
        gb->pixels.dot = 0;
        unsigned selected = 32;
        unsigned word = selected + (column & 6u);
        for (unsigned i = 0; i < 2; ++i) {
            uint8_t merged = (uint8_t)(original[selected + i] | (original[i] & original[word + i]));
            expected[i] = expected[selected + i] = merged;
        }
        memcpy(expected + 2, expected + selected + 2, 6);
        assert(cupid_gb_read_bus_u8(gb, (uint16_t)(0xfe20u + column)) == 0xff);
        assert(memcmp(expected, gb->object_attribute_memory, 160) == 0);
        memcpy(gb->object_attribute_memory, original, 160);
        gb->pixels.dot = 80;
        memcpy(expected, original, 160);
        unsigned target = 152u + (column & 6u);
        for (unsigned i = 0; i < 2; ++i) {
            uint8_t a = original[156 + i], b = original[target + i];
            uint8_t c = original[(column & 6u) == 2u ? word + i : selected + i];
            if ((column & 6u) == 0u || (column & 6u) == 2u)
                expected[target + i] = (uint8_t)((a & b) | (a & c) | (b & c));
            else if ((column & 6u) == 6u)
                expected[target + i] = (uint8_t)(b | (a & c));
        }
        memcpy(expected + selected, expected + 152, 8);
        assert(cupid_gb_read_bus_u8(gb, (uint16_t)(0xfe20u + column)) == 0xff);
        assert(memcmp(expected, gb->object_attribute_memory, 160) == 0);
        release(gb);
    }
}

int main(void) {
    test_passive_and_ordinary();
    test_edge_rows();
    test_instruction_ownership();
    puts("OAM read bus checks passed");
    return 0;
}

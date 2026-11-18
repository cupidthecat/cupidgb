/**
 * @file    test_cgb_revision.c
 * @brief   Color hardware revision banking, register, sound, and state regressions.
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

#include "cupid/gb/apu.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

enum { TAIL_BYTES = 96, ROM_BYTES = 32768 };

static const CupidGbModel models[] = {CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A,
                                      CUPID_GB_MODEL_CGB_B, CUPID_GB_MODEL_CGB_C,
                                      CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB_E};

/* Each row describes a distinct physical byte by its independently observed
 * aliases. Early hardware has three groups of eight bytes, D has 48 bytes. */
static const uint8_t early_groups[TAIL_BYTES] = {
    0,  1,  2,  3,  4,  5,  6,  7,  0,  1,  2,  3,  4,  5,  6,  7,  0,  1,  2,  3,  4,  5,  6,  7,
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 8,  9,  10, 11, 12, 13, 14, 15,
    8,  9,  10, 11, 12, 13, 14, 15, 8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    16, 17, 18, 19, 20, 21, 22, 23, 16, 17, 18, 19, 20, 21, 22, 23, 16, 17, 18, 19, 20, 21, 22, 23};

static const uint8_t late_groups[TAIL_BYTES] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

static CupidGb *machine(CupidGbModel model) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t rom[ROM_BYTES] = {0};
    assert(gb);
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    rom[0x143] = 0x80;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(gb->model == model && gb->cgb_mode);
    cupid_gb_write_u8(gb, 0xff40, 0);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static uint8_t fixed_readback(unsigned offset) {
    static const uint8_t values[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    return values[offset / 16u];
}

static void read_tail(CupidGb *gb, uint8_t tail[TAIL_BYTES]) {
    for (unsigned offset = 0; offset < TAIL_BYTES; ++offset)
        tail[offset] = cupid_gb_read_u8(gb, (uint16_t)(0xfea0u + offset));
}

static void clear_tail(CupidGb *gb) {
    for (unsigned offset = 0; offset < TAIL_BYTES; ++offset)
        cupid_gb_write_u8(gb, (uint16_t)(0xfea0u + offset), 0);
}

static void test_tail_aliases(CupidGbModel model) {
    CupidGb *gb = machine(model);
    const uint8_t *groups = model == CUPID_GB_MODEL_CGB_D ? late_groups : early_groups;
    for (unsigned address = 0; address < 160; ++address)
        cupid_gb_write_u8(gb, (uint16_t)(0xfe00u + address), (uint8_t)(address ^ 0x69u));
    for (unsigned selected = 0; selected < TAIL_BYTES; ++selected) {
        clear_tail(gb);
        cupid_gb_write_u8(gb, (uint16_t)(0xfea0u + selected), 0x93);
        for (unsigned offset = 0; offset < TAIL_BYTES; ++offset) {
            const uint8_t expected = model == CUPID_GB_MODEL_CGB_E        ? fixed_readback(offset)
                                     : groups[selected] == groups[offset] ? 0x93
                                                                          : 0;
            assert(cupid_gb_read_u8(gb, (uint16_t)(0xfea0u + offset)) == expected);
            assert(cupid_gb_read_bus_u8(gb, (uint16_t)(0xfea0u + offset)) == expected);
        }
        for (unsigned address = 0; address < 160; ++address)
            assert(cupid_gb_read_u8(gb, (uint16_t)(0xfe00u + address)) ==
                   (uint8_t)(address ^ 0x69u));
    }
    destroy(gb);
}

static void test_infrared_mask(CupidGbModel model) {
    CupidGb *gb = machine(model);
    const uint8_t revision_bit = model == CUPID_GB_MODEL_CGB_E ? 0 : 0x10;
    for (unsigned value = 0; value < 256; ++value) {
        /* Keep the transmitter off and the receiver unclocked so only the
         * writable latch and physical read mask contribute to this result. */
        const uint8_t control = (uint8_t)(value & 0xfeu);
        cupid_gb_write_u8(gb, 0xff56, control);
        assert(cupid_gb_read_u8(gb, 0xff56) == (uint8_t)(0x2eu | revision_bit | (control & 0xc0u)));
    }
    destroy(gb);
}

static uint8_t *snapshot(CupidGb *gb, size_t *size) {
    *size = cupid_gb_state_size(gb);
    uint8_t *data = malloc(*size);
    size_t written = 0;
    assert(data);
    assert(cupid_gb_save_state(gb, data, *size, &written) == CUPID_GB_STATE_OK);
    assert(written == *size && data[8] == CUPID_GB_STATE_VERSION);
    return data;
}

static void assert_tail(CupidGb *gb, const uint8_t expected[TAIL_BYTES]) {
    uint8_t actual[TAIL_BYTES];
    read_tail(gb, actual);
    assert(!memcmp(expected, actual, sizeof(actual)));
}

static void test_tail_continuation(CupidGbModel model) {
    CupidGb *first = machine(model), *second = machine(model);
    for (unsigned offset = 0; offset < TAIL_BYTES; ++offset)
        cupid_gb_write_u8(first, (uint16_t)(0xfea0u + offset), (uint8_t)(offset * 71u + 1u));
    uint8_t expected[TAIL_BYTES];
    read_tail(first, expected);
    size_t size;
    uint8_t *saved = snapshot(first, &size);
    assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_OK);
    assert(second->model == model);
    assert_tail(second, expected);
    clear_tail(second);
    assert(cupid_gb_load_state_live(second, saved, size) == CUPID_GB_STATE_OK);
    assert_tail(second, expected);
    assert(cupid_gb_reset(second, CUPID_GB_RESET_QUICK));
    assert(second->model == model);
    cupid_gb_write_u8(second, 0xff40, 0);
    assert_tail(second, expected);
    size_t after_size;
    uint8_t *after = snapshot(second, &after_size);
    saved[size - 1] ^= 1u;
    assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_INVALID);
    size_t rejected_size;
    uint8_t *rejected = snapshot(second, &rejected_size);
    assert(after_size == rejected_size && !memcmp(after, rejected, after_size));
    free(rejected);
    free(after);
    free(saved);
    destroy(first);
    destroy(second);
}

static void test_revision_identity(CupidGbModel model) {
    CupidGb *first = machine(model);
    size_t size;
    uint8_t *saved = snapshot(first, &size);
    for (unsigned i = 0; i < COUNT_OF(models); ++i) {
        if (models[i] == model)
            continue;
        CupidGb *second = machine(models[i]);
        size_t before_size;
        uint8_t *before = snapshot(second, &before_size);
        assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_WRONG_MODEL);
        size_t after_size;
        uint8_t *after = snapshot(second, &after_size);
        assert(after_size == before_size && !memcmp(before, after, before_size));
        free(after);
        free(before);
        destroy(second);
    }
    free(saved);
    destroy(first);
}

static void test_length_clear(CupidGbModel model) {
    const uint16_t length_port[] = {0xff11, 0xff16, 0xff1b, 0xff20};
    const uint16_t dac_port[] = {0xff12, 0xff17, 0xff1a, 0xff21};
    const uint16_t control_port[] = {0xff14, 0xff19, 0xff1e, 0xff23};
    const uint8_t length_value[] = {0x3f, 0x3f, 0xff, 0x3f};
    const uint8_t dac_value[] = {0xf0, 0xf0, 0x80, 0xf0};
    const bool early = model == CUPID_GB_MODEL_CGB_0 || model == CUPID_GB_MODEL_CGB_A ||
                       model == CUPID_GB_MODEL_CGB_B;
    for (unsigned channel = 0; channel < 4; ++channel) {
        for (unsigned phase = 0; phase < 16; ++phase) {
            CupidGb *gb = machine(model);
            cupid_gb_write_u8(gb, 0xff26, 0);
            cupid_gb_write_u8(gb, 0xff04, 0);
            cupid_gb_write_u8(gb, 0xff26, 0x80);
            cupid_gb_write_u8(gb, length_port[channel], length_value[channel]);
            cupid_gb_write_u8(gb, dac_port[channel], dac_value[channel]);
            cupid_gb_write_u8(gb, control_port[channel], 0x80);
            const unsigned delay = 1024u + phase * 4096u;
            const uint64_t start = gb->elapsed_cycles;
            cupid_gb_tick(gb, (uint16_t)(delay / 4u));
            assert(gb->elapsed_cycles - start == delay);
            assert(cupid_gb_read_u8(gb, 0xff26) & (1u << channel));
            cupid_gb_write_u8(gb, control_port[channel], 0);
            const bool expected = !(early && channel < 3 && phase % 4u >= 2u);
            const bool active = (cupid_gb_read_u8(gb, 0xff26) & (1u << channel)) != 0;
            assert(active == expected);
            destroy(gb);
        }
    }
}

int main(void) {
    for (unsigned i = 0; i < COUNT_OF(models); ++i) {
        test_tail_aliases(models[i]);
        test_infrared_mask(models[i]);
        test_tail_continuation(models[i]);
        test_revision_identity(models[i]);
        test_length_clear(models[i]);
    }
    return 0;
}

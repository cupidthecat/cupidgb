/**
 * @file    test_mapper_guest.c
 * @brief   Executed cartridge banking and native continuation across GB/GBC controller types.
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
#include "cupid/gb/rtc.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BANK_SIZE = 0x4000, PROGRAM_ADDRESS = 0xc100, RESULT_ADDRESS = 0xc800 };

typedef struct BankStep {
    uint16_t address;
    uint8_t value;
    uint16_t lower;
    uint16_t upper;
} BankStep;

typedef struct MapperCase {
    uint8_t type;
    unsigned banks;
    const BankStep *steps;
    size_t count;
    bool multicart;
} MapperCase;

static const BankStep fixed_steps[] = {
    {0x2000, 7, 0, 1}, {0x3fff, 0xff, 0, 1}, {0x5fff, 3, 0, 1}, {0x7fff, 1, 0, 1}};
static const BankStep mbc1_steps[] = {{0x2fff, 0, 0, 1},   {0x3fff, 31, 0, 31},
                                      {0x5fff, 3, 0, 127}, {0x7fff, 1, 96, 127},
                                      {0x2000, 0, 96, 97}, {0x6000, 0, 0, 97}};
static const BankStep multicart_steps[] = {{0x2fff, 0, 0, 1},    {0x3fff, 31, 0, 15},
                                           {0x5fff, 3, 0, 63},   {0x7fff, 1, 48, 63},
                                           {0x2000, 16, 48, 48}, {0x6000, 0, 0, 48}};
static const BankStep mbc2_steps[] = {{0x0100, 0, 0, 1},  {0x3fff, 0xff, 0, 15},
                                      {0x2000, 0, 0, 15}, {0x2100, 0x10, 0, 1},
                                      {0x4000, 7, 0, 1},  {0x7fff, 9, 0, 1}};
static const BankStep mbc3_steps[] = {{0x2000, 0, 0, 1},   {0x3fff, 127, 0, 127},
                                      {0x2fff, 128, 0, 1}, {0x3000, 129, 0, 1},
                                      {0x5fff, 3, 0, 1},   {0x7fff, 1, 0, 1}};
static const BankStep extended_steps[] = {{0x2000, 0, 0, 1},     {0x3fff, 127, 0, 127},
                                          {0x2fff, 128, 0, 128}, {0x3000, 129, 0, 129},
                                          {0x5fff, 7, 0, 129},   {0x7fff, 1, 0, 129}};
static const BankStep mbc5_steps[] = {{0x2fff, 255, 0, 255}, {0x3fff, 255, 0, 511},
                                      {0x2000, 0, 0, 256},   {0x3000, 2, 0, 0},
                                      {0x4000, 15, 0, 0},    {0x7fff, 1, 0, 0}};
static const BankStep mmm01_steps[] = {{0x2000, 5, 126, 127}, {0x6000, 0, 126, 127},
                                       {0x0000, 0x40, 0, 5},  {0x3fff, 0, 0, 1},
                                       {0x7fff, 1, 0, 1},     {0x2000, 7, 0, 7}};
static const BankStep byte_steps[] = {{0x2000, 0, 0, 0},     {0x3fff, 255, 0, 255},
                                      {0x2fff, 128, 0, 128}, {0x3000, 129, 0, 129},
                                      {0x5fff, 3, 0, 129},   {0x7fff, 1, 0, 129}};
static const BankStep huc1_steps[] = {{0x0000, 0, 0, 0},   {0x3fff, 255, 0, 63}, {0x2000, 0, 0, 0},
                                      {0x2fff, 129, 0, 1}, {0x5fff, 3, 0, 1},    {0x7fff, 1, 0, 1}};
static const BankStep huc3_steps[] = {{0x7fff, 0, 0, 0},   {0x3fff, 255, 0, 127},
                                      {0x2000, 128, 0, 0}, {0x2fff, 129, 0, 1},
                                      {0x5fff, 3, 0, 1},   {0x1fff, 10, 0, 1}};
static const BankStep tpp1_steps[] = {{0x001c, 255, 0, 255}, {0x11fd, 1, 0, 511},
                                      {0x0000, 0, 0, 256},   {0x3ffd, 2, 0, 0},
                                      {0x0001, 255, 0, 256}, {0x2000, 7, 0, 263}};

#define MAPPER_CASE(type, banks, steps)                                                            \
    {type, banks, steps, sizeof(steps) / sizeof(*(steps)), false}

static const MapperCase cases[] = {
    MAPPER_CASE(0x00, 2, fixed_steps),
    MAPPER_CASE(0x08, 2, fixed_steps),
    MAPPER_CASE(0x09, 2, fixed_steps),
    MAPPER_CASE(0x01, 128, mbc1_steps),
    MAPPER_CASE(0x02, 128, mbc1_steps),
    MAPPER_CASE(0x03, 128, mbc1_steps),
    MAPPER_CASE(0x05, 16, mbc2_steps),
    MAPPER_CASE(0x06, 16, mbc2_steps),
    MAPPER_CASE(0x0b, 128, mmm01_steps),
    MAPPER_CASE(0x0c, 128, mmm01_steps),
    MAPPER_CASE(0x0d, 128, mmm01_steps),
    MAPPER_CASE(0x0f, 128, mbc3_steps),
    MAPPER_CASE(0x10, 128, mbc3_steps),
    MAPPER_CASE(0x11, 128, mbc3_steps),
    MAPPER_CASE(0x12, 128, mbc3_steps),
    MAPPER_CASE(0x13, 128, mbc3_steps),
    MAPPER_CASE(0x19, 512, mbc5_steps),
    MAPPER_CASE(0x1a, 512, mbc5_steps),
    MAPPER_CASE(0x1b, 512, mbc5_steps),
    MAPPER_CASE(0x1c, 512, mbc5_steps),
    MAPPER_CASE(0x1d, 512, mbc5_steps),
    MAPPER_CASE(0x1e, 512, mbc5_steps),
    MAPPER_CASE(0x22, 256, byte_steps),
    MAPPER_CASE(0xfc, 256, byte_steps),
    MAPPER_CASE(0xfe, 128, huc3_steps),
    MAPPER_CASE(0xff, 64, huc1_steps),
    MAPPER_CASE(0xbc, 512, tpp1_steps),
    MAPPER_CASE(0x10, 256, extended_steps),
    MAPPER_CASE(0x13, 256, extended_steps),
    {0x03, 64, multicart_steps, sizeof(multicart_steps) / sizeof(*multicart_steps), true}};

static int64_t fixed_time(void *context) {
    (void)context;
    return INT64_C(1800000000);
}

static uint8_t *make_rom(const MapperCase *test, CupidGbModel model) {
    const size_t size = (size_t)test->banks * BANK_SIZE;
    uint8_t *rom = calloc(1, size);
    assert(rom);
    for (unsigned bank = 0; bank < test->banks; ++bank) {
        rom[(size_t)bank * BANK_SIZE + 0x1000] = (uint8_t)bank;
        rom[(size_t)bank * BANK_SIZE + 0x1001] = (uint8_t)(bank >> 8);
    }
    const bool menu = test->type >= 0x0b && test->type <= 0x0d;
    uint8_t *header = rom + (menu ? size - 2 * BANK_SIZE : 0);
    /* Distinct primary and secondary logos select ordinary MBC1 wiring. */
    for (unsigned i = 0; i < 48; ++i)
        header[0x104 + i] = (uint8_t)(i + 1);
    if (menu)
        memcpy(rom + 0x104, header + 0x104, 48);
    if (test->multicart)
        memcpy(rom + 0x40104, header + 0x104, 48);
    memcpy(header + 0x134, "MAPPER GUEST", 12);
    header[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    header[0x147] = test->type;
    for (unsigned banks = test->banks; banks > 2; banks >>= 1)
        ++header[0x148];
    static const bool has_sram[256] = {[0x02] = true, [0x03] = true, [0x08] = true, [0x09] = true,
                                       [0x0c] = true, [0x0d] = true, [0x10] = true, [0x12] = true,
                                       [0x13] = true, [0x1a] = true, [0x1b] = true, [0x1d] = true,
                                       [0x1e] = true, [0xfc] = true, [0xfe] = true, [0xff] = true};
    header[0x149] = has_sram[test->type] ? 3 : 0;
    if (test->type == 0xbc) {
        header[0x149] = 0xc1;
        header[0x14a] = 0x65;
        header[0x152] = 3;
        header[0x153] = 8;
    }
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        header[0x14d] = (uint8_t)(header[0x14d] - header[i] - 1);
    return rom;
}

static void emit_store(uint8_t *program, size_t *length, uint16_t address, uint8_t value) {
    const uint8_t bytes[] = {0x3e, value, 0xea, (uint8_t)address, (uint8_t)(address >> 8)};
    memcpy(program + *length, bytes, sizeof(bytes));
    *length += sizeof(bytes);
}

static void emit_sample(uint8_t *program, size_t *length, uint16_t address, uint16_t result) {
    const uint8_t bytes[] = {0xfa, (uint8_t)address, (uint8_t)(address >> 8),
                             0xea, (uint8_t)result,  (uint8_t)(result >> 8)};
    memcpy(program + *length, bytes, sizeof(bytes));
    *length += sizeof(bytes);
}

static void execute_until(CupidGb *gb, uint16_t end) {
    for (unsigned steps = 0; steps < 512; ++steps) {
        if (gb->cpu.pc == end)
            return;
        assert(cupid_gb_step(gb));
    }
    assert(!"Mapper guest program exceeded its instruction bound");
}

static void check_results(const CupidGb *gb, const MapperCase *test, CupidGbModel model) {
    for (size_t step = 0; step < test->count; ++step) {
        const uint16_t address = (uint16_t)(RESULT_ADDRESS + step * 4);
        const unsigned lower = cupid_gb_read_u8(gb, address) |
                               (unsigned)cupid_gb_read_u8(gb, (uint16_t)(address + 1)) << 8;
        const unsigned upper = cupid_gb_read_u8(gb, (uint16_t)(address + 2)) |
                               (unsigned)cupid_gb_read_u8(gb, (uint16_t)(address + 3)) << 8;
        if (lower != test->steps[step].lower || upper != test->steps[step].upper)
            fprintf(stderr, "Mapper %02x model %u step %zu expected %u/%u, observed %u/%u\n",
                    test->type, model, step, test->steps[step].lower, test->steps[step].upper,
                    lower, upper);
        assert(lower == test->steps[step].lower && upper == test->steps[step].upper);
    }
}

static void test_guest(const MapperCase *test, CupidGbModel model) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t *rom = make_rom(test, model);
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_rtc_time_source(gb, fixed_time, NULL);
    assert(cupid_gb_load_rom(gb, rom, (size_t)test->banks * BANK_SIZE));
    free(rom);

    uint8_t program[512];
    size_t length = 0;
    size_t checkpoint = 0;
    emit_store(program, &length, 0xff40, 0);
    emit_store(program, &length, 0xffff, 0);
    for (size_t step = 0; step < test->count; ++step) {
        emit_store(program, &length, test->steps[step].address, test->steps[step].value);
        for (unsigned byte = 0; byte < 4; ++byte)
            emit_sample(program, &length, (uint16_t)((byte < 2 ? 0x1000 : 0x5000) + (byte & 1)),
                        (uint16_t)(RESULT_ADDRESS + step * 4 + byte));
        if (step + 1 == test->count / 2)
            checkpoint = length;
    }
    assert(length <= sizeof(program) && checkpoint);
    for (size_t byte = 0; byte < length; ++byte)
        cupid_gb_write_u8(gb, (uint16_t)(PROGRAM_ADDRESS + byte), program[byte]);
    gb->cpu.pc = PROGRAM_ADDRESS;
    execute_until(gb, (uint16_t)(PROGRAM_ADDRESS + checkpoint));

    const size_t state_size = cupid_gb_state_size(gb);
    uint8_t *state = malloc(state_size);
    uint8_t *first = malloc(state_size);
    uint8_t *second = malloc(state_size);
    assert(state && first && second);
    assert(cupid_gb_save_state(gb, state, state_size, NULL) == CUPID_GB_STATE_OK);
    execute_until(gb, (uint16_t)(PROGRAM_ADDRESS + length));
    check_results(gb, test, model);
    assert(cupid_gb_save_state(gb, first, state_size, NULL) == CUPID_GB_STATE_OK);

    /* Reset discards the live latches before the checkpoint restores them.
     * The saved PC resumes the suffix through CPU bus accesses on both runs. */
    assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
    assert(cupid_gb_load_state(gb, state, state_size) == CUPID_GB_STATE_OK);
    execute_until(gb, (uint16_t)(PROGRAM_ADDRESS + length));
    check_results(gb, test, model);
    assert(cupid_gb_save_state(gb, second, state_size, NULL) == CUPID_GB_STATE_OK);
    assert(memcmp(first, second, state_size) == 0);
    free(second);
    free(first);
    free(state);
    cupid_gb_cleanup(gb);
    free(gb);
}

int main(void) {
    for (unsigned model = 0; model < 2; ++model)
        for (size_t test = 0; test < sizeof(cases) / sizeof(*cases); ++test)
            test_guest(&cases[test], model ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
    printf("Mapper guest banking and restored continuation passed %zu model/type cases.\n",
           2 * sizeof(cases) / sizeof(*cases));
    return 0;
}

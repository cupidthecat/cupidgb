/**
 * @file    test_rumble_sampling.c
 * @brief   Motor sample boundaries across CPU instructions, waits, DMA, and clock changes.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "cupid/gb/gb.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Samples {
    unsigned count, sum, levels[4];
} Samples;

static void collect(const CupidGb *gb, uint8_t strength, void *context) {
    Samples *samples = context;
    assert(strength < 4 && strength == cupid_gb_rumble_strength(gb));
    ++samples->count;
    samples->sum += strength;
    ++samples->levels[strength];
}

static CupidGb *machine(unsigned profile, uint8_t opcode, uint8_t operand, Samples *samples) {
    uint8_t rom[0x8000] = {0};
    rom[0x100] = opcode;
    rom[0x101] = operand;
    rom[0x102] = 0xc0;
    memcpy(rom + 0x134, "CUPID MOTOR", 11);
    rom[0x143] = profile ? 0x80 : 0;
    rom[0x147] = 0xbc;
    rom[0x149] = 0xc1;
    rom[0x14a] = 0x65;
    rom[0x150] = rom[0x152] = rom[0x153] = 1;
    const uint8_t loop[] = {0x3e, 0x21, 0xea, 3, 0, 0x3e, 0x22, 0xea, 3, 0, 0x18, 0xf4};
    memcpy(rom + 0x200, loop, sizeof(loop));
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);

    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, profile ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
    cupid_gb_set_power_on_seed(gb, 0);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    cupid_gb_write_u8(gb, 0xff00, 0x30);
    cupid_gb_write_u8(gb, 3, 0x23);
    gb->interrupt_enable = gb->interrupt_flags = 0;
    gb->double_speed = profile == 2;
    gb->cpu.pc = 0x100;
    gb->cpu.sp = 0xdff0;
    gb->cpu.h = 0xc0;
    gb->cpu.l = 0x30;
    gb->cpu.f = 0;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
    if (samples)
        cupid_gb_set_rumble_sample_callback(gb, collect, samples);
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_unequal_deliveries(void) {
    for (unsigned profile = 0; profile < 3; ++profile) {
        Samples samples = {0};
        CupidGb *gb = machine(profile, 0, 0, &samples);
        const uint8_t levels[] = {1, 3, 2};
        const uint16_t cycles[] = {1, 3, 6};
        for (unsigned index = 0; index < 3; ++index) {
            cupid_gb_write_u8(gb, 3, (uint8_t)(0x20u + levels[index]));
            assert(samples.count == index);
            cupid_gb_deliver_cpu_cycles(gb, cycles[index]);
            assert(samples.count == index + 1u);
        }
        /* These unequal spans have an average motor level of 2, not 2.2. */
        assert(samples.sum == 6 && samples.levels[1] == 1 && samples.levels[2] == 1 &&
               samples.levels[3] == 1);
        samples = (Samples){0};
        cupid_gb_tick(gb, 12);
        assert(samples.count == 12 && samples.sum == 24);
        cupid_gb_tick(gb, 0);
        cupid_gb_deliver_cpu_cycles(gb, 0);
        assert(samples.count == 12);
        cupid_gb_set_rumble_sample_callback(gb, NULL, &samples);
        assert(!gb->rumble_sample_user_data);
        cupid_gb_tick(gb, 3);
        assert(samples.count == 12);
        release(gb);
    }
}

static void test_instruction_boundaries(void) {
    static const struct {
        uint8_t opcode, operand, samples[2], cycles;
    } cases[] = {{0x00, 0x10, {1, 1}, 4},  {0x18, 0x10, {3, 3}, 12}, {0xc9, 0x10, {3, 3}, 16},
                 {0xcd, 0x10, {6, 6}, 24}, {0xea, 0x10, {4, 4}, 16}, {0xe0, 0x10, {3, 4}, 12},
                 {0xcb, 0x86, {4, 4}, 16}, {0x76, 0x10, {1, 1}, 4},  {0x10, 0x10, {2, 2}, 8}};

    for (unsigned profile = 0; profile < 3; ++profile) {
        for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); ++index) {
            Samples samples = {0};
            CupidGb *gb = machine(profile, cases[index].opcode, cases[index].operand, &samples);
            const uint64_t before = gb->elapsed_cycles;
            assert(cupid_gb_step(gb));
            /* A double-speed NR10 write has an extra delivery at its settling edge. */
            assert(samples.count == cases[index].samples[profile == 2] &&
                   samples.sum == 3u * samples.count);
            assert(gb->elapsed_cycles - before == cases[index].cycles / (profile == 2 ? 2u : 1u));
            release(gb);
        }
    }
}

static void arm_dma(CupidGb *gb, unsigned kind) {
    cupid_gb_write_u8(gb, 0xff51, 0xc1);
    cupid_gb_write_u8(gb, 0xff52, 0);
    cupid_gb_write_u8(gb, 0xff53, 0);
    cupid_gb_write_u8(gb, 0xff54, 0);
    cupid_gb_write_u8(gb, 0xff55, kind >= 9 ? 0x81 : kind == 8 ? 1 : 0);
}

static void test_waits_and_dma(void) {
    /* NOP, first/steady HALT, STOP, IRQ, HALT wake, HALT IRQ, one/two DMA
     * blocks, armed HBlank transfer, and a transfer held by HALT. */
    static const uint8_t counts[3][11] = {{1, 1, 2, 1, 5, 2, 7, 0, 0, 0, 0},
                                          {1, 1, 1, 1, 5, 1, 6, 19, 35, 19, 1},
                                          {1, 1, 1, 1, 5, 1, 6, 18, 34, 18, 1}};
    for (unsigned profile = 0; profile < 3; ++profile) {
        for (unsigned kind = 0; kind < (profile ? 11u : 7u); ++kind) {
            Samples samples = {0};
            CupidGb *gb = machine(profile, 0, 0, &samples);
            if (kind == 1 || kind == 2 || kind == 5 || kind == 6 || kind == 10) {
                gb->cpu.halted = true;
                gb->cpu.just_halted = kind == 1;
            }
            if (kind == 3)
                gb->cpu.stopped = true;
            if (kind >= 4 && kind <= 6) {
                gb->interrupt_enable = gb->interrupt_flags = 1;
                gb->cpu.ime = kind != 5;
            }
            if (kind >= 7)
                arm_dma(gb, kind);
            assert(!samples.count);
            assert(cupid_gb_step(gb));
            assert(samples.count == counts[profile][kind] && samples.sum == 3u * samples.count);
            if (kind >= 7)
                assert(gb->hdma_source == (kind == 10 ? 0xc100 : kind == 8 ? 0xc120 : 0xc110));
            release(gb);
        }
    }
}

static void test_speed_segments(void) {
    static const struct {
        uint8_t toggle, freeze, samples;
    } spans[] = {{0, 0, 1}, {0, 4, 1}, {0, 8, 1},  {0, 12, 0}, {4, 0, 2},
                 {6, 0, 2}, {6, 8, 1}, {12, 0, 1}, {16, 0, 1}};

    for (unsigned profile = 1; profile < 3; ++profile) {
        for (size_t index = 0; index < sizeof(spans) / sizeof(*spans); ++index) {
            Samples samples = {0};
            CupidGb *gb = machine(profile, 0, 0, &samples);
            gb->speed_switch_toggle_cycles = spans[index].toggle;
            gb->speed_switch_freeze_cycles = spans[index].freeze;
            cupid_gb_deliver_cpu_cycles(gb, 3);
            assert(samples.count == spans[index].samples && samples.sum == 3u * samples.count);
            release(gb);
        }
    }

    static const struct {
        uint8_t toggle, freeze, samples[2];
    } dma[] = {{0, 0, {19, 18}}, {1, 0, {19, 20}}, {2, 1, {18, 20}},
               {4, 0, {18, 19}}, {6, 3, {17, 20}}, {8, 5, {16, 18}}};

    for (unsigned profile = 1; profile < 3; ++profile) {
        for (size_t index = 0; index < sizeof(dma) / sizeof(*dma); ++index) {
            Samples samples = {0};
            CupidGb *gb = machine(profile, 0, 0, &samples);
            arm_dma(gb, 7);
            gb->speed_switch_toggle_cycles = dma[index].toggle;
            gb->speed_switch_freeze_cycles = dma[index].freeze;
            assert(cupid_gb_step(gb));
            assert(samples.count == dma[index].samples[profile - 1u]);
            assert(samples.sum == 3u * samples.count && gb->hdma_source == 0xc110);
            release(gb);
        }
    }
}

static void test_observation_preserves_guest(void) {
    for (unsigned profile = 0; profile < 3; ++profile) {
        Samples samples = {0};
        CupidGb *observed = machine(profile, 0, 0, &samples);
        CupidGb *unobserved = machine(profile, 0, 0, NULL);
        observed->cpu.pc = 0x200;
        const size_t length = cupid_gb_state_size(observed);
        uint8_t *first = malloc(length), *second = malloc(length);
        assert(first && second);
        assert(cupid_gb_save_state(observed, first, length, NULL) == CUPID_GB_STATE_OK);
        assert(cupid_gb_load_state(unobserved, first, length) == CUPID_GB_STATE_OK);
        for (unsigned instruction = 0; instruction < 512; ++instruction) {
            assert(cupid_gb_step(observed));
            assert(cupid_gb_step(unobserved));
        }
        assert(samples.count && samples.levels[1] && samples.levels[2]);
        assert(cupid_gb_save_state(observed, first, length, NULL) == CUPID_GB_STATE_OK);
        assert(cupid_gb_save_state(unobserved, second, length, NULL) == CUPID_GB_STATE_OK);
        assert(!memcmp(first, second, length));
        free(first);
        free(second);
        release(observed);
        release(unobserved);
    }
}

int main(void) {
    test_unequal_deliveries();
    test_instruction_boundaries();
    test_waits_and_dma();
    test_speed_segments();
    test_observation_preserves_guest();
    puts("Motor delivery sampling regressions passed.");
    return 0;
}

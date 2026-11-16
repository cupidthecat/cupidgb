/**
 * @file    test_rtc_timing.c
 * @brief   Regression tests for clock scheduling, timestamp history, and rate controls.
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

#include "cupid/gb/gb.h"
#include "cupid/gb/timer.h"
#include "cupid/gbc/cgb.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestClock {
    int64_t now;
    unsigned reads;
} TestClock;

static int64_t observe_time(void *context) {
    TestClock *clock = context;
    ++clock->reads;
    return clock->now;
}

static CupidGb *new_machine(CupidGbModel model, TestClock *clock, uint8_t opcode,
                            uint16_t operand) {
    uint8_t rom[0x8000] = {0};
    memcpy(rom + 0x134, "RTC TIMING", 10);
    rom[0x143] = 0x80;
    rom[0x147] = 0x10;
    rom[0x149] = 2;
    rom[0x150] = opcode;
    rom[0x151] = (uint8_t)operand;
    rom[0x152] = (uint8_t)(operand >> 8);
    for (size_t offset = 0x134; offset < 0x14d; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    cupid_gb_set_rtc_time_source(gb, observe_time, clock);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    cupid_gb_write_u8(gb, 0xff40, 0);
    gb->cpu.pc = 0x150;
    gb->cpu.sp = 0xdff0;
    gb->cpu.a = 0x42;
    gb->cpu.f = 0;
    gb->cpu.h = 0xc1;
    gb->cpu.l = 0;
    gb->cpu.ime = false;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    memset(gb->work_ram, 0, sizeof(gb->work_ram));
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    gb->rtc.base_time = clock->now;
    gb->rtc.s = 10;
    gb->rtc.m = 5;
    gb->rtc.h = 2;
    gb->rtc.dl = 1;
    gb->rtc.dh = 0;
    clock->reads = 0;
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void select_period(CupidGb *gb, uint32_t period) {
    assert(cupid_gb_set_rtc_multiplier(gb, ((double)period + 0.25) /
                                               ((double)cupid_gb_base_clock_hz(gb) * 2.0)));
    assert(gb->rtc_policy.second_length == period);
}

static void test_oscillator_and_debt(void) {
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned speed = 0; speed <= (model == CUPID_GB_MODEL_CGB); ++speed) {
            TestClock clock = {1700000040, 0};
            CupidGb *gb = new_machine((CupidGbModel)model, &clock, 0, 0);
            gb->double_speed = speed != 0;
            const uint32_t half_per_mcycle = speed ? 4u : 8u;
            cupid_gb_tick(gb, 5);
            assert(gb->rtc_cycles == half_per_mcycle * 5u);
            assert(gb->rtc.s == 10 && clock.reads == 0);

            select_period(gb, 1);
            gb->rtc_cycles = 0;
            cupid_gb_tick(gb, 5);
            assert(gb->rtc_cycles == half_per_mcycle * 5u - 1u);
            assert(gb->rtc.s == 11);
            const uint32_t debt = gb->rtc_cycles;
            cupid_gb_tick(gb, 0);
            assert(gb->rtc_cycles == debt && gb->rtc.s == 11);
            cupid_gb_tick(gb, 1);
            assert(gb->rtc.s == 12);
            assert(gb->rtc_cycles == debt + half_per_mcycle - 1u);
            assert(clock.reads == 0);

            assert(cupid_gb_set_rtc_multiplier(gb, 1));
            gb->rtc_cycles = cupid_gb_base_clock_hz(gb) * 2u - half_per_mcycle;
            cupid_gb_tick(gb, 1);
            assert(gb->rtc_cycles == 0 && gb->rtc.s == 13);
            destroy(gb);
        }
    }
}

static void test_shared_bus_and_halt(void) {
    TestClock clock = {1700000040, 0};
    CupidGb *gb = new_machine(CUPID_GB_MODEL_DMG_ABC, &clock, 0, 0);
    select_period(gb, 1);
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    cupid_gb_write_u8(gb, 0xff46, 0x80);
    cupid_gb_tick(gb, 5);
    assert(gb->rtc.s == 11 && gb->rtc_cycles == 39);
    destroy(gb);

    for (unsigned color = 0; color < 2; ++color) {
        for (unsigned first = 0; first < 2; ++first) {
            gb = new_machine(color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC, &clock, 0, 0);
            select_period(gb, 1);
            gb->cpu.halted = true;
            gb->cpu.just_halted = first != 0;
            assert(cupid_gb_step(gb));
            const unsigned deliveries = color || first ? 1u : 2u;
            assert(gb->rtc.s == 10u + deliveries);
            assert(gb->rtc_cycles == 8u - deliveries);
            destroy(gb);
        }
    }
}

static void test_clock_cartridge_gate(void) {
    TestClock clock = {1700000040, 0};
    CupidGb *gb = new_machine(CUPID_GB_MODEL_CGB, &clock, 0, 0);
    gb->header.has_timer = false;
    gb->rtc_cycles = 123;
    cupid_gb_tick(gb, 8);
    assert(gb->rtc_cycles == 123 && gb->rtc.s == 10);
    gb->header.has_timer = true;
    gb->header.mbc_type = CUPID_GB_MBC5;
    cupid_gb_tick(gb, 8);
    assert(gb->rtc_cycles == 123 && gb->rtc.s == 10);
    gb->header.mbc_type = CUPID_GB_MBC3;
    gb->rtc.dh = 0x40;
    cupid_gb_tick(gb, 8);
    assert(gb->rtc_cycles == 123 && gb->rtc.s == 10);
    gb->rtc.dh = 0;
    select_period(gb, 8);
    gb->rtc_cycles = UINT32_MAX - 3u;
    cupid_gb_tick(gb, 1);
    assert(gb->rtc_cycles == 4 && gb->rtc.s == 10);
    assert(clock.reads == 0);
    destroy(gb);
}

static void test_host_polling(void) {
    TestClock clock = {1700000040, 0};
    CupidGb *gb = new_machine(CUPID_GB_MODEL_SGB, &clock, 0, 0);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_SYNC_TO_HOST));
    clock.reads = 0;
    clock.now += 2;
    const uint32_t interval = cupid_gb_base_clock_hz(gb) / 16u;
    gb->rtc_cycles = interval - 8u;
    cupid_gb_tick(gb, 1);
    assert(gb->rtc.s == 12 && gb->rtc_cycles == 0 && clock.reads == 1);
    gb->rtc_cycles = interval * 3u;
    cupid_gb_tick(gb, 2);
    assert(gb->rtc_cycles == interval * 2u + 16u && clock.reads == 2);
    destroy(gb);
}

static void test_instruction_spans(void) {
    static const struct {
        uint8_t opcode;
        uint16_t operand;
        uint32_t half_cycles;
        uint8_t deliveries;
    } cases[] = {{0x00, 0, 8, 1},  {0xc3, 0x150, 32, 3}, {0xc9, 0, 32, 3},   {0xe8, 0, 32, 2},
                 {0xf8, 0, 24, 2}, {0xe0, 0x41, 24, 4},  {0xe0, 0x40, 24, 4}};

    for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); ++index) {
        TestClock clock = {1700000040, 0};
        CupidGb *gb =
            new_machine(CUPID_GB_MODEL_DMG_ABC, &clock, cases[index].opcode, cases[index].operand);
        select_period(gb, 1);
        assert(cupid_gb_step(gb));
        assert(gb->rtc.s == 10u + cases[index].deliveries);
        assert(gb->rtc_cycles == cases[index].half_cycles - cases[index].deliveries);
        assert(clock.reads == 0);
        destroy(gb);
    }
}

static void test_register_spans(void) {
    static const uint8_t ports[] = {0x10, 0x40, 0x41, 0x42, 0x43, 0x45, 0x47, 0x48, 0x49, 0x4b};
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        const bool color = model == CUPID_GB_MODEL_CGB;
        const bool handheld = model == CUPID_GB_MODEL_DMG_ABC || model == CUPID_GB_MODEL_DMG0 ||
                              model == CUPID_GB_MODEL_MGB;
        for (unsigned speed = 0; speed <= color; ++speed) {
            for (size_t index = 0; index < sizeof(ports); ++index) {
                TestClock clock = {1700000040, 0};
                CupidGb *gb = new_machine((CupidGbModel)model, &clock, 0xe0, ports[index]);
                gb->double_speed = speed != 0;
                select_period(gb, 1);
                unsigned deliveries = 3;
                if (ports[index] == 0x41 || (ports[index] == 0x40 && (!color || speed)) ||
                    (ports[index] == 0x10 && color && speed) ||
                    (ports[index] >= 0x47 && ports[index] <= 0x49 && handheld) ||
                    (ports[index] == 0x4b && !color))
                    deliveries = 4;
                assert(cupid_gb_step(gb));
                if (gb->rtc.s != 10u + deliveries)
                    fprintf(stderr,
                            "RTC write model=%u speed=%u port=%02x seconds=%u expected=%u\n", model,
                            speed, ports[index], gb->rtc.s, 10u + deliveries);
                assert(gb->rtc.s == 10u + deliveries);
                assert(gb->rtc_cycles == (speed ? 12u : 24u) - deliveries);
                assert(clock.reads == 0);
                destroy(gb);
            }
        }
    }
}

static void test_switch_span_boundaries(void) {
    static const struct {
        uint8_t mcycles, toggle, normal_half, fast_half, deliveries;
    } cases[] = {{2, 4, 12, 12, 2}, {3, 4, 16, 20, 2}, {2, 8, 8, 16, 1}, {3, 8, 20, 16, 2}};

    for (unsigned speed = 0; speed < 2; ++speed) {
        for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); ++index) {
            TestClock clock = {1700000040, 0};
            CupidGb *gb = new_machine(CUPID_GB_MODEL_CGB, &clock, 0, 0);
            gb->double_speed = speed != 0;
            gb->speed_switch_toggle_cycles = cases[index].toggle;
            select_period(gb, 1);
            const uint64_t before = gb->elapsed_cycles;
            cupid_gb_tick(gb, cases[index].mcycles);
            const unsigned half = speed ? cases[index].fast_half : cases[index].normal_half;
            assert(gb->rtc.s == 10u + cases[index].deliveries);
            assert(gb->rtc_cycles == half - cases[index].deliveries);
            assert(gb->elapsed_cycles - before == half / 2u);
            assert(gb->speed_switch_toggle_cycles == 0 && gb->double_speed == !speed);
            assert((gb->io_registers[0x4d] & 0x80u) == (speed ? 0u : 0x80u));
            assert(clock.reads == 0);
            destroy(gb);
        }
    }
}

static void test_interrupt_switch_spans(void) {
    static const struct {
        uint32_t period, phase;
    } profiles[] = {{1, 0}, {8, 7}, {8, 31}};

    static const struct {
        uint16_t stack;
        uint8_t speed, toggle, freeze;
        uint8_t phase[3], updates[3];
    } cases[] = {
        {0xdff0, 0, 4, 0, {15, 3, 11}, {5, 3, 5}}, {0xdff0, 0, 6, 3, {14, 3, 3}, {6, 3, 6}},
        {0xdff0, 0, 8, 5, {13, 1, 9}, {5, 3, 5}},  {0xff43, 0, 4, 0, {14, 3, 3}, {6, 3, 6}},
        {0xff43, 0, 6, 3, {13, 3, 3}, {7, 3, 6}},  {0xff43, 0, 8, 5, {12, 1, 1}, {6, 3, 6}},
        {0xff11, 0, 4, 0, {14, 3, 3}, {6, 3, 6}},  {0xff11, 0, 6, 3, {14, 3, 3}, {6, 3, 6}},
        {0xff11, 0, 8, 5, {13, 1, 9}, {5, 3, 5}},  {0xdff0, 1, 4, 0, {35, 7, 31}, {5, 5, 5}},
        {0xdff0, 1, 6, 3, {25, 6, 14}, {6, 4, 6}}, {0xdff0, 1, 8, 5, {22, 2, 18}, {5, 4, 5}},
        {0xff43, 1, 4, 0, {34, 7, 23}, {6, 5, 6}}, {0xff43, 1, 6, 3, {24, 6, 6}, {7, 4, 7}},
        {0xff43, 1, 8, 5, {21, 2, 10}, {6, 4, 6}}, {0xff11, 1, 4, 0, {35, 7, 31}, {5, 5, 5}},
        {0xff11, 1, 6, 3, {24, 6, 6}, {7, 4, 7}},  {0xff11, 1, 8, 5, {21, 2, 10}, {6, 4, 6}},
    };

    /* Tiny periods expose each completed delivery, including the two-cycle
     * high-stack span and conflicts selected before a delayed speed change. */
    for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); ++index) {
        for (size_t profile = 0; profile < sizeof(profiles) / sizeof(*profiles); ++profile) {
            TestClock clock = {1700000040, 0};
            CupidGb *gb = new_machine(CUPID_GB_MODEL_CGB, &clock, 0, 0);
            select_period(gb, profiles[profile].period);
            gb->rtc_cycles = profiles[profile].phase;
            gb->cpu.sp = cases[index].stack;
            gb->cpu.ime = true;
            gb->interrupt_enable = gb->interrupt_flags = 1;
            gb->double_speed = cases[index].speed != 0;
            gb->speed_switch_toggle_cycles = cases[index].toggle;
            gb->speed_switch_freeze_cycles = cases[index].freeze;
            assert(cupid_gb_step(gb));
            if (gb->rtc_cycles != cases[index].phase[profile] ||
                gb->rtc.s != 10u + cases[index].updates[profile])
                fprintf(stderr,
                        "RTC interrupt speed=%u stack=%04x toggle=%u freeze=%u profile=%zu "
                        "phase=%u expected=%u seconds=%u expected=%u\n",
                        cases[index].speed, cases[index].stack, cases[index].toggle,
                        cases[index].freeze, profile, gb->rtc_cycles, cases[index].phase[profile],
                        gb->rtc.s, 10u + cases[index].updates[profile]);
            assert(gb->rtc_cycles == cases[index].phase[profile]);
            assert(gb->rtc.s == 10u + cases[index].updates[profile]);
            assert(gb->rtc.base_time == clock.now + cases[index].updates[profile]);
            assert(gb->cpu.pc == 0x40 && gb->cpu.sp == cases[index].stack - 2u);
            assert(gb->double_speed == !cases[index].speed);
            assert(gb->speed_switch_toggle_cycles == 0 && gb->speed_switch_freeze_cycles == 0);
            assert(clock.reads == 0);
            destroy(gb);
        }
    }
}

int main(void) {
    test_oscillator_and_debt();
    test_shared_bus_and_halt();
    test_clock_cartridge_gate();
    test_host_polling();
    test_instruction_spans();
    test_register_spans();
    test_switch_span_boundaries();
    test_interrupt_switch_spans();
    return 0;
}

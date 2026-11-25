/**
 * @file    test_apu.c
 * @brief   Regression coverage for sound hardware, clock delivery, and audio output.
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
#include "cupid/gb/apu.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/timer.h"

#include "../src/gb/apu_noise.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void prepare(CupidGb *gb, bool cgb, bool double_speed) {
    *gb = (CupidGb){0};
    gb->model = cgb ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
    cupid_gb_sgb_startup_reset(gb, false);
    gb->cgb_mode = cgb;
    gb->double_speed = double_speed;
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff17, 0xf0);
    cupid_gb_write_u8(gb, 0xff16, 62); /* Length two. */
    cupid_gb_write_u8(gb, 0xff19, 0xc0);
    assert((cupid_gb_read_u8(gb, 0xff26) & 2) != 0);
}

static void test_divider_clock(bool double_speed) {
    CupidGb gb;
    unsigned period = double_speed ? 4096 : 2048;
    prepare(&gb, double_speed, double_speed);
    cupid_gb_tick(&gb, (uint16_t)(period - 1));
    assert(gb.apu.ch2_len == 2);
    cupid_gb_tick(&gb, 1);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_tick(&gb, (uint16_t)period);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_tick(&gb, (uint16_t)period);
    assert(gb.apu.ch2_len == 0);
    assert((cupid_gb_read_u8(&gb, 0xff26) & 2) == 0);
}

static void test_div_reset_edge(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_tick(&gb, 1024); /* DIV bit 12 high, no falling edge yet. */
    assert(gb.apu.ch2_len == 2);
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1); /* A low-to-low DIV reset cannot clock length. */
}

static void test_power_on_high_div(void) {
    CupidGb gb = {0};
    gb.io_registers[4] = 0x10;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff17, 0xf0);
    cupid_gb_write_u8(&gb, 0xff16, 62);
    cupid_gb_write_u8(&gb, 0xff19, 0xc0);
    assert(gb.apu.ch2_len == 1); /* Enabling length in this phase clocks once. */
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1); /* First edge after power-on in the high half is skipped. */
    cupid_gb_tick(&gb, 2048);
    assert(gb.apu.ch2_len == 0);
}

static void test_power_counter_reset(bool cgb) {
    CupidGb gb;
    prepare(&gb, cgb, false);
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.ch2_len == 0);
    cupid_gb_write_u8(&gb, 0xff16, 60);
    cupid_gb_write_u8(&gb, 0xff20, 60);
    assert(gb.apu.ch2_len == (cgb ? 0 : 4));
    assert(gb.apu.ch4_len == (cgb ? 0 : 4));
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    assert(gb.apu.ch2_len == (cgb ? 0 : 4));
    assert(gb.apu.ch4_len == (cgb ? 0 : 4));
}

static void test_apu_ticks_do_not_clock_length(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_tick_apu(&gb, 32768);
    assert(gb.apu.ch2_len == 2); /* The frequency clock cannot substitute for DIV edges. */
}

static void test_power_cycle_resets_sound_but_preserves_stream(bool color) {
    CupidGb gb;
    prepare(&gb, color, false);
    cupid_gb_write_u8(&gb, 0xff30, 0xa5);
    cupid_gb_write_u8(&gb, 0xff16, 0xc0);
    cupid_gb_write_u8(&gb, 0xff18, 0xff);
    cupid_gb_write_u8(&gb, 0xff19, 0x87);
    cupid_gb_tick_apu(&gb, 95); /* Sample before the duty wrap, including trigger delay. */
    assert(gb.apu.ch2_duty_pos != 0);
    uint32_t count = gb.apu.output.buf_write;
    uint32_t fraction = gb.apu.output.sample_acc;
    int16_t left = gb.apu.output.buf[0];
    uint32_t partial = gb.apu.output.sample_fraction;
    double capacitor = gb.apu.output.filter.capacitor[0];
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.ch2_duty_pos == 0);
    assert(gb.apu.ch2_freq == 0);
    assert(!gb.apu.ch2_on && !gb.apu.ch2_dac);
    assert(gb.io_registers[0x16] == 0);
    assert(gb.io_registers[0x30] == 0xa5);
    assert(gb.apu.output.buf_write == count && gb.apu.output.buf[0] == left);
    assert(gb.apu.output.sample_acc == fraction);
    assert(gb.apu.output.sample_fraction == partial &&
           gb.apu.output.filter.capacitor[0] == capacitor);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    assert(gb.apu.ch2_duty_pos == 0);
    assert(!gb.apu.ch2_on && !gb.apu.ch2_dac);
    assert(gb.io_registers[0x30] == 0xa5);
    assert(gb.apu.output.buf_write == count && gb.apu.output.sample_acc == fraction);
    assert(gb.apu.output.sample_fraction == partial &&
           gb.apu.output.filter.capacitor[0] == capacitor);
}

static void test_cgb_compatibility_uses_color_hardware(void) {
    CupidGb gb;
    prepare(&gb, true, false);
    gb.cgb_mode = false; /* A monochrome cartridge changes video mode, not APU hardware. */
    gb.apu.ch3_on = true;
    gb.apu.ch3_current_byte = 3;
    gb.apu.ch3_wave_access_ticks = 0;
    gb.io_registers[0x33] = 0x5a;
    assert(cupid_gb_read_u8(&gb, 0xff3f) == 0x5a);
    cupid_gb_write_u8(&gb, 0xff30, 0xa5);
    assert(gb.io_registers[0x33] == 0xa5);
    cupid_gb_write_u8(&gb, 0xff26, 0);
    cupid_gb_write_u8(&gb, 0xff16, 60);
    cupid_gb_write_u8(&gb, 0xff20, 60);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    assert(gb.apu.ch2_len == 0);
    assert(gb.apu.ch4_len == 0);
}

static void test_sweep_zero_period_and_shadow(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_write_u8(&gb, 0xff10, 0x01);
    cupid_gb_write_u8(&gb, 0xff12, 0xf0);
    cupid_gb_write_u8(&gb, 0xff13, 0);
    cupid_gb_write_u8(&gb, 0xff14, 0x82);
    for (unsigned i = 0; i < 64; ++i) {
        cupid_gb_apu_div_event(&gb);
    }
    assert(gb.apu.ch1_on);
    assert(gb.apu.ch1_freq == 0x200);
    cupid_gb_write_u8(&gb, 0xff10, 0x12);
    cupid_gb_write_u8(&gb, 0xff13, 4);
    cupid_gb_write_u8(&gb, 0xff14, 0x80);
    cupid_gb_write_u8(&gb, 0xff13, 0);
    assert(gb.apu.ch1_sweep_shadow == 4);
    for (unsigned i = 0; i < 3; ++i) {
        cupid_gb_apu_div_event(&gb);
    }
    assert(gb.apu.ch1_freq == 5);
    assert(gb.apu.ch1_sweep_shadow == 5);
}

static void tick_audio(CupidGb *gb, uint32_t cycles) {
    while (cycles) {
        uint16_t batch = cycles > 32768u ? 32768u : (uint16_t)cycles;
        cupid_gb_tick_apu(gb, batch);
        cycles -= batch;
    }
}

static void test_pulse_pitch(void) {
    const uint16_t frequencies[] = {0, 1024, 2040, 2047};
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); ++f) {
            CupidGb gb = {0};
            uint16_t frequency = frequencies[f];
            uint32_t period = (2048u - frequency) * 4u;
            uint16_t base = channel ? 0xff16u : 0xff11u;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, base, 0x80);
            cupid_gb_write_u8(&gb, (uint16_t)(base + 1), 0xf0);
            cupid_gb_write_u8(&gb, (uint16_t)(base + 2), (uint8_t)frequency);
            cupid_gb_write_u8(&gb, (uint16_t)(base + 3), (uint8_t)(0x80u | (frequency >> 8)));
            /* Start after the first reload: this checks sustained pitch, not trigger latency. */
            tick_audio(&gb, channel ? gb.apu.ch2_timer : gb.apu.ch1_timer);
            uint8_t start = channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos;
            for (unsigned edge = 1; edge <= 17; ++edge) {
                tick_audio(&gb, period - 1);
                assert((channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos) ==
                       ((start + edge - 1) & 7));
                tick_audio(&gb, 1);
                assert((channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos) ==
                       ((start + edge) & 7));
            }
        }
    }
}

static void check_pulse_reload(CupidGb *gb, CupidGbModel model, bool double_speed, unsigned channel,
                               unsigned age, bool high_write, uint8_t value) {
    *gb = (CupidGb){0};
    gb->model = model;
    gb->cgb_mode = model == CUPID_GB_MODEL_CGB;
    gb->double_speed = double_speed;
    cupid_gb_sgb_startup_reset(gb, false);
    const uint16_t base = channel ? 0xff16 : 0xff11;
    const uint16_t previous = high_write ? 0x6f0 : 0x7f0;
    const uint16_t frequency =
        high_write ? (uint16_t)((value << 8u) | 0xf0u) : (uint16_t)(0x700u | value);
    const uint32_t previous_period = (2048u - previous) * 4u;
    const uint32_t period = (2048u - frequency) * 4u;
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, base, 0xc0);
    cupid_gb_write_u8(gb, base + 1, 0xf0);
    cupid_gb_write_u8(gb, base + 2, (uint8_t)previous);
    cupid_gb_write_u8(gb, base + 3, (uint8_t)(0x80u | (previous >> 8u)));
    tick_audio(gb, (channel ? gb->apu.ch2_timer : gb->apu.ch1_timer) + age);
    assert(gb->apu.pulse_timing[channel].edge_age == age);
    const uint8_t position = channel ? gb->apu.ch2_duty_pos : gb->apu.ch1_duty_pos;
    const bool output_high = gb->apu.pulse_timing[channel].output_high;
    const uint8_t sample = gb->apu.dac_sample[channel];
    /* Either byte can replace the next period inside the two-base-clock reload
     * window. Later writes retain the old deadline. These high-byte cases avoid
     * the separate color duty-cancellation gate at the 0x700 boundary. */
    const uint32_t next_edge = (age < 2 ? period : previous_period) - age;
    cupid_gb_write_u8(gb, (uint16_t)(base + (high_write ? 3 : 2)), value);
    assert((channel ? gb->apu.ch2_freq : gb->apu.ch1_freq) == frequency);
    assert((channel ? gb->apu.ch2_duty_pos : gb->apu.ch1_duty_pos) == position);
    assert(gb->apu.pulse_timing[channel].output_high == output_high);
    assert(gb->apu.dac_sample[channel] == sample);
    tick_audio(gb, next_edge - 1u);
    assert((channel ? gb->apu.ch2_duty_pos : gb->apu.ch1_duty_pos) == position);
    tick_audio(gb, 1);
    assert((channel ? gb->apu.ch2_duty_pos : gb->apu.ch1_duty_pos) == ((position + 1u) & 7u));
    tick_audio(gb, period - 1u);
    assert((channel ? gb->apu.ch2_duty_pos : gb->apu.ch1_duty_pos) == ((position + 1u) & 7u));
    tick_audio(gb, 1);
    assert((channel ? gb->apu.ch2_duty_pos : gb->apu.ch1_duty_pos) == ((position + 2u) & 7u));
}

static void test_pulse_frequency_reload_window(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB};
    const uint8_t writes[2][2] = {{0xe0, 0xf8}, {5, 7}};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model)
        for (unsigned speed = 0; speed < 2; ++speed)
            for (unsigned channel = 0; channel < 2; ++channel)
                for (unsigned age = 0; age <= 4; ++age)
                    for (unsigned high = 0; high < 2; ++high)
                        for (unsigned change = 0; change < 2; ++change)
                            check_pulse_reload(gb, models[model], speed != 0, channel, age,
                                               high != 0, writes[high][change]);
    free(gb);
}

static void prepare_noise_phase(CupidGb *gb, CupidGbModel model, bool double_speed,
                                unsigned divisor, unsigned phase) {
    *gb = (CupidGb){0};
    gb->model = model;
    gb->cgb_mode = model == CUPID_GB_MODEL_CGB;
    gb->double_speed = double_speed;
    cupid_gb_sgb_startup_reset(gb, false);
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff21, 0xf0);
    cupid_gb_write_u8(gb, 0xff22, (uint8_t)(0x10u | divisor));
    gb->apu.noise_timing.clock_phase = (uint8_t)(phase * 2u);
}

static void test_noise_startup_clock_matrix(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB};
    /* First prescaler deadlines in 2 MHz clocks, before retrigger/history effects.
     * The second row family covers fast monochrome clocks used by music files. */
    static const uint8_t deadlines[2][8][4] = {{{6, 7, 6, 7},
                                                {10, 9, 8, 11},
                                                {10, 13, 12, 11},
                                                {14, 17, 16, 15},
                                                {18, 21, 20, 19},
                                                {22, 25, 24, 23},
                                                {26, 29, 28, 27},
                                                {30, 33, 32, 31}},
                                               {{8, 7, 8, 7},
                                                {10, 9, 12, 11},
                                                {14, 13, 12, 11},
                                                {18, 17, 16, 15},
                                                {22, 21, 20, 19},
                                                {26, 25, 24, 23},
                                                {30, 29, 28, 27},
                                                {34, 33, 32, 31}}};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        const bool color = models[model] == CUPID_GB_MODEL_CGB;
        for (unsigned speed = 0; speed < 2; ++speed) {
            const bool fast_monochrome = !color && speed;
            for (unsigned divisor = 0; divisor < 8; ++divisor) {
                for (unsigned phase = 0; phase < 4; ++phase) {
                    for (unsigned active = 0; active < 2; ++active) {
                        prepare_noise_phase(gb, models[model], speed != 0, divisor, phase);
                        gb->apu.ch4_on = active != 0;
                        gb->apu.noise_timing.background = active != 0;
                        gb->apu.noise_timing.remaining = active ? 7 : 0;
                        unsigned deadline = deadlines[fast_monochrome][divisor][phase];
                        if (active && divisor == 1 && (phase & 1))
                            deadline -= 4;
                        if (active && color && !divisor && (phase & 1))
                            deadline -= 2;
                        cupid_gb_noise_prepare(gb, active != 0);
                        assert(gb->apu.noise_timing.remaining == deadline);
                        assert(gb->apu.ch4_lfsr ==
                               (active && !divisor && phase == 3 ? 0x7faa : 0x7fff));
                        gb->apu.ch4_on = true;
                        gb->apu.ch4_vol = 15;
                        tick_audio(gb, deadline * 2u - 1u);
                        assert(gb->apu.noise_timing.divider_counter == 0);
                        tick_audio(gb, 1);
                        assert(gb->apu.noise_timing.divider_counter == 1);
                        const unsigned period = divisor ? divisor * 8u : 4u;
                        tick_audio(gb, period - 1u);
                        assert(gb->apu.noise_timing.divider_counter == 1);
                        tick_audio(gb, 1);
                        assert(gb->apu.noise_timing.divider_counter == 2);
                    }
                }
            }

            /* A retrigger can retain a carry immediately before the old counter
             * reload. Fast monochrome clocks expose one additional old tick. */
            for (unsigned divisor = 2; divisor < 8; ++divisor)
                for (unsigned phase = 0; phase < 4; ++phase)
                    for (unsigned remaining = 1; remaining <= 2; ++remaining) {
                        prepare_noise_phase(gb, models[model], speed != 0, divisor, phase);
                        gb->apu.ch4_on = true;
                        gb->apu.noise_timing.background = true;
                        gb->apu.noise_timing.remaining = (uint8_t)remaining;
                        cupid_gb_noise_prepare(gb, true);
                        assert(gb->apu.noise_timing.divider_counter ==
                               (remaining == 1 || fast_monochrome ? 1 : 0));
                    }

            /* Inactive background counting and a disconnected DAC affect startup
             * independently. Preserve their ordering around the speed adjustment. */
            const unsigned history_divisors[] = {0, 1, 5};
            for (unsigned item = 0; item < 3; ++item) {
                const unsigned divisor = history_divisors[item];
                for (unsigned phase = 0; phase < 4; ++phase)
                    for (unsigned dac = 0; dac < 2; ++dac)
                        for (unsigned started_off = 0; started_off < 2; ++started_off) {
                            prepare_noise_phase(gb, models[model], speed != 0, divisor, phase);
                            cupid_gb_write_u8(gb, 0xff21, dac ? 0xf0 : 0);
                            gb->apu.noise_timing.background = true;
                            gb->apu.noise_timing.started_dac_off = started_off != 0;
                            unsigned deadline = deadlines[fast_monochrome][divisor][phase];
                            if (!divisor) {
                                if (color && (phase & 1))
                                    deadline -= 2;
                                if (!phase && started_off)
                                    deadline += 28;
                                if (fast_monochrome)
                                    --deadline;
                            } else if (!phase && divisor == 1) {
                                deadline -= 4;
                            } else if (!phase && !dac) {
                                deadline += 4;
                            }
                            cupid_gb_noise_prepare(gb, false);
                            assert(gb->apu.noise_timing.remaining == deadline);
                            assert(gb->apu.noise_timing.foreground == (dac != 0));
                            assert(gb->apu.noise_timing.started_dac_off == (dac == 0));
                            assert(!gb->apu.noise_timing.output_high && !gb->apu.ch4_on);
                        }
            }
        }
    }
    free(gb);
}

static void test_pulse_trigger_length_enable(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB};
    CupidGb *subject = calloc(1, sizeof(*subject)), *control = calloc(1, sizeof(*control));
    assert(subject && control);
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        const unsigned speeds = models[model] == CUPID_GB_MODEL_CGB ? 2 : 1;
        for (unsigned speed = 0; speed < speeds; ++speed) {
            for (unsigned channel = 0; channel < 2; ++channel) {
                for (unsigned phase = 0; phase < 4; ++phase) {
                    *subject = (CupidGb){0};
                    subject->model = models[model];
                    cupid_gb_sgb_startup_reset(subject, false);
                    subject->cgb_mode = models[model] == CUPID_GB_MODEL_CGB;
                    subject->double_speed = speed != 0;
                    const uint16_t base = channel ? 0xff16 : 0xff11;
                    cupid_gb_write_u8(subject, 0xff26, 0x80);
                    cupid_gb_apu_set_highpass(subject, false);
                    cupid_gb_write_u8(subject, 0xff24, 0x70);
                    cupid_gb_write_u8(subject, 0xff25, (uint8_t)(0x10u << channel));
                    cupid_gb_write_u8(subject, base, 0xc0);
                    cupid_gb_write_u8(subject, base + 1, 0xf0);
                    cupid_gb_write_u8(subject, base + 3, 0x84);
                    const uint16_t first_edge =
                        channel ? subject->apu.ch2_timer : subject->apu.ch1_timer;
                    tick_audio(subject, first_edge + 4u + phase);
                    assert(subject->apu.clock_phase == phase);
                    assert(subject->apu.pulse_timing[channel].output_high);
                    subject->apu.fs_step = 1;
                    *control = *subject;
                    cupid_gb_write_u8(subject, base, 0xff); /* Length one, duty unchanged. */
                    cupid_gb_write_u8(control, base, 0xfe); /* Non-expiring control. */
                    int16_t actual[256], expected[256];
                    cupid_gb_apu_drain(subject, actual, 128);
                    cupid_gb_apu_drain(control, expected, 128);

                    /* The combined write reloads an expired length to 63 without
                     * interrupting the active pulse's held output or trigger timing. */
                    cupid_gb_write_u8(subject, base + 3, 0xc4);
                    cupid_gb_write_u8(control, base + 3, 0xc4);
                    assert((channel ? subject->apu.ch2_len : subject->apu.ch1_len) == 63);
                    assert((channel ? control->apu.ch2_len : control->apu.ch1_len) == 1);
                    assert(cupid_gb_read_u8(subject, 0xff26) & (1u << channel));
                    const CupidGbPulseTiming *timing = &subject->apu.pulse_timing[channel];
                    assert(timing->output_high && !timing->output_blocked);
                    assert(timing->start_delay == 6u + (phase & 2u));
                    assert(!memcmp(timing, &control->apu.pulse_timing[channel], sizeof(*timing)));
                    assert((channel ? subject->apu.ch2_timer : subject->apu.ch1_timer) ==
                           (channel ? control->apu.ch2_timer : control->apu.ch1_timer));
                    tick_audio(subject, 192);
                    tick_audio(control, 192);
                    unsigned count = cupid_gb_apu_drain(subject, actual, 128);
                    assert(count > 0 && count == cupid_gb_apu_drain(control, expected, 128));
                    assert(!memcmp(actual, expected, count * 2u * sizeof(*actual)));
                    assert(subject->apu.output.channels[channel].input[0] == -120);

                    const uint16_t remaining =
                        channel ? subject->apu.ch2_timer : subject->apu.ch1_timer;
                    const uint8_t position =
                        channel ? subject->apu.ch2_duty_pos : subject->apu.ch1_duty_pos;
                    tick_audio(subject, remaining - 1u);
                    assert((channel ? subject->apu.ch2_duty_pos : subject->apu.ch1_duty_pos) ==
                           position);
                    tick_audio(subject, 1);
                    assert((channel ? subject->apu.ch2_duty_pos : subject->apu.ch1_duty_pos) ==
                           ((position + 1u) & 7u));
                }
            }
        }
    }
    free(subject);
    free(control);
}

static void test_monochrome_pulse_clock_lifetime(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned channel = 0; channel < 2; ++channel) {
            CupidGb gb = {0};
            gb.model = models[model];
            cupid_gb_sgb_startup_reset(&gb, false);
            const uint16_t base = channel ? 0xff16 : 0xff11;
            uint16_t *timer = channel ? &gb.apu.ch2_timer : &gb.apu.ch1_timer;
            uint8_t *position = channel ? &gb.apu.ch2_duty_pos : &gb.apu.ch1_duty_pos;
            CupidGbPulseTiming *timing = &gb.apu.pulse_timing[channel];
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, base + 1, 0xf0);
            cupid_gb_write_u8(&gb, base + 2, 0xf0);
            cupid_gb_write_u8(&gb, base + 3, 7);
            tick_audio(&gb, 256);
            assert(*position == 0 && *timer == 0 && !timing->clock_started);
            /* A connected DAC alone cannot start the pulse frequency clock. */
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            assert(timing->clock_started && timing->output_blocked);
            tick_audio(&gb, 71);
            assert(*position == 0);
            tick_audio(&gb, 1);
            assert(*position == 1 && !timing->output_blocked);
            cupid_gb_write_u8(&gb, base, 63);
            cupid_gb_write_u8(&gb, base + 3, 0x47);
            cupid_gb_apu_div_event(&gb);
            assert(!(cupid_gb_read_u8(&gb, 0xff26) & (1u << channel)));
            /* Length expiry gates the channel output while its phase keeps moving. */
            tick_audio(&gb, 64);
            assert(*position == 2 && timing->clock_started);
            cupid_gb_write_u8(&gb, base + 1, 0);
            const uint16_t held_timer = *timer;
            tick_audio(&gb, 256);
            assert(*position == 2 && *timer == held_timer && !timing->clock_started);
            cupid_gb_write_u8(&gb, base + 1, 0xf0);
            tick_audio(&gb, 256);
            assert(*position == 2 && *timer == held_timer && !timing->clock_started);
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            tick_audio(&gb, 72);
            assert(*position == 3 && timing->clock_started);
            cupid_gb_write_u8(&gb, 0xff26, 0);
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            tick_audio(&gb, 256);
            assert(*position == 0 && *timer == 0 && !timing->output_high);
        }
    }
}

static void test_monochrome_pulse_duty_hold(void) {
    for (unsigned channel = 0; channel < 2; ++channel) {
        CupidGb gb = {0};
        const uint16_t base = channel ? 0xff16 : 0xff11;
        CupidGbPulseTiming *timing = &gb.apu.pulse_timing[channel];
        cupid_gb_write_u8(&gb, 0xff26, 0x80);
        cupid_gb_apu_set_highpass(&gb, false);
        cupid_gb_write_u8(&gb, 0xff24, 0x70);
        cupid_gb_write_u8(&gb, 0xff25, (uint8_t)(0x10u << channel));
        cupid_gb_write_u8(&gb, base, 0xc0);
        cupid_gb_write_u8(&gb, base + 1, 0xf0);
        cupid_gb_write_u8(&gb, base + 3, 0x80);
        tick_audio(&gb, 8200);
        assert(timing->output_high);
        cupid_gb_write_u8(&gb, base, 0);
        tick_audio(&gb, 192);
        assert(timing->output_high);
        assert(gb.apu.output.channels[channel].input[0] == -120);
        /* Retriggering changes volume immediately without resampling the new duty. */
        cupid_gb_write_u8(&gb, base + 1, 0xe0);
        cupid_gb_write_u8(&gb, base + 3, 0x80);
        tick_audio(&gb, 192);
        assert(timing->output_high);
        assert(gb.apu.output.channels[channel].input[0] == -104);
        const uint16_t remaining = channel ? gb.apu.ch2_timer : gb.apu.ch1_timer;
        tick_audio(&gb, remaining - 1u);
        assert(timing->output_high);
        tick_audio(&gb, 1);
        assert(!timing->output_high);
    }
}

static void test_monochrome_pulse_high_bank_edge(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model)
        for (unsigned channel = 0; channel < 2; ++channel)
            for (unsigned age = 0; age <= 2; ++age) {
                CupidGb gb = {0};
                gb.model = models[model];
                cupid_gb_sgb_startup_reset(&gb, false);
                const uint16_t base = channel ? 0xff16 : 0xff11;
                const uint8_t *position = channel ? &gb.apu.ch2_duty_pos : &gb.apu.ch1_duty_pos;
                const uint16_t *timer = channel ? &gb.apu.ch2_timer : &gb.apu.ch1_timer;
                const CupidGbPulseTiming *timing = &gb.apu.pulse_timing[channel];
                cupid_gb_write_u8(&gb, 0xff26, 0x80);
                cupid_gb_write_u8(&gb, base, 0xc0);
                cupid_gb_write_u8(&gb, base + 1, 0xf0);
                cupid_gb_write_u8(&gb, base + 2, 0xf3);
                cupid_gb_write_u8(&gb, base + 3, 0x87);
                tick_audio(&gb, *timer + age);
                assert(*position == 1 && timing->edge_age == age && timing->output_high);
                assert(gb.apu.dac_sample[channel] == 15);

                cupid_gb_write_u8(&gb, base + 3, 0x06);
                const unsigned retained_position = age < 2 ? 0 : 1;
                const unsigned next_edge = (age < 2 ? 1076u : 52u) - age;
                assert(*position == retained_position && *timer == next_edge);
                assert(timing->output_high && gb.apu.dac_sample[channel] == 15);
                tick_audio(&gb, next_edge - 1);
                assert(*position == retained_position && timing->output_high);
                tick_audio(&gb, 1);
                assert(*position == retained_position + 1);
            }
}

static void test_pulse_dac_hold(void) {
    for (unsigned color = 0; color < 2; ++color) {
        for (unsigned channel = 0; channel < 2; ++channel) {
            for (unsigned expire_length = 0; expire_length < 2; ++expire_length) {
                CupidGb gb = {0};
                gb.model = color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
                const uint16_t base = channel ? 0xff16 : 0xff11;
                cupid_gb_write_u8(&gb, 0xff26, 0x80);
                cupid_gb_apu_set_highpass(&gb, false);
                cupid_gb_write_u8(&gb, 0xff24, 0x70);
                cupid_gb_write_u8(&gb, 0xff25, (uint8_t)(0x10u << channel));
                cupid_gb_write_u8(&gb, base, 0xc0);
                cupid_gb_write_u8(&gb, base + 1, 0xf0);
                cupid_gb_write_u8(&gb, base + 3, 0x80);
                tick_audio(&gb, 8400);
                assert(gb.apu.output.channels[channel].input[0] == -120);
                for (unsigned toggle = 0; toggle < 2; ++toggle) {
                    cupid_gb_write_u8(&gb, base + 1, 0);
                    tick_audio(&gb, 192);
                    assert(!(channel ? gb.apu.ch2_dac : gb.apu.ch1_dac));
                    assert(gb.apu.output.channels[channel].input[0] == -120);
                    /* A DAC-only enable retains its prior code even though the
                     * output gate and the readable PCM tap remain inactive. */
                    cupid_gb_write_u8(&gb, base + 1, 0x80);
                    tick_audio(&gb, 192);
                    assert(!(cupid_gb_read_u8(&gb, 0xff26) & (1u << channel)));
                    assert(gb.apu.output.channels[channel].input[0] == -120);
                    if (color)
                        assert(cupid_gb_apu_read_pcm12(&gb) == 0);
                }
                if (expire_length) {
                    cupid_gb_write_u8(&gb, base, 63);
                    cupid_gb_write_u8(&gb, base + 3, 0x40);
                    gb.apu.fs_step = gb.apu.fs_skip_div_event = 0;
                    cupid_gb_apu_div_event(&gb);
                } else {
                    cupid_gb_write_u8(&gb, base + 3, 0x80);
                }
                tick_audio(&gb, 192);
                assert(gb.apu.output.channels[channel].input[0] == 120);
                /* Expiry and a new trigger both replace the held DAC code. */
                cupid_gb_write_u8(&gb, base + 1, 0);
                cupid_gb_write_u8(&gb, base + 1, 0xf0);
                tick_audio(&gb, 192);
                assert(gb.apu.output.channels[channel].input[0] == 120);
            }
        }
    }
}

static void test_noise_clock(void) {
    const uint32_t divisors[] = {8, 16, 32, 48, 64, 80, 96, 112};
    for (unsigned divisor = 0; divisor < 8; ++divisor) {
        for (unsigned shift = 0; shift < 4; ++shift) {
            CupidGb gb = {0};
            uint32_t period = divisors[divisor] << shift;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, 0xff21, 0xf0);
            cupid_gb_write_u8(&gb, 0xff22, (uint8_t)((shift << 4) | divisor));
            cupid_gb_write_u8(&gb, 0xff23, 0x80);
            const unsigned startup = divisor == 0 ? 12 : divisor == 1 ? 20 : divisor * 8 + 4;
            const unsigned first_rise = startup + ((1u << shift) - 1u) * divisors[divisor] / 2;
            tick_audio(&gb, first_rise);
            /* Reset clears the physical feedback latch. Its first rising edge
             * shifts that zero, before a falling edge has captured feedback. */
            assert(gb.apu.ch4_lfsr == 0x7fff);
            uint16_t previous = gb.apu.ch4_lfsr;
            for (unsigned edge = 0; edge < 24; ++edge) {
                tick_audio(&gb, period - 1);
                assert(gb.apu.ch4_lfsr == previous);
                tick_audio(&gb, 1);
                assert(gb.apu.ch4_lfsr != previous);
                previous = gb.apu.ch4_lfsr;
            }
        }
    }
    for (unsigned shift = 14; shift <= 15; ++shift) {
        CupidGb gb = {0};
        cupid_gb_write_u8(&gb, 0xff26, 0x80);
        cupid_gb_write_u8(&gb, 0xff21, 0xf0);
        cupid_gb_write_u8(&gb, 0xff22, (uint8_t)(shift << 4));
        cupid_gb_write_u8(&gb, 0xff23, 0x80);
        tick_audio(&gb, 1u << 21);
        assert(gb.apu.ch4_on);
        assert(gb.apu.ch4_lfsr == 0x7fff);
    }
}

static void test_monochrome_noise_latches(void) {
    CupidGb gb = {0};
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff22, 0x10);
    tick_audio(&gb, 4);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    assert(!gb.apu.ch4_on && gb.apu.noise_timing.start_delay == 12);
    tick_audio(&gb, 11);
    assert(!gb.apu.ch4_on && gb.apu.noise_timing.start_delay == 1);
    tick_audio(&gb, 1);
    assert(gb.apu.ch4_on && !gb.apu.noise_timing.start_delay);
    assert(!gb.apu.noise_timing.feedback_latch);
    tick_audio(&gb, 16);
    assert(gb.apu.noise_timing.divider_counter == 2);
    assert(gb.apu.ch4_lfsr == 0x7fff && !gb.apu.noise_timing.feedback_latch);
    tick_audio(&gb, 8);
    assert(gb.apu.noise_timing.divider_counter == 4);
    assert(gb.apu.ch4_lfsr == 0x7fff && gb.apu.noise_timing.feedback_latch);
    tick_audio(&gb, 8);
    assert(gb.apu.noise_timing.divider_counter == 6 && gb.apu.ch4_lfsr == 0x3fff);

    /* At a reload edge the selector sees counter 6 together with counter 5.
     * The intermediate write captures feedback; the final selector adds a
     * narrow shift even though the requested width is wide. */
    cupid_gb_write_u8(&gb, 0xff22, 0);
    assert(gb.apu.ch4_lfsr == 0x1fbf);
    tick_audio(&gb, 4);
    assert(gb.apu.ch4_lfsr == 0x0fdf);

    gb.apu.ch4_on = false;
    uint16_t counter = gb.apu.noise_timing.divider_counter;
    tick_audio(&gb, 16);
    assert(gb.apu.noise_timing.divider_counter != counter);
    cupid_gb_write_u8(&gb, 0xff21, 0);
    counter = gb.apu.noise_timing.divider_counter;
    tick_audio(&gb, 16);
    assert(gb.apu.noise_timing.divider_counter == counter);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    tick_audio(&gb, 16);
    assert(gb.apu.noise_timing.divider_counter == counter);
}

static void test_partial_drain(void) {
    CupidGb gb = {0};
    int16_t output[12];
    const int16_t frames[] = {1, -1, 2, -2, 3, -3, 4, -4};
    memcpy(gb.apu.output.buf, frames, sizeof(frames));
    gb.apu.output.buf_write = 4;
    assert(cupid_gb_apu_drain(&gb, output, 0) == 0);
    assert(gb.apu.output.buf_write == 4);
    assert(cupid_gb_apu_drain(&gb, output, 1) == 1);
    assert(memcmp(output, frames, 2 * sizeof(int16_t)) == 0);
    assert(gb.apu.output.buf_write == 3);
    assert(cupid_gb_apu_drain(&gb, output, 6) == 3);
    assert(memcmp(output, frames + 2, 6 * sizeof(int16_t)) == 0);
    assert(gb.apu.output.buf_write == 0);
    assert(cupid_gb_apu_drain(&gb, output, 6) == 0);
}

static void test_stop_oscillator(bool color) {
    CupidGb gb;
    prepare(&gb, color, false);
    gb.cpu.stopped = true;
    gb.apu.ch2_timer = 4;
    gb.apu.ch2_freq = 2047;
    uint8_t duty_position = gb.apu.ch2_duty_pos;
    tick_audio(&gb, 100);
    assert(gb.apu.ch2_duty_pos == ((duty_position + (color ? 25 : 0)) & 7));
    assert(gb.apu.output.buf_write == 1); /* STOP does not stop the host's audio stream. */
}

static void prepare_dc(CupidGb *gb) {
    *gb = (CupidGb){0};
    cupid_gb_sgb_startup_reset(gb, false);
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff12, 0xf0); /* DAC connected, channel not triggered: code zero. */
    cupid_gb_write_u8(gb, 0xff24, 0x70);
    cupid_gb_write_u8(gb, 0xff25, 0x10);
}

static CupidGbAudioFrame settled_output(CupidGb *gb) {
    int16_t frames[CUPID_GB_APU_BUF_FRAMES * 2];
    (void)cupid_gb_apu_drain(gb, frames, CUPID_GB_APU_BUF_FRAMES);
    tick_audio(gb, 8192);
    const uint32_t count = cupid_gb_apu_drain(gb, frames, CUPID_GB_APU_BUF_FRAMES);
    assert(count > CUPID_GB_AUDIO_TAPS);
    return (CupidGbAudioFrame){frames[(count - 1) * 2], frames[(count - 1) * 2 + 1]};
}

static void test_dac_transfer_and_pan(void) {
    CupidGb gb;
    prepare_dc(&gb);
    cupid_gb_apu_set_highpass(&gb, false);
    gb.cpu.stopped = true; /* Hold synthetic digital levels while output settles. */
    CupidGbAudioFrame frame = settled_output(&gb);
    assert(frame.left == 4080 && frame.right == 0);
    gb.apu.ch1_on = true;
    gb.apu.ch1_duty = 0;
    gb.apu.ch1_duty_pos = 7;
    gb.apu.ch1_vol = 15;
    gb.apu.ch1_timer = 8192;
    cupid_gb_apu_import_legacy(&gb);
    cupid_gb_audio_import_legacy(&gb);
    frame = settled_output(&gb);
    assert(frame.left == -4080 && frame.right == 0);
    cupid_gb_write_u8(&gb, 0xff25, 1);
    frame = settled_output(&gb);
    assert(frame.left == 0 && frame.right == -510);
    cupid_gb_write_u8(&gb, 0xff12, 0);
    frame = settled_output(&gb);
    assert(frame.left == 0 && frame.right == 0);
}

static void test_highpass_dc_and_disconnect(void) {
    CupidGb gb;
    int16_t output[CUPID_GB_APU_BUF_FRAMES * 2];
    prepare_dc(&gb);
    tick_audio(&gb, 96000);
    uint32_t count = cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
    assert(count > 1000);
    assert(output[0] == 0 && output[1] == 0);
    assert(output[96 * 2] > 1000);
    for (uint32_t i = 96; i < count; ++i) {
        assert(output[i * 2] >= 0);
        assert(output[i * 2 + 1] == 0);
    }
    assert(output[(count - 1) * 2] < 100);
    cupid_gb_write_u8(&gb, 0xff12, 0);
    tick_audio(&gb, 4096); /* Retire the monochrome DAC connection delay. */
    (void)cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
    double capacitor = gb.apu.output.filter.capacitor[0];
    tick_audio(&gb, 960);
    count = cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
    assert(count >= 10);
    assert(gb.apu.output.filter.capacitor[0] ==
           capacitor); /* Disconnected DMG capacitor holds charge. */
    for (uint32_t i = 0; i < count * 2; ++i) {
        assert(output[i] == 0);
    }
    gb.model = CUPID_GB_MODEL_CGB;
    tick_audio(&gb, 960);
    assert(gb.apu.output.filter.capacitor[0] <
           capacitor); /* Color hardware keeps the output connected. */
}

static void test_duty_phase(void) {
    const char *waveforms[] = {"00000001", "10000001", "10000111", "01111110"};
    CupidGb gb;
    int16_t output[8];
    prepare_dc(&gb);
    cupid_gb_apu_set_highpass(&gb, false);
    cupid_gb_write_u8(&gb, 0xff17, 0xf0);
    cupid_gb_write_u8(&gb, 0xff25, 0x30);
    gb.apu.ch1_on = true;
    gb.apu.ch2_on = true;
    gb.apu.ch1_vol = 15;
    gb.apu.ch2_vol = 15;
    for (unsigned phase = 0; phase < 8; ++phase) {
        for (unsigned duty = 0; duty < 4; ++duty) {
            gb.apu.ch1_duty_pos = (uint8_t)phase;
            gb.apu.ch2_duty_pos = (uint8_t)phase;
            gb.apu.ch1_duty = (uint8_t)duty;
            gb.apu.ch2_duty = (uint8_t)duty;
            gb.apu.ch1_timer = 8192;
            gb.apu.ch2_timer = 8192;
            cupid_gb_apu_import_legacy(&gb); /* Materialize this synthetic duty/phase. */
            cupid_gb_audio_import_legacy(&gb);
            tick_audio(&gb, 192);
            uint32_t count = cupid_gb_apu_drain(&gb, output, 4);
            assert(count >= 2);
            assert(output[(count - 1) * 2] == (waveforms[duty][phase] == '1' ? -8160 : 8160));
            assert(output[(count - 1) * 2 + 1] == 0);
        }
    }
}

static void test_sample_rates_and_batching(void) {
    const uint32_t rates[] = {8000, 22050, 44100, 48000, 96000, 192000};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        CupidGb gb = {0};
        int16_t output[CUPID_GB_APU_BUF_FRAMES * 2];
        uint32_t count = 0;
        uint32_t cycles = 4194304;
        assert(cupid_gb_apu_set_sample_rate(&gb, rates[r]));
        assert(cupid_gb_apu_get_sample_rate(&gb) == rates[r]);
        while (cycles) {
            uint16_t batch = cycles > 8192 ? 8192 : (uint16_t)cycles;
            tick_audio(&gb, batch);
            count += cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
            cycles -= batch;
        }
        assert(count == rates[r]);
        assert(gb.apu.output.sample_acc == 0);
        assert(gb.apu.output.sample_fraction < (1u << 28));
        assert(!cupid_gb_apu_set_sample_rate(&gb, 7999));
        assert(!cupid_gb_apu_set_sample_rate(&gb, 192001));
        assert(cupid_gb_apu_get_sample_rate(&gb) == rates[r]);
    }

    CupidGb one;
    CupidGb batch;
    prepare_dc(&one);
    cupid_gb_write_u8(&one, 0xff11, 0x80);
    cupid_gb_write_u8(&one, 0xff13, 0xf8);
    cupid_gb_write_u8(&one, 0xff14, 0x87);
    batch = one;
    for (unsigned i = 0; i < 12000; ++i) {
        cupid_gb_tick_apu(&one, 1);
    }
    tick_audio(&batch, 12000);
    assert(one.apu.output.buf_write == batch.apu.output.buf_write);
    assert(memcmp(one.apu.output.buf, batch.apu.output.buf,
                  one.apu.output.buf_write * 2 * sizeof(int16_t)) == 0);
    assert(one.apu.output.sample_fraction == batch.apu.output.sample_fraction);
    assert(one.apu.output.sample_acc == batch.apu.output.sample_acc);
    assert(one.apu.output.filter.capacitor[0] == batch.apu.output.filter.capacitor[0]);
    assert(one.apu.ch1_duty_pos == batch.apu.ch1_duty_pos);
}

static void test_physical_model_clocks(void) {
    assert(cupid_gb_base_clock_hz(NULL) == 4194304u);
    double handheld_filter = 0;
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb gb = {0};
        gb.model = (CupidGbModel)model;
        cupid_gb_sgb_startup_reset(&gb, false);
        uint32_t clock_hz = model == CUPID_GB_MODEL_SGB ? 4295454u : 4194304u;
        assert(cupid_gb_base_clock_hz(&gb) == clock_hz);
        gb.double_speed = true;
        assert(cupid_gb_base_clock_hz(&gb) == clock_hz);
        assert(cupid_gb_apu_set_sample_rate(&gb, 48000));
        if (model == CUPID_GB_MODEL_DMG_ABC) {
            handheld_filter = gb.apu.output.filter.retention;
        }
        assert(model == CUPID_GB_MODEL_SGB ? gb.apu.output.filter.retention < handheld_filter
                                           : gb.apu.output.filter.retention == handheld_filter);
        uint32_t count = 0, cycles = clock_hz;
        int16_t output[CUPID_GB_APU_BUF_FRAMES * 2];
        while (cycles) {
            uint16_t chunk = cycles > 8192 ? 8192 : (uint16_t)cycles;
            tick_audio(&gb, chunk);
            count += cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
            cycles -= chunk;
        }
        assert(count == 48000 && gb.apu.output.sample_acc == 0);
    }

    CupidGb one, batch;
    prepare_dc(&one);
    one.model = CUPID_GB_MODEL_SGB;
    cupid_gb_apu_set_highpass(&one, false);
    cupid_gb_audio_import_legacy(&one);
    batch = one;
    for (unsigned dot = 0; dot < 12000; ++dot) {
        cupid_gb_tick_apu(&one, 1);
    }
    tick_audio(&batch, 12000);
    assert(one.apu.output.buf_write && one.apu.output.buf_write == batch.apu.output.buf_write);
    assert(memcmp(one.apu.output.buf, batch.apu.output.buf,
                  one.apu.output.buf_write * 2 * sizeof(int16_t)) == 0);
    assert(one.apu.output.sample_acc == batch.apu.output.sample_acc);
    assert(one.apu.output.sample_fraction == batch.apu.output.sample_fraction &&
           one.apu.output.render_cycles == batch.apu.output.render_cycles);
    for (unsigned frame = 0; frame < one.apu.output.buf_write; ++frame) {
        /* Clock changes affect pitch and sample boundaries, not the DAC's DC gain. */
        assert(one.apu.output.buf[frame * 2] == 4080 && one.apu.output.buf[frame * 2 + 1] == 0);
    }
}

static void test_fast_pulse_integration(void) {
    CupidGb gb;
    int16_t output[CUPID_GB_APU_BUF_FRAMES * 2];
    prepare_dc(&gb);
    cupid_gb_apu_set_highpass(&gb, false);
    cupid_gb_write_u8(&gb, 0xff11, 0x80); /* 50 percent duty, 131072 Hz fundamental. */
    cupid_gb_write_u8(&gb, 0xff13, 0xff);
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    tick_audio(&gb, 32768);
    uint32_t count = cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
    assert(count > 300);
    /* After the reconstruction tail, an ultrasonic pulse must not produce
     * full-amplitude aliases in the audible output. */
    for (uint32_t i = CUPID_GB_AUDIO_TAPS; i < count; ++i) {
        assert(output[i * 2] > -1400 && output[i * 2] < 1400);
        assert(output[i * 2 + 1] == 0);
    }
}

static void test_pcm_taps(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    gb.cgb_mode = false; /* Digital taps belong to the hardware, not the cartridge mode. */
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    gb.apu.ch1_on = true;
    gb.apu.ch1_dac = true;
    gb.apu.ch1_vol = 5;
    gb.apu.ch1_duty = 0;
    gb.apu.ch1_duty_pos = 7;
    gb.apu.ch2_on = true;
    gb.apu.ch2_dac = true;
    gb.apu.ch2_vol = 12;
    gb.apu.ch2_duty = 1;
    gb.apu.ch2_duty_pos = 0;
    gb.apu.ch3_on = true;
    gb.apu.ch3_dac = true;
    gb.apu.ch3_sample = 13;
    gb.apu.ch3_out_level = 3;
    gb.apu.ch4_on = true;
    gb.apu.ch4_dac = true;
    gb.apu.ch4_vol = 9;
    gb.apu.ch4_lfsr = 0;
    cupid_gb_apu_import_legacy(&gb); /* Materialize the synthetic fixture's digital latches. */
    assert(cupid_gb_apu_read_pcm12(&gb) == 0xc5);
    assert(cupid_gb_apu_read_pcm34(&gb) == 0x93);
    for (unsigned color_mode = 0; color_mode < 2; ++color_mode) {
        gb.cgb_mode = color_mode != 0;
        /* Software reads the memory bus, so direct tap tests alone cannot
         * establish that the hardware registers are actually reachable. */
        assert(cupid_gb_read_u8(&gb, 0xff76) == 0xc5);
        assert(cupid_gb_read_u8(&gb, 0xff77) == 0x93);
    }
    cupid_gb_write_u8(&gb, 0xff24, 0x77);
    cupid_gb_write_u8(&gb, 0xff25, 0xff);
    cupid_gb_apu_set_highpass(&gb, false);
    assert(cupid_gb_apu_read_pcm12(&gb) == 0xc5);
    assert(cupid_gb_apu_read_pcm34(&gb) == 0x93);
    const uint8_t wave_outputs[] = {0, 13, 6, 3};
    for (unsigned level = 0; level < 4; ++level) {
        cupid_gb_write_u8(&gb, 0xff1c, (uint8_t)(level << 5));
        assert(cupid_gb_apu_read_pcm34(&gb) == (0x90 | wave_outputs[level]));
    }
    gb.apu.ch1_on = false;
    gb.apu.ch4_lfsr = 1;
    assert(cupid_gb_apu_read_pcm34(&gb) == 0x93); /* The sample is held between shift edges. */
    cupid_gb_apu_import_legacy(&gb);
    assert(cupid_gb_apu_read_pcm12(&gb) == 0xc0);
    assert(cupid_gb_apu_read_pcm34(&gb) == 3);
    cupid_gb_write_u8(&gb, 0xff17, 0);
    cupid_gb_write_u8(&gb, 0xff1a, 0);
    assert(cupid_gb_apu_read_pcm12(&gb) == 0);
    assert(cupid_gb_apu_read_pcm34(&gb) == 0);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        gb.model = (CupidGbModel)model;
        assert(cupid_gb_apu_read_pcm12(&gb) == (model == CUPID_GB_MODEL_CGB ? 0 : 0xff));
        assert(cupid_gb_apu_read_pcm34(&gb) == (model == CUPID_GB_MODEL_CGB ? 0 : 0xff));
        assert(cupid_gb_read_u8(&gb, 0xff76) == (model == CUPID_GB_MODEL_CGB ? 0 : 0xff));
        assert(cupid_gb_read_u8(&gb, 0xff77) == (model == CUPID_GB_MODEL_CGB ? 0 : 0xff));
    }
}

static void set_length_state(CupidGb *gb, unsigned channel, uint16_t count, bool enable) {
    switch (channel) {
        case 0:
            gb->apu.ch1_len = (uint8_t)count;
            gb->apu.ch1_len_en = enable;
            gb->apu.ch1_on = count != 0;
            break;
        case 1:
            gb->apu.ch2_len = (uint8_t)count;
            gb->apu.ch2_len_en = enable;
            gb->apu.ch2_on = count != 0;
            break;
        case 2:
            gb->apu.ch3_len = count;
            gb->apu.ch3_len_en = enable;
            gb->apu.ch3_on = count != 0;
            break;
        default:
            gb->apu.ch4_len = (uint8_t)count;
            gb->apu.ch4_len_en = enable;
            gb->apu.ch4_on = count != 0;
            break;
    }
}

static uint16_t get_length_count(const CupidGb *gb, unsigned channel) {
    const uint16_t counts[] = {gb->apu.ch1_len, gb->apu.ch2_len, gb->apu.ch3_len, gb->apu.ch4_len};
    return counts[channel];
}

static void test_length_control_matrix(void) {
    const uint16_t registers[] = {0xff14, 0xff19, 0xff1e, 0xff23};
    for (unsigned startup = 0; startup < 2; ++startup) {
        for (unsigned edges = 0; edges < 10; ++edges) {
            for (unsigned channel = 0; channel < 4; ++channel) {
                for (unsigned count = 0; count < 3; ++count) {
                    for (unsigned controls = 0; controls < 8; ++controls) {
                        CupidGb gb = {0};
                        bool old_enable = (controls & 1) != 0;
                        bool enable = (controls & 2) != 0;
                        bool trigger = (controls & 4) != 0;
                        bool extra =
                            startup && edges < 2 ? true : ((edges - (startup ? 1u : 0u)) & 1u) != 0;
                        gb.model = CUPID_GB_MODEL_CGB;
                        gb.io_registers[0x04] = startup ? 0x10 : 0;
                        cupid_gb_write_u8(&gb, 0xff26, 0x80);
                        cupid_gb_write_u8(&gb, 0xff12, 0xf0);
                        cupid_gb_write_u8(&gb, 0xff17, 0xf0);
                        cupid_gb_write_u8(&gb, 0xff1a, 0x80);
                        cupid_gb_write_u8(&gb, 0xff21, 0xf0);
                        for (unsigned edge = 0; edge < edges; ++edge) {
                            cupid_gb_apu_div_event(&gb);
                        }
                        set_length_state(&gb, channel, (uint16_t)count, old_enable);
                        unsigned consumed = extra && enable && !old_enable && count ? 1u : 0u;
                        unsigned expected = count - consumed;
                        bool active = expected != 0;
                        if (trigger) {
                            if (!expected) {
                                expected =
                                    (channel == 2 ? 256u : 64u) - (extra && enable ? 1u : 0u);
                            }
                            active = true;
                        }
                        cupid_gb_write_u8(&gb, registers[channel],
                                          (uint8_t)((enable ? 0x40 : 0) | (trigger ? 0x80 : 0)));
                        assert(get_length_count(&gb, channel) == expected);
                        assert(((cupid_gb_read_u8(&gb, 0xff26) >> channel) & 1u) == active);
                    }
                }
            }
        }
    }
}

static void test_power_preserves_output_pipeline(bool color) {
    CupidGb gb;
    prepare_dc(&gb);
    gb.model = color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
    assert(cupid_gb_apu_set_sample_rate(&gb, 48000));
    cupid_gb_write_u8(&gb, 0xff11, 0x80);
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    tick_audio(&gb, 200);
    for (unsigned byte = 0; byte < 16; ++byte) {
        cupid_gb_write_u8(&gb, (uint16_t)(0xff30 + byte), (uint8_t)(byte * 13));
    }
    CupidGbApu pipeline = gb.apu;
    assert(pipeline.output.buf_write != 0 && pipeline.output.sample_acc != 0);
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.output.sample_rate == pipeline.output.sample_rate);
    assert(gb.apu.output.sample_acc == pipeline.output.sample_acc);
    assert(gb.apu.output.sample_fraction == pipeline.output.sample_fraction);
    assert(gb.apu.output.filter.capacitor[0] == pipeline.output.filter.capacitor[0]);
    assert(gb.apu.output.filter.retention == pipeline.output.filter.retention);
    assert(gb.apu.output.buf_write == pipeline.output.buf_write);
    assert(memcmp(gb.apu.output.buf, pipeline.output.buf, sizeof(pipeline.output.buf)) == 0);
    assert(gb.apu.ch1_freq == 0 && gb.apu.ch1_timer == 0 && gb.apu.ch1_duty_pos == 0);
    assert(gb.apu.ch1_vol == 0 && !gb.apu.ch1_dac && !gb.apu.ch1_on);
    for (unsigned reg = 0x10; reg < 0x26; ++reg) {
        assert(gb.io_registers[reg] == 0);
    }
    for (unsigned byte = 0; byte < 16; ++byte) {
        assert(cupid_gb_read_u8(&gb, (uint16_t)(0xff30 + byte)) == byte * 13);
    }
    cupid_gb_write_u8(&gb, 0xff11, 63);
    cupid_gb_write_u8(&gb, 0xff16, 62);
    cupid_gb_write_u8(&gb, 0xff1b, 253);
    cupid_gb_write_u8(&gb, 0xff20, 60);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    for (unsigned channel = 0; channel < 4; ++channel) {
        assert(get_length_count(&gb, channel) == (color ? 0 : channel + 1));
    }
    assert(gb.apu.output.sample_acc == pipeline.output.sample_acc);
    assert(gb.apu.output.sample_fraction == pipeline.output.sample_fraction);
    assert(gb.apu.output.filter.capacitor[0] == pipeline.output.filter.capacitor[0]);
    assert(gb.apu.output.buf_write == pipeline.output.buf_write);
}

static void test_color_pulse_clock_and_held_duty(void) {
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned phase = 0; phase < 4; phase += 2) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            uint16_t duty = channel ? 0xff16 : 0xff11;
            uint16_t envelope = channel ? 0xff17 : 0xff12;
            uint16_t low = channel ? 0xff18 : 0xff13;
            uint16_t high = channel ? 0xff19 : 0xff14;
            unsigned shift = channel * 4;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_tick_apu(&gb, 100);
            assert(gb.apu.ch1_duty_pos == 0 && gb.apu.ch2_duty_pos == 0);
            cupid_gb_tick_apu(&gb, (uint16_t)phase);
            cupid_gb_write_u8(&gb, duty, 0);
            cupid_gb_write_u8(&gb, envelope, 0xf0);
            cupid_gb_write_u8(&gb, low, 0xff);
            cupid_gb_write_u8(&gb, high, 0x87);
            unsigned first_high = 36 + phase;
            cupid_gb_tick_apu(&gb, (uint16_t)(first_high - 1));
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 0);
            cupid_gb_tick_apu(&gb, 1);
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 15);
            cupid_gb_write_u8(&gb, duty, 0xc0); /* New duty is low at phase seven. */
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 15);
            cupid_gb_tick_apu(&gb, 3);
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 15);
            cupid_gb_tick_apu(&gb, 1);
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 0);
            cupid_gb_write_u8(&gb, envelope, 0);
            unsigned index = channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos;
            cupid_gb_tick_apu(&gb, 100);
            assert((channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos) == index);
        }
    }
}

static void test_monochrome_envelope_retrigger_between_divider_edges(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_DMG_ABC;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff12, 0xf1);
    cupid_gb_write_u8(&gb, 0xff17, 0xf1);
    cupid_gb_write_u8(&gb, 0xff21, 0xf1);

    for (unsigned trigger = 0; trigger < 2; ++trigger) {
        cupid_gb_write_u8(&gb, 0xff14, 0x80);
        cupid_gb_write_u8(&gb, 0xff19, 0x80);
        cupid_gb_write_u8(&gb, 0xff23, 0x80);
        cupid_gb_tick(&gb, trigger ? 1024 : 7 * 2048 + 1024);
        assert(gb.apu.ch1_vol == 15 && gb.apu.ch2_vol == 15 && gb.apu.ch4_vol == 15);
    }

    cupid_gb_tick(&gb, 7 * 2048 + 1024);
    assert(gb.apu.ch1_vol == 15 && gb.apu.ch2_vol == 15 && gb.apu.ch4_vol == 15);
    cupid_gb_tick(&gb, 1024);
    assert(gb.apu.ch1_vol == 14 && gb.apu.ch2_vol == 14 && gb.apu.ch4_vol == 14);
}

static void test_color_envelope_edges(void) {
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            gb.double_speed = speed != 0;
            uint16_t envelope = channel ? 0xff17 : 0xff12;
            uint16_t high = channel ? 0xff19 : 0xff14;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, envelope, 0xf0);
            cupid_gb_write_u8(&gb, high, 0x80);
            cupid_gb_write_u8(&gb, envelope, 0xf8);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 0);
            cupid_gb_write_u8(&gb, envelope, 0xf8);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 1);
            cupid_gb_write_u8(&gb, envelope, 0xf1);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 14);
            cupid_gb_write_u8(&gb, high, 0x80);
            for (unsigned edge = 0; edge < 7; ++edge) {
                cupid_gb_apu_div_event(&gb);
            }
            cupid_gb_apu_div_secondary_event(&gb);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 15);
            cupid_gb_apu_div_event(&gb);
            if (speed) {
                cupid_gb_tick_apu(&gb, 1);
                assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 15);
                cupid_gb_tick_apu(&gb, 1);
            }
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 14);

            cupid_gb_write_u8(&gb, envelope, 0xf9);
            cupid_gb_write_u8(&gb, high, 0x80);
            for (unsigned edge = 0; edge < 7; ++edge) {
                cupid_gb_apu_div_event(&gb);
            }
            cupid_gb_apu_div_secondary_event(&gb);
            cupid_gb_write_u8(&gb, envelope, 0xf1);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 1);
            cupid_gb_apu_div_event(&gb);
            cupid_gb_tick_apu(&gb, 2);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 1);
            cupid_gb_write_u8(&gb, 0xff26, 0);
            assert(gb.apu.envelope_delay == 0);
            assert(!gb.apu.pulse_timing[channel].envelope.locked);
            assert(gb.apu.pulse_timing[channel].edge_age == 4);
        }
    }
}

static void test_color_frequency_reload_window(void) {
    for (unsigned age = 0; age <= 4; age += 2) {
        CupidGb gb = {0};
        gb.model = CUPID_GB_MODEL_CGB;
        cupid_gb_write_u8(&gb, 0xff26, 0x80);
        cupid_gb_write_u8(&gb, 0xff12, 0xf0);
        cupid_gb_write_u8(&gb, 0xff13, 0xfc);
        cupid_gb_write_u8(&gb, 0xff14, 0x87);
        cupid_gb_tick_apu(&gb, (uint16_t)(104 + age));
        assert(gb.apu.ch1_duty_pos == 6);
        cupid_gb_write_u8(&gb, 0xff14, 0);
        assert(cupid_gb_read_u8(&gb, 0xff76) == 0);
        cupid_gb_tick_apu(&gb, 32);
        assert(cupid_gb_read_u8(&gb, 0xff76) == (age <= 2 ? 0 : 15));
    }
}

static void test_color_sweep_live_frequency_and_latency(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff10, 0x1f);
    cupid_gb_write_u8(&gb, 0xff12, 0x80);
    cupid_gb_write_u8(&gb, 0xff13, 0xff);
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    cupid_gb_tick_apu(&gb, 64);
    for (unsigned edge = 0; edge < 3; ++edge) {
        cupid_gb_apu_div_event(&gb);
    }
    assert(gb.apu.ch1_freq == 0x7f0);
    assert(gb.io_registers[0x13] == 0xff); /* Sweep output is not the software write latch. */
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    assert(gb.apu.ch1_freq == 0x7f0);

    cupid_gb_write_u8(&gb, 0xff26, 0);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff10, 0x17);
    cupid_gb_write_u8(&gb, 0xff12, 0x80);
    cupid_gb_write_u8(&gb, 0xff13, 0xff);
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    assert(cupid_gb_read_u8(&gb, 0xff26) & 1);
    cupid_gb_tick_apu(&gb, 37);
    assert(cupid_gb_read_u8(&gb, 0xff26) & 1);
    cupid_gb_tick_apu(&gb, 1);
    assert(!(cupid_gb_read_u8(&gb, 0xff26) & 1));
}

static void test_color_sweep_overflow_mixer_edge(void) {
    for (unsigned sample_boundary = 0; sample_boundary < 2; ++sample_boundary) {
        CupidGb batch = {0};
        batch.model = CUPID_GB_MODEL_CGB;
        cupid_gb_write_u8(&batch, 0xff26, 0x80);
        cupid_gb_write_u8(&batch, 0xff10, 0x17);
        cupid_gb_write_u8(&batch, 0xff11, 0x80);
        cupid_gb_write_u8(&batch, 0xff12, 0x80);
        cupid_gb_write_u8(&batch, 0xff13, 0xff);
        cupid_gb_write_u8(&batch, 0xff14, 0x87);
        cupid_gb_write_u8(&batch, 0xff24, 0x77);
        cupid_gb_write_u8(&batch, 0xff25, 0xff);
        cupid_gb_tick_apu(&batch, 36);
        if (sample_boundary) {
            batch.apu.output.sample_acc =
                cupid_gb_base_clock_hz(&batch) - 2 * CUPID_GB_APU_SAMPLE_RATE;
        }
        CupidGb split = batch;
        cupid_gb_tick_apu(&batch, 4);
        for (unsigned dot = 0; dot < 4; ++dot) {
            cupid_gb_tick_apu(&split, 1);
        }
        assert(!batch.apu.ch1_on && !split.apu.ch1_on);
        /* Channel shutdown changes the mixer on its own base-clock edge,
         * including partial integration and a host sample crossing that edge. */
        assert(memcmp(&batch.apu, &split.apu, sizeof(batch.apu)) == 0);
    }
}

static void test_color_noise_prescaler_and_reload(void) {
    static const uint8_t start_ticks[4][8] = {
        {6, 10, 10, 14, 18, 22, 26, 30},
        {7, 9, 13, 17, 21, 25, 29, 33},
        {6, 8, 12, 16, 20, 24, 28, 32},
        {7, 11, 11, 15, 19, 23, 27, 31},
    };
    for (unsigned phase = 0; phase < 4; ++phase) {
        for (unsigned divisor = 0; divisor < 8; ++divisor) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_tick_apu(&gb, (uint16_t)(phase * 2));
            cupid_gb_write_u8(&gb, 0xff21, 0xf0);
            cupid_gb_write_u8(&gb, 0xff22, (uint8_t)divisor);
            cupid_gb_write_u8(&gb, 0xff23, 0x80);
            assert(cupid_gb_read_u8(&gb, 0xff77) == 0);
            cupid_gb_tick_apu(&gb, (uint16_t)(start_ticks[phase][divisor] * 2 - 1));
            assert(gb.apu.noise_timing.divider_counter == 0);
            cupid_gb_tick_apu(&gb, 1);
            assert(gb.apu.noise_timing.divider_counter == 1);
            assert(gb.apu.noise_timing.remaining == (divisor ? divisor * 4 : 2));
        }
    }

    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    cupid_gb_tick_apu(&gb, 12);
    cupid_gb_write_u8(&gb, 0xff22, 3);
    assert(gb.apu.noise_timing.remaining == 12); /* Reload edge sees the new divisor. */
    cupid_gb_tick_apu(&gb, 2);
    cupid_gb_write_u8(&gb, 0xff22, 0);
    assert(gb.apu.noise_timing.remaining == 11); /* A running countdown is retained. */
    cupid_gb_tick_apu(&gb, 22);
    assert(gb.apu.noise_timing.remaining == 2);
}

static void test_color_noise_retrigger_and_gates(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    cupid_gb_tick_apu(&gb, 6);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    assert(gb.apu.ch4_lfsr == 0x7faa);
    assert(cupid_gb_read_u8(&gb, 0xff77) == 0); /* Trigger holds zero despite the LFSR seed. */
    cupid_gb_write_u8(&gb, 0xff21, 0);
    uint16_t previous = gb.apu.noise_timing.divider_counter;
    cupid_gb_tick_apu(&gb, 32);
    assert(gb.apu.noise_timing.divider_counter != previous); /* Divisor zero retains background. */
    assert(!gb.apu.ch4_on && cupid_gb_read_u8(&gb, 0xff77) == 0);

    cupid_gb_write_u8(&gb, 0xff22, 1);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    cupid_gb_tick_apu(&gb, 32);
    cupid_gb_write_u8(&gb, 0xff21, 0);
    previous = gb.apu.noise_timing.divider_counter;
    cupid_gb_tick_apu(&gb, 32);
    assert(gb.apu.noise_timing.divider_counter == previous);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_tick_apu(&gb, 32);
    assert(gb.apu.noise_timing.divider_counter == previous); /* DAC enable is not a trigger. */
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.noise_timing.divider_counter == 0);
    assert(!gb.apu.noise_timing.background && !gb.apu.noise_timing.foreground);
}

static void test_color_noise_selector_edge(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff22, 0x38);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    cupid_gb_tick_apu(&gb, 12);
    assert(gb.apu.noise_timing.divider_counter == 1 && gb.apu.ch4_lfsr == 0x7fff);
    cupid_gb_write_u8(&gb, 0xff22, 8);
    assert(gb.apu.ch4_lfsr == 0x3fbf);
    cupid_gb_write_u8(&gb, 0xff22, 0x38);
    assert(gb.apu.ch4_lfsr == 0x3fbf);
    cupid_gb_write_u8(&gb, 0xff22, 8);
    assert(gb.apu.ch4_lfsr == 0x1f9f);
}

static void test_color_noise_selector_corruption(void) {
    /* Fixed digital observations exercise partial shifts, neighbor-cell gates,
     * held output, and the overwritten bit exposed when narrow mode is left. */
    const struct {
        uint16_t counter, before;
        uint8_t old_register, new_register, narrow_history, bit7_history, active;
        uint16_t after;
        uint8_t held, after_narrow, after_bit7;
    } cases[] = {
        {0x0100, 0x1234, 0x01, 0xa1, 0, 0, 1, 0x4918, 0, 0, 0},
        {0x0200, 0x47c7, 0x11, 0xb1, 1, 1, 1, 0x43e3, 1, 0, 1},
        {0x0100, 0x5555, 0x01, 0xb1, 0, 0, 1, 0x0000, 0, 0, 0},
        {0x0400, 0x47c7, 0x21, 0x81, 0, 0, 1, 0x63e3, 1, 0, 1},
        {0x0400, 0x7abc, 0x29, 0xb9, 1, 1, 1, 0x7d5c, 0, 1, 1},
        {0x0400, 0x1234, 0x21, 0x91, 0, 1, 1, 0x0010, 0, 0, 0},
        {0x0400, 0x0022, 0x29, 0xc9, 1, 0, 1, 0x0023, 1, 1, 0},
        {0x0400, 0x47c7, 0x21, 0xd1, 0, 1, 1, 0x47c7, 1, 0, 1},
        {0x2000, 0x0041, 0x59, 0x89, 1, 1, 1, 0x0000, 0, 1, 0},
        {0x2000, 0x4002, 0x51, 0x81, 0, 0, 1, 0x2001, 1, 0, 0},
        {0x0402, 0x1234, 0x11, 0xa1, 0, 1, 1, 0x5234, 0, 0, 1},
        {0x0801, 0x47c7, 0x09, 0xb1, 1, 1, 1, 0x47c7, 1, 0, 1},
        {0x0804, 0x7abc, 0x29, 0xb1, 1, 0, 1, 0x781c, 0, 0, 0},
        {0x0804, 0x7abc, 0x29, 0xb1, 1, 1, 1, 0x785c, 0, 0, 1},
        {0x0810, 0x5555, 0x41, 0xb9, 0, 0, 1, 0x45c5, 1, 1, 0},
        {0x0201, 0x47c7, 0x99, 0x01, 1, 1, 1, 0x63e3, 1, 0, 1},
        {0x0002, 0x1234, 0x01, 0x19, 0, 0, 1, 0x495a, 0, 1, 0},
        {0x0001, 0x47c7, 0x01, 0x11, 1, 0, 1, 0x47c7, 1, 1, 0},
        {0xffff, 0x7abc, 0x29, 0x21, 1, 1, 1, 0x7abc, 0, 1, 1},
        {0x0804, 0x1234, 0x29, 0xb1, 1, 1, 0, 0x4054, 0, 0, 1},
        {0x0100, 0x5555, 0x01, 0xb1, 0, 0, 0, 0x0000, 0, 0, 0},
        {0x0400, 0x0019, 0x21, 0xc1, 0, 0, 1, 0x0018, 0, 0, 0},
        {0x0400, 0x0002, 0x21, 0xc1, 0, 0, 1, 0x0003, 1, 0, 0},
        {0x2000, 0x4000, 0x51, 0x81, 0, 0, 1, 0x6000, 0, 0, 0},
    };

    for (unsigned speed = 0; speed < 2; ++speed) {
        for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            gb.double_speed = speed != 0;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, 0xff21, 0xf0);
            cupid_gb_write_u8(&gb, 0xff22, cases[i].old_register);
            gb.apu.ch4_lfsr = cases[i].before ^ 0x7fff;
            gb.apu.ch4_on = cases[i].active != 0;
            gb.apu.ch4_vol = 15;
            CupidGbNoiseTiming *timing = &gb.apu.noise_timing;
            timing->divider_counter = cases[i].counter;
            timing->remaining = 3;
            timing->output_high = (cases[i].before & 1) != 0;
            timing->last_shift_narrow = cases[i].narrow_history != 0;
            timing->pre_shift_bit7 = cases[i].bit7_history != 0;
            cupid_gb_write_u8(&gb, 0xff22, cases[i].new_register);
            assert(gb.apu.ch4_lfsr == (cases[i].after ^ 0x7fff));
            assert(timing->output_high == (cases[i].held != 0));
            assert(timing->last_shift_narrow == (cases[i].after_narrow != 0));
            assert(timing->pre_shift_bit7 == (cases[i].after_bit7 != 0));
            assert(timing->divider_counter == cases[i].counter && timing->remaining == 3);
            assert((cupid_gb_apu_read_pcm34(&gb) >> 4) ==
                   (cases[i].active && cases[i].held ? 15 : 0));
        }
        CupidGb gb = {0};
        gb.model = CUPID_GB_MODEL_CGB;
        gb.double_speed = speed != 0;
        cupid_gb_write_u8(&gb, 0xff26, 0x80);
        cupid_gb_write_u8(&gb, 0xff21, 0xf0);
        cupid_gb_write_u8(&gb, 0xff22, 1);
        cupid_gb_write_u8(&gb, 0xff23, 0x80);
        cupid_gb_tick_apu(&gb, 38);
        assert(gb.apu.noise_timing.divider_counter == 3);
        /* Both endpoint bits are low. The intermediate bit still clocks a shift. */
        cupid_gb_write_u8(&gb, 0xff22, 0x81);
        cupid_gb_write_u8(&gb, 0xff22, 0x21);
        assert(gb.apu.ch4_lfsr == 0x0fff);
    }
}

static void test_color_noise_dac_off_counter_carry(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff22, 9);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    tick_audio(&gb, 65540u * 2);
    assert(gb.apu.noise_timing.divider_counter == 0x3fff);
    assert(gb.apu.noise_timing.remaining == 2);
    assert(gb.apu.ch4_lfsr == (0x47c7 ^ 0x7fff));
    cupid_gb_write_u8(&gb, 0xff21, 0);
    assert(gb.apu.noise_timing.divider_counter == 0x4000);
    assert(!gb.apu.noise_timing.foreground && !gb.apu.noise_timing.background);
    tick_audio(&gb, 16);
    assert(gb.apu.noise_timing.divider_counter == 0x4000);
    cupid_gb_write_u8(&gb, 0xff22, 0xe9);
    assert(gb.apu.ch4_lfsr == (0x63e3 ^ 0x7fff));
    assert(cupid_gb_read_u8(&gb, 0xff77) == 0);
    cupid_gb_write_u8(&gb, 0xff22, 9);
    /* Repeated gate transitions can carry beyond 0x4000 without a normal
     * prescaler edge. The held counter wraps at its sixteen-bit storage width. */
    for (unsigned restart = 0; restart < 0xc000; ++restart) {
        cupid_gb_write_u8(&gb, 0xff21, 0xf0);
        cupid_gb_write_u8(&gb, 0xff23, 0x80);
        cupid_gb_tick_apu(&gb, 16);
        cupid_gb_write_u8(&gb, 0xff21, 0);
        if (restart < 3) {
            assert(gb.apu.noise_timing.divider_counter == 0x4001 + restart);
        }
    }
    assert(gb.apu.noise_timing.divider_counter == 0);
}

static void test_audio_chunk_splitting(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    const uint32_t rates[] = {8000, 44100, 192000};
    const uint16_t chunks[] = {1, 2, 4, 31, 257, UINT16_MAX};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
            for (unsigned scenario = 0; scenario < 6; ++scenario) {
                CupidGb batch = {0};
                batch.model = models[model];
                cupid_gb_sgb_startup_reset(&batch, false);
                assert(cupid_gb_apu_set_sample_rate(&batch, rates[rate]));
                cupid_gb_write_u8(&batch, 0xff26, 0x80);
                cupid_gb_write_u8(&batch, 0xff10, scenario == 2 ? 0x17 : 0);
                cupid_gb_write_u8(&batch, 0xff11, 0x80);
                cupid_gb_write_u8(&batch, 0xff12, 0x89);
                cupid_gb_write_u8(&batch, 0xff13, 0xff);
                cupid_gb_write_u8(&batch, 0xff14, 0x87);
                cupid_gb_write_u8(&batch, 0xff21, 0xb3);
                cupid_gb_write_u8(&batch, 0xff22, 9);
                cupid_gb_write_u8(&batch, 0xff23, 0x80);
                cupid_gb_write_u8(&batch, 0xff24, 0x77);
                cupid_gb_write_u8(&batch, 0xff25, 0xff);
                if (scenario == 0) {
                    cupid_gb_write_u8(&batch, 0xff26, 0);
                } else if (scenario == 3) {
                    batch.apu.output.buf_write = CUPID_GB_APU_BUF_FRAMES;
                } else if (scenario == 4) {
                    batch.cpu.stopped = true;
                    batch.apu.ch3_wave_access_ticks = 3;
                } else if (scenario == 5) {
                    batch.apu.ch1_timer = 500;
                    batch.apu.ch2_timer = 500;
                    batch.apu.ch3_wave_access_ticks = 3;
                    batch.apu.ch4_timer = 2;
                    batch.apu.ch4_shift = 14;
                }
                for (unsigned chunk = 0; chunk < sizeof(chunks) / sizeof(chunks[0]); ++chunk) {
                    CupidGb split = batch;
                    cupid_gb_tick_apu(&batch, chunks[chunk]);
                    for (unsigned dot = 0; dot < chunks[chunk]; ++dot) {
                        cupid_gb_tick_apu(&split, 1);
                    }
                    assert(memcmp(&batch.apu, &split.apu, sizeof(batch.apu)) == 0);
                }
            }
        }
    }
}

static void test_color_pulse_phantom_trigger(void) {
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            gb.cgb_mode = true;
            gb.double_speed = speed != 0;
            unsigned base = channel ? 0xff16 : 0xff11;
            uint16_t *timer = channel ? &gb.apu.ch2_timer : &gb.apu.ch1_timer;
            uint8_t *index = channel ? &gb.apu.ch2_duty_pos : &gb.apu.ch1_duty_pos;
            CupidGbPulseTiming *timing = &gb.apu.pulse_timing[channel];
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, base, 0);
            cupid_gb_write_u8(&gb, base + 1, 0xf0);
            cupid_gb_write_u8(&gb, base + 2, 0);
            cupid_gb_write_u8(&gb, base + 3, 0x83);
            assert(*index == 0); /* Cold power has no pending frequency-counter edge. */
            assert(timing->output_blocked);
            *timer = 402;
            *index = 6;
            timing->start_delay = 0;
            timing->edge_age = 4;
            cupid_gb_write_u8(&gb, base + 3, 0x83);
            assert(*index == 7);
            assert(!timing->output_blocked);
            assert(((cupid_gb_apu_read_pcm12(&gb) >> (channel * 4)) & 15) == 15);
            /* Bit 2 gates the phantom edge; a real reload suppresses a second edge. */
            *timer = 402;
            *index = 6;
            timing->start_delay = 0;
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            assert(*index == 6);
            timing->edge_age = 0;
            cupid_gb_write_u8(&gb, base + 3, 0x83);
            assert(*index == 6);
            assert(timing->edge_age == 0);
            /* Inactive channels retain the last reload until their clock restarts. */
            cupid_gb_write_u8(&gb, base + 1, 0);
            cupid_gb_tick_apu(&gb, 8);
            assert(timing->edge_age == 0);
            cupid_gb_write_u8(&gb, base + 2, 0xfe);
            assert(*timer == 4104);
            cupid_gb_write_u8(&gb, base + 3, 3);
            assert(*timer == 4104);
            cupid_gb_write_u8(&gb, base + 1, 0xf0);
            cupid_gb_write_u8(&gb, base + 3, 0x83);
            assert(*index == 6);
            assert(timing->output_blocked);
            /* A suppressed retrigger at the maximum frequency has four extra base clocks. */
            cupid_gb_write_u8(&gb, base + 2, 0xff);
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            assert(timing->start_delay == 10);
            assert(*timer == 12);
            /* A retrigger retains reload writes but clears the prior real duty edge. */
            cupid_gb_write_u8(&gb, 0xff26, 0);
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, base + 1, 0xf0);
            cupid_gb_write_u8(&gb, base + 2, 0xfc);
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            cupid_gb_tick_apu(&gb, 24);
            assert(*index == 1 && timing->edge_age == 0);
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            cupid_gb_write_u8(&gb, base + 3, 3);
            assert(*index == 1);
            assert(*timer == 4112);
        }
    }
}

static void test_pending_delivery_and_host_rate(void) {
    CupidGb gb;
    prepare_dc(&gb);
    cupid_gb_tick_apu_divider(&gb, 4);
    const uint32_t credit = gb.apu.output.sample_acc;
    const uint64_t elapsed = gb.elapsed_cycles;
    cupid_gb_apu_run(&gb, false);
    assert(gb.apu.pending_cycles == 4 && gb.apu.output.buf_write == 0);
    (void)cupid_gb_read_u8(&gb, 0xff26);
    assert(gb.apu.pending_cycles == 4); /* Debugger inspection is passive. */
    (void)cupid_gb_read_bus_u8(&gb, 0xff26);
    assert(gb.apu.pending_cycles == 0 && gb.apu.output.sample_acc == credit);
    assert(gb.elapsed_cycles == elapsed);
    cupid_gb_tick_apu_divider(&gb, 4);
    const uint32_t before_change = gb.apu.output.sample_acc;
    assert(cupid_gb_apu_set_sample_rate(&gb, 192000));
    assert(gb.apu.pending_cycles == 0);
    assert(gb.apu.output.sample_acc == before_change && gb.elapsed_cycles == elapsed);
    assert(cupid_gb_apu_set_channel_muted(&gb, 0, true));
    cupid_gb_write_u8(&gb, 0xff24, gb.io_registers[0x24]);
    assert(gb.apu.output.channels[0].input[0] == 0);
    assert(gb.apu.ch1_dac && gb.io_registers[0x12] == 0xf0);
}

static void test_interference_instance_history(void) {
    CupidGb first, second;
    prepare_dc(&first);
    assert(cupid_gb_apu_set_interference_volume(&first, 0.25));
    second = first;
    tick_audio(&first, 4096);
    tick_audio(&second, 4096);
    assert(first.apu.output.noise_state != 0);
    assert(memcmp(&first.apu.output, &second.apu.output, sizeof(first.apu.output)) == 0);
    const uint32_t sequence = first.apu.output.noise_state;
    const double history = first.apu.output.filter.interference;
    assert(cupid_gb_apu_set_interference_volume(&first, 0));
    tick_audio(&first, 4096);
    assert(first.apu.output.noise_state == sequence);
    assert(first.apu.output.filter.interference == history);
}

static void test_frozen_audio_backlog(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB,
                                   CUPID_GB_MODEL_SGB2};
    const uint32_t rates[] = {8000, 44100, 48000, 110250, 176400, 190000, 192000};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
            CupidGb gb = {0};
            gb.model = models[model];
            /* This fixture measures handheld backlog after host startup. */
            cupid_gb_sgb_startup_reset(&gb, false);
            assert(cupid_gb_apu_set_sample_rate(&gb, rates[rate]));
            cupid_gb_tick_apu_divider(&gb, 2048);
            const uint32_t clock = cupid_gb_base_clock_hz(&gb);
            const uint32_t credit = rates[rate] * 2048u;
            assert(gb.apu.output.sample_acc == credit && gb.apu.pending_cycles == 2048);
            cupid_gb_apu_run(&gb, true);
            assert(gb.apu.pending_cycles == 0);
            assert(gb.apu.output.buf_write == credit / clock);
            assert(gb.apu.output.sample_acc == credit % clock);
            assert(gb.apu.output.sample_fraction < (2u << 28));
        }
    }
}

static void test_color_startup_wave_guest(void) {
    static const CupidGbModel models[] = {
        CUPID_GB_MODEL_CGB_0,
        CUPID_GB_MODEL_CGB_A,
        CUPID_GB_MODEL_CGB_E,
    };
    /* Read the wave cells, enable CH3, then sample FF77 256 times. Later color
     * revisions supply the 00/FF startup pattern read by this program. */
    static const uint8_t play[] = {
        0x3e, 0x00, 0xe0, 0x12, /* Disconnect CH1. */
        0x3e, 0x80, 0xe0, 0x1a, 0x3e, 0x20, 0xe0, 0x1c, 0x3e, 0xfc, 0xe0, 0x1d, 0x3e, 0x87, 0xe0,
        0x1e, 0x21, 0x00, 0xc1, 0x06, 0x00, 0xf0, 0x77, 0x22, 0x05, 0x20, 0xfa, 0xc3, 0x00, 0x02,
    };
    uint8_t rom[32768] = {0};
    rom[0x100] = 0xc3;
    rom[0x101] = 0x50;
    rom[0x102] = 1;
    unsigned end = 0x150;
    rom[end++] = 0x21;
    rom[end++] = 0;
    rom[end++] = 0xc0;
    for (unsigned i = 0; i < 16; ++i) {
        rom[end++] = 0xf0;
        rom[end++] = (uint8_t)(0x30u + i);
        rom[end++] = 0x22;
    }
    memcpy(rom + end, play, sizeof(play));
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned native = 0; native < 2; ++native) {
            rom[0x143] = native ? 0x80 : 0;
            rom[0x14d] = 0;
            for (unsigned i = 0x134; i <= 0x14c; ++i)
                rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
            CupidGb *gb = calloc(1, sizeof(*gb));
            assert(gb);
            cupid_gb_set_model(gb, models[model]);
            cupid_gb_set_power_on_seed(gb, 0);
            assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
            for (unsigned reset = 0; reset < 3; ++reset) {
                if (reset)
                    assert(cupid_gb_reset(gb,
                                          reset == 1 ? CUPID_GB_RESET_QUICK : CUPID_GB_RESET_HARD));
                unsigned steps = 0;
                while (gb->cpu.pc != 0x200 && steps++ < 2000)
                    assert(cupid_gb_step(gb));
                assert(gb->cpu.pc == 0x200 && steps < 2000);
                for (unsigned i = 0; i < 16; ++i) {
                    const uint8_t expected = model && (i & 1u) ? 0xff : 0;
                    assert(cupid_gb_read_u8(gb, (uint16_t)(0xc000u + i)) == expected);
                }
                unsigned nonzero = 0;
                for (unsigned i = 0; i < 256; ++i) {
                    const uint8_t pcm = cupid_gb_read_u8(gb, (uint16_t)(0xc100u + i));
                    assert(pcm == 0 || (model && pcm == 0x0f));
                    nonzero += pcm != 0;
                }
                assert(nonzero == (model ? 128u : 0u));
            }
            cupid_gb_cleanup(gb);
            free(gb);
        }
    }
}

int main(void) {
    test_color_startup_wave_guest();
    test_pending_delivery_and_host_rate();
    test_interference_instance_history();
    test_frozen_audio_backlog();
    test_pulse_trigger_length_enable();
    test_pulse_dac_hold();
    test_monochrome_pulse_clock_lifetime();
    test_monochrome_pulse_duty_hold();
    test_monochrome_pulse_high_bank_edge();
    test_monochrome_envelope_retrigger_between_divider_edges();
    test_color_pulse_phantom_trigger();
    test_color_pulse_clock_and_held_duty();
    test_color_envelope_edges();
    test_color_frequency_reload_window();
    test_color_sweep_live_frequency_and_latency();
    test_color_sweep_overflow_mixer_edge();
    test_color_noise_prescaler_and_reload();
    test_color_noise_retrigger_and_gates();
    test_color_noise_selector_edge();
    test_color_noise_selector_corruption();
    test_color_noise_dac_off_counter_carry();
    test_audio_chunk_splitting();
    test_divider_clock(false);
    test_divider_clock(true);
    test_div_reset_edge();
    test_power_on_high_div();
    test_power_counter_reset(false);
    test_power_counter_reset(true);
    test_power_cycle_resets_sound_but_preserves_stream(false);
    test_power_cycle_resets_sound_but_preserves_stream(true);
    test_apu_ticks_do_not_clock_length();
    test_cgb_compatibility_uses_color_hardware();
    test_sweep_zero_period_and_shadow();
    test_pulse_pitch();
    test_pulse_frequency_reload_window();
    test_noise_startup_clock_matrix();
    test_noise_clock();
    test_monochrome_noise_latches();
    test_partial_drain();
    test_stop_oscillator(false);
    test_stop_oscillator(true);
    test_dac_transfer_and_pan();
    test_highpass_dc_and_disconnect();
    test_duty_phase();
    test_sample_rates_and_batching();
    test_physical_model_clocks();
    test_fast_pulse_integration();
    test_pcm_taps();
    test_length_control_matrix();
    test_power_preserves_output_pipeline(false);
    test_power_preserves_output_pipeline(true);
    puts("APU timing and audio regression checks passed.");
    return 0;
}

/**
 * @file    test_audio_filter.c
 * @brief   Regression coverage for filter arithmetic, connection state, and noise.
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
#include "../src/gb/audio_filter.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void expect_frame(CupidGbAudioFrame frame, int left, int right) {
    assert(frame.left == left && frame.right == right);
}

static void expect_close(double actual, double expected) {
    assert(fabs(actual - expected) <= 2e-10);
}

static void test_coefficient_preserves_history(void) {
    CupidGbAudioFilterState state = {{123.25, -77.75}, 91.125, 0.5};
    cupid_gb_audio_filter_set_rate(&state, 4194304, 44100);
    assert(fabs(state.retention - 0.9960133089108504) <= 1e-15);
    assert(state.capacitor[0] == 123.25 && state.capacitor[1] == -77.75);
    assert(state.interference == 91.125);
    cupid_gb_audio_filter_set_rate(&state, 4194304, 48000);
    assert(fabs(state.retention - 0.9963366334875375) <= 1e-15);
    assert(state.capacitor[0] == 123.25 && state.capacitor[1] == -77.75);
    assert(state.interference == 91.125);
    const CupidGbAudioFilterState before = state;
    cupid_gb_audio_filter_set_rate(&state, 4194304, 0);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
}

static void test_off_and_connection(void) {
    const CupidGbAudioPolicy off = {.filter_disabled = true};
    CupidGbAudioConditions conditions = {0};
    CupidGbAudioFilterState state = {{123.25, -77.75}, 42.5, 0.5};
    const CupidGbAudioFilterState before = state;
    expect_frame(cupid_gb_audio_filter_frame(&state, &off, &conditions, 4080, 1020, 0, 0x71, 0x11,
                                             UINT32_MAX),
                 0, 0);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    expect_frame(
        cupid_gb_audio_filter_frame(&state, &off, &conditions, 4080, 1020, 8, 0x71, 0x11, 0), 4080,
        1020);
    assert(state.capacitor[0] == 0 && state.capacitor[1] == 0);
    assert(state.interference == 42.5 && state.retention == 0.5);

    state = before;
    conditions.color_hardware = true;
    expect_frame(
        cupid_gb_audio_filter_frame(&state, &off, &conditions, 4080, 1020, 0, 0x71, 0x11, 0), 4080,
        1020);
    assert(state.capacitor[0] == 0 && state.capacitor[1] == 0);
    assert(state.interference == 42.5 && state.retention == 0.5);
}

static void test_constant_accurate(void) {
    static const struct {
        uint32_t rate;
        int sign;
        int16_t pcm[4][2];
        double capacitor[2];
    } cases[] = {
        {44100,
         1,
         {{3957, 1097}, {3941, 1093}, {3926, 1089}, {3910, 1084}},
         {185.97103719583174, -60.34884537013363}},
        {44100,
         -1,
         {{-4203, -943}, {-4186, -939}, {-4169, -935}, {-4153, -932}},
         {56.62152976726338, -92.68622222727561}},
        {48000,
         1,
         {{3957, 1097}, {3943, 1093}, {3928, 1089}, {3914, 1085}},
         {180.91227564287965, -61.75233446971083}},
        {48000,
         -1,
         {{-4203, -943}, {-4187, -939}, {-4172, -936}, {-4157, -932}},
         {61.99544510117357, -91.48154210513769}},
    };

    const CupidGbAudioPolicy policy = {0};
    for (unsigned color = 0; color < 2; ++color) {
        const CupidGbAudioConditions conditions = {.color_hardware = color != 0};
        for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
            CupidGbAudioFilterState state = {{123.25, -77.75}, 0, 0};
            cupid_gb_audio_filter_set_rate(&state, 4194304, cases[test].rate);
            for (unsigned frame = 0; frame < 4; ++frame) {
                expect_frame(cupid_gb_audio_filter_frame(&state, &policy, &conditions,
                                                         cases[test].sign * 4080,
                                                         cases[test].sign * 1020, 1, 0x71, 0x11, 0),
                             cases[test].pcm[frame][0], cases[test].pcm[frame][1]);
            }
            expect_close(state.capacitor[0], cases[test].capacitor[0]);
            expect_close(state.capacitor[1], cases[test].capacitor[1]);
        }
    }
}

static void test_narrowing_order(void) {
    const CupidGbAudioConditions conditions = {.color_hardware = true};
    const CupidGbAudioPolicy policy = {0};
    CupidGbAudioFilterState state = {{0, 0}, 0, 1};
    expect_frame(
        cupid_gb_audio_filter_frame(&state, &policy, &conditions, 35936, -35936, 15, 0, 0, 0),
        -29600, 29600);
    expect_frame(cupid_gb_audio_filter_frame(&state, &policy, &conditions, INT32_MAX, INT32_MIN, 15,
                                             0, 0, 0),
                 -1, 0);
    state.capacitor[0] = -32768;
    state.capacitor[1] = 32767;
    expect_frame(
        cupid_gb_audio_filter_frame(&state, &policy, &conditions, 32767, -32768, 15, 0, 0, 0), -1,
        1);
    assert(state.capacitor[0] == -32768 && state.capacitor[1] == 32767);
    state.capacitor[0] = 123.25;
    state.capacitor[1] = -77.75;
    expect_frame(
        cupid_gb_audio_filter_frame(&state, &policy, &conditions, 4080, -1020, 1, 0x71, 0x11, 0),
        3957, -943);
}

static void test_preserve_waveform(void) {
    static const struct {
        uint8_t dacs, route, gain;
        int left, right;
    } cases[] = {
        {1, 0x08, 0x17, 4080, 0}, {1, 0x80, 0x17, 0, 1020},      {2, 0x04, 0x17, 4080, 0},
        {2, 0x40, 0x17, 0, 1020}, {4, 0x02, 0x17, 4080, 0},      {4, 0x20, 0x17, 0, 1020},
        {8, 0x01, 0x17, 4080, 0}, {8, 0x10, 0x17, 0, 1020},      {1, 0x11, 0x17, 0, 0},
        {8, 0x88, 0x17, 0, 0},    {15, 0xff, 0x71, 4080, 16320}, {15, 0xff, 0xff, 16320, 16320},
    };

    for (unsigned color = 0; color < 2; ++color) {
        const CupidGbAudioConditions conditions = {.color_hardware = color != 0};
        for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
            /* A mute flag does not change the separate enabled-DAC DC path. */
            const CupidGbAudioPolicy policy = {.muted_channels = 15, .preserve_waveform = true};
            CupidGbAudioFilterState state = {{0, 0}, 0, 0.5};
            expect_frame(cupid_gb_audio_filter_frame(&state, &policy, &conditions, 0, 0,
                                                     cases[test].dacs, cases[test].gain,
                                                     cases[test].route, 0),
                         0, 0);
            assert(state.capacitor[0] == cases[test].left / 2.0);
            assert(state.capacitor[1] == cases[test].right / 2.0);
            expect_frame(cupid_gb_audio_filter_frame(&state, &policy, &conditions, 0, 0,
                                                     cases[test].dacs, cases[test].gain,
                                                     cases[test].route, 0),
                         -cases[test].left / 2, -cases[test].right / 2);
            assert(state.capacitor[0] == cases[test].left * 0.75);
            assert(state.capacitor[1] == cases[test].right * 0.75);
        }
    }
    const CupidGbAudioConditions mono = {0};
    const CupidGbAudioPolicy policy = {.preserve_waveform = true};
    CupidGbAudioFilterState state = {{123.25, -77.75}, 0, 0.5};
    const CupidGbAudioFilterState before = state;
    expect_frame(cupid_gb_audio_filter_frame(&state, &policy, &mono, 4080, 1020, 0, 0x77, 0xff, 0),
                 0, 0);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
}

static void test_interference(void) {
    static const struct {
        CupidGbAudioConditions conditions;
        uint32_t random_value;
        int full, quiet;
        double history;
    } cases[] = {
        {{0}, 0, 0, 0, 0.125},
        {{.halted = true}, 0, -204, -10, -101.875},
        {{.lcd_enabled = true, .lcd_mode = 0}, 0, 145, 7, 72.125},
        {{.lcd_enabled = true, .lcd_mode = 1}, 0, 0, 0, 0.125},
        {{.lcd_enabled = true, .lcd_mode = 2}, 0, 145, 7, 72.125},
        {{.lcd_enabled = true, .lcd_mode = 3}, 0, 218, 10, 108.625},
        {{.apu_enabled = true}, 0, 102, 5, 51.125},
        {{.halted = true, .lcd_enabled = true, .apu_enabled = true, .lcd_mode = 3},
         0,
         116,
         5,
         58.125},
        {{.infrared_output = true}, 0, 0, 0, 0.125},
        {{.color_hardware = true, .halted = true}, 0, -816, -40, -407.875},
        {{.color_hardware = true, .lcd_enabled = true, .lcd_mode = 0}, 0, 582, 29, 290.625},
        {{.color_hardware = true, .lcd_enabled = true, .lcd_mode = 1}, 0, 0, 0, 0.125},
        {{.color_hardware = true, .lcd_enabled = true, .lcd_mode = 2}, 0, 582, 29, 290.625},
        {{.color_hardware = true, .lcd_enabled = true, .lcd_mode = 3}, 0, 873, 43, 436.125},
        {{.color_hardware = true, .apu_enabled = true}, 0, 408, 20, 203.625},
        {{.color_hardware = true, .infrared_output = true}, 0, 408, 20, 203.625},
        {{.color_hardware = true,
          .lcd_enabled = true,
          .apu_enabled = true,
          .infrared_output = true,
          .lcd_mode = 3},
         339,
         2028,
         101,
         1013.625},
        {{0}, 339, 339, 16, 169.125},
        {{0}, 679, 339, 16, 169.125},
        {{0}, 340, 0, 0, 0.125},
        {{0}, UINT32_MAX, 255, 12, 127.125},
    };

    for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
        for (unsigned level = 0; level < 3; ++level) {
            const CupidGbAudioPolicy policy = {.interference_volume = level == 2 ? 1 : level * 0.05,
                                               .filter_disabled = true};
            CupidGbAudioFilterState state = {{0, 0}, 123.25, 0.5};
            const int expected = level == 2 ? cases[test].full : level ? cases[test].quiet : 0;
            expect_frame(cupid_gb_audio_filter_frame(&state, &policy, &cases[test].conditions, 0, 0,
                                                     0, 0, 0, cases[test].random_value),
                         expected, expected);
            assert(state.interference == (level ? cases[test].history : 123.25));
        }
    }
}

static void test_interference_clipping(void) {
    const CupidGbAudioPolicy policy = {.interference_volume = 1, .filter_disabled = true};
    CupidGbAudioConditions conditions = {.color_hardware = true,
                                         .lcd_enabled = true,
                                         .apu_enabled = true,
                                         .infrared_output = true,
                                         .lcd_mode = 3};
    CupidGbAudioFilterState state = {{0, 0}, 0, 0.5};
    expect_frame(
        cupid_gb_audio_filter_frame(&state, &policy, &conditions, 32760, -32760, 0, 0, 0, 339),
        32767, -30732);
    conditions = (CupidGbAudioConditions){.color_hardware = true, .halted = true};
    expect_frame(
        cupid_gb_audio_filter_frame(&state, &policy, &conditions, 32760, -32760, 0, 0, 0, 0), 31944,
        -32768);
    conditions = (CupidGbAudioConditions){.color_hardware = true,
                                          .lcd_enabled = true,
                                          .apu_enabled = true,
                                          .infrared_output = true,
                                          .lcd_mode = 3};
    expect_frame(
        cupid_gb_audio_filter_frame(&state, &policy, &conditions, 36000, -36000, 0, 0, 0, 339),
        -27508, 31564);
}

static void test_safe_import_boundaries(void) {
    static const double histories[] = {-65536, -32768, -0.75, 0, 32767.75, 65536};
    static const double retentions[] = {0, 0.5, 0.9963366334875375, 1};
    const CupidGbAudioConditions conditions = {.color_hardware = true,
                                               .lcd_enabled = true,
                                               .apu_enabled = true,
                                               .infrared_output = true,
                                               .lcd_mode = 3};
    for (unsigned history = 0; history < sizeof(histories) / sizeof(histories[0]); ++history) {
        for (unsigned rate = 0; rate < sizeof(retentions) / sizeof(retentions[0]); ++rate) {
            for (unsigned mode = 0; mode < 3; ++mode) {
                const CupidGbAudioPolicy policy = {.interference_volume = 1,
                                                   .filter_disabled = mode == 0,
                                                   .preserve_waveform = mode == 2};
                CupidGbAudioFilterState state = {{histories[history], -histories[history]},
                                                 histories[history],
                                                 retentions[rate]};
                for (unsigned frame = 0; frame < 4; ++frame) {
                    (void)cupid_gb_audio_filter_frame(&state, &policy, &conditions, INT32_MAX,
                                                      INT32_MIN, 15, 0x77, 0xff, UINT32_MAX);
                    assert(isfinite(state.capacitor[0]) && isfinite(state.capacitor[1]) &&
                           isfinite(state.interference));
                    assert(fabs(state.capacitor[0]) <= CUPID_GB_AUDIO_FILTER_HISTORY_LIMIT);
                    assert(fabs(state.capacitor[1]) <= CUPID_GB_AUDIO_FILTER_HISTORY_LIMIT);
                    assert(fabs(state.interference) <= CUPID_GB_AUDIO_FILTER_HISTORY_LIMIT);
                }
            }
        }
    }
}

int main(void) {
    test_coefficient_preserves_history();
    test_off_and_connection();
    test_constant_accurate();
    test_narrowing_order();
    test_preserve_waveform();
    test_interference();
    test_interference_clipping();
    test_safe_import_boundaries();
    puts("CupidGB audio filter tests passed.");
    return 0;
}

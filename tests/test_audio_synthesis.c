/**
 * @file    test_audio_synthesis.c
 * @brief   Regression coverage for reconstruction coefficients, rounding, and history.
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
#include "../src/gb/audio_synthesis.h"
#include "cupid/common/sha256.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_kernel(void) {
    /* A phase-major, little-endian fingerprint of the independently generated
     * integer response bank. This also catches accidental edits to generated data. */
    static const uint8_t expected[32] = {0xa2, 0xbc, 0xda, 0x0f, 0x21, 0x9b, 0xfb, 0x15,
                                         0x86, 0x31, 0x84, 0xca, 0x31, 0x6b, 0x54, 0x53,
                                         0x76, 0xa9, 0x41, 0x20, 0xf2, 0xd4, 0xe6, 0xe3,
                                         0x8f, 0xe8, 0xa7, 0x72, 0x4e, 0x1e, 0x7a, 0xaa};
    CupidSha256 hash;
    cupid_sha256_init(&hash);
    for (unsigned phase = 0; phase < CUPID_GB_AUDIO_PHASES; ++phase) {
        const int32_t *weights = cupid_gb_audio_kernel(phase);
        int64_t sum = 0;
        for (unsigned tap = 0; tap < CUPID_GB_AUDIO_TAPS; ++tap) {
            const uint32_t value = (uint32_t)weights[tap];
            const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                                      (uint8_t)(value >> 24)};
            cupid_sha256_update(&hash, bytes, sizeof(bytes));
            sum += weights[tap];
        }
        assert(sum == 65536);
    }
    uint8_t digest[32];
    cupid_sha256_final(&hash, digest);
    assert(memcmp(digest, expected, sizeof(expected)) == 0);
}

static void test_rounding(void) {
    static const struct {
        int32_t voltage;
        unsigned gain;
        int16_t expected;
    } cases[] = {{65535, 1, 0},
                 {-1, 1, -1},
                 {-65535, 1, -1},
                 {-65536, 1, -1},
                 {-65537, 1, -2},
                 {65535, 34, 33},
                 {-65535, 34, -34},
                 {65536, 34, 34},
                 {INT32_MAX, 1, INT16_MAX},
                 {INT32_MIN, 1, INT16_MIN},
                 {INT32_MAX, 34, -1},
                 {INT32_MIN, 34, 0},
                 {INT32_MIN, 0, 0}};

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CupidGbAudioChannel channel = {0};
        channel.level[0] = channel.level[1] = cases[i].voltage;
        CupidGbAudioFrame frame = cupid_gb_audio_synth_read(&channel, cases[i].gain);
        assert(frame.left == cases[i].expected && frame.right == cases[i].expected);
    }
    assert(cupid_gb_audio_narrow16(65535) == -1);
    assert(cupid_gb_audio_narrow16(65536) == 0);
    assert(cupid_gb_audio_narrow16(-65537) == -1);
    assert(cupid_gb_audio_narrow16(INT64_MIN) == 0);
    assert(cupid_gb_audio_narrow16(INT64_MAX) == -1);
}

static void test_step_conservation(void) {
    static const unsigned cursors[] = {0, 31, 97, 127};
    static const unsigned phases[] = {0, 255, 256, 511, 768, 1023};
    for (unsigned cursor = 0; cursor < sizeof(cursors) / sizeof(cursors[0]); ++cursor) {
        for (unsigned phase = 0; phase < sizeof(phases) / sizeof(phases[0]); ++phase) {
            CupidGbAudioChannel channel = {0};
            channel.cursor = (uint8_t)cursors[cursor];
            cupid_gb_audio_synth_update(&channel, (CupidGbAudioFrame){120, -120}, phases[phase]);
            CupidGbAudioFrame frame = {0};
            for (unsigned i = 0; i < CUPID_GB_AUDIO_RING_FRAMES; ++i) {
                frame = cupid_gb_audio_synth_read(&channel, 34);
                if (i < phases[phase] / CUPID_GB_AUDIO_PHASES)
                    assert(frame.left == 0 && frame.right == 0);
            }
            assert(channel.cursor == cursors[cursor]);
            assert(channel.level[0] == 120 * 65536 && channel.level[1] == -120 * 65536);
            assert(frame.left == 4080 && frame.right == -4080);
            cupid_gb_audio_synth_update(&channel, (CupidGbAudioFrame){0, 0}, phases[phase]);
            for (unsigned i = 0; i < CUPID_GB_AUDIO_RING_FRAMES; ++i)
                frame = cupid_gb_audio_synth_read(&channel, 34);
            assert(channel.level[0] == 0 && channel.level[1] == 0);
            assert(frame.left == 0 && frame.right == 0);
            for (unsigned side = 0; side < 2; ++side)
                for (unsigned i = 0; i < CUPID_GB_AUDIO_RING_FRAMES; ++i)
                    assert(channel.pending[side][i] == 0);
        }
    }
}

static void test_quiet_anchor(void) {
    CupidGbAudioChannel channel = {0};
    channel.level[0] = 10 * 65536;
    assert(cupid_gb_audio_synth_read(&channel, 1).left == 10);
    assert(channel.anchor.left == 10 && channel.quiet_frames == 0);
    channel.level[0] = 14 * 65536;
    for (unsigned i = 0; i < 4000; ++i)
        assert(cupid_gb_audio_synth_read(&channel, 1).left == 14);
    assert(channel.quiet_frames == 4000);
    assert(cupid_gb_audio_synth_read(&channel, 1).left == 10);
    channel.level[1] = -5 * 65536;
    CupidGbAudioFrame frame = cupid_gb_audio_synth_read(&channel, 1);
    assert(frame.left == 14 && frame.right == -5);
    assert(channel.anchor.left == 14 && channel.anchor.right == -5);
    assert(channel.quiet_frames == 0);
}

static void test_imported_accumulator_wrap(void) {
    CupidGbAudioChannel channel = {0};
    channel.cursor = 127;
    channel.level[0] = INT32_MAX;
    channel.pending[0][127] = 1;
    CupidGbAudioFrame frame = cupid_gb_audio_synth_read(&channel, 1);
    assert(channel.level[0] == INT32_MIN && frame.left == INT16_MIN);
    assert(channel.pending[0][127] == 0 && channel.cursor == 0);
}

int main(void) {
    test_kernel();
    test_rounding();
    test_step_conservation();
    test_quiet_anchor();
    test_imported_accumulator_wrap();
    puts("Audio synthesis checks passed");
    return 0;
}

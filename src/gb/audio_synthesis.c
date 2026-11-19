/**
 * @file    audio_synthesis.c
 * @brief   Band-limited channel reconstruction with portable fixed-point arithmetic.
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
#include "audio_synthesis.h"

#include <assert.h>

#include "audio_kernel.inc"

static int32_t signed_word(uint32_t bits) {
    return bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - INT64_C(4294967296));
}

static int32_t add_word(int32_t left, int32_t right) {
    return signed_word((uint32_t)left + (uint32_t)right);
}

const int32_t *cupid_gb_audio_kernel(unsigned fraction) {
    assert(fraction < CUPID_GB_AUDIO_PHASES);
    return audio_kernel[fraction];
}

static void schedule_side(int32_t *pending, const int32_t *weights, unsigned start,
                          int difference) {
    /* Each contiguous portion lets the compiler vectorize without a wrap test
     * for every tap. Normal ordered histories fit int32; modular addition also
     * defines behavior for every representable imported accumulator. */
    unsigned first = CUPID_GB_AUDIO_RING_FRAMES - start;
    if (first > CUPID_GB_AUDIO_TAPS)
        first = CUPID_GB_AUDIO_TAPS;
    for (unsigned tap = 0; tap < first; ++tap)
        pending[start + tap] = add_word(pending[start + tap], difference * weights[tap]);
    for (unsigned tap = first; tap < CUPID_GB_AUDIO_TAPS; ++tap)
        pending[tap - first] = add_word(pending[tap - first], difference * weights[tap]);
}

void cupid_gb_audio_synth_update(CupidGbAudioChannel *channel, CupidGbAudioFrame input,
                                 unsigned phase) {
    assert(channel && phase < 4u * CUPID_GB_AUDIO_PHASES);
    assert(input.left >= -120 && input.left <= 120 && input.right >= -120 && input.right <= 120);
    const unsigned start =
        (channel->cursor + phase / CUPID_GB_AUDIO_PHASES) & (CUPID_GB_AUDIO_RING_FRAMES - 1u);
    const int32_t *weights = cupid_gb_audio_kernel(phase & (CUPID_GB_AUDIO_PHASES - 1u));
    const int16_t incoming[2] = {input.left, input.right};
    for (unsigned side = 0; side < 2; ++side) {
        const int difference = (int)incoming[side] - channel->input[side];
        channel->input[side] = incoming[side];
        if (difference)
            schedule_side(channel->pending[side], weights, start, difference);
    }
}

static int16_t apply_gain(int32_t level, unsigned gain) {
    /* The unsigned product and high half implement floor rounding for negative
     * fractional voltages as well as the final signed 16-bit wrap. */
    const uint32_t product = (uint32_t)level * gain;
    return cupid_gb_audio_narrow16(product >> 16);
}

CupidGbAudioFrame cupid_gb_audio_synth_read(CupidGbAudioChannel *channel, unsigned gain) {
    assert(channel && channel->cursor < CUPID_GB_AUDIO_RING_FRAMES && gain <= 34);
    for (unsigned side = 0; side < 2; ++side) {
        int32_t *pending = &channel->pending[side][channel->cursor];
        channel->level[side] = add_word(channel->level[side], *pending);
        *pending = 0;
    }
    channel->cursor = (uint8_t)((channel->cursor + 1u) & (CUPID_GB_AUDIO_RING_FRAMES - 1u));
    CupidGbAudioFrame frame = {apply_gain(channel->level[0], gain),
                               apply_gain(channel->level[1], gain)};
    const int left_change = (int)frame.left - channel->anchor.left;
    const int right_change = (int)frame.right - channel->anchor.right;
    if (left_change < -4 || left_change > 4 || right_change < -4 || right_change > 4) {
        channel->anchor = frame;
        channel->quiet_frames = 0;
    } else if (channel->quiet_frames < 4000) {
        ++channel->quiet_frames;
    } else {
        frame = channel->anchor;
    }
    return frame;
}

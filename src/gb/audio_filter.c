/**
 * @file    audio_filter.c
 * @brief   Stereo coupling-capacitor filtering and optional hardware interference.
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
#include "audio_filter.h"
#include "audio_synthesis.h"

#include <assert.h>
#include <math.h>

void cupid_gb_audio_filter_set_rate(CupidGbAudioFilterState *state, uint32_t clock_hz,
                                    uint32_t sample_rate) {
    assert(state);
    if (!sample_rate)
        return;
    assert(clock_hz);
    state->retention = pow(0.999958, (double)clock_hz / sample_rate);
}

static unsigned connection_count(unsigned mask) {
    const unsigned pairs = (mask & 5u) + ((mask >> 1) & 5u);
    return (pairs & 3u) + (pairs >> 2);
}

static void dc_targets(uint8_t enabled_dacs, uint8_t nr50, uint8_t nr51, double target[2]) {
    const unsigned reversed = ((enabled_dacs & 1u) << 3) | ((enabled_dacs & 2u) << 1) |
                              ((enabled_dacs & 4u) >> 1) | ((enabled_dacs & 8u) >> 3);
    /* The DC path reverses channel selection and exchanges the mixer sides.
     * Keep its estimate separate from the audible NR51 routing. */
    const unsigned low = connection_count(reversed & (nr51 & 15u));
    const unsigned high = connection_count(reversed & (nr51 >> 4));
    target[0] = (double)(low * ((nr50 & 7u) + 1u) * 510u);
    target[1] = (double)(high * (((nr50 >> 4) & 7u) + 1u) * 510u);
}

static int32_t hardware_bias(const CupidGbAudioConditions *conditions, uint32_t random_value) {
    static const int32_t display_bias[4] = {582, 0, 582, 873};
    int32_t bias = conditions->lcd_enabled ? display_bias[conditions->lcd_mode] : 0;
    if (conditions->halted)
        bias -= 816;
    if (conditions->apu_enabled)
        bias += 408;
    if (conditions->color_hardware) {
        if (conditions->infrared_output)
            bias += 408;
    } else {
        /* Reduce the signed hardware contribution before adding random noise. */
        bias /= 4;
    }
    return bias + (int32_t)(random_value % 340u);
}

static int16_t clip_sample(int32_t sample) {
    if (sample > INT16_MAX)
        return INT16_MAX;
    if (sample < INT16_MIN)
        return INT16_MIN;
    return (int16_t)sample;
}

CupidGbAudioFrame cupid_gb_audio_filter_frame(CupidGbAudioFilterState *state,
                                              const CupidGbAudioPolicy *policy,
                                              const CupidGbAudioConditions *conditions,
                                              int32_t left, int32_t right, uint8_t enabled_dacs,
                                              uint8_t nr50, uint8_t nr51, uint32_t random_value) {
    assert(state && policy && conditions);
    assert(!policy->filter_disabled || !policy->preserve_waveform);
    assert(isfinite(policy->interference_volume) && policy->interference_volume >= 0 &&
           policy->interference_volume <= 1);
    assert(isfinite(state->retention) && state->retention >= 0 && state->retention <= 1);
    assert(isfinite(state->interference) &&
           fabs(state->interference) <= CUPID_GB_AUDIO_FILTER_HISTORY_LIMIT);
    assert(isfinite(state->capacitor[0]) &&
           fabs(state->capacitor[0]) <= CUPID_GB_AUDIO_FILTER_HISTORY_LIMIT);
    assert(isfinite(state->capacitor[1]) &&
           fabs(state->capacitor[1]) <= CUPID_GB_AUDIO_FILTER_HISTORY_LIMIT);
    assert(enabled_dacs <= 15 && conditions->lcd_mode <= 3);

    const int16_t mixed[2] = {cupid_gb_audio_narrow16(left), cupid_gb_audio_narrow16(right)};
    int16_t filtered[2] = {0, 0};
    const double retention = state->retention;
    if (conditions->color_hardware || enabled_dacs) {
        if (policy->filter_disabled) {
            filtered[0] = mixed[0];
            filtered[1] = mixed[1];
            state->capacitor[0] = 0;
            state->capacitor[1] = 0;
        } else {
            double target[2] = {0, 0};
            if (policy->preserve_waveform)
                dc_targets(enabled_dacs, nr50, nr51, target);
            for (unsigned side = 0; side < 2; ++side) {
                const double previous = state->capacitor[side];
                const int16_t integral = cupid_gb_audio_narrow16((int32_t)previous);
                /* Convert the capacitor before subtraction, then narrow again.
                 * Converting the complete fractional difference changes PCM. */
                filtered[side] = cupid_gb_audio_narrow16((int32_t)mixed[side] - integral);
                if (policy->preserve_waveform) {
                    state->capacitor[side] = target[side] * (1 - retention) + previous * retention;
                } else {
                    const double difference = mixed[side] - previous;
                    state->capacitor[side] = mixed[side] - difference * retention;
                }
            }
        }
    }

    if (policy->interference_volume != 0) {
        const int32_t bias = hardware_bias(conditions, random_value);
        const int16_t difference = cupid_gb_audio_narrow16((int32_t)(bias - state->interference));
        state->interference = state->interference * retention + (1 - retention) * difference;
        /* Keep the interference history, but add the scaled raw bias to PCM. */
        const int32_t scaled = (int32_t)(bias * policy->interference_volume);
        filtered[0] = clip_sample((int32_t)filtered[0] + scaled);
        filtered[1] = clip_sample((int32_t)filtered[1] + scaled);
    }
    return (CupidGbAudioFrame){filtered[0], filtered[1]};
}

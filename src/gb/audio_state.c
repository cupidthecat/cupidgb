/**
 * @file    audio_state.c
 * @brief   Portable audio snapshots with bounded reconstruction and filter history.
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

#include "audio_state.h"

#include "audio_filter.h"
#include "audio_output.h"
#include "cupid/gb/apu.h"
#include "state_io.h"

#include <float.h>
#include <math.h>

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Native audio states require IEEE 754 binary64");

static void signed_word(StateIo *io, int16_t *value) {
    uint16_t raw = (uint16_t)*value;
    word(io, &raw);
    if (io->input)
        *value = raw <= INT16_MAX ? (int16_t)raw : (int16_t)((int32_t)raw - 65536);
}

static void signed_double_word(StateIo *io, int32_t *value) {
    uint32_t raw = (uint32_t)*value;
    double_word(io, &raw);
    if (io->input)
        *value = raw <= INT32_MAX ? (int32_t)raw : -1 - (int32_t)(UINT32_MAX - raw);
}

static void real(StateIo *io, double *value) {
    uint64_t raw;
    memcpy(&raw, value, sizeof(raw));
    quad_word(io, &raw);
    if (io->input)
        memcpy(value, &raw, sizeof(raw));
}

static void channel_fields(StateIo *io, CupidGbAudioChannel *channel) {
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned frame = 0; frame < CUPID_GB_AUDIO_RING_FRAMES; ++frame)
            signed_double_word(io, &channel->pending[side][frame]);
    for (unsigned side = 0; side < 2; ++side)
        signed_double_word(io, &channel->level[side]);
    for (unsigned side = 0; side < 2; ++side)
        signed_word(io, &channel->input[side]);
    signed_word(io, &channel->anchor.left);
    signed_word(io, &channel->anchor.right);
    word(io, &channel->quiet_frames);
    byte(io, &channel->cursor);
}

static void audio_fields(StateIo *io, CupidGb *gb, bool startup_filter_flag) {
    CupidGbAudioOutput *output = &gb->apu.output;
    for (unsigned channel = 0; channel < CUPID_GB_AUDIO_CHANNELS; ++channel)
        channel_fields(io, &output->channels[channel]);
    real(io, &output->filter.capacitor[0]);
    real(io, &output->filter.capacitor[1]);
    real(io, &output->filter.interference);
    real(io, &output->filter.retention);
    real(io, &output->policy.interference_volume);
    byte(io, &output->policy.muted_channels);
    /* V30 uses a spare bit in the old Preserve Waveform byte. */
    uint8_t flags =
        (uint8_t)(output->policy.preserve_waveform | (output->filter.startup_pending << 1));
    byte(io, &flags);
    if (io->input) {
        if (flags > (startup_filter_flag ? 3u : 1u))
            io->invalid = true;
        output->policy.preserve_waveform = (flags & 1u) != 0;
        output->filter.startup_pending = (flags & 2u) != 0;
    }
    for (unsigned channel = 0; channel < CUPID_GB_AUDIO_CHANNELS; ++channel)
        real(io, &output->dac_charge[channel]);
    for (unsigned frame = 0; frame < CUPID_GB_AUDIO_DAC_DELAY; ++frame)
        bytes(io, output->dac_gain_delay[frame], CUPID_GB_AUDIO_CHANNELS);
    bytes(io, output->dac_enable_delay, CUPID_GB_AUDIO_DAC_DELAY);
    byte(io, &output->dac_delay_cursor);
    double_word(io, &output->sample_fraction);
    double_word(io, &output->render_cycles);
    double_word(io, &output->fraction_step);
    word(io, &output->max_span);
    boolean(io, &output->fraction_half);
    double_word(io, &output->noise_state);
    bytes(io, gb->apu.dac_sample, CUPID_GB_AUDIO_CHANNELS);
    word(io, &gb->apu.pending_cycles);
    boolean(io, &gb->apu.noise_timing.feedback_latch);
    byte(io, &gb->apu.noise_timing.start_delay);
}

static bool unit_value(double value) {
    return isfinite(value) && value >= 0 && value <= 1;
}

static bool filter_history(double value) {
    return isfinite(value) && fabs(value) <= CUPID_GB_AUDIO_FILTER_HISTORY_LIMIT;
}

bool cupid_gb_audio_state_valid(const CupidGb *gb) {
    if (!gb)
        return false;
    const CupidGbAudioOutput *output = &gb->apu.output;
    const uint32_t rate = cupid_gb_apu_get_sample_rate(gb);
    const uint32_t clock = cupid_gb_base_clock_hz(gb);
    if (rate < 8000 || rate > 192000 || output->buf_write > CUPID_GB_APU_BUF_FRAMES ||
        gb->apu.pending_cycles > 2048 || output->render_cycles > 2048 ||
        gb->apu.noise_timing.start_delay > 12 || output->sample_fraction >= (UINT32_C(2) << 28) ||
        output->sample_acc > clock + UINT32_C(192000) * 2048 ||
        output->dac_delay_cursor >= CUPID_GB_AUDIO_DAC_DELAY ||
        output->policy.muted_channels > 15 ||
        (output->policy.filter_disabled && output->policy.preserve_waveform) ||
        (output->filter.startup_pending && !cupid_gb_audio_startup_filter_supported(gb)) ||
        !unit_value(output->policy.interference_volume) || !unit_value(output->filter.retention) ||
        !filter_history(output->filter.capacitor[0]) ||
        !filter_history(output->filter.capacitor[1]) ||
        !filter_history(output->filter.interference))
        return false;
    /* A rate change keeps pending delivery credit. Its bound therefore uses the
     * largest supported rate, rather than the newly selected smaller rate. */
    const uint32_t step = (uint32_t)(((uint64_t)rate * (UINT64_C(1) << 29)) / clock);
    const uint64_t phase_pair = 2u * (uint64_t)step;
    const uint16_t span = (uint16_t)(2u * (((UINT64_C(1) << 29) + phase_pair - 1u) / phase_pair));
    if ((output->max_span || output->fraction_step) &&
        (output->max_span != span || output->fraction_step != step))
        return false;
    if (!output->max_span && (gb->apu.pending_cycles || output->render_cycles ||
                              output->sample_fraction || output->fraction_half))
        return false;
    /* Pending hardware clocks need matching rational delivery credit. Permit
     * one frame of rounded-phase carry, but reject histories that could run
     * reconstruction past its four-frame event window without a render. */
    const uint64_t phase = (uint64_t)output->sample_fraction * 2u + output->fraction_half +
                           (uint64_t)output->fraction_step * gb->apu.pending_cycles;
    const uint64_t credit = (uint64_t)output->sample_acc + clock;
    if (phase * clock > (UINT64_C(1) << 29) * credit)
        return false;
    /* A shortened first span may spend one frame of credit before its rounded
     * phase reaches a whole frame. Rendering then clamps that fraction to zero.
     * Account for that lost carry before validating the remaining hardware work.
     * Later complete spans each advance at least one frame at every allowed rate. */
    const unsigned available =
        output->render_cycles < output->max_span ? output->max_span - output->render_cycles : 0;
    const unsigned first = gb->apu.pending_cycles < available ? gb->apu.pending_cycles : available;
    const uint64_t first_phase = (uint64_t)output->sample_fraction * 2u + output->fraction_half +
                                 (uint64_t)output->fraction_step * first;
    if (output->sample_acc >= clock && first_phase < (UINT64_C(1) << 29)) {
        const uint64_t rest = (uint64_t)output->fraction_step * (gb->apu.pending_cycles - first);
        if (rest * clock > (UINT64_C(1) << 29) * output->sample_acc)
            return false;
    }
    for (unsigned channel = 0; channel < CUPID_GB_AUDIO_CHANNELS; ++channel) {
        const CupidGbAudioChannel *history = &output->channels[channel];
        if (gb->apu.dac_sample[channel] > 15 || !unit_value(output->dac_charge[channel]) ||
            history->cursor >= CUPID_GB_AUDIO_RING_FRAMES || history->quiet_frames > 4000 ||
            history->input[0] < -120 || history->input[0] > 120 || history->input[1] < -120 ||
            history->input[1] > 120)
            return false;
        /* Pending values and running integrals are full signed 32-bit words.
         * Synthesis defines their modular arithmetic for every imported value. */
        for (unsigned frame = 0; frame < CUPID_GB_AUDIO_DAC_DELAY; ++frame)
            if (output->dac_gain_delay[frame][channel] > 34)
                return false;
    }
    for (unsigned frame = 0; frame < CUPID_GB_AUDIO_DAC_DELAY; ++frame)
        if (output->dac_enable_delay[frame] > 15)
            return false;
    return true;
}

bool cupid_gb_audio_state_encode(const CupidGb *gb, uint8_t *data, size_t length) {
    if (!data || length != CUPID_GB_AUDIO_STATE_SIZE || !cupid_gb_audio_state_valid(gb))
        return false;
    StateIo io = {NULL, data, 0, length, false};
    audio_fields(&io, (CupidGb *)gb, true);
    return !io.invalid && io.offset == length;
}

bool cupid_gb_audio_state_decode(CupidGb *gb, const uint8_t *data, size_t length,
                                 bool startup_filter_flag) {
    if (!gb || !data || length != CUPID_GB_AUDIO_STATE_SIZE)
        return false;
    StateIo io = {data, NULL, 0, length, false};
    audio_fields(&io, gb, startup_filter_flag);
    if (io.invalid || io.offset != length)
        return false;
    CupidGbAudioOutput *output = &gb->apu.output;
    const uint32_t rate = cupid_gb_apu_get_sample_rate(gb);
    const uint32_t clock = cupid_gb_base_clock_hz(gb);
    if (rate >= 8000 && rate <= 192000) {
        /* Early V14 writers rounded this derived cache to the nearest integer.
         * Recognize only that exact cache and its matching span, then retain the
         * captured history with an increment that cannot resume positive drift. */
        const uint64_t numerator = (uint64_t)rate * (UINT64_C(1) << 29);
        const uint32_t legacy_step = (uint32_t)((numerator + clock / 2) / clock);
        const uint16_t span = (uint16_t)(2u * ((clock + 2u * rate - 1u) / (2u * rate)));
        if (output->max_span == span && output->fraction_step == legacy_step)
            output->fraction_step = (uint32_t)(numerator / clock);
    }
    return cupid_gb_audio_state_valid(gb);
}

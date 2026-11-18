/**
 * @file    audio_output.c
 * @brief   Channel mixing, DAC response, host audio policy, and stereo delivery.
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
#include "audio_output.h"

#include "audio_filter.h"
#include "audio_synthesis.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/sgb_startup.h"

#include <assert.h>
#include <math.h>
#include <string.h>

uint32_t cupid_gb_apu_get_sample_rate(const CupidGb *gb) {
    return gb && gb->apu.output.sample_rate ? gb->apu.output.sample_rate : CUPID_GB_APU_SAMPLE_RATE;
}

static void configure_rate(CupidGb *gb, uint32_t rate) {
    CupidGbAudioOutput *output = &gb->apu.output;
    const uint32_t clock = cupid_gb_base_clock_hz(gb);
    /* Reconstruction must not gain phase over rational sample delivery. An
     * upward-rounded increment accumulates whole frames on the television
     * adapter's clock; truncation keeps its history bounded during long runs. */
    output->fraction_step = (uint32_t)(((uint64_t)rate * (UINT64_C(1) << 29)) / clock);
    /* Keep the even scheduling span large enough for the truncated phase
     * increment. Exact rational divisors can otherwise stop one phase unit
     * before a reconstruction frame (PAL at 64,489 Hz). */
    const uint64_t phase_pair = 2u * (uint64_t)output->fraction_step;
    output->max_span = (uint16_t)(2u * (((UINT64_C(1) << 29) + phase_pair - 1u) / phase_pair));
    cupid_gb_audio_filter_set_rate(&output->filter, clock, rate);
}

void cupid_gb_audio_configure(CupidGb *gb) {
    if (!gb->apu.output.max_span)
        configure_rate(gb, cupid_gb_apu_get_sample_rate(gb));
}

bool cupid_gb_apu_set_sample_rate(CupidGb *gb, uint32_t rate) {
    if (!gb || rate < 8000u || rate > 192000u)
        return false;
    if (rate != cupid_gb_apu_get_sample_rate(gb)) {
        /* A materialized startup batch belongs to its generation rate. Reject
         * before deferred clocks or either output owner can change. */
        if (gb->sgb_startup.output.batch_count)
            return false;
        cupid_gb_apu_run(gb, true);
    }
    gb->apu.output.sample_rate = rate;
    configure_rate(gb, rate);
    return true;
}

bool cupid_gb_apu_set_filter_mode(CupidGb *gb, CupidGbAudioFilter mode) {
    if (!gb || mode < CUPID_GB_AUDIO_FILTER_OFF || mode > CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM)
        return false;
    gb->apu.output.policy.filter_disabled = mode == CUPID_GB_AUDIO_FILTER_OFF;
    gb->apu.output.policy.preserve_waveform = mode == CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM;
    return true;
}

CupidGbAudioFilter cupid_gb_apu_get_filter_mode(const CupidGb *gb) {
    if (gb && gb->apu.output.policy.filter_disabled)
        return CUPID_GB_AUDIO_FILTER_OFF;
    if (gb && gb->apu.output.policy.preserve_waveform)
        return CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM;
    return CUPID_GB_AUDIO_FILTER_ACCURATE;
}

void cupid_gb_apu_set_highpass(CupidGb *gb, bool enabled) {
    (void)cupid_gb_apu_set_filter_mode(gb, enabled ? CUPID_GB_AUDIO_FILTER_ACCURATE
                                                   : CUPID_GB_AUDIO_FILTER_OFF);
}

bool cupid_gb_apu_set_interference_volume(CupidGb *gb, double volume) {
    if (!gb || !isfinite(volume) || volume < 0 || volume > 1)
        return false;
    gb->apu.output.policy.interference_volume = volume;
    return true;
}

double cupid_gb_apu_get_interference_volume(const CupidGb *gb) {
    return gb ? gb->apu.output.policy.interference_volume : 0;
}

bool cupid_gb_apu_set_channel_muted(CupidGb *gb, unsigned channel, bool muted) {
    if (!gb || channel >= CUPID_GB_AUDIO_CHANNELS)
        return false;
    const uint8_t mask = (uint8_t)(1u << channel);
    if (muted)
        gb->apu.output.policy.muted_channels |= mask;
    else
        gb->apu.output.policy.muted_channels &= (uint8_t)~mask;
    return true;
}

bool cupid_gb_apu_is_channel_muted(const CupidGb *gb, unsigned channel) {
    return gb && channel < CUPID_GB_AUDIO_CHANNELS &&
           (gb->apu.output.policy.muted_channels & (1u << channel)) != 0;
}

uint8_t cupid_gb_audio_dac_mask(const CupidGb *gb) {
    const CupidGbApu *apu = &gb->apu;
    return (uint8_t)((apu->ch1_dac ? 1u : 0) | (apu->ch2_dac ? 2u : 0) | (apu->ch3_dac ? 4u : 0) |
                     (apu->ch4_dac ? 8u : 0));
}

void cupid_gb_audio_update(CupidGb *gb, unsigned channel, uint8_t value, unsigned base_offset,
                           bool force) {
    assert(channel < CUPID_GB_AUDIO_CHANNELS && value < 16);
    CupidGbApu *apu = &gb->apu;
    CupidGbAudioOutput *output = &apu->output;
    if (!force && value == 0 && apu->dac_sample[channel] == 0)
        return;
    if (cupid_gb_audio_dac_mask(gb) & (1u << channel))
        apu->dac_sample[channel] = value;
    else
        value = apu->dac_sample[channel];
    cupid_gb_audio_configure(gb);
    CupidGbAudioFrame input = {0};
    if (!(output->policy.muted_channels & (1u << channel))) {
        const uint8_t routing = gb->io_registers[0x25];
        const uint8_t volume = gb->io_registers[0x24];
        const int voltage = 15 - 2 * (int)value;
        if (routing & (0x10u << channel))
            input.left = (int16_t)(voltage * (((volume >> 4) & 7) + 1));
        if (routing & (1u << channel))
            input.right = (int16_t)(voltage * ((volume & 7) + 1));
    }
    const uint64_t fraction = ((uint64_t)output->sample_fraction << 1) + output->fraction_half +
                              (uint64_t)base_offset * output->fraction_step;
    cupid_gb_audio_synth_update(&output->channels[channel], input, (unsigned)(fraction >> 21));
}

void cupid_gb_audio_update_channels(CupidGb *gb, uint8_t channels, unsigned base_offset,
                                    bool force) {
    uint8_t samples[CUPID_GB_AUDIO_CHANNELS];
    cupid_gb_apu_channel_samples(gb, samples);
    for (unsigned channel = 0; channel < CUPID_GB_AUDIO_CHANNELS; ++channel)
        if (channels & (1u << channel))
            cupid_gb_audio_update(gb, channel,
                                  force ? gb->apu.dac_sample[channel] : samples[channel],
                                  base_offset, force);
}

void cupid_gb_audio_credit(CupidGb *gb, unsigned base_cycles) {
    cupid_gb_audio_configure(gb);
    const uint64_t total = (uint64_t)gb->apu.output.sample_acc +
                           (uint64_t)cupid_gb_apu_get_sample_rate(gb) * base_cycles;
    assert(total <= UINT32_MAX);
    gb->apu.output.sample_acc = (uint32_t)total;
}

void cupid_gb_audio_advance(CupidGb *gb, unsigned base_cycles) {
    CupidGbAudioOutput *output = &gb->apu.output;
    const uint64_t fraction = ((uint64_t)output->sample_fraction << 1) + output->fraction_half +
                              (uint64_t)base_cycles * output->fraction_step;
    output->sample_fraction = (uint32_t)(fraction >> 1);
    output->fraction_half = (fraction & 1u) != 0;
    output->render_cycles += base_cycles;
    assert(output->sample_fraction < (4u << 28));
}

static uint8_t dac_gain(CupidGbAudioOutput *output, unsigned channel, bool enabled, uint32_t rate) {
    double charge = output->dac_charge[channel] + (enabled ? 20000.0 : -20000.0) / rate;
    if (charge < 0)
        charge = 0;
    else if (charge > 1)
        charge = 1;
    output->dac_charge[channel] = charge;
    return (uint8_t)(34 * (3 * charge * charge - 2 * charge * charge * charge));
}

void cupid_gb_audio_render(CupidGb *gb) {
    CupidGbAudioOutput *output = &gb->apu.output;
    const uint32_t rate = cupid_gb_apu_get_sample_rate(gb);
    const bool color = gb->model == CUPID_GB_MODEL_CGB;
    const bool delay = !color && output->max_span != 2;
    uint8_t enabled = cupid_gb_audio_dac_mask(gb);
    int32_t left = 0, right = 0;
    for (unsigned channel = 0; channel < CUPID_GB_AUDIO_CHANNELS; ++channel) {
        uint8_t gain = dac_gain(output, channel, (enabled & (1u << channel)) != 0, rate);
        if (delay) {
            uint8_t *slot = &output->dac_gain_delay[output->dac_delay_cursor][channel];
            const uint8_t prior = *slot;
            *slot = gain;
            gain = prior;
        }
        const CupidGbAudioFrame frame = cupid_gb_audio_synth_read(&output->channels[channel], gain);
        left += frame.left;
        right += frame.right;
    }
    if (delay) {
        uint8_t *slot = &output->dac_enable_delay[output->dac_delay_cursor];
        const uint8_t prior = *slot;
        *slot = enabled;
        enabled = prior;
        output->dac_delay_cursor =
            (uint8_t)((output->dac_delay_cursor + 1u) % CUPID_GB_AUDIO_DAC_DELAY);
    }
    output->render_cycles = 0;
    if (output->sample_fraction < (1u << 28)) {
        output->sample_fraction = 0;
        output->fraction_half = false;
    } else {
        output->sample_fraction -= 1u << 28;
    }
    /* Reconstruction and DAC history continue behind adapter startup. Its own
     * sound bypasses the handheld filter, interference, and ordinary queue. */
    if (cupid_gb_sgb_startup_active(gb))
        return;
    const CupidGbAudioConditions conditions = {color,
                                               gb->cpu.halted,
                                               (gb->io_registers[0x40] & 0x80) != 0,
                                               gb->apu.apu_on,
                                               (gb->io_registers[0x56] & 1) != 0,
                                               (uint8_t)(gb->io_registers[0x41] & 3)};
    /* Keep optional noise local to this instance so native continuation remains
     * reproducible across hosts. A zero volume leaves its sequence untouched. */
    uint32_t random_value = 0;
    if (output->policy.interference_volume) {
        uint32_t state = output->noise_state ? output->noise_state : UINT32_C(0x6d2b79f5);
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        random_value = output->noise_state = state;
    }
    const CupidGbAudioFrame frame = cupid_gb_audio_filter_frame(
        &output->filter, &output->policy, &conditions, left, right, enabled, gb->io_registers[0x24],
        gb->io_registers[0x25], random_value);
    if (output->buf_write < CUPID_GB_APU_BUF_FRAMES) {
        const size_t index = (size_t)output->buf_write++ * 2u;
        output->buf[index] = frame.left;
        output->buf[index + 1] = frame.right;
    }
}

uint32_t cupid_gb_apu_drain(CupidGb *gb, int16_t *out, uint32_t max_frames) {
    if (!gb || !out)
        return 0;
    uint32_t written = 0;
    while (written < max_frames) {
        const uint32_t count =
            cupid_gb_apu_drain_chunk(gb, out + (size_t)written * 2u, max_frames - written, NULL);
        if (!count)
            break;
        written += count;
    }
    return written;
}

void cupid_gb_audio_import_legacy(CupidGb *gb) {
    CupidGbAudioOutput *output = &gb->apu.output;
    const uint32_t rate = output->sample_rate;
    const uint32_t credit = output->sample_acc;
    const uint32_t count = output->buf_write;
    const bool disabled = output->policy.filter_disabled;
    /* The old prefix contains queued PCM and delivery credit, but no step
     * response or DAC history. Keep that prefix and seed a settled waveform. */
    memset(output, 0, offsetof(CupidGbAudioOutput, buf));
    output->sample_rate = rate;
    output->sample_acc = credit;
    output->buf_write = count;
    output->policy.filter_disabled = disabled;
    cupid_gb_audio_configure(gb);
    output->sample_fraction = (uint32_t)(((uint64_t)(credit % cupid_gb_base_clock_hz(gb)) << 28) /
                                         cupid_gb_base_clock_hz(gb));
    gb->apu.pending_cycles = 0;
    cupid_gb_apu_channel_samples(gb, gb->apu.dac_sample);
    const uint8_t enabled = cupid_gb_audio_dac_mask(gb);
    const uint8_t routing = gb->io_registers[0x25];
    const uint8_t volume = gb->io_registers[0x24];
    for (unsigned channel = 0; channel < CUPID_GB_AUDIO_CHANNELS; ++channel) {
        CupidGbAudioChannel *state = &output->channels[channel];
        const int voltage = 15 - 2 * (int)gb->apu.dac_sample[channel];
        state->input[0] = (routing & (0x10u << channel))
                              ? (int16_t)(voltage * (int)(((volume >> 4) & 7u) + 1u))
                              : 0;
        state->input[1] =
            (routing & (1u << channel)) ? (int16_t)(voltage * (int)((volume & 7u) + 1u)) : 0;
        for (unsigned side = 0; side < 2; ++side)
            state->level[side] = (int32_t)state->input[side] * 65536;
        const uint8_t gain = (enabled & (1u << channel)) ? 34 : 0;
        output->dac_charge[channel] = gain ? 1 : 0;
        state->anchor = (CupidGbAudioFrame){(int16_t)(state->input[0] * gain),
                                            (int16_t)(state->input[1] * gain)};
        for (unsigned frame = 0; frame < CUPID_GB_AUDIO_DAC_DELAY; ++frame)
            output->dac_gain_delay[frame][channel] = gain;
    }
    memset(output->dac_enable_delay, enabled, sizeof(output->dac_enable_delay));
}

/**
 * @file    apu.c
 * @brief   Game Boy sound-channel clocks, register behavior, and audio delivery.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * The DIV-driven sequencer clocks channel lengths, volume envelopes, and
 * the first pulse channel's sweep unit. Band-limited reconstruction and DC
 * blocking feed the bounded stereo output buffer, which defaults to 44100 Hz.
 * The hardware model selects quirks such as monochrome wave RAM corruption
 * on an active wave-channel retrigger.
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
#include "apu_noise.h"
#include "audio_output.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <string.h>

#include "cupid/gb/gb.h"

/**
 * @brief Pulse duty-cycle waveforms.
 *
 * Encodes each eight-step waveform in a byte, with phase zero in bit zero.
 * Duty phase is observable when switching duty without retriggering the channel.
 *
 *   - Duty 0: 12.5%  (1 high out of 8)
 *   - Duty 1: 25%    (2 high out of 8)
 *   - Duty 2: 50%    (4 high out of 8)
 *   - Duty 3: 75%    (6 high out of 8)
 */
static const uint8_t cupid_gb_duty_mask[4] = {0x80, 0x81, 0xe1, 0x7e};

/**
 * @brief CH4 noise channel base clock divisors indexed by div_code (0–7).
 *
 * Each value represents the base period in T-cycles before applying the
 * clock shift. Used in computing the LFSR timer period.
 */
static const uint16_t cupid_gb_noise_div[8u] = {8u, 16u, 32u, 48u, 64u, 80u, 96u, 112u};

void cupid_gb_apu_import_legacy(CupidGb *gb) {
    if (!gb) {
        return;
    }
    CupidGbApu *apu = &gb->apu;
    apu->clock_phase = apu->apu_on ? (uint8_t)(gb->elapsed_cycles & 3u) : 0;
    apu->envelope_delay = 0;
    memset(&apu->sweep_timing, 0, sizeof(apu->sweep_timing));
    memset(&apu->noise_timing, 0, sizeof(apu->noise_timing));
    apu->noise_timing.foreground = apu->noise_timing.background = apu->ch4_on && apu->ch4_dac;
    apu->noise_timing.output_high = apu->ch4_on && !(apu->ch4_lfsr & 1);
    apu->noise_timing.remaining = apu->ch4_div_code ? (uint8_t)(apu->ch4_div_code * 4) : 2;
    apu->noise_timing.feedback_latch = ((apu->ch4_lfsr ^ (apu->ch4_lfsr >> 1)) & 1u) == 0;
    memset(apu->pulse_timing, 0, sizeof(apu->pulse_timing));
    apu->pulse_timing[0].edge_age = apu->pulse_timing[1].edge_age = 4;
    apu->pulse_timing[0].clock_started = apu->ch1_on;
    apu->pulse_timing[1].clock_started = apu->ch2_on;
    apu->pulse_timing[0].output_high =
        ((cupid_gb_duty_mask[apu->ch1_duty] >> apu->ch1_duty_pos) & 1u) != 0;
    apu->pulse_timing[1].output_high =
        ((cupid_gb_duty_mask[apu->ch2_duty] >> apu->ch2_duty_pos) & 1u) != 0;
}

static void pulse_latch_wave(CupidGbApu *apu, unsigned channel) {
    uint8_t duty = channel ? apu->ch2_duty : apu->ch1_duty;
    uint8_t index = channel ? apu->ch2_duty_pos : apu->ch1_duty_pos;
    apu->pulse_timing[channel].output_high = ((cupid_gb_duty_mask[duty] >> index) & 1u) != 0;
}

void cupid_gb_apu_apply_cgb_boot_pulse(CupidGb *gb, unsigned native_divider,
                                       unsigned compatibility_cycles) {
    enum { BOOT_FREQUENCY = 1985, PERIOD = (2048 - BOOT_FREQUENCY) * 4 };

    /* The last trigger runs eight clocks later on the compatibility path. */
    const unsigned extra = compatibility_cycles ? compatibility_cycles - 8u : 0u;
    const unsigned duty_phase = PERIOD - 148u + extra;
    CupidGbApu *apu = &gb->apu;
    apu->ch1_duty_pos = (uint8_t)((6u + duty_phase / PERIOD) & 7u);
    apu->ch1_freq = BOOT_FREQUENCY;
    apu->ch1_timer = (uint16_t)(PERIOD - duty_phase % PERIOD);
    apu->ch1_len = 64u;
    if (gb->model == CUPID_GB_MODEL_CGB_A || gb->model == CUPID_GB_MODEL_CGB_B) {
        /* Both trigger writes can clock length on A/B. During this interval
         * the parity of the sequencer step follows divider bit 13. */
        const unsigned first = native_divider - 0xaf08u + (compatibility_cycles ? 16u : 0u);
        const unsigned second = native_divider - 0x65b8u + (compatibility_cycles ? 8u : 0u);
        apu->ch1_len -= (uint8_t)(((first >> 13u) & 1u) + ((second >> 13u) & 1u));
    }

    apu->ch1_vol = 0;
    apu->ch1_env_timer = 2u;
    apu->ch1_sweep_timer = 8u;
    apu->pulse_timing[0].envelope = (CupidGbEnvelopeGate){.locked = true, .lock_pending = true};
    pulse_latch_wave(apu, 0);
    gb->io_registers[0x10] = 0x00u;
    gb->io_registers[0x11] = 0x80u;
    gb->io_registers[0x13] = (uint8_t)BOOT_FREQUENCY;
    gb->io_registers[0x14] = (uint8_t)(0x80u | (BOOT_FREQUENCY >> 8u));
    /* Seed the existing output path from the cartridge-entry DAC level. */
    cupid_gb_audio_import_legacy(gb);
    gb->apu.output.filter.startup_pending = true;
}

static bool pulse_gate_off(CupidGbApu *apu, unsigned channel) {
    bool dac = channel ? apu->ch2_dac : apu->ch1_dac;
    bool *active = channel ? &apu->ch2_on : &apu->ch1_on;
    bool changed = *active || (dac && !apu->pulse_timing[channel].clock_started &&
                               apu->pulse_dac_hold[channel] != 0);
    *active = false;
    if (dac)
        apu->pulse_dac_hold[channel] = 0;
    return changed;
}

static void pulse_disconnect_dac(CupidGbApu *apu, unsigned channel) {
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    bool *dac = channel ? &apu->ch2_dac : &apu->ch1_dac;
    bool *active = channel ? &apu->ch2_on : &apu->ch1_on;
    if (*dac && (*active || timing->clock_started)) {
        bool high = *active && timing->output_high && !timing->output_blocked;
        apu->pulse_dac_hold[channel] = high ? (channel ? apu->ch2_vol : apu->ch1_vol) : 0;
    }
    /* Disconnect freezes the code before removing the output/clock gates.
     * Repeated disconnects cannot replace that code with an inactive zero. */
    *dac = *active = timing->clock_started = false;
}

void cupid_gb_apu_import_monochrome_pulses(CupidGb *gb) {
    if (!gb || cupid_gb_model_is_cgb(gb->model))
        return;
    CupidGbApu *apu = &gb->apu;
    for (unsigned channel = 0; channel < 2; ++channel) {
        CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
        bool active = channel ? apu->ch2_on : apu->ch1_on;
        bool dac = channel ? apu->ch2_dac : apu->ch1_dac;
        uint16_t timer = channel ? apu->ch2_timer : apu->ch1_timer;
        /* Older engines did not record the held duty or clock gate. Preserve
         * their combinational output and infer a running clock from its timer. */
        *timing = (CupidGbPulseTiming){0};
        timing->edge_age = 4;
        timing->clock_started = apu->apu_on && dac && (active || timer != 0);
        pulse_latch_wave(apu, channel);
    }
}

void cupid_gb_apu_import_monochrome_noise(CupidGb *gb) {
    if (!gb)
        return;
    CupidGbApu *apu = &gb->apu;
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    timing->start_delay = 0;
    timing->feedback_latch = ((apu->ch4_lfsr ^ (apu->ch4_lfsr >> 1)) & 1u) == 0;
    if (cupid_gb_model_is_cgb(gb->model))
        return;
    const unsigned period = apu->ch4_div_code ? apu->ch4_div_code * 8u : 4u;
    const uint32_t remaining = apu->ch4_timer ? apu->ch4_timer : period;
    const uint32_t edges = (uint32_t)(((uint64_t)remaining + period - 1u) / period);
    *timing = (CupidGbNoiseTiming){0};
    timing->clock_phase = (uint8_t)(gb->elapsed_cycles & 7u);
    timing->remaining = (uint8_t)((((remaining - 1u) % period) + 2u) / 2u);
    timing->divider_counter = (uint16_t)(((1u << apu->ch4_shift) - edges) & 0x3fffu);
    timing->foreground = timing->background = apu->ch4_on && apu->ch4_dac;
    timing->output_high = apu->ch4_on && !(apu->ch4_lfsr & 1u);
    timing->feedback_latch = ((apu->ch4_lfsr ^ (apu->ch4_lfsr >> 1)) & 1u) == 0;
}

static void monochrome_pulse_trigger(CupidGbApu *apu, unsigned channel, bool was_active) {
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    uint16_t *timer = channel ? &apu->ch2_timer : &apu->ch1_timer;
    timing->clock_started = channel ? apu->ch2_dac : apu->ch1_dac;
    /* The divider retains its phase across triggers. An active retrigger takes
     * four base clocks less; neither path samples a newly written duty bit. */
    timing->start_delay = (uint8_t)((was_active ? 6u : 10u) + (apu->clock_phase & 2u));
    *timer = (uint16_t)(*timer + timing->start_delay - 2u);
    timing->edge_age = 4;
    if (!was_active)
        timing->output_blocked = true;
}

static void color_pulse_trigger(CupidGb *gb, unsigned channel, bool was_active,
                                uint16_t previous_timer, uint16_t previous_frequency) {
    CupidGbApu *apu = &gb->apu;
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    uint16_t *timer = channel ? &apu->ch2_timer : &apu->ch1_timer;
    uint8_t *index = channel ? &apu->ch2_duty_pos : &apu->ch1_duty_pos;
    uint16_t frequency = channel ? apu->ch2_freq : apu->ch1_freq;
    bool dac = channel ? apu->ch2_dac : apu->ch1_dac;
    bool reloaded = timing->edge_age < 2;
    /* The stored countdown is two base dots beyond the 2 MHz counter. Removing
     * that offset and the remaining trigger delay exposes its 1 MHz gate phase. */
    int phase = ((int)previous_timer - timing->start_delay - (was_active ? 4 : 2)) / 4;
    /* A zero timer is the cold-power sentinel, before the counter has ever run. */
    bool phantom = previous_timer != 0 && !(frequency & 0x400) && !(phase & 0x400) &&
                   (!was_active || !reloaded);
    unsigned extra_delay = 0;
    if (phantom) {
        *index = (uint8_t)((*index + 1) & 7);
        timing->output_blocked = false;
    } else if (was_active && frequency == 0x7ff && previous_frequency != 0x7ff &&
               timing->output_blocked) {
        extra_delay = 4;
    }
    /* The trigger path settles against the power-relative audio divider. An
     * active retrigger takes one 1 MHz tick less than an inactive start. The
     * final two base clocks overlap the ordinary frequency-counter reload. */
    unsigned phase_delay = apu->clock_phase & 2u;
    if (!was_active && gb->double_speed && cupid_gb_model_is_early_cgb(gb->model)) {
        /* The earlier color revisions hold an inactive fast start through
         * the opposite half of the audio divider. */
        phase_delay = 4u - phase_delay;
    }
    timing->start_delay = (uint8_t)((was_active ? 6u : 10u) + phase_delay + extra_delay);
    *timer = (uint16_t)(*timer + timing->start_delay - 2u);
    timing->clock_started = dac;
    if (!reloaded) {
        timing->edge_age = 4;
    }
    timing->envelope = (CupidGbEnvelopeGate){0};
    if (!was_active) {
        timing->output_blocked = !phantom;
        timing->output_high = false;
    } else if (!timing->output_blocked) {
        pulse_latch_wave(apu, channel);
    }
}

static bool pulse_tick(CupidGbApu *apu, unsigned channel, bool color, bool sample_pcm) {
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    uint16_t *timer = channel ? &apu->ch2_timer : &apu->ch1_timer;
    uint8_t *index = channel ? &apu->ch2_duty_pos : &apu->ch1_duty_pos;
    uint16_t frequency = channel ? apu->ch2_freq : apu->ch1_freq;
    bool active = channel ? apu->ch2_on : apu->ch1_on;
    /* Color channels stop with their output gate. Monochrome frequency clocks
     * survive length/sweep expiry until a DAC or master-power transition. */
    if (!timing->clock_started || (color && !active)) {
        return false;
    }
    if (timing->edge_age < 4) {
        ++timing->edge_age;
    }
    if (timing->start_delay) {
        --timing->start_delay;
    }
    if (*timer) {
        --*timer;
    }
    if (*timer) {
        return false;
    }
    if (sample_pcm && (!timing->output_high || timing->output_blocked ||
                       !(channel ? apu->ch2_vol : apu->ch1_vol)))
        apu->pcm_read_inhibit |= channel ? 0x00f0u : 0x000fu;
    *timer = (uint16_t)((2048u - frequency) * 4u);
    *index = (uint8_t)((*index + 1u) & 7u);
    timing->edge_age = 0;
    timing->output_blocked = false;
    pulse_latch_wave(apu, channel);
    return active;
}

/* The envelope has separate rising and falling clock latches. Reaching an
 * endpoint locks on the falling edge, even if a register write has changed the
 * volume or direction since the rising edge sampled that endpoint. */
static void color_envelope_lower(CupidGbEnvelopeGate *gate) {
    if (gate->clock) {
        gate->clock = false;
        gate->locked |= gate->lock_pending;
    }
}

static void color_envelope_raise(CupidGbEnvelopeGate *gate, uint8_t *countdown, uint8_t control,
                                 uint8_t volume) {
    if (*countdown)
        return;
    *countdown = control & 7u;
    if (*countdown && !gate->clock) {
        gate->clock = true;
        gate->lock_pending = volume == ((control & 8u) ? 15u : 0u);
    }
}

static bool color_envelope_step(CupidGb *gb, CupidGbEnvelopeGate *gate, uint8_t *volume,
                                uint8_t control, unsigned channel) {
    if (!gate->clock)
        return false;
    color_envelope_lower(gate);
    if (gate->locked || !(control & 7u))
        return false;

    const bool increase = (control & 8u) != 0;
    if (gb->double_speed && cupid_gb_model_is_early_cgb(gb->model)) {
        /* At this edge, high bits newly set by the step can still read low.
         * Bit zero is already available. Pulse 2 normally exposes bit one
         * too, except for the revision-0 increasing 1-to-2 transition. */
        const bool pulse2_bit1 =
            channel == 1 && !(gb->model == CUPID_GB_MODEL_CGB_0 && increase && *volume == 1);
        const unsigned held_bits = pulse2_bit1 ? 0xcu : 0xeu;
        gb->apu.pcm_read_inhibit |= (uint16_t)((held_bits & (15u ^ *volume)) << (4u * channel));
    }
    *volume = (uint8_t)((*volume + (increase ? 1u : 15u)) & 15u);
    return true;
}

static uint8_t color_envelope_fall(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    uint8_t changed = 0;
    for (unsigned channel = 0; channel < 2; ++channel) {
        CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
        uint8_t *volume = channel ? &apu->ch2_vol : &apu->ch1_vol;
        const uint8_t control = gb->io_registers[channel ? 0x17 : 0x12];
        if (color_envelope_step(gb, &timing->envelope, volume, control, channel) &&
            (channel ? apu->ch2_on : apu->ch1_on) && !timing->output_blocked) {
            pulse_latch_wave(apu, channel);
            changed |= (uint8_t)(1u << channel);
        }
    }
    if (color_envelope_step(gb, &apu->noise_timing.envelope, &apu->ch4_vol, gb->io_registers[0x21],
                            3) &&
        apu->ch4_on)
        changed |= 8u;
    return changed;
}

void cupid_gb_apu_finish_divider_period(CupidGb *gb) {
    if (!gb || !gb->apu.envelope_delay)
        return;
    if (gb->apu.envelope_delay > 2) {
        gb->apu.envelope_delay -= 2;
        return;
    }
    gb->apu.envelope_delay = 0;
    cupid_gb_apu_run(gb, true);
    gb->apu.pcm_read_inhibit = 0;
    if (gb->apu.apu_on && cupid_gb_model_is_cgb(gb->model)) {
        cupid_gb_audio_update_channels(gb, color_envelope_fall(gb), 0, false);
    }
}

void cupid_gb_apu_div_secondary_event(CupidGb *gb) {
    if (!gb)
        return;
    cupid_gb_apu_run(gb, true);
    gb->apu.pcm_read_inhibit = 0;
    if (!gb->apu.apu_on || !cupid_gb_model_is_cgb(gb->model)) {
        return;
    }
    CupidGbApu *apu = &gb->apu;
    for (unsigned channel = 0; channel < 2; ++channel) {
        if (channel ? apu->ch2_on : apu->ch1_on)
            color_envelope_raise(&apu->pulse_timing[channel].envelope,
                                 channel ? &apu->ch2_env_timer : &apu->ch1_env_timer,
                                 gb->io_registers[channel ? 0x17 : 0x12],
                                 channel ? apu->ch2_vol : apu->ch1_vol);
    }
    if (apu->ch4_on)
        color_envelope_raise(&apu->noise_timing.envelope, &apu->ch4_env_timer,
                             gb->io_registers[0x21], apu->ch4_vol);
}

static void envelope_control_transition(CupidGbEnvelopeGate *gate, uint8_t *volume,
                                        uint8_t *counter, uint8_t old, uint8_t value) {
    if (gate->clock) {
        *counter = value & 7;
    }
    bool advance =
        !gate->locked && (((value & 7) && !(old & 7)) || ((value & 15) == 8 && (old & 15) == 8));
    if ((old ^ value) & 8) {
        if (value & 8) {
            unsigned complement = !(old & 7) && !gate->locked ? 15 : 14;
            *volume = (uint8_t)((complement - *volume) & 15);
            advance = false;
        } else {
            *volume = (uint8_t)((16 - *volume) & 15);
        }
    }
    if (advance) {
        *volume = (uint8_t)((*volume + ((value & 8) ? 1 : 15)) & 15);
    } else if (!(value & 7)) {
        color_envelope_lower(gate);
    }
}

static void color_envelope_write(CupidGbModel model, CupidGbEnvelopeGate *gate, uint8_t *volume,
                                 uint8_t *countdown, uint8_t old, uint8_t value) {
    /* The earlier color bus passes through an all-high control nibble
     * before settling on the value written by the CPU. */
    if (cupid_gb_model_is_early_cgb(model)) {
        envelope_control_transition(gate, volume, countdown, old, 0x0f);
        old = 0x0f;
    }
    envelope_control_transition(gate, volume, countdown, old, value);
}

static void color_pulse_envelope_write(CupidGb *gb, unsigned channel, uint8_t old, uint8_t value) {
    CupidGbApu *apu = &gb->apu;
    color_envelope_write(gb->model, &apu->pulse_timing[channel].envelope,
                         channel ? &apu->ch2_vol : &apu->ch1_vol,
                         channel ? &apu->ch2_env_timer : &apu->ch1_env_timer, old, value);
    pulse_latch_wave(apu, channel);
}

static void pulse_frequency_reload(CupidGbApu *apu, unsigned channel) {
    const uint8_t age = apu->pulse_timing[channel].edge_age;
    if (age < 2) {
        const uint16_t frequency = channel ? apu->ch2_freq : apu->ch1_freq;
        uint16_t *timer = channel ? &apu->ch2_timer : &apu->ch1_timer;
        /* Either frequency byte can replace a reload during this two-clock
         * window. The current duty bit remains held until the next edge. */
        *timer = (uint16_t)((2048u - frequency) * 4u - age);
    }
}

static void color_pulse_high_write(CupidGbApu *apu, unsigned channel, uint16_t old_frequency,
                                   bool trigger) {
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    uint16_t frequency = channel ? apu->ch2_freq : apu->ch1_freq;
    uint8_t *index = channel ? &apu->ch2_duty_pos : &apu->ch1_duty_pos;
    bool active = channel ? apu->ch2_on : apu->ch1_on;
    if (timing->edge_age > 2) {
        return;
    }
    /* The high frequency bits also gate the duty clock. Removing the high-bank
     * input during its reload window cancels that phase advance, while the
     * already latched digital sample survives until the next frequency edge. A fresh
     * trigger clears eligibility for that cancellation while its delay remains. */
    if (active && !timing->start_delay && !trigger && old_frequency >= 0x700 && frequency < 0x700) {
        *index = (uint8_t)((*index + 7u) & 7u);
        timing->output_blocked = false;
    }
}

static bool color_sweep_complete(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    CupidGbSweepTiming *timing = &apu->sweep_timing;
    if (!timing->restart_hold) {
        apu->ch1_sweep_shadow = apu->ch1_freq;
    }
    bool subtract = (gb->io_registers[0x10] & 8) != 0;
    uint16_t addend = subtract ? (timing->delta ^ 0x7ff) : timing->delta;
    timing->delta = addend;
    timing->completed_delta = addend;
    if (subtract) {
        apu->ch1_sweep_subtracted = true;
    } else if (apu->ch1_sweep_shadow + addend > 0x7ff) {
        pulse_gate_off(apu, 0);
        return true;
    }
    return false;
}

static bool color_sweep_tick(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    CupidGbSweepTiming *timing = &apu->sweep_timing;
    bool output_changed = false;
    /* Calculation and setup share a 1 MHz clock; restart hold is in the base
     * clock domain. A paused zero-shift calculation retains its partial work. */
    if (apu->clock_phase == 2) {
        if (timing->startup_ticks) {
            if (!--timing->startup_ticks && !timing->shift_ticks && timing->zero_shift_armed) {
                timing->zero_shift_armed = false;
                output_changed |= color_sweep_complete(gb);
            }
        } else if (timing->shift_ticks &&
                   ((gb->io_registers[0x10] & 7) || timing->zero_shift_pending)) {
            if (!--timing->shift_ticks) {
                output_changed |= color_sweep_complete(gb);
            }
        }
    }
    if (timing->restart_hold) {
        --timing->restart_hold;
    }
    return output_changed;
}

static void color_sweep_schedule(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    CupidGbSweepTiming *timing = &apu->sweep_timing;
    uint8_t control = gb->io_registers[0x10];
    if (!(control & 0x70) || apu->ch1_sweep_timer != 1) {
        return;
    }
    if (control & 7) {
        apu->ch1_freq =
            (uint16_t)((apu->ch1_sweep_shadow + timing->delta + ((control & 8) != 0)) & 0x7ff);
    }
    if (!timing->restart_hold) {
        timing->delta = (uint16_t)(apu->ch1_freq >> (control & 7));
    }
    timing->startup_ticks = (uint8_t)(1 + ((apu->clock_phase & 2) == 0));
    timing->shift_ticks = control & 7;
    timing->zero_shift_pending = !(control & 7);
    timing->zero_shift_armed = !(control & 7);
    apu->ch1_sweep_timer = (uint8_t)(((control >> 4) & 7) + 1);
}

static bool color_sweep_write(CupidGb *gb, uint8_t previous, uint8_t value) {
    CupidGbApu *apu = &gb->apu;
    CupidGbSweepTiming *timing = &apu->sweep_timing;
    bool cutoff = false;
    if (timing->startup_ticks == 2) {
        timing->shift_ticks = value & 7;
        if (!timing->shift_ticks) {
            timing->startup_ticks = 0;
        }
    }
    if ((value & 7) && !(previous & 7) && (apu->clock_phase & 2) && timing->shift_ticks > 1) {
        --timing->shift_ticks;
    }
    if (!(value & 8) &&
        apu->ch1_sweep_shadow + timing->completed_delta + ((previous & 8) != 0) > 0x7ff) {
        pulse_gate_off(apu, 0);
        cutoff = true;
    }
    color_sweep_schedule(gb);
    return cutoff;
}

static bool cupid_gb_apu_clock_pulse_length(CupidGbApu *apu, unsigned channel) {
    uint8_t *length = channel ? &apu->ch2_len : &apu->ch1_len;
    if (*length && !--*length) {
        pulse_gate_off(apu, channel);
        return true;
    }
    return false;
}

/**
 * @brief Clocks the length counters for all four channels.
 *
 * Decrements each channel's length counter if length is enabled and
 * the counter is non-zero. If a counter reaches zero, the corresponding
 * channel is disabled.
 *
 * Called on even frame sequencer steps (0, 2, 4, 6) at 256 Hz.
 *
 * @param apu Pointer to the APU state.
 */
static uint8_t cupid_gb_apu_clock_length(CupidGbApu *apu) {
    uint8_t expired = 0;
    if (apu->ch1_len_en && cupid_gb_apu_clock_pulse_length(apu, 0))
        expired |= 1;
    if (apu->ch2_len_en && cupid_gb_apu_clock_pulse_length(apu, 1))
        expired |= 2;
    if (apu->ch3_len_en && apu->ch3_len > 0u) {
        apu->ch3_len = (uint16_t)(apu->ch3_len - 1u);
        if (apu->ch3_len == 0u) {
            apu->ch3_on = false;
            expired |= 4;
        }
    }
    if (apu->ch4_len_en && apu->ch4_len > 0u) {
        apu->ch4_len = (uint8_t)(apu->ch4_len - 1u);
        if (apu->ch4_len == 0u) {
            apu->ch4_on = false;
            expired |= 8;
        }
    }
    return expired;
}

/**
 * @brief Returns whether the next frame sequencer step will clock length counters.
 *
 * Used to implement the "length clock on enable" edge case: when the length
 * counter is enabled mid-frame, an extra length clock may be applied
 * immediately if the next FS step would not normally clock it.
 *
 * @param apu Pointer to the APU state.
 *
 * @return `true` if the upcoming FS step will clock length counters,
 *         `false` otherwise.
 */
static bool cupid_gb_apu_next_step_clocks_length(const CupidGbApu *apu) {
    return (apu->fs_step & 1u) == 0u;
}

static bool pulse_or_wave_length_write_clocks(const CupidGb *gb, bool previous, bool enabled) {
    /* Revisions 0 through B clock a disabled pulse or wave length counter
     * during this phase even when the write leaves length disabled. */
    const bool early = gb->model >= CUPID_GB_MODEL_CGB_0 && gb->model <= CUPID_GB_MODEL_CGB_B;
    return !previous && !cupid_gb_apu_next_step_clocks_length(&gb->apu) && (enabled || early);
}

/**
 * @brief Clocks an 8-bit length counter by one and disables the channel if it expires.
 *
 * @param length     Pointer to the channel's 8-bit length counter.
 * @param channel_on Pointer to the channel's active flag; set to `false` if
 *                   the counter reaches zero.
 */
static bool cupid_gb_apu_clock_length_once_u8(uint8_t *length, bool *channel_on) {
    if (*length > 0u) {
        *length = (uint8_t)(*length - 1u);
        if (*length == 0u) {
            *channel_on = false;
            return true;
        }
    }
    return false;
}

/**
 * @brief Clocks a 16-bit length counter by one and disables the channel if it expires.
 *
 * Used for CH3, which has a 256-step (16-bit) length counter.
 *
 * @param length     Pointer to the channel's 16-bit length counter.
 * @param channel_on Pointer to the channel's active flag; set to `false` if
 *                   the counter reaches zero.
 */
static bool cupid_gb_apu_clock_length_once_u16(uint16_t *length, bool *channel_on) {
    if (*length > 0u) {
        *length = (uint16_t)(*length - 1u);
        if (*length == 0u) {
            *channel_on = false;
            return true;
        }
    }
    return false;
}

/**
 * @brief Calculates the new CH1 frequency after one sweep iteration.
 *
 * Applies the current sweep shift and direction (addition or subtraction)
 * to the sweep shadow register. Also sets the `ch1_sweep_subtracted` flag
 * if a subtraction was performed.
 *
 * @param apu      Pointer to the APU state.
 * @param overflow Output parameter; set to `true` if the resulting frequency
 *                 exceeds 2047 (overflow disables CH1).
 *
 * @return The newly calculated frequency value.
 *
 * @note This function does not apply the result to the channel;
 *       the caller is responsible for updating state if needed.
 */
static uint16_t cupid_gb_apu_calc_sweep_freq(CupidGbApu *apu, bool *overflow) {
    uint16_t delta = (uint16_t)(apu->ch1_sweep_shadow >> apu->ch1_sweep_shift);
    uint16_t new_freq;

    if (apu->ch1_sweep_neg) {
        new_freq = (uint16_t)(apu->ch1_sweep_shadow - delta);
        apu->ch1_sweep_subtracted = true;
    } else {
        new_freq = (uint16_t)(apu->ch1_sweep_shadow + delta);
    }

    *overflow = new_freq > 2047u;
    return new_freq;
}

/**
 * @brief Clocks the CH1 frequency sweep unit.
 *
 * Called on frame sequencer steps 2 and 6 (128 Hz). Decrements the sweep
 * timer and, when it expires, calculates a new frequency. If the new
 * frequency overflows 2047, CH1 is disabled. If the shift is non-zero,
 * the shadow register and NR13/NR14 are also updated, followed by a second
 * overflow check.
 *
 * @param gb Pointer to the Game Boy state.
 */
static uint8_t cupid_gb_apu_clock_sweep(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    if (cupid_gb_model_is_cgb(gb->model)) {
        if (!apu->ch1_sweep_timer) {
            apu->ch1_sweep_timer = 8;
        }
        apu->ch1_sweep_timer = apu->ch1_sweep_timer == 1 ? 8 : apu->ch1_sweep_timer - 1;
        color_sweep_schedule(gb);
        return 0;
    }

    if (!apu->ch1_sweep_en) {
        return 0;
    }
    if (apu->ch1_sweep_timer > 0u) {
        apu->ch1_sweep_timer = (uint8_t)(apu->ch1_sweep_timer - 1u);
    }
    if (apu->ch1_sweep_timer == 0u) {
        uint8_t period = apu->ch1_sweep_period;
        apu->ch1_sweep_timer = (period != 0u) ? period : 8u;
        if (period != 0u) {
            bool overflow = false;
            uint16_t new_freq = cupid_gb_apu_calc_sweep_freq(apu, &overflow);

            if (overflow) {
                pulse_gate_off(apu, 0);
                return 1;
            } else if (apu->ch1_sweep_shift != 0u) {
                bool second_overflow = false;

                apu->ch1_sweep_shadow = new_freq;
                apu->ch1_freq = new_freq;
                gb->io_registers[0x13u] = (uint8_t)(new_freq & 0xffu);
                gb->io_registers[0x14u] =
                    (uint8_t)((gb->io_registers[0x14u] & 0xf8u) | ((new_freq >> 8u) & 0x07u));

                (void)cupid_gb_apu_calc_sweep_freq(apu, &second_overflow);
                if (second_overflow) {
                    pulse_gate_off(apu, 0);
                    return 1;
                }
            }
        }
    }
    return 0;
}

/**
 * @brief Clocks the volume envelopes for CH1, CH2, and CH4.
 *
 * Called on frame sequencer step 7 (64 Hz). For each channel with a non-zero
 * envelope period, decrements the envelope timer and adjusts the volume up
 * or down when the timer expires. Volume is clamped to the range [0, 15].
 *
 * @param apu Pointer to the APU state.
 */
static uint8_t clock_monochrome_envelopes(CupidGbApu *apu) {
    uint8_t changed = 0;
    /* CH1 */
    if (apu->ch1_env_period != 0u) {
        if (apu->ch1_env_timer > 0u) {
            apu->ch1_env_timer = (uint8_t)(apu->ch1_env_timer - 1u);
        }
        if (apu->ch1_env_timer == 0u) {
            apu->ch1_env_timer = apu->ch1_env_period;
            if (apu->ch1_env_add && apu->ch1_vol < 15u) {
                apu->ch1_vol = (uint8_t)(apu->ch1_vol + 1u);
                changed |= apu->ch1_on && !apu->pulse_timing[0].output_blocked ? 1 : 0;
            } else if (!apu->ch1_env_add && apu->ch1_vol > 0u) {
                apu->ch1_vol = (uint8_t)(apu->ch1_vol - 1u);
                changed |= apu->ch1_on && !apu->pulse_timing[0].output_blocked ? 1 : 0;
            }
        }
    }
    /* CH2 */
    if (apu->ch2_env_period != 0u) {
        if (apu->ch2_env_timer > 0u) {
            apu->ch2_env_timer = (uint8_t)(apu->ch2_env_timer - 1u);
        }
        if (apu->ch2_env_timer == 0u) {
            apu->ch2_env_timer = apu->ch2_env_period;
            if (apu->ch2_env_add && apu->ch2_vol < 15u) {
                apu->ch2_vol = (uint8_t)(apu->ch2_vol + 1u);
                changed |= apu->ch2_on && !apu->pulse_timing[1].output_blocked ? 2 : 0;
            } else if (!apu->ch2_env_add && apu->ch2_vol > 0u) {
                apu->ch2_vol = (uint8_t)(apu->ch2_vol - 1u);
                changed |= apu->ch2_on && !apu->pulse_timing[1].output_blocked ? 2 : 0;
            }
        }
    }
    /* CH4 */
    if (apu->ch4_env_period != 0u) {
        if (apu->ch4_env_timer > 0u) {
            apu->ch4_env_timer = (uint8_t)(apu->ch4_env_timer - 1u);
        }
        if (apu->ch4_env_timer == 0u) {
            apu->ch4_env_timer = apu->ch4_env_period;
            if (apu->ch4_env_add && apu->ch4_vol < 15u) {
                apu->ch4_vol = (uint8_t)(apu->ch4_vol + 1u);
                changed |= apu->ch4_on ? 8 : 0;
            } else if (!apu->ch4_env_add && apu->ch4_vol > 0u) {
                apu->ch4_vol = (uint8_t)(apu->ch4_vol - 1u);
                changed |= apu->ch4_on ? 8 : 0;
            }
        }
    }
    return changed;
}

void cupid_gb_apu_div_event(CupidGb *gb) {
    if (!gb)
        return;
    cupid_gb_apu_run(gb, true);
    gb->apu.pcm_read_inhibit = 0;
    if (!gb->apu.apu_on) {
        return;
    }

    CupidGbApu *apu = &gb->apu;
    /* A high DIV input at power-on inhibits one edge. The following edge starts
     * phase zero. Retain the three serialized gate states for native states. */
    static const uint8_t gate_next[] = {0, 2, 0};
    unsigned gate = apu->fs_skip_div_event;
    apu->fs_skip_div_event = gate_next[gate];
    if (gate == 1) {
        return;
    }

    unsigned phase = gate == 2 ? 0 : apu->fs_step;
    const bool color = cupid_gb_model_is_cgb(gb->model);
    if (color) {
        if (phase == 6) {
            if (!apu->pulse_timing[0].envelope.clock) {
                apu->ch1_env_timer = (uint8_t)((apu->ch1_env_timer - 1u) & 7u);
            }
            if (!apu->pulse_timing[1].envelope.clock) {
                apu->ch2_env_timer = (uint8_t)((apu->ch2_env_timer - 1u) & 7u);
            }
            if (!apu->noise_timing.envelope.clock) {
                apu->ch4_env_timer = (uint8_t)((apu->ch4_env_timer - 1u) & 7u);
            }
        }
        if (gb->double_speed && !cupid_gb_model_is_early_cgb(gb->model)) {
            apu->envelope_delay = 2;
        } else {
            cupid_gb_audio_update_channels(gb, color_envelope_fall(gb), 0, false);
        }
    }
    /* Bit 0 clocks length, bit 1 sweep, and bit 2 envelopes. */
    static const uint8_t clocks[] = {1, 0, 3, 0, 1, 0, 3, 4};
    unsigned edges = clocks[phase];
    apu->fs_step = (uint8_t)((phase + 1) & 7);
    /* Each real event owns its output update. Preserve their order: an envelope
     * can publish a nonzero code before length expiry returns it to zero. Host
     * reconstruction may retain a different routing after a live state load. */
    if (edges & 1) {
        cupid_gb_audio_update_channels(gb, cupid_gb_apu_clock_length(apu), 0, false);
    }
    if (edges & 2) {
        cupid_gb_audio_update_channels(gb, cupid_gb_apu_clock_sweep(gb), 0, false);
    }
    if (!color && (edges & 4)) {
        cupid_gb_audio_update_channels(gb, clock_monochrome_envelopes(apu), 0, false);
    }
}

static void cupid_gb_apu_digital_samples(const CupidGb *gb, uint8_t digital[4]) {
    const CupidGbApu *apu = &gb->apu;
    memset(digital, 0, 4);
    if (apu->ch1_on) {
        bool high = apu->pulse_timing[0].output_high && !apu->pulse_timing[0].output_blocked;
        digital[0] = high ? apu->ch1_vol : 0;
    }
    if (apu->ch2_on) {
        bool high = apu->pulse_timing[1].output_high && !apu->pulse_timing[1].output_blocked;
        digital[1] = high ? apu->ch2_vol : 0;
    }
    if (apu->ch3_on && apu->ch3_out_level) {
        digital[2] = (uint8_t)(apu->ch3_sample >> (apu->ch3_out_level - 1u));
    }
    if (apu->ch4_on) {
        digital[3] = apu->noise_timing.output_high ? apu->ch4_vol : 0;
    }
}

void cupid_gb_apu_channel_samples(const CupidGb *gb, uint8_t samples[4]) {
    cupid_gb_apu_digital_samples(gb, samples);
    for (unsigned channel = 0; channel < 2; ++channel) {
        const bool active = channel ? gb->apu.ch2_on : gb->apu.ch1_on;
        if (!active && !gb->apu.pulse_timing[channel].clock_started)
            samples[channel] = gb->apu.pulse_dac_hold[channel];
    }
}

uint8_t cupid_gb_apu_read_pcm12(const CupidGb *gb) {
    uint8_t digital[4];
    if (!gb || !cupid_gb_model_is_cgb(gb->model)) {
        return 0xff;
    }
    cupid_gb_apu_digital_samples(gb, digital);
    return (uint8_t)((digital[0] | (digital[1] << 4)) & ~gb->apu.pcm_read_inhibit);
}

uint8_t cupid_gb_apu_read_pcm34(const CupidGb *gb) {
    uint8_t digital[4];
    if (!gb || !cupid_gb_model_is_cgb(gb->model)) {
        return 0xff;
    }
    cupid_gb_apu_digital_samples(gb, digital);
    return (uint8_t)((digital[2] | (digital[3] << 4)) & ~(gb->apu.pcm_read_inhibit >> 8));
}

/**
 * @brief Triggers (restarts) CH1 (pulse + sweep).
 *
 * Reloads the frequency timer, volume, envelope, and sweep unit from the
 * current NR10–NR14 register values. If the length counter is zero it is
 * reloaded to 64. Performs an immediate overflow check on the sweep unit;
 * if overflow is detected CH1 is disabled immediately.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_apu_trigger_ch1(CupidGb *gb, uint16_t previous_frequency, bool was_active) {
    CupidGbApu *apu = &gb->apu;
    uint8_t nr10 = gb->io_registers[0x10u];
    uint8_t nr12 = gb->io_registers[0x12u];
    uint8_t nr13 = gb->io_registers[0x13u];
    uint8_t nr14 = gb->io_registers[0x14u];

    apu->ch1_dac = (nr12 & 0xf8u) != 0u;
    if (apu->ch1_len == 0u) {
        apu->ch1_len = 64u;
    }

    if (!cupid_gb_model_is_cgb(gb->model)) {
        apu->ch1_freq = (uint16_t)(((uint16_t)(nr14 & 0x07u) << 8u) | (uint16_t)nr13);
    }
    uint16_t previous_timer = apu->ch1_timer;
    apu->ch1_timer = (uint16_t)((2048u - (uint32_t)apu->ch1_freq) * 4u);
    if (cupid_gb_model_is_cgb(gb->model)) {
        color_pulse_trigger(gb, 0, was_active, previous_timer, previous_frequency);
    } else {
        monochrome_pulse_trigger(apu, 0, was_active);
    }
    apu->ch1_vol = (uint8_t)((nr12 >> 4u) & 0x0fu);
    apu->ch1_env_add = (nr12 & 0x08u) != 0u;
    apu->ch1_env_period = (uint8_t)(nr12 & 0x07u);
    apu->ch1_env_timer = (apu->ch1_env_period != 0u) ? apu->ch1_env_period : 8u;
    if (cupid_gb_model_is_cgb(gb->model)) {
        apu->ch1_env_timer &= 7;
    }
    apu->ch1_sweep_shadow = apu->ch1_freq;
    apu->ch1_sweep_subtracted = false;

    apu->ch1_sweep_period = (uint8_t)((nr10 >> 4u) & 0x07u);
    apu->ch1_sweep_neg = (nr10 & 0x08u) != 0u;
    apu->ch1_sweep_shift = (uint8_t)(nr10 & 0x07u);
    apu->ch1_sweep_en = (apu->ch1_sweep_period != 0u || apu->ch1_sweep_shift != 0u);
    apu->ch1_sweep_timer = (apu->ch1_sweep_period != 0u) ? apu->ch1_sweep_period : 8u;
    if (cupid_gb_model_is_cgb(gb->model)) {
        CupidGbSweepTiming *timing = &apu->sweep_timing;
        memset(timing, 0, sizeof(*timing));
        apu->ch1_sweep_shadow = 0;
        apu->ch1_sweep_timer = (uint8_t)(apu->ch1_sweep_period + 1);
        if (nr10 & 7) {
            timing->startup_ticks = (uint8_t)(was_active ? 2 : 3);
            timing->shift_ticks = nr10 & 7;
            timing->delta = (uint16_t)(apu->ch1_freq >> (nr10 & 7));
        }
        timing->restart_hold = (uint8_t)(6 + (apu->clock_phase & 2));
    }

    if (apu->ch1_dac) {
        apu->ch1_on = true;
    }

    /* Trigger-time overflow check */
    if (!cupid_gb_model_is_cgb(gb->model) && apu->ch1_sweep_shift != 0u) {
        bool overflow = false;

        (void)cupid_gb_apu_calc_sweep_freq(apu, &overflow);
        if (overflow) {
            pulse_gate_off(apu, 0);
        }
    }
}

/**
 * @brief Triggers (restarts) CH2 (pulse).
 *
 * Reloads the frequency timer, volume, and envelope from the current
 * NR21–NR24 register values. If the length counter is zero it is
 * reloaded to 64.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_apu_trigger_ch2(CupidGb *gb, uint16_t previous_frequency, bool was_active) {
    CupidGbApu *apu = &gb->apu;
    uint8_t nr22 = gb->io_registers[0x17u];
    uint8_t nr23 = gb->io_registers[0x18u];
    uint8_t nr24 = gb->io_registers[0x19u];

    apu->ch2_dac = (nr22 & 0xf8u) != 0u;
    if (apu->ch2_len == 0u) {
        apu->ch2_len = 64u;
    }

    apu->ch2_freq = (uint16_t)(((uint16_t)(nr24 & 0x07u) << 8u) | (uint16_t)nr23);
    uint16_t previous_timer = apu->ch2_timer;
    apu->ch2_timer = (uint16_t)((2048u - (uint32_t)apu->ch2_freq) * 4u);
    if (cupid_gb_model_is_cgb(gb->model)) {
        color_pulse_trigger(gb, 1, was_active, previous_timer, previous_frequency);
    } else {
        monochrome_pulse_trigger(apu, 1, was_active);
    }
    apu->ch2_vol = (uint8_t)((nr22 >> 4u) & 0x0fu);
    apu->ch2_env_add = (nr22 & 0x08u) != 0u;
    apu->ch2_env_period = (uint8_t)(nr22 & 0x07u);
    apu->ch2_env_timer = (apu->ch2_env_period != 0u) ? apu->ch2_env_period : 8u;
    if (cupid_gb_model_is_cgb(gb->model)) {
        apu->ch2_env_timer &= 7;
    }

    if (apu->ch2_dac) {
        apu->ch2_on = true;
    }
}

/**
 * @brief Triggers (restarts) CH3 (wave).
 *
 * Reloads the frequency timer and resets the wave position. An enabled DAC
 * selects the high nibble of the retained fetch byte, independently of live
 * wave RAM. An exhausted length reloads to 256. The initial timer includes
 * a 6 T-cycle startup delay.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_apu_trigger_ch3(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    uint8_t nr33 = gb->io_registers[0x1du];
    uint8_t nr34 = gb->io_registers[0x1eu];
    uint16_t period;

    apu->ch3_dac = (gb->io_registers[0x1au] & 0x80u) != 0u;
    if (apu->ch3_len == 0u) {
        apu->ch3_len = 256u;
    }

    apu->ch3_freq = (uint16_t)(((uint16_t)(nr34 & 0x07u) << 8u) | (uint16_t)nr33);
    apu->ch3_active_freq = apu->ch3_freq;
    period = (uint16_t)((2048u - (uint32_t)apu->ch3_freq) * 2u);
    if (period == 0u) {
        period = 2u;
    }
    apu->ch3_timer = (uint16_t)(period + 6u);
    apu->ch3_wave_pos = 0u;
    apu->ch3_current_byte = 0u;
    apu->ch3_started = false;
    apu->ch3_wave_access_ticks = 0u;
    apu->ch3_freq_pending = false;

    if (apu->ch3_dac) {
        apu->ch3_sample = (uint8_t)(apu->ch3_sample_byte >> 4u);
        apu->ch3_on = true;
    }
}

/**
 * @brief Applies monochrome wave-memory corruption at an active CH3 retrigger.
 *
 * The final two T-cycles before a fetch expose the upcoming byte's block.
 * Corruption can change that source block as well as the first four bytes.
 * Pocket retains block zero; the other monochrome models copy one byte there.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note This function has no effect on CGB hardware or when CH3 is inactive
 *       or its timer is more than 2 T-cycles from expiry.
 */
static void cupid_gb_apu_apply_dmg_ch3_retrigger_bug(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    uint8_t offset;

    if ((cupid_gb_model_is_cgb(gb->model)) || !apu->ch3_on || apu->ch3_timer > 2u) {
        return;
    }

    offset = (uint8_t)(((apu->ch3_wave_pos + 1u) >> 1u) & 0x0fu);
    uint8_t *wave = &gb->io_registers[0x30u];
    if (offset < 4u) {
        /* This exception belongs to Pocket specifically, not SGB2. */
        if (gb->model != CUPID_GB_MODEL_MGB)
            wave[0] = wave[offset];
        return;
    }

    const uint8_t block = (uint8_t)(offset & 0xfcu);
    /* A source bit flips only when both the first byte and the upcoming byte
     * disagree with its old value. Read all operands before replacing it. */
    const uint8_t flipped = (uint8_t)((wave[block] ^ wave[0]) & (wave[block] ^ wave[offset]));
    wave[block] ^= flipped;
    memcpy(wave, wave + block, 4u);
}

/**
 * @brief Triggers (restarts) CH4 (noise).
 *
 * Reloads the volume, envelope, LFSR, and noise timer from the current
 * NR41–NR44 register values. If the length counter is zero it is reloaded
 * to 64. The LFSR is reset to 0x7FFF.
 *
 * @param gb Pointer to the Game Boy state.
 */
static bool cupid_gb_apu_trigger_ch4(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    if (!cupid_gb_model_is_cgb(gb->model) && (apu->noise_timing.clock_phase >> 1) != 0) {
        /* A monochrome trigger must cross the 512 kHz alignment gate. The
         * twelve-clock delay leaves existing sound running until the restart. */
        apu->noise_timing.start_delay = 12;
        return false;
    }
    apu->noise_timing.start_delay = 0;
    uint8_t nr42 = gb->io_registers[0x21u];
    uint8_t nr43 = gb->io_registers[0x22u];
    uint32_t period;

    apu->ch4_dac = (nr42 & 0xf8u) != 0u;
    if (apu->ch4_len == 0u) {
        apu->ch4_len = 64u;
    }

    apu->ch4_vol = (uint8_t)((nr42 >> 4u) & 0x0fu);
    apu->ch4_env_add = (nr42 & 0x08u) != 0u;
    apu->ch4_env_period = (uint8_t)(nr42 & 0x07u);
    apu->ch4_env_timer = (apu->ch4_env_period != 0u) ? apu->ch4_env_period : 8u;
    apu->noise_timing.envelope = (CupidGbEnvelopeGate){0};
    if (cupid_gb_model_is_cgb(gb->model))
        apu->ch4_env_timer &= 7u;

    apu->ch4_div_code = (uint8_t)(nr43 & 0x07u);
    apu->ch4_shift = (uint8_t)((nr43 >> 4u) & 0x0fu);
    apu->ch4_width7 = (nr43 & 0x08u) != 0u;

    period = (uint32_t)cupid_gb_noise_div[apu->ch4_div_code];
    period = period << apu->ch4_shift;
    apu->ch4_timer = (period > 0u) ? period : 8u;
    apu->ch4_lfsr = 0x7fffu;

    cupid_gb_noise_prepare(gb, apu->ch4_on);

    if (apu->ch4_dac) {
        apu->ch4_on = true;
    }
    return true;
}

/* The hardware portion precedes output. Resetting it leaves generated host
 * samples, reconstruction responses, and coupling capacitors continuous.
 * Wave RAM is a separate memory array and survives both power transitions. */
static void cupid_gb_apu_power(CupidGb *gb, bool enabled) {
    CupidGbApu *apu = &gb->apu;
    if (enabled == apu->apu_on) {
        return;
    }

    uint16_t lengths[4] = {0};
    if (enabled && !cupid_gb_model_is_cgb(gb->model)) {
        lengths[0] = apu->ch1_len;
        lengths[1] = apu->ch2_len;
        lengths[2] = apu->ch3_len;
        lengths[3] = apu->ch4_len;
    }
    if (!enabled)
        for (unsigned channel = 0; channel < 4; ++channel)
            cupid_gb_audio_update(gb, channel, 0, 0, false);
    memset(apu, 0, offsetof(CupidGbApu, output));
    apu->apu_on = enabled;
    apu->pulse_timing[0].edge_age = apu->pulse_timing[1].edge_age = 4;
    apu->ch1_len = (uint8_t)lengths[0];
    apu->ch2_len = (uint8_t)lengths[1];
    apu->ch3_len = lengths[2];
    apu->ch4_len = (uint8_t)lengths[3];

    if (enabled) {
        /* DIV bit 12 clocks audio, or bit 13 at double CPU speed. The visible
         * divider byte supplies the same input without advancing the clock. */
        bool high = (gb->io_registers[0x04] >> (4 + gb->double_speed)) & 1;
        apu->fs_step = high ? 1 : 0;
        apu->fs_skip_div_event = high ? 1 : 0;
    } else {
        memset(gb->io_registers + 0x10, 0, 0x20);
    }
}

/**
 * @brief Handles side-effects of APU register writes.
 *
 * Should be called immediately after an I/O register in the APU range
 * (0xFF10–0xFF3F) is written. Updates internal APU state to reflect the
 * new register value, including:
 *   - Wave RAM writes (with CH3 access conflict handling)
 *   - NR52 APU power on/off (clears or resets all channel state)
 *   - Length counter writes while APU is off (DMG/CGB differences)
 *   - Per-channel register writes (duty, envelope, frequency, trigger)
 *
 * @param gb  Pointer to the Game Boy state.
 * @param off I/O register offset relative to 0xFF00 (e.g. 0x11 for NR11 at 0xFF11).
 * @param val The value being written to the register.
 */
void cupid_gb_apu_on_write(CupidGb *gb, uint8_t off, uint8_t val) {
    cupid_gb_apu_run(gb, true);
    CupidGbApu *apu = &gb->apu;
    const bool disconnected = cupid_gb_audio_dac_mask(gb) == 0;

    if (off >= 0x30u && off <= 0x3fu) {
        if (apu->apu_on && apu->ch3_on) {
            if (!cupid_gb_model_is_cgb(gb->model)) {
                if (apu->ch3_wave_access_ticks == 0u) {
                    return;
                }
            }
            gb->io_registers[0x30u + apu->ch3_current_byte] = val;
        } else {
            gb->io_registers[off] = val;
        }
        return;
    }

    if (off == 0x26u) {
        cupid_gb_apu_power(gb, (val & 0x80u) != 0);
        gb->io_registers[off] = (uint8_t)(0x70u | (apu->apu_on ? 0x80u : 0));
        return;
    }

    if (!apu->apu_on) {
        if (!cupid_gb_model_is_cgb(gb->model)) {
            if (off == 0x11u) {
                apu->ch1_len = (uint8_t)(64u - (val & 0x3fu));
            } else if (off == 0x16u) {
                apu->ch2_len = (uint8_t)(64u - (val & 0x3fu));
            } else if (off == 0x1bu) {
                apu->ch3_len = (uint16_t)(256u - (uint16_t)val);
            } else if (off == 0x20u) {
                gb->io_registers[off] = val;
                apu->ch4_len = (uint8_t)(64u - (val & 0x3fu));
            }
        }
        return;
    }

    uint8_t previous = gb->io_registers[off];
    gb->io_registers[off] = val;
    uint8_t changed = 0;

    switch (off) {
        // CH1
        case 0x10u: // NR10 sweep
        {
            bool old_neg = apu->ch1_sweep_neg;

            apu->ch1_sweep_period = (uint8_t)((val >> 4u) & 0x07u);
            apu->ch1_sweep_neg = (val & 0x08u) != 0u;
            apu->ch1_sweep_shift = (uint8_t)(val & 0x07u);
            if (cupid_gb_model_is_cgb(gb->model)) {
                changed = color_sweep_write(gb, previous, val) ? 1 : 0;
            } else if (old_neg && !apu->ch1_sweep_neg && apu->ch1_sweep_subtracted) {
                pulse_gate_off(apu, 0);
                changed = 1;
            }
            break;
        }
        case 0x11u: // NR11 length/duty
            apu->ch1_duty = (uint8_t)((val >> 6u) & 0x03u);
            apu->ch1_len = (uint8_t)(64u - (val & 0x3fu));
            break;
        case 0x12u: // NR12 envelope
            changed =
                !(val & 0xf8u) || (apu->ch1_on && !apu->pulse_timing[0].output_blocked) ? 1 : 0;
            if (cupid_gb_model_is_cgb(gb->model)) {
                if (apu->ch1_on && (val & 0xf8)) {
                    color_pulse_envelope_write(gb, 0, previous, val);
                }
                apu->ch1_env_period = val & 7;
                apu->ch1_env_add = (val & 8) != 0;
            }
            if (val & 0xf8u)
                apu->ch1_dac = true;
            else
                pulse_disconnect_dac(apu, 0);
            break;
        case 0x13u: // NR13 freq low
            apu->ch1_freq = (uint16_t)((apu->ch1_freq & 0x700u) | (uint16_t)val);
            pulse_frequency_reload(apu, 0);
            break;
        case 0x14u: // NR14 freq high + trigger
        {
            /* Trigger timing samples activity before this write's extra length
             * clock. A simultaneous reload must not become an inactive start. */
            const bool was_active = apu->ch1_on;
            bool old_len_en;
            bool trigger;
            bool length_reloaded;
            uint16_t old_frequency = apu->ch1_freq;

            apu->ch1_freq = (uint16_t)((apu->ch1_freq & 0x00ffu) | ((uint32_t)(val & 0x07u) << 8u));
            old_len_en = apu->ch1_len_en;
            apu->ch1_len_en = (val & 0x40u) != 0u;
            trigger = (val & 0x80u) != 0u;
            if (cupid_gb_model_is_cgb(gb->model)) {
                color_pulse_high_write(apu, 0, old_frequency, trigger);
            }
            pulse_frequency_reload(apu, 0);

            if (pulse_or_wave_length_write_clocks(gb, old_len_en, apu->ch1_len_en)) {
                if (cupid_gb_apu_clock_pulse_length(apu, 0) && !trigger)
                    changed = 1;
            }

            length_reloaded = apu->ch1_len == 0u;
            if (trigger) {
                cupid_gb_apu_trigger_ch1(gb, old_frequency, was_active);
                if (apu->ch1_dac && (!was_active || !apu->pulse_timing[0].output_blocked))
                    changed = 1;
                if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch1_len_en &&
                    length_reloaded) {
                    cupid_gb_apu_clock_pulse_length(apu, 0);
                }
            }
            break;
        }
        // CH2
        case 0x16u: // NR21
            apu->ch2_duty = (uint8_t)((val >> 6u) & 0x03u);
            apu->ch2_len = (uint8_t)(64u - (val & 0x3fu));
            break;
        case 0x17u: // NR22
            changed =
                !(val & 0xf8u) || (apu->ch2_on && !apu->pulse_timing[1].output_blocked) ? 2 : 0;
            if (cupid_gb_model_is_cgb(gb->model)) {
                if (apu->ch2_on && (val & 0xf8)) {
                    color_pulse_envelope_write(gb, 1, previous, val);
                }
                apu->ch2_env_period = val & 7;
                apu->ch2_env_add = (val & 8) != 0;
            }
            if (val & 0xf8u)
                apu->ch2_dac = true;
            else
                pulse_disconnect_dac(apu, 1);
            break;
        case 0x18u: // NR23
            apu->ch2_freq = (uint16_t)((apu->ch2_freq & 0x700u) | (uint16_t)val);
            pulse_frequency_reload(apu, 1);
            break;
        case 0x19u: // NR24
        {
            const bool was_active = apu->ch2_on;
            bool old_len_en;
            bool trigger;
            bool length_reloaded;
            uint16_t old_frequency = apu->ch2_freq;

            apu->ch2_freq = (uint16_t)((apu->ch2_freq & 0x00ffu) | ((uint32_t)(val & 0x07u) << 8u));
            old_len_en = apu->ch2_len_en;
            apu->ch2_len_en = (val & 0x40u) != 0u;
            trigger = (val & 0x80u) != 0u;
            if (cupid_gb_model_is_cgb(gb->model)) {
                color_pulse_high_write(apu, 1, old_frequency, trigger);
            }
            pulse_frequency_reload(apu, 1);

            if (pulse_or_wave_length_write_clocks(gb, old_len_en, apu->ch2_len_en)) {
                if (cupid_gb_apu_clock_pulse_length(apu, 1) && !trigger)
                    changed = 2;
            }

            length_reloaded = apu->ch2_len == 0u;
            if (trigger) {
                cupid_gb_apu_trigger_ch2(gb, old_frequency, was_active);
                if (apu->ch2_dac && (!was_active || !apu->pulse_timing[1].output_blocked))
                    changed = 2;
                if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch2_len_en &&
                    length_reloaded) {
                    cupid_gb_apu_clock_pulse_length(apu, 1);
                }
            }
            break;
        }
        // CH3
        case 0x1au: // NR30 DAC power
            apu->ch3_dac = (val & 0x80u) != 0u;
            if (!apu->ch3_dac) {
                if (apu->ch3_on) {
                    /* Disconnect can replace the fetch byte before silencing
                     * CH3. At the upcoming fetch boundary, the executing PC
                     * selects the byte. A completed monochrome fetch instead
                     * exposes NR30's low address bits. Keep RAM and the old
                     * output nibble intact. */
                    if (apu->ch3_timer <= 2u)
                        apu->ch3_sample_byte = gb->io_registers[0x30u + (gb->cpu.pc & 0x0fu)];
                    else if (!cupid_gb_model_is_cgb(gb->model) && apu->ch3_wave_access_ticks)
                        apu->ch3_sample_byte = gb->io_registers[0x3au];
                }
                apu->ch3_on = false;
                changed = 4;
            }
            break;
        case 0x1bu: // NR31 length
            apu->ch3_len = (uint16_t)(256u - (uint16_t)val);
            break;
        case 0x1cu: // NR32 output level
            apu->ch3_out_level = (uint8_t)((val >> 5u) & 0x03u);
            changed = apu->ch3_on ? 4 : 0;
            break;
        case 0x1du: // NR33
            apu->ch3_freq = (uint16_t)((apu->ch3_freq & 0x700u) | (uint16_t)val);
            if (apu->ch3_on) {
                if (cupid_gb_model_is_cgb(gb->model)) {
                    apu->ch3_freq_pending = true;
                } else {
                    apu->ch3_active_freq = apu->ch3_freq;
                    apu->ch3_freq_pending = false;
                }
            } else {
                apu->ch3_active_freq = apu->ch3_freq;
            }
            break;
        case 0x1eu: // NR34
        {
            bool old_len_en;
            bool trigger;
            bool length_reloaded;

            apu->ch3_freq = (uint16_t)((apu->ch3_freq & 0x00ffu) | ((uint32_t)(val & 0x07u) << 8u));
            old_len_en = apu->ch3_len_en;
            apu->ch3_len_en = (val & 0x40u) != 0u;
            trigger = (val & 0x80u) != 0u;

            /* Retrigger sees the active channel before length enabling can
             * expire it. Keep the subsequent length reload accounting intact. */
            if (trigger) {
                cupid_gb_apu_apply_dmg_ch3_retrigger_bug(gb);
                /* A fetch-boundary trigger reloads the byte after any RAM
                 * corruption, while the pre-write channel is still active. */
                if (apu->ch3_on && apu->ch3_timer <= 2u)
                    apu->ch3_sample_byte = gb->io_registers[0x30u];
            }

            if (pulse_or_wave_length_write_clocks(gb, old_len_en, apu->ch3_len_en)) {
                if (cupid_gb_apu_clock_length_once_u16(&apu->ch3_len, &apu->ch3_on) && !trigger)
                    changed = 4;
            }

            length_reloaded = apu->ch3_len == 0u;
            if (trigger) {
                cupid_gb_apu_trigger_ch3(gb);
                changed = apu->ch3_dac ? 4 : 0;
                if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch3_len_en &&
                    length_reloaded) {
                    cupid_gb_apu_clock_length_once_u16(&apu->ch3_len, &apu->ch3_on);
                }
            } else if (apu->ch3_on) {
                if (cupid_gb_model_is_cgb(gb->model)) {
                    apu->ch3_freq_pending = true;
                } else {
                    apu->ch3_active_freq = apu->ch3_freq;
                    apu->ch3_freq_pending = false;
                }
            } else {
                apu->ch3_active_freq = apu->ch3_freq;
            }
            break;
        }
        // CH4
        case 0x20u: // NR41
            apu->ch4_len = (uint8_t)(64u - (val & 0x3fu));
            break;
        case 0x21u: // NR42
            changed = !(val & 0xf8u) || apu->ch4_on ? 8 : 0;
            apu->ch4_dac = (val & 0xf8u) != 0u;
            if (!apu->ch4_dac) {
                cupid_gb_noise_disconnect(gb);
            } else if (apu->ch4_on && cupid_gb_model_is_cgb(gb->model)) {
                color_envelope_write(gb->model, &apu->noise_timing.envelope, &apu->ch4_vol,
                                     &apu->ch4_env_timer, previous, val);
            }
            break;
        case 0x22u: // NR43
            apu->ch4_div_code = (uint8_t)(val & 0x07u);
            apu->ch4_shift = (uint8_t)((val >> 4u) & 0x0fu);
            apu->ch4_width7 = (val & 0x08u) != 0u;
            cupid_gb_noise_frequency_write(gb, previous);
            break;
        case 0x23u: // NR44
        {
            bool old_len_en;
            bool trigger;
            bool length_reloaded;

            old_len_en = apu->ch4_len_en;
            apu->ch4_len_en = (val & 0x40u) != 0u;
            trigger = (val & 0x80u) != 0u;

            if (!cupid_gb_apu_next_step_clocks_length(apu) && !old_len_en && apu->ch4_len_en) {
                if (cupid_gb_apu_clock_length_once_u8(&apu->ch4_len, &apu->ch4_on) && !trigger)
                    changed = 8;
            }

            length_reloaded = apu->ch4_len == 0u;
            if (trigger) {
                const bool started = cupid_gb_apu_trigger_ch4(gb);
                if (started && apu->ch4_dac)
                    changed = 8;
                if (started && !cupid_gb_apu_next_step_clocks_length(apu) && apu->ch4_len_en &&
                    length_reloaded) {
                    cupid_gb_apu_clock_length_once_u8(&apu->ch4_len, &apu->ch4_on);
                }
            }
            break;
        }
        default:
            break;
    }
    if (off == 0x24 || off == 0x25) {
        cupid_gb_audio_update_channels(gb, 15, 0, true);
    } else if (changed) {
        cupid_gb_audio_update_channels(gb, changed, 0, false);
    }
    /* Reconnecting the first monochrome DAC restores its full voltage against
     * the held capacitor; an attack ramp here would create a false transient. */
    if (disconnected && !cupid_gb_model_is_cgb(gb->model) &&
        (off == 0x12 || off == 0x17 || off == 0x1a || off == 0x21)) {
        const uint8_t enabled = cupid_gb_audio_dac_mask(gb);
        for (unsigned channel = 0; channel < 4; ++channel)
            if (enabled & (1u << channel))
                apu->output.dac_charge[channel] = 1;
    }
}

/* The digital engine retains base-clock ordering. Each changed channel adds a
 * step at its exact clock offset; host reconstruction runs at sample boundaries. */
static void execute_audio_cycles(CupidGb *gb, uint16_t cycles, bool run_end) {
    CupidGbApu *apu = &gb->apu;
    const bool color = cupid_gb_model_is_cgb(gb->model);
    /* Earlier color revisions latch a held zero only on the final oscillator
     * step of an audio run. Observe it at the step, independently of output. */
    const unsigned pcm_tick =
        run_end && cupid_gb_model_is_early_cgb(gb->model) ? cycles - 1u : UINT16_MAX;
    const unsigned noise_pcm_tick = gb->double_speed ? UINT16_MAX : pcm_tick;
    uint16_t c;
    for (c = 0u; c < cycles; ++c) {
        uint8_t changed = 0;
        bool envelope_due = false;
        bool noise_restart_due = false;
        bool oscillator_running = !gb->cpu.stopped || color;
        if (oscillator_running && apu->ch3_wave_access_ticks > 0u) {
            apu->ch3_wave_access_ticks = (uint8_t)(apu->ch3_wave_access_ticks - 1u);
        }

        if (apu->apu_on && oscillator_running) {
            apu->clock_phase = (uint8_t)((apu->clock_phase + 1u) & 3u);
            if (color) {
                if (color_sweep_tick(gb))
                    changed |= 1;
                if (pulse_tick(apu, 0, true, c == pcm_tick))
                    changed |= 1;
                if (pulse_tick(apu, 1, true, c == pcm_tick))
                    changed |= 2;
                if (apu->envelope_delay && !--apu->envelope_delay) {
                    envelope_due = true;
                }
            } else {
                if (pulse_tick(apu, 0, false, false))
                    changed |= 1;
                if (pulse_tick(apu, 1, false, false))
                    changed |= 2;
            }

            // CH3 wave timer
            if (apu->ch3_on) {
                if (apu->ch3_timer > 0u) {
                    apu->ch3_timer = (uint16_t)(apu->ch3_timer - 1u);
                }
                if (apu->ch3_timer == 0u) {
                    uint16_t period;

                    if (!apu->ch3_started) {
                        apu->ch3_wave_pos = 1u;
                        apu->ch3_started = true;
                    } else {
                        apu->ch3_wave_pos = (uint8_t)((apu->ch3_wave_pos + 1u) & 31u);
                    }
                    apu->ch3_current_byte = (uint8_t)(apu->ch3_wave_pos >> 1u);
                    {
                        /* Each nibble clock fetches a complete byte. CPU wave
                         * writes change RAM without replacing this held value. */
                        apu->ch3_sample_byte = gb->io_registers[0x30u + apu->ch3_current_byte];
                        if ((apu->ch3_wave_pos & 1u) == 0u) {
                            apu->ch3_sample = (uint8_t)(apu->ch3_sample_byte >> 4u);
                        } else {
                            apu->ch3_sample = (uint8_t)(apu->ch3_sample_byte & 0x0fu);
                        }
                    }
                    apu->ch3_wave_access_ticks = 1u;
                    if (apu->ch3_freq_pending) {
                        apu->ch3_active_freq = apu->ch3_freq;
                        apu->ch3_freq_pending = false;
                    }
                    period = (uint16_t)((2048u - (uint32_t)apu->ch3_active_freq) * 2u);
                    if (period == 0u) {
                        period = 2u;
                    }
                    apu->ch3_timer = period;
                    changed |= 4;
                }
            }

            if (cupid_gb_noise_tick(gb, c == noise_pcm_tick))
                changed |= 8;
            if (apu->noise_timing.start_delay && !--apu->noise_timing.start_delay)
                noise_restart_due = true;
        }

        if (changed)
            cupid_gb_audio_update_channels(gb, changed, (unsigned)c + 1u, false);
        /* Oscillator edges precede delayed restarts and envelope steps, even
         * on the same clock. Combining their final code can suppress the
         * routing update after a live state restore. */
        if (noise_restart_due) {
            const bool empty_length = apu->ch4_len == 0;
            if (cupid_gb_apu_trigger_ch4(gb)) {
                if (empty_length && apu->ch4_len_en && !cupid_gb_apu_next_step_clocks_length(apu))
                    cupid_gb_apu_clock_length_once_u8(&apu->ch4_len, &apu->ch4_on);
                if (apu->ch4_dac)
                    cupid_gb_audio_update_channels(gb, 8, (unsigned)c + 1u, false);
            }
        }
        if (envelope_due)
            cupid_gb_audio_update_channels(gb, color_envelope_fall(gb), (unsigned)c + 1u, false);
    }
    cupid_gb_audio_advance(gb, cycles);
}

static bool output_needs_run(const CupidGb *gb) {
    const CupidGbApu *apu = &gb->apu;
    return (unsigned)apu->pending_cycles + apu->output.render_cycles >= apu->output.max_span ||
           apu->output.sample_acc >= cupid_gb_base_clock_hz(gb) ||
           apu->sweep_timing.startup_ticks || apu->sweep_timing.shift_ticks ||
           apu->sweep_timing.restart_hold || apu->ch3_on || apu->ch3_wave_access_ticks;
}

static void run_pending_audio(CupidGb *gb, bool force, bool direct) {
    CupidGbApu *apu = &gb->apu;
    CupidGbAudioOutput *output = &apu->output;
    cupid_gb_audio_configure(gb);
    if (!force && !output_needs_run(gb))
        return;
    const uint32_t clock = cupid_gb_base_clock_hz(gb);
    unsigned remaining = apu->pending_cycles;
    apu->pending_cycles = 0;
    const uint8_t envelope_delay = apu->envelope_delay;
    if (!direct)
        apu->envelope_delay = 0;
    if (!remaining) {
        /* Monochrome STOP can supply host credit without a digital APU clock. */
        while (output->sample_acc >= clock) {
            output->sample_acc -= clock;
            cupid_gb_audio_render(gb);
        }
    }
    while (remaining) {
        unsigned available =
            output->render_cycles < output->max_span ? output->max_span - output->render_cycles : 0;
        if (!available) {
            if (output->sample_acc >= clock) {
                output->sample_acc -= clock;
                cupid_gb_audio_render(gb);
            }
            available = output->max_span;
        }
        const unsigned span = remaining < available ? remaining : available;
        execute_audio_cycles(gb, (uint16_t)span, span == remaining);
        remaining -= span;
        if (output->sample_acc >= clock) {
            output->sample_acc -= clock;
            cupid_gb_audio_render(gb);
        }
        /* A long frozen interval can owe several samples. Rounded two-clock
         * spans may each advance slightly more than one sample at high rates;
         * retire the accumulated whole frame before adding another span. Each
         * delivery still consumes real host credit, including after rate changes. */
        while (output->sample_fraction >= (1u << 28) && output->sample_acc >= clock) {
            output->sample_acc -= clock;
            cupid_gb_audio_render(gb);
        }
        apu->pending_cycles = (uint16_t)remaining;
        if (!force && !output_needs_run(gb))
            break;
        apu->pending_cycles = 0;
    }
    if (!direct)
        apu->envelope_delay = envelope_delay;
}

void cupid_gb_apu_run(CupidGb *gb, bool force) {
    if (gb)
        run_pending_audio(gb, force, false);
}

void cupid_gb_apu_credit_divider(CupidGb *gb, uint16_t cycles) {
    cupid_gb_audio_credit(gb, cycles);
    if (!gb->cpu.stopped || cupid_gb_model_is_cgb(gb->model)) {
        assert((unsigned)gb->apu.pending_cycles + cycles <= UINT16_MAX);
        gb->apu.pending_cycles = (uint16_t)(gb->apu.pending_cycles + cycles);
    }
}

void cupid_gb_tick_apu_divider(CupidGb *gb, uint16_t cycles) {
    if (!gb || !cycles)
        return;
    gb->apu.pcm_read_inhibit = 0;
    cupid_gb_apu_credit_divider(gb, cycles);
}

void cupid_gb_tick_apu(CupidGb *gb, uint16_t cycles) {
    if (!gb)
        return;
    /* The direct-clock API also accepts odd spans. Small deliveries keep its
     * rational credit bounded and make one-clock and batched calls equivalent. */
    run_pending_audio(gb, true, true);
    while (cycles) {
        uint16_t span = cycles > 1 ? 2 : 1;
        const uint32_t rate = cupid_gb_apu_get_sample_rate(gb);
        const uint32_t available = cupid_gb_base_clock_hz(gb) - gb->apu.output.sample_acc;
        if (available <= rate)
            span = 1;
        cupid_gb_tick_apu_divider(gb, span);
        run_pending_audio(gb, true, true);
        cycles = (uint16_t)(cycles - span);
    }
}

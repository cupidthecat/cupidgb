/**
 * @file apu.c
 * @brief Audio Processing Unit (APU) emulation for Game Boy (DMG) and Game Boy Color (CGB).
 *
 * Emulates all four Game Boy audio channels:
 *   - CH1: Pulse with frequency sweep
 *   - CH2: Pulse (no sweep)
 *   - CH3: Wave (4-bit PCM playback from wave RAM)
 *   - CH4: Noise (Linear Feedback Shift Register)
 *
 * The frame sequencer advances every 8192 T-cycles (512 Hz at the handheld clock) and drives
 * length counters, volume envelopes, and the CH1 frequency sweep unit.
 * Audio samples default to @ref CUPID_GB_APU_SAMPLE_RATE (44100 Hz).
 * Weighted integration and DC blocking feed a bounded stereo output buffer.
 *
 * @note DMG-specific hardware bugs (e.g. CH3 wave RAM corruption on retrigger)
 *       are selected by the hardware model, including cartridge compatibility mode.
 */
#include "cupid/gb/apu.h"
#include "cupid/gb/timer.h"

#include <math.h>
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

static void color_pulse_trigger(CupidGb *gb, unsigned channel, bool was_active) {
    CupidGbApu *apu = &gb->apu;
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    uint16_t *timer = channel ? &apu->ch2_timer : &apu->ch1_timer;
    bool dac = channel ? apu->ch2_dac : apu->ch1_dac;
    /* The trigger path settles against the power-relative audio divider. An
     * active retrigger takes one 1 MHz tick less than an inactive start. The
     * final two base clocks overlap the ordinary frequency-counter reload. */
    timing->start_delay = (uint8_t)((was_active ? 6u : 10u) + (apu->clock_phase & 2u));
    *timer = (uint16_t)(*timer + timing->start_delay - 2u);
    timing->clock_started = dac;
    timing->edge_age = 4;
    timing->envelope_clock = false;
    timing->envelope_locked = false;
    timing->envelope_lock_pending = false;
    if (!was_active) {
        timing->output_blocked = true;
    } else if (!timing->output_blocked) {
        pulse_latch_wave(apu, channel);
    }
}

static bool color_pulse_tick(CupidGbApu *apu, unsigned channel) {
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    uint16_t *timer = channel ? &apu->ch2_timer : &apu->ch1_timer;
    uint8_t *index = channel ? &apu->ch2_duty_pos : &apu->ch1_duty_pos;
    uint16_t frequency = channel ? apu->ch2_freq : apu->ch1_freq;
    bool active = channel ? apu->ch2_on : apu->ch1_on;
    if (timing->edge_age < 4) {
        ++timing->edge_age;
    }
    if (!active || !timing->clock_started) {
        return false;
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
    *timer = (uint16_t)((2048u - frequency) * 4u);
    *index = (uint8_t)((*index + 1u) & 7u);
    timing->edge_age = 0;
    timing->output_blocked = false;
    pulse_latch_wave(apu, channel);
    return true;
}

/* The envelope has separate rising and falling clock latches. Reaching an
 * endpoint locks on the falling edge, even if a register write has changed the
 * volume or direction since the rising edge sampled that endpoint. */
static void color_envelope_lower(CupidGbApu *apu, unsigned channel) {
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    if (timing->envelope_clock) {
        timing->envelope_clock = false;
        timing->envelope_locked |= timing->envelope_lock_pending;
    }
}

static void color_envelope_fall(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    for (unsigned channel = 0; channel < 2; ++channel) {
        CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
        if (!timing->envelope_clock) {
            continue;
        }
        color_envelope_lower(apu, channel);
        uint8_t value = gb->io_registers[channel ? 0x17 : 0x12];
        if (timing->envelope_locked || !(value & 7)) {
            continue;
        }
        uint8_t *volume = channel ? &apu->ch2_vol : &apu->ch1_vol;
        *volume = (uint8_t)((*volume + ((value & 8) ? 1 : 15)) & 15);
        pulse_latch_wave(apu, channel);
    }
}

void cupid_gb_apu_div_secondary_event(CupidGb *gb) {
    if (!gb || !gb->apu.apu_on || gb->model != CUPID_GB_MODEL_CGB) {
        return;
    }
    CupidGbApu *apu = &gb->apu;
    for (unsigned channel = 0; channel < 2; ++channel) {
        CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
        bool active = channel ? apu->ch2_on : apu->ch1_on;
        uint8_t *counter = channel ? &apu->ch2_env_timer : &apu->ch1_env_timer;
        if (!active || *counter) {
            continue;
        }
        uint8_t value = gb->io_registers[channel ? 0x17 : 0x12];
        *counter = value & 7;
        if (*counter && !timing->envelope_clock) {
            uint8_t volume = channel ? apu->ch2_vol : apu->ch1_vol;
            timing->envelope_clock = true;
            timing->envelope_lock_pending = (value & 8) ? volume == 15 : volume == 0;
        }
    }
}

static void color_envelope_write(CupidGb *gb, unsigned channel, uint8_t old, uint8_t value) {
    CupidGbApu *apu = &gb->apu;
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    uint8_t *volume = channel ? &apu->ch2_vol : &apu->ch1_vol;
    uint8_t *counter = channel ? &apu->ch2_env_timer : &apu->ch1_env_timer;
    if (timing->envelope_clock) {
        *counter = value & 7;
    }
    bool advance = !timing->envelope_locked &&
                   (((value & 7) && !(old & 7)) || ((value & 15) == 8 && (old & 15) == 8));
    if ((old ^ value) & 8) {
        if (value & 8) {
            unsigned complement = !(old & 7) && !timing->envelope_locked ? 15 : 14;
            *volume = (uint8_t)((complement - *volume) & 15);
            advance = false;
        } else {
            *volume = (uint8_t)((16 - *volume) & 15);
        }
    }
    if (advance) {
        *volume = (uint8_t)((*volume + ((value & 8) ? 1 : 15)) & 15);
    } else if (!(value & 7)) {
        color_envelope_lower(apu, channel);
    }
    pulse_latch_wave(apu, channel);
}

static void color_pulse_frequency_write(CupidGbApu *apu, unsigned channel, uint16_t old_frequency,
                                        bool high_write, bool trigger) {
    CupidGbPulseTiming *timing = &apu->pulse_timing[channel];
    uint16_t frequency = channel ? apu->ch2_freq : apu->ch1_freq;
    uint16_t *timer = channel ? &apu->ch2_timer : &apu->ch1_timer;
    uint8_t *index = channel ? &apu->ch2_duty_pos : &apu->ch1_duty_pos;
    bool active = channel ? apu->ch2_on : apu->ch1_on;
    if (!active || timing->edge_age > 2) {
        return;
    }
    /* The high frequency bits also gate the duty clock. Removing the high-bank
     * input during its reload window cancels that phase advance, while the
     * already latched digital sample survives until the next frequency edge. */
    if (high_write && !trigger && old_frequency >= 0x700 && frequency < 0x700) {
        *index = (uint8_t)((*index + 7u) & 7u);
        timing->output_blocked = false;
    }
    if (timing->edge_age < 2) {
        *timer = (uint16_t)((2048u - frequency) * 4u - timing->edge_age);
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
        bool changed = apu->ch1_on;
        apu->ch1_on = false;
        return changed;
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

static void color_sweep_write(CupidGb *gb, uint8_t previous, uint8_t value) {
    CupidGbApu *apu = &gb->apu;
    CupidGbSweepTiming *timing = &apu->sweep_timing;
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
        apu->ch1_on = false;
    }
    color_sweep_schedule(gb);
}

static void color_noise_shift(CupidGbApu *apu) {
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    /* Keep the existing complemented LFSR representation. Its feedback is XOR,
     * and the held digital output is the complement of bit zero after a shift. */
    timing->pre_shift_bit7 = !(apu->ch4_lfsr & 0x80);
    unsigned feedback = (apu->ch4_lfsr ^ (apu->ch4_lfsr >> 1)) & 1;
    apu->ch4_lfsr = (uint16_t)((apu->ch4_lfsr >> 1) | (feedback << 14));
    if (apu->ch4_width7) {
        apu->ch4_lfsr = (uint16_t)((apu->ch4_lfsr & ~0x40u) | (feedback << 6));
    }
    timing->last_shift_narrow = apu->ch4_width7;
    timing->output_high = !(apu->ch4_lfsr & 1);
}

static bool color_noise_tick(CupidGbApu *apu) {
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    timing->clock_phase = (uint8_t)((timing->clock_phase + 1) & 7);
    if ((timing->clock_phase & 1) || !(timing->foreground || timing->background)) {
        return false;
    }
    unsigned divisor = apu->ch4_div_code ? apu->ch4_div_code * 4u : 2u;
    if (!timing->remaining) {
        timing->remaining = (uint8_t)divisor;
    }
    if (--timing->remaining) {
        timing->reload_edge = false;
        return false;
    }
    timing->remaining = (uint8_t)divisor;
    uint16_t mask = (uint16_t)(1u << apu->ch4_shift);
    bool previous = (timing->divider_counter & mask) != 0;
    timing->divider_counter = (uint16_t)((timing->divider_counter + 1) & 0x3fff);
    timing->counter_stepped = true;
    timing->reload_edge = true;
    if (!previous && (timing->divider_counter & mask) && apu->ch4_on) {
        color_noise_shift(apu);
        return true;
    }
    return false;
}

static void color_noise_trigger(CupidGbApu *apu, bool active) {
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    unsigned phase = timing->clock_phase >> 1;
    unsigned divisor = apu->ch4_div_code;
    bool background = timing->background;
    bool started_off = timing->started_dac_off;
    bool extra_step = false;
    bool shorten = false;
    timing->foreground = apu->ch4_dac;
    timing->background = true;
    timing->started_dac_off = !apu->ch4_dac;
    if (divisor > 1 && timing->remaining == 1) {
        timing->divider_counter = (uint16_t)((timing->divider_counter + 1) & 0x3fff);
    } else if (active && timing->remaining == 2 && phase == 0) {
        if (divisor == 0) {
            divisor = 8;
        } else if (divisor == 1) {
            shorten = !timing->counter_stepped;
            uint16_t mask = (uint16_t)(1u << apu->ch4_shift);
            bool previous = (timing->divider_counter & mask) != 0;
            timing->divider_counter = (uint16_t)((timing->divider_counter + 1) & 0x3fff);
            extra_step = !previous && (timing->divider_counter & mask);
        }
    }

    /* Trigger reload is phase dependent; ordinary prescaler reloads are not.
     * The counter itself survives retriggers and frequency-selector writes. */
    int delay = divisor ? (int)(divisor * 4 + 6) : 6;
    if (!divisor) {
        if (phase & 1) {
            delay += background ? -1 : 1;
        }
        if (background && !active && phase == 0 && started_off) {
            delay += 28;
        }
    } else if (divisor == 1) {
        static const int8_t inactive_adjustment[] = {0, -1, -2, 1};
        static const int8_t active_adjustment[] = {0, -5, -2, -3};
        delay += active ? active_adjustment[phase] : inactive_adjustment[phase];
        if (phase == 0 && ((active && apu->ch4_shift == 0) || (background && !active))) {
            delay -= 4;
        }
    } else {
        delay -= (int)phase;
        if (phase == 0) {
            delay -= 4;
            if (!apu->ch4_dac) {
                delay += 4;
            }
        }
    }
    if (shorten) {
        delay -= 4;
    }
    timing->remaining = (uint8_t)delay;
    apu->ch4_lfsr = (uint16_t)((!divisor && active && phase == 3) ? 0x7faa : 0x7fff);
    if (extra_step) {
        color_noise_shift(apu);
    }
    timing->output_high = false;
    timing->counter_stepped = phase == 2;
}

static void color_noise_frequency_write(CupidGbApu *apu, uint8_t previous) {
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    if (timing->reload_edge) {
        unsigned divisor = apu->ch4_div_code ? apu->ch4_div_code * 4u : 2u;
        unsigned phase = timing->clock_phase >> 1;
        timing->remaining = (uint8_t)(divisor + (divisor == 2 ? 0 : ((2 - phase) & 3)));
    }
    unsigned old_shift = previous >> 4;
    if (old_shift != apu->ch4_shift && !(timing->divider_counter & (1u << old_shift)) &&
        (timing->divider_counter & (1u << apu->ch4_shift))) {
        color_noise_shift(apu);
    }
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
static void cupid_gb_apu_clock_length(CupidGbApu *apu) {
    if (apu->ch1_len_en && apu->ch1_len > 0u) {
        apu->ch1_len = (uint8_t)(apu->ch1_len - 1u);
        if (apu->ch1_len == 0u) {
            apu->ch1_on = false;
        }
    }
    if (apu->ch2_len_en && apu->ch2_len > 0u) {
        apu->ch2_len = (uint8_t)(apu->ch2_len - 1u);
        if (apu->ch2_len == 0u) {
            apu->ch2_on = false;
        }
    }
    if (apu->ch3_len_en && apu->ch3_len > 0u) {
        apu->ch3_len = (uint16_t)(apu->ch3_len - 1u);
        if (apu->ch3_len == 0u) {
            apu->ch3_on = false;
        }
    }
    if (apu->ch4_len_en && apu->ch4_len > 0u) {
        apu->ch4_len = (uint8_t)(apu->ch4_len - 1u);
        if (apu->ch4_len == 0u) {
            apu->ch4_on = false;
        }
    }
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

/**
 * @brief Clocks an 8-bit length counter by one and disables the channel if it expires.
 *
 * @param length     Pointer to the channel's 8-bit length counter.
 * @param channel_on Pointer to the channel's active flag; set to `false` if
 *                   the counter reaches zero.
 */
static void cupid_gb_apu_clock_length_once_u8(uint8_t *length, bool *channel_on) {
    if (*length > 0u) {
        *length = (uint8_t)(*length - 1u);
        if (*length == 0u) {
            *channel_on = false;
        }
    }
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
static void cupid_gb_apu_clock_length_once_u16(uint16_t *length, bool *channel_on) {
    if (*length > 0u) {
        *length = (uint16_t)(*length - 1u);
        if (*length == 0u) {
            *channel_on = false;
        }
    }
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
static void cupid_gb_apu_clock_sweep(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    if (gb->model == CUPID_GB_MODEL_CGB) {
        if (!apu->ch1_sweep_timer) {
            apu->ch1_sweep_timer = 8;
        }
        apu->ch1_sweep_timer = apu->ch1_sweep_timer == 1 ? 8 : apu->ch1_sweep_timer - 1;
        color_sweep_schedule(gb);
        return;
    }

    if (!apu->ch1_sweep_en) {
        return;
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
                apu->ch1_on = false;
            } else if (apu->ch1_sweep_shift != 0u) {
                bool second_overflow = false;

                apu->ch1_sweep_shadow = new_freq;
                apu->ch1_freq = new_freq;
                gb->io_registers[0x13u] = (uint8_t)(new_freq & 0xffu);
                gb->io_registers[0x14u] =
                    (uint8_t)((gb->io_registers[0x14u] & 0xf8u) | ((new_freq >> 8u) & 0x07u));

                (void)cupid_gb_apu_calc_sweep_freq(apu, &second_overflow);
                if (second_overflow) {
                    apu->ch1_on = false;
                }
            }
        }
    }
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
static void cupid_gb_apu_clock_envelope(CupidGbApu *apu, bool color) {
    /* CH1 */
    if (!color && apu->ch1_env_period != 0u) {
        if (apu->ch1_env_timer > 0u) {
            apu->ch1_env_timer = (uint8_t)(apu->ch1_env_timer - 1u);
        }
        if (apu->ch1_env_timer == 0u) {
            apu->ch1_env_timer = apu->ch1_env_period;
            if (apu->ch1_env_add && apu->ch1_vol < 15u) {
                apu->ch1_vol = (uint8_t)(apu->ch1_vol + 1u);
            } else if (!apu->ch1_env_add && apu->ch1_vol > 0u) {
                apu->ch1_vol = (uint8_t)(apu->ch1_vol - 1u);
            }
        }
    }
    /* CH2 */
    if (!color && apu->ch2_env_period != 0u) {
        if (apu->ch2_env_timer > 0u) {
            apu->ch2_env_timer = (uint8_t)(apu->ch2_env_timer - 1u);
        }
        if (apu->ch2_env_timer == 0u) {
            apu->ch2_env_timer = apu->ch2_env_period;
            if (apu->ch2_env_add && apu->ch2_vol < 15u) {
                apu->ch2_vol = (uint8_t)(apu->ch2_vol + 1u);
            } else if (!apu->ch2_env_add && apu->ch2_vol > 0u) {
                apu->ch2_vol = (uint8_t)(apu->ch2_vol - 1u);
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
            } else if (!apu->ch4_env_add && apu->ch4_vol > 0u) {
                apu->ch4_vol = (uint8_t)(apu->ch4_vol - 1u);
            }
        }
    }
}

void cupid_gb_apu_div_event(CupidGb *gb) {
    if (!gb || !gb->apu.apu_on) {
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
    if (gb->model == CUPID_GB_MODEL_CGB) {
        if (phase == 6) {
            if (!apu->pulse_timing[0].envelope_clock) {
                apu->ch1_env_timer = (uint8_t)((apu->ch1_env_timer - 1u) & 7u);
            }
            if (!apu->pulse_timing[1].envelope_clock) {
                apu->ch2_env_timer = (uint8_t)((apu->ch2_env_timer - 1u) & 7u);
            }
        }
        if (gb->double_speed) {
            apu->envelope_delay = 2;
        } else {
            color_envelope_fall(gb);
        }
    }
    /* Bit 0 clocks length, bit 1 sweep, and bit 2 envelopes. */
    static const uint8_t clocks[] = {1, 0, 3, 0, 1, 0, 3, 4};
    unsigned edges = clocks[phase];
    apu->fs_step = (uint8_t)((phase + 1) & 7);
    if (edges & 1) {
        cupid_gb_apu_clock_length(apu);
    }
    if (edges & 2) {
        cupid_gb_apu_clock_sweep(gb);
    }
    if (edges & 4) {
        cupid_gb_apu_clock_envelope(apu, gb->model == CUPID_GB_MODEL_CGB);
    }
}

static void cupid_gb_apu_digital_samples(const CupidGb *gb, uint8_t digital[4]) {
    const CupidGbApu *apu = &gb->apu;
    memset(digital, 0, 4);
    if (apu->ch1_on) {
        bool high = gb->model == CUPID_GB_MODEL_CGB
                        ? apu->pulse_timing[0].output_high && !apu->pulse_timing[0].output_blocked
                        : ((cupid_gb_duty_mask[apu->ch1_duty] >> apu->ch1_duty_pos) & 1u) != 0;
        digital[0] = high ? apu->ch1_vol : 0;
    }
    if (apu->ch2_on) {
        bool high = gb->model == CUPID_GB_MODEL_CGB
                        ? apu->pulse_timing[1].output_high && !apu->pulse_timing[1].output_blocked
                        : ((cupid_gb_duty_mask[apu->ch2_duty] >> apu->ch2_duty_pos) & 1u) != 0;
        digital[1] = high ? apu->ch2_vol : 0;
    }
    if (apu->ch3_on && apu->ch3_out_level) {
        digital[2] = (uint8_t)(apu->ch3_sample >> (apu->ch3_out_level - 1u));
    }
    if (apu->ch4_on) {
        bool high =
            gb->model == CUPID_GB_MODEL_CGB ? apu->noise_timing.output_high : !(apu->ch4_lfsr & 1u);
        digital[3] = high ? apu->ch4_vol : 0;
    }
}

uint8_t cupid_gb_apu_read_pcm12(const CupidGb *gb) {
    uint8_t digital[4];
    if (!gb || gb->model != CUPID_GB_MODEL_CGB) {
        return 0xff;
    }
    cupid_gb_apu_digital_samples(gb, digital);
    return (uint8_t)(digital[0] | (digital[1] << 4));
}

uint8_t cupid_gb_apu_read_pcm34(const CupidGb *gb) {
    uint8_t digital[4];
    if (!gb || gb->model != CUPID_GB_MODEL_CGB) {
        return 0xff;
    }
    cupid_gb_apu_digital_samples(gb, digital);
    return (uint8_t)(digital[2] | (digital[3] << 4));
}

/* The signed DAC transfer is inverted: digital zero is positive full scale.
 * A stopped channel still drives zero through its DAC until the DAC is disabled.
 */
static void cupid_gb_apu_mix(const CupidGb *gb, int32_t *left, int32_t *right) {
    const CupidGbApu *apu = &gb->apu;
    uint8_t digital[4];
    const bool dac[4] = {apu->ch1_dac, apu->ch2_dac, apu->ch3_dac, apu->ch4_dac};
    uint8_t routing = gb->io_registers[0x25];
    uint8_t volume = gb->io_registers[0x24];
    *left = 0;
    *right = 0;
    cupid_gb_apu_digital_samples(gb, digital);
    for (unsigned channel = 0; channel < 4; ++channel) {
        if (dac[channel]) {
            int32_t amplitude = 15 - 2 * (int32_t)digital[channel];
            if (routing & (0x10u << channel)) {
                *left += amplitude;
            }
            if (routing & (1u << channel)) {
                *right += amplitude;
            }
        }
    }
    /* Four channels peak at 16320, leaving headroom for a full-polarity filter step. */
    *left *= (int32_t)(((volume >> 4) & 7u) + 1u) * 34;
    *right *= (int32_t)((volume & 7u) + 1u) * 34;
}

static uint32_t cupid_gb_apu_filter_rate(const CupidGb *gb, uint32_t sample_rate) {
    return (uint32_t)(pow(0.999958, (double)cupid_gb_base_clock_hz(gb) / sample_rate) *
                          1073741824.0 +
                      0.5);
}

bool cupid_gb_apu_set_sample_rate(CupidGb *gb, uint32_t sample_rate) {
    if (!gb || sample_rate < 8000u || sample_rate > 192000u) {
        return false;
    }
    if (cupid_gb_apu_get_sample_rate(gb) != sample_rate) {
        /* A partial frame cannot span two host sample rates. Queued frames use the old rate. */
        gb->apu.sample_acc = 0;
        gb->apu.mix_left = 0;
        gb->apu.mix_right = 0;
        gb->apu.buf_write = 0;
    }
    gb->apu.sample_rate = sample_rate;
    gb->apu.highpass_rate = cupid_gb_apu_filter_rate(gb, sample_rate);
    return true;
}

uint32_t cupid_gb_apu_get_sample_rate(const CupidGb *gb) {
    return gb && gb->apu.sample_rate ? gb->apu.sample_rate : CUPID_GB_APU_SAMPLE_RATE;
}

void cupid_gb_apu_set_highpass(CupidGb *gb, bool enabled) {
    if (gb) {
        gb->apu.highpass_disabled = !enabled;
        if (!enabled) {
            gb->apu.highpass_left = 0;
            gb->apu.highpass_right = 0;
        }
    }
}

static int16_t cupid_gb_apu_filter_sample(int64_t area, int64_t *capacitor, const CupidGbApu *apu,
                                          bool connected, uint32_t clock_hz) {
    int64_t input = (area / clock_hz) * 65536;
    int64_t output;
    if (!connected) {
        return 0;
    }
    output = input - *capacitor;
    if (apu->highpass_disabled) {
        output = input;
        *capacitor = 0;
    } else {
        /* Q16 voltage and Q30 retention keep exact-resume state independent of FP layout. */
        *capacitor = input - output * apu->highpass_rate / 1073741824;
    }
    output /= 65536;
    if (output > 32767) {
        output = 32767;
    } else if (output < -32768) {
        output = -32768;
    }
    return (int16_t)output;
}

static void cupid_gb_apu_emit_sample(CupidGb *gb, uint32_t clock_hz) {
    CupidGbApu *apu = &gb->apu;
    /* Monochrome hardware disconnects its coupling capacitor when all DACs are off. */
    bool connected = gb->model == CUPID_GB_MODEL_CGB || apu->ch1_dac || apu->ch2_dac ||
                     apu->ch3_dac || apu->ch4_dac;
    int16_t left =
        cupid_gb_apu_filter_sample(apu->mix_left, &apu->highpass_left, apu, connected, clock_hz);
    int16_t right =
        cupid_gb_apu_filter_sample(apu->mix_right, &apu->highpass_right, apu, connected, clock_hz);
    if (apu->buf_write < CUPID_GB_APU_BUF_FRAMES) {
        size_t index = (size_t)apu->buf_write * 2;
        apu->buf[index] = left;
        apu->buf[index + 1] = right;
        ++apu->buf_write;
    }
    apu->mix_left = 0;
    apu->mix_right = 0;
}

/* Integrate both sides of a sample boundary instead of point-sampling a fast oscillator.
 * The area uses sample-phase units: each output frame spans the model's base clock rate.
 * This box integration attenuates aliasing; it is not a full reconstruction filter.
 */
static void cupid_gb_apu_integrate(CupidGb *gb, uint32_t sample_rate, uint32_t clock_hz,
                                   int32_t left, int32_t right, uint16_t duration) {
    CupidGbApu *apu = &gb->apu;
    uint64_t remaining = (uint64_t)sample_rate * duration;
    while (remaining) {
        uint32_t portion = clock_hz - apu->sample_acc;
        if (portion > remaining) {
            portion = (uint32_t)remaining;
        }
        apu->mix_left += (int64_t)left * portion;
        apu->mix_right += (int64_t)right * portion;
        apu->sample_acc += portion;
        remaining -= portion;
        if (apu->sample_acc == clock_hz) {
            apu->sample_acc = 0;
            cupid_gb_apu_emit_sample(gb, clock_hz);
        }
    }
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
static void cupid_gb_apu_trigger_ch1(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    const bool was_active = apu->ch1_on;
    uint8_t nr10 = gb->io_registers[0x10u];
    uint8_t nr12 = gb->io_registers[0x12u];
    uint8_t nr13 = gb->io_registers[0x13u];
    uint8_t nr14 = gb->io_registers[0x14u];

    apu->ch1_dac = (nr12 & 0xf8u) != 0u;
    if (apu->ch1_len == 0u) {
        apu->ch1_len = 64u;
    }

    if (gb->model != CUPID_GB_MODEL_CGB) {
        apu->ch1_freq = (uint16_t)(((uint16_t)(nr14 & 0x07u) << 8u) | (uint16_t)nr13);
    }
    apu->ch1_timer = (uint16_t)((2048u - (uint32_t)apu->ch1_freq) * 4u);
    if (gb->model == CUPID_GB_MODEL_CGB) {
        color_pulse_trigger(gb, 0, was_active);
    }
    apu->ch1_vol = (uint8_t)((nr12 >> 4u) & 0x0fu);
    apu->ch1_env_add = (nr12 & 0x08u) != 0u;
    apu->ch1_env_period = (uint8_t)(nr12 & 0x07u);
    apu->ch1_env_timer = (apu->ch1_env_period != 0u) ? apu->ch1_env_period : 8u;
    if (gb->model == CUPID_GB_MODEL_CGB) {
        apu->ch1_env_timer &= 7;
    }
    apu->ch1_sweep_shadow = apu->ch1_freq;
    apu->ch1_sweep_subtracted = false;

    apu->ch1_sweep_period = (uint8_t)((nr10 >> 4u) & 0x07u);
    apu->ch1_sweep_neg = (nr10 & 0x08u) != 0u;
    apu->ch1_sweep_shift = (uint8_t)(nr10 & 0x07u);
    apu->ch1_sweep_en = (apu->ch1_sweep_period != 0u || apu->ch1_sweep_shift != 0u);
    apu->ch1_sweep_timer = (apu->ch1_sweep_period != 0u) ? apu->ch1_sweep_period : 8u;
    if (gb->model == CUPID_GB_MODEL_CGB) {
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
    if (gb->model != CUPID_GB_MODEL_CGB && apu->ch1_sweep_shift != 0u) {
        bool overflow = false;

        (void)cupid_gb_apu_calc_sweep_freq(apu, &overflow);
        if (overflow) {
            apu->ch1_on = false;
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
static void cupid_gb_apu_trigger_ch2(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    const bool was_active = apu->ch2_on;
    uint8_t nr22 = gb->io_registers[0x17u];
    uint8_t nr23 = gb->io_registers[0x18u];
    uint8_t nr24 = gb->io_registers[0x19u];

    apu->ch2_dac = (nr22 & 0xf8u) != 0u;
    if (apu->ch2_len == 0u) {
        apu->ch2_len = 64u;
    }

    apu->ch2_freq = (uint16_t)(((uint16_t)(nr24 & 0x07u) << 8u) | (uint16_t)nr23);
    apu->ch2_timer = (uint16_t)((2048u - (uint32_t)apu->ch2_freq) * 4u);
    if (gb->model == CUPID_GB_MODEL_CGB) {
        color_pulse_trigger(gb, 1, was_active);
    }
    apu->ch2_vol = (uint8_t)((nr22 >> 4u) & 0x0fu);
    apu->ch2_env_add = (nr22 & 0x08u) != 0u;
    apu->ch2_env_period = (uint8_t)(nr22 & 0x07u);
    apu->ch2_env_timer = (apu->ch2_env_period != 0u) ? apu->ch2_env_period : 8u;
    if (gb->model == CUPID_GB_MODEL_CGB) {
        apu->ch2_env_timer &= 7;
    }

    if (apu->ch2_dac) {
        apu->ch2_on = true;
    }
}

/**
 * @brief Triggers (restarts) CH3 (wave).
 *
 * Reloads the frequency timer and resets the wave position and sample
 * state from the current NR30–NR34 register values. If the length counter
 * is zero it is reloaded to 256. The initial timer includes a 6 T-cycle
 * startup delay.
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
        apu->ch3_on = true;
    }
}

/**
 * @brief Emulates the DMG CH3 wave RAM corruption bug on retrigger.
 *
 * On original DMG hardware, retriggering CH3 while it is active and its
 * timer is about to expire can corrupt wave RAM. This function replicates
 * that behaviour by copying part of the wave RAM based on the current
 * wave position.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note This function has no effect in CGB mode or when CH3 is inactive
 *       or its timer is more than 2 T-cycles from expiry.
 */
static void cupid_gb_apu_apply_dmg_ch3_retrigger_bug(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    uint8_t offset;

    if ((gb->model == CUPID_GB_MODEL_CGB) || !apu->ch3_on || apu->ch3_timer > 2u) {
        return;
    }

    offset = (uint8_t)(((apu->ch3_wave_pos + 1u) >> 1u) & 0x0fu);
    if (offset < 4u) {
        gb->io_registers[0x30u] = gb->io_registers[0x30u + offset];
    } else {
        memcpy(&gb->io_registers[0x30u], &gb->io_registers[0x30u + (offset & 0xfcu)], 4u);
    }
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
static void cupid_gb_apu_trigger_ch4(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
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

    apu->ch4_div_code = (uint8_t)(nr43 & 0x07u);
    apu->ch4_shift = (uint8_t)((nr43 >> 4u) & 0x0fu);
    apu->ch4_width7 = (nr43 & 0x08u) != 0u;

    period = (uint32_t)cupid_gb_noise_div[apu->ch4_div_code];
    period = period << apu->ch4_shift;
    apu->ch4_timer = (period > 0u) ? period : 8u;
    apu->ch4_lfsr = 0x7fffu;

    if (gb->model == CUPID_GB_MODEL_CGB) {
        color_noise_trigger(apu, apu->ch4_on);
    }

    if (apu->ch4_dac) {
        apu->ch4_on = true;
    }
}

/* The hardware portion precedes sample_acc. Resetting it leaves already generated
 * host samples, partial sample integration, and coupling capacitors continuous.
 * Wave RAM is a separate memory array and survives both power transitions. */
static void cupid_gb_apu_power(CupidGb *gb, bool enabled) {
    CupidGbApu *apu = &gb->apu;
    if (enabled == apu->apu_on) {
        return;
    }

    uint16_t lengths[4] = {0};
    if (enabled && gb->model != CUPID_GB_MODEL_CGB) {
        lengths[0] = apu->ch1_len;
        lengths[1] = apu->ch2_len;
        lengths[2] = apu->ch3_len;
        lengths[3] = apu->ch4_len;
    }
    memset(apu, 0, offsetof(CupidGbApu, sample_acc));
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
    CupidGbApu *apu = &gb->apu;

    if (off >= 0x30u && off <= 0x3fu) {
        if (apu->apu_on && apu->ch3_on) {
            if (!(gb->model == CUPID_GB_MODEL_CGB)) {
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
        if (!(gb->model == CUPID_GB_MODEL_CGB)) {
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

    switch (off) {
        // CH1
        case 0x10u: // NR10 sweep
        {
            bool old_neg = apu->ch1_sweep_neg;

            apu->ch1_sweep_period = (uint8_t)((val >> 4u) & 0x07u);
            apu->ch1_sweep_neg = (val & 0x08u) != 0u;
            apu->ch1_sweep_shift = (uint8_t)(val & 0x07u);
            if (gb->model == CUPID_GB_MODEL_CGB) {
                color_sweep_write(gb, previous, val);
            } else if (old_neg && !apu->ch1_sweep_neg && apu->ch1_sweep_subtracted) {
                apu->ch1_on = false;
            }
            break;
        }
        case 0x11u: // NR11 length/duty
            apu->ch1_duty = (uint8_t)((val >> 6u) & 0x03u);
            apu->ch1_len = (uint8_t)(64u - (val & 0x3fu));
            break;
        case 0x12u: // NR12 envelope
            if (gb->model == CUPID_GB_MODEL_CGB) {
                if (apu->ch1_on && (val & 0xf8)) {
                    color_envelope_write(gb, 0, previous, val);
                }
                apu->ch1_env_period = val & 7;
                apu->ch1_env_add = (val & 8) != 0;
            }
            apu->ch1_dac = (val & 0xf8u) != 0u;
            if (!apu->ch1_dac) {
                apu->ch1_on = false;
                apu->pulse_timing[0].clock_started = false;
            }
            break;
        case 0x13u: // NR13 freq low
        {
            uint16_t old_frequency = apu->ch1_freq;
            apu->ch1_freq = (uint16_t)((apu->ch1_freq & 0x700u) | (uint16_t)val);
            if (gb->model == CUPID_GB_MODEL_CGB) {
                color_pulse_frequency_write(apu, 0, old_frequency, false, false);
            }
            break;
        }
        case 0x14u: // NR14 freq high + trigger
        {
            bool old_len_en;
            bool trigger;
            bool length_reloaded;
            uint16_t old_frequency = apu->ch1_freq;

            apu->ch1_freq = (uint16_t)((apu->ch1_freq & 0x00ffu) | ((uint32_t)(val & 0x07u) << 8u));
            old_len_en = apu->ch1_len_en;
            apu->ch1_len_en = (val & 0x40u) != 0u;
            trigger = (val & 0x80u) != 0u;
            if (gb->model == CUPID_GB_MODEL_CGB) {
                color_pulse_frequency_write(apu, 0, old_frequency, true, trigger);
            }

            if (!cupid_gb_apu_next_step_clocks_length(apu) && !old_len_en && apu->ch1_len_en) {
                cupid_gb_apu_clock_length_once_u8(&apu->ch1_len, &apu->ch1_on);
            }

            length_reloaded = apu->ch1_len == 0u;
            if (trigger) {
                cupid_gb_apu_trigger_ch1(gb);
                if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch1_len_en &&
                    length_reloaded) {
                    cupid_gb_apu_clock_length_once_u8(&apu->ch1_len, &apu->ch1_on);
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
            if (gb->model == CUPID_GB_MODEL_CGB) {
                if (apu->ch2_on && (val & 0xf8)) {
                    color_envelope_write(gb, 1, previous, val);
                }
                apu->ch2_env_period = val & 7;
                apu->ch2_env_add = (val & 8) != 0;
            }
            apu->ch2_dac = (val & 0xf8u) != 0u;
            if (!apu->ch2_dac) {
                apu->ch2_on = false;
                apu->pulse_timing[1].clock_started = false;
            }
            break;
        case 0x18u: // NR23
        {
            uint16_t old_frequency = apu->ch2_freq;
            apu->ch2_freq = (uint16_t)((apu->ch2_freq & 0x700u) | (uint16_t)val);
            if (gb->model == CUPID_GB_MODEL_CGB) {
                color_pulse_frequency_write(apu, 1, old_frequency, false, false);
            }
            break;
        }
        case 0x19u: // NR24
        {
            bool old_len_en;
            bool trigger;
            bool length_reloaded;
            uint16_t old_frequency = apu->ch2_freq;

            apu->ch2_freq = (uint16_t)((apu->ch2_freq & 0x00ffu) | ((uint32_t)(val & 0x07u) << 8u));
            old_len_en = apu->ch2_len_en;
            apu->ch2_len_en = (val & 0x40u) != 0u;
            trigger = (val & 0x80u) != 0u;
            if (gb->model == CUPID_GB_MODEL_CGB) {
                color_pulse_frequency_write(apu, 1, old_frequency, true, trigger);
            }

            if (!cupid_gb_apu_next_step_clocks_length(apu) && !old_len_en && apu->ch2_len_en) {
                cupid_gb_apu_clock_length_once_u8(&apu->ch2_len, &apu->ch2_on);
            }

            length_reloaded = apu->ch2_len == 0u;
            if (trigger) {
                cupid_gb_apu_trigger_ch2(gb);
                if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch2_len_en &&
                    length_reloaded) {
                    cupid_gb_apu_clock_length_once_u8(&apu->ch2_len, &apu->ch2_on);
                }
            }
            break;
        }
        // CH3
        case 0x1au: // NR30 DAC power
            apu->ch3_dac = (val & 0x80u) != 0u;
            if (!apu->ch3_dac) {
                apu->ch3_on = false;
            }
            break;
        case 0x1bu: // NR31 length
            apu->ch3_len = (uint16_t)(256u - (uint16_t)val);
            break;
        case 0x1cu: // NR32 output level
            apu->ch3_out_level = (uint8_t)((val >> 5u) & 0x03u);
            break;
        case 0x1du: // NR33
            apu->ch3_freq = (uint16_t)((apu->ch3_freq & 0x700u) | (uint16_t)val);
            if (apu->ch3_on) {
                if ((gb->model == CUPID_GB_MODEL_CGB)) {
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

            if (!cupid_gb_apu_next_step_clocks_length(apu) && !old_len_en && apu->ch3_len_en) {
                cupid_gb_apu_clock_length_once_u16(&apu->ch3_len, &apu->ch3_on);
            }

            length_reloaded = apu->ch3_len == 0u;
            if (trigger) {
                cupid_gb_apu_apply_dmg_ch3_retrigger_bug(gb);
                cupid_gb_apu_trigger_ch3(gb);
                if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch3_len_en &&
                    length_reloaded) {
                    cupid_gb_apu_clock_length_once_u16(&apu->ch3_len, &apu->ch3_on);
                }
            } else if (apu->ch3_on) {
                if ((gb->model == CUPID_GB_MODEL_CGB)) {
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
            apu->ch4_dac = (val & 0xf8u) != 0u;
            if (!apu->ch4_dac) {
                if (gb->model == CUPID_GB_MODEL_CGB) {
                    CupidGbNoiseTiming *timing = &apu->noise_timing;
                    if (apu->ch4_on && apu->ch4_div_code) {
                        if (timing->remaining <= 2) {
                            /* DAC-off retains the carry before the ordinary
                             * fourteen-bit reload mask. Repeated starts and stops
                             * can advance this held value across its full uint16_t
                             * range; selector writes can observe those upper bits. */
                            ++timing->divider_counter;
                        }
                        timing->background = false;
                    }
                    timing->foreground = false;
                    timing->output_high = false;
                }
                apu->ch4_on = false;
            }
            break;
        case 0x22u: // NR43
            apu->ch4_div_code = (uint8_t)(val & 0x07u);
            apu->ch4_shift = (uint8_t)((val >> 4u) & 0x0fu);
            apu->ch4_width7 = (val & 0x08u) != 0u;
            if (gb->model == CUPID_GB_MODEL_CGB) {
                color_noise_frequency_write(apu, previous);
            }
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
                cupid_gb_apu_clock_length_once_u8(&apu->ch4_len, &apu->ch4_on);
            }

            length_reloaded = apu->ch4_len == 0u;
            if (trigger) {
                cupid_gb_apu_trigger_ch4(gb);
                if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch4_len_en &&
                    length_reloaded) {
                    cupid_gb_apu_clock_length_once_u8(&apu->ch4_len, &apu->ch4_on);
                }
            }
            break;
        }
        default:
            break;
    }
}

/**
 * @brief Advances the APU state by the given number of T-cycles.
 *
 * For each T-cycle:
 *   - Clocks CH1 and CH2 frequency timers and advances their duty positions
 *   - Clocks the CH3 wave timer and fetches the next 4-bit wave sample
 *   - Clocks the CH4 LFSR noise timer and shifts the LFSR
 *   - Integrates the mixer and emits samples at the configured output rate
 *
 * The DIV falling-edge callback clocks the frame sequencer separately.
 * On monochrome hardware STOP holds oscillator state while output sampling continues.
 *
 * @param gb     Pointer to the Game Boy state.
 * @param cycles Number of T-cycles to advance.
 */
void cupid_gb_tick_apu(CupidGb *gb, uint16_t cycles) {
    CupidGbApu *apu = &gb->apu;
    uint16_t c;
    int32_t left = 0;
    int32_t right = 0;
    uint32_t sample_rate = cupid_gb_apu_get_sample_rate(gb);
    const uint32_t clock_hz = cupid_gb_base_clock_hz(gb);
    if (!apu->highpass_rate) {
        apu->highpass_rate = cupid_gb_apu_filter_rate(gb, sample_rate);
    }

    uint16_t pending = 0;
    for (c = 0u; c < cycles; ++c) {
        bool mix_changed = c == 0;
        bool oscillator_running = !gb->cpu.stopped || gb->model == CUPID_GB_MODEL_CGB;
        if (oscillator_running && apu->ch3_wave_access_ticks > 0u) {
            apu->ch3_wave_access_ticks = (uint8_t)(apu->ch3_wave_access_ticks - 1u);
        }

        if (apu->apu_on && oscillator_running) {
            apu->clock_phase = (uint8_t)((apu->clock_phase + 1u) & 3u);
            if (gb->model == CUPID_GB_MODEL_CGB) {
                mix_changed |= color_sweep_tick(gb);
                mix_changed |= color_pulse_tick(apu, 0);
                mix_changed |= color_pulse_tick(apu, 1);
                if (apu->envelope_delay && !--apu->envelope_delay) {
                    color_envelope_fall(gb);
                    mix_changed = true;
                }
            } else {
                // CH1 frequency timer
                if (apu->ch1_timer > 0u) {
                    apu->ch1_timer = (uint16_t)(apu->ch1_timer - 1u);
                }
                if (apu->ch1_timer == 0u) {
                    apu->ch1_timer = (uint16_t)((2048u - (uint32_t)apu->ch1_freq) * 4u);
                    apu->ch1_duty_pos = (uint8_t)((apu->ch1_duty_pos + 1u) & 7u);
                    mix_changed |= apu->ch1_on && apu->ch1_dac;
                }

                // CH2 frequency timer
                if (apu->ch2_timer > 0u) {
                    apu->ch2_timer = (uint16_t)(apu->ch2_timer - 1u);
                }
                if (apu->ch2_timer == 0u) {
                    apu->ch2_timer = (uint16_t)((2048u - (uint32_t)apu->ch2_freq) * 4u);
                    apu->ch2_duty_pos = (uint8_t)((apu->ch2_duty_pos + 1u) & 7u);
                    mix_changed |= apu->ch2_on && apu->ch2_dac;
                }
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
                        uint8_t wbyte = gb->io_registers[0x30u + apu->ch3_current_byte];
                        if ((apu->ch3_wave_pos & 1u) == 0u) {
                            apu->ch3_sample = (uint8_t)((wbyte >> 4u) & 0x0fu);
                        } else {
                            apu->ch3_sample = (uint8_t)(wbyte & 0x0fu);
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
                    mix_changed |= apu->ch3_dac;
                }
            }

            // CH4 LFSR timer
            if (gb->model == CUPID_GB_MODEL_CGB) {
                mix_changed |= color_noise_tick(apu);
            } else {
                if (apu->ch4_timer > 0u) {
                    apu->ch4_timer = apu->ch4_timer - 1u;
                }
                /* The noise divider is fourteen bits wide; bits 14 and 15 never rise. */
                if (apu->ch4_timer == 0u && apu->ch4_shift < 14u) {
                    uint16_t xor_bit = (uint16_t)((apu->ch4_lfsr ^ (apu->ch4_lfsr >> 1u)) & 1u);
                    apu->ch4_lfsr = (uint16_t)((apu->ch4_lfsr >> 1u) | (uint16_t)(xor_bit << 14u));
                    if (apu->ch4_width7) {
                        apu->ch4_lfsr =
                            (uint16_t)((apu->ch4_lfsr & 0xffbfu) | (uint16_t)(xor_bit << 6u));
                    }
                    mix_changed |= apu->ch4_on && apu->ch4_dac;
                    {
                        uint32_t noise_period = (uint32_t)cupid_gb_noise_div[apu->ch4_div_code];
                        noise_period = noise_period << apu->ch4_shift;
                        apu->ch4_timer = (noise_period > 0u) ? noise_period : 8u;
                    }
                }
            }
        }

        /* Oscillators retain their per-base-clock event ordering. Model, DAC and
         * filter settings stay fixed throughout this call, and sample emission
         * has no guest callback. Integrating a constant mixer run at once keeps
         * sample boundaries, queued PCM, capacitors and partial areas identical
         * at return, including when the output queue is already full.
         */
        if (mix_changed) {
            cupid_gb_apu_integrate(gb, sample_rate, clock_hz, left, right, pending);
            pending = 0;
            cupid_gb_apu_mix(gb, &left, &right);
        }
        ++pending;
    }
    cupid_gb_apu_integrate(gb, sample_rate, clock_hz, left, right, pending);
}

/**
 * @brief Drains accumulated audio samples from the internal buffer.
 *
 * Copies up to @p max_frames stereo sample frames from the APU's internal
 * buffer into @p out, retaining any unread frames. Each frame consists
 * of two `int16_t` values (left, right), so @p out must have capacity for
 * at least `max_frames * 2` elements.
 *
 * @param gb         Pointer to the Game Boy state.
 * @param out        Destination buffer for interleaved stereo int16_t samples.
 * @param max_frames Maximum number of frames to copy.
 *
 * @return The number of frames actually copied, or `0` if @p gb or @p out
 *         is NULL.
 */
uint32_t cupid_gb_apu_drain(CupidGb *gb, int16_t *out, uint32_t max_frames) {
    CupidGbApu *apu;
    uint32_t count;

    if (gb == 0 || out == 0) {
        return 0u;
    }
    apu = &gb->apu;
    count = (apu->buf_write < max_frames) ? apu->buf_write : max_frames;
    if (count > 0u) {
        memcpy(out, apu->buf, (size_t)(count * 2u * sizeof(int16_t)));
        apu->buf_write -= count;
        memmove(apu->buf, apu->buf + count * 2u, (size_t)apu->buf_write * 2u * sizeof(int16_t));
    }
    return count;
}

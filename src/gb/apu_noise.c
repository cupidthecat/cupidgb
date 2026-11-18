/**
 * @file    apu_noise.c
 * @brief   Noise prescaler, feedback latches, and frequency-selector transitions.
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
#include "apu_noise.h"

#include "audio_output.h"

static void noise_shift(CupidGbApu *apu, bool feedback) {
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    /* Keep the existing complemented LFSR representation. Its feedback is XOR,
     * and the held digital output is the complement of bit zero after a shift. */
    timing->pre_shift_bit7 = !(apu->ch4_lfsr & 0x80);
    apu->ch4_lfsr = (uint16_t)((apu->ch4_lfsr >> 1) | (feedback << 14));
    if (apu->ch4_width7) {
        apu->ch4_lfsr = (uint16_t)((apu->ch4_lfsr & ~0x40u) | (feedback << 6));
    }
    timing->last_shift_narrow = apu->ch4_width7;
    timing->output_high = !(apu->ch4_lfsr & 1);
}

static bool noise_feedback(const CupidGbApu *apu) {
    return ((apu->ch4_lfsr ^ (apu->ch4_lfsr >> 1)) & 1u) != 0;
}

static void color_noise_shift(CupidGbApu *apu) {
    noise_shift(apu, noise_feedback(apu));
}

static void monochrome_noise_shift(CupidGbApu *apu) {
    noise_shift(apu, !apu->noise_timing.feedback_latch);
}

static void noise_publish_register_edge(CupidGb *gb) {
    const CupidGbApu *apu = &gb->apu;
    if (apu->ch4_on)
        cupid_gb_audio_update(gb, 3, apu->noise_timing.output_high ? apu->ch4_vol : 0, 0, false);
}

static void noise_register_shift(CupidGb *gb, bool color) {
    if (color)
        color_noise_shift(&gb->apu);
    else
        monochrome_noise_shift(&gb->apu);
    noise_publish_register_edge(gb);
}

bool cupid_gb_noise_tick(CupidGb *gb, bool sample_pcm) {
    CupidGbApu *apu = &gb->apu;
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
    const bool next = (timing->divider_counter & mask) != 0;
    const bool color = cupid_gb_model_is_cgb(gb->model);
    /* Monochrome hardware captures feedback on the falling selector edge. The
     * following rising edge shifts that held value, so the first rise after
     * reset cannot generate new feedback by itself. */
    if (!color && previous && !next && apu->ch4_on)
        timing->feedback_latch = !noise_feedback(apu);
    if (!previous && next && apu->ch4_on) {
        if (sample_pcm && (!timing->output_high || !apu->ch4_vol))
            apu->pcm_read_inhibit |= 0xf000u;
        if (color)
            color_noise_shift(apu);
        else
            monochrome_noise_shift(apu);
        return true;
    }
    return false;
}

void cupid_gb_noise_prepare(CupidGb *gb, bool active) {
    CupidGbApu *apu = &gb->apu;
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    unsigned phase = timing->clock_phase >> 1;
    unsigned divisor = apu->ch4_div_code;
    bool background = timing->background;
    bool started_off = timing->started_dac_off;
    bool extra_step = false;
    bool shorten = false;
    const bool color = cupid_gb_model_is_cgb(gb->model);
    const bool early_clock = !color || cupid_gb_model_is_early_cgb(gb->model);
    const bool fast_early_clock = early_clock && gb->double_speed;
    if (!color)
        timing->feedback_latch = false;
    timing->foreground = apu->ch4_dac;
    timing->background = true;
    timing->started_dac_off = !apu->ch4_dac;
    if (divisor > 1 &&
        (timing->remaining == 1 || (fast_early_clock && active && timing->remaining == 2))) {
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
            delay += !early_clock && background ? -1 : 1;
        } else if (fast_early_clock) {
            delay += 2;
        }
        if (background && !active && phase == 0 && started_off) {
            delay += 28;
        }
    } else if (divisor == 1) {
        static const int8_t inactive_adjustment[] = {0, -1, -2, 1};
        static const int8_t active_adjustment[] = {0, -5, -2, -3};
        delay += active ? active_adjustment[phase] : inactive_adjustment[phase];
        if (fast_early_clock && phase == 2)
            delay += 4;
        if (phase == 0 && ((active && apu->ch4_shift == 0) || (background && !active))) {
            delay -= 4;
        }
    } else {
        delay -= (int)phase;
        if (phase == 0) {
            /* Fast early-model clocks retain this startup interval; the
             * ordinary monochrome and color paths bypass four prescaler ticks. */
            if (!fast_early_clock)
                delay -= 4;
            if (!apu->ch4_dac) {
                delay += 4;
            }
        }
    }
    if (!divisor && fast_early_clock && background && !active)
        --delay;
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

/* A short selector pulse can clock only part of the noise shift register. The
 * operation depends on the old and new low selector lines, independently of
 * the counter bits that decide whether the pulse exists. */
typedef enum NoiseSelectorPulse {
    NOISE_PULSE_SHIFT,
    NOISE_PULSE_RETAIN,
    NOISE_PULSE_NEIGHBORS,
    NOISE_PULSE_LOW_PAIR,
    NOISE_PULSE_CARRY,
    NOISE_PULSE_HOLD,
    NOISE_PULSE_HOLD_CLEAR
} NoiseSelectorPulse;

static NoiseSelectorPulse color_noise_short_pulse(unsigned from, unsigned to) {
    if (from < 2 && to >= 2 && to <= 5)
        return to == from + 2 ? NOISE_PULSE_NEIGHBORS : NOISE_PULSE_RETAIN;
    if (from == 2) {
        if (to == 0 || to == 3)
            return NOISE_PULSE_LOW_PAIR;
        if (to == 1)
            return NOISE_PULSE_RETAIN;
        if (to == 4)
            return NOISE_PULSE_HOLD_CLEAR;
        if (to == 5)
            return NOISE_PULSE_HOLD;
    }
    if ((from == 3 && (to == 4 || to == 5)) || (from == 4 && to >= 1 && to <= 3) ||
        (from == 5 && (to == 2 || to == 3)))
        return NOISE_PULSE_RETAIN;
    return from == 5 && to == 0 ? NOISE_PULSE_CARRY : NOISE_PULSE_SHIFT;
}

static uint16_t color_noise_bits(const CupidGbApu *apu) {
    return apu->ch4_lfsr ^ 0x7fffu;
}

static void color_noise_store_bits(CupidGbApu *apu, uint16_t bits) {
    apu->ch4_lfsr = bits ^ 0x7fffu;
}

static void color_noise_selector_pulse(CupidGb *gb, unsigned from, unsigned to) {
    CupidGbApu *apu = &gb->apu;
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    NoiseSelectorPulse pulse = color_noise_short_pulse(from, to);
    uint16_t before = color_noise_bits(apu);
    uint16_t bits;
    if (pulse == NOISE_PULSE_HOLD || pulse == NOISE_PULSE_HOLD_CLEAR) {
        bits = before;
        if (pulse == NOISE_PULSE_HOLD_CLEAR) {
            if ((bits & 3u) == 2u)
                bits &= (uint16_t)~(apu->ch4_width7 ? 0x4040u : 0x4000u);
            if ((bits & 0x19u) == 8u)
                bits &= 0xfff7u;
        }
        bits = (uint16_t)((bits & ~1u) | ((bits >> 1) & 1u));
        timing->output_high = (bits & 1u) != 0;
        noise_publish_register_edge(gb);
        timing->last_shift_narrow = apu->ch4_width7;
    } else {
        noise_register_shift(gb, true);
        bits = color_noise_bits(apu);
        if (pulse == NOISE_PULSE_RETAIN) {
            bits &= before | 1u;
        } else if (pulse == NOISE_PULSE_NEIGHBORS) {
            uint16_t cells = apu->ch4_width7 ? 0x2022u : 0x2002u;
            bits &= (uint16_t)(before | ~cells);
        } else if (pulse == NOISE_PULSE_LOW_PAIR || pulse == NOISE_PULSE_CARRY) {
            if (pulse == NOISE_PULSE_CARRY) {
                unsigned low = bits & 0x71u;
                if ((apu->ch4_width7 && low == 0x20u) || low == 0x61u)
                    bits &= 0xffdfu;
                unsigned high = bits & 0x7001u;
                if (high == 0x2000u || high == 0x6001u)
                    bits &= 0xdfffu;
            }
            if ((bits & 3u) == 2u)
                bits &= 0xfffdu;
        }
    }
    color_noise_store_bits(apu, bits);
}

typedef enum CgbDNoiseAction {
    CGB_D_NOISE_HOLD,
    CGB_D_NOISE_SHIFT,
    CGB_D_NOISE_SET_FEEDBACK_WITHOUT_SHIFT,
    CGB_D_NOISE_SHIFT_SET_FEEDBACK,
    CGB_D_NOISE_SHIFT_RETAIN_NARROW_FEEDBACK,
    CGB_D_NOISE_SHIFT_INTERSECT_PRIOR,
    CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR_IF_BIT6_LOW,
    CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR_UNLESS_BIT6_ONLY,
    CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR,
    CGB_D_NOISE_SHIFT_RETAIN_FEEDBACK_ON_LOW_PAIR
} CgbDNoiseAction;

/* When all three sampled levels are high, selected writes set the feedback
 * cells without a shift. Each bit identifies the new selector. */
static const uint16_t cgb_d_feedback_set[16] = {
    0x2e28u, 0x3c04u, 0x3a20u, 0x3000u, 0x2e08u, 0x0c04u, 0x2a00u, 0x0000u,
    0x3e28u, 0x3c04u, 0x3a20u, 0x3010u, 0x2e08u, 0x0c04u, 0x0000u, 0x0000u,
};

/* Old selector by new selector for high endpoints and a low intermediate
 * level. The measured operations are encoded as CgbDNoiseAction values. */
static const uint8_t cgb_d_noise_action[16][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 8, 3, 0, 0}, /* 0 */
    {0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 3, 3, 7, 7, 0, 0}, /* 1 */
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 0, 3, 7, 3, 0, 0}, /* 2 */
    {0, 0, 0, 0, 0, 0, 0, 0, 5, 2, 2, 0, 3, 3, 0, 0}, /* 3 */
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 3, 3, 0, 3, 0, 0}, /* 4 */
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 3, 3, 2, 0, 0, 0}, /* 5 */
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 2, 3, 2, 3, 0, 0}, /* 6 */
    {0, 0, 0, 0, 0, 0, 0, 0, 5, 2, 2, 2, 2, 2, 0, 0}, /* 7 */
    {0, 4, 6, 4, 3, 9, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0}, /* 8 */
    {3, 0, 9, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0}, /* 9 */
    {3, 4, 0, 9, 3, 9, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0}, /* A */
    {3, 8, 3, 0, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0}, /* B */
    {6, 9, 9, 9, 0, 4, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0}, /* C */
    {3, 5, 9, 3, 3, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0}, /* D */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* E */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* F */
};

static bool cgb_d_clear_noise_neighbor(CgbDNoiseAction action, uint16_t bits) {
    if (bits & 1u)
        return false;
    switch (action) {
        case CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR_IF_BIT6_LOW:
            return !(bits & 0x40u);
        case CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR_UNLESS_BIT6_ONLY:
            return (bits & 0x60u) != 0x40u;
        case CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR:
            return true;
        default:
            return false;
    }
}

static void cgb_d_noise_selector_write(CupidGb *gb, unsigned from, unsigned to, bool high,
                                       bool intermediate_high) {
    CgbDNoiseAction action;
    if (!high) {
        action = intermediate_high ? CGB_D_NOISE_SHIFT : CGB_D_NOISE_HOLD;
    } else if (intermediate_high) {
        action = (cgb_d_feedback_set[from] & (1u << to)) ? CGB_D_NOISE_SET_FEEDBACK_WITHOUT_SHIFT
                                                         : CGB_D_NOISE_HOLD;
    } else {
        action = (CgbDNoiseAction)cgb_d_noise_action[from][to];
    }
    if (action == CGB_D_NOISE_HOLD)
        return;

    CupidGbApu *apu = &gb->apu;
    const uint16_t before = color_noise_bits(apu);
    const uint16_t cells = apu->ch4_width7 ? 0x4040u : 0x4000u;
    if (action == CGB_D_NOISE_SET_FEEDBACK_WITHOUT_SHIFT) {
        color_noise_store_bits(apu, before | cells);
        return;
    }

    if (cgb_d_clear_noise_neighbor(action, before))
        color_noise_store_bits(apu, before & 0x7ffdu);

    /* A shift publishes its output before the feedback cells settle.
     * Only the intersecting write samples the output a second time. */
    noise_register_shift(gb, true);
    uint16_t after = color_noise_bits(apu);
    switch (action) {
        case CGB_D_NOISE_SHIFT_SET_FEEDBACK:
        case CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR_IF_BIT6_LOW:
        case CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR_UNLESS_BIT6_ONLY:
        case CGB_D_NOISE_SHIFT_CLEAR_NEIGHBOR:
            after |= cells;
            break;
        case CGB_D_NOISE_SHIFT_RETAIN_NARROW_FEEDBACK:
            if (apu->ch4_width7)
                after |= before & cells;
            break;
        case CGB_D_NOISE_SHIFT_INTERSECT_PRIOR:
            after = (after & before) | (before & 1u) | cells;
            apu->noise_timing.output_high = (after & 1u) != 0;
            noise_publish_register_edge(gb);
            break;
        case CGB_D_NOISE_SHIFT_RETAIN_FEEDBACK_ON_LOW_PAIR:
            after |= (before & 3u) == 2u ? before & cells : cells;
            break;
        default:
            break;
    }
    color_noise_store_bits(apu, after);
}

static void color_noise_frequency_write(CupidGb *gb, uint8_t previous) {
    CupidGbApu *apu = &gb->apu;
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    if (timing->reload_edge) {
        unsigned divisor = apu->ch4_div_code ? apu->ch4_div_code * 4u : 2u;
        unsigned phase = timing->clock_phase >> 1;
        timing->remaining = (uint8_t)(divisor + (divisor == 2 ? 0 : ((2 - phase) & 3)));
    }
    unsigned from = previous >> 4;
    unsigned to = apu->ch4_shift;
    if (from == to)
        return;
    /* The top selector line settles before the other three lines. Returning
     * through that intermediate bit produces a pulse even when the final
     * selected counter level is unchanged. */
    unsigned middle = (from & 7u) | (to & 8u);
    bool old_level = (timing->divider_counter & (1u << from)) != 0;
    bool new_level = (timing->divider_counter & (1u << to)) != 0;
    bool middle_level = (timing->divider_counter & (1u << middle)) != 0;
    if (gb->model == CUPID_GB_MODEL_CGB_D && old_level == new_level) {
        cgb_d_noise_selector_write(gb, from, to, old_level, middle_level);
        return;
    }
    if (old_level != new_level) {
        if (new_level)
            noise_register_shift(gb, true);
        return;
    }
    if (old_level == middle_level)
        return;
    if (!new_level) {
        if (to < 8)
            noise_register_shift(gb, true);
        else
            color_noise_selector_pulse(gb, from & 7u, to & 7u);
    } else if (to < 8) {
        noise_register_shift(gb, true);
    } else if ((to & 7u) & ~(from & 7u)) {
        uint16_t bits = color_noise_bits(apu);
        bits = (uint16_t)((bits & ~0x80u) | ((bits >> 1) & 0x80u));
        unsigned low_from = from & 7u;
        if ((to & 7u) == 3 && (low_from == 0 || low_from == 2 || low_from == 4)) {
            uint16_t lanes = 0x545u;
            if (low_from == 2)
                lanes |= 0x10u | (uint16_t)((bits & (bits >> 1) & 0x404u) << 1);
            bits &= (uint16_t)((bits >> 1) | lanes);
            timing->output_high = (bits & 1u) != 0;
            noise_publish_register_edge(gb);
        }
        /* A wide write can expose the bit overwritten by the preceding narrow
         * edge. Forcing feedback cells does not refresh the held output tap. */
        if (!apu->ch4_width7 && (previous & 8u) && timing->last_shift_narrow)
            bits = (uint16_t)((bits & ~0x40u) | (timing->pre_shift_bit7 ? 0x40u : 0));
        bits |= apu->ch4_width7 ? 0x4040u : 0x4000u;
        timing->last_shift_narrow = apu->ch4_width7;
        color_noise_store_bits(apu, bits);
    }
}

static void early_selector_transition(CupidGb *gb, uint8_t from, uint8_t to) {
    CupidGbApu *apu = &gb->apu;
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    const bool color = cupid_gb_model_is_cgb(gb->model);
    apu->ch4_width7 = (to & 8u) != 0;
    if ((from >> 4) == (to >> 4))
        return;

    /* The upper selector settles first. On a reload edge both adjacent
     * divider counts drive the selector. Monochrome falling edges capture
     * feedback for the next shift. Color feedback follows the live bits. */
    const unsigned middle = ((from >> 4) & 7u) | ((to >> 4) & 8u);
    uint16_t counter = timing->divider_counter;
    if (timing->reload_edge)
        counter |= (uint16_t)((counter - 1u) & 0x3fffu);
    const bool before = ((counter >> (from >> 4)) & 1u) != 0;
    const bool between = ((counter >> middle) & 1u) != 0;
    const bool after = ((counter >> (to >> 4)) & 1u) != 0;
    if (!color && before && !between)
        timing->feedback_latch = !noise_feedback(apu);

    if (before == after && before != between) {
        if (!after)
            noise_register_shift(gb, color);
    } else if (!before && after) {
        const bool narrow = apu->ch4_width7;
        apu->ch4_width7 = true;
        noise_register_shift(gb, color);
        apu->ch4_width7 = narrow;
        if ((to >> 4) <= 2u && between && !(counter & 8u)) {
            noise_register_shift(gb, true);
            const uint16_t cells = narrow ? 0x4040u : 0x4000u;
            apu->ch4_lfsr =
                (uint16_t)((apu->ch4_lfsr & ~cells) | ((apu->ch4_lfsr & (cells >> 1)) << 1));
        }
    } else if ((to >> 4) <= 2u && !before && !between && !after && (counter & 8u)) {
        noise_register_shift(gb, true);
    }

    if (!color && between && !after)
        timing->feedback_latch = !noise_feedback(apu);
}

void cupid_gb_noise_frequency_write(CupidGb *gb, uint8_t previous) {
    CupidGbApu *apu = &gb->apu;
    const bool early_color = cupid_gb_model_is_early_cgb(gb->model);
    if (cupid_gb_model_is_cgb(gb->model) && !early_color) {
        color_noise_frequency_write(gb, previous);
        return;
    }

    CupidGbNoiseTiming *timing = &apu->noise_timing;
    const uint8_t value = gb->io_registers[0x22];
    if (timing->reload_edge) {
        const unsigned divisor = apu->ch4_div_code ? apu->ch4_div_code * 4u : 2u;
        const unsigned phase = timing->clock_phase >> 1;
        static const uint8_t adjustment[] = {2, 1, 4, 3};
        timing->remaining = (uint8_t)(divisor + (divisor == 2 ? 0 : adjustment[phase]));
        const uint16_t counter = timing->divider_counter;
        const uint16_t prior = (uint16_t)((counter - 1u) & 0x3fffu);
        const unsigned old_mask = 1u << (previous >> 4);
        const unsigned new_mask = 1u << (value >> 4);
        if (!(counter & old_mask) && (counter & new_mask) && (counter & 0x80u) &&
            (prior & old_mask) && !(prior & new_mask) && (prior & 0x80u)) {
            if (early_color)
                apu->ch4_width7 = (previous & 8u) != 0;
            noise_register_shift(gb, true);
        }
    }
    /* On monochrome and CGB 0 through C, even a write that keeps the same
     * selector passes through the all-high register value. */
    early_selector_transition(gb, previous, 0xff);
    early_selector_transition(gb, 0xff, value);
}

void cupid_gb_noise_disconnect(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    CupidGbNoiseTiming *timing = &apu->noise_timing;
    if (apu->ch4_on && apu->ch4_div_code) {
        /* Removing the DAC gate can retain a carry above the ordinary fourteen
         * divider bits. A subsequent selector write can still observe it. */
        if (timing->remaining <= 2)
            ++timing->divider_counter;
        timing->background = false;
    }
    if (!cupid_gb_model_is_cgb(gb->model))
        timing->background = false;
    timing->foreground = false;
    timing->output_high = false;
    apu->ch4_on = false;
}

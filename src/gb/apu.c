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
 * The frame sequencer is clocked at 512 Hz (every 8192 T-cycles) and drives
 * length counters, volume envelopes, and the CH1 frequency sweep unit.
 * Audio samples are generated at @ref CUPID_GB_APU_SAMPLE_RATE (44100 Hz)
 * via sample accumulation and written into an internal ring buffer.
 *
 * @note DMG-specific hardware bugs (e.g. CH3 wave RAM corruption on retrigger)
 *       are emulated when not in CGB mode.
 */
#include "cupid/gb/apu.h"

#include <string.h>

#include "cupid/gb/gb.h"

/**
 * @brief Pulse duty-cycle waveforms.
 *
 * Encodes the four standard Game Boy duty cycles as 8-step binary sequences.
 * Index with [duty][pos] where duty is 0–3 and pos is the current duty
 * position (0–7). A value of 1 means the channel output is high.
 *
 *   - Duty 0: 12.5%  (1 high out of 8)
 *   - Duty 1: 25%    (2 high out of 8)
 *   - Duty 2: 50%    (4 high out of 8)
 *   - Duty 3: 75%    (6 high out of 8)
 */
static const uint8_t cupid_gb_duty_table[4u][8u] = {
    { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u }, /* 12.5% */
    { 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u }, /* 25%   */
    { 0u, 0u, 0u, 0u, 1u, 1u, 1u, 1u }, /* 50%   */
    { 1u, 1u, 1u, 1u, 1u, 1u, 0u, 0u }  /* 75%   */
};

/**
 * @brief CH4 noise channel base clock divisors indexed by div_code (0–7).
 *
 * Each value represents the base period in T-cycles before applying the
 * clock shift. Used in computing the LFSR timer period.
 */
static const uint16_t cupid_gb_noise_div[8u] = {
    8u, 16u, 32u, 48u, 64u, 80u, 96u, 112u
};

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
static void cupid_gb_apu_clock_length(CupidGbApu *apu)
{
    if (apu->ch1_len_en && apu->ch1_len > 0u) {
        apu->ch1_len = (uint8_t)(apu->ch1_len - 1u);
        if (apu->ch1_len == 0u) { apu->ch1_on = false; }
    }
    if (apu->ch2_len_en && apu->ch2_len > 0u) {
        apu->ch2_len = (uint8_t)(apu->ch2_len - 1u);
        if (apu->ch2_len == 0u) { apu->ch2_on = false; }
    }
    if (apu->ch3_len_en && apu->ch3_len > 0u) {
        apu->ch3_len = (uint16_t)(apu->ch3_len - 1u);
        if (apu->ch3_len == 0u) { apu->ch3_on = false; }
    }
    if (apu->ch4_len_en && apu->ch4_len > 0u) {
        apu->ch4_len = (uint8_t)(apu->ch4_len - 1u);
        if (apu->ch4_len == 0u) { apu->ch4_on = false; }
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
static bool cupid_gb_apu_next_step_clocks_length(const CupidGbApu *apu)
{
    return (apu->fs_step & 1u) == 0u;
}

/**
 * @brief Clocks an 8-bit length counter by one and disables the channel if it expires.
 *
 * @param length     Pointer to the channel's 8-bit length counter.
 * @param channel_on Pointer to the channel's active flag; set to `false` if
 *                   the counter reaches zero.
 */
static void cupid_gb_apu_clock_length_once_u8(uint8_t *length, bool *channel_on)
{
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
static void cupid_gb_apu_clock_length_once_u16(uint16_t *length, bool *channel_on)
{
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
static uint16_t cupid_gb_apu_calc_sweep_freq(CupidGbApu *apu, bool *overflow)
{
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
static void cupid_gb_apu_clock_sweep(CupidGb *gb)
{
    CupidGbApu *apu = &gb->apu;

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
                gb->io_registers[0x14u] = (uint8_t)((gb->io_registers[0x14u] & 0xf8u)
                                                    | ((new_freq >> 8u) & 0x07u));

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
static void cupid_gb_apu_clock_envelope(CupidGbApu *apu)
{
    /* CH1 */
    if (apu->ch1_env_period != 0u) {
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
    if (apu->ch2_env_period != 0u) {
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

/**
 * @brief Dispatches frame sequencer sub-clocks for a given step.
 *
 * Calls the appropriate sub-clock functions based on the current step:
 *   - Steps 0, 2, 4, 6: clock length counters
 *   - Step 7:            clock volume envelopes
 *
 * @param apu  Pointer to the APU state.
 * @param step The current frame sequencer step (0–7).
 */
static void cupid_gb_apu_clock_fs(CupidGbApu *apu, uint8_t step)
{
    if ((step & 1u) == 0u) {        /* steps 0, 2, 4, 6 */
        cupid_gb_apu_clock_length(apu);
    }
    if (step == 7u) {
        cupid_gb_apu_clock_envelope(apu);
    }
}

/**
 * @brief Mixes all active channels and writes one stereo sample to the output buffer.
 *
 * Computes the amplitude of each channel based on its current state and
 * applies NR51 stereo panning and NR50 master volume scaling. The resulting
 * left/right sample pair is scaled to `int16_t` range and appended to the
 * internal sample buffer if space is available.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Maximum unscaled amplitude is 480 per channel (15 vol × 4 ch × 8 vol levels).
 *       Samples are scaled by 64, yielding a maximum of 30720, safely within int16_t range.
 */
static void cupid_gb_apu_emit_sample(CupidGb *gb)
{
    CupidGbApu *apu = &gb->apu;
    uint8_t nr50 = gb->io_registers[0x24u];
    uint8_t nr51 = gb->io_registers[0x25u];
    int32_t left = 0;
    int32_t right = 0;
    uint8_t ch1_amp;
    uint8_t ch2_amp;
    uint8_t ch3_amp;
    uint8_t ch4_amp;
    uint8_t raw;

    // CH1 – pulse 
    if (apu->ch1_on && apu->ch1_dac) {
        ch1_amp = (uint8_t)(cupid_gb_duty_table[apu->ch1_duty][apu->ch1_duty_pos]
                            * apu->ch1_vol);
    } else {
        ch1_amp = 0u;
    }

    // CH2 – pulse
    if (apu->ch2_on && apu->ch2_dac) {
        ch2_amp = (uint8_t)(cupid_gb_duty_table[apu->ch2_duty][apu->ch2_duty_pos]
                            * apu->ch2_vol);
    } else {
        ch2_amp = 0u;
    }

    // CH3 – wave; output level shifts the 4-bit sample
    if (apu->ch3_on && apu->ch3_dac) {
        raw = apu->ch3_sample & 0x0fu;
        switch (apu->ch3_out_level) {
        case 1u:  ch3_amp = raw;                break;
        case 2u:  ch3_amp = (uint8_t)(raw >> 1u); break;
        case 3u:  ch3_amp = (uint8_t)(raw >> 2u); break;
        default:  ch3_amp = 0u;                 break;
        }
    } else {
        ch3_amp = 0u;
    }

    // CH4 – noise; LFSR bit 0 inverted
    if (apu->ch4_on && apu->ch4_dac) {
        ch4_amp = ((apu->ch4_lfsr & 1u) == 0u) ? apu->ch4_vol : 0u;
    } else {
        ch4_amp = 0u;
    }

    // Stereo mix (NR51 panning)
    if ((nr51 & 0x10u) != 0u) { left  += (int32_t)ch1_amp; }
    if ((nr51 & 0x01u) != 0u) { right += (int32_t)ch1_amp; }
    if ((nr51 & 0x20u) != 0u) { left  += (int32_t)ch2_amp; }
    if ((nr51 & 0x02u) != 0u) { right += (int32_t)ch2_amp; }
    if ((nr51 & 0x40u) != 0u) { left  += (int32_t)ch3_amp; }
    if ((nr51 & 0x04u) != 0u) { right += (int32_t)ch3_amp; }
    if ((nr51 & 0x80u) != 0u) { left  += (int32_t)ch4_amp; }
    if ((nr51 & 0x08u) != 0u) { right += (int32_t)ch4_amp; }

    // Master volume
    left  *= (int32_t)(((uint32_t)(nr50 >> 4u) & 7u) + 1u);
    right *= (int32_t)(((uint32_t)nr50 & 7u) + 1u);

    // Scale to int16_t: max = 15*4*8 = 480; 480*64 = 30720 < 32767
    if (apu->buf_write < CUPID_GB_APU_BUF_FRAMES) {
        size_t idx = (size_t)(apu->buf_write * 2u);
        apu->buf[idx]     = (int16_t)(left  * 64);
        apu->buf[idx + 1u] = (int16_t)(right * 64);
        apu->buf_write = apu->buf_write + 1u;
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
static void cupid_gb_apu_trigger_ch1(CupidGb *gb)
{
    CupidGbApu *apu = &gb->apu;
    uint8_t nr10 = gb->io_registers[0x10u];
    uint8_t nr12 = gb->io_registers[0x12u];
    uint8_t nr13 = gb->io_registers[0x13u];
    uint8_t nr14 = gb->io_registers[0x14u];

    apu->ch1_dac = (nr12 & 0xf8u) != 0u;
    if (apu->ch1_len == 0u) { apu->ch1_len = 64u; }

    apu->ch1_freq       = (uint16_t)(((uint16_t)(nr14 & 0x07u) << 8u) | (uint16_t)nr13);
    apu->ch1_timer      = (uint16_t)((2048u - (uint32_t)apu->ch1_freq) * 4u);
    apu->ch1_vol        = (uint8_t)((nr12 >> 4u) & 0x0fu);
    apu->ch1_env_add    = (nr12 & 0x08u) != 0u;
    apu->ch1_env_period = (uint8_t)(nr12 & 0x07u);
    apu->ch1_env_timer  = (apu->ch1_env_period != 0u) ? apu->ch1_env_period : 8u;
    apu->ch1_sweep_shadow = apu->ch1_freq;
    apu->ch1_sweep_subtracted = false;

    apu->ch1_sweep_period = (uint8_t)((nr10 >> 4u) & 0x07u);
    apu->ch1_sweep_neg    = (nr10 & 0x08u) != 0u;
    apu->ch1_sweep_shift  = (uint8_t)(nr10 & 0x07u);
    apu->ch1_sweep_en     = (apu->ch1_sweep_period != 0u || apu->ch1_sweep_shift != 0u);
    apu->ch1_sweep_timer  = (apu->ch1_sweep_period != 0u) ? apu->ch1_sweep_period : 8u;

    if (apu->ch1_dac) { apu->ch1_on = true; }

    /* Trigger-time overflow check */
    if (apu->ch1_sweep_shift != 0u) {
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
static void cupid_gb_apu_trigger_ch2(CupidGb *gb)
{
    CupidGbApu *apu = &gb->apu;
    uint8_t nr22 = gb->io_registers[0x17u];
    uint8_t nr23 = gb->io_registers[0x18u];
    uint8_t nr24 = gb->io_registers[0x19u];

    apu->ch2_dac = (nr22 & 0xf8u) != 0u;
    if (apu->ch2_len == 0u) { apu->ch2_len = 64u; }

    apu->ch2_freq       = (uint16_t)(((uint16_t)(nr24 & 0x07u) << 8u) | (uint16_t)nr23);
    apu->ch2_timer      = (uint16_t)((2048u - (uint32_t)apu->ch2_freq) * 4u);
    apu->ch2_vol        = (uint8_t)((nr22 >> 4u) & 0x0fu);
    apu->ch2_env_add    = (nr22 & 0x08u) != 0u;
    apu->ch2_env_period = (uint8_t)(nr22 & 0x07u);
    apu->ch2_env_timer  = (apu->ch2_env_period != 0u) ? apu->ch2_env_period : 8u;

    if (apu->ch2_dac) { apu->ch2_on = true; }
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
static void cupid_gb_apu_trigger_ch3(CupidGb *gb)
{
    CupidGbApu *apu = &gb->apu;
    uint8_t nr33 = gb->io_registers[0x1du];
    uint8_t nr34 = gb->io_registers[0x1eu];
    uint16_t period;

    apu->ch3_dac = (gb->io_registers[0x1au] & 0x80u) != 0u;
    if (apu->ch3_len == 0u) { apu->ch3_len = 256u; }

    apu->ch3_freq     = (uint16_t)(((uint16_t)(nr34 & 0x07u) << 8u) | (uint16_t)nr33);
    apu->ch3_active_freq = apu->ch3_freq;
    period            = (uint16_t)((2048u - (uint32_t)apu->ch3_freq) * 2u);
    if (period == 0u) { period = 2u; }
    apu->ch3_timer    = (uint16_t)(period + 6u);
    apu->ch3_wave_pos = 0u;
    apu->ch3_current_byte = 0u;
    apu->ch3_started = false;
    apu->ch3_wave_access_ticks = 0u;
    apu->ch3_freq_pending = false;

    if (apu->ch3_dac) { apu->ch3_on = true; }
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
static void cupid_gb_apu_apply_dmg_ch3_retrigger_bug(CupidGb *gb)
{
    CupidGbApu *apu = &gb->apu;
    uint8_t offset;

    if (gb->cgb_mode || !apu->ch3_on || apu->ch3_timer > 2u) {
        return;
    }

    offset = (uint8_t)(((apu->ch3_wave_pos + 1u) >> 1u) & 0x0fu);
    if (offset < 4u) {
        gb->io_registers[0x30u] = gb->io_registers[0x30u + offset];
    } else {
        memcpy(&gb->io_registers[0x30u],
               &gb->io_registers[0x30u + (offset & (uint8_t)~0x03u)],
               4u);
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
static void cupid_gb_apu_trigger_ch4(CupidGb *gb)
{
    CupidGbApu *apu = &gb->apu;
    uint8_t nr42 = gb->io_registers[0x21u];
    uint8_t nr43 = gb->io_registers[0x22u];
    uint32_t period;

    apu->ch4_dac = (nr42 & 0xf8u) != 0u;
    if (apu->ch4_len == 0u) { apu->ch4_len = 64u; }

    apu->ch4_vol        = (uint8_t)((nr42 >> 4u) & 0x0fu);
    apu->ch4_env_add    = (nr42 & 0x08u) != 0u;
    apu->ch4_env_period = (uint8_t)(nr42 & 0x07u);
    apu->ch4_env_timer  = (apu->ch4_env_period != 0u) ? apu->ch4_env_period : 8u;

    apu->ch4_div_code = (uint8_t)(nr43 & 0x07u);
    apu->ch4_shift    = (uint8_t)((nr43 >> 4u) & 0x0fu);
    apu->ch4_width7   = (nr43 & 0x08u) != 0u;

    period = (uint32_t)cupid_gb_noise_div[apu->ch4_div_code];
    if (apu->ch4_shift < 14u) {
        period = period << apu->ch4_shift;
    } else {
        period = period << 13u;
    }
    apu->ch4_timer = (period > 0u) ? period : 8u;
    apu->ch4_lfsr  = 0x7fffu;

    if (apu->ch4_dac) { apu->ch4_on = true; }
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
void cupid_gb_apu_on_write(CupidGb *gb, uint8_t off, uint8_t val)
{
    CupidGbApu *apu = &gb->apu;

    if (off >= 0x30u && off <= 0x3fu) {
        if (apu->apu_on && apu->ch3_on) {
            if (!gb->cgb_mode) {
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

    /* NR52 – APU power; always accessible */
    if (off == 0x26u) {
        bool was_on = apu->apu_on;
        apu->apu_on = (val & 0x80u) != 0u;
        if (was_on && !apu->apu_on) {
            /* Power-off: clear all NR registers except length counters */
            size_t r;
            for (r = 0x10u; r <= 0x25u; ++r) {
                gb->io_registers[r] = 0u;
            }
            apu->ch1_on = false;
            apu->ch2_on = false;
            apu->ch3_on = false;
            apu->ch4_on = false;
            apu->ch1_dac = false;
            apu->ch2_dac = false;
            apu->ch3_dac = false;
            apu->ch4_dac = false;
            apu->ch1_len_en = false;
            apu->ch2_len_en = false;
            apu->ch3_len_en = false;
            apu->ch4_len_en = false;
            apu->ch1_sweep_en = false;
            apu->ch1_sweep_timer = 0u;
            apu->ch1_sweep_period = 0u;
            apu->ch1_sweep_neg = false;
            apu->ch1_sweep_shift = 0u;
            apu->ch1_sweep_shadow = 0u;
            apu->ch1_sweep_subtracted = false;
            apu->ch1_env_period = 0u;
            apu->ch1_env_timer = 0u;
            apu->ch2_env_period = 0u;
            apu->ch2_env_timer = 0u;
            apu->ch4_env_period = 0u;
            apu->ch4_env_timer = 0u;
            apu->ch3_active_freq = 0u;
            apu->ch3_current_byte = 0u;
            apu->ch3_started = false;
            apu->ch3_wave_access_ticks = 0u;
            apu->ch3_freq_pending = false;
            apu->ch3_sample = 0u;
            apu->fs_counter = 8191u;
            apu->fs_step = 0u;
        } else if (!was_on && apu->apu_on) {
            if (gb->cgb_mode) {
                apu->ch1_len = 0u;
                apu->ch2_len = 0u;
                apu->ch3_len = 0u;
                apu->ch4_len = 0u;
            }
            apu->ch3_active_freq = 0u;
            apu->ch3_current_byte = 0u;
            apu->ch3_started = false;
            apu->ch3_wave_access_ticks = 0u;
            apu->ch3_freq_pending = false;
            apu->ch3_sample = 0u;
            apu->fs_counter = 8191u;
            apu->fs_step = 0u;
        }
        gb->io_registers[0x26u] = (uint8_t)(
            (apu->apu_on ? 0x80u : 0u) | 0x70u |
            (apu->ch4_on ? 0x08u : 0u) |
            (apu->ch3_on ? 0x04u : 0u) |
            (apu->ch2_on ? 0x02u : 0u) |
            (apu->ch1_on ? 0x01u : 0u));
        return;
    }

    if (!apu->apu_on) {
        if (!gb->cgb_mode) {
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
        } else if (off == 0x20u) {
            gb->io_registers[off] = val;
            apu->ch4_len = (uint8_t)(64u - (val & 0x3fu));
        }
        return;
    }

    gb->io_registers[off] = val;

    switch (off) {
    // CH1
    case 0x10u: // NR10 sweep
    {
        bool old_neg = apu->ch1_sweep_neg;

        apu->ch1_sweep_period = (uint8_t)((val >> 4u) & 0x07u);
        apu->ch1_sweep_neg    = (val & 0x08u) != 0u;
        apu->ch1_sweep_shift  = (uint8_t)(val & 0x07u);
        if (old_neg && !apu->ch1_sweep_neg && apu->ch1_sweep_subtracted) {
            apu->ch1_on = false;
        }
        break;
    }
    case 0x11u: // NR11 length/duty
        apu->ch1_duty = (uint8_t)((val >> 6u) & 0x03u);
        apu->ch1_len  = (uint8_t)(64u - (val & 0x3fu));
        break;
    case 0x12u: // NR12 envelope
        apu->ch1_dac = (val & 0xf8u) != 0u;
        if (!apu->ch1_dac) { apu->ch1_on = false; }
        break;
    case 0x13u: // NR13 freq low
        apu->ch1_freq = (uint16_t)((apu->ch1_freq & 0x700u) | (uint16_t)val);
        break;
    case 0x14u: // NR14 freq high + trigger
    {
        bool old_len_en;
        bool trigger;
        bool length_reloaded;

        apu->ch1_freq   = (uint16_t)((apu->ch1_freq & 0x00ffu) | ((uint16_t)(val & 0x07u) << 8u));
        old_len_en      = apu->ch1_len_en;
        apu->ch1_len_en = (val & 0x40u) != 0u;
        trigger         = (val & 0x80u) != 0u;

        if (!cupid_gb_apu_next_step_clocks_length(apu) && !old_len_en && apu->ch1_len_en) {
            cupid_gb_apu_clock_length_once_u8(&apu->ch1_len, &apu->ch1_on);
        }

        length_reloaded = apu->ch1_len == 0u;
        if (trigger) {
            cupid_gb_apu_trigger_ch1(gb);
            if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch1_len_en && length_reloaded) {
                cupid_gb_apu_clock_length_once_u8(&apu->ch1_len, &apu->ch1_on);
            }
        }
        break;
    }
    // CH2  
    case 0x16u: // NR21
        apu->ch2_duty = (uint8_t)((val >> 6u) & 0x03u);
        apu->ch2_len  = (uint8_t)(64u - (val & 0x3fu));
        break;
    case 0x17u: // NR22
        apu->ch2_dac = (val & 0xf8u) != 0u;
        if (!apu->ch2_dac) { apu->ch2_on = false; }
        break;
    case 0x18u: // NR23
        apu->ch2_freq = (uint16_t)((apu->ch2_freq & 0x700u) | (uint16_t)val);
        break;
    case 0x19u: // NR24
    {
        bool old_len_en;
        bool trigger;
        bool length_reloaded;

        apu->ch2_freq   = (uint16_t)((apu->ch2_freq & 0x00ffu) | ((uint16_t)(val & 0x07u) << 8u));
        old_len_en      = apu->ch2_len_en;
        apu->ch2_len_en = (val & 0x40u) != 0u;
        trigger         = (val & 0x80u) != 0u;

        if (!cupid_gb_apu_next_step_clocks_length(apu) && !old_len_en && apu->ch2_len_en) {
            cupid_gb_apu_clock_length_once_u8(&apu->ch2_len, &apu->ch2_on);
        }

        length_reloaded = apu->ch2_len == 0u;
        if (trigger) {
            cupid_gb_apu_trigger_ch2(gb);
            if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch2_len_en && length_reloaded) {
                cupid_gb_apu_clock_length_once_u8(&apu->ch2_len, &apu->ch2_on);
            }
        }
        break;
    }
    // CH3
    case 0x1au: // NR30 DAC power
        apu->ch3_dac = (val & 0x80u) != 0u;
        if (!apu->ch3_dac) { apu->ch3_on = false; }
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
            if (gb->cgb_mode) {
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

        apu->ch3_freq   = (uint16_t)((apu->ch3_freq & 0x00ffu) | ((uint16_t)(val & 0x07u) << 8u));
        old_len_en      = apu->ch3_len_en;
        apu->ch3_len_en = (val & 0x40u) != 0u;
        trigger         = (val & 0x80u) != 0u;

        if (!cupid_gb_apu_next_step_clocks_length(apu) && !old_len_en && apu->ch3_len_en) {
            cupid_gb_apu_clock_length_once_u16(&apu->ch3_len, &apu->ch3_on);
        }

        length_reloaded = apu->ch3_len == 0u;
        if (trigger) {
            cupid_gb_apu_apply_dmg_ch3_retrigger_bug(gb);
            cupid_gb_apu_trigger_ch3(gb);
            if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch3_len_en && length_reloaded) {
                cupid_gb_apu_clock_length_once_u16(&apu->ch3_len, &apu->ch3_on);
            }
        } else if (apu->ch3_on) {
            if (gb->cgb_mode) {
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
        if (!apu->ch4_dac) { apu->ch4_on = false; }
        break;
    case 0x22u: // NR43
        apu->ch4_div_code = (uint8_t)(val & 0x07u);
        apu->ch4_shift    = (uint8_t)((val >> 4u) & 0x0fu);
        apu->ch4_width7   = (val & 0x08u) != 0u;
        break;
    case 0x23u: // NR44
    {
        bool old_len_en;
        bool trigger;
        bool length_reloaded;

        old_len_en      = apu->ch4_len_en;
        apu->ch4_len_en = (val & 0x40u) != 0u;
        trigger         = (val & 0x80u) != 0u;

        if (!cupid_gb_apu_next_step_clocks_length(apu) && !old_len_en && apu->ch4_len_en) {
            cupid_gb_apu_clock_length_once_u8(&apu->ch4_len, &apu->ch4_on);
        }

        length_reloaded = apu->ch4_len == 0u;
        if (trigger) {
            cupid_gb_apu_trigger_ch4(gb);
            if (!cupid_gb_apu_next_step_clocks_length(apu) && apu->ch4_len_en && length_reloaded) {
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
 *   - Decrements and fires the frame sequencer (every 8192 T-cycles)
 *   - Clocks CH1 and CH2 frequency timers and advances their duty positions
 *   - Clocks the CH3 wave timer and fetches the next 4-bit wave sample
 *   - Clocks the CH4 LFSR noise timer and shifts the LFSR
 *   - Accumulates samples and calls @ref cupid_gb_apu_emit_sample at
 *     @ref CUPID_GB_APU_SAMPLE_RATE (44100 Hz)
 *
 * @param gb     Pointer to the Game Boy state.
 * @param cycles Number of T-cycles to advance.
 */
void cupid_gb_tick_apu(CupidGb *gb, uint16_t cycles)
{
    CupidGbApu *apu = &gb->apu;
    uint16_t c;

    for (c = 0u; c < cycles; ++c) {
        if (apu->ch3_wave_access_ticks > 0u) {
            apu->ch3_wave_access_ticks = (uint8_t)(apu->ch3_wave_access_ticks - 1u);
        }

        // Frame sequencer: fires every 8192 T-cycles at 512 Hz
        if (apu->fs_counter > 0u) {
            apu->fs_counter = (uint16_t)(apu->fs_counter - 1u);
        } else {
            apu->fs_counter = 8191u;
            if (apu->apu_on) {
                cupid_gb_apu_clock_fs(apu, apu->fs_step);
                if (apu->fs_step == 2u || apu->fs_step == 6u) {
                    cupid_gb_apu_clock_sweep(gb);
                }
            }
            apu->fs_step = (uint8_t)((apu->fs_step + 1u) & 7u);
        }

        if (apu->apu_on) {
            // CH1 frequency timer
            if (apu->ch1_timer > 0u) {
                apu->ch1_timer = (uint16_t)(apu->ch1_timer - 1u);
            } else {
                apu->ch1_timer = (uint16_t)((2048u - (uint32_t)apu->ch1_freq) * 4u);
                apu->ch1_duty_pos = (uint8_t)((apu->ch1_duty_pos + 1u) & 7u);
            }

            // CH2 frequency timer
            if (apu->ch2_timer > 0u) {
                apu->ch2_timer = (uint16_t)(apu->ch2_timer - 1u);
            } else {
                apu->ch2_timer = (uint16_t)((2048u - (uint32_t)apu->ch2_freq) * 4u);
                apu->ch2_duty_pos = (uint8_t)((apu->ch2_duty_pos + 1u) & 7u);
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
                    if (period == 0u) { period = 2u; }
                    apu->ch3_timer = period;
                }
            }

            // CH4 LFSR timer
            if (apu->ch4_timer > 0u) {
                apu->ch4_timer = apu->ch4_timer - 1u;
            } else {
                uint16_t xor_bit = (uint16_t)((apu->ch4_lfsr ^ (apu->ch4_lfsr >> 1u)) & 1u);
                apu->ch4_lfsr = (uint16_t)((apu->ch4_lfsr >> 1u) | (uint16_t)(xor_bit << 14u));
                if (apu->ch4_width7) {
                    apu->ch4_lfsr = (uint16_t)((apu->ch4_lfsr & (uint16_t)~0x40u)
                                                | (uint16_t)(xor_bit << 6u));
                }
                {
                    uint32_t noise_period = (uint32_t)cupid_gb_noise_div[apu->ch4_div_code];
                    if (apu->ch4_shift < 14u) {
                        noise_period = noise_period << apu->ch4_shift;
                    } else {
                        noise_period = noise_period << 13u;
                    }
                    apu->ch4_timer = (noise_period > 0u) ? noise_period : 8u;
                }
            }
        }

        // Output one sample at 44100 Hz
        apu->sample_acc += CUPID_GB_APU_SAMPLE_RATE;
        if (apu->sample_acc >= 4194304u) {
            apu->sample_acc -= 4194304u;
            cupid_gb_apu_emit_sample(gb);
        }
    }
}


/**
 * @brief Drains accumulated audio samples from the internal buffer.
 *
 * Copies up to @p max_frames stereo sample frames from the APU's internal
 * buffer into @p out, then resets the write pointer. Each frame consists
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
uint32_t cupid_gb_apu_drain(CupidGb *gb, int16_t *out, uint32_t max_frames)
{
    CupidGbApu *apu;
    uint32_t count;

    if (gb == 0 || out == 0) { return 0u; }
    apu   = &gb->apu;
    count = (apu->buf_write < max_frames) ? apu->buf_write : max_frames;
    if (count > 0u) {
        memcpy(out, apu->buf, (size_t)(count * 2u * sizeof(int16_t)));
        apu->buf_write = 0u;
    }
    return count;
}

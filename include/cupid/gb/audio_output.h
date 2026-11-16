/**
 * @file    audio_output.h
 * @brief   Host audio reconstruction history, policy, and queued stereo frames.
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
#ifndef CUPID_GB_AUDIO_OUTPUT_H
#define CUPID_GB_AUDIO_OUTPUT_H

#include "cupid/gb/audio_types.h"

#define CUPID_GB_APU_SAMPLE_RATE 44100u
#define CUPID_GB_APU_BUF_FRAMES 2048u

/* Host output history is separate from the sound-channel clocks. Exact native
 * snapshots include both; live frontend restores retain this receiving history. */
typedef struct CupidGbAudioOutput {
    CupidGbAudioChannel channels[CUPID_GB_AUDIO_CHANNELS];
    CupidGbAudioFilterState filter;
    CupidGbAudioPolicy policy;
    double dac_charge[CUPID_GB_AUDIO_CHANNELS];
    uint8_t dac_gain_delay[CUPID_GB_AUDIO_DAC_DELAY][CUPID_GB_AUDIO_CHANNELS];
    uint8_t dac_enable_delay[CUPID_GB_AUDIO_DAC_DELAY];
    uint8_t dac_delay_cursor;
    /* Rational delivery credit and reconstruction phase have distinct clocks.
     * A frozen CPU segment can add credit without delivering an output frame. */
    uint32_t sample_acc;
    uint32_t sample_fraction; /* Q4.28 frames. */
    uint32_t render_cycles;   /* Base T-cycles since the last synthesis read. */
    uint32_t sample_rate;     /* Zero selects the default rate on first use. */
    uint32_t fraction_step;   /* Q4.28 increment per two base T-cycles. */
    uint16_t max_span;        /* Maximum base T-cycles between synthesis reads. */
    bool fraction_half;       /* Retains an odd direct-APU tick without rounding drift. */
    uint32_t noise_state;     /* Per-instance optional interference sequence; zero is its seed. */
    int16_t buf[CUPID_GB_APU_BUF_FRAMES * 2u]; /* Interleaved left/right frames. */
    uint32_t buf_write;
} CupidGbAudioOutput;

#endif

/**
 * @file    audio_state.h
 * @brief   Portable encoding and validation of reconstructed audio history.
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

#ifndef CUPID_GB_AUDIO_STATE_H
#define CUPID_GB_AUDIO_STATE_H

#include "cupid/gb/gb.h"

/* The old wire prefix retains the queue, rational credit, sample rate and
 * disabled-filter flag. V14 appends the remaining output and channel history. */
enum {
    CUPID_GB_AUDIO_STATE_CHANNEL_SIZE = 2 * CUPID_GB_AUDIO_RING_FRAMES * 4 + 8 + 4 + 4 + 2 + 1,
    CUPID_GB_AUDIO_STATE_SIZE = CUPID_GB_AUDIO_CHANNELS * CUPID_GB_AUDIO_STATE_CHANNEL_SIZE + 32 +
                                10 + 32 + CUPID_GB_AUDIO_DAC_DELAY * (CUPID_GB_AUDIO_CHANNELS + 1) +
                                1 + 4 + 4 + 4 + 2 + 1 + 4 + CUPID_GB_AUDIO_CHANNELS + 2 + 2
};

bool cupid_gb_audio_state_valid(const CupidGb *gb);
bool cupid_gb_audio_state_encode(const CupidGb *gb, uint8_t *data, size_t length);
bool cupid_gb_audio_state_decode(CupidGb *gb, const uint8_t *data, size_t length,
                                 bool startup_filter_flag);

#endif

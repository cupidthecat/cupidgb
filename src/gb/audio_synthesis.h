/**
 * @file    audio_synthesis.h
 * @brief   Internal channel reconstruction and signed sample conversion interfaces.
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
#ifndef CUPID_GB_AUDIO_SYNTHESIS_H
#define CUPID_GB_AUDIO_SYNTHESIS_H

#include "cupid/gb/audio_types.h"

/* Interpret the low bits explicitly; signed narrowing and negative shifts are
 * implementation-defined in the C versions supported by the project. */
static inline int16_t cupid_gb_audio_narrow16(int64_t value) {
    const uint16_t bits = (uint16_t)(uint64_t)value;
    return bits <= INT16_MAX ? (int16_t)bits : (int16_t)((int32_t)bits - 65536);
}

const int32_t *cupid_gb_audio_kernel(unsigned fraction);
/* Inputs are routed channel voltages in [-120, 120]. Phase units are 1/256
 * frame; values below 1024 include up to three frames of pending clock work. */
void cupid_gb_audio_synth_update(CupidGbAudioChannel *channel, CupidGbAudioFrame input,
                                 unsigned phase);
CupidGbAudioFrame cupid_gb_audio_synth_read(CupidGbAudioChannel *channel, unsigned gain);

#endif

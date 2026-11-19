/**
 * @file    audio_filter.h
 * @brief   Internal stereo filter contracts and imported-history bounds.
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
#ifndef CUPID_GB_AUDIO_FILTER_H
#define CUPID_GB_AUDIO_FILTER_H

#include "cupid/gb/audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A conservative import bound, not an output clip. It leaves ample headroom for
 * signed 16-bit history while keeping every floating-to-integer conversion safe. */
enum { CUPID_GB_AUDIO_FILTER_HISTORY_LIMIT = 65536 };

/* A zero rate retains the previous coefficient. Neither path resets history. */
void cupid_gb_audio_filter_set_rate(CupidGbAudioFilterState *state, uint32_t clock_hz,
                                    uint32_t sample_rate);

void cupid_gb_audio_filter_settle(CupidGbAudioFilterState *state, const CupidGbAudioPolicy *policy,
                                  CupidGbAudioFrame mixed, uint8_t enabled_dacs, uint8_t nr50,
                                  uint8_t nr51);

/* Histories must be finite and within HISTORY_LIMIT; retention and interference
 * volume must be finite in [0, 1]. The two filter flags are mutually exclusive.
 * enabled_dacs is the effective four-bit mask after any monochrome DAC delay.
 * Muting acts upstream on synthesis; it does not remove a DAC from the DC path.
 * random_value is supplied by the caller and is ignored at zero interference. */
CupidGbAudioFrame cupid_gb_audio_filter_frame(CupidGbAudioFilterState *state,
                                              const CupidGbAudioPolicy *policy,
                                              const CupidGbAudioConditions *conditions,
                                              int32_t left, int32_t right, uint8_t enabled_dacs,
                                              uint8_t nr50, uint8_t nr51, uint32_t random_value);

#ifdef __cplusplus
}
#endif

#endif

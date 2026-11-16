/**
 * @file    audio_types.h
 * @brief   Portable channel reconstruction, filter, and audio policy types.
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
#ifndef CUPID_GB_AUDIO_TYPES_H
#define CUPID_GB_AUDIO_TYPES_H

#include <stdbool.h>
#include <stdint.h>

enum {
    CUPID_GB_AUDIO_CHANNELS = 4,
    CUPID_GB_AUDIO_PHASES = 256,
    CUPID_GB_AUDIO_TAPS = 64,
    CUPID_GB_AUDIO_RING_FRAMES = 128,
    CUPID_GB_AUDIO_DAC_DELAY = 32
};

typedef enum CupidGbAudioFilter {
    CUPID_GB_AUDIO_FILTER_OFF = 0,
    CUPID_GB_AUDIO_FILTER_ACCURATE = 1,
    CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM = 2
} CupidGbAudioFilter;

typedef struct CupidGbAudioFrame {
    int16_t left;
    int16_t right;
} CupidGbAudioFrame;

/* Pending step responses and their running integral are Q16 channel voltages.
 * Routing and master gain precede this stage; DAC gain follows it. */
typedef struct CupidGbAudioChannel {
    int32_t pending[2][CUPID_GB_AUDIO_RING_FRAMES];
    int32_t level[2];
    int16_t input[2];
    CupidGbAudioFrame anchor;
    uint16_t quiet_frames;
    uint8_t cursor;
} CupidGbAudioChannel;

/* Zero initialization selects Accurate filtering, no interference and all
 * channels audible. The two filter flags must never both be set. */
typedef struct CupidGbAudioPolicy {
    double interference_volume;
    uint8_t muted_channels;
    bool filter_disabled;
    bool preserve_waveform;
} CupidGbAudioPolicy;

typedef struct CupidGbAudioFilterState {
    double capacitor[2];
    double interference;
    double retention;
} CupidGbAudioFilterState;

/* Inputs to optional hardware-interference synthesis at an output boundary. */
typedef struct CupidGbAudioConditions {
    bool color_hardware;
    bool halted;
    bool lcd_enabled;
    bool apu_enabled;
    bool infrared_output;
    uint8_t lcd_mode;
} CupidGbAudioConditions;

#endif

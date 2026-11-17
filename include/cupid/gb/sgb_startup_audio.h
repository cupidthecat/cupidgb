/**
 * @file    sgb_startup_audio.h
 * @brief   Retained adapter startup synthesis and ordered host audio delivery.
 * @author  cupidthecat
 * @date    2026-11-17
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
#ifndef CUPID_GB_SGB_STARTUP_AUDIO_H
#define CUPID_GB_SGB_STARTUP_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define CUPID_GB_SGB_AUDIO_VOICES 7u
#define CUPID_GB_SGB_AUDIO_BATCH_FRAMES 3214u
#define CUPID_GB_SGB_AUDIO_BATCHES 2u
#define CUPID_GB_SGB_AUDIO_FILTER_LIMIT 1048576
#define CUPID_GB_AUDIO_DRAIN_WATERMARK 1536u

struct CupidGb;

typedef struct CupidGbSgbStartupBatch {
    int16_t generation_phase; /* -10..199; independent of the current hardware phase. */
    uint32_t rate;            /* Effective output rate when this batch was generated. */
    uint16_t length;          /* Original batch length, before any drain. */
    uint16_t cursor;          /* Next unread mono frame; always less than length. */
    int16_t mono[CUPID_GB_SGB_AUDIO_BATCH_FRAMES];
} CupidGbSgbStartupBatch;

/* This entire owner travels with ordinary output history during a live restore.
 * Oscillator phases use unsigned Q0.32 cycles; drains never advance synthesis.
 * Wire codecs store explicit fields and unread samples, never this struct image. */
typedef struct CupidGbSgbStartupAudio {
    uint32_t voice_phase[CUPID_GB_SGB_AUDIO_VOICES];
    uint32_t sweep_phase;
    int32_t sweep_filter_q16; /* Signed Q16; bounded by CUPID_GB_SGB_AUDIO_FILTER_LIMIT. */
    uint64_t noise_state;     /* A private, nonzero stream; no handheld RNG consumption. */
    uint64_t dropped_frames;  /* Saturating count; cleared by an explicit startup reset. */
    uint16_t normal_prefix_frames;
    uint8_t batch_count;
    CupidGbSgbStartupBatch batches[CUPID_GB_SGB_AUDIO_BATCHES];
} CupidGbSgbStartupAudio;

typedef enum CupidGbSgbAudioEnqueue {
    CUPID_GB_SGB_AUDIO_NONE = 0,
    CUPID_GB_SGB_AUDIO_QUEUED,
    CUPID_GB_SGB_AUDIO_DROPPED,
    CUPID_GB_SGB_AUDIO_INVALID
} CupidGbSgbAudioEnqueue;

typedef enum CupidGbAudioOrigin {
    CUPID_GB_AUDIO_ORIGIN_HANDHELD = 0,
    CUPID_GB_AUDIO_ORIGIN_SGB_STARTUP
} CupidGbAudioOrigin;

typedef struct CupidGbAudioPending {
    uint32_t handheld_frames;
    uint32_t startup_frames;
} CupidGbAudioPending;

#ifdef __cplusplus
extern "C" {
#endif

/* A zero seed maps to one. Legacy imports use one directly, independent of the
 * receiving machine. Every reset clears synthesis, queued output, and drops. */
void cupid_gb_sgb_startup_audio_reset(CupidGbSgbStartupAudio *audio, uint64_t generation_seed);

/* Returns zero for unsupported rates or clocks. Each delivery truncates its own
 * rational frame count; there is no carried fractional sample remainder. */
uint32_t cupid_gb_sgb_startup_audio_frame_count(uint32_t rate, uint32_t base_clock);

/* Pure validation: no clocks, output, or random streams change. The caller checks
 * physical adapter presence. Rate is the effective ordinary output rate. */
bool cupid_gb_sgb_startup_audio_valid(const CupidGbSgbStartupAudio *audio, uint32_t ordinary_frames,
                                      uint32_t rate, uint32_t base_clock);

/* The presentation owner supplies the old phase once per actual presentation.
 * Phase 200 emits nothing. A full queue, or an older intro with an ordinary
 * suffix, drops only this new batch after advancing synthesis. No guest or
 * presentation clock stalls, and existing queued frames are never overwritten. */
CupidGbSgbAudioEnqueue cupid_gb_sgb_startup_audio_enqueue(CupidGbSgbStartupAudio *audio,
                                                          uint32_t ordinary_frames, int phase,
                                                          uint32_t rate, uint32_t base_clock);

uint32_t cupid_gb_sgb_startup_audio_pending(const CupidGbSgbStartupAudio *audio);

/* A chunk has exactly one origin. Playback consumes both origins; handheld WAVE
 * recording consumes HANDHELD only. Drain until zero before advancing with any
 * startup output pending, at the ordinary watermark, and on every stop/exit.
 * The stream order is ordinary prefix, startup batches, then ordinary suffix. */
CupidGbAudioPending cupid_gb_apu_pending(const struct CupidGb *gb);
uint32_t cupid_gb_apu_drain_chunk(struct CupidGb *gb, int16_t *out, uint32_t max_frames,
                                  CupidGbAudioOrigin *origin);

#ifdef __cplusplus
}
#endif

#endif

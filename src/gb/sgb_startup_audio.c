/**
 * @file    sgb_startup_audio.c
 * @brief   Deterministic adapter startup voices, bounded batches, and tagged audio drains.
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
#include "cupid/gb/sgb_startup_audio.h"

#include "cupid/gb/gb.h"

#include <limits.h>
#include <string.h>

enum {
    SGB_CLOCK_HZ = 4295454,
    SGB2_CLOCK_HZ = 4194304,
    STARTUP_FRAME_CYCLES = 70224,
    STARTUP_HOLD_THRESHOLD = 2048,
    Q16_ONE = 65536
};

typedef struct StartupFrameShape {
    uint32_t voice_step[CUPID_GB_SGB_AUDIO_VOICES];
    uint32_t voice_gain_q16[CUPID_GB_SGB_AUDIO_VOICES];
    uint32_t tone_fade_q16;
    uint32_t sweep_step;
    uint32_t sweep_gain_q16;
    uint32_t sweep_filter_q16;
    unsigned voices;
    unsigned hold;
} StartupFrameShape;

void cupid_gb_sgb_startup_audio_reset(CupidGbSgbStartupAudio *audio, uint64_t generation_seed) {
    if (!audio)
        return;
    memset(audio, 0, sizeof(*audio));
    audio->noise_state = generation_seed ? generation_seed : UINT64_C(1);
}

uint32_t cupid_gb_sgb_startup_audio_frame_count(uint32_t rate, uint32_t base_clock) {
    if (rate < 8000u || rate > 192000u ||
        (base_clock != SGB_CLOCK_HZ && base_clock != SGB2_CLOCK_HZ))
        return 0;
    return (uint32_t)(((uint64_t)rate * STARTUP_FRAME_CYCLES) / base_clock);
}

bool cupid_gb_sgb_startup_audio_valid(const CupidGbSgbStartupAudio *audio, uint32_t ordinary_frames,
                                      uint32_t rate, uint32_t base_clock) {
    const uint32_t length = cupid_gb_sgb_startup_audio_frame_count(rate, base_clock);
    if (!audio || !length || ordinary_frames > CUPID_GB_APU_BUF_FRAMES || !audio->noise_state ||
        audio->sweep_filter_q16 < -CUPID_GB_SGB_AUDIO_FILTER_LIMIT ||
        audio->sweep_filter_q16 > CUPID_GB_SGB_AUDIO_FILTER_LIMIT ||
        audio->batch_count > CUPID_GB_SGB_AUDIO_BATCHES ||
        audio->normal_prefix_frames > ordinary_frames)
        return false;
    if (!audio->batch_count && audio->normal_prefix_frames)
        return false;
    for (unsigned index = 0; index < audio->batch_count; ++index) {
        const CupidGbSgbStartupBatch *batch = &audio->batches[index];
        if (batch->generation_phase < -10 || batch->generation_phase >= 200 ||
            batch->rate != rate || batch->length != length || batch->cursor >= batch->length)
            return false;
    }
    return true;
}

/* A parabolic half-wave uses only normalized phase and exact integer products.
 * The 6:2:1 harmonic blend is original voice shaping; it needs no sampled asset
 * or shared mutable lookup table. Signed divisions round toward zero. */
static int32_t rounded_wave(uint32_t phase) {
    const uint32_t position = (phase & UINT32_C(0x7fffffff)) >> 15;
    const int32_t magnitude = (int32_t)((UINT64_C(4) * position * (Q16_ONE - position)) / Q16_ONE);
    return phase & UINT32_C(0x80000000) ? -magnitude : magnitude;
}

static int32_t voiced_wave(uint32_t phase) {
    const int32_t fundamental = rounded_wave(phase);
    const int32_t second = rounded_wave(phase * UINT32_C(2));
    const int32_t third = rounded_wave(phase * UINT32_C(3));
    return (6 * fundamental + 2 * second + third) / 9;
}

/* Nine nonconstant Taylor terms evaluate 2^(phase / period) without host libm rounding.
 * ln(2) is rounded to Q30; the result is Q24. Only phases 0..119 are used. */
static uint64_t octave_gain_q24(unsigned phase, unsigned period) {
    const uint64_t one = UINT64_C(1) << 30;
    const uint64_t exponent = (uint64_t)(phase % period) * UINT64_C(744261118) / period;
    uint64_t term = one;
    uint64_t sum = one;
    for (unsigned divisor = 1; divisor <= 9; ++divisor) {
        term = (term * exponent / one) / divisor;
        sum += term;
    }
    return (sum >> 6) << (phase / period);
}

static uint32_t pitch_step(uint32_t cents_hz, uint32_t rate, unsigned hold) {
    const uint64_t denominator = (uint64_t)rate * 100u;
    const uint64_t numerator = (uint64_t)cents_hz * (UINT64_C(1) << 32) * hold;
    return (uint32_t)((numerator + denominator / 2u) / denominator);
}

static StartupFrameShape frame_shape(int phase, uint32_t rate, uint32_t length) {
    static const uint32_t pitches[] = {46616u, 58733u, 69846u, 83061u, 104650u, 124451u, 156798u};
    StartupFrameShape shape = {0};
    shape.hold = length > STARTUP_HOLD_THRESHOLD ? 2u : 1u;
    shape.tone_fade_q16 = Q16_ONE;
    if (phase > 100) {
        const uint64_t remaining = (uint64_t)(200 - phase);
        shape.tone_fade_q16 =
            (uint32_t)(remaining * remaining * remaining * Q16_ONE / UINT64_C(1000000));
    }
    if (phase >= 67) {
        const unsigned entries = (unsigned)(phase - 64) / 3u;
        shape.voices = entries < CUPID_GB_SGB_AUDIO_VOICES ? entries : CUPID_GB_SGB_AUDIO_VOICES;
        for (unsigned voice = 0; voice < shape.voices; ++voice) {
            const unsigned age = entries - voice;
            const uint32_t transient = age < 32u ? UINT32_C(49152) >> age : 0;
            shape.voice_gain_q16[voice] = (UINT32_C(16384) + transient + 2u) / 5u;
            shape.voice_step[voice] = pitch_step(pitches[voice], rate, shape.hold);
        }
    }
    if (phase < 120) {
        const uint64_t pitch = octave_gain_q24((unsigned)phase, 40);
        const uint64_t cutoff = octave_gain_q24((unsigned)phase, 20);
        shape.sweep_step =
            (uint32_t)((UINT64_C(1000) * pitch * 256u * shape.hold + rate / 2u) / rate);
        uint64_t coefficient =
            UINT64_C(2000) * cutoff * Q16_ONE / ((uint64_t)rate * (UINT64_C(1) << 24));
        if (coefficient > Q16_ONE)
            coefficient = Q16_ONE;
        /* The held-sample path doubles the synthesis interval after limiting the
         * ordinary-rate coefficient. Keep that order, including coefficients
         * above unity; the retained state has an explicit saturation bound. */
        shape.sweep_filter_q16 = (uint32_t)coefficient * shape.hold;
        const uint32_t remaining = (uint32_t)(120 - phase);
        shape.sweep_gain_q16 = remaining * remaining * UINT32_C(52429) / 14400u;
    }
    return shape;
}

static int32_t next_noise(CupidGbSgbStartupAudio *audio) {
    uint64_t state = audio->noise_state;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    audio->noise_state = state;
    return (int32_t)(state >> 47) - Q16_ONE;
}

static int16_t synthesize(CupidGbSgbStartupAudio *audio, const StartupFrameShape *shape) {
    int64_t tone_q16 = 0;
    for (unsigned voice = 0; voice < shape->voices; ++voice) {
        tone_q16 += (int64_t)voiced_wave(audio->voice_phase[voice]) * shape->voice_gain_q16[voice] /
                    Q16_ONE;
        audio->voice_phase[voice] += shape->voice_step[voice];
    }
    int64_t mixed_q16 = tone_q16 * shape->tone_fade_q16 / Q16_ONE;
    if (shape->sweep_gain_q16) {
        const int32_t drive = (3 * voiced_wave(audio->sweep_phase) + 7 * next_noise(audio)) / 10;
        audio->sweep_phase += shape->sweep_step;
        const int64_t difference = (int64_t)drive - audio->sweep_filter_q16;
        int64_t filtered = audio->sweep_filter_q16 + difference * shape->sweep_filter_q16 / Q16_ONE;
        if (filtered < -CUPID_GB_SGB_AUDIO_FILTER_LIMIT)
            filtered = -CUPID_GB_SGB_AUDIO_FILTER_LIMIT;
        else if (filtered > CUPID_GB_SGB_AUDIO_FILTER_LIMIT)
            filtered = CUPID_GB_SGB_AUDIO_FILTER_LIMIT;
        audio->sweep_filter_q16 = (int32_t)filtered;
        mixed_q16 += filtered * shape->sweep_gain_q16 / Q16_ONE;
    }
    const int64_t pcm = mixed_q16 * 28672 / Q16_ONE;
    if (pcm < INT16_MIN)
        return INT16_MIN;
    if (pcm > INT16_MAX)
        return INT16_MAX;
    return (int16_t)pcm;
}

static void render_batch(CupidGbSgbStartupAudio *audio, int phase, uint32_t rate, uint32_t length,
                         int16_t *mono) {
    if (phase < 0) {
        if (mono)
            memset(mono, 0, (size_t)length * sizeof(*mono));
        return;
    }
    const StartupFrameShape shape = frame_shape(phase, rate, length);
    for (uint32_t frame = 0; frame < length; frame += shape.hold) {
        const int16_t sample = synthesize(audio, &shape);
        if (mono) {
            mono[frame] = sample;
            if (shape.hold == 2u && frame + 1u < length)
                mono[frame + 1u] = sample;
        }
    }
}

CupidGbSgbAudioEnqueue cupid_gb_sgb_startup_audio_enqueue(CupidGbSgbStartupAudio *audio,
                                                          uint32_t ordinary_frames, int phase,
                                                          uint32_t rate, uint32_t base_clock) {
    if (phase < -10 || phase > 200 ||
        !cupid_gb_sgb_startup_audio_valid(audio, ordinary_frames, rate, base_clock))
        return CUPID_GB_SGB_AUDIO_INVALID;
    if (phase == 200)
        return CUPID_GB_SGB_AUDIO_NONE;
    const uint32_t length = cupid_gb_sgb_startup_audio_frame_count(rate, base_clock);
    if (audio->batch_count == CUPID_GB_SGB_AUDIO_BATCHES ||
        (audio->batch_count && ordinary_frames > audio->normal_prefix_frames)) {
        render_batch(audio, phase, rate, length, NULL);
        audio->dropped_frames = UINT64_MAX - audio->dropped_frames < length
                                    ? UINT64_MAX
                                    : audio->dropped_frames + length;
        return CUPID_GB_SGB_AUDIO_DROPPED;
    }
    if (!audio->batch_count)
        audio->normal_prefix_frames = (uint16_t)ordinary_frames;
    CupidGbSgbStartupBatch *batch = &audio->batches[audio->batch_count];
    memset(batch, 0, sizeof(*batch));
    batch->generation_phase = (int16_t)phase;
    batch->rate = rate;
    batch->length = (uint16_t)length;
    render_batch(audio, phase, rate, length, batch->mono);
    ++audio->batch_count;
    return CUPID_GB_SGB_AUDIO_QUEUED;
}

uint32_t cupid_gb_sgb_startup_audio_pending(const CupidGbSgbStartupAudio *audio) {
    if (!audio || audio->batch_count > CUPID_GB_SGB_AUDIO_BATCHES)
        return 0;
    uint32_t count = 0;
    for (unsigned index = 0; index < audio->batch_count; ++index) {
        const CupidGbSgbStartupBatch *batch = &audio->batches[index];
        if (batch->length > CUPID_GB_SGB_AUDIO_BATCH_FRAMES || batch->cursor >= batch->length)
            return 0;
        count += (uint32_t)batch->length - batch->cursor;
    }
    return count;
}

CupidGbAudioPending cupid_gb_apu_pending(const CupidGb *gb) {
    if (!gb)
        return (CupidGbAudioPending){0, 0};
    return (CupidGbAudioPending){gb->apu.output.buf_write,
                                 cupid_gb_sgb_startup_audio_pending(&gb->sgb_startup.output)};
}

static uint32_t drain_ordinary(CupidGbAudioOutput *ordinary, int16_t *out, uint32_t limit) {
    const uint32_t count = ordinary->buf_write < limit ? ordinary->buf_write : limit;
    if (count) {
        memcpy(out, ordinary->buf, (size_t)count * 2u * sizeof(*out));
        ordinary->buf_write -= count;
        memmove(ordinary->buf, ordinary->buf + count * 2u,
                (size_t)ordinary->buf_write * 2u * sizeof(*out));
    }
    return count;
}

uint32_t cupid_gb_apu_drain_chunk(CupidGb *gb, int16_t *out, uint32_t max_frames,
                                  CupidGbAudioOrigin *origin) {
    if (!gb || !out || !max_frames)
        return 0;
    CupidGbAudioOutput *ordinary = &gb->apu.output;
    CupidGbSgbStartupAudio *audio = &gb->sgb_startup.output;
    if (ordinary->buf_write > CUPID_GB_APU_BUF_FRAMES ||
        audio->batch_count > CUPID_GB_SGB_AUDIO_BATCHES ||
        audio->normal_prefix_frames > ordinary->buf_write)
        return 0;
    if (!audio->batch_count || audio->normal_prefix_frames) {
        const uint32_t limit = audio->batch_count && audio->normal_prefix_frames < max_frames
                                   ? audio->normal_prefix_frames
                                   : max_frames;
        const uint32_t count = drain_ordinary(ordinary, out, limit);
        if (audio->batch_count)
            audio->normal_prefix_frames -= (uint16_t)count;
        if (origin)
            *origin = CUPID_GB_AUDIO_ORIGIN_HANDHELD;
        return count;
    }
    CupidGbSgbStartupBatch *batch = &audio->batches[0];
    if (batch->length > CUPID_GB_SGB_AUDIO_BATCH_FRAMES || batch->cursor >= batch->length)
        return 0;
    const uint32_t remaining = (uint32_t)batch->length - batch->cursor;
    const uint32_t count = remaining < max_frames ? remaining : max_frames;
    for (uint32_t frame = 0; frame < count; ++frame)
        out[frame * 2u] = out[frame * 2u + 1u] = batch->mono[batch->cursor + frame];
    batch->cursor += (uint16_t)count;
    if (batch->cursor == batch->length) {
        --audio->batch_count;
        if (audio->batch_count)
            memmove(audio->batches, audio->batches + 1,
                    (size_t)audio->batch_count * sizeof(*audio->batches));
        memset(&audio->batches[audio->batch_count], 0, sizeof(*audio->batches));
        if (!audio->batch_count)
            audio->normal_prefix_frames = 0;
    }
    if (origin)
        *origin = CUPID_GB_AUDIO_ORIGIN_SGB_STARTUP;
    return count;
}

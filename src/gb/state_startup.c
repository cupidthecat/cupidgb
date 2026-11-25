/**
 * @file    state_startup.c
 * @brief   Portable startup clock, synthesis, and two-batch native state codec.
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

#include "state_startup.h"

#include "cupid/gb/apu.h"
#include "state_io.h"

#include <limits.h>
#include <string.h>

_Static_assert(CUPID_GB_SGB_AUDIO_VOICES == 7 && CUPID_GB_SGB_AUDIO_BATCHES == 2 &&
                   CUPID_GB_SGB_AUDIO_BATCH_FRAMES == 3214,
               "Startup field counts belong to the versioned native format");
_Static_assert(CUPID_GB_STARTUP_STATE_MAX_SIZE == 12941,
               "Startup wire bound must cover two complete high-rate batches");

typedef struct StartupBatchFields {
    int16_t phase;
    uint32_t rate;
    uint16_t length;
    uint16_t cursor;
} StartupBatchFields;

typedef struct StartupFields {
    int16_t phase;
    uint64_t elapsed_cycles;
    uint32_t voice_phase[CUPID_GB_SGB_AUDIO_VOICES];
    uint32_t sweep_phase;
    int32_t sweep_filter_q16;
    uint64_t noise_state;
    uint64_t dropped_frames;
    uint16_t normal_prefix_frames;
    uint8_t batch_count;
    StartupBatchFields batches[CUPID_GB_SGB_AUDIO_BATCHES];
} StartupFields;

static void signed_word(StateIo *io, int16_t *value) {
    uint16_t raw = (uint16_t)*value;
    word(io, &raw);
    if (io->input)
        *value = raw <= INT16_MAX ? (int16_t)raw : (int16_t)((int32_t)raw - 65536);
}

static void signed_double_word(StateIo *io, int32_t *value) {
    uint32_t raw = (uint32_t)*value;
    double_word(io, &raw);
    if (io->input)
        *value = raw <= INT32_MAX ? (int32_t)raw : -1 - (int32_t)(UINT32_MAX - raw);
}

static void header_fields(StateIo *io, StartupFields *fields) {
    signed_word(io, &fields->phase);
    quad_word(io, &fields->elapsed_cycles);
    for (unsigned voice = 0; voice < CUPID_GB_SGB_AUDIO_VOICES; ++voice)
        double_word(io, &fields->voice_phase[voice]);
    double_word(io, &fields->sweep_phase);
    signed_double_word(io, &fields->sweep_filter_q16);
    quad_word(io, &fields->noise_state);
    quad_word(io, &fields->dropped_frames);
    word(io, &fields->normal_prefix_frames);
    byte(io, &fields->batch_count);
}

static void batch_fields(StateIo *io, StartupBatchFields *batch) {
    signed_word(io, &batch->phase);
    double_word(io, &batch->rate);
    word(io, &batch->length);
    word(io, &batch->cursor);
}

static bool descriptor_valid(const StartupBatchFields *batch, uint32_t clock) {
    const uint32_t frames = cupid_gb_sgb_startup_audio_frame_count(batch->rate, clock);
    return batch->phase >= -10 && batch->phase < 200 && frames &&
           frames <= CUPID_GB_SGB_AUDIO_BATCH_FRAMES && batch->length == frames &&
           batch->cursor < batch->length;
}

static bool fields_size(const StartupFields *fields, uint32_t clock, size_t *length) {
    if (fields->phase < -10 || fields->phase > 200 || !fields->noise_state ||
        fields->sweep_filter_q16 < -CUPID_GB_SGB_AUDIO_FILTER_LIMIT ||
        fields->sweep_filter_q16 > CUPID_GB_SGB_AUDIO_FILTER_LIMIT ||
        fields->batch_count > CUPID_GB_SGB_AUDIO_BATCHES ||
        fields->normal_prefix_frames > CUPID_GB_APU_BUF_FRAMES ||
        (!fields->batch_count && fields->normal_prefix_frames))
        return false;

    size_t size = CUPID_GB_STARTUP_STATE_HEADER_SIZE +
                  (size_t)fields->batch_count * CUPID_GB_STARTUP_STATE_BATCH_SIZE;
    for (unsigned index = 0; index < fields->batch_count; ++index) {
        const StartupBatchFields *batch = &fields->batches[index];
        if (!descriptor_valid(batch, clock))
            return false;
        /* Both operands were bounded before subtraction. At most two batches
         * contribute 6,428 samples, independent of an untrusted length field. */
        size += 2u * (batch->length - batch->cursor);
    }
    if (size > CUPID_GB_STARTUP_STATE_MAX_SIZE)
        return false;
    *length = size;
    return true;
}

static void capture_fields(const CupidGbSgbStartup *startup, StartupFields *fields) {
    const CupidGbSgbStartupAudio *audio = &startup->output;
    *fields = (StartupFields){0};
    fields->phase = startup->phase;
    fields->elapsed_cycles = startup->elapsed_cycles;
    memcpy(fields->voice_phase, audio->voice_phase, sizeof(fields->voice_phase));
    fields->sweep_phase = audio->sweep_phase;
    fields->sweep_filter_q16 = audio->sweep_filter_q16;
    fields->noise_state = audio->noise_state;
    fields->dropped_frames = audio->dropped_frames;
    fields->normal_prefix_frames = audio->normal_prefix_frames;
    fields->batch_count = audio->batch_count;
    /* Called only after the complete output validator bounds batch_count. */
    for (unsigned index = 0; index < audio->batch_count; ++index) {
        const CupidGbSgbStartupBatch *source = &audio->batches[index];
        fields->batches[index] = (StartupBatchFields){source->generation_phase, source->rate,
                                                      source->length, source->cursor};
    }
}

static bool audio_valid(const CupidGb *gb, const CupidGbSgbStartupAudio *audio) {
    if (!cupid_gb_sgb_host_present(gb)) {
        /* With no integrated host there is no startup audio to record. */
        if (audio->sweep_phase || audio->sweep_filter_q16 || audio->noise_state != 1 ||
            audio->dropped_frames || audio->normal_prefix_frames || audio->batch_count)
            return false;
        for (unsigned voice = 0; voice < CUPID_GB_SGB_AUDIO_VOICES; ++voice)
            if (audio->voice_phase[voice])
                return false;
        return true;
    }
    if (!cupid_gb_sgb_startup_audio_valid(audio, gb->apu.output.buf_write,
                                          cupid_gb_apu_get_sample_rate(gb),
                                          cupid_gb_base_clock_hz(gb)))
        return false;
    for (unsigned index = 0; index < audio->batch_count; ++index) {
        const CupidGbSgbStartupBatch *batch = &audio->batches[index];
        if (batch->generation_phase < 0)
            for (unsigned frame = batch->cursor; frame < batch->length; ++frame)
                if (batch->mono[frame])
                    return false;
    }
    return true;
}

static bool startup_valid(const CupidGb *gb, const CupidGbSgbStartup *startup) {
    if (!gb || startup->phase < -10 || startup->phase > 200 ||
        startup->elapsed_cycles > UINT64_MAX - gb->elapsed_cycles ||
        (!cupid_gb_sgb_host_present(gb) && (startup->phase != 200 || startup->elapsed_cycles)))
        return false;
    return audio_valid(gb, &startup->output);
}

bool cupid_gb_sgb_startup_output_valid(const CupidGb *gb) {
    return gb && audio_valid(gb, &gb->sgb_startup.output);
}

bool cupid_gb_sgb_startup_state_valid(const CupidGb *gb) {
    return gb && startup_valid(gb, &gb->sgb_startup);
}

bool cupid_gb_sgb_startup_state_size(const CupidGb *gb, size_t *length) {
    if (!length)
        return false;
    *length = 0;
    if (!cupid_gb_sgb_startup_state_valid(gb))
        return false;
    if (!cupid_gb_sgb_host_present(gb))
        return true;
    StartupFields fields;
    capture_fields(&gb->sgb_startup, &fields);
    return fields_size(&fields, cupid_gb_base_clock_hz(gb), length);
}

static bool inspect_fields(const CupidGb *gb, const uint8_t *data, size_t available,
                           StartupFields *fields, size_t *length) {
    if (!data || available < CUPID_GB_STARTUP_STATE_HEADER_SIZE)
        return false;
    StateIo io = {data, NULL, 0, available, false};
    *fields = (StartupFields){0};
    header_fields(&io, fields);
    if (io.invalid || io.offset != CUPID_GB_STARTUP_STATE_HEADER_SIZE ||
        fields->batch_count > CUPID_GB_SGB_AUDIO_BATCHES)
        return false;
    for (unsigned index = 0; index < fields->batch_count; ++index)
        batch_fields(&io, &fields->batches[index]);
    size_t size = 0;
    if (io.invalid || !fields_size(fields, cupid_gb_base_clock_hz(gb), &size) || size > available)
        return false;
    /* Preflight checks negative-phase silence without allocating or consulting
     * receiving output. Later fields begin after the returned exact byte count. */
    for (unsigned index = 0; index < fields->batch_count; ++index) {
        const StartupBatchFields *batch = &fields->batches[index];
        const size_t bytes_remaining = 2u * (batch->length - batch->cursor);
        if (!reserve(&io, bytes_remaining))
            return false;
        if (batch->phase < 0)
            for (size_t byte_index = 0; byte_index < bytes_remaining; ++byte_index)
                if (data[io.offset + byte_index])
                    return false;
        io.offset += bytes_remaining;
    }
    if (io.invalid || io.offset != size)
        return false;
    *length = size;
    return true;
}

bool cupid_gb_sgb_startup_state_measure(const CupidGb *gb, const uint8_t *data, size_t available,
                                        size_t *length) {
    if (!gb || !length)
        return false;
    *length = 0;
    if (!cupid_gb_sgb_host_present(gb))
        return true;
    StartupFields fields;
    return inspect_fields(gb, data, available, &fields, length);
}

bool cupid_gb_sgb_startup_state_encode(const CupidGb *gb, uint8_t *data, size_t length) {
    size_t required = 0;
    if (!cupid_gb_sgb_startup_state_size(gb, &required) || required != length)
        return false;
    if (!required)
        return true;
    if (!data)
        return false;
    StartupFields fields;
    capture_fields(&gb->sgb_startup, &fields);
    StateIo io = {NULL, data, 0, length, false};
    header_fields(&io, &fields);
    for (unsigned index = 0; index < fields.batch_count; ++index)
        batch_fields(&io, &fields.batches[index]);
    for (unsigned index = 0; index < fields.batch_count; ++index) {
        const CupidGbSgbStartupBatch *batch = &gb->sgb_startup.output.batches[index];
        for (unsigned frame = batch->cursor; frame < batch->length; ++frame) {
            int16_t sample = batch->mono[frame];
            signed_word(&io, &sample);
        }
    }
    return !io.invalid && io.offset == length;
}

bool cupid_gb_sgb_startup_state_decode(CupidGb *gb, const uint8_t *data, size_t length) {
    if (!gb)
        return false;
    if (!cupid_gb_sgb_host_present(gb)) {
        if (length)
            return false;
        cupid_gb_sgb_startup_state_import_legacy(gb);
        return true;
    }

    StartupFields fields;
    size_t required = 0;
    if (!inspect_fields(gb, data, length, &fields, &required) || required != length)
        return false;
    CupidGbSgbStartup next = {0};
    cupid_gb_sgb_startup_audio_reset(&next.output, 1);
    next.phase = fields.phase;
    next.elapsed_cycles = fields.elapsed_cycles;
    memcpy(next.output.voice_phase, fields.voice_phase, sizeof(fields.voice_phase));
    next.output.sweep_phase = fields.sweep_phase;
    next.output.sweep_filter_q16 = fields.sweep_filter_q16;
    next.output.noise_state = fields.noise_state;
    next.output.dropped_frames = fields.dropped_frames;
    next.output.normal_prefix_frames = fields.normal_prefix_frames;
    next.output.batch_count = fields.batch_count;

    const size_t payload = CUPID_GB_STARTUP_STATE_HEADER_SIZE +
                           (size_t)fields.batch_count * CUPID_GB_STARTUP_STATE_BATCH_SIZE;
    StateIo io = {data, NULL, payload, length, false};
    for (unsigned index = 0; index < fields.batch_count; ++index) {
        CupidGbSgbStartupBatch *batch = &next.output.batches[index];
        const StartupBatchFields *saved = &fields.batches[index];
        batch->generation_phase = saved->phase;
        batch->rate = saved->rate;
        batch->length = saved->length;
        batch->cursor = saved->cursor;
        for (unsigned frame = batch->cursor; frame < batch->length; ++frame)
            signed_word(&io, &batch->mono[frame]);
    }
    if (io.invalid || io.offset != length || !startup_valid(gb, &next))
        return false;
    gb->sgb_startup = next;
    return true;
}

void cupid_gb_sgb_startup_state_import_legacy(CupidGb *gb) {
    if (!gb)
        return;
    gb->sgb_startup = (CupidGbSgbStartup){.phase = 200};
    cupid_gb_sgb_startup_audio_reset(&gb->sgb_startup.output, 1);
}

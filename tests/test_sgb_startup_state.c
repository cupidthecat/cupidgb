/**
 * @file    test_sgb_startup_state.c
 * @brief   Adapter startup snapshots, live ownership, and bounded wire regression checks.
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

#include "../src/gb/state_internal.h"
#include "../src/gb/state_startup.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/sgb_startup_audio.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HEADER_BYTES = 128,
    ACCESSORY_BYTES = 8,
    MAX_STREAM_FRAMES =
        CUPID_GB_APU_BUF_FRAMES + CUPID_GB_SGB_AUDIO_BATCHES * CUPID_GB_SGB_AUDIO_BATCH_FRAMES
};

typedef struct Blob {
    uint8_t *data;
    size_t length;
} Blob;

typedef struct Stream {
    size_t frames;
    int16_t samples[MAX_STREAM_FRAMES * 2];
    uint8_t origins[MAX_STREAM_FRAMES];
} Stream;

static CupidGb *create(CupidGbModel model, bool enhanced) {
    uint8_t rom[32768] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    memcpy(rom + 0x134, "HOST STATE", 10);
    rom[0x146] = enhanced ? 3 : 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x5354415445484f53));
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(gb->sgb_startup.phase == 200 && gb->sgb_startup.elapsed_cycles == 0);
    assert(cupid_gb_sgb_startup_state_valid(gb));
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void put(uint8_t *data, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        data[i] = (uint8_t)(value >> (8u * i));
}

static void seal(Blob blob) {
    put(blob.data + 12, blob.length, 4);
    cupid_sha256_parts(blob.data, 96, blob.data + HEADER_BYTES, blob.length - HEADER_BYTES,
                       blob.data + 96);
}

static Blob save(const CupidGb *gb) {
    Blob blob = {NULL, cupid_gb_state_size(gb)};
    assert(blob.length);
    blob.data = malloc(blob.length + 1);
    assert(blob.data);
    blob.data[blob.length] = 0xa7;
    size_t written = 0;
    assert(cupid_gb_save_state(gb, blob.data, blob.length, &written) == CUPID_GB_STATE_OK);
    assert(written == blob.length && blob.data[blob.length] == 0xa7);
    assert(blob.data[8] == CUPID_GB_STATE_VERSION);
    return blob;
}

static Blob duplicate(Blob original) {
    Blob copy = {malloc(original.length + 1), original.length};
    assert(copy.data);
    memcpy(copy.data, original.data, original.length);
    return copy;
}

static CupidGbStateResult load(CupidGb *gb, Blob blob, unsigned kind) {
    CupidGbStateContext context;
    assert(kind < 4);
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    switch (kind) {
        case 0:
            return cupid_gb_load_state(gb, blob.data, blob.length);
        case 1:
            return cupid_gb_load_state_with_context(gb, blob.data, blob.length, &context);
        case 2:
            return cupid_gb_load_state_live(gb, blob.data, blob.length);
        default:
            return cupid_gb_load_state_live_with_context(gb, blob.data, blob.length, &context);
    }
}

static void same_snapshot(const CupidGb *gb, Blob expected) {
    Blob actual = save(gb);
    assert(actual.length == expected.length);
    assert(!memcmp(actual.data, expected.data, actual.length));
    free(actual.data);
}

static void prepare_output(CupidGb *gb, int phase, uint32_t rate, unsigned batches, unsigned salt) {
    cupid_gb_sgb_startup_audio_reset(&gb->sgb_startup.output, salt + 1u);
    assert(cupid_gb_apu_set_sample_rate(gb, rate));
    gb->elapsed_cycles = 37;
    gb->display_cycles_since_frame = 140447;
    gb->sgb_startup.phase = (int16_t)phase;
    gb->sgb_startup.elapsed_cycles = 114u * (salt + 1u);
    CupidGbSgbStartupAudio *audio = &gb->sgb_startup.output;
    for (unsigned voice = 0; voice < CUPID_GB_SGB_AUDIO_VOICES; ++voice)
        audio->voice_phase[voice] = UINT32_C(0xfedcba98) - voice * UINT32_C(0x13579);
    audio->sweep_phase = UINT32_C(0xf0123456);
    audio->sweep_filter_q16 = -CUPID_GB_SGB_AUDIO_FILTER_LIMIT;
    audio->dropped_frames = 9;
    gb->apu.output.buf_write = 7;
    for (unsigned i = 0; i < 14; ++i)
        gb->apu.output.buf[i] = (int16_t)(1000 + i * 13 + salt);
    audio->normal_prefix_frames = batches ? 3 : 0;
    audio->batch_count = (uint8_t)batches;
    const uint32_t count = cupid_gb_sgb_startup_audio_frame_count(rate, cupid_gb_base_clock_hz(gb));
    assert(count && count <= CUPID_GB_SGB_AUDIO_BATCH_FRAMES);
    for (unsigned index = 0; index < batches; ++index) {
        CupidGbSgbStartupBatch *batch = &audio->batches[index];
        batch->generation_phase = (int16_t)(phase < 0 ? -2 + (int)index : 198 + (int)index);
        batch->rate = rate;
        batch->length = (uint16_t)count;
        /* Independent cursors also exercise an imported second partial batch. */
        batch->cursor = (uint16_t)(index + 1);
        for (unsigned frame = 0; frame < count; ++frame)
            batch->mono[frame] = batch->generation_phase < 0
                                     ? 0
                                     : (int16_t)((frame * 19u + index * 67u + salt) % 30000u);
    }
    assert(cupid_gb_sgb_startup_state_valid(gb));
}

static Stream *drain(CupidGb *gb, uint32_t chunk) {
    Stream *stream = calloc(1, sizeof(*stream));
    assert(stream);
    for (;;) {
        CupidGbAudioOrigin origin = CUPID_GB_AUDIO_ORIGIN_HANDHELD;
        size_t available = MAX_STREAM_FRAMES - stream->frames;
        if (!available) {
            const CupidGbAudioPending pending = cupid_gb_apu_pending(gb);
            assert(!pending.handheld_frames && !pending.startup_frames);
            break;
        }
        const uint32_t limit = available < chunk ? (uint32_t)available : chunk;
        uint32_t count =
            cupid_gb_apu_drain_chunk(gb, stream->samples + stream->frames * 2u, limit, &origin);
        assert(count <= limit);
        if (!count)
            break;
        memset(stream->origins + stream->frames, (int)origin, count);
        stream->frames += count;
    }
    const CupidGbAudioPending pending = cupid_gb_apu_pending(gb);
    assert(!pending.handheld_frames && !pending.startup_frames);
    return stream;
}

static void same_stream(const Stream *first, const Stream *second) {
    assert(first->frames == second->frames);
    assert(!memcmp(first->samples, second->samples, first->frames * 2u * sizeof(int16_t)));
    assert(!memcmp(first->origins, second->origins, first->frames));
}

static void check_four_apis(void) {
    const int phases[] = {-10, -1, 0, 95, 96, 199, 200};
    const CupidGbModel models[] = {CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2};
    for (unsigned model = 0; model < 2; ++model)
        for (unsigned enhanced = 0; enhanced < 2; ++enhanced)
            for (unsigned stage = 0; stage < sizeof(phases) / sizeof(phases[0]); ++stage) {
                CupidGb *source = create(models[model], enhanced != 0);
                const uint32_t rate = stage & 1u ? 8000u : 192000u;
                prepare_output(source, phases[stage], rate, stage % 3u, 17u);
                Blob saved = save(source);
                for (unsigned kind = 0; kind < 4; ++kind) {
                    CupidGb *target = create(models[model], enhanced != 0);
                    prepare_output(target, phases[stage] < 96 ? 199 : -10, 44100u, 2, 43u);
                    const CupidGbSgbStartupAudio receiving_intro = target->sgb_startup.output;
                    CupidGbAudioOutput receiving_ordinary = target->apu.output;
                    receiving_ordinary.sample_fraction = 0;
                    receiving_ordinary.fraction_half = false;
                    assert(load(target, saved, kind) == CUPID_GB_STATE_OK);
                    assert(target->sgb_startup.phase == source->sgb_startup.phase);
                    assert(target->sgb_startup.elapsed_cycles ==
                           source->sgb_startup.elapsed_cycles);
                    assert(target->elapsed_cycles == source->elapsed_cycles);
                    assert(target->display_cycles_since_frame == 140447);
                    if (kind < 2) {
                        same_snapshot(target, saved);
                        CupidGb *control = create(models[model], enhanced != 0);
                        assert(load(control, saved, 0) == CUPID_GB_STATE_OK);
                        Stream *expected = drain(control, 1);
                        Stream *actual = drain(target, kind ? 2048 : 64);
                        same_stream(expected, actual);
                        free(expected);
                        free(actual);
                        assert(cupid_gb_sgb_startup_audio_enqueue(
                                   &control->sgb_startup.output, 0, 97, rate,
                                   cupid_gb_base_clock_hz(control)) == CUPID_GB_SGB_AUDIO_QUEUED);
                        assert(cupid_gb_sgb_startup_audio_enqueue(
                                   &target->sgb_startup.output, 0, 97, rate,
                                   cupid_gb_base_clock_hz(target)) == CUPID_GB_SGB_AUDIO_QUEUED);
                        expected = drain(control, 1);
                        actual = drain(target, 2048);
                        same_stream(expected, actual);
                        free(expected);
                        free(actual);
                        destroy(control);
                    } else {
                        assert(!memcmp(&target->sgb_startup.output, &receiving_intro,
                                       sizeof(receiving_intro)));
                        assert(!memcmp(&target->apu.output, &receiving_ordinary,
                                       sizeof(receiving_ordinary)));
                    }
                    destroy(target);
                }
                free(saved.data);
                destroy(source);
            }
}

static void expect_rejection(CupidGb *target, Blob invalid) {
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, target, sizeof(*before));
    seal(invalid);
    for (unsigned kind = 0; kind < 4; ++kind) {
        assert(load(target, invalid, kind) == CUPID_GB_STATE_INVALID);
        assert(!memcmp(before, target, sizeof(*before)));
    }
    free(before);
}

static void *no_memory(size_t length) {
    (void)length;
    return NULL;
}

static void check_wire_and_legacy(void) {
    CupidGb *source = create(CUPID_GB_MODEL_SGB2, false);
    CupidGb *target = create(CUPID_GB_MODEL_SGB2, false);
    prepare_output(source, 200, 192000u, 2, 11u);
    prepare_output(target, -10, 8000u, 1, 39u);
    for (unsigned index = 0; index < 2; ++index)
        source->sgb_startup.output.batches[index].cursor = 0;
    size_t extension = 0;
    assert(cupid_gb_sgb_startup_state_size(source, &extension));
    assert(extension == CUPID_GB_STARTUP_STATE_MAX_SIZE);
    Blob original = save(source);
    const size_t offset = original.length - ACCESSORY_BYTES - extension;
    size_t measured = 0;
    assert(cupid_gb_sgb_startup_state_measure(target, original.data + offset,
                                              extension + ACCESSORY_BYTES, &measured));
    assert(measured == extension); /* The receiver has a different listening rate. */

    const struct Mutation {
        size_t offset;
        uint64_t value;
        unsigned width;
    } mutations[] = {{0, 201, 2},        {0, UINT16_C(0x8000), 2},
                     {2, UINT64_MAX, 8}, {42, 1048577, 4},
                     {46, 0, 8},         {62, 8, 2},
                     {64, 3, 1},         {65, UINT16_C(0xffff), 2},
                     {67, 44100, 4},     {71, 0, 2},
                     {71, 3215, 2},      {73, 3214, 2},
                     {83, 3214, 2}};

    for (unsigned i = 0; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
        Blob invalid = duplicate(original);
        put(invalid.data + offset + mutations[i].offset, mutations[i].value, mutations[i].width);
        expect_rejection(target, invalid);
        free(invalid.data);
    }
    Blob truncated = duplicate(original);
    --truncated.length;
    expect_rejection(target, truncated);
    free(truncated.data);
    Blob extra = duplicate(original);
    extra.data[extra.length++] = 0;
    expect_rejection(target, extra);
    free(extra.data);

    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, target, sizeof(*before));
    for (unsigned live = 0; live < 2; ++live) {
        assert(cupid_gb_load_state_with_allocator(target, original.data, original.length, live != 0,
                                                  no_memory) == CUPID_GB_STATE_OUT_OF_MEMORY);
        assert(!memcmp(before, target, sizeof(*before)));
    }
    free(before);

    /* This new projection test supplements the immutable historical fixtures. */
    Blob legacy = {malloc(original.length - extension), original.length - extension};
    assert(legacy.data);
    memcpy(legacy.data, original.data, offset);
    memcpy(legacy.data + offset, original.data + offset + extension, ACCESSORY_BYTES);
    put(legacy.data + 8, 20, 4);
    seal(legacy);
    for (unsigned kind = 0; kind < 4; ++kind) {
        CupidGb *receiver = create(CUPID_GB_MODEL_SGB2, false);
        prepare_output(receiver, -10, 44100, 2, 53);
        const CupidGbSgbStartupAudio receiving = receiver->sgb_startup.output;
        assert(load(receiver, legacy, kind) == CUPID_GB_STATE_OK);
        assert(receiver->sgb_startup.phase == 200 && !receiver->sgb_startup.elapsed_cycles);
        assert(receiver->elapsed_cycles == source->elapsed_cycles);
        assert(receiver->display_cycles_since_frame == 140447);
        if (kind < 2) {
            assert(!receiver->sgb_startup.output.batch_count);
            assert(!receiver->sgb_startup.output.normal_prefix_frames);
            assert(receiver->sgb_startup.output.noise_state == 1);
            assert(!receiver->sgb_startup.output.dropped_frames);
        } else {
            assert(!memcmp(&receiver->sgb_startup.output, &receiving, sizeof(receiving)));
        }
        destroy(receiver);
    }
    free(legacy.data);

    source->sgb_startup.output.batch_count = 3;
    assert(!cupid_gb_state_size(source));
    size_t written = 123;
    assert(cupid_gb_save_state(source, original.data, original.length, &written) ==
           CUPID_GB_STATE_INVALID);
    assert(!written);
    source->sgb_startup.output.batch_count = 2;
    free(original.data);
    destroy(source);
    destroy(target);
}

static void check_wrong_model(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_SGB};
    for (unsigned direction = 0; direction < 2; ++direction) {
        CupidGb *source = create(models[direction], false);
        CupidGb *target = create(models[1u - direction], false);
        if (direction)
            prepare_output(source, 199, 192000, 2, 71);
        else
            prepare_output(target, -10, 8000, 1, 73);
        Blob blob = save(source);
        CupidGb *before = malloc(sizeof(*before));
        assert(before);
        memcpy(before, target, sizeof(*before));
        for (unsigned kind = 0; kind < 4; ++kind) {
            assert(load(target, blob, kind) == CUPID_GB_STATE_WRONG_MODEL);
            assert(!memcmp(before, target, sizeof(*before)));
        }
        free(before);
        free(blob.data);
        destroy(source);
        destroy(target);
    }
}

static void count_presentation(const CupidGb *gb, uint8_t type, void *context) {
    (void)gb;
    (void)type;
    ++*(unsigned *)context;
}

static void check_two_presentations(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2};
    for (unsigned model = 0; model < 2; ++model)
        for (unsigned enhanced = 0; enhanced < 2; ++enhanced) {
            CupidGb *gb = create(models[model], enhanced != 0);
            assert(cupid_gb_apu_set_sample_rate(gb, 192000));
            cupid_gb_write_u8(gb, 0xff40, 0);
            gb->display_cycles_since_frame = 140447;
            gb->sgb_startup.phase = 198;
            unsigned publications = 0;
            cupid_gb_set_presentation_callback(gb, count_presentation, &publications);
            assert(cupid_gb_step(gb));
            assert(publications == 2 && gb->sgb_startup.phase == 200);
            assert(gb->sgb_startup.output.batch_count == 2);
            assert(!gb->sgb_startup.output.dropped_frames);
            Blob snapshot = save(gb);
            CupidGb *restored = create(models[model], enhanced != 0);
            assert(load(restored, snapshot, 0) == CUPID_GB_STATE_OK);
            Stream *first = drain(gb, 1);
            Stream *second = drain(restored, 2048);
            same_stream(first, second);
            free(first);
            free(second);
            free(snapshot.data);
            destroy(restored);
            destroy(gb);
        }
}

int main(void) {
    check_four_apis();
    check_wire_and_legacy();
    check_wrong_model();
    check_two_presentations();
    puts("SGB startup state continuation and ownership checks passed.");
    return 0;
}

/**
 * @file    test_sgb_startup_audio.c
 * @brief   Adapter startup sample counts, musical clocks, ordered drains, and output ownership.
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
#include "../src/gb/audio_output.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/sgb_startup_audio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STREAM_FRAMES = 2 * CUPID_GB_SGB_AUDIO_BATCH_FRAMES + 8 };

static CupidGb *new_machine(CupidGbModel model, uint32_t rate, uint64_t seed) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    gb->model = model;
    gb->sgb_startup.phase = 200;
    cupid_gb_sgb_startup_audio_reset(&gb->sgb_startup.output, seed);
    assert(cupid_gb_apu_set_sample_rate(gb, rate));
    return gb;
}

static CupidGbSgbAudioEnqueue enqueue(CupidGb *gb, int phase) {
    return cupid_gb_sgb_startup_audio_enqueue(&gb->sgb_startup.output, gb->apu.output.buf_write,
                                              phase, cupid_gb_apu_get_sample_rate(gb),
                                              cupid_gb_base_clock_hz(gb));
}

static void discard_pending(CupidGb *gb) {
    int16_t scratch[274];
    CupidGbAudioPending pending = cupid_gb_apu_pending(gb);
    uint32_t remaining = pending.handheld_frames + pending.startup_frames;
    while (remaining) {
        const uint32_t count = cupid_gb_apu_drain_chunk(gb, scratch, 137, NULL);
        assert(count && count <= remaining);
        remaining -= count;
    }
    pending = cupid_gb_apu_pending(gb);
    assert(!pending.handheld_frames && !pending.startup_frames);
    assert(cupid_gb_apu_drain_chunk(gb, scratch, 137, NULL) == 0);
}

static void assert_history(const CupidGbSgbStartupAudio *left,
                           const CupidGbSgbStartupAudio *right) {
    assert(memcmp(left->voice_phase, right->voice_phase, sizeof(left->voice_phase)) == 0);
    assert(left->sweep_phase == right->sweep_phase);
    assert(left->sweep_filter_q16 == right->sweep_filter_q16);
    assert(left->noise_state == right->noise_state);
}

static void test_counts_and_boundaries(void) {
    static const struct {
        uint32_t rate;
        uint32_t sgb;
        uint32_t sgb2;
    } cases[] = {{8000, 130, 133},
                 {44100, 720, 738},
                 {48000, 784, 803},
                 {96000, 1569, 1607},
                 {192000, 3138, 3214}};

    for (unsigned index = 0; index < sizeof(cases) / sizeof(*cases); ++index) {
        assert(cupid_gb_sgb_startup_audio_frame_count(cases[index].rate, 4295454) ==
               cases[index].sgb);
        assert(cupid_gb_sgb_startup_audio_frame_count(cases[index].rate, 4194304) ==
               cases[index].sgb2);
    }
    assert(cupid_gb_sgb_startup_audio_frame_count(7999, 4194304) == 0);
    assert(cupid_gb_sgb_startup_audio_frame_count(192001, 4194304) == 0);
    assert(cupid_gb_sgb_startup_audio_frame_count(48000, 0) == 0);
    CupidGb *gb = new_machine(CUPID_GB_MODEL_SGB2, 48000, 1);
    CupidGbSgbStartupAudio *audio = &gb->sgb_startup.output;
    for (int phase = -10; phase < 0; ++phase) {
        const CupidGbSgbStartupAudio before = *audio;
        assert(enqueue(gb, phase) == CUPID_GB_SGB_AUDIO_QUEUED);
        assert(audio->batches[0].length == 803);
        for (unsigned frame = 0; frame < 803; ++frame)
            assert(audio->batches[0].mono[frame] == 0);
        assert_history(audio, &before);
        discard_pending(gb);
    }
    assert(enqueue(gb, 0) == CUPID_GB_SGB_AUDIO_QUEUED);
    assert(audio->noise_state != 1 && audio->sweep_phase != 0);
    bool audible = false;
    for (unsigned frame = 0; frame < audio->batches[0].length; ++frame)
        audible = audible || audio->batches[0].mono[frame] != 0;
    assert(audible);
    discard_pending(gb);
    assert(enqueue(gb, 199) == CUPID_GB_SGB_AUDIO_QUEUED);
    const CupidGbSgbStartupAudio before = *audio;
    assert(enqueue(gb, 200) == CUPID_GB_SGB_AUDIO_NONE);
    assert(enqueue(gb, -11) == CUPID_GB_SGB_AUDIO_INVALID);
    assert(enqueue(gb, 201) == CUPID_GB_SGB_AUDIO_INVALID);
    assert(memcmp(audio, &before, sizeof(before)) == 0);
    free(gb);
}

static void test_voice_timing_and_hold(void) {
    static const uint32_t pitches[] = {46616, 58733, 69846, 83061, 104650, 124451, 156798};
    CupidGb *gb = new_machine(CUPID_GB_MODEL_SGB2, 48000, 7);
    CupidGbSgbStartupAudio *audio = &gb->sgb_startup.output;
    for (int phase = 66; phase <= 85; ++phase) {
        const CupidGbSgbStartupAudio before = *audio;
        assert(enqueue(gb, phase) == CUPID_GB_SGB_AUDIO_QUEUED);
        for (unsigned voice = 0; voice < CUPID_GB_SGB_AUDIO_VOICES; ++voice) {
            if (phase < 67 + (int)voice * 3)
                assert(audio->voice_phase[voice] == 0);
            else
                assert(audio->voice_phase[voice] != before.voice_phase[voice]);
        }
        discard_pending(gb);
    }
    assert(enqueue(gb, 119) == CUPID_GB_SGB_AUDIO_QUEUED);
    discard_pending(gb);
    const CupidGbSgbStartupAudio sweep_end = *audio;
    assert(enqueue(gb, 120) == CUPID_GB_SGB_AUDIO_QUEUED);
    assert(audio->sweep_phase == sweep_end.sweep_phase);
    assert(audio->sweep_filter_q16 == sweep_end.sweep_filter_q16);
    assert(audio->noise_state == sweep_end.noise_state);
    free(gb);

    /* The odd high-rate batch must end after its first held sample. Musical
     * phase still advances by two output periods for that final synthesis read. */
    gb = new_machine(CUPID_GB_MODEL_SGB2, 191960, 7);
    audio = &gb->sgb_startup.output;
    assert(enqueue(gb, 85) == CUPID_GB_SGB_AUDIO_QUEUED);
    const CupidGbSgbStartupBatch *batch = &audio->batches[0];
    assert(batch->length == 3213);
    for (unsigned frame = 0; frame + 1 < batch->length; frame += 2)
        assert(batch->mono[frame] == batch->mono[frame + 1]);
    const uint64_t synthesized_periods = 3214;
    for (unsigned voice = 0; voice < CUPID_GB_SGB_AUDIO_VOICES; ++voice) {
        const uint64_t ideal =
            synthesized_periods * pitches[voice] * (UINT64_C(1) << 32) / (UINT64_C(191960) * 100);
        const uint32_t difference = audio->voice_phase[voice] - (uint32_t)ideal;
        const uint32_t distance = difference <= INT32_MAX ? difference : 0u - difference;
        /* The tolerance accounts for one Q0.32 rounding per synthesis read. */
        assert(distance <= 1607);
    }
    free(gb);
}

static uint32_t prepare_ordered_stream(CupidGb *gb, int16_t *expected, int first_phase) {
    CupidGbAudioOutput *ordinary = &gb->apu.output;
    for (unsigned frame = 0; frame < 4; ++frame) {
        ordinary->buf[frame * 2] = (int16_t)(100 + frame);
        ordinary->buf[frame * 2 + 1] = (int16_t)(-100 - (int)frame);
        expected[frame * 2] = ordinary->buf[frame * 2];
        expected[frame * 2 + 1] = ordinary->buf[frame * 2 + 1];
    }
    ordinary->buf_write = 4;
    assert(enqueue(gb, first_phase) == CUPID_GB_SGB_AUDIO_QUEUED);
    assert(enqueue(gb, first_phase + 1) == CUPID_GB_SGB_AUDIO_QUEUED);
    uint32_t count = 4;
    const CupidGbSgbStartupAudio *audio = &gb->sgb_startup.output;
    for (unsigned index = 0; index < audio->batch_count; ++index) {
        const CupidGbSgbStartupBatch *batch = &audio->batches[index];
        for (unsigned frame = 0; frame < batch->length; ++frame) {
            expected[count * 2] = expected[count * 2 + 1] = batch->mono[frame];
            ++count;
        }
    }
    for (unsigned frame = 0; frame < 3; ++frame) {
        ordinary->buf[(4 + frame) * 2] = expected[count * 2] = (int16_t)(300 + frame);
        ordinary->buf[(4 + frame) * 2 + 1] = expected[count * 2 + 1] = (int16_t)(400 + frame);
        ++count;
    }
    ordinary->buf_write = 7;
    return count;
}

static void test_ordered_partial_drains(void) {
    static const uint32_t chunks[] = {1, 63, 64, 2047, 2048, 3214, STREAM_FRAMES};
    static const int phases[] = {85, 198};
    int16_t expected[STREAM_FRAMES * 2];
    int16_t received[STREAM_FRAMES * 2];
    CupidGb *original = new_machine(CUPID_GB_MODEL_SGB2, 192000, 41);
    CupidGb *gb = malloc(sizeof(*gb));
    assert(gb);
    for (unsigned pair = 0; pair < sizeof(phases) / sizeof(*phases); ++pair) {
        cupid_gb_sgb_startup_audio_reset(&original->sgb_startup.output, 41);
        const uint32_t total = prepare_ordered_stream(original, expected, phases[pair]);
        assert(total == 6435);
        for (unsigned iteration = 0; iteration < sizeof(chunks) / sizeof(*chunks); ++iteration) {
            memcpy(gb, original, sizeof(*gb));
            uint32_t cursor = 0;
            uint32_t recording_frames = 0;
            while (cursor < total) {
                CupidGbAudioOrigin origin;
                const uint32_t count =
                    cupid_gb_apu_drain_chunk(gb, received, chunks[iteration], &origin);
                assert(count && count <= chunks[iteration] && count <= total - cursor);
                for (uint32_t frame = 0; frame < count; ++frame) {
                    const bool handheld = cursor + frame < 4 || cursor + frame >= total - 3;
                    assert(origin == (handheld ? CUPID_GB_AUDIO_ORIGIN_HANDHELD
                                               : CUPID_GB_AUDIO_ORIGIN_SGB_STARTUP));
                    assert(received[frame * 2] == expected[(cursor + frame) * 2]);
                    assert(received[frame * 2 + 1] == expected[(cursor + frame) * 2 + 1]);
                }
                if (origin == CUPID_GB_AUDIO_ORIGIN_HANDHELD)
                    recording_frames += count;
                cursor += count;
                const CupidGbAudioPending pending = cupid_gb_apu_pending(gb);
                assert(pending.handheld_frames + pending.startup_frames == total - cursor);
                assert_history(&gb->sgb_startup.output, &original->sgb_startup.output);
            }
            assert(recording_frames == 7);
            assert(cupid_gb_apu_drain_chunk(gb, received, STREAM_FRAMES, NULL) == 0);
            assert(gb->sgb_startup.output.batch_count == 0);
            assert(gb->sgb_startup.output.normal_prefix_frames == 0);
        }
        memcpy(gb, original, sizeof(*gb));
        assert(cupid_gb_apu_drain(gb, received, STREAM_FRAMES) == total);
        assert(memcmp(received, expected, (size_t)total * 2 * sizeof(*received)) == 0);
    }
    free(gb);
    free(original);
}

static void test_overflow_preserves_old_output(void) {
    CupidGb *full = new_machine(CUPID_GB_MODEL_SGB2, 48000, 91);
    CupidGb *drained = new_machine(CUPID_GB_MODEL_SGB2, 48000, 91);
    for (int phase = 67; phase <= 68; ++phase) {
        assert(enqueue(full, phase) == CUPID_GB_SGB_AUDIO_QUEUED);
        assert(enqueue(drained, phase) == CUPID_GB_SGB_AUDIO_QUEUED);
        discard_pending(drained);
    }
    const CupidGbSgbStartupAudio before = full->sgb_startup.output;
    assert(enqueue(full, 69) == CUPID_GB_SGB_AUDIO_DROPPED);
    assert(enqueue(drained, 69) == CUPID_GB_SGB_AUDIO_QUEUED);
    discard_pending(drained);
    assert_history(&full->sgb_startup.output, &drained->sgb_startup.output);
    assert(full->sgb_startup.output.dropped_frames == 803);
    assert(full->sgb_startup.output.batch_count == 2);
    assert(memcmp(full->sgb_startup.output.batches, before.batches, sizeof(before.batches)) == 0);
    full->sgb_startup.output.dropped_frames = UINT64_MAX - 7;
    assert(enqueue(full, 70) == CUPID_GB_SGB_AUDIO_DROPPED);
    assert(full->sgb_startup.output.dropped_frames == UINT64_MAX);
    discard_pending(full);
    cupid_gb_sgb_startup_audio_reset(&full->sgb_startup.output, 91);
    assert(!full->sgb_startup.output.dropped_frames);

    /* A live restore can leave an old batch beside a newer ordinary suffix.
     * Advancing without the required drain must retain their existing order. */
    full->apu.output.buf_write = 1;
    assert(enqueue(full, 67) == CUPID_GB_SGB_AUDIO_QUEUED);
    full->apu.output.buf_write = 2;
    const CupidGbSgbStartupAudio suffix = full->sgb_startup.output;
    assert(enqueue(full, 68) == CUPID_GB_SGB_AUDIO_DROPPED);
    assert(full->sgb_startup.output.batch_count == 1);
    assert(full->sgb_startup.output.normal_prefix_frames == 1);
    assert(full->apu.output.buf_write == 2);
    assert(memcmp(full->sgb_startup.output.batches, suffix.batches, sizeof(suffix.batches)) == 0);
    free(full);
    free(drained);
}

static void test_rate_transaction_and_reset(void) {
    CupidGb *gb = new_machine(CUPID_GB_MODEL_SGB2, 48000, 113);
    assert(enqueue(gb, 85) == CUPID_GB_SGB_AUDIO_QUEUED);
    int16_t scratch[74];
    assert(cupid_gb_apu_drain_chunk(gb, scratch, 37, NULL) == 37);
    cupid_gb_tick_apu_divider(gb, 4);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, gb, sizeof(*gb));
    assert(!cupid_gb_apu_set_sample_rate(gb, 96000));
    assert(memcmp(gb, before, sizeof(*gb)) == 0);
    assert(cupid_gb_apu_set_sample_rate(gb, 48000));
    assert(memcmp(&gb->sgb_startup.output, &before->sgb_startup.output,
                  sizeof(gb->sgb_startup.output)) == 0);
    discard_pending(gb);
    const CupidGbSgbStartupAudio history = gb->sgb_startup.output;
    assert(cupid_gb_apu_set_sample_rate(gb, 96000));
    assert_history(&gb->sgb_startup.output, &history);
    assert(enqueue(gb, 86) == CUPID_GB_SGB_AUDIO_QUEUED);
    assert(gb->sgb_startup.output.batches[0].rate == 96000);
    assert(gb->sgb_startup.output.batches[0].length == 1607);
    const CupidGbAudioOutput ordinary = gb->apu.output;
    cupid_gb_sgb_startup_audio_reset(&gb->sgb_startup.output, 0);
    assert(gb->sgb_startup.output.noise_state == 1);
    assert(!gb->sgb_startup.output.batch_count && !gb->sgb_startup.output.dropped_frames);
    assert(memcmp(&ordinary, &gb->apu.output, sizeof(ordinary)) == 0);
    free(before);
    free(gb);
}

static void test_metadata_validation(void) {
    CupidGbSgbStartupAudio audio;
    cupid_gb_sgb_startup_audio_reset(&audio, 1);
    assert(cupid_gb_sgb_startup_audio_valid(&audio, 0, 48000, 4194304));
    audio.noise_state = 0;
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 0, 48000, 4194304));
    cupid_gb_sgb_startup_audio_reset(&audio, 1);
    audio.sweep_filter_q16 = CUPID_GB_SGB_AUDIO_FILTER_LIMIT;
    assert(cupid_gb_sgb_startup_audio_valid(&audio, 0, 48000, 4194304));
    ++audio.sweep_filter_q16;
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 0, 48000, 4194304));
    cupid_gb_sgb_startup_audio_reset(&audio, 1);
    audio.normal_prefix_frames = 1;
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 1, 48000, 4194304));
    cupid_gb_sgb_startup_audio_reset(&audio, 1);
    assert(cupid_gb_sgb_startup_audio_enqueue(&audio, 2, 198, 48000, 4194304) ==
           CUPID_GB_SGB_AUDIO_QUEUED);
    assert(cupid_gb_sgb_startup_audio_enqueue(&audio, 2, 199, 48000, 4194304) ==
           CUPID_GB_SGB_AUDIO_QUEUED);
    audio.batches[0].cursor = 6;
    audio.batches[1].cursor = 17;
    assert(cupid_gb_sgb_startup_audio_valid(&audio, 4, 48000, 4194304));
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 1, 48000, 4194304));
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 4, 44100, 4194304));
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 4, 48000, 4295454));
    audio.batches[1].generation_phase = 200;
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 4, 48000, 4194304));
    audio.batches[1].generation_phase = 199;
    audio.batches[1].cursor = audio.batches[1].length;
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 4, 48000, 4194304));
    audio.batches[1].cursor = 17;
    ++audio.batches[1].length;
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 4, 48000, 4194304));
    --audio.batches[1].length;
    audio.batch_count = 3;
    assert(!cupid_gb_sgb_startup_audio_valid(&audio, 4, 48000, 4194304));
}

static void test_suppression_boundary(void) {
    CupidGb *gb = new_machine(CUPID_GB_MODEL_SGB2, 48000, 17);
    CupidGbAudioOutput *output = &gb->apu.output;
    gb->sgb.enabled = false;
    gb->sgb_startup.phase = 120;
    gb->apu.ch1_dac = true;
    output->policy.interference_volume = 1;
    output->noise_state = UINT32_C(0x12345678);
    output->sample_fraction = 1u << 28;
    output->render_cycles = 10;
    output->buf_write = 3;
    const CupidGbAudioFilterState filter = output->filter;
    const uint8_t cursor = output->channels[0].cursor;
    cupid_gb_audio_render(gb);
    assert(output->channels[0].cursor != cursor && output->dac_charge[0] > 0);
    assert(!output->render_cycles && !output->sample_fraction);
    assert(memcmp(&output->filter, &filter, sizeof(filter)) == 0);
    assert(output->noise_state == UINT32_C(0x12345678) && output->buf_write == 3);
    assert(!gb->sgb_startup.output.batch_count);
    gb->sgb_startup.phase = 200;
    output->sample_fraction = 1u << 28;
    cupid_gb_audio_render(gb);
    assert(output->noise_state != UINT32_C(0x12345678) && output->buf_write == 4);
    gb->model = CUPID_GB_MODEL_DMG_ABC;
    gb->sgb_startup.phase = 120;
    output->sample_fraction = 1u << 28;
    cupid_gb_audio_render(gb);
    assert(output->buf_write == 5);
    free(gb);
}

int main(void) {
    test_counts_and_boundaries();
    test_voice_timing_and_hold();
    test_ordered_partial_drains();
    test_overflow_preserves_old_output();
    test_rate_transaction_and_reset();
    test_metadata_validation();
    test_suppression_boundary();
    puts("SGB startup audio contracts passed");
    return 0;
}

/**
 * @file    test_audio_state.c
 * @brief   Native audio replay, live restore policy, and malformed-history regression tests.
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

#include "../src/gb/audio_state.h"
#include "../src/gb/display_clock.h"
#include "../src/gb/vram_bus.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { INPUT_SAMPLE_BYTES = 1 };

enum {
    HEADER_BYTES = 128,
    ACCESSORY_BYTES = 8,
    CHANNEL_BYTES = 1043,
    FILTER_OFFSET = 4 * CHANNEL_BYTES,
    POLICY_OFFSET = FILTER_OFFSET + 32,
    CHARGE_OFFSET = POLICY_OFFSET + 10,
    GAIN_OFFSET = CHARGE_OFFSET + 32,
    ENABLE_OFFSET = GAIN_OFFSET + 128,
    CURSOR_OFFSET = ENABLE_OFFSET + 32,
    FRACTION_OFFSET = CURSOR_OFFSET + 1,
    RENDER_OFFSET = FRACTION_OFFSET + 4,
    STEP_OFFSET = RENDER_OFFSET + 4,
    SPAN_OFFSET = STEP_OFFSET + 4,
    HALF_OFFSET = SPAN_OFFSET + 2,
    NOISE_OFFSET = HALF_OFFSET + 1,
    DAC_OFFSET = NOISE_OFFSET + 4,
    PENDING_OFFSET = DAC_OFFSET + 4,
    FEEDBACK_OFFSET = PENDING_OFFSET + 2,
    START_DELAY_OFFSET = FEEDBACK_OFFSET + 1,
    LEGACY_CREDIT_OFFSET = 292148,
    LEGACY_QUEUE_OFFSET = 292152,
    LEGACY_FRAMES_OFFSET = 300344,
    LEGACY_RATE_OFFSET = 300349,
    LEGACY_DISABLED_OFFSET = 300389
};

typedef struct Blob {
    uint8_t *bytes;
    size_t length;
} Blob;

static unsigned replay_cases, rate_change_cases, live_cases, live_rate_cases, legacy_cases,
    rejected_cases;

static void put_word(uint8_t *bytes, uint64_t value, unsigned size) {
    for (unsigned index = 0; index < size; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8));
}

static void resign(Blob *blob) {
    CupidSha256 hash;
    put_word(blob->bytes + 12, blob->length, 4);
    cupid_sha256_init(&hash);
    cupid_sha256_update(&hash, blob->bytes, 96);
    cupid_sha256_update(&hash, blob->bytes + HEADER_BYTES, blob->length - HEADER_BYTES);
    cupid_sha256_final(&hash, blob->bytes + 96);
}

static CupidGb *create_machine(CupidGbModel model, uint32_t rate, unsigned seed) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    uint8_t rom[32768] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    memcpy(rom + 0x134, "AUDIO STATE", 11);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x146] = 3;
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, seed);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(cupid_gb_apu_set_sample_rate(gb, rate));
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff24, 0x77);
    cupid_gb_write_u8(gb, 0xff25, 0xff);
    cupid_gb_write_u8(gb, 0xff11, 0x80);
    cupid_gb_write_u8(gb, 0xff12, 0xf2);
    cupid_gb_write_u8(gb, 0xff13, 0xc3);
    cupid_gb_write_u8(gb, 0xff14, 0x87);
    cupid_gb_write_u8(gb, 0xff16, 0x40);
    cupid_gb_write_u8(gb, 0xff17, 0xb3);
    cupid_gb_write_u8(gb, 0xff18, 0x96);
    cupid_gb_write_u8(gb, 0xff19, 0x87);
    cupid_gb_write_u8(gb, 0xff21, 0xa2);
    cupid_gb_write_u8(gb, 0xff22, 0x19);
    cupid_gb_write_u8(gb, 0xff23, 0x80);
    for (unsigned index = 0; index < 16; ++index)
        cupid_gb_write_u8(gb, (uint16_t)(0xff30 + index), (uint8_t)(index * 17));
    cupid_gb_write_u8(gb, 0xff1a, 0x80);
    cupid_gb_write_u8(gb, 0xff1c, 0x20);
    cupid_gb_write_u8(gb, 0xff1d, 0xbd);
    cupid_gb_write_u8(gb, 0xff1e, 0x87);
    return gb;
}

static void destroy_machine(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static Blob encode(const CupidGb *gb) {
    Blob blob = {NULL, cupid_gb_state_size(gb)};
    assert(blob.length > HEADER_BYTES + CUPID_GB_AUDIO_STATE_SIZE + ACCESSORY_BYTES);
    blob.bytes = malloc(blob.length);
    assert(blob.bytes);
    size_t written = 0;
    assert(cupid_gb_save_state(gb, blob.bytes, blob.length, &written) == CUPID_GB_STATE_OK);
    assert(written == blob.length && blob.bytes[8] == CUPID_GB_STATE_VERSION);
    return blob;
}

static void assert_same(const CupidGb *left, const CupidGb *right) {
    Blob a = encode(left), b = encode(right);
    assert(a.length == b.length && memcmp(a.bytes, b.bytes, a.length) == 0);
    free(a.bytes);
    free(b.bytes);
}

static CupidGbStateResult load(CupidGb *gb, const Blob *blob, unsigned api) {
    CupidGbStateContext context = {0};
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    switch (api) {
        case 0:
            return cupid_gb_load_state(gb, blob->bytes, blob->length);
        case 1:
            return cupid_gb_load_state_with_context(gb, blob->bytes, blob->length, &context);
        case 2:
            return cupid_gb_load_state_live(gb, blob->bytes, blob->length);
        default:
            return cupid_gb_load_state_live_with_context(gb, blob->bytes, blob->length, &context);
    }
}

static void compare_pcm(CupidGb *left, CupidGb *right) {
    int16_t a[CUPID_GB_APU_BUF_FRAMES * 2], b[CUPID_GB_APU_BUF_FRAMES * 2];
    const uint32_t count = cupid_gb_apu_drain(left, a, CUPID_GB_APU_BUF_FRAMES);
    assert(count == cupid_gb_apu_drain(right, b, CUPID_GB_APU_BUF_FRAMES));
    assert(memcmp(a, b, count * 2 * sizeof(*a)) == 0);
}

static void assert_same_live_guest(const CupidGb *source, const CupidGb *target) {
    CupidGb *settled = malloc(sizeof(*settled));
    assert(settled);
    memcpy(settled, source, sizeof(*settled));
    /* Lazy and eager APU work describe the same guest boundary. Settle a copy
     * before comparing every serialized guest field, retaining only host output. */
    cupid_gb_apu_run(settled, true);
    settled->apu.output = target->apu.output;
    assert_same(settled, target);
    free(settled);
}

static void test_exact_replay(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    const uint32_t rates[] = {8000, 44100, 192000};
    unsigned pending = 0, charged = 0, filtered = 0, noise_delay = 0;
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned rate = 0; rate < sizeof(rates) / sizeof(*rates); ++rate)
            for (unsigned mode = 0; mode < 3; ++mode)
                for (unsigned phase = 0; phase < 2; ++phase)
                    for (unsigned api = 0; api < 2; ++api) {
                        CupidGb *a = create_machine(models[model], rates[rate], 17);
                        CupidGb *b = create_machine(models[model], 48000, 99);
                        assert(cupid_gb_apu_set_filter_mode(a, (CupidGbAudioFilter)mode));
                        assert(cupid_gb_apu_set_interference_volume(a, 0.35));
                        cupid_gb_tick(a, 241);
                        if (phase) {
                            cupid_gb_write_u8(a, 0xff12, 0);
                            cupid_gb_write_u8(a, 0xff1a, 0);
                            cupid_gb_write_u8(a, 0xff23, 0x80);
                            for (unsigned step = 0; step < 16 && !a->apu.pending_cycles; ++step)
                                cupid_gb_tick(a, 1);
                        }
                        cupid_gb_tick(a, 1);
                        pending += a->apu.pending_cycles != 0;
                        charged += a->apu.output.dac_charge[1] != 0;
                        filtered += a->apu.output.filter.capacitor[0] != 0;
                        noise_delay += a->apu.noise_timing.start_delay != 0;
                        Blob saved = encode(a);
                        assert(load(b, &saved, api) == CUPID_GB_STATE_OK);
                        assert_same(a, b);
                        for (unsigned step = 0; step < 512; ++step) {
                            if (step == 43 || step == 257) {
                                const uint8_t volume = step == 43 ? 0 : 0xf2;
                                cupid_gb_write_u8(a, 0xff12, volume);
                                cupid_gb_write_u8(b, 0xff12, volume);
                            }
                            cupid_gb_tick(a, (uint16_t)(1 + (step % 5)));
                            cupid_gb_tick(b, (uint16_t)(1 + (step % 5)));
                            if (step % 97 == 0)
                                compare_pcm(a, b);
                        }
                        compare_pcm(a, b);
                        assert_same(a, b);
                        free(saved.bytes);
                        destroy_machine(a);
                        destroy_machine(b);
                        ++replay_cases;
                    }
    assert(pending);
    assert(charged);
    assert(filtered);
    assert(noise_delay);
}

static void test_live_history(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned api = 2; api < 4; ++api) {
            CupidGb *source = create_machine(models[model], 44100, 17);
            CupidGb *target = create_machine(models[model], 48000, 99);
            cupid_gb_tick(source, 173);
            assert(cupid_gb_apu_set_filter_mode(target, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM));
            assert(cupid_gb_apu_set_interference_volume(target, 0.75));
            assert(cupid_gb_apu_set_channel_muted(target, 1, true));
            assert(cupid_gb_apu_set_channel_muted(target, 3, true));
            cupid_gb_tick(target, 317);
            CupidGbAudioOutput *receiving = malloc(sizeof(*receiving));
            assert(receiving);
            memcpy(receiving, &target->apu.output, sizeof(*receiving));
            receiving->sample_fraction = 0;
            receiving->fraction_half = false;
            Blob saved = encode(source);
            assert(load(target, &saved, api) == CUPID_GB_STATE_OK);
            assert(memcmp(receiving, &target->apu.output, sizeof(*receiving)) == 0);
            assert(target->apu.pending_cycles == 0);
            assert_same_live_guest(source, target);
            assert(cupid_gb_apu_get_sample_rate(target) == 48000);
            assert(cupid_gb_apu_get_filter_mode(target) == CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM);
            assert(cupid_gb_apu_get_interference_volume(target) == 0.75);
            assert(target->apu.output.policy.muted_channels == 10);
            free(receiving);
            free(saved.bytes);
            destroy_machine(source);
            destroy_machine(target);
            ++live_cases;
        }
}

static void test_rate_change_history(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned api = 0; api < 2; ++api) {
            CupidGb *source = create_machine(models[model], 8000, 17);
            CupidGb *target = create_machine(models[model], 44100, 99);
            cupid_gb_tick_apu(source, 520);
            assert(source->apu.output.render_cycles == 520);
            assert(cupid_gb_apu_set_sample_rate(source, 192000));
            /* A rate change may shorten the next scheduling span below the
             * retained old-rate interval. That reachable history remains valid. */
            assert(source->apu.output.render_cycles == 520 && source->apu.output.max_span == 22);
            Blob saved = encode(source);
            assert(load(target, &saved, api) == CUPID_GB_STATE_OK);
            assert_same(source, target);
            cupid_gb_tick_apu(source, 512);
            cupid_gb_tick_apu(target, 512);
            compare_pcm(source, target);
            assert_same(source, target);
            free(saved.bytes);
            destroy_machine(source);
            destroy_machine(target);
            ++rate_change_cases;
        }
}

static void test_live_rate_changes(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    const uint32_t rates[] = {8000, 44100, 48000, 192000};
    unsigned deferred_low_rate = 0;
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned source_rate = 0; source_rate < sizeof(rates) / sizeof(*rates); ++source_rate)
            for (unsigned target_rate = 0; target_rate < sizeof(rates) / sizeof(*rates);
                 ++target_rate)
                for (unsigned api = 2; api < 4; ++api) {
                    CupidGb *source = create_machine(models[model], rates[source_rate], 17);
                    CupidGb *target = create_machine(models[model], rates[target_rate], 99);
                    CupidGb *eager = create_machine(models[model], rates[target_rate], 99);
                    /* Ordinary guest clocks naturally defer several hundred APU
                     * clocks at 8 kHz when wave and sweep work do not force a run. */
                    cupid_gb_write_u8(source, 0xff26, 0);
                    /* Model startup phases differ. A normal DIV write keeps a
                     * sequencer edge from settling this short capture interval. */
                    cupid_gb_write_u8(source, 0xff04, 0);
                    cupid_gb_tick(source, 100);
                    if (rates[source_rate] == 8000) {
                        assert(source->apu.pending_cycles > 300);
                        ++deferred_low_rate;
                    }
                    assert(cupid_gb_audio_state_valid(source));
                    Blob deferred = encode(source);
                    cupid_gb_apu_run(source, true);
                    Blob settled = encode(source);
                    assert(load(target, &deferred, api) == CUPID_GB_STATE_OK);
                    assert(load(eager, &settled, api) == CUPID_GB_STATE_OK);
                    assert(cupid_gb_apu_get_sample_rate(target) == rates[target_rate]);
                    assert(target->apu.pending_cycles == 0);
                    assert_same(target, eager);
                    assert_same_live_guest(source, target);
                    /* Restoring eager or deferred snapshots must also produce
                     * identical later hardware and output at the receiving rate. */
                    for (unsigned step = 0; step < 64; ++step) {
                        cupid_gb_tick(target, (uint16_t)(1 + step % 5));
                        cupid_gb_tick(eager, (uint16_t)(1 + step % 5));
                    }
                    compare_pcm(target, eager);
                    assert_same(target, eager);
                    free(deferred.bytes);
                    free(settled.bytes);
                    destroy_machine(source);
                    destroy_machine(target);
                    destroy_machine(eager);
                    ++live_rate_cases;
                }
    assert(deferred_low_rate == 24);
}

static Blob legacy_v13(const Blob *current) {
    const size_t removed = INPUT_SAMPLE_BYTES + CUPID_GB_AUDIO_STATE_SIZE +
                           CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
                           CUPID_GB_VRAM_BUS_STATE_SIZE;
    Blob old = {malloc(current->length), current->length - removed};
    assert(old.bytes);
    const size_t audio = current->length - ACCESSORY_BYTES - removed;
    memcpy(old.bytes, current->bytes, audio);
    memcpy(old.bytes + audio, current->bytes + audio + removed, ACCESSORY_BYTES);
    put_word(old.bytes + 8, 13, 4);
    resign(&old);
    return old;
}

static void test_v13_import(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned api = 0; api < 2; ++api) {
            CupidGb *source = create_machine(models[model], 32000, 17);
            CupidGb *target = create_machine(models[model], 48000, 99);
            assert(cupid_gb_apu_set_filter_mode(source, CUPID_GB_AUDIO_FILTER_OFF));
            assert(cupid_gb_apu_set_interference_volume(source, 0.5));
            assert(cupid_gb_apu_set_channel_muted(source, 2, true));
            cupid_gb_tick(source, 431);
            cupid_gb_apu_run(source, true);
            Blob current = encode(source), old = legacy_v13(&current);
            const uint32_t credit = source->apu.output.sample_acc;
            const uint32_t frames = source->apu.output.buf_write;
            cupid_gb_tick(target, 971);
            assert(load(target, &old, api) == CUPID_GB_STATE_OK);
            assert(target->apu.output.sample_acc == credit &&
                   target->apu.output.buf_write == frames);
            assert(target->apu.output.sample_rate == 32000);
            assert(cupid_gb_apu_get_filter_mode(target) == CUPID_GB_AUDIO_FILTER_OFF);
            assert(cupid_gb_apu_get_interference_volume(target) == 0);
            assert(target->apu.output.policy.muted_channels == 0);
            assert(target->apu.output.filter.capacitor[0] == 0 &&
                   target->apu.output.filter.capacitor[1] == 0);
            cupid_gb_apu_import_monochrome_noise(source);
            cupid_gb_audio_import_legacy(source);
            cupid_gb_pixels_import_legacy(source);
            cupid_gb_display_clock_import_legacy(source);
            cupid_gb_vram_bus_import_legacy(source);
            cupid_gb_input_import_legacy(source);
            assert_same(source, target);
            compare_pcm(source, target);
            for (unsigned step = 0; step < 128; ++step) {
                cupid_gb_tick(source, 3);
                cupid_gb_tick(target, 3);
            }
            compare_pcm(source, target);
            assert_same(source, target);
            free(current.bytes);
            free(old.bytes);
            destroy_machine(source);
            destroy_machine(target);
            ++legacy_cases;
        }
}

static void reject_unchanged(CupidGb *target, const Blob *bad) {
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, target, sizeof(*before));
    for (unsigned api = 0; api < 4; ++api) {
        assert(load(target, bad, api) == CUPID_GB_STATE_INVALID);
        assert(memcmp(before, target, sizeof(*before)) == 0);
        ++rejected_cases;
    }
    free(before);
}

static void test_live_combination(void) {
    CupidGb *source = create_machine(CUPID_GB_MODEL_DMG_ABC, 44100, 17);
    CupidGb *target = create_machine(CUPID_GB_MODEL_DMG_ABC, 44100, 99);
    source->apu.pending_cycles = 2048;
    source->apu.output.sample_acc = 2048 * 44100;
    source->apu.output.sample_fraction = 0;
    source->apu.output.fraction_half = false;
    Blob saved = encode(source);

    target->apu.pending_cycles = 0;
    target->apu.output.sample_acc = 0;
    target->apu.output.sample_fraction = 0;
    target->apu.output.fraction_half = false;
    Blob receiving = encode(target);
    /* The saved backlog owns its delivery credit. Finish it before attaching
     * the receiving output, so an otherwise valid snapshot remains loadable. */
    for (unsigned api = 2; api < 4; ++api) {
        assert(load(target, &receiving, 0) == CUPID_GB_STATE_OK);
        assert(load(target, &saved, api) == CUPID_GB_STATE_OK);
        assert(target->apu.pending_cycles == 0);
        assert_same_live_guest(source, target);
        ++live_cases;
    }
    for (unsigned api = 0; api < 2; ++api) {
        assert(load(target, &saved, api) == CUPID_GB_STATE_OK);
        assert_same(source, target);
    }

    free(receiving.bytes);
    free(saved.bytes);
    destroy_machine(source);
    destroy_machine(target);
}

static void test_malformed_history(void) {
    CupidGb *source = create_machine(CUPID_GB_MODEL_DMG_ABC, 44100, 17);
    CupidGb *target = create_machine(CUPID_GB_MODEL_DMG_ABC, 48000, 99);
    cupid_gb_tick(source, 347);
    cupid_gb_apu_run(source, true);
    cupid_gb_tick(target, 133);
    Blob valid = encode(source), bad = {malloc(valid.length), valid.length};
    assert(bad.bytes);
    const size_t audio = valid.length - INPUT_SAMPLE_BYTES - ACCESSORY_BYTES -
                         CUPID_GB_DISPLAY_CLOCK_STATE_SIZE - CUPID_GB_PIXEL_TIMING_STATE_SIZE -
                         CUPID_GB_VRAM_BUS_STATE_SIZE - CUPID_GB_AUDIO_STATE_SIZE;

    const struct Mutation {
        size_t offset;
        unsigned bytes;
        uint64_t value;
    } mutations[] = {{FILTER_OFFSET, 8, UINT64_C(0x7ff8000000000000)},
                     {FILTER_OFFSET + 8, 8, UINT64_C(0x7ff0000000000000)},
                     {FILTER_OFFSET + 16, 8, UINT64_C(0xfff0000000000000)},
                     {FILTER_OFFSET, 8, UINT64_C(0x6000000000000000)},
                     {FILTER_OFFSET + 24, 8, UINT64_C(0xbff0000000000000)},
                     {FILTER_OFFSET + 24, 8, UINT64_C(0x3ff199999999999a)},
                     {POLICY_OFFSET, 8, UINT64_C(0x7ff8000000000000)},
                     {POLICY_OFFSET, 8, UINT64_C(0x3ff199999999999a)},
                     {POLICY_OFFSET, 8, UINT64_C(0xbfb999999999999a)},
                     {POLICY_OFFSET + 8, 1, 16},
                     {POLICY_OFFSET + 9, 1, 2},
                     {CHARGE_OFFSET, 8, UINT64_C(0x7ff8000000000000)},
                     {CHARGE_OFFSET + 8, 8, UINT64_C(0xbfb999999999999a)},
                     {CHARGE_OFFSET + 16, 8, UINT64_C(0x3ff199999999999a)},
                     {CHANNEL_BYTES - 1, 1, 128},
                     {CHANNEL_BYTES - 3, 2, 4001},
                     {1032, 2, 121},
                     {1034, 2, (uint16_t)-121},
                     {GAIN_OFFSET, 1, 35},
                     {ENABLE_OFFSET, 1, 16},
                     {CURSOR_OFFSET, 1, 32},
                     {FRACTION_OFFSET, 4, UINT32_C(2) << 28},
                     {RENDER_OFFSET, 4, 2049},
                     {STEP_OFFSET, 4, UINT32_MAX},
                     {SPAN_OFFSET, 2, UINT16_MAX},
                     {HALF_OFFSET, 1, 2},
                     {DAC_OFFSET + 3, 1, 16},
                     {PENDING_OFFSET, 2, 2049},
                     {FEEDBACK_OFFSET, 1, 2},
                     {START_DELAY_OFFSET, 1, 13}};

    for (unsigned index = 0; index < sizeof(mutations) / sizeof(*mutations); ++index) {
        memcpy(bad.bytes, valid.bytes, valid.length);
        const struct Mutation *mutation = &mutations[index];
        put_word(bad.bytes + audio + mutation->offset, mutation->value, mutation->bytes);
        resign(&bad);
        reject_unchanged(target, &bad);
    }
    const struct Mutation prefix[] = {{LEGACY_FRAMES_OFFSET, 4, CUPID_GB_APU_BUF_FRAMES + 1},
                                      {LEGACY_RATE_OFFSET, 4, 7999},
                                      {LEGACY_DISABLED_OFFSET, 1, 2},
                                      {LEGACY_CREDIT_OFFSET, 4, UINT32_MAX}};
    for (unsigned index = 0; index < sizeof(prefix) / sizeof(*prefix); ++index) {
        memcpy(bad.bytes, valid.bytes, valid.length);
        put_word(bad.bytes + prefix[index].offset, prefix[index].value, prefix[index].bytes);
        resign(&bad);
        reject_unchanged(target, &bad);
    }
    memcpy(bad.bytes, valid.bytes, valid.length);
    bad.bytes[LEGACY_DISABLED_OFFSET] = 1;
    bad.bytes[audio + POLICY_OFFSET + 9] = 1;
    resign(&bad);
    reject_unchanged(target, &bad);

    /* Early rendering clamps a short phase to zero. This combination has enough
     * total credit but would leave an invalid phase after that first clamp. */
    memcpy(bad.bytes, valid.bytes, valid.length);
    put_word(bad.bytes + LEGACY_RATE_OFFSET, 79822, 4);
    put_word(bad.bytes + LEGACY_CREDIT_OFFSET, 39916778, 4);
    put_word(bad.bytes + audio + FRACTION_OFFSET, 123149863, 4);
    put_word(bad.bytes + audio + RENDER_OFFSET, 808, 4);
    put_word(bad.bytes + audio + STEP_OFFSET, 10217216, 4);
    put_word(bad.bytes + audio + SPAN_OFFSET, 54, 2);
    put_word(bad.bytes + audio + HALF_OFFSET, 0, 1);
    put_word(bad.bytes + audio + PENDING_OFFSET, 508, 2);
    resign(&bad);
    reject_unchanged(target, &bad);

    /* Each field is in range, but this combination would run reconstruction
     * beyond its credited output interval and overflow the phase window. */
    memcpy(bad.bytes, valid.bytes, valid.length);
    put_word(bad.bytes + LEGACY_CREDIT_OFFSET, 0, 4);
    put_word(bad.bytes + audio + PENDING_OFFSET, 2048, 2);
    resign(&bad);
    reject_unchanged(target, &bad);

    /* In-memory validation must fail before the writer touches caller storage. */
    source->apu.output.filter.capacitor[0] = NAN;
    memset(bad.bytes, 0xa5, bad.length);
    size_t written = 99;
    assert(cupid_gb_save_state(source, bad.bytes, bad.length, &written) == CUPID_GB_STATE_INVALID);
    assert(written == 0);
    for (size_t index = 0; index < bad.length; ++index)
        assert(bad.bytes[index] == 0xa5);
    free(valid.bytes);
    free(bad.bytes);
    destroy_machine(source);
    destroy_machine(target);
}

int main(void) {
    _Static_assert(CUPID_GB_AUDIO_STATE_SIZE == 4434, "Unexpected native audio wire size");
    _Static_assert(START_DELAY_OFFSET + 1 == CUPID_GB_AUDIO_STATE_SIZE,
                   "Incomplete audio wire map");
    test_exact_replay();
    test_live_history();
    test_rate_change_history();
    test_live_rate_changes();
    test_v13_import();
    test_live_combination();
    test_malformed_history();
    printf("Audio state checks: %u exact replays, %u rate changes, %u live restores, "
           "%u mixed-rate live restores, %u legacy imports, "
           "%u transactional rejections.\n",
           replay_cases, rate_change_cases, live_cases, live_rate_cases, legacy_cases,
           rejected_cases);
    return 0;
}

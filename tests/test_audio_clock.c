/**
 * @file    test_audio_clock.c
 * @brief   Audio clock bounds, sustained phase continuity, and legacy cache restoration.
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
#include "cupid/gb/apu.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STEP_OFFSET = 4415, SPAN_OFFSET = 4419 };

static CupidGb *new_machine(CupidGbModel model, uint32_t rate) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    gb->model = model;
    assert(cupid_gb_apu_set_sample_rate(gb, rate));
    assert(cupid_gb_audio_state_valid(gb));
    return gb;
}

static void test_rate_domain(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_SGB};
    const uint64_t frame = UINT64_C(1) << 29;
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model) {
        CupidGb *gb = new_machine(models[model], 8000);
        const uint32_t clock = cupid_gb_base_clock_hz(gb);
        for (uint32_t rate = 8000; rate <= 192000; ++rate) {
            assert(cupid_gb_apu_set_sample_rate(gb, rate));
            const CupidGbAudioOutput *output = &gb->apu.output;
            /* Phase never outruns earned samples, and every complete scheduling
             * span still reaches at least one reconstruction frame. */
            assert((uint64_t)output->fraction_step * clock <= (uint64_t)rate * frame);
            assert((uint64_t)output->fraction_step * output->max_span >= frame);
        }
        free(gb);
    }
}

static void seed_boundary(CupidGb *gb, unsigned index) {
    const uint32_t credit[] = {1634062, 1251738};
    const uint32_t fraction[] = {370552759, 346660216};
    const uint32_t render[] = {204, 6};
    gb->apu.output.sample_acc = credit[index];
    gb->apu.output.sample_fraction = fraction[index];
    gb->apu.output.fraction_half = index != 0;
    gb->apu.output.render_cycles = render[index];
}

static void test_phase_boundary(void) {
    const uint32_t rates[] = {8000, 192000};
    for (unsigned index = 0; index < sizeof(rates) / sizeof(*rates); ++index) {
        CupidGb *gb = new_machine(CUPID_GB_MODEL_SGB, rates[index]);
        /* These retained histories sit just below the phase/credit limit.
         * Another ordinary clock must not make a valid snapshot unsaveable. */
        seed_boundary(gb, index);
        assert(cupid_gb_audio_state_valid(gb));
        cupid_gb_tick_apu(gb, 1);
        assert(cupid_gb_audio_state_valid(gb));
        uint8_t encoded[CUPID_GB_AUDIO_STATE_SIZE];
        assert(cupid_gb_audio_state_encode(gb, encoded, sizeof(encoded)));
        for (unsigned step = 0; step < 128; ++step) {
            cupid_gb_tick_apu_divider(gb, 4);
            cupid_gb_apu_run(gb, false);
            assert(cupid_gb_audio_state_valid(gb));
        }
        free(gb);
    }
}

static void put_word(uint8_t *bytes, uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index)
        bytes[index] = (uint8_t)(value >> (8u * index));
}

static void test_legacy_increment(void) {
    const uint32_t rates[] = {8000, 192000};
    for (unsigned index = 0; index < sizeof(rates) / sizeof(*rates); ++index) {
        CupidGb *gb = new_machine(CUPID_GB_MODEL_SGB, rates[index]);
        CupidGb *restored = malloc(sizeof(*restored));
        assert(restored);
        seed_boundary(gb, index);
        gb->apu.output.channels[0].pending[1][3] = 271828;
        gb->apu.output.dac_charge[1] = 0.75;
        uint8_t canonical[CUPID_GB_AUDIO_STATE_SIZE], legacy[CUPID_GB_AUDIO_STATE_SIZE];
        uint8_t recaptured[CUPID_GB_AUDIO_STATE_SIZE];
        assert(cupid_gb_audio_state_encode(gb, canonical, sizeof(canonical)));
        const uint32_t floor_step = gb->apu.output.fraction_step;
        const uint32_t clock = cupid_gb_base_clock_hz(gb);
        const uint32_t old_step =
            (uint32_t)(((uint64_t)rates[index] * (UINT64_C(1) << 29) + clock / 2) / clock);
        assert(old_step == floor_step + 1);
        memcpy(legacy, canonical, sizeof(legacy));
        /* These offsets are the retained V14 wire positions, independent of
         * native structure padding and host integer byte order. */
        put_word(legacy + STEP_OFFSET, old_step, 4);
        *restored = *gb;
        assert(cupid_gb_audio_state_decode(restored, legacy, sizeof(legacy)));
        assert(restored->apu.output.fraction_step == floor_step);
        assert(cupid_gb_audio_state_encode(restored, recaptured, sizeof(recaptured)));
        assert(memcmp(canonical, recaptured, sizeof(canonical)) == 0);
        cupid_gb_tick_apu(restored, 1);
        assert(cupid_gb_audio_state_valid(restored));

        *restored = *gb;
        assert(cupid_gb_audio_state_decode(restored, canonical, sizeof(canonical)));
        assert(cupid_gb_audio_state_encode(restored, recaptured, sizeof(recaptured)));
        assert(memcmp(canonical, recaptured, sizeof(canonical)) == 0);

        put_word(legacy + STEP_OFFSET, old_step + 1, 4);
        *restored = *gb;
        assert(!cupid_gb_audio_state_decode(restored, legacy, sizeof(legacy)));
        put_word(legacy + STEP_OFFSET, old_step, 4);
        put_word(legacy + SPAN_OFFSET, (uint32_t)gb->apu.output.max_span + 2, 2);
        *restored = *gb;
        assert(!cupid_gb_audio_state_decode(restored, legacy, sizeof(legacy)));
        free(restored);
        free(gb);
    }
}

int main(void) {
    test_rate_domain();
    test_phase_boundary();
    test_legacy_increment();
    puts("Audio clock bounds and legacy cache restoration passed");
    return 0;
}

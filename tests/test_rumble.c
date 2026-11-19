/**
 * @file    test_rumble.c
 * @brief   Cartridge motor levels, capability gates, and host observer lifetimes.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "cupid/gb/gb.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROM_SIZE = 0x8000 };

typedef struct Observer {
    unsigned raw_calls, strength_calls, sample_calls;
    bool enabled;
    uint8_t strength;
    uint64_t raw_cycle, strength_cycle;
} Observer;

static void raw_motor(const CupidGb *gb, bool enabled, uint64_t cycles, void *context) {
    Observer *observer = context;
    assert(enabled == cupid_gb_rumble_enabled(gb));
    assert(cycles == gb->elapsed_cycles);
    ++observer->raw_calls;
    observer->enabled = enabled;
    observer->raw_cycle = cycles;
}

static void motor_strength(const CupidGb *gb, uint8_t strength, uint64_t cycles, void *context) {
    Observer *observer = context;
    assert(strength <= 3 && strength == cupid_gb_rumble_strength(gb));
    assert(cycles == gb->elapsed_cycles);
    ++observer->strength_calls;
    observer->strength = strength;
    observer->strength_cycle = cycles;
}

static void motor_sample(const CupidGb *gb, uint8_t strength, void *context) {
    Observer *observer = context;
    assert(strength <= 3 && strength == cupid_gb_rumble_strength(gb));
    ++observer->sample_calls;
}

static void observe(CupidGb *gb, Observer *observer) {
    cupid_gb_set_rumble_callback(gb, raw_motor, observer);
    cupid_gb_set_rumble_strength_callback(gb, motor_strength, observer);
    cupid_gb_set_rumble_sample_callback(gb, motor_sample, observer);
}

static void make_rom(uint8_t *rom, CupidGbModel model, uint8_t type, uint8_t features) {
    memset(rom, 0, ROM_SIZE);
    memcpy(rom + 0x134, "CUPID MOTOR", 11);
    rom[0x100] = 0xc3;
    rom[0x102] = 2;
    rom[0x200] = 0x18;
    rom[0x201] = 0xfe;
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x147] = type;
    if (type == 0xbc) {
        rom[0x149] = 0xc1;
        rom[0x14a] = 0x65;
        rom[0x150] = 1;
        rom[0x152] = 1;
        rom[0x153] = features;
    }
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);
}

static void load(CupidGb *gb, const uint8_t *rom, CupidGbModel model) {
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_load_rom(gb, rom, ROM_SIZE));
    cupid_gb_write_u8(gb, 0xff40, 0);
}

static void test_levels(CupidGb *gb, uint8_t *rom, CupidGbModel model) {
    const uint8_t features[] = {0, 1, 8, 9, 0xfe, 0xff};
    const uint8_t levels[] = {1, 1, 2, 3, 2, 0, 0, 3, 0};
    for (unsigned feature = 0; feature < sizeof(features); ++feature) {
        make_rom(rom, model, 0xbc, features[feature]);
        load(gb, rom, model);
        Observer observer = {0};
        observe(gb, &observer);
        unsigned expected_raw_calls = 0, expected_strength_calls = 0;
        uint8_t previous = 0;
        assert(!cupid_gb_rumble_strength(gb) && !cupid_gb_rumble_enabled(gb));
        for (unsigned index = 0; index < sizeof(levels); ++index) {
            const uint8_t level = levels[index];
            const uint8_t effective = features[feature] & 1u ? level : 0;
            expected_raw_calls += (previous != 0) != (level != 0);
            expected_strength_calls += (features[feature] & 1u) && previous != level;
            gb->elapsed_cycles += 28u;
            cupid_gb_write_u8(gb, 0x3fff, (uint8_t)(0x20u + level));
            assert((cupid_gb_read_u8(gb, 0xa003) & 3u) == level);
            assert(gb->tpp1.motor_strength == level);
            assert(cupid_gb_rumble_enabled(gb) == (level != 0));
            assert(cupid_gb_rumble_strength(gb) == effective);
            assert(observer.raw_calls == expected_raw_calls);
            assert(observer.strength_calls == expected_strength_calls);
            if (observer.strength_calls)
                assert(observer.strength == effective);
            previous = level;
        }
        cupid_gb_set_rumble_strength_callback(gb, NULL, &observer);
        assert(!gb->rumble_strength_callback && !gb->rumble_strength_user_data);
        cupid_gb_set_rumble_callback(gb, NULL, NULL);
        cupid_gb_set_rumble_sample_callback(gb, NULL, NULL);
    }
}

static void test_mbc5(CupidGb *gb, uint8_t *rom, CupidGbModel model) {
    const uint8_t types[] = {0x19, 0x1c, 0x1d, 0x1e};
    const uint8_t writes[] = {8, 15, 7, 0, 8, 0};
    for (unsigned type = 0; type < sizeof(types); ++type) {
        make_rom(rom, model, types[type], 0);
        load(gb, rom, model);
        Observer observer = {0};
        observe(gb, &observer);
        bool previous = false;
        unsigned transitions = 0;
        for (unsigned index = 0; index < sizeof(writes); ++index) {
            const bool enabled = types[type] != 0x19 && (writes[index] & 8u);
            transitions += previous != enabled;
            gb->elapsed_cycles += 12;
            cupid_gb_write_u8(gb, 0x4000, writes[index]);
            assert(cupid_gb_rumble_strength(gb) == (enabled ? 3 : 0));
            assert(observer.raw_calls == transitions && observer.strength_calls == transitions);
            if (transitions)
                assert(observer.strength_cycle == observer.raw_cycle);
            previous = enabled;
        }
        cupid_gb_set_rumble_callback(gb, NULL, NULL);
        cupid_gb_set_rumble_strength_callback(gb, NULL, NULL);
        cupid_gb_set_rumble_sample_callback(gb, NULL, NULL);
    }
}

static void retained_observers(const CupidGb *gb, const Observer *observer) {
    assert(gb->rumble_callback == raw_motor && gb->rumble_user_data == observer);
    assert(gb->rumble_strength_callback == motor_strength &&
           gb->rumble_strength_user_data == observer);
    assert(gb->rumble_sample_callback == motor_sample && gb->rumble_sample_user_data == observer);
}

static void test_lifetimes(CupidGb *gb, uint8_t *rom, CupidGbModel model) {
    make_rom(rom, model, 0xbc, 9);
    load(gb, rom, model);
    Observer first = {0}, receiving = {0};
    observe(gb, &first);
    cupid_gb_write_u8(gb, 0x0003, 0x22);
    assert(first.raw_calls == 1 && first.strength_calls == 1);
    const size_t length = cupid_gb_state_size(gb);
    uint8_t *saved = malloc(length), *without_observers = malloc(length);
    CupidGb *before = malloc(sizeof(*before));
    assert(saved && without_observers && before);
    assert(cupid_gb_save_state(gb, saved, length, NULL) == CUPID_GB_STATE_OK);

    cupid_gb_set_rumble_callback(gb, NULL, NULL);
    cupid_gb_set_rumble_strength_callback(gb, NULL, NULL);
    cupid_gb_set_rumble_sample_callback(gb, NULL, NULL);
    assert(cupid_gb_state_size(gb) == length);
    assert(cupid_gb_save_state(gb, without_observers, length, NULL) == CUPID_GB_STATE_OK);
    assert(!memcmp(saved, without_observers, length));
    observe(gb, &receiving);
    cupid_gb_write_u8(gb, 0x0003, 0x20);
    const Observer prior_load = receiving;
    assert(cupid_gb_load_state(gb, saved, length) == CUPID_GB_STATE_OK);
    retained_observers(gb, &receiving);
    assert(!memcmp(&prior_load, &receiving, sizeof(receiving)));
    assert(cupid_gb_rumble_strength(gb) == 2 && gb->tpp1.motor_strength == 2);
    assert(first.raw_calls == 1 && first.strength_calls == 1);

    *before = *gb;
    saved[length - 1u] ^= 1u;
    assert(cupid_gb_load_state(gb, saved, length) != CUPID_GB_STATE_OK);
    assert(!memcmp(gb, before, sizeof(*gb)));
    assert(!memcmp(&prior_load, &receiving, sizeof(receiving)));
    saved[length - 1u] ^= 1u;

    const CupidGbResetKind kinds[] = {CUPID_GB_RESET_QUICK, CUPID_GB_RESET_HARD};
    for (unsigned index = 0; index < sizeof(kinds) / sizeof(*kinds); ++index) {
        cupid_gb_write_u8(gb, 0x0003, 0x23);
        const Observer prior_reset = receiving;
        assert(cupid_gb_reset(gb, kinds[index]));
        retained_observers(gb, &receiving);
        assert(!memcmp(&prior_reset, &receiving, sizeof(receiving)));
        assert(!cupid_gb_rumble_strength(gb) && !cupid_gb_rumble_enabled(gb));
        cupid_gb_write_u8(gb, 0x0003, 0x21);
        assert(receiving.raw_calls == prior_reset.raw_calls + 1u &&
               receiving.strength_calls == prior_reset.strength_calls + 1u);
    }

    const Observer prior_init = receiving;
    cupid_gb_init(gb);
    retained_observers(gb, &receiving);
    assert(!cupid_gb_rumble_strength(gb) && !memcmp(&prior_init, &receiving, sizeof(receiving)));
    assert(cupid_gb_load_rom(gb, rom, ROM_SIZE));
    retained_observers(gb, &receiving);
    assert(!cupid_gb_rumble_strength(gb) && !memcmp(&prior_init, &receiving, sizeof(receiving)));
    cupid_gb_cleanup(gb);
    assert(!gb->rumble_callback && !gb->rumble_user_data && !gb->rumble_strength_callback &&
           !gb->rumble_strength_user_data && !gb->rumble_sample_callback &&
           !gb->rumble_sample_user_data && !cupid_gb_rumble_strength(gb));
    free(before);
    free(without_observers);
    free(saved);
}

int main(void) {
    assert(!cupid_gb_rumble_strength(NULL));
    cupid_gb_set_rumble_strength_callback(NULL, motor_strength, NULL);
    cupid_gb_set_rumble_sample_callback(NULL, motor_sample, NULL);
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t *rom = malloc(ROM_SIZE);
    assert(gb && rom);
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model) {
        test_levels(gb, rom, models[model]);
        test_mbc5(gb, rom, models[model]);
        test_lifetimes(gb, rom, models[model]);
    }
    free(rom);
    free(gb);
    puts("Cartridge motor strength and observer regressions passed.");
    return 0;
}

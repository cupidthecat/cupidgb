/**
 * @file    test_wave_history.c
 * @brief   CH3 fetch-byte lifetime, DAC boundaries, guest continuation and reset checks.
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
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"
#include "wave_history_fixture.h"

#include <stdio.h>

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

typedef struct Profile {
    CupidGbModel model;
    bool color, fast;
} Profile;

typedef struct Witness {
    unsigned writes, reads, stores, timer, raw_sample;
    uint16_t read_pc, store_pc;
    uint8_t stored;
} Witness;

static const Profile profiles[] = {
    {CUPID_GB_MODEL_DMG_ABC, false, false}, {CUPID_GB_MODEL_DMG0, false, false},
    {CUPID_GB_MODEL_MGB, false, false},     {CUPID_GB_MODEL_SGB, false, false},
    {CUPID_GB_MODEL_SGB2, false, false},    {CUPID_GB_MODEL_CGB, true, false},
    {CUPID_GB_MODEL_CGB, false, false},     {CUPID_GB_MODEL_CGB, true, true},
    {CUPID_GB_MODEL_CGB, false, true},
};

static unsigned guest_cases, fetch_cases, boundary_cases, reset_cases, dac_cases, dac_continuations;

static void observe(void *data, CupidGb *gb, uint16_t address, bool writing, uint8_t value) {
    Witness *witness = data;
    if (writing && address == 0xff1e) {
        ++witness->writes;
        witness->timer = gb->apu.ch3_timer;
        witness->raw_sample = gb->apu.ch3_sample;
    } else if (!writing && address == 0xff77) {
        /* Read callbacks precede the access. Only the completed guest
         * instruction, followed by its HRAM store, establishes the value. */
        ++witness->reads;
        witness->read_pc = gb->cpu.pc;
    } else if (writing && address == 0xff80) {
        ++witness->stores;
        witness->store_pc = gb->cpu.pc;
        witness->stored = value;
    }
}

static void odd_phase_restart(CupidGb *gb) {
    wave_history_arm(gb, 0x200, 0);
    unsigned steps = 0;
    while (!(gb->apu.fs_step & 1u)) {
        assert(++steps <= 5000 && cupid_gb_step(gb));
    }
    cupid_gb_write_u8(gb, 0xff1a, 0);
    cupid_gb_write_u8(gb, 0xff1e, 7);
    cupid_gb_write_u8(gb, 0xff1b, 0xff);
    for (unsigned index = 0; index < WAVE_HISTORY_BYTES; ++index)
        cupid_gb_write_u8(gb, (uint16_t)(0xff30 + index), wave_history_input[index]);
    cupid_gb_write_u8(gb, 0xff1a, 0x80);
    cupid_gb_write_u8(gb, 0xff1e, 0x87);
    assert(gb->apu.ch3_timer == 20 && gb->apu.ch3_len == 1);
    assert(!gb->apu.ch3_len_en && gb->apu.ch3_on);
}

static uint8_t complete_guest(CupidGb *gb, Witness *witness) {
    cupid_gb_set_memory_access_callback(gb, observe, witness);
    assert(cupid_gb_step(gb) && gb->cpu.pc == 0x103);
    assert(cupid_gb_step(gb) && gb->cpu.pc == 0x105);
    const uint8_t returned = gb->cpu.a;
    assert(cupid_gb_step(gb) && gb->cpu.pc == 0x107);
    assert(witness->writes == 1 && witness->reads == 1 && witness->stores == 1);
    assert(witness->read_pc == 0x105 && witness->store_pc == 0x107);
    assert(witness->stored == returned && gb->high_ram[0] == returned && gb->cpu.a == returned);
    cupid_gb_set_memory_access_callback(gb, NULL, NULL);
    return returned;
}

static void test_guest_pcm(void) {
    static const struct {
        unsigned next_nibble, timer;
        uint8_t control, expected;
    } cases[] = {{42, 4, 0x87, 6}, {43, 4, 0x87, 10}, {42, 2, 0x87, 9}, {42, 4, 0x07, 10}};

    for (unsigned color = 0; color < 2; ++color)
        for (unsigned fast = 0; fast < 2; ++fast)
            for (unsigned index = 0; index < COUNT_OF(cases); ++index) {
                CupidGb *gb = wave_history_new(CUPID_GB_MODEL_CGB, color != 0, fast != 0, 0x7f9);
                odd_phase_restart(gb);
                const unsigned access =
                    20u + (cases[index].next_nibble - 1u) * 14u - cases[index].timer;
                wave_history_advance(gb, access - (fast ? 6u : 12u));
                wave_history_arm(gb, 0x100, cases[index].control);
                Witness witness = {0};
                assert(complete_guest(gb, &witness) == cases[index].expected);
                assert(witness.timer == cases[index].timer);
                assert(gb->apu.ch3_len == 1 && !gb->apu.ch3_len_en && gb->apu.ch3_on);
                assert(!memcmp(gb->io_registers + 0x30, wave_history_input, WAVE_HISTORY_BYTES));
                wave_history_free(gb);
                ++guest_cases;
            }
}

static void test_wave_write_retains_fetch(void) {
    for (unsigned color = 0; color < 2; ++color)
        for (unsigned fast = 0; fast < 2; ++fast) {
            CupidGb *gb = wave_history_hold(color != 0, fast != 0);
            Witness witness = {0};
            assert(complete_guest(gb, &witness) == 6);
            assert(witness.raw_sample == 9 && witness.timer > 2);
            assert(gb->apu.ch3_sample_byte == 0x69 && gb->io_registers[0x34] == 0xe1);
            wave_history_free(gb);
            ++guest_cases;
        }
}

static void test_each_nibble_fetches(void) {
    for (unsigned color = 0; color < 2; ++color)
        for (unsigned fast = 0; fast < 2; ++fast) {
            CupidGb *gb = wave_history_new(CUPID_GB_MODEL_CGB, color != 0, fast != 0,
                                           WAVE_HISTORY_HOLD_FREQUENCY);
            wave_history_advance(gb, 134u + 7u * 128u + 32u);
            assert(gb->apu.ch3_wave_pos == 8 && gb->apu.ch3_timer == 96);
            assert(gb->apu.ch3_sample_byte == 0x69 && gb->apu.ch3_sample == 6);
            wave_history_arm(gb, 0x110, 0xe1);
            assert(cupid_gb_step(gb) && gb->cpu.pc == 0x112);
            assert(gb->io_registers[0x34] == 0xe1);
            assert(gb->apu.ch3_sample_byte == 0x69 && gb->apu.ch3_sample == 6);
            wave_history_advance(gb, gb->apu.ch3_timer);
            assert(gb->apu.ch3_wave_pos == 9 && gb->apu.ch3_timer == 128);
            assert(gb->apu.ch3_sample_byte == 0xe1 && gb->apu.ch3_sample == 1);
            wave_history_arm(gb, 0x120, 0);
            assert(cupid_gb_step(gb) && gb->cpu.a == 1);
            assert(cupid_gb_step(gb) && gb->high_ram[1] == 1);
            wave_history_free(gb);
            ++fetch_cases;
        }
}

static void test_level_and_dac_controls(void) {
    for (unsigned level = 0; level < 4; ++level) {
        CupidGb *gb = wave_history_hold(true, false);
        cupid_gb_write_u8(gb, 0xff1c, (uint8_t)(level << 5));
        wave_history_arm(gb, 0x120, 0);
        assert(cupid_gb_step(gb));
        const uint8_t expected[] = {0, 9, 4, 2};
        assert(gb->cpu.a == expected[level]);
        assert(cupid_gb_step(gb) && gb->high_ram[1] == expected[level]);
        assert(gb->apu.ch3_sample_byte == 0x69);
        wave_history_free(gb);
    }

    CupidGb *gb = wave_history_hold(true, false);
    cupid_gb_write_u8(gb, 0xff1a, 0);
    assert(!gb->apu.ch3_on && !gb->apu.ch3_dac && gb->apu.ch3_sample_byte == 0x69);
    /* No inactive clocks or NR30 countdown boundary are exercised here. */
    cupid_gb_write_u8(gb, 0xff1a, 0x80);
    wave_history_arm(gb, 0x100, 0x87);
    assert(wave_history_guest_result(gb) == 6 && gb->apu.ch3_sample_byte == 0x69);
    wave_history_free(gb);
}

typedef struct DacCase {
    uint16_t pc;
    unsigned access_ticks, timer, index;
    uint8_t retained, color_byte, monochrome_byte;
} DacCase;

typedef struct DacWitness {
    const DacCase *scenario;
    uint64_t origin;
    unsigned quantum, off, on, triggers, reads, stores;
    uint8_t stored;
} DacWitness;

typedef struct WaveSnapshot {
    uint8_t *bytes;
    size_t size;
} WaveSnapshot;

static WaveSnapshot capture_wave(const CupidGb *gb) {
    WaveSnapshot snapshot = {malloc(cupid_gb_state_size(gb)), cupid_gb_state_size(gb)};
    assert(snapshot.bytes);
    size_t written = 0;
    assert(cupid_gb_save_state(gb, snapshot.bytes, snapshot.size, &written) == CUPID_GB_STATE_OK);
    assert(written == snapshot.size);
    return snapshot;
}

static void observe_dac(void *data, CupidGb *gb, uint16_t address, bool writing, uint8_t value) {
    DacWitness *witness = data;
    const DacCase *scenario = witness->scenario;
    const uint64_t ticks = gb->elapsed_cycles - witness->origin;
    if (writing && address == 0xff1a && !value) {
        ++witness->off;
        assert(gb->cpu.pc == scenario->pc + 2 && ticks == scenario->access_ticks);
        assert(gb->apu.ch3_on && gb->apu.ch3_dac && !gb->apu.pending_cycles);
        assert(gb->apu.ch3_timer == scenario->timer && gb->apu.ch3_wave_pos == scenario->index);
        assert(gb->apu.ch3_sample_byte == scenario->retained);
        assert(gb->apu.ch3_sample ==
               ((scenario->index & 1) ? (scenario->retained & 15) : (scenario->retained >> 4)));
        assert(gb->apu.ch3_wave_access_ticks == (scenario->timer == 14));
    } else if (writing && address == 0xff1a) {
        ++witness->on;
        assert(value == 0x80 && gb->cpu.pc == scenario->pc + 6);
        assert(ticks == scenario->access_ticks + 5u * witness->quantum);
    } else if (writing && address == 0xff1e) {
        ++witness->triggers;
        assert(value == 0x80 && gb->cpu.pc == scenario->pc + 8);
        assert(ticks == scenario->access_ticks + 8u * witness->quantum);
    } else if (!writing && address == 0xff77) {
        /* The callback establishes timing only. The completed load and its
         * following HRAM store establish the returned PCM34 value. */
        ++witness->reads;
        assert(gb->cpu.pc == scenario->pc + 10);
        assert(ticks == scenario->access_ticks + 11u * witness->quantum);
    } else if (writing && address == 0xff80) {
        ++witness->stores;
        witness->stored = value;
        assert(gb->cpu.pc == scenario->pc + 12);
        assert(ticks == scenario->access_ticks + 14u * witness->quantum);
    }
}

static uint8_t dac_retained_byte(const CupidGb *gb, const DacCase *scenario) {
    return gb->model == CUPID_GB_MODEL_CGB ? scenario->color_byte : scenario->monochrome_byte;
}

static void finish_dac_program(CupidGb *gb, DacWitness *witness, bool include_off,
                               WaveSnapshot *after_off) {
    const DacCase *scenario = witness->scenario;
    const uint8_t expected_byte = dac_retained_byte(gb, scenario);
    cupid_gb_set_memory_access_callback(gb, observe_dac, witness);
    if (include_off)
        assert(cupid_gb_step(gb));
    assert(gb->cpu.pc == scenario->pc + 2 && !gb->apu.ch3_on && !gb->apu.ch3_dac);
    assert(gb->apu.ch3_sample_byte == expected_byte);
    assert(gb->apu.ch3_sample ==
           ((scenario->index & 1) ? (scenario->retained & 15) : (scenario->retained >> 4)));
    if (after_off)
        *after_off = capture_wave(gb);
    assert(cupid_gb_step(gb) && gb->cpu.pc == scenario->pc + 4 && gb->cpu.a == 0x80);
    assert(cupid_gb_step(gb) && gb->cpu.pc == scenario->pc + 6);
    assert(gb->apu.ch3_dac && !gb->apu.ch3_on && gb->apu.ch3_sample_byte == expected_byte);
    assert(cupid_gb_step(gb) && gb->cpu.pc == scenario->pc + 8 && gb->apu.ch3_on);
    assert(gb->apu.ch3_sample_byte == expected_byte && gb->apu.ch3_sample == (expected_byte >> 4));
    /* Only color hardware exposes the channel's output through PCM34. The
     * monochrome byte checks qualify retained core state, not audible output. */
    const uint8_t expected = gb->model == CUPID_GB_MODEL_CGB ? expected_byte >> 4 : 0xff;
    assert(cupid_gb_step(gb) && gb->cpu.pc == scenario->pc + 10 && gb->cpu.a == expected);
    assert(cupid_gb_step(gb) && gb->cpu.pc == scenario->pc + 12 && gb->high_ram[0] == expected);
    assert(witness->off == (unsigned)include_off && witness->on == 1 && witness->triggers == 1);
    assert(witness->reads == 1 && witness->stores == 1 && witness->stored == expected);
    assert(gb->apu.ch3_len == 256 && !gb->apu.ch3_len_en && gb->apu.ch3_on && gb->apu.ch3_dac);
    assert(!memcmp(gb->io_registers + 0x30, wave_history_input, WAVE_HISTORY_BYTES));
    cupid_gb_set_memory_access_callback(gb, NULL, NULL);
}

static void test_dac_boundary_history(void) {
    static const DacCase cases[] = {
        {0xc300, 592, 2, 9, 0x69, 0xca, 0xca},  {0xc344, 592, 2, 9, 0x69, 0x5c, 0x5c},
        {0xc300, 588, 6, 9, 0x69, 0x69, 0x69},  {0xc300, 596, 12, 10, 0xa6, 0xa6, 0xa6},
        {0xc300, 580, 14, 9, 0x69, 0x69, 0x87},
    };
    /* Disable, enable, trigger at a long period, then read PCM34 and store it.
     * WRAM holds the original program so native reloads restore its bytes too. */
    static const uint8_t guest[] = {0xe0, 0x1a, 0x3e, 0x80, 0xe0, 0x1a,
                                    0xe0, 0x1e, 0xf0, 0x77, 0xe0, 0x80};
    for (unsigned profile_index = 0; profile_index < COUNT_OF(profiles); ++profile_index) {
        const Profile *profile = &profiles[profile_index];
        const unsigned quantum = profile->fast ? 2u : 4u;
        for (unsigned index = 0; index < COUNT_OF(cases); ++index) {
            const DacCase *scenario = &cases[index];
            CupidGb *gb = wave_history_new(profile->model, profile->color, profile->fast, 0x7f9);
            cupid_gb_write_u8(gb, 0xff1b, 0);
            cupid_gb_write_u8(gb, 0xff80, 0xcc);
            for (unsigned offset = 0; offset < sizeof(guest); ++offset)
                cupid_gb_write_u8(gb, (uint16_t)(scenario->pc + offset), guest[offset]);
            const uint64_t origin = gb->elapsed_cycles;
            wave_history_advance(gb, scenario->access_ticks - 2u * quantum);
            wave_history_arm(gb, scenario->pc, 0);
            WaveSnapshot saved[] = {capture_wave(gb), {0}};
            DacWitness witness = {.scenario = scenario, .origin = origin, .quantum = quantum};
            finish_dac_program(gb, &witness, true, &saved[1]);
            ++dac_cases;

            for (unsigned after = 0; after < 2; ++after)
                for (unsigned live = 0; live < 2; ++live) {
                    CupidGb *target = wave_history_new(profile->model, profile->color,
                                                       profile->fast, WAVE_HISTORY_HOLD_FREQUENCY);
                    /* Reach a different retained byte naturally. A loader that
                     * forgets fetch history cannot pass by retaining old data. */
                    wave_history_advance(target, 166);
                    assert(target->apu.ch3_sample_byte == 0x96);
                    CupidGbStateResult result =
                        live ? cupid_gb_load_state_live(target, saved[after].bytes,
                                                        saved[after].size)
                             : cupid_gb_load_state(target, saved[after].bytes, saved[after].size);
                    assert(result == CUPID_GB_STATE_OK);
                    DacWitness replay = {
                        .scenario = scenario, .origin = origin, .quantum = quantum};
                    finish_dac_program(target, &replay, !after, NULL);
                    wave_history_free(target);
                    ++dac_continuations;
                }
            free(saved[0].bytes);
            free(saved[1].bytes);
            wave_history_free(gb);
        }
    }
}

static void test_repeated_dac_disconnect(void) {
    static const uint8_t guest[] = {0xe0, 0x1a, 0xe0, 0x1a, 0x3e, 0x80, 0xe0,
                                    0x1a, 0xe0, 0x1e, 0xf0, 0x77, 0xe0, 0x80};
    for (unsigned index = 0; index < COUNT_OF(profiles); ++index) {
        const Profile *profile = &profiles[index];
        CupidGb *gb = wave_history_new(profile->model, profile->color, profile->fast, 0x7f9);
        for (unsigned offset = 0; offset < sizeof(guest); ++offset)
            cupid_gb_write_u8(gb, (uint16_t)(0xc300 + offset), guest[offset]);
        wave_history_advance(gb, 592u - (profile->fast ? 4u : 8u));
        wave_history_arm(gb, 0xc300, 0);
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0xc302);
        assert(!gb->apu.ch3_on && !gb->apu.ch3_dac && gb->apu.ch3_sample_byte == 0xca);
        /* The stopped timer still lies on the boundary. A second disconnect
         * has a different PC but must not replace the first retained byte. */
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0xc304);
        assert(gb->apu.ch3_timer == 2 && gb->apu.ch3_sample_byte == 0xca);
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0xc306 && gb->cpu.a == 0x80);
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0xc308);
        assert(!gb->apu.ch3_on && gb->apu.ch3_dac && gb->apu.ch3_sample_byte == 0xca);
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0xc30a && gb->apu.ch3_sample == 12);
        const uint8_t expected = profile->model == CUPID_GB_MODEL_CGB ? 12 : 0xff;
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0xc30c && gb->cpu.a == expected);
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0xc30e && gb->high_ram[0] == expected);
        assert(!memcmp(gb->io_registers + 0x30, wave_history_input, WAVE_HISTORY_BYTES));
        wave_history_free(gb);
        ++dac_cases;
    }
}

static void test_length_boundary_refresh(void) {
    static const uint8_t corrupted[WAVE_HISTORY_BYTES] = {0xa6, 0xa6, 0x5c, 0xf0, 0xa6, 0xa6,
                                                          0x5c, 0xf0, 0x3c, 0xc3, 0x87, 0x78,
                                                          0x55, 0xaa, 0xe1, 0x1e};
    for (unsigned index = 0; index < COUNT_OF(profiles); ++index) {
        const Profile *profile = &profiles[index];
        CupidGb *gb = wave_history_new(profile->model, profile->color, profile->fast, 0x7f9);
        odd_phase_restart(gb);
        wave_history_advance(gb, 144u - (profile->fast ? 6u : 12u));
        wave_history_arm(gb, 0x100, 0xc7);
        Witness witness = {0};
        cupid_gb_set_memory_access_callback(gb, observe, &witness);
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0x103);
        cupid_gb_set_memory_access_callback(gb, NULL, NULL);
        assert(witness.writes == 1 && witness.timer == 2 && witness.raw_sample == 9);
        const bool physical_color = profile->model == CUPID_GB_MODEL_CGB;
        const uint8_t *expected = physical_color ? wave_history_input : corrupted;
        assert(!memcmp(gb->io_registers + 0x30, expected, WAVE_HISTORY_BYTES));
        assert(gb->apu.ch3_sample_byte == (physical_color ? 0x96 : 0xa6));
        assert(gb->apu.ch3_sample == (physical_color ? 9 : 10));
        assert(gb->apu.ch3_on && gb->apu.ch3_dac && gb->apu.ch3_len_en);
        assert(gb->apu.ch3_len == 255 && (gb->apu.fs_step & 1u));
        wave_history_free(gb);
        ++boundary_cases;
    }
}

static void test_reset_history(void) {
    for (unsigned kind = 0; kind < 3; ++kind) {
        CupidGb *gb = wave_history_hold(true, false);
        assert(gb->apu.ch3_sample_byte == 0x69);
        if (kind == 0) {
            cupid_gb_write_u8(gb, 0xff26, 0);
            assert(!gb->apu.ch3_sample_byte);
            cupid_gb_write_u8(gb, 0xff26, 0x80);
        } else {
            assert(cupid_gb_reset(gb, kind == 1 ? CUPID_GB_RESET_HARD : CUPID_GB_RESET_QUICK));
        }
        assert(!gb->apu.ch3_sample_byte);
        wave_history_free(gb);
        ++reset_cases;
    }
}

int main(void) {
    test_guest_pcm();
    test_wave_write_retains_fetch();
    test_each_nibble_fetches();
    test_level_and_dac_controls();
    test_dac_boundary_history();
    test_repeated_dac_disconnect();
    test_length_boundary_refresh();
    test_reset_history();
    printf("Wave history checks passed: %u guest results, %u nibble fetches, "
           "%u DAC boundaries, %u DAC continuations, %u length boundaries and %u reset cases.\n",
           guest_cases, fetch_cases, dac_cases, dac_continuations, boundary_cases, reset_cases);
    return 0;
}

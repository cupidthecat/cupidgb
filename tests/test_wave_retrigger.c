/**
 * @file    test_wave_retrigger.c
 * @brief   CH3 wave-memory readbacks, guest-store timing, and native continuation regressions.
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

#include "cupid/gb/apu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

enum { WAVE_BYTES = 16, ROM_BYTES = 32768, PERIOD_T = 14, FIRST_FETCH_T = 20 };

typedef struct Profile {
    const char *name;
    CupidGbModel model;
    bool color, fast;
} Profile;

typedef enum Action { RETRIGGER, INACTIVE, DAC_OFF, NO_TRIGGER, IDLE, DELAYED_OFF } Action;

typedef struct Spec {
    const Profile *profile;
    unsigned offset, nibble, pattern;
    int window;
    Action action;
} Spec;

typedef struct Guest {
    CupidGb *gb;
    uint64_t origin;
    unsigned preclock, event_t;
} Guest;

typedef struct Watch {
    unsigned writes, index, timer, pending, pc;
    uint64_t clocks;
    uint8_t wave[WAVE_BYTES];
} Watch;

typedef struct Blob {
    uint8_t *bytes;
    size_t length;
} Blob;

static const Profile profiles[] = {{"DMG", CUPID_GB_MODEL_DMG_ABC, false, false},
                                   {"DMG0", CUPID_GB_MODEL_DMG0, false, false},
                                   {"MGB", CUPID_GB_MODEL_MGB, false, false},
                                   {"SGB", CUPID_GB_MODEL_SGB, false, false},
                                   {"SGB2", CUPID_GB_MODEL_SGB2, false, false},
                                   {"CGB", CUPID_GB_MODEL_CGB, true, false},
                                   {"CGB compatibility", CUPID_GB_MODEL_CGB, false, false},
                                   {"CGB double speed", CUPID_GB_MODEL_CGB, true, true}};

static const uint8_t mixed_input[WAVE_BYTES] = {0x96, 0x35, 0xca, 0x6b, 0x69, 0xa6, 0x5c, 0xf0,
                                                0x3c, 0xc3, 0x87, 0x78, 0x55, 0xaa, 0xe1, 0x1e};

/* Complete readbacks include both the destination and the original source.
 * Literal data keeps the expected result independent of the mutation formula.
 * The first table covers DMG/SGB/SGB2; the second covers Pocket. */
static const char *const mixed_readbacks[2][16][2] = {
    {{"9635ca6b69a65cf03cc3877855aae11e", "69ca35949659a30fc33c7887aa551ee1"},
     {"3535ca6b69a65cf03cc3877855aae11e", "caca35949659a30fc33c7887aa551ee1"},
     {"ca35ca6b69a65cf03cc3877855aae11e", "35ca35949659a30fc33c7887aa551ee1"},
     {"6b35ca6b69a65cf03cc3877855aae11e", "94ca35949659a30fc33c7887aa551ee1"},
     {"69a65cf069a65cf03cc3877855aae11e", "9659a30f9659a30fc33c7887aa551ee1"},
     {"a6a65cf0a6a65cf03cc3877855aae11e", "5959a30f5959a30fc33c7887aa551ee1"},
     {"5ca65cf05ca65cf03cc3877855aae11e", "a359a30fa359a30fc33c7887aa551ee1"},
     {"f0a65cf0f0a65cf03cc3877855aae11e", "0f59a30f0f59a30fc33c7887aa551ee1"},
     {"3cc3877869a65cf03cc3877855aae11e", "c33c78879659a30fc33c7887aa551ee1"},
     {"96c3877869a65cf096c3877855aae11e", "693c78879659a30f693c7887aa551ee1"},
     {"96c3877869a65cf096c3877855aae11e", "693c78879659a30f693c7887aa551ee1"},
     {"3cc3877869a65cf03cc3877855aae11e", "c33c78879659a30fc33c7887aa551ee1"},
     {"55aae11e69a65cf03cc3877855aae11e", "aa551ee19659a30fc33c7887aa551ee1"},
     {"96aae11e69a65cf03cc3877896aae11e", "69551ee19659a30fc33c788769551ee1"},
     {"d5aae11e69a65cf03cc38778d5aae11e", "2a551ee19659a30fc33c78872a551ee1"},
     {"16aae11e69a65cf03cc3877816aae11e", "e9551ee19659a30fc33c7887e9551ee1"}},
    {{"9635ca6b69a65cf03cc3877855aae11e", "69ca35949659a30fc33c7887aa551ee1"},
     {"9635ca6b69a65cf03cc3877855aae11e", "69ca35949659a30fc33c7887aa551ee1"},
     {"9635ca6b69a65cf03cc3877855aae11e", "69ca35949659a30fc33c7887aa551ee1"},
     {"9635ca6b69a65cf03cc3877855aae11e", "69ca35949659a30fc33c7887aa551ee1"},
     {"69a65cf069a65cf03cc3877855aae11e", "9659a30f9659a30fc33c7887aa551ee1"},
     {"a6a65cf0a6a65cf03cc3877855aae11e", "5959a30f5959a30fc33c7887aa551ee1"},
     {"5ca65cf05ca65cf03cc3877855aae11e", "a359a30fa359a30fc33c7887aa551ee1"},
     {"f0a65cf0f0a65cf03cc3877855aae11e", "0f59a30f0f59a30fc33c7887aa551ee1"},
     {"3cc3877869a65cf03cc3877855aae11e", "c33c78879659a30fc33c7887aa551ee1"},
     {"96c3877869a65cf096c3877855aae11e", "693c78879659a30f693c7887aa551ee1"},
     {"96c3877869a65cf096c3877855aae11e", "693c78879659a30f693c7887aa551ee1"},
     {"3cc3877869a65cf03cc3877855aae11e", "c33c78879659a30fc33c7887aa551ee1"},
     {"55aae11e69a65cf03cc3877855aae11e", "aa551ee19659a30fc33c7887aa551ee1"},
     {"96aae11e69a65cf03cc3877896aae11e", "69551ee19659a30fc33c788769551ee1"},
     {"d5aae11e69a65cf03cc38778d5aae11e", "2a551ee19659a30fc33c78872a551ee1"},
     {"16aae11e69a65cf03cc3877816aae11e", "e9551ee19659a30fc33c7887e9551ee1"}}};

/* Eight assignments to byte zero, the source-block head, and its next byte.
 * Each nonzero source block has its own complete expected memory image. */
static const char *const bit_readbacks[3][8] = {
    {"00005cf000005cf03cc3877855aae11e", "00005cf000005cf03cc3877855aae11e",
     "00005cf000005cf03cc3877855aae11e", "ff005cf0ff005cf03cc3877855aae11e",
     "00ff5cf000ff5cf03cc3877855aae11e", "ffff5cf0ffff5cf03cc3877855aae11e",
     "ffff5cf0ffff5cf03cc3877855aae11e", "ffff5cf0ffff5cf03cc3877855aae11e"},
    {"0000877869a65cf00000877855aae11e", "0000877869a65cf00000877855aae11e",
     "0000877869a65cf00000877855aae11e", "ff00877869a65cf0ff00877855aae11e",
     "00ff877869a65cf000ff877855aae11e", "ffff877869a65cf0ffff877855aae11e",
     "ffff877869a65cf0ffff877855aae11e", "ffff877869a65cf0ffff877855aae11e"},
    {"0000e11e69a65cf03cc387780000e11e", "0000e11e69a65cf03cc387780000e11e",
     "0000e11e69a65cf03cc387780000e11e", "ff00e11e69a65cf03cc38778ff00e11e",
     "00ffe11e69a65cf03cc3877800ffe11e", "ffffe11e69a65cf03cc38778ffffe11e",
     "ffffe11e69a65cf03cc38778ffffe11e", "ffffe11e69a65cf03cc38778ffffe11e"}};

static unsigned readback_cases, bit_cases, control_cases, guest_cases, continuation_cases;

static void input_bytes(const Spec *spec, uint8_t bytes[WAVE_BYTES]) {
    for (unsigned index = 0; index < WAVE_BYTES; ++index)
        bytes[index] = spec->pattern == 9 ? (uint8_t)~mixed_input[index] : mixed_input[index];
    if (spec->pattern < 8) {
        bytes[0] = spec->pattern & 1u ? 0xff : 0;
        bytes[spec->offset & ~3u] = spec->pattern & 2u ? 0xff : 0;
        bytes[spec->offset] = spec->pattern & 4u ? 0xff : 0;
    }
}

static uint8_t hex_digit(char digit) {
    assert((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'));
    return (uint8_t)(digit <= '9' ? digit - '0' : digit - 'a' + 10);
}

static void expected_bytes(const Spec *spec, uint8_t bytes[WAVE_BYTES]) {
    input_bytes(spec, bytes);
    if (spec->profile->model == CUPID_GB_MODEL_CGB || (spec->window != -2 && spec->window != -1) ||
        (spec->action != RETRIGGER && spec->action != DELAYED_OFF))
        return;
    const char *text;
    if (spec->pattern < 8) {
        assert(spec->offset == 5 || spec->offset == 9 || spec->offset == 13);
        text = bit_readbacks[(spec->offset - 5u) / 4u][spec->pattern];
    } else {
        const unsigned pocket = spec->profile->model == CUPID_GB_MODEL_MGB;
        text = mixed_readbacks[pocket][spec->offset][spec->pattern - 8u];
    }
    assert(strlen(text) == WAVE_BYTES * 2u);
    for (unsigned index = 0; index < WAVE_BYTES; ++index)
        bytes[index] = (uint8_t)(hex_digit(text[index * 2]) * 16u + hex_digit(text[index * 2 + 1]));
}

static void assert_wave(const CupidGb *gb, const Spec *spec, const uint8_t expected[WAVE_BYTES]) {
    /* A mapped wave-port read can synchronize the APU. Inspect stored bytes
     * directly so validation cannot move the register-write boundary. */
    if (memcmp(gb->io_registers + 0x30, expected, WAVE_BYTES) == 0)
        return;
    fprintf(stderr, "%s offset=%u nibble=%u pattern=%u window=%d action=%u\n", spec->profile->name,
            spec->offset, spec->nibble, spec->pattern, spec->window, (unsigned)spec->action);
    for (unsigned index = 0; index < WAVE_BYTES; ++index)
        fprintf(stderr, "%04x actual=%02x expected=%02x\n", 0xff30u + index,
                gb->io_registers[0x30u + index], expected[index]);
    assert(false);
}

static CupidGb *create_machine(const Profile *profile, bool cartridge) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, profile->model);
    if (cartridge) {
        uint8_t rom[ROM_BYTES] = {0};
        rom[0x100] = 0xea; /* LD (FF1E),A, then two NOPs. */
        rom[0x101] = 0x1e;
        rom[0x102] = 0xff;
        memcpy(rom + 0x134, "WAVE MEMORY", 11);
        rom[0x143] = profile->color ? 0x80 : 0;
        rom[0x146] = 3;
        rom[0x14b] = 0x33;
        for (unsigned address = 0x134; address <= 0x14c; ++address)
            rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);
        cupid_gb_set_power_on_seed(gb, UINT64_C(0x574156452d544553));
        assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
        assert(gb->model == profile->model && gb->header.header_checksum_valid);
        assert(gb->cgb_mode == profile->color);
    } else {
        gb->cgb_mode = profile->color;
    }
    gb->double_speed = profile->fast;
    assert(cupid_gb_base_clock_hz(gb) ==
           (profile->model == CUPID_GB_MODEL_SGB ? 4295454u : 4194304u));
    assert(cupid_gb_apu_set_sample_rate(gb, 48000));
    return gb;
}

static void destroy_machine(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void prepare_wave(CupidGb *gb, const Spec *spec) {
    static const struct {
        uint16_t address;
        uint8_t value;
    } setup[] = {{0xff40, 0},    {0xffff, 0}, {0xff0f, 0},    {0xff07, 0},
                 {0xff04, 0},    {0xff26, 0}, {0xff26, 0x80}, {0xff24, 0x77},
                 {0xff25, 0x44}, {0xff1b, 0}, {0xff1c, 0x20}, {0xff1d, 0xf9}};

    for (unsigned index = 0; index < COUNT_OF(setup); ++index)
        cupid_gb_write_u8(gb, setup[index].address, setup[index].value);
    uint8_t bytes[WAVE_BYTES];
    input_bytes(spec, bytes);
    for (unsigned index = 0; index < WAVE_BYTES; ++index)
        cupid_gb_write_u8(gb, (uint16_t)(0xff30u + index), bytes[index]);
    cupid_gb_write_u8(gb, 0xff1a, 0x80);
    cupid_gb_write_u8(gb, 0xff1e, 0x87);
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
    gb->cpu.halt_bug = false;
    gb->cpu.halted = false;
    gb->cpu.just_halted = false;
    gb->cpu.stopped = false;
    assert(gb->apu.ch3_timer == FIRST_FETCH_T && gb->apu.ch3_wave_pos == 0);
    assert(gb->apu.ch3_on && gb->apu.ch3_dac);
}

static unsigned event_time(const Spec *spec) {
    const unsigned fetch = 32u + 2u * spec->offset + spec->nibble;
    return (unsigned)((int)(FIRST_FETCH_T + (fetch - 1u) * PERIOD_T) + spec->window);
}

static void capture(const CupidGb *gb, Watch *watch) {
    watch->index = gb->apu.ch3_wave_pos;
    watch->timer = gb->apu.ch3_timer;
    watch->pending = gb->apu.pending_cycles;
    watch->pc = gb->cpu.pc;
    watch->clocks = gb->elapsed_cycles;
    memcpy(watch->wave, gb->io_registers + 0x30, WAVE_BYTES);
}

static void assert_boundary(const Spec *spec, const Watch *watch) {
    const unsigned fetch = 32u + 2u * spec->offset + spec->nibble;
    const unsigned index = (fetch - (spec->window < 0)) & 31u;
    const unsigned timer =
        spec->window < 0 ? (unsigned)-spec->window : PERIOD_T - (unsigned)spec->window;
    uint8_t input[WAVE_BYTES];
    input_bytes(spec, input);
    assert(watch->index == index && watch->timer == timer && !watch->pending);
    assert(memcmp(watch->wave, input, WAVE_BYTES) == 0);
}

static void run_component(const Spec *spec) {
    CupidGb *gb = create_machine(spec->profile, false);
    prepare_wave(gb, spec);
    cupid_gb_tick_apu(gb, (uint16_t)event_time(spec));
    if (spec->action == INACTIVE || spec->action == DAC_OFF) {
        cupid_gb_write_u8(gb, 0xff1a, 0);
        if (spec->action == INACTIVE)
            cupid_gb_write_u8(gb, 0xff1a, 0x80);
    }
    Watch before = {0};
    capture(gb, &before);
    assert_boundary(spec, &before);
    assert(gb->apu.ch3_on == (spec->action != INACTIVE && spec->action != DAC_OFF));
    if (spec->action != IDLE)
        cupid_gb_write_u8(gb, 0xff1e, spec->action == NO_TRIGGER ? 7 : 0x87);
    uint8_t expected[WAVE_BYTES];
    expected_bytes(spec, expected);
    assert_wave(gb, spec, expected);
    assert(gb->apu.ch3_on == (spec->action != DAC_OFF));
    if (spec->action != IDLE && spec->action != NO_TRIGGER) {
        assert(gb->apu.ch3_timer == FIRST_FETCH_T);
        assert(gb->apu.ch3_wave_pos == 0 && !gb->apu.ch3_started);
    }
    cupid_gb_tick_apu(gb, 8);
    if (spec->action == DELAYED_OFF)
        cupid_gb_write_u8(gb, 0xff1a, 0);
    assert_wave(gb, spec, expected);
    assert(gb->apu.ch3_on == (spec->action != DAC_OFF && spec->action != DELAYED_OFF));
    destroy_machine(gb);
}

static void test_complete_readbacks(void) {
    const int windows[] = {-4, -3, -2, -1, 0, 1};
    for (unsigned model = 0; model < COUNT_OF(profiles); ++model)
        for (unsigned offset = 0; offset < WAVE_BYTES; ++offset)
            for (unsigned nibble = 0; nibble < 2; ++nibble)
                for (unsigned pattern = 8; pattern < 10; ++pattern)
                    for (unsigned window = 0; window < COUNT_OF(windows); ++window) {
                        const Spec spec = {&profiles[model], offset,          nibble,
                                           pattern,          windows[window], RETRIGGER};
                        run_component(&spec);
                        ++readback_cases;
                    }
}

static void test_bit_inputs(void) {
    const unsigned offsets[] = {5, 9, 13};
    for (unsigned model = 0; model < COUNT_OF(profiles); ++model)
        for (unsigned block = 0; block < COUNT_OF(offsets); ++block)
            for (unsigned nibble = 0; nibble < 2; ++nibble)
                for (unsigned pattern = 0; pattern < 8; ++pattern)
                    for (int window = -2; window <= -1; ++window) {
                        const Spec spec = {&profiles[model], offsets[block], nibble,
                                           pattern,          window,         RETRIGGER};
                        run_component(&spec);
                        ++bit_cases;
                    }
}

static void test_controls(void) {
    const unsigned offsets[] = {1, 5, 9, 13};
    for (unsigned model = 0; model < COUNT_OF(profiles); ++model)
        for (unsigned offset = 0; offset < COUNT_OF(offsets); ++offset)
            for (unsigned nibble = 0; nibble < 2; ++nibble)
                for (int window = -3; window <= -2; ++window)
                    for (unsigned action = INACTIVE; action <= DELAYED_OFF; ++action) {
                        const Spec spec = {&profiles[model], offsets[offset], nibble, 8,
                                           window,           (Action)action};
                        run_component(&spec);
                        ++control_cases;
                    }
}

static void step(CupidGb *gb) {
    assert(cupid_gb_step(gb));
}

static Guest prepare_guest(const Spec *spec) {
    Guest guest = {.gb = create_machine(spec->profile, true), .event_t = event_time(spec)};
    CupidGb *gb = guest.gb;
    prepare_wave(gb, spec);
    guest.origin = gb->elapsed_cycles;
    const unsigned nop_t = spec->profile->fast ? 2u : 4u;
    guest.preclock = guest.event_t % nop_t;
    cupid_gb_tick_apu(gb, (uint16_t)guest.preclock);
    gb->cpu.pc = 0x200;
    const unsigned warmup = (guest.event_t - guest.preclock) / nop_t - 3u;
    for (unsigned index = 0; index < warmup; ++index)
        step(gb);
    assert(gb->cpu.pc == 0x200u + warmup);
    gb->cpu.pc = spec->action == IDLE ? 0x110 : 0x100;
    gb->cpu.a = spec->action == NO_TRIGGER ? 7 : 0x87;
    return guest;
}

static void watch_write(void *data, CupidGb *gb, uint16_t address, bool writing, uint8_t value) {
    (void)value;
    if (writing && address == 0xff1e) {
        Watch *watch = data;
        ++watch->writes;
        capture(gb, watch);
    }
}

static void run_guest_instruction(Guest *guest, const Spec *spec) {
    CupidGb *gb = guest->gb;
    Watch watch = {0};
    const unsigned start_pc = spec->action == IDLE ? 0x110u : 0x100u;
    const unsigned nop_t = spec->profile->fast ? 2u : 4u;
    if (spec->action == IDLE) {
        for (unsigned index = 0; index < 3; ++index)
            step(gb);
        capture(gb, &watch);
        step(gb);
    } else {
        cupid_gb_set_memory_access_callback(gb, watch_write, &watch);
        step(gb);
        cupid_gb_set_memory_access_callback(gb, NULL, NULL);
    }
    assert(watch.writes == (spec->action != IDLE));
    assert_boundary(spec, &watch);
    assert(watch.clocks - guest->origin + guest->preclock == guest->event_t);
    assert(watch.pc == start_pc + 3u);
    assert(gb->elapsed_cycles - guest->origin + guest->preclock == guest->event_t + nop_t);
    assert(gb->cpu.pc == start_pc + (spec->action == IDLE ? 4u : 3u));
    uint8_t expected[WAVE_BYTES];
    expected_bytes(spec, expected);
    assert_wave(gb, spec, expected);
}

static void test_guest_stores(void) {
    const int windows[] = {-4, -2, 0};
    const Action actions[] = {RETRIGGER, NO_TRIGGER, IDLE, DELAYED_OFF};
    for (unsigned model = 0; model < COUNT_OF(profiles); ++model)
        for (unsigned offset = 0; offset < WAVE_BYTES; ++offset)
            for (unsigned nibble = 0; nibble < 2; ++nibble)
                for (unsigned window = 0; window < COUNT_OF(windows); ++window)
                    for (unsigned action = 0; action < COUNT_OF(actions); ++action) {
                        const Spec spec = {&profiles[model], offset,         nibble, 8,
                                           windows[window],  actions[action]};
                        Guest guest = prepare_guest(&spec);
                        run_guest_instruction(&guest, &spec);
                        step(guest.gb);
                        step(guest.gb);
                        if (spec.action == DELAYED_OFF)
                            cupid_gb_write_u8(guest.gb, 0xff1a, 0);
                        uint8_t expected[WAVE_BYTES];
                        expected_bytes(&spec, expected);
                        assert_wave(guest.gb, &spec, expected);
                        destroy_machine(guest.gb);
                        ++guest_cases;
                    }
}

static Blob encode(const CupidGb *gb) {
    Blob blob = {NULL, cupid_gb_state_size(gb)};
    assert(blob.length > 128);
    blob.bytes = malloc(blob.length);
    assert(blob.bytes);
    size_t written = 0;
    assert(cupid_gb_save_state(gb, blob.bytes, blob.length, &written) == CUPID_GB_STATE_OK);
    assert(written == blob.length && blob.bytes[8] == CUPID_GB_STATE_VERSION);
    return blob;
}

static void assert_same_state(const CupidGb *left, const CupidGb *right, bool live) {
    CupidGb *settled = NULL;
    if (live) {
        settled = malloc(sizeof(*settled));
        assert(settled);
        memcpy(settled, left, sizeof(*settled));
        cupid_gb_apu_run(settled, true);
        settled->apu.output = right->apu.output;
        left = settled;
    }
    Blob a = encode(left), b = encode(right);
    assert(a.length == b.length && memcmp(a.bytes, b.bytes, a.length) == 0);
    free(a.bytes);
    free(b.bytes);
    free(settled);
}

static void load(CupidGb *gb, const Blob *blob, unsigned api) {
    CupidGbStateContext context = {0};
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    CupidGbStateResult result;
    if (api == 0)
        result = cupid_gb_load_state(gb, blob->bytes, blob->length);
    else if (api == 1)
        result = cupid_gb_load_state_with_context(gb, blob->bytes, blob->length, &context);
    else if (api == 2)
        result = cupid_gb_load_state_live(gb, blob->bytes, blob->length);
    else
        result = cupid_gb_load_state_live_with_context(gb, blob->bytes, blob->length, &context);
    assert(result == CUPID_GB_STATE_OK);
}

static void compare_pcm(CupidGb *left, CupidGb *right) {
    int16_t a[CUPID_GB_APU_BUF_FRAMES * 2], b[CUPID_GB_APU_BUF_FRAMES * 2];
    const uint32_t frames = cupid_gb_apu_drain(left, a, CUPID_GB_APU_BUF_FRAMES);
    assert(frames == cupid_gb_apu_drain(right, b, CUPID_GB_APU_BUF_FRAMES));
    assert(memcmp(a, b, frames * 2u * sizeof(*a)) == 0);
}

static void run_continuation(const Spec *spec, bool after, unsigned api) {
    Guest source = prepare_guest(spec);
    if (after)
        run_guest_instruction(&source, spec);
    /* Native capture happens at instruction boundaries. No observer reenters
     * the machine or attempts to save a partially executed CPU instruction. */
    Blob saved = encode(source.gb);
    CupidGb *restored = create_machine(spec->profile, true);
    assert(cupid_gb_apu_set_sample_rate(restored, 44100));
    cupid_gb_tick_apu(restored, 127);
    CupidGbAudioOutput *receiving = malloc(sizeof(*receiving));
    assert(receiving);
    memcpy(receiving, &restored->apu.output, sizeof(*receiving));
    receiving->sample_fraction = 0;
    receiving->fraction_half = false;
    load(restored, &saved, api);
    if (api >= 2)
        assert(memcmp(receiving, &restored->apu.output, sizeof(*receiving)) == 0);
    assert_same_state(source.gb, restored, api >= 2);
    uint8_t expected[WAVE_BYTES];
    if (after)
        expected_bytes(spec, expected);
    else
        input_bytes(spec, expected);
    assert_wave(restored, spec, expected);
    Guest target = source;
    target.gb = restored;
    if (!after) {
        run_guest_instruction(&source, spec);
        run_guest_instruction(&target, spec);
    }
    for (unsigned index = 0; index < 2; ++index) {
        step(source.gb);
        step(restored);
    }
    expected_bytes(spec, expected);
    assert_wave(source.gb, spec, expected);
    assert_wave(restored, spec, expected);
    if (api < 2)
        compare_pcm(source.gb, restored);
    assert_same_state(source.gb, restored, api >= 2);
    free(receiving);
    free(saved.bytes);
    destroy_machine(source.gb);
    destroy_machine(restored);
}

static void test_native_continuation(void) {
    const unsigned offsets[] = {1, 5};
    for (unsigned model = 0; model < COUNT_OF(profiles); ++model)
        for (unsigned offset = 0; offset < COUNT_OF(offsets); ++offset)
            for (unsigned nibble = 0; nibble < 2; ++nibble)
                for (unsigned after = 0; after < 2; ++after)
                    for (unsigned api = 0; api < 4; ++api) {
                        const Spec spec = {&profiles[model], offsets[offset], nibble, 8, -2,
                                           RETRIGGER};
                        run_continuation(&spec, after != 0, api);
                        ++continuation_cases;
                    }
}

int main(void) {
    test_complete_readbacks();
    test_bit_inputs();
    test_controls();
    test_guest_stores();
    test_native_continuation();
    assert(readback_cases == 3072 && bit_cases == 768 && control_cases == 640);
    assert(guest_cases == 3072 && continuation_cases == 256);
    printf("Wave-memory checks: %u complete readbacks, %u bit inputs, %u controls, "
           "%u guest stores and %u native continuations.\n",
           readback_cases, bit_cases, control_cases, guest_cases, continuation_cases);
    return 0;
}

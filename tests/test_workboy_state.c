/**
 * @file    test_workboy_state.c
 * @brief   WorkBoy serial continuation across native, portable, reset and cartridge boundaries.
 * @author  cupidthecat
 * @date    2026-11-24
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

#include "../src/gb/workboy_internal.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/bess.h"
#include "cupid/gb/printer.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/timer.h"
#include "test_serial_helpers.h"

#include <stdio.h>

enum { SB = 0xff01, SC = 0xff02, DEVICE_DESCRIPTOR_BYTES = 8, STATE_HEADER_BYTES = 128 };

typedef struct Clock {
    unsigned reads;
    unsigned writes;
    int64_t seconds;
} Clock;

typedef struct Blob {
    uint8_t *data;
    size_t size;
} Blob;

static int64_t read_clock(void *context) {
    Clock *clock = context;
    ++clock->reads;
    return clock->seconds;
}

static void write_clock(int64_t seconds, void *context) {
    Clock *clock = context;
    ++clock->writes;
    clock->seconds = seconds;
}

static uint8_t finish_byte(CupidGb *gb) {
    for (unsigned ticks = 0; gb->serial_bits_remaining && ticks < 1100; ++ticks)
        cupid_gb_tick(gb, 4);
    assert(!gb->serial_bits_remaining);
    return cupid_gb_read_u8(gb, SB);
}

static uint8_t transfer(CupidGb *gb, uint8_t byte) {
    cupid_gb_write_u8(gb, SB, byte);
    cupid_gb_write_u8(gb, SC, 0x81);
    return finish_byte(gb);
}

static Blob capture(const CupidGb *gb) {
    Blob blob = {NULL, cupid_gb_state_size(gb)};
    assert(blob.size);
    blob.data = malloc(blob.size);
    assert(blob.data);
    size_t written;
    assert(cupid_gb_save_state(gb, blob.data, blob.size, &written) == CUPID_GB_STATE_OK);
    assert(written == blob.size && blob.data[8] == CUPID_GB_STATE_VERSION);
    return blob;
}

static void expect_state(const CupidGb *gb, const Blob *expected) {
    Blob actual = capture(gb);
    assert(actual.size == expected->size && !memcmp(actual.data, expected->data, actual.size));
    free(actual.data);
}

static void check_rejection(CupidGb *gb, const Blob *state, size_t offset, uint8_t replacement) {
    Blob before = capture(gb);
    uint8_t *bad = malloc(state->size);
    assert(bad);
    memcpy(bad, state->data, state->size);
    bad[offset] = replacement;
    cupid_sha256_parts(bad, 96, bad + STATE_HEADER_BYTES, state->size - STATE_HEADER_BYTES,
                       bad + 96);
    assert(cupid_gb_load_state(gb, bad, state->size) == CUPID_GB_STATE_INVALID);
    expect_state(gb, &before);
    free(before.data);
    free(bad);
}

static void verify_states(CupidGbModel model) {
    bool color = cupid_gb_model_is_cgb(model);
    CupidGb *source = serial_test_new(model, color, false, 0);
    CupidGb *target = serial_test_new(model, color, false, 0);
    Blob bare = capture(source);
    Clock source_clock = {.seconds = 1704164645};
    Clock target_clock = {.seconds = 1680000000};
    CupidGbWorkBoy *keyboard = cupid_gb_workboy_create(read_clock, write_clock, &source_clock);
    CupidGbWorkBoy *replacement = cupid_gb_workboy_create(read_clock, write_clock, &target_clock);
    CupidGbPrinter *printer = cupid_gb_printer_create(NULL, NULL, NULL);
    assert(keyboard && replacement && printer);
    cupid_gb_set_serial_link(source, cupid_gb_workboy_link(keyboard));
    cupid_gb_set_serial_link(target, cupid_gb_workboy_link(replacement));
    assert(transfer(source, 'O') == 0 && cupid_gb_workboy_enabled(keyboard));
    cupid_gb_workboy_set_key(keyboard, CUPID_GB_WORKBOY_KEY_A);
    assert(transfer(source, 0) == 0);
    cupid_gb_write_u8(source, SB, 0);
    cupid_gb_write_u8(source, SC, 0x81);
    cupid_gb_tick(source, 1000);
    assert(source->serial_bits_remaining > 0 && source->serial_bits_remaining < 8);
    Blob partial = capture(source);
    const size_t descriptor = partial.size - DEVICE_DESCRIPTOR_BYTES - WORKBOY_STATE_WIRE_SIZE;
    assert(partial.size == bare.size + WORKBOY_STATE_WIRE_SIZE);
    assert(!memcmp(partial.data + descriptor,
                   (uint8_t[]){CUPID_GB_SERIAL_DEVICE_WORKBOY, WORKBOY_STATE_CODEC_VERSION, 0, 0,
                               WORKBOY_STATE_WIRE_SIZE, 0, 0, 0},
                   DEVICE_DESCRIPTOR_BYTES));
    assert(finish_byte(source) == CUPID_GB_WORKBOY_KEY_A);

    assert(cupid_gb_load_state(target, partial.data, partial.size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_workboy_attached(target) == replacement);
    expect_state(target, &partial);
    assert(finish_byte(target) == CUPID_GB_WORKBOY_KEY_A);
    CupidGbStateContext context;
    assert(cupid_gb_state_context_init(&context, target) == CUPID_GB_STATE_OK);
    assert(cupid_gb_load_state_live_with_context(target, partial.data, partial.size, &context) ==
           CUPID_GB_STATE_OK);
    assert(finish_byte(target) == CUPID_GB_WORKBOY_KEY_A && !target_clock.reads);

    check_rejection(target, &partial, 8, 29);
    check_rejection(target, &partial, descriptor + 1, 2);
    check_rejection(target, &partial, descriptor + DEVICE_DESCRIPTOR_BYTES + 1, 2);
    assert(cupid_gb_load_state(target, bare.data, bare.size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_workboy_attached(target) == replacement &&
           !cupid_gb_workboy_enabled(replacement));

    cupid_gb_set_serial_link(target, cupid_gb_printer_link(printer));
    Blob printer_state = capture(target);
    check_rejection(target, &partial, descriptor + DEVICE_DESCRIPTOR_BYTES + 3, 8);
    assert(cupid_gb_load_state(target, partial.data, partial.size) == CUPID_GB_STATE_OK);
    assert(target->serial_link.user_data == printer);
    SerialWitness cable = {0};
    serial_test_observe(target, &cable);
    assert(cupid_gb_load_state(target, partial.data, partial.size) == CUPID_GB_STATE_OK);
    assert(target->serial_link.user_data == &cable && !cable.samples && !cable.launches &&
           !cable.completions);
    cupid_gb_set_serial_output_callback(target, NULL, NULL);
    cupid_gb_set_serial_link(target, cupid_gb_workboy_link(replacement));
    assert(cupid_gb_load_state(target, bare.data, bare.size) == CUPID_GB_STATE_OK);
    assert(transfer(target, 'O') == 0 && cupid_gb_workboy_enabled(replacement));
    assert(cupid_gb_load_state(target, printer_state.data, printer_state.size) ==
           CUPID_GB_STATE_OK);
    assert(cupid_gb_workboy_attached(target) == replacement &&
           !cupid_gb_workboy_enabled(replacement));

    transfer(source, 'R');
    for (unsigned index = 0; index < 8; ++index)
        transfer(source, 0);
    assert(source_clock.reads == 1);
    Blob clock_read = capture(source);
    assert(cupid_gb_load_state(target, clock_read.data, clock_read.size) == CUPID_GB_STATE_OK);
    for (unsigned index = 0; index < 48; ++index)
        assert(transfer(source, 0) == transfer(target, 0));
    assert(source_clock.reads == 1 && !target_clock.reads && !target_clock.writes);
    transfer(target, 'R');
    assert(target_clock.reads == 1 && source_clock.reads == 1);

    const size_t portable_size = cupid_gb_bess_size(source);
    uint8_t *portable = malloc(portable_size);
    assert(portable);
    assert(cupid_gb_bess_save(source, portable, portable_size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_bess_load(target, portable, portable_size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_workboy_attached(target) == replacement &&
           !cupid_gb_workboy_enabled(replacement));

    for (unsigned kind = CUPID_GB_RESET_HARD; kind <= CUPID_GB_RESET_QUICK; ++kind) {
        transfer(target, 'O');
        assert(cupid_gb_workboy_enabled(replacement));
        assert(cupid_gb_reset(target, (CupidGbResetKind)kind));
        assert(cupid_gb_workboy_attached(target) == replacement &&
               !cupid_gb_workboy_enabled(replacement));
    }
    assert(transfer(target, 'O') == 0);
    cupid_gb_workboy_set_key(replacement, CUPID_GB_WORKBOY_KEY_Z);
    CupidGb *staged = calloc(1, sizeof(*staged));
    assert(staged &&
           cupid_gb_prepare_cartridge_swap(staged, target, source->rom, source->rom_size));
    cupid_gb_commit_cartridge_swap(target, staged);
    assert(cupid_gb_workboy_attached(target) == replacement &&
           cupid_gb_workboy_enabled(replacement));
    assert(transfer(target, 0) == 0 && transfer(target, 0) == CUPID_GB_WORKBOY_KEY_Z);
    assert(!source_clock.writes && !target_clock.writes);

    cupid_gb_set_serial_link(source, (CupidGbSerialLink){0});
    cupid_gb_set_serial_link(target, (CupidGbSerialLink){0});
    cupid_gb_workboy_destroy(keyboard);
    cupid_gb_workboy_destroy(replacement);
    cupid_gb_printer_destroy(printer);
    serial_test_free(source);
    serial_test_free(target);
    free(staged);
    free(portable);
    free(clock_read.data);
    free(printer_state.data);
    free(partial.data);
    free(bare.data);
}

int main(void) {
    verify_states(CUPID_GB_MODEL_DMG_ABC);
    verify_states(CUPID_GB_MODEL_CGB);
    puts("WorkBoy snapshots, portable import, reset and hot-swap checks passed.");
    return 0;
}

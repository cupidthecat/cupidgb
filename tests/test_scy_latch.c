/**
 * @file    test_scy_latch.c
 * @brief   Vertical-scroll fetch latching and native-state continuation checks.
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

#include "../src/gb/display_clock.h"
#include "../src/gb/vram_bus.h"

#include "cupid/common/sha256.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum {
    HEADER_BYTES = 128,
    ACCESSORY_DESCRIPTOR_BYTES = 8,
    INPUT_SAMPLE_BYTES = 1,
    SERIAL_CLOCK_BYTES = 1,
    WAVE_HISTORY_BYTES = 1,
    SRAM_LENGTH_BYTES = 4
};

typedef struct Fixture {
    CupidGb *gb;
    uint8_t rom[32768];
} Fixture;

static Fixture *new_fixture(bool color, bool compatibility, bool fast) {
    Fixture *fixture = calloc(1, sizeof(*fixture));
    assert(fixture);
    fixture->gb = calloc(1, sizeof(*fixture->gb));
    assert(fixture->gb);
    fixture->rom[0x143] = compatibility ? 0 : color ? 0x80 : 0;
    fixture->rom[0x147] = 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        fixture->rom[0x14d] = (uint8_t)(fixture->rom[0x14d] - fixture->rom[i] - 1u);
    cupid_gb_set_power_on_seed(fixture->gb, 0);
    cupid_gb_set_model(fixture->gb, color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
    assert(cupid_gb_load_rom(fixture->gb, fixture->rom, sizeof(fixture->rom)));
    cupid_gb_write_u8(fixture->gb, 0xff40, 0);
    fixture->gb->double_speed = fast;
    return fixture;
}

static void free_fixture(Fixture *fixture) {
    cupid_gb_cleanup(fixture->gb);
    free(fixture->gb);
    free(fixture);
}

static void advance_phase(CupidGb *gb, unsigned phase) {
    for (unsigned attempt = 0; attempt < 64 && gb->pixels.phase != phase; ++attempt)
        assert(!cupid_gb_pixels_tick(gb));
    assert(gb->pixels.phase == phase);
}

static void prepare_fetch(Fixture *fixture, bool window, bool flip) {
    CupidGb *gb = fixture->gb;
    gb->io_registers[CUPID_GB_IO_LCDC] = window ? 0xb1 : 0x91;
    gb->io_registers[CUPID_GB_IO_SCX] = 0;
    gb->io_registers[CUPID_GB_IO_SCY] = 7;
    gb->io_registers[CUPID_GB_IO_WX] = 200;
    gb->window_line_counter = 7;
    memset(gb->video_ram, 0, sizeof(gb->video_ram));
    gb->video_ram[0x1800] = 1;
    gb->video_ram[0x3800] = flip ? 0x40 : 0;
    for (unsigned row = 0; row < 8; ++row) {
        gb->video_ram[16 + row * 2] = (uint8_t)(0x80 | row);
        gb->video_ram[17 + row * 2] = (uint8_t)(0x40 | row);
    }
    gb->pixels.line = 0;
    gb->pixels.window_active = window;
    gb->pixels.window_armed = window;
    cupid_gb_pixels_begin_transfer(gb);
    if (window)
        gb->pixels.column = 0;
    advance_phase(gb, 1);
    assert(gb->vram_bus.fetch_map_address == 0x1800);
}

static void test_split_fetch(bool color, bool compatibility, bool fast, bool window, bool flip,
                             bool changed) {
    Fixture *fixture = new_fixture(color, compatibility, fast);
    CupidGb *gb = fixture->gb;
    prepare_fetch(fixture, window, flip);
    if (changed)
        gb->io_registers[CUPID_GB_IO_SCY] = 8;
    advance_phase(gb, 3);
    const unsigned low_row = window || color || !changed ? 7u : 0u;
    const unsigned low_physical_row = low_row ^ (flip && color ? 7u : 0u);
    const uint16_t low_address = (uint16_t)(16u + 2u * low_physical_row);
    assert(gb->vram_bus.fetch_data_address == low_address);
    advance_phase(gb, 4);
    assert(gb->pixels.low == (uint8_t)(0x80u | low_physical_row));

    if (changed)
        gb->io_registers[CUPID_GB_IO_SCY] = 9;
    advance_phase(gb, 5);
    const unsigned high_row = window || color || !changed ? 7u : 1u;
    const unsigned high_physical_row = high_row ^ (flip && color ? 7u : 0u);
    const uint16_t high_address = (uint16_t)(17u + 2u * high_physical_row);
    assert(gb->vram_bus.fetch_data_address == high_address);
    advance_phase(gb, 6);
    assert(gb->pixels.high == (uint8_t)(0x40u | high_physical_row));

    if (changed && !window && color) {
        advance_phase(gb, 1);
        assert((gb->vram_bus.fetch_map_address & ~31u) == 0x1820);
    }
    free_fixture(fixture);
}

static void resign_state(uint8_t *bytes, size_t length) {
    for (unsigned index = 0; index < 4; ++index)
        bytes[12 + index] = (uint8_t)(length >> (index * 8u));
    cupid_sha256_parts(bytes, 96, bytes + HEADER_BYTES, length - HEADER_BYTES, bytes + 96);
}

static void test_snapshot(bool compatibility, bool fast, bool changed) {
    Fixture *source = new_fixture(true, compatibility, fast);
    Fixture *restored = new_fixture(true, compatibility, fast);
    prepare_fetch(source, false, true);
    if (changed)
        source->gb->io_registers[CUPID_GB_IO_SCY] = 8;

    const size_t size = cupid_gb_state_size(source->gb);
    assert(size > 128);
    uint8_t *bytes = calloc(size, 1);
    assert(bytes);
    size_t written = 0;
    assert(cupid_gb_save_state(source->gb, bytes, size, &written) == CUPID_GB_STATE_OK);
    assert(written == size && bytes[8] == CUPID_GB_STATE_VERSION);
    assert(cupid_gb_load_state(restored->gb, bytes, size) == CUPID_GB_STATE_OK);

#if CUPID_GB_STATE_VERSION >= 17
    assert(source->gb->pixel_timing.fetch_y == 7);
    assert(restored->gb->pixel_timing.fetch_y == source->gb->pixel_timing.fetch_y);
#endif
    for (unsigned step = 0; step < 5; ++step) {
        assert(cupid_gb_pixels_tick(source->gb) == cupid_gb_pixels_tick(restored->gb));
        assert(source->gb->pixels.phase == restored->gb->pixels.phase);
        assert(source->gb->pixels.low == restored->gb->pixels.low);
        assert(source->gb->pixels.high == restored->gb->pixels.high);
        assert(source->gb->vram_bus.fetch_data_address ==
               restored->gb->vram_bus.fetch_data_address);
    }

#if CUPID_GB_STATE_VERSION >= 17
    /* The preceding formats require a zero reserved byte. Their missing row
     * latch is reconstructed from the saved registers, before commit. */
    assert(source->gb->cartridge_ram_size <= CUPID_GB_INLINE_RAM_SIZE);
    const size_t descriptor_offset = size - ACCESSORY_DESCRIPTOR_BYTES;
    const size_t latch_offset = descriptor_offset - SRAM_LENGTH_BYTES - WAVE_HISTORY_BYTES -
                                SERIAL_CLOCK_BYTES - INPUT_SAMPLE_BYTES -
                                CUPID_GB_VRAM_BUS_STATE_SIZE - 1u;
    const uint8_t new_latch = bytes[latch_offset];
    assert(new_latch == 7);
    /* V16 retains sampled input but predates serial selection, fetch history and SRAM length. */
    const size_t removed = SERIAL_CLOCK_BYTES + WAVE_HISTORY_BYTES + SRAM_LENGTH_BYTES;
    memmove(bytes + descriptor_offset - removed, bytes + descriptor_offset,
            ACCESSORY_DESCRIPTOR_BYTES);
    const size_t v16_size = size - removed;
    bytes[8] = 16;
    bytes[latch_offset] = 0;
    resign_state(bytes, v16_size);
    assert(cupid_gb_load_state(restored->gb, bytes, v16_size) == CUPID_GB_STATE_OK);
    assert(restored->gb->pixel_timing.fetch_y == (changed ? 8 : 7));
    bytes[latch_offset] = 1;
    resign_state(bytes, v16_size);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, restored->gb, sizeof(*before));
    assert(cupid_gb_load_state(restored->gb, bytes, v16_size) == CUPID_GB_STATE_INVALID);
    assert(memcmp(before, restored->gb, sizeof(*before)) == 0);

    /* V15 used the same pixel record but had no trailing input-sample byte. */
    bytes[8] = 15;
    bytes[latch_offset] = 0;
    const size_t v16_descriptor = v16_size - ACCESSORY_DESCRIPTOR_BYTES;
    memmove(bytes + v16_descriptor - INPUT_SAMPLE_BYTES, bytes + v16_descriptor,
            ACCESSORY_DESCRIPTOR_BYTES);
    const size_t v15_size = v16_size - INPUT_SAMPLE_BYTES;
    resign_state(bytes, v15_size);
    assert(cupid_gb_load_state(restored->gb, bytes, v15_size) == CUPID_GB_STATE_OK);
    assert(restored->gb->pixel_timing.fetch_y == (changed ? 8 : 7));
    bytes[latch_offset] = 255;
    resign_state(bytes, v15_size);
    memcpy(before, restored->gb, sizeof(*before));
    assert(cupid_gb_load_state(restored->gb, bytes, v15_size) == CUPID_GB_STATE_INVALID);
    assert(memcmp(before, restored->gb, sizeof(*before)) == 0);
    free(before);
#endif
    free(bytes);
    free_fixture(source);
    free_fixture(restored);
}

static void advance_guest_dot(CupidGb *gb) {
    const uint64_t before = gb->elapsed_cycles;
    for (unsigned tries = 0; gb->elapsed_cycles == before && tries < 8; ++tries)
        (void)cupid_gb_tick_color_dma_slot(gb, 1, gb->divider_subcycle);
    assert(gb->elapsed_cycles != before);
    /* A guest STAT read forces the pending display batch at this edge. */
    (void)cupid_gb_read_bus_u8(gb, 0xff41);
}

static void test_scheduled_snapshot(bool compatibility, bool fast) {
    Fixture *source = new_fixture(true, compatibility, fast);
    Fixture *restored = new_fixture(true, compatibility, fast);
    CupidGb *a = source->gb, *b = restored->gb;
    a->io_registers[CUPID_GB_IO_SCY] = 7;
    a->io_registers[CUPID_GB_IO_WX] = 200;
    a->video_ram[0x1800] = 1;
    for (unsigned row = 0; row < 8; ++row) {
        a->video_ram[16 + row * 2] = (uint8_t)(0x80 | row);
        a->video_ram[17 + row * 2] = (uint8_t)(0x40 | row);
    }
    cupid_gb_write_u8(a, 0xff40, 0x91);
    unsigned dots = 0;
    while (!(a->pixels.initialized && a->pixels.active && a->pixels.line == 0 &&
             a->pixels.phase == 1) &&
           dots++ < 3000)
        advance_guest_dot(a);
    assert(dots < 3000);
    assert(a->pixel_timing.fetch_y == 7);
    cupid_gb_write_u8(a, 0xff42, 8);
    assert(a->pixels.phase == 1 && a->pixel_timing.fetch_y == 7);

    const size_t size = cupid_gb_state_size(a);
    assert(size);
    uint8_t *snapshot = malloc(size);
    assert(snapshot);
    size_t written = 0;
    assert(cupid_gb_save_state(a, snapshot, size, &written) == CUPID_GB_STATE_OK);
    assert(written == size);
    assert(cupid_gb_load_state(b, snapshot, size) == CUPID_GB_STATE_OK);
    free(snapshot);

    bool reached_low = false;
    for (unsigned dot = 0; dot < 40; ++dot) {
        uint8_t clock_a[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE];
        uint8_t clock_b[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE];
        assert(cupid_gb_display_clock_state_encode(a, clock_a, sizeof(clock_a)));
        assert(cupid_gb_display_clock_state_encode(b, clock_b, sizeof(clock_b)));
        assert(memcmp(clock_a, clock_b, sizeof(clock_a)) == 0);
        assert(a->pixel_timing.fetch_y == b->pixel_timing.fetch_y);
        assert(a->pixels.phase == b->pixels.phase && a->pixels.low == b->pixels.low &&
               a->pixels.high == b->pixels.high);
        assert(a->vram_bus.fetch_data_address == b->vram_bus.fetch_data_address);
        if (a->pixels.phase == 3 && !reached_low) {
            assert(a->vram_bus.fetch_data_address == 30);
            reached_low = true;
        }
        advance_guest_dot(a);
        advance_guest_dot(b);
        assert(a->elapsed_cycles == b->elapsed_cycles);
    }
    assert(reached_low);
    free_fixture(source);
    free_fixture(restored);
}

int main(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned compatibility = 0; compatibility < 2; ++compatibility)
            for (unsigned flip = 0; flip < 2; ++flip)
                for (unsigned changed = 0; changed < 2; ++changed) {
                    test_split_fetch(true, compatibility != 0, fast != 0, false, flip != 0,
                                     changed != 0);
                    test_split_fetch(true, compatibility != 0, fast != 0, true, flip != 0,
                                     changed != 0);
                }
        for (unsigned changed = 0; changed < 2; ++changed)
            test_split_fetch(false, false, false, false, false, changed != 0);
        for (unsigned compatibility = 0; compatibility < 2; ++compatibility)
            for (unsigned changed = 0; changed < 2; ++changed)
                test_snapshot(compatibility != 0, fast != 0, changed != 0);
        for (unsigned compatibility = 0; compatibility < 2; ++compatibility)
            test_scheduled_snapshot(compatibility != 0, fast != 0);
    }
    return 0;
}

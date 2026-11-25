/**
 * @file    test_pixel_timing.c
 * @brief   Window, object, and video-port timing regressions with generated tile data.
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
#include "cupid/gb/ppu.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fixture {
    CupidGb *gb;
    unsigned cycles;
    unsigned phase;
    unsigned last_mode;
    unsigned hblank[3];
    unsigned hblank_count;
} Fixture;

static void write_byte(Fixture *fixture, uint16_t address, uint8_t value) {
    cupid_gb_write_u8(fixture->gb, address, value);
}

static void tile(Fixture *fixture, unsigned index, unsigned color) {
    for (unsigned row = 0; row < 8; ++row) {
        write_byte(fixture, (uint16_t)(0x8000u + index * 16u + row * 2u), color & 1u ? 0xffu : 0);
        write_byte(fixture, (uint16_t)(0x8001u + index * 16u + row * 2u), color & 2u ? 0xffu : 0);
    }
}

/* Profile0 is monochrome,1 native color,2 color hardware in compatibility mode. */
static Fixture create_fixture(unsigned profile, bool fast, unsigned scroll, unsigned wx,
                              bool object) {
    Fixture fixture = {0};
    fixture.gb = calloc(1, sizeof(*fixture.gb));
    assert(fixture.gb);
    uint8_t rom[32768] = {0};
    rom[0x143] = profile == 1 ? 0x80 : 0;
    rom[0x147] = profile ? 0 : 0xff;
    rom[0x149] = 3;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
    cupid_gb_set_power_on_seed(fixture.gb, 0);
    cupid_gb_set_model(fixture.gb, profile ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
    assert(cupid_gb_load_rom(fixture.gb, rom, sizeof(rom)));
    write_byte(&fixture, 0xff40, 0);
    write_byte(&fixture, 0xff04, 0);
    write_byte(&fixture, 0xff41, 0);
    write_byte(&fixture, 0xff42, 0);
    write_byte(&fixture, 0xff43, (uint8_t)scroll);
    write_byte(&fixture, 0xff45, 0);
    write_byte(&fixture, 0xff4a, 0);
    write_byte(&fixture, 0xff4b, (uint8_t)wx);
    memset(fixture.gb->video_ram, 0, sizeof(fixture.gb->video_ram));
    memset(fixture.gb->frame_buffer, 0, sizeof(fixture.gb->frame_buffer));
    for (unsigned i = 0; i < 160; ++i)
        write_byte(&fixture, (uint16_t)(0xfe00u + i), 0);
    tile(&fixture, 0, 1);
    tile(&fixture, 1, 2);
    tile(&fixture, 2, 3);
    write_byte(&fixture, 0x8030, 0x5a);
    for (unsigned i = 0; i < 0x400; ++i)
        write_byte(&fixture, (uint16_t)(0x9c00u + i), 1);
    write_byte(&fixture, 0xff47, 0xe4);
    write_byte(&fixture, 0xff48, 0xe4);
    write_byte(&fixture, 0xff49, 0xe4);
    if (object) {
        write_byte(&fixture, 0xfe00, 16);
        write_byte(&fixture, 0xfe01, 8);
        write_byte(&fixture, 0xfe02, 2);
    }
    fixture.gb->double_speed = fast;
    return fixture;
}

static void destroy_fixture(Fixture *fixture) {
    cupid_gb_cleanup(fixture->gb);
    free(fixture->gb);
}

static void advance_to(Fixture *fixture, unsigned cycles) {
    assert(cycles >= fixture->cycles);
    while (fixture->cycles < cycles) {
        fixture->phase = cupid_gb_tick_color_dma_slot(fixture->gb, 1, fixture->phase);
        ++fixture->cycles;
        const unsigned mode = cupid_gb_read_bus_u8(fixture->gb, 0xff41) & 3u;
        if (fixture->last_mode == 3 && mode == 0 && fixture->hblank_count < 3)
            fixture->hblank[fixture->hblank_count++] = fixture->cycles;
        fixture->last_mode = mode;
    }
}

static void check_edges(const Fixture *fixture, unsigned first, unsigned second, unsigned third) {
    assert(fixture->hblank_count == 3);
    const unsigned expected[] = {first, second, third};
    for (unsigned row = 0; row < 3; ++row) {
        const unsigned edge = fixture->gb->double_speed ? expected[row] * 2u + 1u : expected[row];
        assert(fixture->hblank[row] == edge);
    }
}

static void test_window_edges_and_pixels(void) {
    const unsigned positions[] = {0, 7, 80, 166, 167};
    for (unsigned profile = 0; profile < 3; ++profile)
        for (unsigned fast = 0; fast <= (profile != 0); ++fast)
            for (unsigned scroll = 0; scroll < 8; scroll += 7)
                for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
                    const unsigned wx = positions[i];
                    Fixture fixture = create_fixture(profile, fast != 0, scroll, wx, false);
                    write_byte(&fixture, 0xff40, 0xf3);
                    advance_to(&fixture, fast ? 2600 : 1300);
                    unsigned first = (profile ? 251u : 252u) + scroll;
                    if (wx < 166 || (profile && wx == 166))
                        first += 6;
                    if (wx == 0 && scroll)
                        ++first;
                    check_edges(&fixture, first, first + 454, first + 910);
                    for (unsigned row = 0; row < 3; ++row)
                        for (unsigned x = 0; x < 160; ++x) {
                            unsigned color = 1;
                            if (wx < 166)
                                color = x + 7 >= wx ? 2 : 1;
                            else if (wx == 166)
                                color = profile ? (x == 159 ? 2 : 1) : (row ? 2 : 1);
                            /* A left-edge color fetch can repeat the preceding
                             * byte for two pixels before the window row arrives. */
                            if (profile && wx == 0 && scroll && x < 2)
                                color = row ? 3 : 0;
                            assert(fixture.gb->frame_buffer[row * 160 + x] == color);
                        }
                    destroy_fixture(&fixture);
                }
}

static void test_window_y_compare_after_register_writes(void) {
    for (unsigned profile = 0; profile < 3; ++profile)
        for (unsigned fast = 0; fast <= (profile != 0); ++fast)
            for (unsigned matches = 0; matches < 2; ++matches) {
                const unsigned row = matches ? 8 : 12;
                const unsigned speed = fast ? 2 : 1;
                Fixture fixture = create_fixture(profile, fast != 0, 0, 7, false);
                write_byte(&fixture, 0xff4a, matches ? 40 : 8);
                write_byte(&fixture, 0xff40, matches ? 0xf3 : 0xd3);
                advance_to(&fixture, speed * (row * 456 + 40));
                assert(fixture.gb->pixels.line == row);
                if (matches)
                    write_byte(&fixture, 0xff4a, (uint8_t)row);
                else
                    write_byte(&fixture, 0xff40, 0xf3);
                advance_to(&fixture, speed * (row * 456 + 300));

                assert(fixture.gb->pixels.window_armed == (matches != 0));
                assert(fixture.gb->frame_buffer[row * 160] == (matches ? 2 : 1));
                destroy_fixture(&fixture);
            }
}

static void test_dmg_window_y_late_horizontal_trigger(void) {
    const unsigned row = 8;
    const unsigned row_start = row * 456 - 3;
    Fixture fixture = create_fixture(0, false, 0, 7, false);
    write_byte(&fixture, 0xff4a, row);
    write_byte(&fixture, 0xff40, 0xd3);
    advance_to(&fixture, row_start + 92);
    assert(fixture.gb->pixels.line == row && !fixture.gb->pixels.window_armed);
    write_byte(&fixture, 0xff40, 0xf3);
    advance_to(&fixture, row_start + 94);
    assert(fixture.gb->pixels.x == 1 && fixture.gb->pixel_timing.logical_x == 1);
    assert(fixture.gb->pixel_timing.flags & CUPID_GB_PIXEL_WINDOW_Y_EDGE);
    assert(fixture.gb->frame_buffer[row * 160] == 1);

    advance_to(&fixture, row_start + 95);
    assert(fixture.gb->pixels.window_active && fixture.gb->pixels.x == 0);
    advance_to(&fixture, row_start + 300);
    assert(fixture.gb->pixels.window_used);
    assert(fixture.gb->frame_buffer[row * 160] == 2);
    destroy_fixture(&fixture);
}

static void test_dmg_oam_search_at_dma_tail(void) {
    const unsigned row = 3;
    const unsigned row_start = row * 456 - 3;
    Fixture fixture = create_fixture(0, false, 0, 7, false);
    for (unsigned i = 0; i < 160; ++i)
        write_byte(&fixture, (uint16_t)(0xc000 + i), 0);
    write_byte(&fixture, 0xfe9c, row + 16);
    write_byte(&fixture, 0xfe9d, 88);
    write_byte(&fixture, 0xc09c, row + 16);
    write_byte(&fixture, 0xc09d, 88);
    write_byte(&fixture, 0xff40, 0x93);
    advance_to(&fixture, row_start - 642);
    assert(fixture.gb->oam_search_word == 0x5813);
    write_byte(&fixture, 0xff46, 0xc0);

    advance_to(&fixture, row_start + 3);
    assert(fixture.gb->dma_active && !fixture.gb->pixels.object_count);
    advance_to(&fixture, row_start + 4);
    assert(!fixture.gb->dma_active && fixture.gb->pixels.object_count == 1);
    assert(fixture.gb->pixels.objects[0] == 0 && fixture.gb->pixels.object_y[0] == row + 16);
    advance_to(&fixture, row_start + 82);
    assert(fixture.gb->pixels.object_count == 2 && fixture.gb->pixels.objects[1] == 39);
    destroy_fixture(&fixture);
}

static void test_hidden_objects_keep_color_fetch_timing(void) {
    for (unsigned profile = 0; profile < 3; ++profile)
        for (unsigned fast = 0; fast <= (profile != 0); ++fast)
            for (unsigned scroll = 0; scroll < 8; scroll += 7) {
                Fixture fixture = create_fixture(profile, fast != 0, scroll, 200, true);
                write_byte(&fixture, 0xff40, 0x91);
                advance_to(&fixture, fast ? 2600 : 1300);
                const unsigned first = (profile ? 251u : 252u) + scroll;
                const unsigned object_wait = profile ? (scroll ? 6u : 11u) : 0u;
                check_edges(&fixture, first, first + 454 + object_wait, first + 910 + object_wait);
                for (unsigned pixel = 0; pixel < 480; ++pixel)
                    assert(fixture.gb->frame_buffer[pixel] == 1);
                destroy_fixture(&fixture);
            }
}

static void test_monochrome_object_abort_edges(void) {
    const unsigned expected[][4] = {{711, 713, 715, 717}, {713, 713, 715, 717}};
    for (unsigned scrolled = 0; scrolled < 2; ++scrolled)
        for (unsigned edge = 0; edge < 4; ++edge) {
            Fixture fixture = create_fixture(0, false, scrolled ? 7 : 0, 200, true);
            write_byte(&fixture, 0xff40, 0x93);
            advance_to(&fixture, 551u + edge * 2u);
            write_byte(&fixture, 0xff40, 0x91);
            advance_to(&fixture, 1300);
            const unsigned first = scrolled ? 259u : 252u;
            check_edges(&fixture, first, expected[scrolled][edge], first + 910);
            destroy_fixture(&fixture);
        }
}

static void test_window_restart_after_scroll_write(void) {
    Fixture fixture = create_fixture(0, false, 0, 200, false);
    write_byte(&fixture, 0xff40, 0xf3);
    advance_to(&fixture, 85);
    write_byte(&fixture, 0xff43, 7);
    write_byte(&fixture, 0xff4b, 0);
    advance_to(&fixture, 86);
    assert(fixture.gb->pixel_timing.logical_x == -7);
    assert(fixture.gb->pixel_timing.flags & CUPID_GB_PIXEL_WINDOW_HOLD);
    assert(cupid_gb_pixels_timing_valid(fixture.gb));
    advance_to(&fixture, 1300);
    for (unsigned pixel = 0; pixel < 480; ++pixel)
        assert(fixture.gb->frame_buffer[pixel] == 2);
    destroy_fixture(&fixture);
}

static void test_video_read_claims(void) {
    for (unsigned profile = 0; profile < 3; ++profile) {
        Fixture fixture = create_fixture(profile, false, 0, 200, false);
        write_byte(&fixture, 0xff40, 0x91);
        if (profile) {
            for (unsigned cycle = 79; cycle <= 83; ++cycle) {
                advance_to(&fixture, cycle);
                assert((cupid_gb_read_bus_u8(fixture.gb, 0xff41) & 3u) == 3);
                assert(cupid_gb_read_bus_u8(fixture.gb, 0x8030) == 0x5a);
            }
            advance_to(&fixture, 84);
            assert(cupid_gb_read_bus_u8(fixture.gb, 0x8030) == 0xff);
            advance_to(&fixture, 532);
            assert(cupid_gb_read_bus_u8(fixture.gb, 0x8030) == 0x5a);
        } else {
            advance_to(&fixture, 529);
            assert(cupid_gb_read_bus_u8(fixture.gb, 0x8030) == 0x5a);
            for (unsigned cycle = 530; cycle <= 532; ++cycle) {
                advance_to(&fixture, cycle);
                assert((cupid_gb_read_bus_u8(fixture.gb, 0xff41) & 3u) == 2);
                assert(cupid_gb_read_bus_u8(fixture.gb, 0x8030) == 0xff);
            }
        }
        advance_to(&fixture, 533);
        assert(cupid_gb_read_bus_u8(fixture.gb, 0x8030) == 0xff);
        destroy_fixture(&fixture);
    }
}

static void test_double_speed_mode_half_dots(void) {
    const unsigned cycles[] = {156, 157, 502, 503, 1064, 1065, 1410, 1411};
    const unsigned modes[] = {0, 3, 3, 0, 2, 3, 3, 0};
    for (unsigned profile = 1; profile <= 2; ++profile) {
        Fixture fixture = create_fixture(profile, true, 0, 200, false);
        write_byte(&fixture, 0xff40, 0x91);
        for (unsigned i = 0; i < sizeof(cycles) / sizeof(cycles[0]); ++i) {
            advance_to(&fixture, cycles[i]);
            assert((cupid_gb_read_bus_u8(fixture.gb, 0xff41) & 3u) == modes[i]);
            assert(cupid_gb_read_bus_u8(fixture.gb, 0x8030) == (modes[i] == 3 ? 0xff : 0x5a));
        }
        destroy_fixture(&fixture);
    }
}

int main(void) {
    test_window_edges_and_pixels();
    test_window_y_compare_after_register_writes();
    test_dmg_window_y_late_horizontal_trigger();
    test_dmg_oam_search_at_dma_tail();
    test_hidden_objects_keep_color_fetch_timing();
    test_monochrome_object_abort_edges();
    test_window_restart_after_scroll_write();
    test_video_read_claims();
    test_double_speed_mode_half_dots();
    return 0;
}

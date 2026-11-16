/**
 * @file    test_presentation_observer.c
 * @brief   Cartridge rumble samples at actual presentation and CPU delivery boundaries.
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
 * along with this program. If not, see <https://gnu.org>.
 */

#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Presentation {
    uint8_t type;
    uint64_t samples, strength_sum;
    uint64_t cycle;
} Presentation;

typedef struct Observer {
    uint64_t samples, strength_sum, total_samples;
    unsigned presentation_count;
    Presentation presentations[16];
} Observer;

static void sample_motor(const CupidGb *gb, uint8_t strength, void *context) {
    Observer *observer = context;
    assert(strength <= 3 && strength == cupid_gb_rumble_strength(gb));
    ++observer->samples;
    observer->strength_sum += strength;
    ++observer->total_samples;
}

static void publish_motor(const CupidGb *gb, uint8_t type, void *context) {
    Observer *observer = context;
    assert(gb->frame_ready && gb->display_frame_type == type);
    assert(observer->presentation_count <
           sizeof(observer->presentations) / sizeof(observer->presentations[0]));
    if (!gb->sgb.enabled && type != CUPID_GB_FRAME_NORMAL && type != CUPID_GB_FRAME_REPEAT) {
        assert(gb->frame_buffer[0] == 0 && gb->frame_buffer_color[0] == 0x7fff);
    }
    observer->presentations[observer->presentation_count++] =
        (Presentation){type, observer->samples, observer->strength_sum, gb->elapsed_cycles};
    observer->samples = observer->strength_sum = 0;
}

static CupidGb *machine(bool color, Observer *observer) {
    uint8_t rom[32768] = {0};
    rom[0x100] = 0xc3;
    rom[0x101] = 0;
    rom[0x102] = 1;
    rom[0x143] = color ? 0x80 : 0;
    rom[0x147] = 0xbc;
    rom[0x149] = 0xc1;
    rom[0x14a] = 0x65;
    rom[0x150] = rom[0x152] = 1;
    rom[0x153] = 9;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);

    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_power_on_seed(gb, 0);
    cupid_gb_set_model(gb, color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 3, 0x23);
    if (observer) {
        cupid_gb_set_rumble_sample_callback(gb, sample_motor, observer);
        cupid_gb_set_presentation_callback(gb, publish_motor, observer);
    }
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void vblank_start(CupidGb *gb, bool first, bool repeat) {
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    gb->pixels.initialized = true;
    gb->pixels.line = 144;
    gb->pixels.dot = 0;
    gb->ppu_lcd_startup = false;
    gb->lcd_first_frame = first;
    gb->lcd_repeat_countdown = repeat ? 67483 : 0;
}

static void test_presentation_inside_cpu_delivery(void) {
    for (unsigned speed = 0; speed < 2; ++speed) {
        Observer observer = {0};
        CupidGb *gb = machine(true, &observer);
        vblank_start(gb, false, false);
        gb->double_speed = speed != 0;
        assert(cupid_gb_step(gb));
        assert(observer.presentation_count == 1);
        assert(observer.presentations[0].type == CUPID_GB_FRAME_NORMAL);
        /* The current CPU delivery contributes before the frame notification;
         * later deliveries in the same instruction remain in the new bucket. */
        assert(observer.presentations[0].samples == 1);
        assert(observer.presentations[0].strength_sum == 3);
        assert(observer.samples && observer.total_samples > 1);
        release(gb);
    }
}

static void test_frozen_and_immediate_presentation(void) {
    Observer observer = {0};
    CupidGb *gb = machine(true, &observer);
    gb->speed_switch_freeze_cycles = 4;
    cupid_gb_deliver_cpu_cycles(gb, 1);
    assert(!observer.samples && !observer.presentation_count);
    cupid_gb_deliver_cpu_cycles(gb, 1);
    assert(observer.samples == 1);

    /* A guest LCDC enable can publish before any additional CPU clock. */
    gb->display_cycles_since_frame = 4561;
    gb->frame_buffer[0] = 3;
    gb->frame_buffer_color[0] = 0x1234;
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    assert(observer.presentation_count == 1);
    assert(observer.presentations[0].type == CUPID_GB_FRAME_LCD_OFF);
    assert(observer.presentations[0].samples == 1);
    assert(observer.presentations[0].strength_sum == 3);
    assert(!observer.samples && observer.total_samples == 1);
    release(gb);
}

static void test_frame_types(void) {
    const uint8_t types[] = {CUPID_GB_FRAME_NORMAL, CUPID_GB_FRAME_REPEAT,
                             CUPID_GB_FRAME_BLANK_FIRST, CUPID_GB_FRAME_LCD_OFF,
                             CUPID_GB_FRAME_ARTIFICIAL};
    for (unsigned kind = 0; kind < sizeof(types) / sizeof(types[0]); ++kind) {
        Observer observer = {0};
        CupidGb *gb = machine(types[kind] != CUPID_GB_FRAME_ARTIFICIAL, &observer);
        if (kind < 3) {
            vblank_start(gb, types[kind] != CUPID_GB_FRAME_NORMAL,
                         types[kind] == CUPID_GB_FRAME_REPEAT);
        } else if (types[kind] == CUPID_GB_FRAME_ARTIFICIAL) {
            cupid_gb_write_u8(gb, 0xff40, 0x91);
            gb->cpu.stopped = true;
        }
        /* A notification before blanking would expose these stale pixels. */
        gb->frame_buffer[0] = 3;
        gb->frame_buffer_color[0] = 0x1234;
        if (kind < 3) {
            cupid_gb_deliver_cpu_cycles(gb, 1);
        } else {
            /* LCD-off and monochrome STOP both publish through the same
             * real guest oscillator, with the sample preceding the event. */
            cupid_gb_tick(gb, 17556);
        }
        assert(observer.presentation_count == 1);
        assert(observer.presentations[0].type == types[kind]);
        assert(observer.presentations[0].samples > 0);
        assert(observer.presentations[0].strength_sum == observer.presentations[0].samples * 3u);
        release(gb);
    }
}

static void test_lifetime_and_guest_equality(void) {
    Observer observer = {0};
    CupidGb *observed = machine(true, &observer);
    CupidGb *control = machine(true, NULL);
    assert(cupid_gb_reset(observed, CUPID_GB_RESET_QUICK));
    assert(observed->presentation_callback == publish_motor &&
           observed->presentation_user_data == &observer);
    assert(observed->rumble_sample_callback == sample_motor);
    cupid_gb_init(observed);
    assert(observed->presentation_callback == publish_motor &&
           observed->presentation_user_data == &observer);
    assert(cupid_gb_load_rom(observed, control->rom, control->rom_size));
    /* Restore identical guest state after the observed instance's re-init. */
    const size_t length = cupid_gb_state_size(control);
    uint8_t *left = malloc(length), *right = malloc(length);
    assert(left && right);
    assert(cupid_gb_save_state(control, left, length, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_load_state(observed, left, length) == CUPID_GB_STATE_OK);
    assert(observed->presentation_callback == publish_motor &&
           observed->rumble_sample_callback == sample_motor);
    assert(cupid_gb_save_state(observed, right, length, NULL) == CUPID_GB_STATE_OK);
    assert(!memcmp(left, right, length));

    cupid_gb_tick(observed, 17556);
    cupid_gb_tick(control, 17556);
    assert(observer.presentation_count == 1);
    assert(cupid_gb_save_state(observed, left, length, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_save_state(control, right, length, NULL) == CUPID_GB_STATE_OK);
    assert(!memcmp(left, right, length));

    cupid_gb_set_presentation_callback(observed, NULL, &observer);
    assert(!observed->presentation_callback && !observed->presentation_user_data);
    assert(cupid_gb_save_state(observed, right, length, NULL) == CUPID_GB_STATE_OK);
    assert(!memcmp(left, right, length));
    cupid_gb_set_presentation_callback(observed, publish_motor, &observer);
    cupid_gb_cleanup(observed);
    assert(!observed->presentation_callback && !observed->presentation_user_data);
    free(observed);
    free(left);
    free(right);
    release(control);
}

int main(void) {
    cupid_gb_set_presentation_callback(NULL, publish_motor, NULL);
    test_presentation_inside_cpu_delivery();
    test_frozen_and_immediate_presentation();
    test_frame_types();
    test_lifetime_and_guest_equality();
    puts("Presentation-boundary and rumble callback regressions passed.");
    return 0;
}

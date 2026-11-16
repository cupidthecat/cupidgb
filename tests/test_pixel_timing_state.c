/**
 * @file    test_pixel_timing_state.c
 * @brief   Native-state continuation of window, object-abort and retained VRAM history.
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
#include "../src/gb/printer_internal.h"
#include "../src/gb/vram_bus.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { INPUT_SAMPLE_BYTES = 1 };

enum {
    HEADER_BYTES = 128,
    DESCRIPTOR_BYTES = 8,
    V11_BYTES = 300600,
    RTC_BYTES = 6,
    CURRENT_BYTES = 305088 + INPUT_SAMPLE_BYTES,
    SGB_BYTES = 10119,
    PIXEL_LINE = 300392,
    PIXEL_X = 300393,
    PIXEL_COUNT = 300419,
    PIXEL_PHASE = 300420,
    PIXEL_DELAY = 300427,
    OBJECT_COUNT = 300460,
    OBJECT_FETCHED = 300461,
    OBJECT_DELAY = 300463,
    OBJECT_INDEX = 300464,
    PIXEL_ACTIVE = 300511,
    WINDOW_ACTIVE = 300513,
    OBJECT_STAGE = 300593,
    TIMING_BYTES = CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
                   CUPID_GB_VRAM_BUS_STATE_SIZE
};

typedef enum Scene {
    FRACTIONAL,
    WINDOW_FIRST,
    WINDOW_HOLD,
    HOLD_AFTER_PORT_WRITE,
    HOLD_AFTER_LATE_ENABLE,
    WX_READ_LATCH,
    INSERT_ALIGNMENT,
    INSERT_WORD,
    INSERT_LOW,
    INSERT_AFTER_PORT_WRITE,
    INSERT_THEN_ABORT,
    ABORT_ALIGNMENT,
    ABORT_WORD,
    ABORT_LOW,
    ABORT_THEN_REENABLE,
    VRAM_HBLANK,
    VRAM_LCD_OFF,
    QUIET_COMPLETION,
    QUIET_FORCED,
    SCENE_COUNT
} Scene;

typedef struct Profile {
    CupidGbModel model;
    bool color;
    bool fast;
} Profile;

static const Profile profiles[] = {
    {CUPID_GB_MODEL_DMG_ABC, false, false}, {CUPID_GB_MODEL_DMG0, false, false},
    {CUPID_GB_MODEL_MGB, false, false},     {CUPID_GB_MODEL_SGB, false, false},
    {CUPID_GB_MODEL_SGB2, false, false},    {CUPID_GB_MODEL_CGB, true, false},
    {CUPID_GB_MODEL_CGB, false, false},     {CUPID_GB_MODEL_CGB, true, true},
    {CUPID_GB_MODEL_CGB, false, true},
};

typedef struct Rig {
    CupidGb *gb;
    CupidGbPrinter *printer;
    CupidGbStateContext context;
    unsigned images, completed;
} Rig;

typedef struct Blob {
    uint8_t *bytes;
    size_t length;
} Blob;

static unsigned continued_cases, legacy_cases, rejected_cases, codec_cases;

static void put_le(uint8_t *bytes, uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8));
}

static void resign(Blob *blob) {
    put_le(blob->bytes + 12, (uint32_t)blob->length, 4);
    cupid_sha256_parts(blob->bytes, 96, blob->bytes + HEADER_BYTES, blob->length - HEADER_BYTES,
                       blob->bytes + 96);
}

static int64_t fixture_time(void *data) {
    (void)data;
    return INT64_C(1800000000);
}

static void on_image(const CupidGbPrinterImage *image, void *data) {
    assert(image);
    ++((Rig *)data)->images;
}

static void on_done(void *data) {
    ++((Rig *)data)->completed;
}

static Rig *create_rig(unsigned profile, bool printer) {
    assert(profile < sizeof(profiles) / sizeof(*profiles));
    Rig *rig = calloc(1, sizeof(*rig));
    assert(rig);
    rig->gb = calloc(1, sizeof(*rig->gb));
    assert(rig->gb);
    CupidGb *gb = rig->gb;
    cupid_gb_set_model(gb, profiles[profile].model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x504958454c535441));
    cupid_gb_set_rtc_time_source(gb, fixture_time, NULL);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    uint8_t rom[32768] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    memcpy(rom + 0x134, "PIXEL STATE", 11);
    rom[0x143] = profiles[profile].color ? 0x80 : 0;
    rom[0x146] = 3;
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[index] - 1u);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->double_speed = profiles[profile].fast;
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    memset(gb->object_attribute_memory, 0, sizeof(gb->object_attribute_memory));
    for (size_t index = 0; index < sizeof(gb->video_ram); ++index)
        gb->video_ram[index] = (uint8_t)((index * 37u + index / 16u + 19u) & 0xffu);
    for (size_t index = 0; index < sizeof(gb->cgb_bg_palette_ram); ++index) {
        gb->cgb_bg_palette_ram[index] = (uint8_t)(index * 3u + 7u);
        gb->cgb_obj_palette_ram[index] = (uint8_t)(index * 5u + 11u);
    }
    cupid_gb_write_u8(gb, 0xff47, 0xe4);
    cupid_gb_write_u8(gb, 0xff48, 0xe4);
    cupid_gb_write_u8(gb, 0xff49, 0x1b);
    if (printer) {
        rig->printer = cupid_gb_printer_create(on_image, on_done, rig);
        assert(rig->printer);
        cupid_gb_set_serial_link(gb, cupid_gb_printer_link(rig->printer));
    }
    cupid_gb_tick(gb, 4500);
    if (printer) {
        cupid_gb_write_u8(gb, 0xff01, 0x88);
        cupid_gb_write_u8(gb, 0xff02, 0x81);
        cupid_gb_tick(gb, 384);
        assert(rig->printer->state.incoming_bits > 0 && rig->printer->state.incoming_bits < 8);
    }
    cupid_gb_apu_run(gb, true);
    assert(cupid_gb_state_context_init(&rig->context, gb) == CUPID_GB_STATE_OK);
    return rig;
}

static void destroy_rig(Rig *rig) {
    cupid_gb_set_serial_link(rig->gb, (CupidGbSerialLink){0});
    cupid_gb_printer_destroy(rig->printer);
    cupid_gb_cleanup(rig->gb);
    free(rig->gb);
    free(rig);
}

static void advance_dot(Rig *rig) {
    /* After setup these fixtures neither reset DIV nor hold its clock. Its
     * serialized subcycle is also the phase of their CPU-T delivery schedule. */
    assert(!rig->gb->cpu.stopped && !rig->gb->stop_divider_hold_cycles);
    const uint64_t before = rig->gb->elapsed_cycles;
    do {
        const unsigned phase = cupid_gb_tick_color_dma_slot(rig->gb, 1, rig->gb->divider_subcycle);
        assert(phase == rig->gb->divider_subcycle);
    } while (rig->gb->elapsed_cycles == before);
}

static bool available(unsigned profile, Scene scene) {
    const bool color = profiles[profile].model == CUPID_GB_MODEL_CGB;
    if (scene == WINDOW_HOLD || scene == HOLD_AFTER_PORT_WRITE || scene == HOLD_AFTER_LATE_ENABLE ||
        scene == INSERT_THEN_ABORT || (scene >= ABORT_ALIGNMENT && scene <= ABORT_THEN_REENABLE))
        return !color;
    if (scene == WX_READ_LATCH)
        return color;
    return true;
}

static bool reached(const CupidGb *gb, Scene scene) {
    const CupidGbPixels *p = &gb->pixels;
    const unsigned flags = gb->pixel_timing.flags;
    switch (scene) {
        case FRACTIONAL:
            return p->active && (flags & CUPID_GB_PIXEL_FRACTIONAL) != 0;
        case WINDOW_FIRST:
            return p->active && p->window_active && (flags & CUPID_GB_PIXEL_WINDOW_FIRST);
        case WINDOW_HOLD:
        case HOLD_AFTER_PORT_WRITE:
            return (flags & CUPID_GB_PIXEL_WINDOW_HOLD) != 0;
        case HOLD_AFTER_LATE_ENABLE:
            return p->active && gb->pixel_timing.logical_x == -7;
        case WX_READ_LATCH:
            return p->active && p->window_active && (flags & CUPID_GB_PIXEL_WX_GLITCH);
        case INSERT_ALIGNMENT:
        case INSERT_AFTER_PORT_WRITE:
        case INSERT_THEN_ABORT:
            return (flags & CUPID_GB_PIXEL_BG_INSERT) &&
                   gb->object_fetch_stage == CUPID_GB_OBJECT_WAIT_BACKGROUND;
        case INSERT_WORD:
            return (flags & CUPID_GB_PIXEL_BG_INSERT) &&
                   gb->object_fetch_stage == CUPID_GB_OBJECT_READ_WORD;
        case INSERT_LOW:
            return (flags & CUPID_GB_PIXEL_BG_INSERT) &&
                   gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE && p->object_delay >= 2;
        case ABORT_ALIGNMENT:
        case ABORT_THEN_REENABLE:
            return p->line == 1 && gb->object_fetch_stage == CUPID_GB_OBJECT_WAIT_BACKGROUND;
        case ABORT_WORD:
            return p->line == 1 && gb->object_fetch_stage == CUPID_GB_OBJECT_READ_WORD;
        case ABORT_LOW:
            return p->line == 1 && gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE &&
                   p->object_delay >= 2;
        case VRAM_HBLANK:
        case VRAM_LCD_OFF:
            return p->initialized && !p->active && p->line == 1 && p->x == 160 &&
                   gb->ppu_transfer_end_dot && p->dot == gb->ppu_transfer_end_dot;
        case QUIET_COMPLETION:
        case QUIET_FORCED:
            return p->line == 1 && p->active &&
                   gb->display_clock.phase == CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET &&
                   gb->display_clock.batch_half_dots && gb->display_clock.credit_half_dots >= 40 &&
                   gb->vram_bus.fetch_map_address != gb->vram_bus.map_address &&
                   gb->vram_bus.fetch_data_address != gb->vram_bus.data_address;
        default:
            return false;
    }
}

static void prepare_scene(Rig *rig, Scene scene) {
    CupidGb *gb = rig->gb;
    const bool insertion = scene >= INSERT_ALIGNMENT && scene <= INSERT_THEN_ABORT;
    const bool abort =
        (scene >= ABORT_ALIGNMENT && scene <= ABORT_THEN_REENABLE) || scene == INSERT_THEN_ABORT;
    const bool window = scene >= WINDOW_FIRST && scene <= INSERT_THEN_ABORT;
    const bool vram = scene >= VRAM_HBLANK;
    cupid_gb_write_u8(gb, 0xff43,
                      scene == WINDOW_FIRST || scene == HOLD_AFTER_LATE_ENABLE ? 0
                      : window                                                 ? 7
                                                                               : 3);
    cupid_gb_write_u8(gb, 0xff4a, 0);
    cupid_gb_write_u8(gb, 0xff4b,
                      scene == HOLD_AFTER_LATE_ENABLE ? 200
                      : scene == WINDOW_FIRST         ? 7
                      : window                        ? 0
                                                      : 167);
    if (insertion || abort) {
        static const uint8_t positions[] = {8, 13, 21, 33};
        const unsigned count = insertion ? 1 : (unsigned)(sizeof(positions) / sizeof(*positions));
        for (unsigned index = 0; index < count; ++index) {
            gb->object_attribute_memory[index * 4] = 16;
            gb->object_attribute_memory[index * 4 + 1] = insertion ? 1 : positions[index];
            gb->object_attribute_memory[index * 4 + 2] = 2;
        }
    }
    if (vram) {
        /* Signed tile data keeps A12 high, separately from the map address. */
        memset(gb->video_ram + 0x1800, 0x20, 0x800);
        memset(gb->video_ram + 0x3800, 0, 0x800);
    }
    (void)cupid_gb_read_bus_u8(gb, 0x8007);
    assert(gb->vram_bus.read_value == gb->video_ram[7]);
    cupid_gb_write_u8(gb, 0xff40, window ? 0xf3 : vram ? 0x83 : 0x93);
    unsigned dots = 0;
    while (!reached(gb, scene) && dots < 2000) {
        advance_dot(rig);
        ++dots;
    }
    if (!reached(gb, scene))
        fprintf(stderr,
                "Missing pixel checkpoint: model=%u color=%u scene=%u line=%u "
                "dot=%u x=%d flags=%u stage=%u delay=%u\n",
                gb->model, gb->cgb_mode, scene, gb->pixels.line, gb->pixels.dot,
                gb->pixel_timing.logical_x, gb->pixel_timing.flags, gb->object_fetch_stage,
                gb->pixels.object_delay);
    assert(reached(gb, scene));
    if (scene == HOLD_AFTER_LATE_ENABLE) {
        /* A guest can enable the WX0 alignment after the initial junk pop. */
        cupid_gb_write_u8(gb, 0xff43, 7);
        cupid_gb_write_u8(gb, 0xff4b, 0);
        advance_dot(rig);
        assert(gb->pixel_timing.logical_x == -7);
        assert(gb->pixel_timing.flags & CUPID_GB_PIXEL_WINDOW_HOLD);
    }
    if (scene == HOLD_AFTER_PORT_WRITE) {
        cupid_gb_write_u8(gb, 0xff43, 0);
        cupid_gb_write_u8(gb, 0xff4b, 91);
        assert(gb->pixel_timing.flags & CUPID_GB_PIXEL_WINDOW_HOLD);
    }
    if (scene == INSERT_AFTER_PORT_WRITE) {
        cupid_gb_write_u8(gb, 0xff4b, 83);
        cupid_gb_write_u8(gb, 0xff40, (uint8_t)(gb->io_registers[CUPID_GB_IO_LCDC] & ~0x20u));
        assert(gb->pixel_timing.flags & CUPID_GB_PIXEL_BG_INSERT);
    }
    if (abort) {
        const uint8_t lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
        cupid_gb_write_u8(gb, 0xff40, (uint8_t)(lcdc & ~2u));
        if (scene == ABORT_THEN_REENABLE)
            cupid_gb_write_u8(gb, 0xff40, lcdc);
        assert(gb->object_fetch_stage == CUPID_GB_OBJECT_ABORTED);
        assert(gb->pixels.object_delay == 0);
    }
    if (scene == VRAM_LCD_OFF) {
        const CupidGbVramBus retained = gb->vram_bus;
        cupid_gb_write_u8(gb, 0xff40, 0);
        assert(memcmp(&retained, &gb->vram_bus, sizeof(retained)) == 0);
    }
    assert(cupid_gb_pixels_timing_valid(gb));
    assert(cupid_gb_vram_bus_state_valid(gb));
}

static Blob save_rig(const Rig *rig) {
    Blob blob = {NULL, cupid_gb_state_size(rig->gb)};
    const bool sgb = rig->gb->model == CUPID_GB_MODEL_SGB || rig->gb->model == CUPID_GB_MODEL_SGB2;
    assert(blob.length ==
           CURRENT_BYTES + (sgb ? SGB_BYTES : 0) + (rig->printer ? PRINTER_STATE_WIRE_SIZE : 0));
    blob.bytes = malloc(blob.length);
    CupidGb *before = malloc(sizeof(*before));
    assert(blob.bytes && before);
    memcpy(before, rig->gb, sizeof(*before));
    size_t written = 0;
    const CupidGbStateResult result =
        cupid_gb_save_state_with_context(rig->gb, blob.bytes, blob.length, &written, &rig->context);
    if (result != CUPID_GB_STATE_OK)
        fprintf(stderr,
                "Pixel capture failed: model=%u stage=%u x=%d flags=%u phase=%u "
                "count=%u delay=%u object_delay=%u result=%s\n",
                rig->gb->model, rig->gb->object_fetch_stage, rig->gb->pixel_timing.logical_x,
                rig->gb->pixel_timing.flags, rig->gb->pixels.phase, rig->gb->pixels.count,
                rig->gb->pixels.delay, rig->gb->pixels.object_delay,
                cupid_gb_state_result_name(result));
    assert(result == CUPID_GB_STATE_OK && written == blob.length);
    assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
    free(before);
    return blob;
}

static Blob copy_blob(const Blob *source) {
    Blob copy = {malloc(source->length), source->length};
    assert(copy.bytes);
    memcpy(copy.bytes, source->bytes, copy.length);
    return copy;
}

static size_t descriptor_offset(const Rig *rig, const Blob *blob) {
    const size_t offset =
        blob->length - DESCRIPTOR_BYTES - (rig->printer ? PRINTER_STATE_WIRE_SIZE : 0);
    assert(blob->bytes[offset] == (rig->printer ? 1 : 0));
    return offset;
}

static size_t pixel_offset(const Rig *rig, const Blob *blob) {
    return descriptor_offset(rig, blob) - INPUT_SAMPLE_BYTES - CUPID_GB_VRAM_BUS_STATE_SIZE -
           CUPID_GB_PIXEL_TIMING_STATE_SIZE;
}

static CupidGbStateResult load_rig(Rig *rig, const Blob *blob, unsigned api) {
    const unsigned images = rig->images, completed = rig->completed;
    CupidGbStateResult result;
    switch (api) {
        case 0:
            result = cupid_gb_load_state(rig->gb, blob->bytes, blob->length);
            break;
        case 1:
            result =
                cupid_gb_load_state_with_context(rig->gb, blob->bytes, blob->length, &rig->context);
            break;
        case 2:
            result = cupid_gb_load_state_live(rig->gb, blob->bytes, blob->length);
            break;
        default:
            result = cupid_gb_load_state_live_with_context(rig->gb, blob->bytes, blob->length,
                                                           &rig->context);
            break;
    }
    assert(rig->images == images && rig->completed == completed);
    if (rig->printer)
        assert(rig->gb->serial_link.user_data == rig->printer);
    return result;
}

static void compare_display(const Rig *a, const Rig *b) {
    assert(memcmp(&a->gb->pixels, &b->gb->pixels, sizeof(a->gb->pixels)) == 0);
    assert(a->gb->pixel_timing.logical_x == b->gb->pixel_timing.logical_x);
    assert(a->gb->pixel_timing.flags == b->gb->pixel_timing.flags);
    assert(a->gb->object_fetch_stage == b->gb->object_fetch_stage);
    assert(a->gb->ppu_counter == b->gb->ppu_counter);
    assert(a->gb->ppu_transfer_end_dot == b->gb->ppu_transfer_end_dot);
    assert(a->gb->window_line_counter == b->gb->window_line_counter);
    assert(a->gb->elapsed_cycles == b->gb->elapsed_cycles);
    assert(a->gb->div_counter == b->gb->div_counter);
    assert(a->gb->divider_subcycle == b->gb->divider_subcycle);
    assert(memcmp(a->gb->io_registers, b->gb->io_registers, sizeof(a->gb->io_registers)) == 0);
    assert(memcmp(a->gb->frame_buffer, b->gb->frame_buffer, sizeof(a->gb->frame_buffer)) == 0);
    assert(memcmp(a->gb->frame_buffer_color, b->gb->frame_buffer_color,
                  sizeof(a->gb->frame_buffer_color)) == 0);
    uint8_t first[CUPID_GB_VRAM_BUS_STATE_SIZE], second[CUPID_GB_VRAM_BUS_STATE_SIZE];
    assert(cupid_gb_vram_bus_state_encode(a->gb, first, sizeof(first)));
    assert(cupid_gb_vram_bus_state_encode(b->gb, second, sizeof(second)));
    assert(memcmp(first, second, sizeof(first)) == 0);
    uint8_t first_clock[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE];
    uint8_t second_clock[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE];
    assert(cupid_gb_display_clock_state_encode(a->gb, first_clock, sizeof(first_clock)));
    assert(cupid_gb_display_clock_state_encode(b->gb, second_clock, sizeof(second_clock)));
    assert(memcmp(first_clock, second_clock, sizeof(first_clock)) == 0);
    if (a->printer && b->printer) {
        uint8_t left[PRINTER_STATE_WIRE_SIZE], right[PRINTER_STATE_WIRE_SIZE];
        assert(cupid_gb_printer_state_encode(&a->printer->state, left, sizeof(left)));
        assert(cupid_gb_printer_state_encode(&b->printer->state, right, sizeof(right)));
        assert(memcmp(left, right, sizeof(left)) == 0);
    }
}

static uint8_t continue_one(Rig *rig, Scene scene, unsigned step) {
    /* Coupled CPU-T deliveries retain DIV, display and DMA history. One complete
     * LCD dot isolates each suspended transaction in either CPU speed mode. */
    uint8_t value = 0xff;
    if (scene == QUIET_FORCED && step == 0) {
        (void)cupid_gb_read_bus_u8(rig->gb, 0xff41);
        assert(rig->gb->vram_bus.map_address == rig->gb->vram_bus.fetch_map_address);
        assert(rig->gb->vram_bus.data_address == rig->gb->vram_bus.fetch_data_address);
    }
    const bool quiet_pending =
        scene == QUIET_COMPLETION && rig->gb->pixels.line == 1 && rig->gb->pixels.active;
    const bool first_hblank = rig->gb->pixels.line == 1 && !rig->gb->pixels.active &&
                              rig->gb->ppu_transfer_end_dot &&
                              rig->gb->pixels.dot == rig->gb->ppu_transfer_end_dot;
    if (!quiet_pending && (step % 37 == 0 || first_hblank)) {
        const uint16_t address = (uint16_t)(step % 2 ? 0x9005 : 0x8007);
        value = cupid_gb_read_bus_u8(rig->gb, address);
    }
    if (scene == VRAM_LCD_OFF && step == 4)
        cupid_gb_write_u8(rig->gb, 0xff40, 0x83);
    const bool aborted = rig->gb->object_fetch_stage == CUPID_GB_OBJECT_ABORTED;
    const unsigned slot = rig->gb->pixels.object_index;
    advance_dot(rig);
    if (aborted) {
        assert(rig->gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE);
        assert(!(rig->gb->pixels.fetched_objects & (1u << slot)));
    }
    return value;
}

static void continue_pair(Rig *expected, Rig *target, Scene scene) {
    for (unsigned step = 0; step < 1400; ++step) {
        assert(continue_one(expected, scene, step) == continue_one(target, scene, step));
        compare_display(expected, target);
    }
}

static void test_continuations(void) {
    for (unsigned profile = 0; profile < sizeof(profiles) / sizeof(*profiles); ++profile) {
        for (Scene scene = FRACTIONAL; scene < SCENE_COUNT; ++scene) {
            if (!available(profile, scene))
                continue;
            const bool attached = (profile + (unsigned)scene) % 2 == 0;
            Rig *source = create_rig(profile, attached);
            prepare_scene(source, scene);
            Blob saved = save_rig(source);
            const size_t offset = pixel_offset(source, &saved);
            uint8_t encoded[CUPID_GB_PIXEL_TIMING_STATE_SIZE];
            assert(cupid_gb_pixels_timing_encode(source->gb, encoded, sizeof(encoded)));
            assert(memcmp(saved.bytes + offset, encoded, sizeof(encoded)) == 0);
            for (unsigned api = 0; api < 4; ++api) {
                /* The expected timeline never decodes the snapshot. Recreate its
                 * exact operations so a lossy codec cannot fool both sides. */
                Rig *expected = create_rig(profile, attached);
                Rig *target = create_rig(profile, attached);
                prepare_scene(expected, scene);
                cupid_gb_tick(target->gb, 97);
                assert(load_rig(target, &saved, api) == CUPID_GB_STATE_OK);
                compare_display(expected, target);
                if (api < 2) {
                    Blob restored = save_rig(target);
                    assert(restored.length == saved.length);
                    assert(memcmp(restored.bytes, saved.bytes, saved.length) == 0);
                    free(restored.bytes);
                }
                continue_pair(expected, target, scene);
                ++continued_cases;
                destroy_rig(expected);
                destroy_rig(target);
            }
            free(saved.bytes);
            destroy_rig(source);
        }
    }
}

static Blob legacy_blob(const Rig *rig, const Blob *source, unsigned version) {
    assert(version >= 9 && version <= 14);
    Blob old = copy_blob(source);
    if (version >= 12) {
        const size_t descriptor = descriptor_offset(rig, source);
        const size_t removed = INPUT_SAMPLE_BYTES + TIMING_BYTES +
                               (version < 14 ? CUPID_GB_AUDIO_STATE_SIZE : 0) +
                               (version < 13 ? RTC_BYTES : 0);
        memmove(old.bytes + descriptor - removed, old.bytes + descriptor,
                source->length - descriptor);
        old.length -= removed;
    } else {
        assert(rig->gb->model != CUPID_GB_MODEL_SGB && rig->gb->model != CUPID_GB_MODEL_SGB2);
        old.length = version == 9 ? V11_BYTES - 6 : version == 10 ? V11_BYTES - 2 : V11_BYTES;
    }
    put_le(old.bytes + 8, version, 4);
    resign(&old);
    return old;
}

static void test_legacy_projection(void) {
    static const struct {
        unsigned profile;
        Scene scene;
    } cases[] = {{0, WINDOW_HOLD},  {5, WX_READ_LATCH},    {6, WX_READ_LATCH},
                 {5, INSERT_WORD},  {6, INSERT_LOW},       {0, VRAM_HBLANK},
                 {5, VRAM_LCD_OFF}, {0, QUIET_COMPLETION}, {5, QUIET_FORCED}};

    for (unsigned index = 0; index < sizeof(cases) / sizeof(*cases); ++index) {
        Rig *source = create_rig(cases[index].profile, true);
        prepare_scene(source, cases[index].scene);
        Blob current = save_rig(source);
        Blob old = legacy_blob(source, &current, 14);
        for (unsigned api = 0; api < 4; ++api) {
            Rig *a = create_rig(cases[index].profile, true);
            Rig *b = create_rig(cases[index].profile, true);
            cupid_gb_tick(a->gb, 7);
            cupid_gb_tick(b->gb, 103);
            memset(&a->gb->pixel_timing, 0xa5, sizeof(a->gb->pixel_timing));
            memset(&b->gb->pixel_timing, 0x5a, sizeof(b->gb->pixel_timing));
            memset(&a->gb->vram_bus, 0xa5, sizeof(a->gb->vram_bus));
            memset(&b->gb->vram_bus, 0x5a, sizeof(b->gb->vram_bus));
            assert(load_rig(a, &old, api) == CUPID_GB_STATE_OK);
            assert(load_rig(b, &old, api) == CUPID_GB_STATE_OK);
            assert(!(a->gb->pixel_timing.flags &
                     (CUPID_GB_PIXEL_WINDOW_HOLD | CUPID_GB_PIXEL_BG_INSERT)));
            assert(a->gb->vram_bus.read_value == 0);
            assert(a->gb->vram_bus.fetch_map_address == a->gb->vram_bus.map_address);
            assert(a->gb->vram_bus.fetch_data_address == a->gb->vram_bus.data_address);
            if (a->gb->pixels.phase == 1 || a->gb->pixels.phase == 2) {
                assert(a->gb->vram_bus.data_address == 0);
                const uint16_t address = a->gb->pixels.address;
                assert(a->gb->vram_bus.map_address ==
                       (address >= 0x1800 && address < 0x2000 ? address : 0));
            } else {
                assert(a->gb->vram_bus.map_address == 0);
                assert(a->gb->vram_bus.data_address == a->gb->pixels.address);
            }
            compare_display(a, b);
            /* An older file has no oracle for omitted history. Require a stable
             * projection independent of the receiving machines' prior activity. */
            continue_pair(a, b, cases[index].scene);
            ++legacy_cases;
            destroy_rig(a);
            destroy_rig(b);
        }
        free(old.bytes);
        free(current.bytes);
        destroy_rig(source);
    }
}

static void reject_unchanged(Rig *target, const Blob *bad) {
    CupidGb *before = malloc(sizeof(*before));
    CupidGbPrinter *device = target->printer ? malloc(sizeof(*device)) : NULL;
    assert(before && (!target->printer || device));
    memcpy(before, target->gb, sizeof(*before));
    if (device)
        memcpy(device, target->printer, sizeof(*device));
    const CupidGbStateContext context = target->context;
    for (unsigned api = 0; api < 4; ++api) {
        assert(load_rig(target, bad, api) == CUPID_GB_STATE_INVALID);
        assert(memcmp(before, target->gb, sizeof(*before)) == 0);
        assert(memcmp(&context, &target->context, sizeof(context)) == 0);
        if (device)
            assert(memcmp(device, target->printer, sizeof(*device)) == 0);
        ++rejected_cases;
    }
    free(device);
    free(before);
}

static void corrupt(Rig *target, const Blob *saved, size_t offset, unsigned size, uint32_t value) {
    Blob bad = copy_blob(saved);
    assert(offset + size <= bad.length);
    put_le(bad.bytes + offset, value, size);
    resign(&bad);
    reject_unchanged(target, &bad);
    free(bad.bytes);
}

static void test_malformed_tails(void) {
    static const struct {
        unsigned offset, size;
        uint32_t value;
    } pixel_bad[] = {{0, 2, 0xffef}, {0, 2, 161},  {0, 2, 0x8000}, {0, 2, 0x7fff},
                     {2, 1, 0x20},   {2, 1, 0x80}, {3, 1, 1},      {3, 1, 255}},
      vram_bad[] = {{0, 2, 1},      {0, 2, 0x17ff}, {0, 2, 0x2000}, {0, 2, 0xffff}, {2, 2, 0x4000},
                    {2, 2, 0xffff}, {5, 1, 1},      {5, 1, 255},    {6, 2, 1},      {6, 2, 0x17ff},
                    {6, 2, 0x2000}, {6, 2, 0xffff}, {8, 2, 0x4000}, {8, 2, 0xffff}};

    for (unsigned attached = 0; attached < 2; ++attached) {
        Rig *source = create_rig(5, attached != 0);
        Rig *target = create_rig(5, attached != 0);
        prepare_scene(source, VRAM_HBLANK);
        Blob saved = save_rig(source);
        const size_t pixel = pixel_offset(source, &saved);
        const size_t vram = pixel + CUPID_GB_PIXEL_TIMING_STATE_SIZE;
        for (unsigned index = 0; index < sizeof(pixel_bad) / sizeof(*pixel_bad); ++index)
            corrupt(target, &saved, pixel + pixel_bad[index].offset, pixel_bad[index].size,
                    pixel_bad[index].value);
        for (unsigned index = 0; index < sizeof(vram_bad) / sizeof(*vram_bad); ++index)
            corrupt(target, &saved, vram + vram_bad[index].offset, vram_bad[index].size,
                    vram_bad[index].value);
        for (unsigned which = 0; which < 2; ++which) {
            const size_t start = which ? vram : pixel;
            const size_t size =
                which ? CUPID_GB_VRAM_BUS_STATE_SIZE : CUPID_GB_PIXEL_TIMING_STATE_SIZE;
            for (size_t missing = 1; missing <= size; ++missing) {
                Blob bad = copy_blob(&saved);
                memmove(bad.bytes + start + size - missing, bad.bytes + start + size,
                        bad.length - start - size);
                bad.length -= missing;
                resign(&bad);
                reject_unchanged(target, &bad);
                free(bad.bytes);
            }
        }
        free(saved.bytes);
        destroy_rig(source);
        destroy_rig(target);
    }
}

static void test_malformed_window_history(void) {
    static const struct {
        size_t offset;
        uint8_t value;
    } hold_bad[] = {{PIXEL_COUNT, 1},  {PIXEL_PHASE, 1},   {PIXEL_DELAY, 1},
                    {PIXEL_ACTIVE, 0}, {WINDOW_ACTIVE, 0}, {OBJECT_DELAY, 1}};

    Rig *source = create_rig(0, true), *target = create_rig(0, true);
    prepare_scene(source, WINDOW_HOLD);
    Blob hold = save_rig(source);
    const size_t pixel = pixel_offset(source, &hold);
    corrupt(target, &hold, pixel, 2, 0);
    corrupt(target, &hold, pixel + 2, 1, CUPID_GB_PIXEL_WINDOW_HOLD);
    corrupt(target, &hold, pixel + 2, 1, source->gb->pixel_timing.flags | CUPID_GB_PIXEL_BG_INSERT);
    corrupt(target, &hold, pixel + 2, 1, source->gb->pixel_timing.flags | CUPID_GB_PIXEL_WX_GLITCH);
    for (unsigned index = 0; index < sizeof(hold_bad) / sizeof(*hold_bad); ++index)
        corrupt(target, &hold, hold_bad[index].offset, 1, hold_bad[index].value);
    free(hold.bytes);
    destroy_rig(source);
    destroy_rig(target);

    for (unsigned profile = 5; profile <= 6; ++profile) {
        source = create_rig(profile, true);
        target = create_rig(profile, true);
        prepare_scene(source, INSERT_WORD);
        Blob inserted = save_rig(source);
        const size_t timing = pixel_offset(source, &inserted);
        corrupt(target, &inserted, timing, 2, 0xfff8);
        corrupt(target, &inserted, timing, 2, 160);
        corrupt(target, &inserted, timing + 2, 1,
                source->gb->pixel_timing.flags | CUPID_GB_PIXEL_WINDOW_FIRST);
        corrupt(target, &inserted, PIXEL_ACTIVE, 1, 0);
        corrupt(target, &inserted, PIXEL_LINE, 1, 144);
        corrupt(target, &inserted, PIXEL_X, 1, 160);
        corrupt(target, &inserted, PIXEL_COUNT, 1, 7);
        corrupt(target, &inserted, OBJECT_STAGE, 1, CUPID_GB_OBJECT_IDLE);
        corrupt(target, &inserted, OBJECT_STAGE, 1, CUPID_GB_OBJECT_ABORTED);
        free(inserted.bytes);
        destroy_rig(source);
        destroy_rig(target);

        source = create_rig(profile, true);
        target = create_rig(profile, true);
        prepare_scene(source, WINDOW_FIRST);
        Blob bad_hold = save_rig(source);
        const size_t offset = pixel_offset(source, &bad_hold);
        put_le(bad_hold.bytes + offset, 0xfff8, 2);
        bad_hold.bytes[offset + 2] = CUPID_GB_PIXEL_WINDOW_FIRST | CUPID_GB_PIXEL_WINDOW_HOLD;
        bad_hold.bytes[PIXEL_COUNT] = bad_hold.bytes[PIXEL_PHASE] = bad_hold.bytes[PIXEL_DELAY] = 0;
        resign(&bad_hold);
        reject_unchanged(target, &bad_hold);
        free(bad_hold.bytes);
        destroy_rig(source);
        destroy_rig(target);
    }
}

static void test_malformed_abort(void) {
    static const struct {
        size_t offset;
        unsigned size;
        uint32_t value;
    } invalid[] = {{OBJECT_STAGE, 1, CUPID_GB_OBJECT_ABORTED + 1},
                   {OBJECT_INDEX, 1, 10},
                   {OBJECT_COUNT, 1, 0},
                   {OBJECT_FETCHED, 2, 0},
                   {OBJECT_DELAY, 1, 1},
                   {PIXEL_ACTIVE, 1, 0},
                   {PIXEL_LINE, 1, 144},
                   {PIXEL_X, 1, 160},
                   {PIXEL_COUNT, 1, 0},
                   {PIXEL_DELAY, 1, 8}};

    Rig *source = create_rig(0, true), *target = create_rig(0, true);
    prepare_scene(source, ABORT_ALIGNMENT);
    Blob saved = save_rig(source);
    assert(saved.bytes[OBJECT_STAGE] == CUPID_GB_OBJECT_ABORTED);
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(*invalid); ++index)
        corrupt(target, &saved, invalid[index].offset, invalid[index].size, invalid[index].value);
    for (unsigned version = 9; version <= 14; ++version) {
        Blob older = legacy_blob(source, &saved, version);
        assert(older.bytes[OBJECT_STAGE] == CUPID_GB_OBJECT_ABORTED);
        reject_unchanged(target, &older);
        free(older.bytes);
    }
    free(saved.bytes);
    destroy_rig(source);
    destroy_rig(target);
}

static void test_direct_codecs(void) {
    Rig *rig = create_rig(0, false);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    uint8_t pixel[CUPID_GB_PIXEL_TIMING_STATE_SIZE + 1] = {0};
    uint8_t vram[CUPID_GB_VRAM_BUS_STATE_SIZE + 1] = {0};
    for (int position = -16; position <= 160; ++position) {
        rig->gb->pixel_timing = (CupidGbPixelTiming){.logical_x = (int16_t)position};
        assert(cupid_gb_pixels_timing_encode(rig->gb, pixel, CUPID_GB_PIXEL_TIMING_STATE_SIZE));
        assert(pixel[0] == (uint8_t)position && pixel[1] == (uint8_t)((uint16_t)position >> 8));
        assert(pixel[2] == 0 && pixel[3] == 0);
        rig->gb->pixel_timing.logical_x = 0;
        assert(cupid_gb_pixels_timing_decode(rig->gb, pixel, CUPID_GB_PIXEL_TIMING_STATE_SIZE));
        assert(rig->gb->pixel_timing.logical_x == position);
        ++codec_cases;
    }
    static const uint16_t maps[] = {0, 0x1800, 0x1fff};
    static const uint16_t data[] = {0, 0x1000, 0x3fff};
    for (unsigned index = 0; index < 3; ++index) {
        rig->gb->vram_bus = (CupidGbVramBus){.map_address = maps[index],
                                             .data_address = data[index],
                                             .read_value = 0xff,
                                             .fetch_map_address = maps[2 - index],
                                             .fetch_data_address = data[2 - index]};
        assert(cupid_gb_vram_bus_state_encode(rig->gb, vram, CUPID_GB_VRAM_BUS_STATE_SIZE));
        rig->gb->vram_bus = (CupidGbVramBus){0};
        assert(cupid_gb_vram_bus_state_decode(rig->gb, vram, CUPID_GB_VRAM_BUS_STATE_SIZE));
        assert(rig->gb->vram_bus.map_address == maps[index]);
        assert(rig->gb->vram_bus.data_address == data[index] &&
               rig->gb->vram_bus.read_value == 0xff);
        assert(rig->gb->vram_bus.fetch_map_address == maps[2 - index]);
        assert(rig->gb->vram_bus.fetch_data_address == data[2 - index]);
        ++codec_cases;
    }
    memcpy(before, rig->gb, sizeof(*before));
    for (size_t size = 0; size <= sizeof(pixel); ++size) {
        if (size == CUPID_GB_PIXEL_TIMING_STATE_SIZE)
            continue;
        assert(!cupid_gb_pixels_timing_decode(rig->gb, pixel, size));
        assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
        ++codec_cases;
    }
    for (size_t size = 0; size <= sizeof(vram); ++size) {
        if (size == CUPID_GB_VRAM_BUS_STATE_SIZE)
            continue;
        assert(!cupid_gb_vram_bus_state_decode(rig->gb, vram, size));
        assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
        ++codec_cases;
    }
    pixel[0] = 161;
    pixel[1] = 0;
    assert(!cupid_gb_pixels_timing_decode(rig->gb, pixel, CUPID_GB_PIXEL_TIMING_STATE_SIZE));
    assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
    pixel[0] = 0;
    pixel[3] = 1;
    assert(!cupid_gb_pixels_timing_decode(rig->gb, pixel, CUPID_GB_PIXEL_TIMING_STATE_SIZE));
    assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
    vram[1] = 0x20;
    assert(!cupid_gb_vram_bus_state_decode(rig->gb, vram, CUPID_GB_VRAM_BUS_STATE_SIZE));
    assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
    vram[1] = 0x18;
    vram[5] = 1;
    assert(!cupid_gb_vram_bus_state_decode(rig->gb, vram, CUPID_GB_VRAM_BUS_STATE_SIZE));
    assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
    vram[5] = 0;
    vram[7] = 0x20;
    assert(!cupid_gb_vram_bus_state_decode(rig->gb, vram, CUPID_GB_VRAM_BUS_STATE_SIZE));
    assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
    vram[7] = 0;
    vram[9] = 0x40;
    assert(!cupid_gb_vram_bus_state_decode(rig->gb, vram, CUPID_GB_VRAM_BUS_STATE_SIZE));
    assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
    codec_cases += 6;

    const size_t size = cupid_gb_state_size(rig->gb);
    uint8_t *buffer = malloc(size), *original = malloc(size);
    assert(buffer && original);
    memset(buffer, 0xa5, size);
    memcpy(original, buffer, size);
    for (unsigned field = 0; field < 5; ++field) {
        memcpy(rig->gb, before, sizeof(*before));
        if (field == 0)
            rig->gb->pixel_timing.logical_x = 161;
        else if (field == 1)
            rig->gb->vram_bus.map_address = 0x17ff;
        else if (field == 2)
            rig->gb->vram_bus.data_address = 0x4000;
        else if (field == 3)
            rig->gb->vram_bus.fetch_map_address = 0x2000;
        else
            rig->gb->vram_bus.fetch_data_address = 0x4000;
        size_t written = 123;
        assert(cupid_gb_save_state(rig->gb, buffer, size, &written) == CUPID_GB_STATE_INVALID);
        assert(written == 0 && memcmp(buffer, original, size) == 0);
        ++codec_cases;
    }
    memcpy(rig->gb, before, sizeof(*before));
    free(original);
    free(buffer);
    free(before);
    destroy_rig(rig);
}

static void write_file(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, size, file) == size && fclose(file) == 0);
}

static void print_digest(const char *name, const void *bytes, size_t size) {
    uint8_t digest[32];
    cupid_sha256(bytes, size, digest);
    printf("%s size=%zu sha256=", name, size);
    for (unsigned index = 0; index < sizeof(digest); ++index)
        printf("%02x", digest[index]);
    putchar('\n');
}

static int portable_fixture(bool read, unsigned selection, const char *initial_path,
                            const char *final_path) {
    static const struct {
        unsigned profile;
        Scene scene;
    } cases[] = {{0, WINDOW_HOLD},       {0, HOLD_AFTER_LATE_ENABLE},
                 {2, INSERT_THEN_ABORT}, {5, INSERT_LOW},
                 {6, WX_READ_LATCH},     {5, VRAM_HBLANK},
                 {0, VRAM_LCD_OFF},      {4, ABORT_THEN_REENABLE},
                 {7, VRAM_HBLANK},       {0, QUIET_COMPLETION},
                 {5, QUIET_FORCED}};

    assert(selection < sizeof(cases) / sizeof(*cases));
    Rig *rig = create_rig(cases[selection].profile, true);
    Blob initial;
    if (read) {
        FILE *file = fopen(initial_path, "rb");
        assert(file && fseek(file, 0, SEEK_END) == 0);
        const long length = ftell(file);
        assert(length > HEADER_BYTES && length < 400000 && fseek(file, 0, SEEK_SET) == 0);
        initial = (Blob){malloc((size_t)length), (size_t)length};
        assert(initial.bytes && fread(initial.bytes, 1, initial.length, file) == initial.length);
        assert(fclose(file) == 0);
        assert(load_rig(rig, &initial, 0) == CUPID_GB_STATE_OK);
    } else {
        prepare_scene(rig, cases[selection].scene);
        initial = save_rig(rig);
        write_file(initial_path, initial.bytes, initial.length);
    }
    print_digest("CUPIDGB_PIXEL_INITIAL", initial.bytes, initial.length);

    enum { TRACE_STEPS = 2048, TRACE_BYTES = TIMING_BYTES + 9 };

    uint8_t *trace = malloc(TRACE_STEPS * TRACE_BYTES);
    assert(trace);
    for (unsigned step = 0; step < TRACE_STEPS; ++step) {
        const uint8_t read_value = continue_one(rig, cases[selection].scene, step);
        uint8_t *row = trace + step * TRACE_BYTES;
        assert(
            cupid_gb_display_clock_state_encode(rig->gb, row, CUPID_GB_DISPLAY_CLOCK_STATE_SIZE));
        assert(cupid_gb_pixels_timing_encode(rig->gb, row + CUPID_GB_DISPLAY_CLOCK_STATE_SIZE,
                                             CUPID_GB_PIXEL_TIMING_STATE_SIZE));
        assert(cupid_gb_vram_bus_state_encode(
            rig->gb, row + CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE,
            CUPID_GB_VRAM_BUS_STATE_SIZE));
        row += TIMING_BYTES;
        put_le(row, rig->gb->pixels.dot, 2);
        row[2] = rig->gb->pixels.line;
        row[3] = rig->gb->pixels.x;
        row[4] = (uint8_t)rig->gb->object_fetch_stage;
        row[5] = rig->gb->pixels.phase;
        row[6] = rig->gb->pixels.count;
        row[7] = rig->gb->pixels.object_delay;
        row[8] = read_value;
    }
    Blob final = save_rig(rig);
    write_file(final_path, final.bytes, final.length);
    char *trace_path = malloc(strlen(final_path) + sizeof(".trace"));
    assert(trace_path);
    strcpy(trace_path, final_path);
    strcat(trace_path, ".trace");
    write_file(trace_path, trace, TRACE_STEPS * TRACE_BYTES);
    print_digest("CUPIDGB_PIXEL_FINAL", final.bytes, final.length);
    print_digest("CUPIDGB_PIXEL_TRACE", trace, TRACE_STEPS * TRACE_BYTES);
    free(trace_path);
    free(trace);
    free(final.bytes);
    free(initial.bytes);
    destroy_rig(rig);
    return 0;
}

int main(int argc, char **argv) {
    _Static_assert(CUPID_GB_PIXEL_TIMING_STATE_SIZE == 4, "Unexpected pixel timing wire size");
    _Static_assert(CUPID_GB_VRAM_BUS_STATE_SIZE == 10, "Unexpected VRAM bus wire size");
    _Static_assert(TIMING_BYTES == 34, "Unexpected V15 timing tail");
    if (argc == 5 &&
        (strcmp(argv[1], "--write-fixture") == 0 || strcmp(argv[1], "--read-fixture") == 0)) {
        char *end;
        const unsigned long selection = strtoul(argv[2], &end, 10);
        assert(*argv[2] && !*end && selection < 11);
        return portable_fixture(strcmp(argv[1], "--read-fixture") == 0, (unsigned)selection,
                                argv[3], argv[4]);
    }
    assert(argc == 1);
    test_continuations();
    test_legacy_projection();
    test_malformed_tails();
    test_malformed_window_history();
    test_malformed_abort();
    test_direct_codecs();
    printf("Pixel state checks: %u continuations, %u legacy projections, "
           "%u transactional rejections, %u codec checks\n",
           continued_cases, legacy_cases, rejected_cases, codec_cases);
    return 0;
}

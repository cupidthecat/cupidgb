/**
 * @file    test_infrared_state.c
 * @brief   Display-clock snapshot continuation, migration, and transactional validation.
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
#include "cupid/gb/infrared.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HEADER_BYTES = 128,
    DESCRIPTOR_BYTES = 8,
    V2_BYTES = 300518,
    V3_BYTES = 300568,
    V4_BYTES = 300572,
    V11_TAIL_BYTES = 28,
    SHARED_IR_BYTES = 6,
    RTC_BYTES = 6,
    HUC3_BYTES = 25,
    SGB_BYTES = 10119,
    CLOCK_CREDIT = 0,
    CLOCK_AGE = 4,
    CLOCK_ROW_DOTS = 8,
    CLOCK_BATCH = 10,
    CLOCK_ROW = 12,
    CLOCK_PHASE = 13,
    CLOCK_CURSOR = 14,
    CLOCK_WINDOW = 15,
    CLOCK_FLAGS = 16,
    CLOCK_FINE_WAIT = 17,
    CLOCK_RESERVED = 18
};

typedef struct Profile {
    CupidGbModel model;
    uint8_t cartridge;
    bool color;
    bool fast;
} Profile;

static const Profile profiles[] = {
    {CUPID_GB_MODEL_CGB, 0, true, false},         {CUPID_GB_MODEL_CGB, 0, true, true},
    {CUPID_GB_MODEL_CGB, 0xfe, true, false},      {CUPID_GB_MODEL_CGB, 0xff, true, false},
    {CUPID_GB_MODEL_CGB, 0, false, false},        {CUPID_GB_MODEL_DMG_ABC, 0xff, false, false},
    {CUPID_GB_MODEL_DMG_ABC, 0xfe, false, false}, {CUPID_GB_MODEL_DMG_ABC, 0, false, false},
    {CUPID_GB_MODEL_DMG0, 0, false, false},       {CUPID_GB_MODEL_MGB, 0, false, false},
    {CUPID_GB_MODEL_SGB, 0, false, false},        {CUPID_GB_MODEL_SGB2, 0, false, false},
    {CUPID_GB_MODEL_SGB, 0xfe, false, false},
};

typedef struct Host {
    unsigned infrared, images, completed;
} Host;

typedef struct Rig {
    CupidGb *gb;
    CupidGbPrinter *printer;
    CupidGbStateContext context;
    Host host;
} Rig;

typedef struct Blob {
    uint8_t *bytes;
    size_t length;
} Blob;

static unsigned continued_cases, legacy_cases, rejected_cases, distinct_histories;

static void put_le(uint8_t *bytes, uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8));
}

static uint32_t get_le(const uint8_t *bytes, unsigned count) {
    uint32_t value = 0;
    for (unsigned index = 0; index < count; ++index)
        value |= (uint32_t)bytes[index] << (index * 8);
    return value;
}

static void resign(Blob *blob) {
    assert(blob->length >= HEADER_BYTES);
    put_le(blob->bytes + 12, (uint32_t)blob->length, 4);
    cupid_sha256_parts(blob->bytes, 96, blob->bytes + HEADER_BYTES, blob->length - HEADER_BYTES,
                       blob->bytes + 96);
}

static void on_infrared(const CupidGb *gb, bool active, uint64_t cycles, void *data) {
    (void)active;
    assert(cycles == gb->elapsed_cycles);
    ++((Host *)data)->infrared;
}

static void on_image(const CupidGbPrinterImage *image, void *data) {
    assert(image);
    ++((Host *)data)->images;
}

static void on_done(void *data) {
    ++((Host *)data)->completed;
}

static int64_t fixture_time(void *data) {
    (void)data;
    return INT64_C(1800000000);
}

static Rig *create_rig(unsigned profile, bool printer) {
    assert(profile < sizeof(profiles) / sizeof(*profiles));
    const Profile *spec = &profiles[profile];
    Rig *rig = calloc(1, sizeof(*rig));
    assert(rig);
    rig->gb = calloc(1, sizeof(*rig->gb));
    assert(rig->gb);
    CupidGb *gb = rig->gb;
    cupid_gb_set_model(gb, spec->model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x49522d5354415445));
    cupid_gb_set_rtc_time_source(gb, fixture_time, NULL);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    uint8_t rom[32768] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    memcpy(rom + 0x134, "IR STATE", 8);
    rom[0x143] = spec->color ? 0x80 : 0;
    rom[0x146] = 3;
    rom[0x147] = spec->cartridge;
    rom[0x149] = spec->cartridge ? 3 : 0;
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[index] - 1u);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    memset(gb->object_attribute_memory, 0, sizeof(gb->object_attribute_memory));
    memset(gb->video_ram, 0, sizeof(gb->video_ram));
    gb->double_speed = spec->fast;
    if (spec->cartridge == 0xfe || spec->cartridge == 0xff)
        cupid_gb_write_u8(gb, 0, 0x0e);
    cupid_gb_write_u8(gb, 0xff56, 0xc0);
    cupid_gb_set_infrared_input(gb, true);
    cupid_gb_set_infrared_callback(gb, on_infrared, &rig->host);
    if (printer) {
        rig->printer = cupid_gb_printer_create(on_image, on_done, &rig->host);
        assert(rig->printer);
        cupid_gb_set_serial_link(gb, cupid_gb_printer_link(rig->printer));
    }
    cupid_gb_tick(gb, 4500);
    if (printer) {
        /* Three serial samples leave a real partial packet to restore with the core. */
        cupid_gb_write_u8(gb, 0xff01, 0x88);
        cupid_gb_write_u8(gb, 0xff02, 0x81);
        cupid_gb_tick(gb, 384);
        assert(rig->printer->state.incoming_bits > 0 && rig->printer->state.incoming_bits < 8);
    }
    cupid_gb_write_u8(gb, 0xff43, 0);
    cupid_gb_write_u8(gb, 0xff4a, 144);
    cupid_gb_write_u8(gb, 0xff4b, 7);
    cupid_gb_write_u8(gb, 0xff40, 0x91);
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

static Blob save_rig(const Rig *rig) {
    Blob blob = {NULL, cupid_gb_state_size(rig->gb)};
    const bool huc3 = rig->gb->header.mbc_type == CUPID_GB_HUC3;
    const bool sgb = rig->gb->model == CUPID_GB_MODEL_SGB || rig->gb->model == CUPID_GB_MODEL_SGB2;
    const size_t expected = V4_BYTES + (sgb ? SGB_BYTES : 0) +
                            (huc3 ? HUC3_BYTES : SHARED_IR_BYTES) + V11_TAIL_BYTES + RTC_BYTES +
                            CUPID_GB_AUDIO_STATE_SIZE + CUPID_GB_DISPLAY_CLOCK_STATE_SIZE +
                            CUPID_GB_PIXEL_TIMING_STATE_SIZE + CUPID_GB_VRAM_BUS_STATE_SIZE +
                            DESCRIPTOR_BYTES + (rig->printer ? PRINTER_STATE_WIRE_SIZE : 0);
    assert(blob.length == expected);
    blob.bytes = malloc(blob.length);
    assert(blob.bytes);
    size_t written = 0;
    const CupidGbStateResult result =
        cupid_gb_save_state_with_context(rig->gb, blob.bytes, blob.length, &written, &rig->context);
    if (result != CUPID_GB_STATE_OK)
        fprintf(stderr, "IR snapshot failed: %s, phase=%u credit=%d batch=%u row=%u\n",
                cupid_gb_state_result_name(result), rig->gb->display_clock.phase,
                rig->gb->display_clock.credit_half_dots, rig->gb->display_clock.batch_half_dots,
                rig->gb->display_clock.row);
    assert(result == CUPID_GB_STATE_OK && written == blob.length);
    assert(get_le(blob.bytes + 8, 4) == 15);
    return blob;
}

static CupidGbStateResult load_rig(Rig *rig, const Blob *blob, unsigned api) {
    const Host before = rig->host;
    const bool input = rig->gb->huc3_ir_input;
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
    assert(memcmp(&before, &rig->host, sizeof(before)) == 0);
    assert(rig->gb->huc3_ir_input == input);
    assert(rig->gb->infrared_callback == on_infrared && rig->gb->infrared_user_data == &rig->host);
    if (rig->printer)
        assert(rig->gb->serial_link.user_data == rig->printer);
    return result;
}

static size_t descriptor_offset(const Rig *rig, const Blob *blob) {
    const size_t offset =
        blob->length - DESCRIPTOR_BYTES - (rig->printer ? PRINTER_STATE_WIRE_SIZE : 0);
    assert(blob->bytes[offset] == (rig->printer ? 1 : 0));
    return offset;
}

static size_t clock_offset(const Rig *rig, const Blob *blob) {
    return descriptor_offset(rig, blob) - CUPID_GB_PIXEL_TIMING_STATE_SIZE -
           CUPID_GB_VRAM_BUS_STATE_SIZE - CUPID_GB_DISPLAY_CLOCK_STATE_SIZE;
}

static Blob copy_blob(const Blob *source) {
    Blob blob = {malloc(source->length), source->length};
    assert(blob.bytes);
    memcpy(blob.bytes, source->bytes, blob.length);
    return blob;
}

static void advance_cycles(Rig *rig, unsigned cycles) {
    /* The scheduler API takes CPU machine cycles, including in double speed. */
    while (cycles) {
        const unsigned span = cycles > 4096 ? 4096 : cycles;
        cupid_gb_tick(rig->gb, (uint16_t)span);
        cycles -= span;
    }
}

static void clock_bytes(const Rig *rig, uint8_t bytes[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE]) {
    assert(cupid_gb_display_clock_state_encode(rig->gb, bytes, CUPID_GB_DISPLAY_CLOCK_STATE_SIZE));
}

static void compare_receiver(const Rig *a, const Rig *b) {
    uint8_t left[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE], right[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE];
    clock_bytes(a, left);
    clock_bytes(b, right);
    assert(memcmp(left, right, sizeof(left)) == 0);
    uint8_t first_pixel[CUPID_GB_PIXEL_TIMING_STATE_SIZE];
    uint8_t second_pixel[CUPID_GB_PIXEL_TIMING_STATE_SIZE];
    assert(cupid_gb_pixels_timing_encode(a->gb, first_pixel, sizeof(first_pixel)));
    assert(cupid_gb_pixels_timing_encode(b->gb, second_pixel, sizeof(second_pixel)));
    assert(memcmp(first_pixel, second_pixel, sizeof(first_pixel)) == 0);
    uint8_t first_vram[CUPID_GB_VRAM_BUS_STATE_SIZE];
    uint8_t second_vram[CUPID_GB_VRAM_BUS_STATE_SIZE];
    assert(cupid_gb_vram_bus_state_encode(a->gb, first_vram, sizeof(first_vram)));
    assert(cupid_gb_vram_bus_state_encode(b->gb, second_vram, sizeof(second_vram)));
    assert(memcmp(first_vram, second_vram, sizeof(first_vram)) == 0);
    assert(a->gb->huc3.ir_warmup == b->gb->huc3.ir_warmup);
    assert(a->gb->huc3.ir_decay == b->gb->huc3.ir_decay);
    assert(a->gb->huc3.ir_detected == b->gb->huc3.ir_detected);
    assert(a->gb->huc3.ir_output == b->gb->huc3.ir_output);
    assert(a->gb->elapsed_cycles == b->gb->elapsed_cycles);
    assert(a->gb->div_counter == b->gb->div_counter);
    assert(a->gb->divider_subcycle == b->gb->divider_subcycle);
    assert(cupid_gb_read_u8(a->gb, 0xff56) == cupid_gb_read_u8(b->gb, 0xff56));
    assert(cupid_gb_read_u8(a->gb, 0xa000) == cupid_gb_read_u8(b->gb, 0xa000));
    if (a->printer && b->printer) {
        uint8_t first[PRINTER_STATE_WIRE_SIZE], second[PRINTER_STATE_WIRE_SIZE];
        assert(cupid_gb_printer_state_encode(&a->printer->state, first, sizeof(first)));
        assert(cupid_gb_printer_state_encode(&b->printer->state, second, sizeof(second)));
        assert(memcmp(first, second, sizeof(first)) == 0);
    }
}

static void continue_one(Rig *rig, unsigned step) {
    CupidGb *gb = rig->gb;
    if (step % 19 == 0)
        cupid_gb_set_infrared_input(gb, (step / 19) % 3 != 0);
    if (step % 31 == 0)
        cupid_gb_write_u8(gb, 0xff04, 0);
    if (step % 37 == 0)
        (void)cupid_gb_read_bus_u8(gb, 0x8000);
    if (step % 43 == 0)
        (void)cupid_gb_read_bus_u8(gb, 0xfe00);
    if (step % 47 == 0)
        cupid_gb_write_u8(gb, 0xff4a, (uint8_t)(step % 144));
    cupid_gb_tick(gb, (uint16_t)(1 + step % 13));
    if (rig->printer)
        cupid_gb_printer_update(rig->printer, gb);
}

static void test_continuation(void) {
    /* Normal-speed machine-cycle positions include OAM/transfer boundaries and VBlank. */
    static const unsigned checkpoints[] = {0,   1,   3,   19,    20,    29,    42,   63,
                                           113, 114, 228, 16308, 16416, 17442, 17556};
    for (unsigned profile = 0; profile < sizeof(profiles) / sizeof(*profiles); ++profile) {
        Rig *source = create_rig(profile, profile % 2 == 0);
        unsigned last = 0;
        for (unsigned checkpoint = 0; checkpoint < sizeof(checkpoints) / sizeof(*checkpoints);
             ++checkpoint) {
            const unsigned speed = profiles[profile].fast ? 2u : 1u;
            advance_cycles(source, (checkpoints[checkpoint] - last) * speed);
            last = checkpoints[checkpoint];
            Blob saved = save_rig(source);
            for (unsigned api = 0; api < 4; ++api) {
                Rig *expected = create_rig(profile, source->printer != NULL);
                Rig *target = create_rig(profile, source->printer != NULL);
                /* The oracle never decodes a snapshot. Preserve the original delivery
                 * boundaries so omitted history cannot disappear on both sides. */
                for (unsigned prior = 0; prior <= checkpoint; ++prior)
                    advance_cycles(expected,
                                   (checkpoints[prior] - (prior ? checkpoints[prior - 1] : 0)) *
                                       speed);
                compare_receiver(source, expected);
                advance_cycles(target, 971);
                (void)cupid_gb_read_bus_u8(target->gb, 0xff41);
                assert(load_rig(target, &saved, api) == CUPID_GB_STATE_OK);
                compare_receiver(expected, target);
                if (api < 2) {
                    Blob restored = save_rig(target);
                    assert(restored.length == saved.length);
                    assert(memcmp(restored.bytes, saved.bytes, saved.length) == 0);
                    free(restored.bytes);
                }
                for (unsigned step = 0; step < 96; ++step) {
                    continue_one(expected, step);
                    continue_one(target, step);
                    compare_receiver(expected, target);
                }
                ++continued_cases;
                destroy_rig(expected);
                destroy_rig(target);
            }
            free(saved.bytes);
        }
        destroy_rig(source);
    }
}

static void find_pending_batch(Rig *rig) {
    for (unsigned cycle = 0; cycle < 2048; ++cycle) {
        const CupidGbDisplayClock *clock = &rig->gb->display_clock;
        if ((clock->phase == CUPID_GB_DISPLAY_CLOCK_OAM_QUIET ||
             clock->phase == CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET) &&
            clock->credit_half_dots > 0)
            return;
        cupid_gb_tick(rig->gb, 1);
    }
    assert(!"No reachable pending display batch");
}

static void test_distinct_bus_histories(void) {
    const unsigned capable_profiles[] = {0, 1, 2, 3, 5, 6};
    for (unsigned index = 0; index < sizeof(capable_profiles) / sizeof(*capable_profiles);
         ++index) {
        const unsigned profile = capable_profiles[index];
        Rig *quiet = create_rig(profile, false);
        find_pending_batch(quiet);
        Blob initial = save_rig(quiet);
        Rig *forced = create_rig(profile, false);
        assert(load_rig(forced, &initial, 0) == CUPID_GB_STATE_OK);
        const uint64_t cycles = forced->gb->elapsed_cycles;
        const uint16_t charge = forced->gb->huc3.ir_warmup;
        (void)cupid_gb_read_bus_u8(forced->gb, 0x8000);
        assert(forced->gb->elapsed_cycles == cycles && forced->gb->huc3.ir_warmup == charge);
        Blob synchronized = save_rig(forced);
        assert(initial.length == synchronized.length);
        const size_t offset = clock_offset(quiet, &initial);
        assert(memcmp(initial.bytes + offset, synchronized.bytes + offset,
                      CUPID_GB_DISPLAY_CLOCK_STATE_SIZE) != 0);
        ++distinct_histories;
        Rig *copies[] = {create_rig(profile, false), create_rig(profile, false)};
        assert(load_rig(copies[0], &initial, 0) == CUPID_GB_STATE_OK);
        assert(load_rig(copies[1], &synchronized, 1) == CUPID_GB_STATE_OK);
        for (unsigned step = 0; step < 256; ++step) {
            continue_one(quiet, step);
            continue_one(forced, step);
            continue_one(copies[0], step);
            continue_one(copies[1], step);
            compare_receiver(quiet, copies[0]);
            compare_receiver(forced, copies[1]);
        }
        free(initial.bytes);
        free(synchronized.bytes);
        destroy_rig(copies[0]);
        destroy_rig(copies[1]);
        destroy_rig(quiet);
        destroy_rig(forced);
    }
}

static Blob legacy_blob(const Rig *rig, const Blob *current, unsigned version) {
    assert(version >= 2 && version <= 14);
    const size_t descriptor = descriptor_offset(rig, current);
    Blob old = copy_blob(current);
    if (version >= 12) {
        const size_t removed = CUPID_GB_DISPLAY_CLOCK_STATE_SIZE +
                               CUPID_GB_PIXEL_TIMING_STATE_SIZE + CUPID_GB_VRAM_BUS_STATE_SIZE +
                               (version < 14 ? CUPID_GB_AUDIO_STATE_SIZE : 0) +
                               (version < 13 ? RTC_BYTES : 0);
        memmove(old.bytes + descriptor - removed, old.bytes + descriptor,
                current->length - descriptor);
        old.length -= removed;
    } else {
        const bool huc3 = rig->gb->header.mbc_type == CUPID_GB_HUC3;
        const size_t v11 = clock_offset(rig, current) - CUPID_GB_AUDIO_STATE_SIZE - RTC_BYTES -
                           (huc3 ? 0 : SHARED_IR_BYTES);
        static const size_t tails[] = {0, 4, 5, 16, 22, 26, 28};
        old.length = version == 2   ? V2_BYTES
                     : version == 3 ? V3_BYTES
                     : version == 4 ? V4_BYTES
                                    : v11 - V11_TAIL_BYTES + tails[version - 5];
    }
    put_le(old.bytes + 8, version, 4);
    resign(&old);
    return old;
}

static void test_legacy_defaults(void) {
    const unsigned selected[] = {0, 2, 3, 5, 6, 7, 10, 12};
    for (unsigned index = 0; index < sizeof(selected) / sizeof(*selected); ++index) {
        const unsigned profile = selected[index];
        Rig *source = create_rig(profile, true);
        advance_cycles(source, 173);
        cupid_gb_apu_run(source->gb, true);
        Blob current = save_rig(source);
        const bool huc3 = source->gb->header.mbc_type == CUPID_GB_HUC3;
        const bool sgb =
            source->gb->model == CUPID_GB_MODEL_SGB || source->gb->model == CUPID_GB_MODEL_SGB2;
        for (unsigned version = 2; version <= 14; ++version) {
            if (version == 5 && !huc3 && !sgb)
                continue;
            Blob old = legacy_blob(source, &current, version);
            Blob unchanged = copy_blob(&old);
            for (unsigned api = 0; api < 4; ++api) {
                Rig *a = create_rig(profile, true), *b = create_rig(profile, true);
                advance_cycles(a, 3);
                advance_cycles(b, 1337);
                (void)cupid_gb_read_bus_u8(b->gb, 0x8000);
                /* Missing history must overwrite every field, including values a
                 * receiver could not produce through ordinary execution. */
                memset(&a->gb->display_clock, 0xa5, sizeof(a->gb->display_clock));
                memset(&b->gb->display_clock, 0x5a, sizeof(b->gb->display_clock));
                memset(&a->gb->pixel_timing, 0xa5, sizeof(a->gb->pixel_timing));
                memset(&b->gb->pixel_timing, 0x5a, sizeof(b->gb->pixel_timing));
                memset(&a->gb->vram_bus, 0xa5, sizeof(a->gb->vram_bus));
                memset(&b->gb->vram_bus, 0x5a, sizeof(b->gb->vram_bus));
                cupid_gb_set_infrared_input(a->gb, false);
                cupid_gb_set_infrared_input(b->gb, true);
                const CupidGbStateResult first = load_rig(a, &old, api);
                const CupidGbStateResult second = load_rig(b, &old, api);
                if (first != CUPID_GB_STATE_OK || second != CUPID_GB_STATE_OK)
                    fprintf(stderr, "Legacy IR profile %u version %u API %u: %s / %s\n", profile,
                            version, api, cupid_gb_state_result_name(first),
                            cupid_gb_state_result_name(second));
                assert(first == CUPID_GB_STATE_OK && second == CUPID_GB_STATE_OK);
                assert(!a->gb->huc3_ir_input && b->gb->huc3_ir_input);
                compare_receiver(a, b);
                assert(a->gb->display_clock.batch_half_dots == 0);
                assert(!(a->gb->display_clock.flags & CUPID_GB_DISPLAY_CLOCK_WINDOW_PENDING));
                const bool saved_ir = version >= 12 || (huc3 && version >= 5);
                assert(a->gb->huc3.ir_warmup == (saved_ir ? source->gb->huc3.ir_warmup : 0));
                assert(a->gb->huc3.ir_decay == (saved_ir ? source->gb->huc3.ir_decay : 0));
                cupid_gb_set_infrared_input(a->gb, true);
                for (unsigned step = 0; step < 32; ++step) {
                    continue_one(a, step);
                    continue_one(b, step);
                    compare_receiver(a, b);
                }
                ++legacy_cases;
                destroy_rig(a);
                destroy_rig(b);
            }
            assert(memcmp(old.bytes, unchanged.bytes, old.length) == 0);
            free(unchanged.bytes);
            free(old.bytes);
        }
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

static void test_malformed_history(void) {
    static const struct {
        unsigned offset, size;
        uint32_t value;
    } invalid[] = {{CLOCK_CREDIT, 4, UINT32_C(0x80000000)},
                   {CLOCK_CREDIT, 4, INT32_MAX},
                   {CLOCK_AGE, 4, UINT32_MAX},
                   {CLOCK_ROW_DOTS, 2, UINT16_MAX},
                   {CLOCK_BATCH, 2, UINT16_MAX},
                   {CLOCK_ROW, 1, 255},
                   {CLOCK_PHASE, 1, 255},
                   {CLOCK_CURSOR, 1, 255},
                   {CLOCK_WINDOW, 1, 8},
                   {CLOCK_FLAGS, 1, 0x40},
                   {CLOCK_FINE_WAIT, 1, 255},
                   {CLOCK_RESERVED, 1, 1},
                   {CLOCK_RESERVED + 1, 1, 1}};

    for (unsigned attached = 0; attached < 2; ++attached) {
        Rig *source = create_rig(2, attached != 0);
        Rig *target = create_rig(2, attached != 0);
        find_pending_batch(source);
        advance_cycles(target, 971);
        Blob saved = save_rig(source);
        const size_t offset = clock_offset(source, &saved);
        const size_t descriptor = descriptor_offset(source, &saved);
        for (unsigned index = 0; index < sizeof(invalid) / sizeof(*invalid); ++index) {
            Blob bad = copy_blob(&saved);
            put_le(bad.bytes + offset + invalid[index].offset, invalid[index].value,
                   invalid[index].size);
            resign(&bad);
            reject_unchanged(target, &bad);
            free(bad.bytes);
        }
        /* Keep the accessory intact while removing each possible timing-tail suffix. */
        for (unsigned missing = 1;
             missing <= CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
                            CUPID_GB_VRAM_BUS_STATE_SIZE;
             ++missing) {
            Blob bad = copy_blob(&saved);
            memmove(bad.bytes + descriptor - missing, bad.bytes + descriptor,
                    saved.length - descriptor);
            bad.length -= missing;
            resign(&bad);
            reject_unchanged(target, &bad);
            free(bad.bytes);
        }
        Blob wrong_version = copy_blob(&saved);
        put_le(wrong_version.bytes + 8, 14, 4);
        resign(&wrong_version);
        reject_unchanged(target, &wrong_version);
        free(wrong_version.bytes);
        for (unsigned scenario = 0; scenario < 3; ++scenario) {
            Blob bad = copy_blob(&saved);
            uint8_t *clock = bad.bytes + offset;
            memset(clock, 0, CUPID_GB_DISPLAY_CLOCK_STATE_SIZE);
            clock[CLOCK_PHASE] = scenario == 0   ? CUPID_GB_DISPLAY_CLOCK_OAM_QUIET
                                 : scenario == 1 ? CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET
                                                 : CUPID_GB_DISPLAY_CLOCK_IDLE_FRAME;
            put_le(clock + CLOCK_BATCH, scenario == 0 ? 600 : scenario == 1 ? 160 : 0, 2);
            if (scenario == 2)
                clock[CLOCK_CURSOR] = 1;
            resign(&bad);
            reject_unchanged(target, &bad);
            free(bad.bytes);
        }
        free(saved.bytes);
        destroy_rig(source);
        destroy_rig(target);
    }
}

static void test_all_models_and_pure_capture(void) {
    const unsigned selected[] = {0, 2, 4, 7, 8, 9, 10, 11};
    const uint16_t addresses[] = {0x8000, 0x9fff, 0xfe00, 0xfeff, 0xff04, 0xff0f,
                                  0xff40, 0xff41, 0xff4a, 0xff56, 0xffff};
    for (unsigned index = 0; index < sizeof(selected) / sizeof(*selected); ++index) {
        Rig *rig = create_rig(selected[index], true);
        advance_cycles(rig, 116);
        CupidGb *before = malloc(sizeof(*before));
        assert(before);
        memcpy(before, rig->gb, sizeof(*before));
        const CupidGbPrinter device = *rig->printer;
        const Host host = rig->host;
        Blob saved = save_rig(rig);
        for (unsigned address = 0; address < sizeof(addresses) / sizeof(*addresses); ++address)
            (void)cupid_gb_read_u8(rig->gb, addresses[address]);
        assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
        assert(memcmp(&device, rig->printer, sizeof(device)) == 0);
        assert(memcmp(&host, &rig->host, sizeof(host)) == 0);
        const size_t offset = clock_offset(rig, &saved);
        /* Every model retains display scheduling because quiet fetches affect
         * later video-port reads even without an infrared receiver. */
        const uint8_t empty[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE] = {0};
        assert(memcmp(saved.bytes + offset, empty, sizeof(empty)) != 0);
        const Profile *profile = &profiles[selected[index]];
        if (profile->model != CUPID_GB_MODEL_CGB && profile->cartridge != 0xfe &&
            profile->cartridge != 0xff)
            assert(!rig->gb->huc3.ir_warmup && !rig->gb->huc3.ir_decay &&
                   !rig->gb->huc3.ir_detected && !rig->gb->huc3.ir_output);
        Blob bad = copy_blob(&saved);
        bad.bytes[offset + CLOCK_PHASE] = 255;
        resign(&bad);
        reject_unchanged(rig, &bad);
        free(bad.bytes);
        uint8_t encoded[CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + 1];
        memset(encoded, 0xa5, sizeof(encoded));
        assert(!cupid_gb_display_clock_state_encode(rig->gb, encoded,
                                                    CUPID_GB_DISPLAY_CLOCK_STATE_SIZE - 1));
        for (unsigned byte = 0; byte < sizeof(encoded); ++byte)
            assert(encoded[byte] == 0xa5);
        assert(!cupid_gb_display_clock_state_decode(rig->gb, saved.bytes + offset,
                                                    CUPID_GB_DISPLAY_CLOCK_STATE_SIZE - 1));
        assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
        memcpy(encoded, saved.bytes + offset, CUPID_GB_DISPLAY_CLOCK_STATE_SIZE);
        encoded[CLOCK_RESERVED] = 1;
        assert(!cupid_gb_display_clock_state_decode(rig->gb, encoded,
                                                    CUPID_GB_DISPLAY_CLOCK_STATE_SIZE));
        assert(memcmp(before, rig->gb, sizeof(*before)) == 0);
        const CupidGbDisplayClock valid = rig->gb->display_clock;
        rig->gb->display_clock.fine_wait_half_dots = 255;
        memcpy(before, rig->gb, sizeof(*before));
        uint8_t *output = malloc(saved.length);
        assert(output);
        memset(output, 0xa5, saved.length);
        size_t written = 123;
        assert(cupid_gb_save_state(rig->gb, output, saved.length, &written) ==
               CUPID_GB_STATE_INVALID);
        assert(written == 0 && memcmp(before, rig->gb, sizeof(*before)) == 0);
        assert(memcmp(&device, rig->printer, sizeof(device)) == 0);
        for (size_t byte = 0; byte < saved.length; ++byte)
            assert(output[byte] == 0xa5);
        free(output);
        rig->gb->display_clock = valid;
        free(before);
        free(saved.bytes);
        destroy_rig(rig);
    }
}

static void write_file(const char *path, const void *bytes, size_t length) {
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static void print_digest(const char *name, const void *bytes, size_t length) {
    uint8_t digest[32];
    cupid_sha256(bytes, length, digest);
    printf("%s size=%zu sha256=", name, length);
    for (unsigned index = 0; index < sizeof(digest); ++index)
        printf("%02x", digest[index]);
    putchar('\n');
}

static int portable_fixture(bool read, const char *initial_path, const char *final_path) {
    Rig *rig = create_rig(2, true);
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
        find_pending_batch(rig);
        (void)cupid_gb_read_bus_u8(rig->gb, 0x8000);
        cupid_gb_write_u8(rig->gb, 0xff04, 0);
        initial = save_rig(rig);
        write_file(initial_path, initial.bytes, initial.length);
    }
    print_digest("CUPIDGB_IR_INITIAL", initial.bytes, initial.length);

    enum {
        TRACE_STEPS = 4096,
        TIMING_BYTES = CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
                       CUPID_GB_VRAM_BUS_STATE_SIZE,
        TRACE_BYTES = TIMING_BYTES + 3
    };

    uint8_t *trace = malloc(TRACE_STEPS * TRACE_BYTES);
    assert(trace);
    for (unsigned step = 0; step < TRACE_STEPS; ++step) {
        continue_one(rig, step);
        uint8_t *row = trace + step * TRACE_BYTES;
        clock_bytes(rig, row);
        assert(cupid_gb_pixels_timing_encode(rig->gb, row + CUPID_GB_DISPLAY_CLOCK_STATE_SIZE,
                                             CUPID_GB_PIXEL_TIMING_STATE_SIZE));
        assert(cupid_gb_vram_bus_state_encode(
            rig->gb, row + CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE,
            CUPID_GB_VRAM_BUS_STATE_SIZE));
        row[TIMING_BYTES] = cupid_gb_read_u8(rig->gb, 0xff56);
        row[TIMING_BYTES + 1] = cupid_gb_read_u8(rig->gb, 0xa000);
        row[TIMING_BYTES + 2] = (uint8_t)cupid_gb_infrared_detected(rig->gb);
    }
    Blob final = save_rig(rig);
    write_file(final_path, final.bytes, final.length);
    char *trace_path = malloc(strlen(final_path) + sizeof(".trace"));
    assert(trace_path);
    strcpy(trace_path, final_path);
    strcat(trace_path, ".trace");
    write_file(trace_path, trace, TRACE_STEPS * TRACE_BYTES);
    print_digest("CUPIDGB_IR_FINAL", final.bytes, final.length);
    print_digest("CUPIDGB_IR_TRACE", trace, TRACE_STEPS * TRACE_BYTES);
    free(trace_path);
    free(trace);
    free(initial.bytes);
    free(final.bytes);
    destroy_rig(rig);
    return 0;
}

int main(int argc, char **argv) {
    _Static_assert(CUPID_GB_DISPLAY_CLOCK_STATE_SIZE == 20, "Unexpected display-clock wire size");
    _Static_assert(CUPID_GB_PIXEL_TIMING_STATE_SIZE == 4, "Unexpected pixel timing wire size");
    _Static_assert(CUPID_GB_VRAM_BUS_STATE_SIZE == 10, "Unexpected VRAM bus wire size");
    _Static_assert(CLOCK_RESERVED + 2 == CUPID_GB_DISPLAY_CLOCK_STATE_SIZE,
                   "Display-clock wire offsets disagree");
    if (argc == 4 && strcmp(argv[1], "--write-fixture") == 0)
        return portable_fixture(false, argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[1], "--read-fixture") == 0)
        return portable_fixture(true, argv[2], argv[3]);
    assert(argc == 1);
    test_continuation();
    test_distinct_bus_histories();
    test_legacy_defaults();
    test_malformed_history();
    test_all_models_and_pure_capture();
    printf("IR state checks: %u continuations, %u distinct bus histories, %u legacy imports, "
           "%u transactional rejections\n",
           continued_cases, distinct_histories, legacy_cases, rejected_cases);
    return 0;
}

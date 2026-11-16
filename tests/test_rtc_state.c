/**
 * @file    test_rtc_state.c
 * @brief   Clock, divider, and motor history in portable emulator snapshots.
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
#include "../src/gb/vram_bus.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/infrared.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/printer.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { INPUT_SAMPLE_BYTES = 1 };

enum {
    HEADER_BYTES = 128,
    V2_BYTES = 300518,
    V3_BYTES = 300568,
    V4_BYTES = 300572,
    SGB_SECTION_BYTES = 10119,
    TPP1_SECTION_BYTES = 21,
    HUC3_SECTION_BYTES = 25,
    V11_TAIL_BYTES = 28,
    INFRARED_BYTES = 6,
    RTC_HISTORY_BYTES = 6,
    MOTOR_HISTORY_OFFSET = 4,
    DIVIDER_HISTORY_OFFSET = 5,
    DESCRIPTOR_BYTES = 8,
    PRINTER_BYTES = 32687,
    PRINTER_INCOMING_BITS = 32660
};

typedef struct Host {
    int64_t now;
    unsigned clocks;
    unsigned serial;
    unsigned launches;
    unsigned samples;
    unsigned rumble;
    unsigned infrared;
    unsigned images;
    unsigned done;
} Host;

typedef struct Rig {
    CupidGb *gb;
    CupidGbStateContext context;
    CupidGbPrinter *printer;
    Host host;
} Rig;

typedef struct Blob {
    uint8_t *bytes;
    size_t length;
} Blob;

static unsigned restored_cases;
static unsigned legacy_cases;
static unsigned rejected_cases;
static unsigned continued_cases;

static uint32_t read_u32(const uint8_t *bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= (uint32_t)bytes[i] << (8 * i);
    return value;
}

static void write_u32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = (uint8_t)(value >> (8 * i));
}

static void resign(Blob *blob) {
    write_u32(blob->bytes + 12, (uint32_t)blob->length);
    cupid_sha256_parts(blob->bytes, 96, blob->bytes + HEADER_BYTES, blob->length - HEADER_BYTES,
                       blob->bytes + 96);
}

static int64_t observe_time(void *user_data) {
    Host *host = user_data;
    ++host->clocks;
    return host->now;
}

static void observe_serial(const CupidGb *gb, uint8_t value, void *user_data) {
    (void)gb;
    (void)value;
    ++((Host *)user_data)->serial;
}

static void observe_launch(const CupidGb *gb, bool output, void *user_data) {
    (void)gb;
    (void)output;
    ++((Host *)user_data)->launches;
}

static bool observe_sample(const CupidGb *gb, void *user_data) {
    (void)gb;
    ++((Host *)user_data)->samples;
    return true;
}

static void observe_rumble(const CupidGb *gb, bool output, uint64_t cycles, void *user_data) {
    (void)gb;
    (void)output;
    (void)cycles;
    ++((Host *)user_data)->rumble;
}

static void observe_infrared(const CupidGb *gb, bool output, uint64_t cycles, void *user_data) {
    (void)gb;
    (void)output;
    (void)cycles;
    ++((Host *)user_data)->infrared;
}

static void observe_image(const CupidGbPrinterImage *image, void *user_data) {
    (void)image;
    ++((Host *)user_data)->images;
}

static void observe_done(void *user_data) {
    ++((Host *)user_data)->done;
}

static bool is_sgb(const CupidGb *gb) {
    return gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2;
}

static Rig *create_rig(CupidGbModel model, uint8_t type, CupidGbRtcMode mode) {
    Rig *rig = calloc(1, sizeof(*rig));
    assert(rig);
    rig->gb = calloc(1, sizeof(*rig->gb));
    assert(rig->gb);
    rig->host.now = INT64_C(1800000000);
    cupid_gb_set_model(rig->gb, model);
    cupid_gb_set_power_on_seed(rig->gb, UINT64_C(0x5254432d53544154));
    cupid_gb_set_rtc_time_source(rig->gb, observe_time, &rig->host);
    assert(cupid_gb_set_rtc_mode(rig->gb, mode));
    uint8_t rom[0x8000] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    memcpy(rom + 0x134, "RTC STATE", 9);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x146] = 3;
    rom[0x147] = type;
    rom[0x149] = type ? 2 : 0;
    if (type == 0xbc) {
        rom[0x149] = 0xc1;
        rom[0x14a] = 0x65;
        rom[0x152] = 3;
        rom[0x153] = 8;
    }
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    assert(cupid_gb_load_rom(rig->gb, rom, sizeof(rom)));
    assert(cupid_gb_get_rtc_mode(rig->gb) == mode);
    cupid_gb_write_u8(rig->gb, 0xff40, 0);
    cupid_gb_write_u8(rig->gb, 0xff26, 0);
    cupid_gb_set_serial_output_callback(rig->gb, observe_serial, &rig->host);
    cupid_gb_set_serial_link(rig->gb, (CupidGbSerialLink){.launch = observe_launch,
                                                          .sample = observe_sample,
                                                          .user_data = &rig->host});
    cupid_gb_set_rumble_callback(rig->gb, observe_rumble, &rig->host);
    cupid_gb_set_infrared_callback(rig->gb, observe_infrared, &rig->host);
    rig->gb->battery_write_generation = 123;
    rig->gb->vblank_events = 456;
    strcpy(rig->gb->save_path, "retained-clock.sav");
    assert(cupid_gb_state_context_init(&rig->context, rig->gb) == CUPID_GB_STATE_OK);
    rig->host.clocks = 0;
    return rig;
}

static void attach_printer(Rig *rig) {
    assert(!rig->printer);
    rig->printer = cupid_gb_printer_create(observe_image, observe_done, &rig->host);
    assert(rig->printer);
    cupid_gb_set_serial_link(rig->gb, cupid_gb_printer_link(rig->printer));
}

static void destroy_rig(Rig *rig) {
    cupid_gb_set_serial_link(rig->gb, (CupidGbSerialLink){0});
    if (rig->printer)
        cupid_gb_printer_destroy(rig->printer);
    cupid_gb_cleanup(rig->gb);
    free(rig->gb);
    free(rig);
}

static Blob save_rig(const Rig *rig, bool trusted) {
    Blob blob = {NULL, cupid_gb_state_size(rig->gb)};
    blob.bytes = malloc(blob.length);
    assert(blob.bytes);
    size_t written = 0;
    const Host before = rig->host;
    CupidGbStateResult result =
        trusted ? cupid_gb_save_state_with_context(rig->gb, blob.bytes, blob.length, &written,
                                                   &rig->context)
                : cupid_gb_save_state(rig->gb, blob.bytes, blob.length, &written);
    assert(result == CUPID_GB_STATE_OK && written == blob.length);
    assert(read_u32(blob.bytes + 8) == CUPID_GB_STATE_VERSION);
    assert(memcmp(&before, &rig->host, sizeof(before)) == 0);
    return blob;
}

static CupidGbStateResult load_rig(Rig *rig, const Blob *blob, bool trusted) {
    const Host before = rig->host;
    const CupidGbRtcPolicy policy = rig->gb->rtc_policy;
    const CupidGbPowerOn power = rig->gb->power_on;
    const CupidGbSerialLink link = rig->gb->serial_link;
    const bool infrared_input = rig->gb->huc3_ir_input;
    const int16_t motion_x = rig->gb->mbc7.motion_x, motion_y = rig->gb->mbc7.motion_y;
    const uint8_t *rom = rig->gb->rom;
    const uint64_t dirty = rig->gb->battery_write_generation, events = rig->gb->vblank_events;
    CupidGbStateResult result =
        trusted
            ? cupid_gb_load_state_with_context(rig->gb, blob->bytes, blob->length, &rig->context)
            : cupid_gb_load_state(rig->gb, blob->bytes, blob->length);
    assert(memcmp(&before, &rig->host, sizeof(before)) == 0);
    assert(rig->gb->rtc_policy.mode == policy.mode &&
           rig->gb->rtc_policy.second_length == policy.second_length &&
           rig->gb->rtc_policy.time_source == policy.time_source &&
           rig->gb->rtc_policy.time_user_data == policy.time_user_data);
    assert(rig->gb->power_on.seed == power.seed &&
           rig->gb->power_on.generation_seed == power.generation_seed &&
           rig->gb->power_on.fixed_seed == power.fixed_seed);
    assert(rig->gb->serial_link.launch == link.launch &&
           rig->gb->serial_link.sample == link.sample &&
           rig->gb->serial_link.user_data == link.user_data &&
           rig->gb->serial_link.device == link.device);
    assert(rig->gb->serial_output_callback == observe_serial &&
           rig->gb->serial_output_user_data == &rig->host);
    assert(rig->gb->rumble_callback == observe_rumble && rig->gb->rumble_user_data == &rig->host);
    assert(rig->gb->infrared_callback == observe_infrared &&
           rig->gb->infrared_user_data == &rig->host);
    assert(rig->gb->huc3_ir_input == infrared_input && rig->gb->mbc7.motion_x == motion_x &&
           rig->gb->mbc7.motion_y == motion_y);
    assert(rig->gb->rom == rom && rig->gb->battery_write_generation == dirty &&
           rig->gb->vblank_events == events &&
           strcmp(rig->gb->save_path, "retained-clock.sav") == 0);
    return result;
}

static size_t descriptor_offset(const Rig *rig, const Blob *blob) {
    const size_t offset = blob->length - DESCRIPTOR_BYTES - (rig->printer ? PRINTER_BYTES : 0);
    assert(offset > HEADER_BYTES + RTC_HISTORY_BYTES);
    assert(blob->bytes[offset] == (rig->printer ? 1 : 0));
    return offset;
}

static size_t expected_size(const Rig *rig) {
    const CupidGb *gb = rig->gb;
    const size_t mapper = gb->header.mbc_type == CUPID_GB_TPP1   ? TPP1_SECTION_BYTES
                          : gb->header.mbc_type == CUPID_GB_HUC3 ? HUC3_SECTION_BYTES
                                                                 : 0;
    const size_t border = (size_t)(is_sgb(gb) ? SGB_SECTION_BYTES : 0);
    const size_t infrared = (size_t)(gb->header.mbc_type == CUPID_GB_HUC3 ? 0 : INFRARED_BYTES);
    const size_t accessory = (size_t)(rig->printer ? PRINTER_BYTES : 0);
    return INPUT_SAMPLE_BYTES + V4_BYTES + border + mapper + V11_TAIL_BYTES + infrared +
           RTC_HISTORY_BYTES + CUPID_GB_AUDIO_STATE_SIZE + CUPID_GB_DISPLAY_CLOCK_STATE_SIZE +
           CUPID_GB_PIXEL_TIMING_STATE_SIZE + CUPID_GB_VRAM_BUS_STATE_SIZE + DESCRIPTOR_BYTES +
           accessory;
}

static Blob copy_blob(const Blob *source, size_t length) {
    Blob copy = {calloc(length, 1), length};
    assert(copy.bytes);
    memcpy(copy.bytes, source->bytes, length < source->length ? length : source->length);
    return copy;
}

static Blob legacy_blob(const Rig *rig, const Blob *current, unsigned version) {
    assert(version >= 2 && version <= 13);
    const size_t descriptor = descriptor_offset(rig, current);
    Blob legacy;
    if (version >= 12) {
        legacy = copy_blob(current, current->length);
        const size_t removed = INPUT_SAMPLE_BYTES + CUPID_GB_AUDIO_STATE_SIZE +
                               CUPID_GB_DISPLAY_CLOCK_STATE_SIZE +
                               CUPID_GB_PIXEL_TIMING_STATE_SIZE + CUPID_GB_VRAM_BUS_STATE_SIZE +
                               (version == 12 ? RTC_HISTORY_BYTES : 0);
        /* The entire accessory tail moves, including a nonempty printer payload. */
        memmove(legacy.bytes + descriptor - removed, legacy.bytes + descriptor,
                current->length - descriptor);
        legacy.length -= removed;
    } else {
        const size_t v11 = descriptor - INPUT_SAMPLE_BYTES - CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
                           CUPID_GB_PIXEL_TIMING_STATE_SIZE - CUPID_GB_VRAM_BUS_STATE_SIZE -
                           CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES -
                           (rig->gb->header.mbc_type == CUPID_GB_HUC3 ? 0 : INFRARED_BYTES);
        static const size_t tails[] = {0, 4, 5, 16, 22, 26, 28};
        const size_t length = version == 2   ? V2_BYTES
                              : version == 3 ? V3_BYTES
                              : version == 4 ? V4_BYTES
                                             : v11 - V11_TAIL_BYTES + tails[version - 5];
        legacy = copy_blob(current, length);
    }
    write_u32(legacy.bytes + 8, version);
    resign(&legacy);
    return legacy;
}

static void assert_equal(const Rig *a, const Rig *b) {
    Blob left = save_rig(a, false), right = save_rig(b, true);
    assert(left.length == right.length && memcmp(left.bytes, right.bytes, left.length) == 0);
    free(left.bytes);
    free(right.bytes);
}

static void fill_clock_history(CupidGb *gb) {
    gb->rtc = (CupidGbRtc){.s = 251,
                           .m = 63,
                           .h = 25,
                           .dl = 0x91,
                           .dh = 0x80,
                           .ls = 19,
                           .lm = 29,
                           .lh = 3,
                           .ldl = 0x42,
                           .ldh = 1,
                           .latch_prev = 7,
                           .base_time = 0};
    gb->tpp1.rtc_base_time = 0;
    gb->tpp1.status = 0x0b;
    const uint8_t live[] = {93, 0xe7, 63, 63}, latched[] = {17, 0x82, 43, 21};
    memcpy(gb->tpp1.live, live, sizeof(live));
    memcpy(gb->tpp1.latched, latched, sizeof(latched));
    gb->huc3.rtc_base_time = 0;
    gb->huc3.minutes = 0x0fed;
    gb->huc3.days = 0x4321;
    gb->huc3.alarm_minutes = 0x0abc;
    gb->huc3.alarm_days = 0x1234;
    gb->huc3.ir_warmup = 3;
    gb->huc3.ir_decay = 7;
    gb->huc3.ir_output = true;
    gb->huc3.ir_detected = true;
}

static void test_history_roundtrip(void) {
    static const uint8_t types[] = {0, 0x10, 0x13, 0xfe, 0xbc};
    static const uint32_t phases[] = {0, 1, 262143, 8388607, 16777233, UINT32_MAX};
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned type = 0; type < sizeof(types); ++type) {
            for (unsigned mode = 0; mode < 2; ++mode) {
                for (unsigned api = 0; api < 2; ++api) {
                    Rig *source =
                        create_rig((CupidGbModel)model, types[type], (CupidGbRtcMode)mode);
                    Rig *target =
                        create_rig((CupidGbModel)model, types[type], (CupidGbRtcMode)!mode);
                    fill_clock_history(source->gb);
                    const uint32_t phase = phases[(model + type + mode + api) % 6];
                    source->gb->rtc_cycles = phase;
                    source->gb->divider_subcycle = (uint8_t)((model + type + mode + api) & 3u);
                    source->gb->timer_counter = UINT16_C(0xa55a);
                    source->gb->tpp1.motor_strength =
                        types[type] == 0xbc ? (uint8_t)((model + mode + api) & 3u) : 255;
                    target->gb->rtc_cycles = phase ^ UINT32_MAX;
                    target->gb->divider_subcycle = (uint8_t)(source->gb->divider_subcycle ^ 3u);
                    target->gb->timer_counter = UINT16_C(0x1234);
                    target->gb->tpp1.motor_strength = 2;
                    assert(cupid_gb_set_rtc_multiplier(target->gb, 0.5));
                    cupid_gb_set_power_on_seed(target->gb, UINT64_MAX);
                    target->gb->huc3_ir_input = true;
                    target->gb->mbc7.motion_x = -321;
                    target->gb->mbc7.motion_y = 123;
                    Blob saved = save_rig(source, api != 0);
                    assert(saved.length == expected_size(source));
                    const size_t rtc = descriptor_offset(source, &saved) - INPUT_SAMPLE_BYTES -
                                       CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
                                       CUPID_GB_PIXEL_TIMING_STATE_SIZE -
                                       CUPID_GB_VRAM_BUS_STATE_SIZE - CUPID_GB_AUDIO_STATE_SIZE -
                                       RTC_HISTORY_BYTES;
                    assert(read_u32(saved.bytes + rtc) == phase);
                    const uint8_t motor = types[type] == 0xbc ? source->gb->tpp1.motor_strength : 0;
                    assert(saved.bytes[rtc + MOTOR_HISTORY_OFFSET] == motor);
                    assert(saved.bytes[rtc + DIVIDER_HISTORY_OFFSET] ==
                           source->gb->divider_subcycle);
                    if (types[type] != 0xbc)
                        assert(source->gb->tpp1.motor_strength == 255);
                    assert(load_rig(target, &saved, api != 0) == CUPID_GB_STATE_OK);
                    assert(target->gb->rtc_cycles == phase &&
                           target->gb->tpp1.motor_strength == motor);
                    assert(target->gb->divider_subcycle == source->gb->divider_subcycle &&
                           target->gb->timer_counter == UINT16_C(0xa55a));
                    assert(target->gb->rtc.base_time == 0 && target->gb->rtc.s == 251 &&
                           target->gb->rtc.ls == 19 && target->gb->rtc.latch_prev == 7);
                    if (types[type] == 0xbc)
                        assert(target->gb->tpp1.status == 0x0b &&
                               target->gb->tpp1.rtc_base_time == 0);
                    if (types[type] == 0xfe)
                        assert(target->gb->huc3.rtc_base_time == 0 &&
                               target->gb->huc3.minutes == 0x0fed);
                    assert(target->gb->huc3.ir_warmup == 3 && target->gb->huc3.ir_decay == 7 &&
                           target->gb->huc3.ir_output && target->gb->huc3.ir_detected);
                    assert_equal(source, target);
                    ++restored_cases;
                    free(saved.bytes);
                    destroy_rig(source);
                    destroy_rig(target);
                }
            }
        }
    }
}

static void test_legacy_defaults(void) {
    static const uint8_t types[] = {0, 0x10, 0xfe, 0xbc};
    for (unsigned type = 0; type < sizeof(types); ++type) {
        Rig *source = create_rig(CUPID_GB_MODEL_SGB, types[type], CUPID_GB_RTC_ACCURATE);
        fill_clock_history(source->gb);
        source->gb->rtc_cycles = UINT32_C(0x76543210);
        source->gb->divider_subcycle = 2;
        source->gb->timer_counter = UINT16_C(0xbeef);
        source->gb->tpp1.motor_strength = 1;
        Blob current = save_rig(source, false);
        for (unsigned version = 2; version <= 12; ++version) {
            Blob legacy = legacy_blob(source, &current, version);
            for (unsigned api = 0; api < 2; ++api) {
                Rig *target =
                    create_rig(CUPID_GB_MODEL_SGB, types[type], CUPID_GB_RTC_SYNC_TO_HOST);
                target->gb->rtc_cycles = UINT32_MAX;
                target->gb->divider_subcycle = 3;
                target->gb->timer_counter = UINT16_C(0x1234);
                target->gb->tpp1.status = 7;
                target->gb->tpp1.motor_strength = 2;
                target->gb->huc3.ir_warmup = 1;
                target->gb->huc3.ir_decay = 2;
                target->gb->huc3_ir_input = true;
                assert(load_rig(target, &legacy, api != 0) == CUPID_GB_STATE_OK);
                assert(target->gb->rtc_cycles == 0);
                assert(target->gb->divider_subcycle == 0 &&
                       target->gb->timer_counter == UINT16_C(0xbeef));
                assert(target->gb->tpp1.motor_strength ==
                       (types[type] == 0xbc && version >= 5 ? 3 : 0));
                assert(target->gb->rtc.base_time == 0);
                if (types[type] == 0xbc)
                    assert(target->gb->tpp1.status == (version >= 5 ? 0x0b : 0) &&
                           target->gb->tpp1.rtc_base_time == 0);
                if (types[type] == 0xfe)
                    assert(target->gb->huc3.rtc_base_time == 0);
                const bool infrared = version >= 12 || (types[type] == 0xfe && version >= 5);
                assert(target->gb->huc3.ir_warmup == (infrared ? 3 : 0) &&
                       target->gb->huc3.ir_decay == (infrared ? 7 : 0) &&
                       target->gb->huc3.ir_output == infrared &&
                       target->gb->huc3.ir_detected == infrared);
                Blob resaved = save_rig(target, true);
                const size_t history =
                    descriptor_offset(target, &resaved) - INPUT_SAMPLE_BYTES -
                    CUPID_GB_DISPLAY_CLOCK_STATE_SIZE - CUPID_GB_PIXEL_TIMING_STATE_SIZE -
                    CUPID_GB_VRAM_BUS_STATE_SIZE - CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES;
                assert(resaved.bytes[history + MOTOR_HISTORY_OFFSET] ==
                       target->gb->tpp1.motor_strength);
                assert(resaved.bytes[history + DIVIDER_HISTORY_OFFSET] == 0);
                free(resaved.bytes);
                ++legacy_cases;
                destroy_rig(target);
            }
            free(legacy.bytes);
        }
        free(current.bytes);
        destroy_rig(source);
    }
}

static void reject_unchanged(Rig *target, const Blob *bad, bool trusted) {
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    *before = *target->gb;
    const CupidGbStateContext context = target->context;
    Blob saved = save_rig(target, true);
    assert(load_rig(target, bad, trusted) == CUPID_GB_STATE_INVALID);
    assert(memcmp(before, target->gb, sizeof(*before)) == 0);
    assert(memcmp(&context, &target->context, sizeof(context)) == 0);
    Blob after = save_rig(target, false);
    assert(saved.length == after.length && memcmp(saved.bytes, after.bytes, saved.length) == 0);
    free(saved.bytes);
    free(after.bytes);
    free(before);
    ++rejected_cases;
}

static void receive_prefix(Rig *rig, uint8_t value) {
    cupid_gb_write_u8(rig->gb, 0xff01, value);
    cupid_gb_write_u8(rig->gb, 0xff02, 0x81);
    unsigned guard = 0;
    while (rig->gb->serial_bits_remaining > 5 && guard++ < 2048)
        cupid_gb_tick(rig->gb, 1);
    assert(rig->gb->serial_bits_remaining == 5);
    cupid_gb_printer_update(rig->printer, rig->gb);
}

static void reject_divider_save_unchanged(Rig *source, uint8_t phase, bool trusted) {
    Blob before = save_rig(source, false);
    const uint8_t valid_phase = source->gb->divider_subcycle;
    source->gb->divider_subcycle = phase;
    CupidGb *machine = malloc(sizeof(*machine));
    uint8_t *output = malloc(before.length);
    assert(machine && output);
    *machine = *source->gb;
    memset(output, 0xa5, before.length);
    const Host host = source->host;
    const CupidGbStateContext context = source->context;
    size_t written = 99;
    const CupidGbStateResult result =
        trusted ? cupid_gb_save_state_with_context(source->gb, output, before.length, &written,
                                                   &source->context)
                : cupid_gb_save_state(source->gb, output, before.length, &written);
    assert(result == CUPID_GB_STATE_INVALID && written == 0);
    assert(memcmp(machine, source->gb, sizeof(*machine)) == 0);
    assert(memcmp(&host, &source->host, sizeof(host)) == 0);
    assert(memcmp(&context, &source->context, sizeof(context)) == 0);
    for (size_t i = 0; i < before.length; ++i)
        assert(output[i] == 0xa5);
    source->gb->divider_subcycle = valid_phase;
    Blob after = save_rig(source, true);
    assert(before.length == after.length && memcmp(before.bytes, after.bytes, before.length) == 0);
    free(after.bytes);
    free(before.bytes);
    free(output);
    free(machine);
    ++rejected_cases;
}

static void test_transactional_rejection(void) {
    static const uint8_t types[] = {0, 0x10, 0xfe, 0xbc};
    for (unsigned type = 0; type < sizeof(types); ++type) {
        Rig *source = create_rig(CUPID_GB_MODEL_CGB, types[type], CUPID_GB_RTC_ACCURATE);
        Rig *target = create_rig(CUPID_GB_MODEL_CGB, types[type], CUPID_GB_RTC_SYNC_TO_HOST);
        attach_printer(source);
        attach_printer(target);
        receive_prefix(source, 0x88);
        receive_prefix(target, 0x33);
        source->gb->rtc_cycles = UINT32_MAX;
        target->gb->rtc_cycles = 123;
        source->gb->divider_subcycle = 2;
        target->gb->divider_subcycle = 3;
        Blob valid = save_rig(source, false);
        const size_t descriptor = descriptor_offset(source, &valid);
        for (unsigned api = 0; api < 2; ++api) {
            for (unsigned scenario = 0; scenario < 8; ++scenario) {
                Blob bad = copy_blob(&valid, valid.length + (scenario == 4) - (scenario == 5));
                if (scenario < 2)
                    bad.bytes[descriptor - INPUT_SAMPLE_BYTES - CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
                              CUPID_GB_PIXEL_TIMING_STATE_SIZE - CUPID_GB_VRAM_BUS_STATE_SIZE -
                              CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES +
                              MOTOR_HISTORY_OFFSET] =
                        scenario ? 255 : (types[type] == 0xbc ? 4 : 1);
                else if (scenario == 2) {
                    const size_t ir_boolean =
                        types[type] == 0xfe
                            ? V4_BYTES + HUC3_SECTION_BYTES - 1
                            : descriptor - INPUT_SAMPLE_BYTES - CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
                                  CUPID_GB_PIXEL_TIMING_STATE_SIZE - CUPID_GB_VRAM_BUS_STATE_SIZE -
                                  CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES - 1;
                    bad.bytes[ir_boolean] = 2;
                } else if (scenario == 3)
                    bad.bytes[descriptor + 2] = 1;
                else if (scenario >= 6)
                    bad.bytes[descriptor - INPUT_SAMPLE_BYTES - CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
                              CUPID_GB_PIXEL_TIMING_STATE_SIZE - CUPID_GB_VRAM_BUS_STATE_SIZE -
                              CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES +
                              DIVIDER_HISTORY_OFFSET] = scenario == 6 ? 4 : 255;
                resign(&bad);
                reject_unchanged(target, &bad, api != 0);
                free(bad.bytes);
            }
            Blob missing = legacy_blob(source, &valid, 12);
            write_u32(missing.bytes + 8, 13);
            resign(&missing);
            reject_unchanged(target, &missing, api != 0);
            free(missing.bytes);
            reject_divider_save_unchanged(source, 4, api != 0);
            reject_divider_save_unchanged(source, 255, api != 0);
        }
        if (types[type] == 0xbc) {
            source->gb->tpp1.motor_strength = 4;
            size_t written = 99;
            memset(valid.bytes, 0xa5, valid.length);
            assert(cupid_gb_save_state(source->gb, valid.bytes, valid.length, &written) ==
                   CUPID_GB_STATE_INVALID);
            assert(written == 0);
            for (size_t i = 0; i < valid.length; ++i)
                assert(valid.bytes[i] == 0xa5);
        }
        free(valid.bytes);
        destroy_rig(source);
        destroy_rig(target);
    }
}

static void test_timestamp_domains(void) {
    static const int64_t baselines[] = {0, -1, -128, INT64_MIN, INT64_MAX};
    Rig *source = create_rig(CUPID_GB_MODEL_CGB, 0x10, CUPID_GB_RTC_ACCURATE);
    Rig *target = create_rig(CUPID_GB_MODEL_CGB, 0x10, CUPID_GB_RTC_SYNC_TO_HOST);
    for (unsigned i = 0; i < sizeof(baselines) / sizeof(baselines[0]); ++i) {
        source->gb->rtc.base_time = baselines[i];
        source->gb->rtc_cycles = UINT32_MAX;
        Blob current = save_rig(source, false);
        Blob old = legacy_blob(source, &current, 12);
        for (unsigned api = 0; api < 2; ++api) {
            uint8_t *storage = malloc(current.length + 1);
            assert(storage);
            memcpy(storage + 1, current.bytes, current.length);
            const Blob unaligned = {storage + 1, current.length};
            assert(load_rig(target, &unaligned, api != 0) == CUPID_GB_STATE_OK);
            assert(target->gb->rtc.base_time == baselines[i] &&
                   target->gb->rtc_cycles == UINT32_MAX);
            assert(load_rig(target, &old, api != 0) == CUPID_GB_STATE_OK);
            assert(target->gb->rtc.base_time == baselines[i] && target->gb->rtc_cycles == 0);
            free(storage);
        }
        free(current.bytes);
        free(old.bytes);
    }
    destroy_rig(source);
    destroy_rig(target);
    static const uint8_t types[] = {0xfe, 0xbc};
    for (unsigned type = 0; type < sizeof(types); ++type) {
        source = create_rig(CUPID_GB_MODEL_CGB, types[type], CUPID_GB_RTC_ACCURATE);
        target = create_rig(CUPID_GB_MODEL_CGB, types[type], CUPID_GB_RTC_SYNC_TO_HOST);
        Blob current = save_rig(source, false);
        Blob old = legacy_blob(source, &current, 12);
        const size_t timestamp = V4_BYTES + (types[type] == 0xbc ? 13 : 0);
        memset(current.bytes + timestamp, 0xff, 8);
        memset(old.bytes + timestamp, 0xff, 8);
        resign(&current);
        resign(&old);
        for (unsigned api = 0; api < 2; ++api) {
            reject_unchanged(target, &current, api != 0);
            reject_unchanged(target, &old, api != 0);
        }
        free(current.bytes);
        free(old.bytes);
        destroy_rig(source);
        destroy_rig(target);
    }
}

static void test_v12_printer_tail(void) {
    for (unsigned api = 0; api < 2; ++api) {
        Rig *source = create_rig(CUPID_GB_MODEL_CGB, 0xbc, CUPID_GB_RTC_ACCURATE);
        Rig *target = create_rig(CUPID_GB_MODEL_CGB, 0xbc, CUPID_GB_RTC_SYNC_TO_HOST);
        attach_printer(source);
        attach_printer(target);
        receive_prefix(source, 0x88);
        receive_prefix(target, 0x33);
        source->gb->tpp1.status = 0x0a;
        source->gb->tpp1.motor_strength = 1;
        source->gb->rtc_cycles = UINT32_C(0x12345678);
        source->gb->divider_subcycle = 2;
        target->gb->divider_subcycle = 3;
        Blob current = save_rig(source, false);
        const size_t descriptor = descriptor_offset(source, &current);
        /* SC launches the first bit before any sample. Three console samples
         * therefore leave four bits captured by the printer's launch callback. */
        assert(current.bytes[descriptor + DESCRIPTOR_BYTES + PRINTER_INCOMING_BITS] == 4);
        Blob legacy = legacy_blob(source, &current, 12);
        assert(memcmp(legacy.bytes + descriptor - INPUT_SAMPLE_BYTES -
                          CUPID_GB_DISPLAY_CLOCK_STATE_SIZE - CUPID_GB_PIXEL_TIMING_STATE_SIZE -
                          CUPID_GB_VRAM_BUS_STATE_SIZE - CUPID_GB_AUDIO_STATE_SIZE -
                          RTC_HISTORY_BYTES,
                      current.bytes + descriptor, DESCRIPTOR_BYTES + PRINTER_BYTES) == 0);
        assert(load_rig(target, &legacy, api != 0) == CUPID_GB_STATE_OK);
        assert(target->gb->rtc_cycles == 0 && target->gb->tpp1.status == 0x0a &&
               target->gb->tpp1.motor_strength == 2 && target->gb->divider_subcycle == 0);
        source->gb->rtc_cycles = 0;
        source->gb->divider_subcycle = 0;
        source->gb->tpp1.motor_strength = 2;
        cupid_gb_apu_import_monochrome_noise(source->gb);
        cupid_gb_audio_import_legacy(source->gb);
        cupid_gb_pixels_import_legacy(source->gb);
        cupid_gb_display_clock_import_legacy(source->gb);
        cupid_gb_vram_bus_import_legacy(source->gb);
        cupid_gb_input_import_legacy(source->gb);
        assert_equal(source, target);
        free(current.bytes);
        free(legacy.bytes);
        destroy_rig(source);
        destroy_rig(target);
    }
}

static void test_clock_continuation(void) {
    static const uint8_t types[] = {0x10, 0xfe, 0xbc};
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_CGB,
                                   CUPID_GB_MODEL_SGB};
    for (unsigned configuration = 0; configuration < 4; ++configuration) {
        for (unsigned type = 0; type < sizeof(types); ++type) {
            for (unsigned mode = 0; mode < 2; ++mode) {
                for (unsigned api = 0; api < 2; ++api) {
                    Rig *source =
                        create_rig(models[configuration], types[type], (CupidGbRtcMode)mode);
                    Rig *target =
                        create_rig(models[configuration], types[type], (CupidGbRtcMode)mode);
                    source->gb->double_speed = target->gb->double_speed = configuration == 2;
                    source->host.now = target->host.now = INT64_C(1800000001);
                    source->gb->rtc.base_time = INT64_C(1800000000);
                    source->gb->rtc.s = 17;
                    source->gb->tpp1.rtc_base_time = INT64_C(1800000000);
                    source->gb->tpp1.live[3] = 17;
                    source->gb->tpp1.status = 5;
                    source->gb->tpp1.motor_strength = 2;
                    source->gb->huc3.rtc_base_time = INT64_C(1799999999);
                    source->gb->huc3.minutes = 7;
                    const uint32_t period = mode ? 2 * cupid_gb_base_clock_hz(source->gb)
                                                 : cupid_gb_base_clock_hz(source->gb) / 16;
                    const uint32_t tick = configuration == 2 ? 4 : 8;
                    source->gb->rtc_cycles = period - tick;
                    Blob saved = save_rig(source, api != 0);
                    assert(load_rig(target, &saved, api != 0) == CUPID_GB_STATE_OK);
                    assert(source->host.clocks == 0 && target->host.clocks == 0);
                    for (unsigned step = 0; step < 16; ++step) {
                        cupid_gb_tick(source->gb, 1);
                        cupid_gb_tick(target->gb, 1);
                        if (step == 0) {
                            assert(source->gb->rtc_cycles == 0 && target->gb->rtc_cycles == 0);
                            if (types[type] == 0x10)
                                assert(target->gb->rtc.s == 18);
                            else if (types[type] == 0xbc)
                                assert(target->gb->tpp1.live[3] == 18);
                            else
                                assert(target->gb->huc3.minutes == 8);
                        }
                        if (step == 0 || step == 1 || step == 15)
                            assert_equal(source, target);
                    }
                    assert(source->host.clocks == (mode ? 0u : 1u));
                    assert(target->host.clocks == source->host.clocks);
                    free(saved.bytes);
                    ++continued_cases;
                    destroy_rig(source);
                    destroy_rig(target);
                }
            }
        }
    }
}

static void test_divider_continuation(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned accessory = 0; accessory < 2; ++accessory) {
            for (unsigned api = 0; api < 2; ++api) {
                Rig *source = create_rig(CUPID_GB_MODEL_CGB, 0x10, CUPID_GB_RTC_ACCURATE);
                Rig *target = create_rig(CUPID_GB_MODEL_CGB, 0x10, CUPID_GB_RTC_ACCURATE);
                if (accessory) {
                    attach_printer(source);
                    attach_printer(target);
                }
                source->gb->double_speed = fast != 0;
                cupid_gb_write_u8(source->gb, 0xff04, 0);
                cupid_gb_write_u8(source->gb, 0xff05, 0x7c);
                cupid_gb_write_u8(source->gb, 0xff07, 5);
                source->gb->serial_master_clock = false;
                cupid_gb_write_u8(source->gb, 0xff01, 0x3c);
                cupid_gb_write_u8(source->gb, 0xff02, 0x83);
                /* The snapshot begins halfway to the next divider event. The
                 * JR loop crosses timer and fast serial edges without resetting it. */
                source->gb->div_counter = 3;
                source->gb->divider_subcycle = 2;
                source->gb->timer_counter = UINT16_C(0xa55a);
                target->gb->divider_subcycle = 3;
                target->gb->timer_counter = UINT16_C(0x1234);
                Blob saved = save_rig(source, api != 0);
                assert(load_rig(target, &saved, api != 0) == CUPID_GB_STATE_OK);
                assert(target->gb->divider_subcycle == 2 &&
                       target->gb->timer_counter == UINT16_C(0xa55a));
                assert_equal(source, target);
                for (unsigned step = 0; step < 32; ++step) {
                    CupidGbStepTrace a = {0}, b = {0};
                    const unsigned source_samples = source->host.samples;
                    const unsigned target_samples = target->host.samples;
                    assert(cupid_gb_step_observed(source->gb, &a));
                    assert(cupid_gb_step_observed(target->gb, &b));
                    assert(a.event == CUPID_GB_STEP_INSTRUCTION && a.opcode == 0x18 &&
                           a.address == 0x100 && a.event == b.event && a.opcode == b.opcode &&
                           a.address == b.address);
                    assert(source->gb->divider_subcycle == 2 && target->gb->divider_subcycle == 2);
                    assert(source->host.samples - source_samples ==
                           target->host.samples - target_samples);
                    if (step == 0) {
                        assert(source->gb->div_counter == 6 && target->gb->div_counter == 6);
                        assert(source->gb->io_registers[5] == 0x7d &&
                               target->gb->io_registers[5] == 0x7d);
                        if (!accessory)
                            assert(source->gb->io_registers[1] == 0x79 &&
                                   target->gb->io_registers[1] == 0x79);
                    }
                    if (step < 2 || step == 15 || step == 31)
                        assert_equal(source, target);
                }
                assert(source->host.clocks == 0 && target->host.clocks == 0);
                free(saved.bytes);
                ++continued_cases;
                destroy_rig(source);
                destroy_rig(target);
            }
        }
    }
}

int main(void) {
    test_history_roundtrip();
    test_legacy_defaults();
    test_transactional_rejection();
    test_timestamp_domains();
    test_v12_printer_tail();
    test_clock_continuation();
    test_divider_continuation();
    printf("RTC state checks: %u roundtrips, %u legacy imports, %u transactional rejections, "
           "%u clock continuations.\n",
           restored_cases, legacy_cases, rejected_cases, continued_cases);
    return 0;
}

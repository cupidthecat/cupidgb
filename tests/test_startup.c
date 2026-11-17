/**
 * @file    test_startup.c
 * @brief   Regression tests for seeded cold memory and initial firmware entry.
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

#include "../src/gb/audio_state.h"
#include "../src/gb/display_clock.h"
#include "../src/gb/state_startup.h"
#include "../src/gb/vram_bus.h"
#include "cupid/common/sha256.h"
#include "cupid/core/emulator.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/infrared.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/printer.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sampled JOYP, serial selection and the CH3 fetch byte precede the SRAM length. */
enum { IO_HISTORY_BYTES = 3, SRAM_LENGTH_BYTES = 4 };

typedef struct MemoryImage {
    uint8_t work[0x8000];
    uint8_t video[0x4000];
    uint8_t high[0x7f];
    uint8_t oam[0xa0];
    uint8_t wave[16];
    uint8_t background[64];
    uint8_t object[64];
} MemoryImage;

typedef struct BitCounts {
    uint64_t bytes;
    uint64_t ones[8];
    bool values[256];
} BitCounts;

static void capture(const CupidGb *gb, MemoryImage *image) {
    memcpy(image->work, gb->work_ram, sizeof(image->work));
    memcpy(image->video, gb->video_ram, sizeof(image->video));
    memcpy(image->high, gb->high_ram, sizeof(image->high));
    memcpy(image->oam, gb->object_attribute_memory, sizeof(image->oam));
    memcpy(image->wave, gb->io_registers + 0x30, sizeof(image->wave));
    memcpy(image->background, gb->cgb_bg_palette_ram, sizeof(image->background));
    memcpy(image->object, gb->cgb_obj_palette_ram, sizeof(image->object));
}

static void count_byte(BitCounts *counts, uint8_t value) {
    ++counts->bytes;
    counts->values[value] = true;
    for (unsigned bit = 0; bit < 8; ++bit)
        counts->ones[bit] += (value >> bit) & 1u;
}

static void require_band(const BitCounts *counts, unsigned low, unsigned high) {
    assert(counts->bytes);
    for (unsigned bit = 0; bit < 8; ++bit) {
        assert(counts->ones[bit] * 100u >= counts->bytes * low);
        assert(counts->ones[bit] * 100u <= counts->bytes * high);
    }
}

static void require_full_byte_domain(const BitCounts *counts) {
    for (unsigned value = 0; value < 256; ++value)
        assert(counts->values[value]);
}

static uint64_t seed_at(unsigned index) {
    if (index == 0)
        return 0;
    if (index == 1)
        return UINT64_MAX;
    if (index == 2)
        return UINT64_C(1) << 63;
    return UINT64_C(0x0123456789abcdef) + (uint64_t)index * UINT64_C(0x0101010101010101);
}

static void test_seeded_generations(void) {
    static const uint64_t seeds[] = {0, 1, UINT64_MAX, UINT64_C(0x8000000000000000),
                                     UINT64_C(0x123456789abcdef0)};
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    MemoryImage *first = malloc(sizeof(*first)), *second = malloc(sizeof(*second));
    assert(a && b && first && second);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); ++seed) {
            cupid_gb_set_model(a, (CupidGbModel)model);
            cupid_gb_set_model(b, (CupidGbModel)model);
            cupid_gb_set_power_on_seed(a, seeds[seed]);
            cupid_gb_set_power_on_seed(b, seeds[seed]);
            cupid_gb_init(a);
            cupid_gb_init(b);
            assert(cupid_gb_get_power_on_seed(a) == seeds[seed]);
            assert(cupid_gb_get_power_on_seed(b) == seeds[seed]);
            capture(a, first);
            capture(b, second);
            assert(memcmp(first, second, sizeof(*first)) == 0);
            /* Reinitialization reproduces the selected image after guest writes. */
            memset(a->work_ram, 0x42, sizeof(a->work_ram));
            memset(a->high_ram, 0xa5, sizeof(a->high_ram));
            cupid_gb_init(a);
            capture(a, second);
            assert(memcmp(first, second, sizeof(*first)) == 0);
            assert(memcmp(first->work, first->work + 1, 0x1fff) != 0);
            for (unsigned byte = 0; byte < sizeof(a->video_ram); ++byte)
                assert(a->video_ram[byte] == 0);
            if (model != CUPID_GB_MODEL_CGB) {
                for (unsigned byte = 0x2000; byte < sizeof(a->work_ram); ++byte)
                    assert(a->work_ram[byte] == 0);
            } else {
                for (unsigned byte = 0x2000; byte < 0x3000; ++byte)
                    assert(a->work_ram[byte] == 0);
                for (unsigned byte = 0; byte < sizeof(a->object_attribute_memory); ++byte)
                    assert(a->object_attribute_memory[byte] == 0);
                for (unsigned byte = 0; byte < 16; ++byte)
                    assert(a->io_registers[0x30 + byte] == (byte & 1u ? 0xff : 0));
            }
        }
        cupid_gb_set_power_on_seed(a, 0);
        cupid_gb_init(a);
        capture(a, first);
        cupid_gb_set_power_on_seed(a, 1);
        cupid_gb_init(a);
        capture(a, second);
        assert(memcmp(first->work, second->work, 0x2000) != 0);
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(first);
    free(second);
    free(a);
    free(b);
}

static void test_model_distributions(void) {
    /* These wide confidence bands check modeled bit biases, not an RNG
     * algorithm or golden sequence. DMG0 has no separately qualified profile. */
    static const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB,
                                          CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB,
                                          CUPID_GB_MODEL_SGB2};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        BitCounts work[2] = {0}, high[2] = {0}, oam[2] = {0}, wave[2] = {0};
        BitCounts background = {0}, object = {0};
        const bool color = models[model] == CUPID_GB_MODEL_CGB;
        const bool pocket = models[model] == CUPID_GB_MODEL_MGB;
        const bool sgb2 = models[model] == CUPID_GB_MODEL_SGB2;
        cupid_gb_set_model(gb, models[model]);
        for (unsigned seed = 0; seed < 128; ++seed) {
            cupid_gb_set_power_on_seed(gb, seed_at(seed));
            cupid_gb_init(gb);
            if (color) {
                gb->boot_rom_size = 0x900;
                cupid_gb_start_boot_rom(gb);
            }
            const size_t bytes = color ? sizeof(gb->work_ram) : 0x2000;
            for (size_t byte = 0; byte < bytes; ++byte)
                count_byte(&work[(byte >> 8) & 1u],
                           (uint8_t)(gb->work_ram[byte] ^ (sgb2 ? 0x55u : 0)));
            for (size_t byte = 0; byte < sizeof(gb->high_ram); ++byte)
                count_byte(&high[byte & 1u], gb->high_ram[byte]);
            if (!color) {
                for (size_t byte = 0; byte < sizeof(gb->object_attribute_memory); ++byte)
                    count_byte(&oam[(byte >> 1) & 1u], gb->object_attribute_memory[byte]);
                for (unsigned byte = 0; byte < 15; ++byte)
                    count_byte(&wave[byte & 1u], gb->io_registers[0x30 + byte]);
                assert(gb->io_registers[0x3f] == 0);
            } else {
                gb->boot_rom_size = 0x900;
                cupid_gb_enter_boot_rom(gb);
                for (size_t byte = 0; byte < sizeof(gb->cgb_bg_palette_ram); ++byte) {
                    count_byte(&background, gb->cgb_bg_palette_ram[byte]);
                    count_byte(&object, gb->cgb_obj_palette_ram[byte]);
                }
            }
        }
        if (color || pocket) {
            require_band(&work[0], 47, 53);
            require_band(&work[1], 47, 53);
            require_full_byte_domain(&work[0]);
            require_full_byte_domain(&work[1]);
        } else if (sgb2) {
            require_band(&work[0], 10, 15);
            require_band(&work[1], 10, 15);
        } else {
            require_band(&work[0], 72, 78);
            require_band(&work[1], 22, 28);
        }
        if (color) {
            require_band(&high[0], 43, 57);
            require_band(&high[1], 43, 57);
            require_band(&background, 43, 57);
            require_band(&object, 43, 57);
            require_full_byte_domain(&background);
            require_full_byte_domain(&object);
        } else {
            require_band(&high[0], 7, 19);
            require_band(&high[1], 81, 93);
            require_band(&oam[0], 81, 93);
            require_band(&oam[1], 7, 19);
            require_band(&wave[0], pocket ? 67 : 81, pocket ? 83 : 95);
            require_band(&wave[1], pocket ? 17 : 5, pocket ? 33 : 19);
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void load_generated_profile(CupidGb *gb, CupidGbModel model, uint64_t seed, bool color) {
    uint8_t rom[0x8000] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    rom[0x143] = color ? 0x80 : 0;
    rom[0x147] = 0x10; /* RTC and battery-backed cartridge RAM. */
    rom[0x149] = 3;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, seed);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
}

static void load_generated_rom(CupidGb *gb, CupidGbModel model, uint64_t seed) {
    load_generated_profile(gb, model, seed, model == CUPID_GB_MODEL_CGB);
}

static void observe_serial(const CupidGb *gb, uint8_t transmitted, void *data) {
    (void)gb;
    (void)transmitted;
    ++*(unsigned *)data;
}

static void observe_level(const CupidGb *gb, bool enabled, uint64_t cycle, void *data) {
    (void)gb;
    (void)enabled;
    (void)cycle;
    ++*(unsigned *)data;
}

static void observe_image(const CupidGbPrinterImage *image, void *data) {
    (void)image;
    ++*(unsigned *)data;
}

static void observe_done(void *data) {
    ++*(unsigned *)data;
}

static void register_observers(CupidGb *gb, unsigned *count) {
    cupid_gb_set_serial_output_callback(gb, observe_serial, count);
    cupid_gb_set_rumble_callback(gb, observe_level, count);
    cupid_gb_set_infrared_callback(gb, observe_level, count);
    cupid_gb_set_infrared_input(gb, true);
    gb->mbc7.motion_x = 19;
    gb->mbc7.motion_y = -23;
}

static void require_observers(const CupidGb *gb, const unsigned *count) {
    assert(gb->serial_output_callback == observe_serial && gb->serial_output_user_data == count);
    assert(gb->rumble_callback == observe_level && gb->rumble_user_data == count);
    assert(gb->infrared_callback == observe_level && gb->infrared_user_data == count);
    assert(gb->huc3_ir_input && gb->mbc7.motion_x == 19 && gb->mbc7.motion_y == -23);
    assert(!*count);
}

static void require_policy(const CupidGb *gb, CupidGbPowerOn policy) {
    assert(gb->power_on.seed == policy.seed);
    assert(gb->power_on.generation_seed == policy.generation_seed);
    assert(gb->power_on.fixed_seed == policy.fixed_seed);
}

static void test_sgb_post_boot_state_continuation(void) {
    static const unsigned checkpoints[] = {0, 1, 16, 64, 6000, 12000};
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    assert(a && b);
    for (unsigned model = CUPID_GB_MODEL_SGB; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned automatic = 0; automatic < 2; ++automatic) {
            load_generated_rom(a, (CupidGbModel)model, 0);
            load_generated_rom(b, (CupidGbModel)model, UINT64_MAX);
            if (automatic)
                cupid_gb_clear_power_on_seed(b);
            const CupidGbPowerOn policy = b->power_on;
            assert(cupid_gb_read_u8(a, 0xff44) == 144);
            assert((cupid_gb_read_u8(a, 0xff41) & 3u) == CUPID_GB_PPU_MODE_VBLANK);
            assert(a->ppu_counter == 50);
            unsigned steps = 0;
            bool visible_after_vblank = false;
            for (unsigned point = 0; point < sizeof(checkpoints) / sizeof(checkpoints[0]);
                 ++point) {
                while (steps < checkpoints[point]) {
                    assert(cupid_gb_step(a) && cupid_gb_step(b));
                    ++steps;
                    assert(a->pixels.line < CUPID_GB_PPU_TOTAL_SCANLINES);
                    assert(b->pixels.line < CUPID_GB_PPU_TOTAL_SCANLINES);
                    visible_after_vblank |= a->pixels.line < CUPID_GB_PPU_VISIBLE_SCANLINES;
                    if (steps == 1)
                        assert(a->pixels.line == 144 && a->pixels.dot == 212);
                }
                const size_t length = cupid_gb_state_size(a);
                size_t written = 0;
                uint8_t *bytes = malloc(length), *roundtrip = malloc(length);
                assert(bytes && roundtrip && length == cupid_gb_state_size(b));
                assert(cupid_gb_save_state(a, bytes, length, &written) == CUPID_GB_STATE_OK);
                assert(written == length);
                if (point) {
                    assert(cupid_gb_save_state(b, roundtrip, length, &written) ==
                           CUPID_GB_STATE_OK);
                    assert(written == length && memcmp(bytes, roundtrip, length) == 0);
                }
                assert(cupid_gb_load_state(b, bytes, length) == CUPID_GB_STATE_OK);
                require_policy(b, policy);
                assert(cupid_gb_save_state(b, roundtrip, length, &written) == CUPID_GB_STATE_OK);
                assert(written == length && memcmp(bytes, roundtrip, length) == 0);
                free(bytes);
                free(roundtrip);
            }
            assert(visible_after_vblank && a->vblank_events == 2);
            assert(a->elapsed_cycles == 144000 && b->elapsed_cycles == a->elapsed_cycles);
        }
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
}

static void load_legacy_sgb_profile(CupidGb *gb, CupidGbModel model) {
    uint8_t rom[0x8000] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    memset(gb->work_ram, 0, sizeof(gb->work_ram));
    memset(gb->high_ram, 0, sizeof(gb->high_ram));
    memset(gb->object_attribute_memory, 0, sizeof(gb->object_attribute_memory));
    memset(gb->io_registers + 0x30, 0xff, 16);
    gb->io_registers[CUPID_GB_IO_LY] = 0xff;
    gb->io_registers[CUPID_GB_IO_STAT] = 0;
    gb->ppu_counter = 0;
    /* The archived writer retained zero baselines in this non-clock cartridge. */
    gb->rtc.base_time = 0;
    gb->tpp1.rtc_base_time = 0;
    gb->huc3.rtc_base_time = 0;
}

static uint8_t *encode_legacy_sgb_profile(const CupidGb *gb, size_t *length) {
    enum { NATIVE_HEADER_BYTES = 128, RTC_HISTORY_BYTES = 6, ACCESSORY_DESCRIPTOR_BYTES = 8 };

    const size_t current = cupid_gb_state_size(gb);
    uint8_t *bytes = malloc(current);
    size_t written = 0;
    assert(bytes);
    assert(cupid_gb_save_state(gb, bytes, current, &written) == CUPID_GB_STATE_OK);
    assert(written == current && bytes[8] == CUPID_GB_STATE_VERSION);
    const uint8_t empty[ACCESSORY_DESCRIPTOR_BYTES] = {0};
    const size_t descriptor = current - ACCESSORY_DESCRIPTOR_BYTES;
    assert(memcmp(bytes + descriptor, empty, sizeof(empty)) == 0);
    /* Remove later clock and reconstruction history before the descriptor. The archived V12
     * receiver history and empty accessory descriptor remain byte-for-byte. */
    size_t startup_bytes = 0;
    assert(cupid_gb_sgb_startup_state_size(gb, &startup_bytes));
    const size_t removed = startup_bytes + SRAM_LENGTH_BYTES + IO_HISTORY_BYTES +
                           RTC_HISTORY_BYTES + CUPID_GB_AUDIO_STATE_SIZE +
                           CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
                           CUPID_GB_VRAM_BUS_STATE_SIZE;
    memmove(bytes + descriptor - removed, bytes + descriptor, ACCESSORY_DESCRIPTOR_BYTES);
    *length = current - removed;
    bytes[8] = 12;
    for (unsigned i = 0; i < 4; ++i)
        bytes[12 + i] = (uint8_t)(*length >> (8 * i));
    cupid_sha256_parts(bytes, 96, bytes + NATIVE_HEADER_BYTES, *length - NATIVE_HEADER_BYTES,
                       bytes + 96);
    return bytes;
}

static void test_legacy_sgb_startup_recovery(void) {
    /* Complete native fixtures were verified against the previous writer's
     * output. Keep their digests fixed so this does not become a roundtrip of
     * whatever startup state the current implementation happens to produce. */
    static const char *const expected_digest[] = {
        "dc64927fd2456e04a3f87e93eee78718635887abd88b376d82c7f037459e8f9b",
        "760a9c7bf3cff34089d16e26030db30b7bdcf669f8be738dad08bcb801999a19"};
    static const unsigned checkpoints[] = {0, 1, 16, 12000};
    CupidGb *expected = calloc(1, sizeof(*expected)), *restored = calloc(1, sizeof(*restored));
    assert(expected && restored);
    for (unsigned model = CUPID_GB_MODEL_SGB; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned automatic = 0; automatic < 2; ++automatic) {
            for (unsigned trusted = 0; trusted < 2; ++trusted) {
                load_legacy_sgb_profile(expected, (CupidGbModel)model);
                load_legacy_sgb_profile(restored, (CupidGbModel)model);
                size_t legacy_length;
                size_t written = 0;
                uint8_t *legacy = encode_legacy_sgb_profile(expected, &legacy_length);
                assert(legacy[8] == 12);
                uint8_t digest[32];
                char hexadecimal[65];
                cupid_sha256(legacy, legacy_length, digest);
                for (unsigned i = 0; i < sizeof(digest); ++i)
                    (void)snprintf(hexadecimal + i * 2, 3, "%02x", digest[i]);
                assert(strcmp(hexadecimal, expected_digest[model - CUPID_GB_MODEL_SGB]) == 0);

                /* Host registrations and dirty bookkeeping come from the receiver;
                 * the old file carries neither policy nor accessory history. */
                cupid_gb_set_power_on_seed(restored, UINT64_MAX);
                if (automatic)
                    cupid_gb_clear_power_on_seed(restored);
                const CupidGbPowerOn policy = restored->power_on;
                restored->battery_write_generation = 123;
                restored->vblank_events = 456;
                strcpy(restored->save_path, "preserved-slot.sav");
                unsigned callbacks = 0;
                register_observers(restored, &callbacks);
                cupid_gb_set_infrared_input(expected, true);
                CupidGbPrinter *source_printer =
                    cupid_gb_printer_create(observe_image, observe_done, &callbacks);
                CupidGbPrinter *target_printer =
                    cupid_gb_printer_create(observe_image, observe_done, &callbacks);
                assert(source_printer && target_printer);
                cupid_gb_set_serial_link(expected, cupid_gb_printer_link(source_printer));
                cupid_gb_set_serial_link(restored, cupid_gb_printer_link(target_printer));
                CupidGbStateContext context;
                assert(cupid_gb_state_context_init(&context, restored) == CUPID_GB_STATE_OK);
                assert((trusted ? cupid_gb_load_state_with_context(restored, legacy, legacy_length,
                                                                   &context)
                                : cupid_gb_load_state(restored, legacy, legacy_length)) ==
                       CUPID_GB_STATE_OK);
                expected->io_registers[CUPID_GB_IO_LY] = 144;
                expected->io_registers[CUPID_GB_IO_STAT] = 0x81;
                expected->ppu_counter = 50;
                assert(restored->io_registers[CUPID_GB_IO_LY] == 144 &&
                       restored->io_registers[CUPID_GB_IO_STAT] == 0x81 &&
                       restored->ppu_counter == 50 && !restored->pixels.initialized);
                require_policy(restored, policy);
                require_observers(restored, &callbacks);
                assert(restored->serial_link.user_data == target_printer &&
                       restored->serial_link.device == CUPID_GB_SERIAL_DEVICE_PRINTER);
                assert(restored->battery_write_generation == 123 &&
                       restored->vblank_events == 456 &&
                       strcmp(restored->save_path, "preserved-slot.sav") == 0);
                const size_t length = cupid_gb_state_size(expected);
                uint8_t *reference = malloc(length), *actual = malloc(length);
                assert(reference && actual && length == cupid_gb_state_size(restored));
                unsigned steps = 0;
                bool visible = false;
                for (unsigned point = 0; point < sizeof(checkpoints) / sizeof(checkpoints[0]);
                     ++point) {
                    while (steps < checkpoints[point]) {
                        assert(cupid_gb_step(expected) && cupid_gb_step(restored));
                        cupid_gb_printer_update(source_printer, expected);
                        cupid_gb_printer_update(target_printer, restored);
                        ++steps;
                        assert(restored->pixels.line < CUPID_GB_PPU_TOTAL_SCANLINES);
                        visible |= restored->pixels.line < CUPID_GB_PPU_VISIBLE_SCANLINES;
                        if (steps == 1)
                            assert(restored->pixels.line == 144 && restored->pixels.dot == 212);
                    }
                    assert(cupid_gb_save_state(expected, reference, length, &written) ==
                           CUPID_GB_STATE_OK);
                    assert(cupid_gb_save_state(restored, actual, length, &written) ==
                           CUPID_GB_STATE_OK);
                    assert(written == length && memcmp(reference, actual, length) == 0);
                    assert(
                        (trusted
                             ? cupid_gb_load_state_with_context(restored, actual, length, &context)
                             : cupid_gb_load_state(restored, actual, length)) == CUPID_GB_STATE_OK);
                    require_policy(restored, policy);
                    require_observers(restored, &callbacks);
                    assert(restored->battery_write_generation == 123);
                }
                assert(visible && expected->vblank_events == 2 && restored->vblank_events == 458);
                assert(expected->elapsed_cycles == 144000 && restored->elapsed_cycles == 144000);
                cupid_gb_set_serial_link(expected, (CupidGbSerialLink){0});
                cupid_gb_set_serial_link(restored, (CupidGbSerialLink){0});
                cupid_gb_printer_destroy(source_printer);
                cupid_gb_printer_destroy(target_printer);
                free(legacy);
                free(reference);
                free(actual);
            }
        }
    }
    cupid_gb_cleanup(expected);
    cupid_gb_cleanup(restored);
    free(expected);
    free(restored);
}

static void test_legacy_sgb_startup_signature(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    for (unsigned model = CUPID_GB_MODEL_SGB; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned variation = 0; variation < 9; ++variation) {
            load_legacy_sgb_profile(source, (CupidGbModel)model);
            load_legacy_sgb_profile(target, (CupidGbModel)model);
            switch (variation) {
                case 0:
                    source->elapsed_cycles = 4;
                    break;
                case 1:
                    source->cpu.pc = 0x101;
                    break;
                case 2:
                    source->io_registers[CUPID_GB_IO_LY] = 0xfe;
                    break;
                case 3:
                    source->io_registers[CUPID_GB_IO_STAT] = 0x80;
                    break;
                case 4:
                    source->io_registers[CUPID_GB_IO_LCDC] = 0;
                    break;
                case 5:
                    source->ppu_counter = 1;
                    break;
                case 6:
                    source->pixels.initialized = true;
                    break;
                case 7:
                    source->cpu.a ^= 1;
                    break;
                case 8:
                    source->boot_rom_enabled = true;
                    source->boot_rom_size = target->boot_rom_size = 0x100;
                    break;
            }
            const size_t length = cupid_gb_state_size(source);
            size_t written = 0;
            uint8_t *bytes = malloc(length), *roundtrip = malloc(length);
            assert(bytes && roundtrip);
            assert(cupid_gb_save_state(source, bytes, length, &written) == CUPID_GB_STATE_OK);
            assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
            assert(cupid_gb_save_state(target, roundtrip, length, &written) == CUPID_GB_STATE_OK);
            assert(written == length && memcmp(bytes, roundtrip, length) == 0);
            free(bytes);
            free(roundtrip);
        }
    }
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void test_cold_firmware_reconstruction(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    CupidGb *before = malloc(sizeof(*before));
    MemoryImage *expected = malloc(sizeof(*expected)), *actual = malloc(sizeof(*actual));
    assert(a && b && before && expected && actual);
    cupid_gb_start_boot_rom(NULL);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned color = 0; color <= (model == CUPID_GB_MODEL_CGB); ++color) {
            for (unsigned sample = 0; sample < 8; ++sample) {
                const uint64_t seed = seed_at(sample);
                load_generated_profile(a, (CupidGbModel)model, seed, color != 0);
                load_generated_profile(b, (CupidGbModel)model, seed, color != 0);
                unsigned callbacks = 0;
                register_observers(a, &callbacks);
                *before = *a;
                /* Missing firmware must not partially reconstruct volatile storage. */
                cupid_gb_start_boot_rom(a);
                assert(memcmp(a, before, sizeof(*a)) == 0);
                a->boot_rom_size = b->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100;
                cupid_gb_start_boot_rom(b);
                capture(b, expected);
                assert(b->boot_rom_enabled && b->cpu.pc == 0);
                assert(cupid_gb_get_power_on_seed(b) == seed);
                if (model == CUPID_GB_MODEL_CGB) {
                    for (size_t byte = 0x2000; byte < 0x3000; ++byte)
                        assert(a->work_ram[byte] == 0);
                    assert(memcmp(a->work_ram + 0x2000, b->work_ram + 0x2000, 0x1000) != 0);
                    assert(memcmp(a->work_ram, b->work_ram, 0x2000) == 0);
                    assert(memcmp(a->work_ram + 0x3000, b->work_ram + 0x3000, 0x5000) == 0);
                } else {
                    assert(memcmp(a->work_ram, b->work_ram, sizeof(a->work_ram)) == 0);
                }
                /* Cold firmware preparation deliberately replaces every listed
                 * volatile domain, but uses this generation rather than future policy. */
                memset(a->work_ram, 0x3c, sizeof(a->work_ram));
                memset(a->video_ram, 0x87, sizeof(a->video_ram));
                memset(a->high_ram, 0xa6, sizeof(a->high_ram));
                memset(a->object_attribute_memory, 0x51, sizeof(a->object_attribute_memory));
                memset(a->io_registers + 0x30, 0x99, 16);
                memset(a->cgb_bg_palette_ram, 0xd2, sizeof(a->cgb_bg_palette_ram));
                memset(a->cgb_obj_palette_ram, 0x6b, sizeof(a->cgb_obj_palette_ram));
                memset(a->cartridge_ram, 0x37, sizeof(a->cartridge_ram));
                a->rtc.s = 47;
                a->rtc.ls = 23;
                a->rtc.base_time = 123456;
                a->tpp1.live[1] = 11;
                a->tpp1.rtc_base_time = 234567;
                a->huc3.minutes = 890;
                a->huc3.rtc_base_time = 345678;
                a->joypad = 0xed;
                cupid_gb_set_power_on_seed(a, ~seed);
                *before = *a;
                cupid_gb_start_boot_rom(a);
                capture(a, actual);
                assert(memcmp(actual, expected, sizeof(*actual)) == 0);
                assert(memcmp(a->cartridge_ram, before->cartridge_ram, sizeof(a->cartridge_ram)) ==
                       0);
                assert(memcmp(&a->rtc, &before->rtc, sizeof(a->rtc)) == 0);
                assert(memcmp(&a->tpp1, &before->tpp1, sizeof(a->tpp1)) == 0);
                assert(memcmp(&a->huc3, &before->huc3, sizeof(a->huc3)) == 0);
                assert(a->joypad == before->joypad && a->boot_rom_size == before->boot_rom_size);
                assert(memcmp(a->boot_rom, before->boot_rom, sizeof(a->boot_rom)) == 0);
                assert(a->boot_rom_enabled && a->cpu.pc == 0 && !a->interrupt_enable);
                require_policy(a, before->power_on);
                require_observers(a, &callbacks);
            }
        }
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
    free(before);
    free(expected);
    free(actual);
}

static void test_boot_entry_preserves_memory(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    CupidGb *before = malloc(sizeof(*before));
    assert(a && b && before);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned seed = 0; seed < 8; ++seed) {
            load_generated_rom(a, (CupidGbModel)model, seed_at(seed));
            load_generated_rom(b, (CupidGbModel)model, seed_at(seed));
            a->boot_rom_size = b->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100;
            cupid_gb_enter_boot_rom(b);
            /* Guest writes between power-on and overlay entry must survive.
             * The boot IO image belongs to the original generation, even if
             * the next cold start has since selected another fixed seed. */
            for (size_t i = 0; i < sizeof(a->work_ram); ++i)
                a->work_ram[i] ^= (uint8_t)(i * 19u + 7u);
            memset(a->high_ram, 0x5a, sizeof(a->high_ram));
            memset(a->video_ram, 0x7b, sizeof(a->video_ram));
            memset(a->object_attribute_memory, 0xa6, sizeof(a->object_attribute_memory));
            memset(a->cartridge_ram, 0x37, sizeof(a->cartridge_ram));
            a->rtc.base_time = 123456;
            cupid_gb_set_power_on_seed(a, ~seed_at(seed));
            *before = *a;
            cupid_gb_enter_boot_rom(a);
            assert(memcmp(a->work_ram, before->work_ram, sizeof(a->work_ram)) == 0);
            assert(memcmp(a->high_ram, before->high_ram, sizeof(a->high_ram)) == 0);
            assert(memcmp(a->video_ram, before->video_ram, sizeof(a->video_ram)) == 0);
            assert(memcmp(a->object_attribute_memory, before->object_attribute_memory,
                          sizeof(a->object_attribute_memory)) == 0);
            assert(memcmp(a->cartridge_ram, before->cartridge_ram, sizeof(a->cartridge_ram)) == 0);
            assert(memcmp(&a->rtc, &before->rtc, sizeof(a->rtc)) == 0);
            assert(a->power_on.seed == before->power_on.seed &&
                   a->power_on.generation_seed == before->power_on.generation_seed &&
                   a->power_on.fixed_seed == before->power_on.fixed_seed);
            assert(memcmp(a->io_registers + 0x30, b->io_registers + 0x30, 16) == 0);
            assert(a->io_registers[0x3f] == 0);
            if (model == CUPID_GB_MODEL_CGB) {
                for (unsigned i = 0; i < 16; ++i)
                    assert(a->io_registers[0x30 + i] == 0);
                assert(memcmp(a->cgb_bg_palette_ram, b->cgb_bg_palette_ram, 64) == 0);
                assert(memcmp(a->cgb_obj_palette_ram, b->cgb_obj_palette_ram, 64) == 0);
            }
        }
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
    free(before);
}

static void test_policy_and_state_lifetime(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    CupidGb *before = malloc(sizeof(*before));
    assert(a && b && before);
    cupid_gb_set_power_on_seed(NULL, 0);
    cupid_gb_clear_power_on_seed(NULL);
    assert(cupid_gb_get_power_on_seed(NULL) == 0);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        load_generated_rom(a, (CupidGbModel)model, 0);
        *before = *a;
        cupid_gb_set_power_on_seed(a, UINT64_MAX);
        assert(a->power_on.fixed_seed && a->power_on.seed == UINT64_MAX);
        assert(cupid_gb_get_power_on_seed(a) == 0);
        before->power_on = a->power_on;
        assert(memcmp(before, a, sizeof(*a)) == 0);
        *before = *a;
        cupid_gb_clear_power_on_seed(a);
        assert(!a->power_on.fixed_seed && !a->power_on.seed);
        assert(cupid_gb_get_power_on_seed(a) == 0);
        before->power_on = a->power_on;
        assert(memcmp(before, a, sizeof(*a)) == 0);
        load_generated_rom(a, (CupidGbModel)model, 0);
        load_generated_rom(b, (CupidGbModel)model, UINT64_MAX);
        const CupidGbPowerOn policy = b->power_on;
        size_t length = cupid_gb_state_size(a), written = 0;
        uint8_t *bytes = malloc(length), *restored = malloc(length);
        assert(bytes && restored);
        assert(cupid_gb_save_state(a, bytes, length, &written) == CUPID_GB_STATE_OK);
        assert(written == length);
        assert(cupid_gb_load_state(b, bytes, length) == CUPID_GB_STATE_OK);
        assert(b->power_on.seed == policy.seed &&
               b->power_on.generation_seed == policy.generation_seed &&
               b->power_on.fixed_seed == policy.fixed_seed);
        assert(cupid_gb_save_state(b, restored, length, &written) == CUPID_GB_STATE_OK);
        assert(written == length && memcmp(bytes, restored, length) == 0);
        *before = *b;
        bytes[length - 1] ^= 1;
        assert(cupid_gb_load_state(b, bytes, length) == CUPID_GB_STATE_INVALID);
        assert(memcmp(before, b, sizeof(*b)) == 0);
        bytes[length - 1] ^= 1;

        /* An integrity-valid invalid STOP-port boolean reaches candidate decoding.
         * Rejecting it must preserve the target's host policy and live state. */
        enum {
            NATIVE_HEADER_BYTES = 128,
            ACCESSORY_DESCRIPTOR_BYTES = 8,
            INFRARED_HISTORY_BYTES = 6,
            RTC_HISTORY_BYTES = 6,
            PULSE_DAC_BYTES = 2,
            STOP_PORT_BYTES = 4
        };

        assert(bytes[8] == CUPID_GB_STATE_VERSION && length > NATIVE_HEADER_BYTES);
        size_t startup_bytes = 0;
        assert(cupid_gb_sgb_startup_state_size(a, &startup_bytes));
        const size_t stop_offset = length - startup_bytes - SRAM_LENGTH_BYTES - IO_HISTORY_BYTES -
                                   ACCESSORY_DESCRIPTOR_BYTES - CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
                                   CUPID_GB_PIXEL_TIMING_STATE_SIZE - CUPID_GB_VRAM_BUS_STATE_SIZE -
                                   CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES -
                                   INFRARED_HISTORY_BYTES - PULSE_DAC_BYTES - STOP_PORT_BYTES;
        assert(bytes[stop_offset] <= 1);
        bytes[stop_offset] = 2;
        cupid_sha256_parts(bytes, 96, bytes + 128, length - 128, bytes + 96);
        assert(cupid_gb_load_state(b, bytes, length) == CUPID_GB_STATE_INVALID);
        assert(memcmp(before, b, sizeof(*b)) == 0);
        free(bytes);
        free(restored);
    }
    cupid_gb_clear_power_on_seed(a);
    cupid_gb_init(a);
    assert(!a->power_on.fixed_seed);
    const uint64_t automatic_seed = cupid_gb_get_power_on_seed(a);
    cupid_gb_set_model(b, a->model);
    cupid_gb_set_power_on_seed(b, automatic_seed);
    cupid_gb_init(b);
    MemoryImage first, second;
    capture(a, &first);
    capture(b, &second);
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    /* Automatic seeds are observable and replayable; clock resolution does not
     * promise that two immediately adjacent constructions choose distinct seeds. */
    cupid_gb_init(a);
    assert(!a->power_on.fixed_seed);
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    assert(!a->power_on.fixed_seed && !a->power_on.seed && !cupid_gb_get_power_on_seed(a));
    assert(!b->power_on.fixed_seed && !b->power_on.seed && !cupid_gb_get_power_on_seed(b));
    free(a);
    free(b);
    free(before);
}

static void test_printer_state_policy(void) {
    enum {
        NATIVE_HEADER_BYTES = 128,
        ACCESSORY_DESCRIPTOR_BYTES = 8,
        INFRARED_HISTORY_BYTES = 6,
        RTC_HISTORY_BYTES = 6,
        PULSE_DAC_BYTES = 2,
        STOP_PORT_BYTES = 4
    };

    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    CupidGb *before = malloc(sizeof(*before));
    assert(a && b && before);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned automatic = 0; automatic < 2; ++automatic) {
            for (unsigned trusted = 0; trusted < 2; ++trusted) {
                load_generated_rom(a, (CupidGbModel)model, 0);
                load_generated_rom(b, (CupidGbModel)model, UINT64_MAX);
                if (model & 1u) {
                    a->boot_rom_size = b->boot_rom_size =
                        model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100;
                    cupid_gb_start_boot_rom(a);
                    cupid_gb_start_boot_rom(b);
                }
                /* This continuation isolates host policy and printer restoration.
                 * The active SGB post-boot display has its own regression above. */
                cupid_gb_write_u8(a, 0xff40, 0);
                cupid_gb_write_u8(b, 0xff40, 0);
                if (automatic)
                    cupid_gb_clear_power_on_seed(b);
                const CupidGbPowerOn policy = b->power_on;
                const size_t empty_size = cupid_gb_state_size(a);
                size_t startup_bytes = 0;
                assert(cupid_gb_sgb_startup_state_size(a, &startup_bytes));
                const size_t stop_offset =
                    empty_size - startup_bytes - SRAM_LENGTH_BYTES - IO_HISTORY_BYTES -
                    ACCESSORY_DESCRIPTOR_BYTES - CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
                    CUPID_GB_PIXEL_TIMING_STATE_SIZE - CUPID_GB_VRAM_BUS_STATE_SIZE -
                    CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES - INFRARED_HISTORY_BYTES -
                    PULSE_DAC_BYTES - STOP_PORT_BYTES;
                unsigned callbacks = 0;
                register_observers(b, &callbacks);
                CupidGbPrinter *source =
                    cupid_gb_printer_create(observe_image, observe_done, &callbacks);
                CupidGbPrinter *target =
                    cupid_gb_printer_create(observe_image, observe_done, &callbacks);
                assert(source && target);
                cupid_gb_set_serial_link(a, cupid_gb_printer_link(source));
                cupid_gb_set_serial_link(b, cupid_gb_printer_link(target));
                CupidGbStateContext source_context, target_context;
                assert(cupid_gb_state_context_init(&source_context, a) == CUPID_GB_STATE_OK);
                assert(cupid_gb_state_context_init(&target_context, b) == CUPID_GB_STATE_OK);
                const size_t length = cupid_gb_state_size(a);
                size_t written = 0;
                uint8_t *bytes = malloc(length), *roundtrip = malloc(length);
                assert(bytes && roundtrip && length > empty_size);
                assert(cupid_gb_save_state(a, bytes, length, &written) == CUPID_GB_STATE_OK);
                assert(written == length && bytes[8] == CUPID_GB_STATE_VERSION);
                assert(cupid_gb_save_state_with_context(a, roundtrip, length, &written,
                                                        &source_context) == CUPID_GB_STATE_OK);
                assert(written == length && memcmp(bytes, roundtrip, length) == 0);
                const CupidGbStateResult loaded =
                    trusted ? cupid_gb_load_state_with_context(b, bytes, length, &target_context)
                            : cupid_gb_load_state(b, bytes, length);
                assert(loaded == CUPID_GB_STATE_OK);
                require_policy(b, policy);
                require_observers(b, &callbacks);
                assert(b->serial_link.user_data == target &&
                       b->serial_link.device == CUPID_GB_SERIAL_DEVICE_PRINTER);
                assert(cupid_gb_save_state(b, roundtrip, length, &written) == CUPID_GB_STATE_OK);
                assert(written == length && memcmp(bytes, roundtrip, length) == 0);
                *before = *b;
                for (unsigned malformed = 0; malformed < 2; ++malformed) {
                    if (!malformed)
                        bytes[96] ^= 1;
                    else {
                        memcpy(bytes, roundtrip, length);
                        assert(bytes[stop_offset] <= 1);
                        bytes[stop_offset] = 2;
                        cupid_sha256_parts(bytes, 96, bytes + NATIVE_HEADER_BYTES,
                                           length - NATIVE_HEADER_BYTES, bytes + 96);
                    }
                    const CupidGbStateResult rejected =
                        trusted
                            ? cupid_gb_load_state_with_context(b, bytes, length, &target_context)
                            : cupid_gb_load_state(b, bytes, length);
                    assert(rejected == CUPID_GB_STATE_INVALID);
                    assert(memcmp(b, before, sizeof(*b)) == 0);
                    require_policy(b, policy);
                    require_observers(b, &callbacks);
                }
                /* Rejection also leaves the external printer's native payload unchanged. */
                assert(cupid_gb_save_state(b, bytes, length, &written) == CUPID_GB_STATE_OK);
                assert(written == length && memcmp(bytes, roundtrip, length) == 0);
                cupid_gb_set_infrared_input(a, true);
                for (unsigned step = 0; step < 16; ++step) {
                    assert(cupid_gb_step(a) && cupid_gb_step(b));
                    cupid_gb_printer_update(source, a);
                    cupid_gb_printer_update(target, b);
                }
                assert(cupid_gb_save_state(a, bytes, length, &written) == CUPID_GB_STATE_OK);
                assert(cupid_gb_save_state(b, roundtrip, length, &written) == CUPID_GB_STATE_OK);
                assert(memcmp(bytes, roundtrip, length) == 0);
                require_policy(b, policy);
                require_observers(b, &callbacks);
                cupid_gb_set_serial_link(a, (CupidGbSerialLink){0});
                cupid_gb_set_serial_link(b, (CupidGbSerialLink){0});
                cupid_gb_printer_destroy(source);
                cupid_gb_printer_destroy(target);
                free(bytes);
                free(roundtrip);
            }
        }
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
    free(before);
}

static void test_color_target_initialization(void) {
    CupidEmulator *emulator = calloc(1, sizeof(*emulator));
    assert(emulator);
    cupid_gb_set_power_on_seed(&emulator->gb, 0);
    cupid_emulator_init(emulator, CUPID_SYSTEM_GBC);
    assert(emulator->gb.model == CUPID_GB_MODEL_CGB && emulator->gb.cgb_mode);
    assert(emulator->gb.cpu.a == 0x11);
    bool nonzero_upper_bank = false;
    for (unsigned i = 0x2000; i < sizeof(emulator->gb.work_ram); ++i)
        nonzero_upper_bank |= emulator->gb.work_ram[i] != 0;
    assert(nonzero_upper_bank);
    cupid_emulator_shutdown(emulator);
    free(emulator);
}

int main(void) {
    test_seeded_generations();
    test_model_distributions();
    test_cold_firmware_reconstruction();
    test_boot_entry_preserves_memory();
    test_policy_and_state_lifetime();
    test_sgb_post_boot_state_continuation();
    test_legacy_sgb_startup_recovery();
    test_legacy_sgb_startup_signature();
    test_printer_state_policy();
    test_color_target_initialization();
    puts("Startup memory and lifecycle checks passed.");
    return 0;
}

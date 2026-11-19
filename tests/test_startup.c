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
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/printer.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/sgb.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"
#include "test_state_layout.h"

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

static void test_color_work_ram_revisions(void) {
    static const CupidGbModel models[] = {CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A,
                                          CUPID_GB_MODEL_CGB_B, CUPID_GB_MODEL_CGB_C,
                                          CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB_E};
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t *post_boot = malloc(sizeof(gb->work_ram));
    assert(gb && post_boot);
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        BitCounts lower = {0}, upper = {0};
        const bool early = cupid_gb_model_is_early_cgb(models[model]);
        cupid_gb_set_model(gb, models[model]);
        for (unsigned seed = 0; seed < 16; ++seed) {
            cupid_gb_set_power_on_seed(gb, seed_at(seed));
            cupid_gb_init(gb);
            memcpy(post_boot, gb->work_ram, sizeof(gb->work_ram));
            gb->boot_rom_size = 0x900;
            cupid_gb_start_boot_rom(gb);
            assert(gb->boot_rom_enabled);
            for (size_t offset = 0; offset < sizeof(gb->work_ram); ++offset) {
                const uint8_t value = gb->work_ram[offset];
                assert(post_boot[offset] == (offset / 0x1000u == 2u ? 0u : value));
                const bool upper_half = (offset & 0x800u) != 0;
                const bool second_lane = (offset & 8u) != 0;
                if (early && upper_half != second_lane)
                    assert(value == 0);
                else
                    count_byte(upper_half ? &upper : &lower, value);
            }
            gb->work_ram[0x800] = 0x39;
            cupid_gb_enter_boot_rom(gb);
            assert(gb->work_ram[0x800] == 0x39);
        }
        if (early) {
            require_band(&lower, 95, 99);
            require_band(&upper, 95, 99);
        } else if (models[model] == CUPID_GB_MODEL_CGB_D) {
            require_band(&lower, 73, 77);
            require_band(&upper, 23, 27);
        } else {
            require_band(&lower, 48, 52);
            require_band(&upper, 48, 52);
            require_full_byte_domain(&lower);
            require_full_byte_domain(&upper);
        }
    }
    free(post_boot);
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

static const uint8_t cgb_entry_ports[] = {0x00, 0x46, 0x48, 0x49, 0x68, 0x6a,
                                          0x6c, 0x70, 0x72, 0x73, 0x74, 0x75};

static void check_cgb_cartridge_entry(CupidGb *gb, bool color, uint8_t checksum) {
    const uint8_t expected[] = {0x11, checksum,           0, color ? 0xff : 0, color ? 0x56 : 8,
                                0,    color ? 0x0d : 0x7c};
    assert(gb->cpu.pc == 0x0100 && gb->cpu.sp == 0xfffe && gb->cpu.f == 0x80);
    assert(gb->cgb_mode == color);
    const uint8_t color_obj_index = gb->model == CUPID_GB_MODEL_CGB_0 ? 0x40 : 0xc1;
    const uint8_t ports[] = {color ? 0xcf : 0xff,
                             0,
                             0,
                             0,
                             color ? 0xc0 : 0xc8,
                             color ? color_obj_index : 0xd0,
                             color ? 0xfe : 0xff,
                             color ? 0xf8 : 0xff,
                             0,
                             0,
                             color ? 0 : 0xff,
                             0x8f};
    assert(sizeof(ports) == sizeof(cgb_entry_ports));
    assert(!gb->dma_active && !gb->hdma_active && !gb->hdma_pending);
    assert(gb->cgb_object_priority_x == !color);
    gb->work_ram[0x1000] = 0x5a;
    gb->work_ram[0x2000] = 0xa5;
    assert(cupid_gb_read_u8(gb, 0xd000) == 0x5a);
    /* Save A, the other six CPU registers, then the first I/O reads. */
    for (unsigned i = 0; i < 2 * (sizeof(expected) + sizeof(ports)); ++i)
        assert(cupid_gb_step(gb));
    for (unsigned i = 0; i < sizeof(expected); ++i)
        assert(cupid_gb_read_u8(gb, (uint16_t)(0xff80 + i)) == expected[i]);
    for (unsigned i = 0; i < sizeof(ports); ++i)
        assert(cupid_gb_read_u8(gb, (uint16_t)(0xff87 + i)) == ports[i]);
}

static void test_cgb_cartridge_entry_registers(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A,
                                   CUPID_GB_MODEL_CGB_B, CUPID_GB_MODEL_CGB_C,
                                   CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB_E};

    const struct {
        uint8_t flag;
        uint8_t old_licensee;
        uint8_t new_licensee_digit;
        uint8_t checksum;
    } cases[] = {
        {0x24, 0x01, '2', 0x38}, {0x24, 0x33, '1', 0x38}, {0x24, 0x33, '2', 0},
        {0x24, 0x00, '1', 0},    {0x80, 0x01, '1', 0},    {0xc0, 0x33, '1', 0},
    };

    const uint8_t entry[] = {
        0xe0, 0x80,       /* LDH [$80], A */
        0x78, 0xe0, 0x81, /* LD A, B and LDH [$81], A */
        0x79, 0xe0, 0x82, /* C */
        0x7a, 0xe0, 0x83, /* D */
        0x7b, 0xe0, 0x84, /* E */
        0x7c, 0xe0, 0x85, /* H */
        0x7d, 0xe0, 0x86, /* L */
    };
    uint8_t rom[0x8000] = {0};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    rom[0x100] = 0xc3; /* JP $0150 */
    rom[0x101] = 0x50;
    rom[0x102] = 0x01;
    memcpy(rom + 0x134, "CUPIDGB HANDOFF", 15);
    memcpy(rom + 0x150, entry, sizeof(entry));
    size_t next = 0x150 + sizeof(entry);
    for (unsigned i = 0; i < sizeof(cgb_entry_ports); ++i) {
        rom[next++] = 0xf0; /* LDH A, [$00 + port] */
        rom[next++] = cgb_entry_ports[i];
        rom[next++] = 0xe0; /* LDH [$87 + index], A */
        rom[next++] = (uint8_t)(0x87 + i);
    }
    rom[0x144] = '0';
    cupid_gb_set_power_on_seed(gb, 0);
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        cupid_gb_set_model(gb, models[model]);
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            rom[0x143] = cases[i].flag;
            rom[0x145] = cases[i].new_licensee_digit;
            rom[0x14b] = cases[i].old_licensee;
            rom[0x14d] = 0;
            for (unsigned j = 0x134; j <= 0x14c; ++j)
                rom[0x14d] = (uint8_t)(rom[0x14d] - rom[j] - 1);

            bool color = (cases[i].flag & 0x80u) != 0;
            assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
            check_cgb_cartridge_entry(gb, color, cases[i].checksum);
            assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
            check_cgb_cartridge_entry(gb, color, cases[i].checksum);
            assert(cupid_gb_reset(gb, CUPID_GB_RESET_HARD));
            check_cgb_cartridge_entry(gb, color, cases[i].checksum);
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
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
    memset(gb->video_ram, 0, sizeof(gb->video_ram));
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
    /* The archived writer stored the previous grayscale palette in every slot. */
    const uint16_t palette[] = {0x7fff, 0x56b5, 0x2d6b, 0};
    for (unsigned slot = 0; slot < 4; ++slot)
        memcpy(gb->sgb.screen_palettes + 4 * slot, palette, sizeof(palette));
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
    const size_t removed = test_state_post_ram_size(gb) + SRAM_LENGTH_BYTES + IO_HISTORY_BYTES +
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

static void test_color_cartridge_entry_work_ram(void) {
    static const CupidGbModel models[] = {CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A,
                                          CUPID_GB_MODEL_CGB_B, CUPID_GB_MODEL_CGB_C,
                                          CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB_E};

    static const struct {
        uint8_t color_flag;
        uint8_t old_license;
        uint8_t new_license_second;
        uint8_t checksum;
    } cartridges[] = {
        {0x00, 0x01, '0', 0xef}, {0x80, 0x33, '1', 0x6f}, {0xc0, 0x01, '0', 0xaf},
        {0x00, 0x33, '0', 0},    {0x80, 0x00, '1', 0},    {0xc0, 0x33, '2', 0},
    };

    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_power_on_seed(gb, 0);
    uint8_t rom[0x8000] = {[0x134] = 0xf8, [0x137] = 0xf7, [0x144] = '0'};

    for (size_t model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        cupid_gb_set_model(gb, models[model]);
        for (size_t cart = 0; cart < sizeof(cartridges) / sizeof(cartridges[0]); ++cart) {
            rom[0x143] = cartridges[cart].color_flag;
            rom[0x145] = cartridges[cart].new_license_second;
            rom[0x14b] = cartridges[cart].old_license;
            rom[0x14d] = 0;
            for (unsigned address = 0x134; address < 0x14d; ++address)
                rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1);
            assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
            for (unsigned startup = 0; startup < 3; ++startup) {
                if (startup) {
                    memset(gb->work_ram + 0x2000, 0x5a, 0x1000);
                    assert(cupid_gb_reset(gb, startup == 1 ? CUPID_GB_RESET_QUICK
                                                           : CUPID_GB_RESET_HARD));
                }
                assert(gb->work_ram[0x2000] == cartridges[cart].checksum);
                for (size_t offset = 0x2840; offset < 0x2880; offset += 2) {
                    assert(gb->work_ram[offset] == 0xff);
                    assert(gb->work_ram[offset + 1] == 0x7f);
                }
                assert(gb->work_ram[0x283f] == 0 && gb->work_ram[0x2880] == 0);
            }
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
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
            if (model == CUPID_GB_MODEL_CGB) {
                cupid_gb_write_u8(a, 0xff4f, 1);
                cupid_gb_write_u8(a, 0xff70, 7);
                a->cgb_object_priority_x = true;
            }
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
                assert(!a->cgb_object_priority_x);
                assert(cupid_gb_read_u8(a, 0xff70) == 0 && cupid_gb_read_u8(a, 0xff4f) == 0xfe);
                assert(cupid_gb_read_u8(a, 0xd000) == a->work_ram[0x1000]);
                assert(cupid_gb_read_u8(a, 0x8000) == a->video_ram[0]);
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

static void run_firmware_after_adapter_hold(CupidGb *gb) {
    for (unsigned step = 0; step < 66000 && cupid_gb_sgb_startup_held(gb); ++step) {
        assert(cupid_gb_step(gb));
        int16_t samples[256];
        while (cupid_gb_apu_drain(gb, samples, 128)) {
        }
    }
    assert(!cupid_gb_sgb_startup_held(gb));
}

static void arm_firmware_entry_timer(CupidGb *gb) {
    cupid_gb_write_u8(gb, 0xff04, 0);
    cupid_gb_write_u8(gb, 0xff06, 0x42);
    cupid_gb_write_u8(gb, 0xff05, 0xff);
    cupid_gb_write_u8(gb, 0xff07, 5);
    for (unsigned cycle = 0; cycle < 8 && !gb->tima_overflow_delay; ++cycle)
        cupid_gb_tick(gb, 1);
    assert(gb->tima_overflow_delay == 1);
}

static void test_firmware_entry_peripheral_transactions(void) {
    enum { TIMER_OVERFLOW, ROW_RELEASE, HELD_BUTTON, SERIAL_TRANSFER, TIMER_RELOAD };

    const struct {
        uint8_t port;
        uint8_t value;
    } cases[] = {
        [TIMER_OVERFLOW] = {0x0f, 0xe0}, [ROW_RELEASE] = {0x00, 0xcf},
        [HELD_BUTTON] = {0x00, 0xce},    [SERIAL_TRANSFER] = {0x02, 0x7e},
        [TIMER_RELOAD] = {0x05, 0x17},
    };

    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_CGB_D; ++model) {
        for (unsigned transaction = 0; transaction < sizeof(cases) / sizeof(cases[0]);
             ++transaction) {
            load_generated_profile(gb, (CupidGbModel)model, 0, true);
            gb->boot_rom_size = cupid_gb_model_is_cgb(gb->model) ? 0x900 : 0x100;
            /* Read the relevant I/O port and save the first firmware result in HRAM. */
            const uint8_t entry[] = {0xf0, cases[transaction].port, 0xe0, 0x80};
            memcpy(gb->boot_rom, entry, sizeof(entry));
            if (transaction == TIMER_OVERFLOW || transaction == TIMER_RELOAD) {
                arm_firmware_entry_timer(gb);
                if (transaction == TIMER_RELOAD) {
                    cupid_gb_tick(gb, 1);
                    assert(gb->tima_reload_just_happened);
                }
            } else if (transaction == SERIAL_TRANSFER) {
                cupid_gb_write_u8(gb, 0xff01, 0xa5);
                cupid_gb_write_u8(gb, 0xff02, 0x81);
                cupid_gb_tick(gb, 128);
                assert(gb->serial_bits_remaining == 7);
            } else {
                cupid_gb_write_u8(gb, 0xff00, 0);
                cupid_gb_write_u8(gb, 0xff00, 0x30);
                if (transaction == HELD_BUTTON)
                    cupid_gb_set_buttons(gb, 0xfe);
            }
            const uint8_t held = gb->joypad;
            const bool pending = gb->joyp_sample_pending;
            cupid_gb_enter_boot_rom(gb);
            assert(gb->joypad == held && gb->joyp_sample_pending == pending);
            assert(!gb->tima_overflow_delay && !gb->tima_reload_just_happened);
            assert(!gb->joyp_select_delay && !gb->joyp_select_value);
            assert(!gb->serial_bits_remaining && !gb->serial_tx_latch);
            if (transaction == TIMER_RELOAD) {
                /* The public bus is writable between overlay entry and stepping.
                 * A pre-entry reload window must not suppress that write. */
                cupid_gb_write_u8(gb, 0xff05, 0x17);
                assert(cupid_gb_read_u8(gb, 0xff05) == 0x17);
            }
            run_firmware_after_adapter_hold(gb);
            assert(cupid_gb_step(gb) && cupid_gb_step(gb));
            assert(cupid_gb_read_u8(gb, 0xff80) == cases[transaction].value);
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_firmware_entry_oam_dma(void) {
    static uint8_t rom[0x8000];
    rom[0x14d] = (uint8_t)-25;
    memset(rom + 0x4000, 0x76, 0x4000); /* A redirected fetch would execute HALT. */
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_power_on_seed(gb, 0);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_CGB_D; ++model) {
        cupid_gb_set_model(gb, (CupidGbModel)model);
        for (unsigned phase = 0; phase < 3; ++phase) {
            assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
            gb->boot_rom_size = cupid_gb_model_is_cgb(gb->model) ? 0x900 : 0x100;
            const uint8_t entry[] = {0x3e, 0x42, 0xe0, 0x80}; /* LD A,$42; LDH [$80],A */
            memcpy(gb->boot_rom, entry, sizeof(entry));
            cupid_gb_write_u8(gb, 0xff46, 0x40);
            assert(gb->dma_start_delay);
            if (phase != 0) {
                cupid_gb_tick_dma_tcycles(gb, 20);
                assert(gb->dma_active && gb->dma_index);
            }
            if (phase == 2) {
                cupid_gb_write_u8(gb, 0xff46, 0x40);
                assert(gb->dma_start_delay && gb->dma_restart_pending);
            }

            uint8_t oam[sizeof(gb->object_attribute_memory)];
            memcpy(oam, gb->object_attribute_memory, sizeof(oam));
            cupid_gb_enter_boot_rom(gb);
            run_firmware_after_adapter_hold(gb);
            assert(cupid_gb_step(gb));
            assert(gb->cpu.pc == 2 && gb->cpu.a == 0x42 && !gb->cpu.halted);
            assert(cupid_gb_step(gb));
            assert(cupid_gb_read_u8(gb, 0xff80) == 0x42);
            assert(!cupid_gb_oam_dma_blocks_bus(gb) && !gb->dma_restart_pending);
            assert(memcmp(oam, gb->object_attribute_memory, sizeof(oam)) == 0);

            cupid_gb_write_u8(gb, 0xff46, 0x40);
            cupid_gb_tick_dma_tcycles(gb, 656);
            assert(!cupid_gb_oam_dma_blocks_bus(gb));
            for (unsigned i = 0; i < sizeof(oam); ++i)
                assert(gb->object_attribute_memory[i] == 0x76);
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_cartridge_entry_logo(void) {
    const CupidGbModel models[] = {
        CUPID_GB_MODEL_DMG0,  CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB,   CUPID_GB_MODEL_SGB,
        CUPID_GB_MODEL_SGB2,  CUPID_GB_MODEL_SGB_PAL, CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A,
        CUPID_GB_MODEL_CGB_B, CUPID_GB_MODEL_CGB_C,   CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB_E,
    };
    uint8_t rom[0x8000] = {0};
    rom[0x104] = 0xa5;
    rom[0x105] = 0x96;
    rom[0x133] = 0xf1;
    rom[0x14b] = 1;
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_power_on_seed(gb, 0);

    for (size_t model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        const bool color = cupid_gb_model_is_cgb(models[model]);
        const bool mark = models[model] != CUPID_GB_MODEL_DMG0;
        cupid_gb_set_model(gb, models[model]);
        cupid_gb_init(gb);
        uint8_t power_on_hram[sizeof(gb->high_ram)], expected_hram[sizeof(gb->high_ram)];
        memcpy(power_on_hram, gb->high_ram, sizeof(power_on_hram));
        memcpy(expected_hram, power_on_hram, sizeof(expected_hram));
        const size_t logo_bytes = color ? 0x30u : 0u;
        memcpy(expected_hram, rom + 0x104, logo_bytes);
        for (unsigned native = 0; native <= (unsigned)color; ++native) {
            /* Title sum 43 selects the legacy map in CGB compatibility mode. */
            rom[0x143] = native ? 0x80 : 0;
            rom[0x134] = (uint8_t)(0x43u - rom[0x143]);
            rom[0x14d] = 0;
            for (unsigned address = 0x134; address < 0x14d; ++address)
                rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1);
            assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
            uint8_t expected[sizeof(gb->video_ram)] = {
                [0x010] = 0xcc, [0x012] = 0xcc, [0x014] = 0x33, [0x016] = 0x33,
                [0x018] = 0xc3, [0x01a] = 0xc3, [0x01c] = 0x3c, [0x01e] = 0x3c,
                [0x188] = 0xff, [0x18a] = 0xff, [0x18c] = 0x03, [0x18e] = 0x03,
            };
            if (mark) {
                const uint8_t mark_tile[] = {0x3c, 0, 0x42, 0, 0xb9, 0, 0xa5, 0,
                                             0xb9, 0, 0xa5, 0, 0x42, 0, 0x3c, 0};
                memcpy(expected + 0x190, mark_tile, sizeof(mark_tile));
            }
            if (!native) {
                for (unsigned tile = 0; tile < 12; ++tile) {
                    expected[0x1904 + tile] = (uint8_t)(tile + 1);
                    expected[0x1924 + tile] = (uint8_t)(tile + 13);
                }
                expected[0x1910] = mark ? 25 : 0;
            }
            uint8_t quick_hram[sizeof(gb->high_ram)];
            memset(quick_hram, 0x5a, sizeof(quick_hram));
            memcpy(quick_hram, expected_hram, logo_bytes);
            if (color) {
                static const uint8_t stack[2][12] = {
                    {0x71, 0x02, 0x4d, 0x01, 0x00, 0xff, 0x0d, 0x00, 0x03, 0x06, 0xf9, 0x00},
                    {0x71, 0x02, 0x4d, 0x01, 0xc1, 0xff, 0x0d, 0x00, 0xd3, 0x05, 0xf9, 0x00},
                };
                memcpy(expected_hram + 0x72, stack[native], sizeof(stack[native]));
                memcpy(quick_hram + 0x72, stack[native], sizeof(stack[native]));
            }
            assert(memcmp(gb->video_ram, expected, sizeof(expected)) == 0);
            assert(memcmp(gb->high_ram, expected_hram, sizeof(expected_hram)) == 0);
            if (color && !native)
                assert(gb->cpu.h == 0x99 && gb->cpu.l == 0x1a);
            memset(gb->video_ram, 0x87, sizeof(gb->video_ram));
            memset(gb->high_ram, 0x5a, sizeof(gb->high_ram));
            assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
            assert(memcmp(gb->video_ram, expected, sizeof(expected)) == 0);
            assert(memcmp(gb->high_ram, quick_hram, sizeof(quick_hram)) == 0);
            memset(gb->video_ram, 0x87, sizeof(gb->video_ram));
            memset(gb->high_ram, 0x87, sizeof(gb->high_ram));
            assert(cupid_gb_reset(gb, CUPID_GB_RESET_HARD));
            assert(memcmp(gb->video_ram, expected, sizeof(expected)) == 0);
            assert(memcmp(gb->high_ram, expected_hram, sizeof(expected_hram)) == 0);

            gb->boot_rom_size = color ? 0x900 : 0x100;
            cupid_gb_start_boot_rom(gb);
            memset(expected, 0, sizeof(expected));
            assert(gb->boot_rom_enabled && memcmp(gb->video_ram, expected, sizeof(expected)) == 0);
            assert(memcmp(gb->high_ram, power_on_hram, sizeof(power_on_hram)) == 0);
            cupid_gb_clear_boot_rom(gb);
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_firmware_entry_vram_dma(void) {
    const uint8_t entry[16] = {0x3e, 0x00, 0xe0, 0x55, 0,    0x15, 0x26, 0x37,
                               0x48, 0x59, 0x6a, 0x7b, 0x8c, 0x9d, 0xae, 0xbf};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_CGB_D; ++model) {
        if (!cupid_gb_model_is_cgb((CupidGbModel)model))
            continue;
        load_generated_profile(gb, (CupidGbModel)model, 0, true);
        gb->boot_rom_size = 0x900;
        memcpy(gb->boot_rom, entry, sizeof(entry));
        /* This fixture requires an immediately pending HBlank request.
         * Establish that state explicitly instead of inheriting startup LY. */
        cupid_gb_write_u8(gb, 0xff40, 0);
        cupid_gb_write_u8(gb, 0xff40, 0x91);
        memset(gb->work_ram, 0x37, sizeof(gb->work_ram));
        memset(gb->video_ram, 0xa5, sizeof(gb->video_ram));

        cupid_gb_write_u8(gb, 0xff51, 0xc0);
        cupid_gb_write_u8(gb, 0xff53, 0x81);
        cupid_gb_write_u8(gb, 0xff54, 0x20);
        cupid_gb_write_u8(gb, 0xff55, 0x81);
        assert(gb->hdma_active && gb->hdma_pending);
        cupid_gb_enter_boot_rom(gb);
        assert(cupid_gb_read_u8(gb, 0xff55) == 0xff);
        /* Issue the first firmware request using the reset source and destination. */
        for (unsigned i = 0; i < 3; ++i)
            assert(cupid_gb_step(gb));
        assert(memcmp(gb->video_ram, entry, sizeof(entry)) == 0);
        for (unsigned i = sizeof(entry); i < sizeof(gb->video_ram); ++i)
            assert(gb->video_ram[i] == 0xa5);
        assert(cupid_gb_read_u8(gb, 0xff55) == 0xff);
    }
    cupid_gb_cleanup(gb);
    free(gb);
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
        const size_t stop_offset = length - test_state_post_ram_size(a) - SRAM_LENGTH_BYTES -
                                   IO_HISTORY_BYTES - ACCESSORY_DESCRIPTOR_BYTES -
                                   CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
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
                const size_t stop_offset =
                    empty_size - test_state_post_ram_size(a) - SRAM_LENGTH_BYTES -
                    IO_HISTORY_BYTES - ACCESSORY_DESCRIPTOR_BYTES -
                    CUPID_GB_DISPLAY_CLOCK_STATE_SIZE - CUPID_GB_PIXEL_TIMING_STATE_SIZE -
                    CUPID_GB_VRAM_BUS_STATE_SIZE - CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES -
                    INFRARED_HISTORY_BYTES - PULSE_DAC_BYTES - STOP_PORT_BYTES;
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

typedef struct StartupPresentation {
    uint64_t cycle;
    uint8_t type;
} StartupPresentation;

static void capture_first_startup_frame(const CupidGb *gb, uint8_t type, void *user) {
    StartupPresentation *first = user;
    if (!first->cycle)
        *first = (StartupPresentation){.cycle = gb->elapsed_cycles, .type = type};
}

static void require_native_color_startup_phase(const CupidGb *g) {
    assert(g->io_registers[0x44] == 144 && g->io_registers[0x41] == 0x81);
    assert(g->pixels.initialized && g->pixels.line == 144 && g->pixels.dot == 168);
    assert(g->pixels.window_armed && g->ppu_counter == 42);
    assert(g->display_clock.row == 144 &&
           g->display_clock.phase == CUPID_GB_DISPLAY_CLOCK_VBLANK_GAP);
    assert(g->display_clock.credit_half_dots == -568 && g->display_clock.frame_age_dots == 164);
    assert(g->display_cycles_since_frame == 166 && g->lcd_repeat_countdown == 67317 &&
           !g->lcd_frame_odd);
}

static void compare_color_phase_continuation(CupidGb *a, CupidGb *b) {
    size_t n = cupid_gb_state_size(a), written = 0;
    uint8_t *x = malloc(n), *y = malloc(n);
    assert(x && y);
    assert(cupid_gb_save_state(a, x, n, &written) == CUPID_GB_STATE_OK && written == n);
    assert(cupid_gb_load_state(b, x, n) == CUPID_GB_STATE_OK);
    for (unsigned i = 0; i < 128; ++i)
        assert(cupid_gb_step(a) && cupid_gb_step(b));
    assert(cupid_gb_save_state(a, x, n, &written) == CUPID_GB_STATE_OK);
    assert(cupid_gb_save_state(b, y, n, &written) == CUPID_GB_STATE_OK);
    assert(!memcmp(x, y, n));
    free(x);
    free(y);
}

static void test_cgb_startup_clock(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_CGB_A, CUPID_GB_MODEL_CGB_B, CUPID_GB_MODEL_CGB_C,
                                   CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB_E};

    const struct {
        uint8_t flag, checksum, fourth, old_licensee;
        char new_licensee[2];
        uint16_t divider;
        uint8_t apu_step;
        uint16_t compatibility_cycles;
        uint8_t pulse_position, pulse_length_ab;
        uint16_t pulse_timer;
    } cases[] = {
        {0x80, 0, 0, 0x01, {'0', '1'}, 8440, 1, 0, 6, 62, 148},
        {0xc0, 0, 0, 0x33, {'0', '1'}, 8472, 1, 0, 6, 62, 148},
        {0x80, 0xb3, 'B', 0x01, {'0', '1'}, 13432, 1, 0, 6, 64, 148},
        {0xc0, 0xb3, 'U', 0x01, {'0', '1'}, 11724, 1, 0, 6, 63, 148},
        {0x80, 0xb3, '?', 0x33, {'0', '1'}, 11900, 1, 0, 6, 63, 148},
        {0xc0, 0x46, '?', 0x01, {'0', '1'}, 11824, 1, 0, 6, 63, 148},
        {0x80, 0x5a, 'A', 0x01, {'0', '1'}, 12200, 1, 0, 6, 64, 148},
        {0xc0, 0, 0, 0x33, {'0', '2'}, 7872, 0, 0, 6, 62, 148},
        {0xc0, 0, 0, 0x33, {'Z', '1'}, 7836, 0, 0, 6, 62, 148},
        {0x80, 0, 0, 0x00, {'0', '1'}, 7840, 0, 0, 6, 62, 148},
        {0x00, 0, 'A', 0x00, {'0', '1'}, 9852, 1, 2012, 6, 62, 160},
        {0x00, 0, 'A', 0x01, {'0', '1'}, 10452, 1, 2012, 6, 62, 160},
        {0x00, 0x43, 'A', 0x01, {'0', '1'}, 13492, 1, 3036, 2, 63, 144},
        {0x00, 0x58, 'A', 0x01, {'0', '1'}, 13676, 1, 3220, 3, 63, 212},
        {0x00, 0x70, 'A', 0x01, {'0', '1'}, 12620, 1, 1660, 4, 63, 8},
    };

    uint8_t rom[32768] = {0};
    const uint8_t entry[] = {0xf0, 0x04, 0xe0, 0x80, 0xf0,
                             0x76, 0xe0, 0x81, 0x18, 0xfe}; /* DIV and PCM12 to HRAM. */
    memcpy(rom + 0x100, entry, sizeof(entry));
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_power_on_seed(gb, 0);
    for (size_t model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        cupid_gb_set_model(gb, models[model]);
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            rom[0x134] = (uint8_t)(cases[i].checksum - cases[i].fourth - cases[i].flag);
            rom[0x137] = cases[i].fourth;
            rom[0x143] = cases[i].flag;
            memcpy(rom + 0x144, cases[i].new_licensee, sizeof(cases[i].new_licensee));
            rom[0x14b] = cases[i].old_licensee;
            rom[0x14d] = 0;
            for (unsigned byte = 0x134; byte < 0x14d; ++byte)
                rom[0x14d] = (uint8_t)(rom[0x14d] - rom[byte] - 1u);
            assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
            for (unsigned reset = 0; reset < 3; ++reset) {
                const unsigned divider = cupid_gb_read_u8(gb, 0xff04) * 256u +
                                         gb->div_counter * 4u + gb->divider_subcycle;
                assert(divider == cases[i].divider);
                assert(gb->apu.fs_step == cases[i].apu_step && !gb->apu.fs_skip_div_event);
                const bool early_length =
                    gb->model == CUPID_GB_MODEL_CGB_A || gb->model == CUPID_GB_MODEL_CGB_B;
                assert(gb->apu.ch1_len == (early_length ? cases[i].pulse_length_ab : 64u));
                assert(gb->apu.ch1_duty_pos == cases[i].pulse_position);
                assert(gb->apu.ch1_freq == 1985u && gb->apu.ch1_timer == cases[i].pulse_timer);
                assert(gb->apu.ch1_vol == 0 && gb->apu.pulse_timing[0].envelope.locked);
                const unsigned position = 168u + cases[i].compatibility_cycles;
                const unsigned line = 144u + position / 456u;
                const unsigned dot = position % 456u;
                assert(cupid_gb_read_u8(gb, 0xff44) == line && gb->pixels.dot == dot);
                assert(cupid_gb_read_u8(gb, 0xff41) == 0x81 && gb->display_clock.row == line);
                assert(gb->display_clock.credit_half_dots == -2 * (452 - (int)dot));
                assert(gb->display_clock.frame_age_dots == 164u + cases[i].compatibility_cycles);
                assert(gb->lcd_repeat_countdown == 67485u - dot);
                const bool legacy_tilemap =
                    cases[i].compatibility_cycles &&
                    (cases[i].checksum == 0x43 || cases[i].checksum == 0x58);
                assert(gb->high_ram[0x76] == (gb->cgb_mode ? 0xc1u : 0));
                const unsigned compatibility_return_low = legacy_tilemap ? 0x03u : 0xf5u;
                assert(gb->high_ram[0x7a] == (gb->cgb_mode ? 0xd3u : compatibility_return_low));
                assert(gb->high_ram[0x7b] == (legacy_tilemap ? 6u : 5u));
                if (cases[i].compatibility_cycles) {
                    const unsigned hl = (unsigned)gb->cpu.h * 256u + gb->cpu.l;
                    assert(hl == (legacy_tilemap ? 0x991au : 0x007cu));
                }
                for (unsigned tile = 0; tile < 12; ++tile) {
                    assert(gb->video_ram[0x1904u + tile] == (legacy_tilemap ? 1u + tile : 0u));
                    assert(gb->video_ram[0x1924u + tile] == (legacy_tilemap ? 13u + tile : 0u));
                    assert(gb->video_ram[0x3904u + tile] == 0u);
                    assert(gb->video_ram[0x3924u + tile] == 0u);
                }
                assert(gb->video_ram[0x1910u] == (legacy_tilemap ? 25u : 0u));
                assert(gb->video_ram[0x3910u] == 0u);
                assert(cupid_gb_step(gb) && cupid_gb_step(gb));
                /* LDH samples the register after the opcode and immediate byte. */
                assert(cupid_gb_read_u8(gb, 0xff80) == (divider + 8u) / 256u);
                assert(cupid_gb_step(gb) && cupid_gb_step(gb));
                assert(cupid_gb_read_u8(gb, 0xff81) == 0 && cupid_gb_read_u8(gb, 0xff77) == 0);

                const unsigned timer_phase = divider + (unsigned)gb->elapsed_cycles;
                cupid_gb_write_u8(gb, 0xff05, 0x47);
                cupid_gb_write_u8(gb, 0xff07, 0x05);
                const unsigned ticks_to_tima = (16u - (timer_phase & 15u)) / 4u;
                cupid_gb_tick(gb, (uint16_t)(ticks_to_tima - 1u));
                assert(cupid_gb_read_u8(gb, 0xff05) == 0x47);
                cupid_gb_tick(gb, 1);
                assert(cupid_gb_read_u8(gb, 0xff05) == 0x48);
                cupid_gb_write_u8(gb, 0xff07, 0);

                /* A one-unit pulse length exposes which sequencer step is next. */
                cupid_gb_write_u8(gb, 0xff16, 0xbf);
                cupid_gb_write_u8(gb, 0xff17, 0xf0);
                cupid_gb_write_u8(gb, 0xff18, 0);
                cupid_gb_write_u8(gb, 0xff19, 0xc0);
                assert(cupid_gb_read_u8(gb, 0xff26) & 2u);
                const unsigned remaining =
                    8192u - ((divider + (unsigned)gb->elapsed_cycles) & 8191u);
                cupid_gb_tick(gb, (uint16_t)(remaining / 4u - 1u));
                assert(gb->apu.fs_step == cases[i].apu_step);
                cupid_gb_tick(gb, 1);
                assert(gb->apu.fs_step == cases[i].apu_step + 1u);
                assert(((cupid_gb_read_u8(gb, 0xff26) & 2u) != 0) == (cases[i].apu_step != 0));
                if (reset < 2)
                    assert(cupid_gb_reset(gb, reset ? CUPID_GB_RESET_HARD : CUPID_GB_RESET_QUICK));
            }
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_native_color_startup_phase(void) {
    static uint8_t rom[32768];
    const uint8_t entry[] = {0xf0, 0x44, 0xe0, 0x80, 0xf0, 0x41, 0xe0,
                             0x81, 0xf0, 0x0f, 0xe0, 0x82, 0x18, 0xfe};
    rom[0x100] = 0xc3;
    rom[0x101] = 0x50;
    rom[0x102] = 1;
    memcpy(rom + 0x150, entry, sizeof(entry));
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    assert(a && b);
    unsigned cases = 0;
    for (unsigned model = 0; model <= CUPID_GB_MODEL_CGB_D; ++model) {
        for (unsigned flag = 0; flag < 3; ++flag) {
            rom[0x143] = flag == 0 ? 0 : flag == 1 ? 0x80 : 0xc0;
            rom[0x14d] = 0;
            for (unsigned k = 0x134; k <= 0x14c; ++k)
                rom[0x14d] -= rom[k] + 1;
            cupid_gb_set_model(a, (CupidGbModel)model);
            cupid_gb_set_model(b, (CupidGbModel)model);
            cupid_gb_set_power_on_seed(a, 0);
            cupid_gb_set_power_on_seed(b, 0);
            assert(cupid_gb_load_rom(a, rom, sizeof(rom)) &&
                   cupid_gb_load_rom(b, rom, sizeof(rom)));
            const bool native =
                cupid_gb_model_is_cgb(a->model) && a->model != CUPID_GB_MODEL_CGB_0 && flag;
            if (native)
                require_native_color_startup_phase(a);
            else if (a->model == CUPID_GB_MODEL_CGB_0)
                assert(a->io_registers[0x44] == 0 && a->ppu_counter == 0);
            else if (cupid_gb_model_is_cgb(a->model))
                assert(a->io_registers[0x44] == 148 && a->pixels.dot == 356);
            compare_color_phase_continuation(a, b);
            for (unsigned kind = 0; kind < 2; ++kind) {
                a->work_ram[0x12] = 0x5a;
                a->high_ram[0x43] = 0xa6;
                assert(cupid_gb_reset(a, (CupidGbResetKind)kind));
                if (native) {
                    require_native_color_startup_phase(a);
                    if (kind == CUPID_GB_RESET_QUICK)
                        assert(a->work_ram[0x12] == 0x5a && a->high_ram[0x43] == 0xa6);
                    for (unsigned i = 0; i < 7; ++i)
                        assert(cupid_gb_step(a));
                    assert(cupid_gb_read_u8(a, 0xff80) == 144 &&
                           cupid_gb_read_u8(a, 0xff81) == 0x81 &&
                           cupid_gb_read_u8(a, 0xff82) == 0xe1);
                }
                compare_color_phase_continuation(a, b);
                ++cases;
            }
        }
    }
    /* Use complete machine cycles for guest-visible IRQ delivery and save states. */
    const CupidGbModel revisions[] = {CUPID_GB_MODEL_CGB_A, CUPID_GB_MODEL_CGB_B,
                                      CUPID_GB_MODEL_CGB_C, CUPID_GB_MODEL_CGB_D,
                                      CUPID_GB_MODEL_CGB_E};
    for (unsigned i = 0; i < sizeof(revisions) / sizeof(revisions[0]); ++i) {
        CupidGbModel rev = revisions[i];
        cupid_gb_set_model(a, rev);
        cupid_gb_set_model(b, rev);
        rom[0x143] = 0x80;
        rom[0x14d] = 0;
        for (unsigned k = 0x134; k <= 0x14c; ++k)
            rom[0x14d] -= rom[k] + 1;
        assert(cupid_gb_load_rom(a, rom, sizeof(rom)) && cupid_gb_load_rom(b, rom, sizeof(rom)));
        cupid_gb_write_u8(a, 0xff45, 145);
        cupid_gb_write_u8(a, 0xff41, 0x40);
        cupid_gb_write_u8(a, 0xff0f, 0);
        cupid_gb_tick(a, 71);
        assert(cupid_gb_read_u8(a, 0xff44) == 144 && !(a->interrupt_flags & 2));
        cupid_gb_tick(a, 1);
        assert(cupid_gb_read_u8(a, 0xff44) == 145 && !(a->interrupt_flags & 2));
        cupid_gb_tick(a, 1);
        assert((cupid_gb_read_u8(a, 0xff41) & 4) && (a->interrupt_flags & 2));
        compare_color_phase_continuation(a, b);
        assert(cupid_gb_reset(a, CUPID_GB_RESET_HARD));
        cupid_gb_write_u8(a, 0xff0f, 0);
        for (unsigned i = 0; i < 17514; ++i)
            cupid_gb_tick(a, 1);
        assert(!(a->interrupt_flags & 1));
        cupid_gb_tick(a, 1);
        assert(a->interrupt_flags & 1);
        compare_color_phase_continuation(a, b);
        assert(cupid_gb_reset(a, CUPID_GB_RESET_HARD));
        cupid_gb_tick(a, 984);
        assert(cupid_gb_read_u8(a, 0xff44) == 153);
        compare_color_phase_continuation(a, b);
        assert(cupid_gb_reset(a, CUPID_GB_RESET_HARD));
        cupid_gb_tick(a, 986);
        assert(cupid_gb_read_u8(a, 0xff44) == 0);
        compare_color_phase_continuation(a, b);
    }
    for (unsigned i = 0; i < sizeof(revisions) / sizeof(revisions[0]); ++i) {
        cupid_gb_set_model(a, revisions[i]);
        assert(cupid_gb_load_rom(a, rom, sizeof(rom)));
        require_native_color_startup_phase(a);
        a->boot_rom_size = 0x900;
        memset(a->boot_rom, 0, sizeof(a->boot_rom));
        a->boot_rom[0] = 0x18;
        a->boot_rom[1] = 0xfe;
        StartupPresentation first = {0};
        cupid_gb_set_presentation_callback(a, capture_first_startup_frame, &first);
        cupid_gb_start_boot_rom(a);
        assert(!a->pixels.initialized && !a->pixels.line && !a->pixels.dot &&
               !a->pixels.window_armed && !a->display_cycles_since_frame &&
               !a->lcd_repeat_countdown);
        assert(!a->display_clock.phase && !a->display_clock.frame_age_dots);
        while (!first.cycle && a->elapsed_cycles < 70240)
            assert(cupid_gb_step(a));
        assert(first.cycle == 70224 && first.type == CUPID_GB_FRAME_LCD_OFF);
        cupid_gb_set_presentation_callback(a, NULL, NULL);
        cupid_gb_clear_boot_rom(a);
    }
    /* Disable and re-enable LCDC through guest writes within the initial VBlank. */
    const uint8_t toggle_lcd[] = {0x3e, 0x00, 0xe0, 0x40, 0x3e, 0x91, 0xe0, 0x40, 0x40, 0x18, 0xfe};
    memcpy(rom + 0x150, toggle_lcd, sizeof(toggle_lcd));
    for (unsigned i = 0; i < sizeof(revisions) / sizeof(revisions[0]); ++i) {
        cupid_gb_set_model(a, revisions[i]);
        assert(cupid_gb_load_rom(a, rom, sizeof(rom)));
        StartupPresentation first = {0};
        cupid_gb_set_presentation_callback(a, capture_first_startup_frame, &first);
        while (!first.cycle && a->elapsed_cycles < 65732)
            assert(cupid_gb_step(a));
        assert(first.cycle == 65716 && first.type == CUPID_GB_FRAME_REPEAT);
        cupid_gb_set_presentation_callback(a, NULL, NULL);
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
    printf("Color startup phase: %u reset cases, snapshot continuation and IRQ checks passed.\n",
           cases);
}

static void test_cgb0_cartridge_entry_divider(void) {
    static const struct {
        uint8_t color, license, first, second;
        uint16_t phase;
    } headers[] = {
        {0x00, 0x33, 'Z', 'Z', 0x2884}, {0x00, 0x33, 'Y', '1', 0x2884},
        {0x00, 0x33, '0', 'Z', 0x28a8}, {0x00, 0x01, 0, 0, 0x2ae0},
        {0x80, 0x33, 'Z', 'Z', 0x20a8}, {0xc0, 0x33, 'Z', 'Z', 0x20a8},
        {0x00, 0x00, 0, 0, 0x2888},     {0x80, 0x01, 0, 0, 0x31b4},
        {0xc0, 0x33, '0', '1', 0x31d4},
    };

    /* The first LDH samples 124 T-cycles after entry. Subsequent samples occur
     * 256, 252, 256, 256 and 260 T-cycles later, straddling divider increments. */
    static const unsigned padding[] = {25, 58, 57, 58, 58, 59};
    static const unsigned read_cycles[] = {124, 380, 632, 888, 1144, 1404};
    for (unsigned header = 0; header < sizeof(headers) / sizeof(headers[0]); ++header) {
        uint8_t rom[32768] = {0};
        rom[0x100] = 0xc3;
        rom[0x101] = 0x50;
        rom[0x102] = 1;
        rom[0x143] = headers[header].color;
        rom[0x144] = headers[header].first;
        rom[0x145] = headers[header].second;
        rom[0x14b] = headers[header].license;
        for (unsigned i = 0x134; i <= 0x14c; ++i)
            rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
        unsigned end = 0x150;
        for (unsigned sample = 0; sample < sizeof(padding) / sizeof(padding[0]); ++sample) {
            end += padding[sample];
            rom[end++] = 0xf0;
            rom[end++] = 0x04;
            rom[end++] = 0xe0;
            rom[end++] = (uint8_t)(0x80u + sample);
        }
        CupidGb *gb = calloc(1, sizeof(*gb));
        assert(gb);
        cupid_gb_set_power_on_seed(gb, 0);
        cupid_gb_set_model(gb, CUPID_GB_MODEL_CGB_0);
        assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
        for (unsigned reset = 0; reset < 3; ++reset) {
            if (reset)
                assert(cupid_gb_reset(gb, reset == 1 ? CUPID_GB_RESET_QUICK : CUPID_GB_RESET_HARD));
            const unsigned phase = (unsigned)gb->io_registers[4] * 256u +
                                   (gb->div_counter & 63u) * 4u + gb->divider_subcycle;
            assert(phase == headers[header].phase);
            unsigned steps = 0;
            while (gb->cpu.pc != end && steps++ < 1000)
                assert(cupid_gb_step(gb));
            assert(gb->cpu.pc == end && steps < 1000);
            for (unsigned sample = 0; sample < sizeof(read_cycles) / sizeof(read_cycles[0]);
                 ++sample) {
                const uint8_t expected =
                    (uint8_t)((headers[header].phase + read_cycles[sample]) >> 8);
                assert(cupid_gb_read_u8(gb, (uint16_t)(0xff80u + sample)) == expected);
            }
        }
        cupid_gb_cleanup(gb);
        free(gb);
    }
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
    test_cartridge_entry_logo();
    test_color_cartridge_entry_work_ram();
    test_cgb0_cartridge_entry_divider();
    test_seeded_generations();
    test_model_distributions();
    test_color_work_ram_revisions();
    test_cold_firmware_reconstruction();
    test_boot_entry_preserves_memory();
    test_firmware_entry_peripheral_transactions();
    test_firmware_entry_oam_dma();
    test_firmware_entry_vram_dma();
    test_policy_and_state_lifetime();
    test_sgb_post_boot_state_continuation();
    test_legacy_sgb_startup_recovery();
    test_legacy_sgb_startup_signature();
    test_printer_state_policy();
    test_cgb_startup_clock();
    test_native_color_startup_phase();
    test_color_target_initialization();
    test_cgb_cartridge_entry_registers();
    puts("Startup memory and lifecycle checks passed.");
    return 0;
}

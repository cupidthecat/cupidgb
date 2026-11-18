/**
 * @file    test_gbs.c
 * @brief   Music loading, raw tracks, owned-input failures, and native program identity.
 * @author  cupidthecat
 * @date    2026-11-18
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

#include "../src/gb/gbs_internal.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/battery.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/gbs.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fixture {
    uint8_t *data;
    size_t length;
} Fixture;

static void word(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void dword(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = (uint8_t)(value >> (8u * i));
}

static Fixture fixture(uint16_t load, size_t payload) {
    Fixture result = {malloc(CUPID_GB_GBS_HEADER_SIZE + payload),
                      CUPID_GB_GBS_HEADER_SIZE + payload};
    assert(result.data);
    memset(result.data, 0xff, result.length);
    memset(result.data, 0, CUPID_GB_GBS_HEADER_SIZE);
    memcpy(result.data, "GBS\1", 4);
    result.data[4] = 3;
    result.data[5] = 2;
    const uint16_t init = load ? load : 0x200;
    word(result.data + 6, load);
    word(result.data + 8, init);
    word(result.data + 10, (uint16_t)(init + 0x10));
    word(result.data + 12, 0xdff0);
    memset(result.data + 16, 'T', 32);
    memset(result.data + 48, 0xe9, 32);
    memset(result.data + 80, 'C', 32);
    const uint8_t initialize[] = {0xea, 0x00, 0xc0, 0x3e, 0xa5, 0xea, 0x01, 0xc0, 0xc9};
    const uint8_t play[] = {0xfa, 0x02, 0xc0, 0x3c, 0xea, 0x02, 0xc0, 0xc9};
    const size_t offset = (size_t)(init - load);
    if (offset + 0x10u + sizeof(play) <= payload) {
        memcpy(result.data + CUPID_GB_GBS_HEADER_SIZE + offset, initialize, sizeof(initialize));
        memcpy(result.data + CUPID_GB_GBS_HEADER_SIZE + offset + 0x10, play, sizeof(play));
    }
    return result;
}

static CupidGb *machine(CupidGbModel model) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x47425326154));
    cupid_gb_set_model(gb, model);
    cupid_gb_init(gb);
    assert(gb->rom);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static bool all_bytes(const uint8_t *data, size_t length, uint8_t expected) {
    for (size_t i = 0; i < length; ++i)
        if (data[i] != expected)
            return false;
    return true;
}

static void execute_init(CupidGb *gb, uint8_t track) {
    for (unsigned i = 0; i < 100 && gb->work_ram[1] != 0xa5; ++i)
        assert(cupid_gb_step(gb));
    assert(gb->work_ram[0] == track && gb->work_ram[1] == 0xa5);
}

static uint8_t *save(const CupidGb *gb, size_t *size) {
    *size = cupid_gb_state_size(gb);
    uint8_t *bytes = malloc(*size);
    size_t written = 0;
    assert(bytes && *size > 128);
    assert(cupid_gb_save_state(gb, bytes, *size, &written) == CUPID_GB_STATE_OK);
    assert(written == *size && bytes[8] == CUPID_GB_STATE_VERSION);
    return bytes;
}

static void rehash(uint8_t *bytes, size_t length) {
    cupid_sha256_parts(bytes, 96, bytes + 128, length - 128, bytes + 96);
}

static void test_initialization(void) {
    static const uint8_t controls[] = {0, 4, 0x40, 0x44, 0x80, 0x84, 0xc0, 0xc4};
    static const uint8_t tracks[] = {0, 1, 254, 255};
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb *gb = machine((CupidGbModel)model);
        Fixture input = fixture(0x100, 0x200);
        /* Arbitrary music bytes at cartridge-header offsets must neither select
         * a peripheral mapper nor force CGB hardware into compatibility mode. */
        input.data[112 + 0x43] = 0;
        input.data[112 + 0x46] = 3;
        input.data[112 + 0x47] = 0x10;
        input.data[112 + 0x49] = 3;
        for (unsigned control = 0; control < sizeof(controls); ++control) {
            input.data[14] = 0x80;
            input.data[15] = controls[control];
            input.data[4] = control & 1u ? 0 : 3;
            input.data[5] = control & 1u ? 0 : 2;
            CupidGbGbsInfo info;
            const uint64_t generation = gb->rom_generation;
            assert(cupid_gb_load_gbs(gb, input.data, input.length, &info) == CUPID_GB_GBS_OK);
            assert(gb->rom_generation == generation + 1u && cupid_gb_gbs_info(gb, NULL));
            assert(gb->rom_size == 0x8000 && gb->gbs_payload_size == 0x200);
            assert(!memcmp(gb->gbs_header, input.data, 112));
            assert(!memcmp(gb->rom + 0x100, input.data + 112, 0x200));
            assert(gb->header.mbc_type == CUPID_GB_MBC3 && gb->header.cartridge_type == 0x11);
            assert(!gb->header.has_battery && !gb->header.has_timer && !gb->header.has_rumble);
            assert(!gb->cartridge_ram_size && !cupid_gb_battery_size(gb));
            assert(!gb->boot_rom_enabled && gb->cpu.pc == 0x61 && gb->cpu.sp == 0xdff0);
            assert(gb->cgb_mode == (model == CUPID_GB_MODEL_CGB));
            assert(gb->double_speed == ((controls[control] & 0x80) != 0));
            assert(gb->interrupt_enable == (controls[control] & 4u ? 4 : 1));
            assert(gb->io_registers[0x40] == 0x80 && gb->io_registers[0x06] == 0x80);
            assert((cupid_gb_read_u8(gb, 0xff07) & 7u) == (controls[control] & 7u));
            assert((cupid_gb_read_u8(gb, 0xff26) & 0x80u) && gb->io_registers[0x25] == 0xff);
            assert(gb->io_registers[0x24] == 0x77 && !gb->cpu.ime && gb->div_counter == 2);
            assert(all_bytes(gb->work_ram, sizeof(gb->work_ram), 0));
            assert(all_bytes(gb->high_ram, sizeof(gb->high_ram), 0));
            assert(all_bytes(gb->object_attribute_memory, sizeof(gb->object_attribute_memory), 0));
            assert(info.first_track == (uint8_t)(input.data[5] - 1u));
            assert(info.track_count == input.data[4] && info.load_address == 0x100);
            assert(info.init_address == 0x100 && info.play_address == 0x110);
            assert(info.title[32] == 0 && info.author[32] == 0 && info.copyright[32] == 0);
            assert((uint8_t)info.author[0] == 0xe9);
            const unsigned last_jump = controls[control] & 0x40u ? 0x50 : 0x38;
            for (unsigned vector = 0; vector <= 0x60; vector += 8) {
                assert(gb->rom[vector] == (vector <= last_jump ? 0xc3 : 0xc9));
                if (vector <= last_jump)
                    assert(gb->rom[vector + 1] == vector && gb->rom[vector + 2] == 1);
            }
            assert(!cupid_gb_sgb_startup_held(gb));
            if (model == CUPID_GB_MODEL_SGB || model == CUPID_GB_MODEL_SGB2)
                assert(gb->sgb.commands_disabled);
            execute_init(gb, info.first_track);
            for (unsigned i = 0; i < sizeof(tracks); ++i) {
                const uint64_t retained_generation = gb->rom_generation;
                uint8_t *const image = gb->rom;
                assert(cupid_gb_gbs_start_track(gb, tracks[i]) == CUPID_GB_GBS_OK);
                assert(gb->rom == image && gb->rom_generation == retained_generation);
                execute_init(gb, tracks[i]);
                CupidGbGbsInfo retained;
                assert(cupid_gb_gbs_info(gb, &retained) && !memcmp(&info, &retained, sizeof(info)));
            }
        }
        free(input.data);
        destroy(gb);
    }
}

static void test_boundaries(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_CGB);
    Fixture zero = fixture(0, 0x300);
    assert(cupid_gb_load_gbs(gb, zero.data, zero.length, NULL) == CUPID_GB_GBS_OK);
    assert(gb->cpu.pc == 0xdfe3 && gb->cpu.sp == 0xdfe3);
    assert(!memcmp(gb->rom, zero.data + 112, 0x300));
    execute_init(gb, 1);
    const uint16_t accepted[] = {0, 0x6e, 0x7fff};
    for (unsigned i = 0; i < sizeof(accepted) / sizeof(*accepted); ++i) {
        word(zero.data + 6, accepted[i]);
        assert(cupid_gb_load_gbs(gb, zero.data, 112, NULL) == CUPID_GB_GBS_OK);
        assert(gb->gbs_payload_size == 0 && gb->rom_size == 0x8000);
    }
    Fixture large = fixture(0, CUPID_GB_GBS_FILE_PAYLOAD_LIMIT + 1u);
    large.data[large.length - 1] = 0xa7;
    assert(cupid_gb_load_gbs(gb, large.data, large.length, NULL) == CUPID_GB_GBS_OK);
    assert(gb->rom_size == 0x800000 && gb->rom[0x400000] == 0xa7);
    assert(!cupid_gb_has_extended_mbc3(gb));
    for (unsigned bank = 0; bank < 256; ++bank) {
        cupid_gb_write_u8(gb, 0x2000, (uint8_t)bank);
        const unsigned selected = bank & 127u;
        assert(cupid_gb_selected_rom_bank(gb, false) == (selected ? selected : 1u));
    }
    free(large.data);
    free(zero.data);
    destroy(gb);
}

static unsigned allocation_count, fail_allocation;

static void *allocate(size_t length) {
    return ++allocation_count == fail_allocation ? NULL : malloc(length);
}

static void presentation(const CupidGb *gb, uint8_t kind, void *context) {
    (void)gb;
    (void)kind;
    ++*(unsigned *)context;
}

static void serial(const CupidGb *gb, uint8_t value, void *context) {
    (void)gb;
    (void)value;
    ++*(unsigned *)context;
}

static void assert_output_prefix(const CupidGbAudioOutput *before,
                                 const CupidGbAudioOutput *after) {
    assert(before->buf_write == after->buf_write);
    assert(!memcmp(before->buf, after->buf, sizeof(before->buf)));
    assert(before->sample_rate == after->sample_rate);
    assert(!memcmp(&before->policy, &after->policy, sizeof(before->policy)));
}

static void test_audio_initialization(void) {
    Fixture input = fixture(0x200, 0x100);
    for (int model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb *gb = machine((CupidGbModel)model);
        assert(cupid_gb_apu_set_sample_rate(gb, 48000));
        assert(cupid_gb_apu_set_filter_mode(gb, CUPID_GB_AUDIO_FILTER_OFF));
        assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) == CUPID_GB_GBS_OK);
        for (unsigned i = 0; i < 12000; ++i)
            assert(cupid_gb_step(gb));
        int16_t samples[CUPID_GB_APU_BUF_FRAMES * 2u];
        const size_t silent_frames = cupid_gb_apu_drain(gb, samples, CUPID_GB_APU_BUF_FRAMES);
        assert(silent_frames > 100);
        for (size_t i = 0; i < silent_frames * 2u; ++i)
            assert(samples[i] == 0);

        /* A running pulse leaves real coupling and reconstruction history.
         * Track entry must retain it while applying new routing/volume writes. */
        cupid_gb_write_u8(gb, 0xff11, 0x80);
        cupid_gb_write_u8(gb, 0xff12, 0xf0);
        cupid_gb_write_u8(gb, 0xff13, 0x87);
        cupid_gb_write_u8(gb, 0xff14, 0x83);
        cupid_gb_tick_apu(gb, 2000);
        CupidGbAudioOutput *output = malloc(sizeof(*output));
        assert(output);
        *output = gb->apu.output;
        assert(output->buf_write > 0);
        assert(cupid_gb_gbs_start_track(gb, 2) == CUPID_GB_GBS_OK);
        assert_output_prefix(output, &gb->apu.output);
        assert(output->sample_acc == gb->apu.output.sample_acc);
        assert(output->sample_fraction == gb->apu.output.sample_fraction);
        assert(output->render_cycles == gb->apu.output.render_cycles);
        assert(!memcmp(output->dac_charge, gb->apu.output.dac_charge, sizeof(output->dac_charge)));
        for (unsigned channel = 0; channel < CUPID_GB_AUDIO_CHANNELS; ++channel) {
            assert(gb->apu.output.channels[channel].input[0] == 120);
            assert(gb->apu.output.channels[channel].input[1] == 120);
        }
        const size_t frames = cupid_gb_apu_drain(gb, samples, CUPID_GB_APU_BUF_FRAMES);
        assert(frames == output->buf_write && !memcmp(samples, output->buf, frames * 4u));
        free(output);
        destroy(gb);
    }
    free(input.data);
}

static void test_transactions_and_host_state(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_DMG_ABC);
    Fixture input = fixture(0x200, 0x100);
    unsigned callbacks = 0;
    cupid_gb_set_presentation_callback(gb, presentation, &callbacks);
    cupid_gb_set_serial_output_callback(gb, serial, &callbacks);
    assert(cupid_gb_apu_set_sample_rate(gb, 48000));
    assert(cupid_gb_apu_set_channel_muted(gb, 1, true));
    gb->apu.output.buf_write = 2;
    gb->apu.output.buf[0] = 1234;
    gb->apu.output.buf[1] = -2345;
    CupidGbAudioOutput *output = malloc(sizeof(*output));
    CupidGb *snapshot = malloc(sizeof(*snapshot));
    assert(output && snapshot);
    *output = gb->apu.output;
    CupidGbGbsInfo info, unchanged_info;
    memset(&info, 0xa5, sizeof(info));
    unchanged_info = info;
    for (fail_allocation = 1; fail_allocation <= 2; ++fail_allocation) {
        allocation_count = 0;
        *snapshot = *gb;
        assert(cupid_gb_load_gbs_with_allocator(gb, input.data, input.length, &info, allocate) ==
               CUPID_GB_GBS_OUT_OF_MEMORY);
        assert(allocation_count == fail_allocation && !memcmp(gb, snapshot, sizeof(*gb)));
        assert(!memcmp(&info, &unchanged_info, sizeof(info)) && !callbacks);
    }
    assert(cupid_gb_load_gbs(gb, input.data, input.length, &info) == CUPID_GB_GBS_OK);
    assert_output_prefix(output, &gb->apu.output);
    assert(!callbacks);
    assert(gb->presentation_callback == presentation && gb->presentation_user_data == &callbacks);
    assert(gb->serial_output_callback == serial && gb->serial_output_user_data == &callbacks);
    fail_allocation = 1;
    allocation_count = 0;
    *snapshot = *gb;
    assert(cupid_gb_gbs_start_track_with_allocator(gb, 255, allocate) ==
           CUPID_GB_GBS_OUT_OF_MEMORY);
    assert(!memcmp(gb, snapshot, sizeof(*gb)) && !callbacks);
    assert(cupid_gb_gbs_start_track(gb, 255) == CUPID_GB_GBS_OK);
    assert_output_prefix(output, &gb->apu.output);
    assert(!callbacks);
    const uint64_t generation = gb->rom_generation;
    assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
    assert(cupid_gb_gbs_info(gb, NULL) && gb->rom_generation == generation);
    assert(cupid_gb_gbs_start_track(gb, 254) == CUPID_GB_GBS_OK);
    assert_output_prefix(output, &gb->apu.output);

    gb->model = (CupidGbModel)(CUPID_GB_MODEL_SGB2 + 1);
    *snapshot = *gb;
    assert(cupid_gb_gbs_start_track(gb, 0) == CUPID_GB_GBS_INVALID_ARGUMENT);
    assert(!memcmp(gb, snapshot, sizeof(*gb)));
    gb->model = CUPID_GB_MODEL_DMG_ABC;
    assert(!cupid_gb_gbs_info(gb, (CupidGbGbsInfo *)gb->rom));
    assert(!cupid_gb_gbs_info(gb, (CupidGbGbsInfo *)gb->work_ram));
    *snapshot = *gb;
    const uint16_t rejected[] = {1, 0x6d, 0x8000, 0xffff};
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(*rejected); ++i) {
        word(input.data + 6, rejected[i]);
        assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) ==
               CUPID_GB_GBS_INVALID_HEADER);
        assert(!memcmp(gb, snapshot, sizeof(*gb)));
    }
    word(input.data + 6, 0x200);
    for (unsigned version = 0; version < 3; version += 2) {
        input.data[3] = (uint8_t)version;
        assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) ==
               CUPID_GB_GBS_INVALID_HEADER);
        assert(!memcmp(gb, snapshot, sizeof(*gb)));
    }
    input.data[3] = 1;
    assert(cupid_gb_load_gbs(gb, input.data, 111, NULL) == CUPID_GB_GBS_INVALID_HEADER);
    assert(cupid_gb_load_gbs(gb, input.data, (size_t)CUPID_GB_MAX_ROM_SIZE + 113u, NULL) ==
           CUPID_GB_GBS_TOO_LARGE);
    assert(cupid_gb_load_gbs(gb, gb->rom + gb->rom_capacity - 8, 112, NULL) ==
           CUPID_GB_GBS_INVALID_ARGUMENT);
    assert(cupid_gb_load_gbs(gb, input.data, input.length, (CupidGbGbsInfo *)input.data) ==
           CUPID_GB_GBS_INVALID_ARGUMENT);
    assert(cupid_gb_load_gbs(gb, input.data, input.length, (CupidGbGbsInfo *)gb->work_ram) ==
           CUPID_GB_GBS_INVALID_ARGUMENT);
    assert(!memcmp(gb, snapshot, sizeof(*gb)));

    memcpy(gb->rom + 0x1000, input.data, input.length);
    assert(cupid_gb_load_gbs(gb, gb->rom + 0x1000, input.length, NULL) == CUPID_GB_GBS_OK);
    memcpy(gb->cartridge_ram, input.data, input.length);
    assert(cupid_gb_load_gbs(gb, gb->cartridge_ram, input.length, NULL) == CUPID_GB_GBS_OK);
    free(input.data);
    free(snapshot);
    free(output);
    destroy(gb);
}

static void test_heap_source(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_CGB);
    Fixture input = fixture(0x200, 0x100);
    /* A valid owned heap is sufficient to exercise source lifetime independently
     * of the peripheral protocol that normally populates this allocation. */
    gb->header.mbc_type = CUPID_GB_TPP1;
    gb->cartridge_ram_size = gb->cartridge_ram_capacity = 0x40000;
    gb->header.ram_bank_count = gb->ram_bank_count = 32;
    gb->cartridge_ram_heap = malloc(gb->cartridge_ram_capacity);
    assert(gb->cartridge_ram_heap);
    memset(gb->cartridge_ram_heap, 0x5a, gb->cartridge_ram_capacity);
    memcpy(gb->cartridge_ram_heap + 17, input.data, input.length);
    CupidGb *snapshot = malloc(sizeof(*snapshot));
    assert(snapshot);
    *snapshot = *gb;
    for (fail_allocation = 1; fail_allocation <= 2; ++fail_allocation) {
        allocation_count = 0;
        assert(cupid_gb_load_gbs_with_allocator(gb, gb->cartridge_ram_heap + 17, input.length, NULL,
                                                allocate) == CUPID_GB_GBS_OUT_OF_MEMORY);
        assert(!memcmp(gb, snapshot, sizeof(*gb)));
        assert(!memcmp(gb->cartridge_ram_heap + 17, input.data, input.length));
    }
    assert(cupid_gb_load_gbs(gb, gb->cartridge_ram_heap + 17, input.length, NULL) ==
           CUPID_GB_GBS_OK);
    assert(!gb->cartridge_ram_heap && !gb->cartridge_ram_capacity);
    execute_init(gb, 1);
    free(snapshot);
    free(input.data);
    destroy(gb);
}

static void test_state_continuation(void) {
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            CupidGb *gb = machine((CupidGbModel)model);
            Fixture input = fixture(0x200, 0x100);
            input.data[14] = 0xfe;
            input.data[15] = (uint8_t)(5u | (speed ? 0x80u : 0));
            assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) == CUPID_GB_GBS_OK);
            execute_init(gb, 1);
            size_t size;
            uint8_t *checkpoint = save(gb, &size);
            assert(checkpoint[8] == 23 && checkpoint[28] == 2 && checkpoint[24] == 0);
            assert(all_bytes(checkpoint + 64, 32, 0));
            CupidGbStateContext context;
            assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
            uint8_t *cached = malloc(size);
            assert(cached && cupid_gb_save_state_with_context(gb, cached, size, NULL, &context) ==
                                 CUPID_GB_STATE_OK);
            assert(!memcmp(cached, checkpoint, size));
            for (unsigned i = 0; i < 1000; ++i)
                assert(cupid_gb_step(gb));
            size_t future_size;
            uint8_t *future = save(gb, &future_size);
            assert(cupid_gb_gbs_start_track(gb, 255) == CUPID_GB_GBS_OK);
            execute_init(gb, 255);
            assert(cupid_gb_load_state_with_context(gb, checkpoint, size, &context) ==
                   CUPID_GB_STATE_OK);
            assert(gb->work_ram[0] == 1 && gb->gbs_header[5] == 2);
            for (unsigned i = 0; i < 1000; ++i)
                assert(cupid_gb_step(gb));
            assert(future_size == size &&
                   cupid_gb_save_state(gb, cached, size, NULL) == CUPID_GB_STATE_OK);
            assert(!memcmp(future, cached, size));
            free(future);
            free(cached);
            free(checkpoint);
            free(input.data);
            destroy(gb);
        }
    }
}

static void test_state_identity_and_flags(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_DMG_ABC);
    Fixture input = fixture(0x200, 0x100);
    input.data[15] = 0x80;
    assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) == CUPID_GB_GBS_OK);
    size_t size;
    uint8_t *state = save(gb, &size);
    uint8_t *altered = malloc(size);
    uint8_t *image = malloc(gb->rom_size);
    const size_t image_size = gb->rom_size;
    assert(altered && image);
    memcpy(image, gb->rom, image_size);
    const unsigned metadata_changes[] = {12, 14, 15, 16, 48, 80};
    for (unsigned i = 0; i < sizeof(metadata_changes) / sizeof(*metadata_changes); ++i) {
        input.data[metadata_changes[i]] ^= 1;
        assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) == CUPID_GB_GBS_OK);
        assert(!memcmp(gb->rom, image, image_size));
        assert(cupid_gb_load_state(gb, state, size) == CUPID_GB_STATE_WRONG_ROM);
        input.data[metadata_changes[i]] ^= 1;
    }
    Fixture longer = fixture(0x200, 0x101);
    memcpy(longer.data, input.data, input.length);
    assert(cupid_gb_load_gbs(gb, longer.data, longer.length, NULL) == CUPID_GB_GBS_OK);
    assert(!memcmp(gb->rom, image, image_size));
    assert(cupid_gb_load_state(gb, state, size) == CUPID_GB_STATE_WRONG_ROM);
    free(longer.data);
    assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) == CUPID_GB_GBS_OK);

    memcpy(altered, state, size);
    dword(altered + 8, 22);
    rehash(altered, size);
    assert(cupid_gb_load_state(gb, altered, size) == CUPID_GB_STATE_INVALID);
    dword(altered + 28, 0);
    rehash(altered, size);
    assert(cupid_gb_load_state(gb, altered, size) == CUPID_GB_STATE_WRONG_ROM);
    memcpy(altered, state, size);
    dword(altered + 28, 6);
    rehash(altered, size);
    assert(cupid_gb_load_state(gb, altered, size) == CUPID_GB_STATE_INVALID);
    memcpy(altered, state, size);
    dword(altered + 28, 3);
    rehash(altered, size);
    assert(cupid_gb_load_state(gb, altered, size) == CUPID_GB_STATE_WRONG_BOOT_ROM);

    assert(cupid_gb_load_rom(gb, image, image_size));
    assert(gb->program_kind == CUPID_GB_PROGRAM_CARTRIDGE && !gb->gbs_payload_size);
    assert(all_bytes(gb->gbs_header, sizeof(gb->gbs_header), 0) && !cupid_gb_gbs_info(gb, NULL));
    assert(cupid_gb_load_state(gb, state, size) == CUPID_GB_STATE_WRONG_ROM);
    /* Even a valid integrity hash and ordinary ROM digest cannot grant the
     * non-CGB speed exception through a state whose program tag was removed. */
    memcpy(altered, state, size);
    dword(altered + 28, 0);
    cupid_sha256(gb->rom, gb->rom_size, altered + 32);
    rehash(altered, size);
    assert(cupid_gb_load_state(gb, altered, size) == CUPID_GB_STATE_INVALID);
    size_t ordinary_size;
    uint8_t *ordinary = save(gb, &ordinary_size);
    uint8_t digest[32];
    cupid_sha256(gb->rom, gb->rom_size, digest);
    assert(!memcmp(ordinary + 32, digest, 32) && ordinary[28] == 0);
    dword(ordinary + 8, 22);
    rehash(ordinary, ordinary_size);
    assert(cupid_gb_load_state(gb, ordinary, ordinary_size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_gbs_start_track(gb, 1) == CUPID_GB_GBS_NOT_LOADED);
    free(ordinary);
    free(image);
    free(altered);
    free(state);
    free(input.data);
    destroy(gb);
}

static void test_halt_clock_domains(void) {
    Fixture input = fixture(0x200, 0x100);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned fast = 0; fast < 2; ++fast) {
            CupidGb *gb = machine((CupidGbModel)model);
            input.data[15] = (uint8_t)(5u | (fast ? 0x80u : 0u));
            assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) == CUPID_GB_GBS_OK);
            for (unsigned step = 0; step < 100 && !gb->cpu.halted; ++step)
                assert(cupid_gb_step(gb));
            assert(gb->cpu.halted && gb->cpu.just_halted);
            const uint16_t pc = gb->cpu.pc;
            const unsigned divider = (unsigned)gb->io_registers[4] * 256u + gb->div_counter * 4u;
            const uint8_t timer = cupid_gb_read_u8(gb, 0xff05);
            const uint64_t start = gb->elapsed_cycles;
            const uint64_t presentation = cupid_gb_presentation_cycles(gb);
            for (unsigned poll = 0; poll < 64; ++poll) {
                const uint64_t before = gb->elapsed_cycles;
                const bool phase = gb->speed_phase;
                const uint8_t alignment = gb->speed_switch_alignment;
                assert(cupid_gb_step(gb));
                /* HALT keeps the CPU divider running at the requested rate.
                 * Its first and steady polls deliver the same elapsed base time. */
                assert(gb->cpu.halted && !gb->cpu.just_halted && gb->cpu.pc == pc);
                assert(gb->elapsed_cycles - before == (fast ? 2u : 4u));
                assert(gb->speed_phase == (fast ? !phase : phase));
                assert(gb->speed_switch_alignment == ((alignment + (fast ? 4u : 0u)) & 7u));
            }
            const unsigned final_divider =
                (unsigned)gb->io_registers[4] * 256u + gb->div_counter * 4u;
            assert((uint16_t)(final_divider - divider) == 256u);
            assert((uint8_t)(cupid_gb_read_u8(gb, 0xff05) - timer) == 16u);
            assert(gb->elapsed_cycles - start == (fast ? 128u : 256u));
            assert(cupid_gb_presentation_cycles(gb) - presentation == (fast ? 128u : 256u));
            cupid_gb_write_u8(gb, 0xff0f, 4);
            const uint64_t wake = gb->elapsed_cycles;
            assert(cupid_gb_step(gb));
            assert(!gb->cpu.halted && gb->cpu.pc == pc);
            assert(gb->elapsed_cycles - wake == (fast ? 2u : 4u));
            destroy(gb);
        }
    }
    free(input.data);
}

static void test_last_vblank_line(void) {
    Fixture input = fixture(0x200, 0x100);
    const uint8_t comparisons[] = {0, 152, 153};
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned fast = 0; fast < 2; ++fast) {
            for (unsigned compare = 0; compare < sizeof(comparisons); ++compare) {
                CupidGb *gb = machine((CupidGbModel)model);
                input.data[15] = (uint8_t)(5u | (fast ? 0x80u : 0u));
                assert(cupid_gb_load_gbs(gb, input.data, input.length, NULL) == CUPID_GB_GBS_OK);
                const bool color = model == CUPID_GB_MODEL_CGB;
                const uint8_t lyc = comparisons[compare];
                cupid_gb_write_u8(gb, 0xff40, 0);
                cupid_gb_write_u8(gb, 0xff41, 0x40);
                cupid_gb_write_u8(gb, 0xff45, lyc);
                cupid_gb_write_u8(gb, 0xff40, 0x80);
                unsigned remaining = (color ? 452u : 453u) + 151u * 456u + 454u;
                while (remaining) {
                    const uint16_t span = remaining > UINT16_MAX ? UINT16_MAX : (uint16_t)remaining;
                    cupid_gb_tick_ppu_dots(gb, span);
                    remaining -= span;
                }
                assert(gb->pixels.line == 152 && gb->pixels.dot == 454);
                cupid_gb_write_u8(gb, 0xff0f, 0);
                cupid_gb_tick_ppu_dots(gb, 1);
                assert(cupid_gb_read_u8(gb, 0xff44) == 153);
                if (fast && !color) {
                    assert(((cupid_gb_read_u8(gb, 0xff41) & 4u) != 0) == (lyc == 152));
                    assert(!(gb->interrupt_flags & 2u));
                }
                cupid_gb_tick_ppu_dots(gb, 1);
                assert(gb->pixels.line == 153 && gb->pixels.dot == 0);
                for (unsigned dot = 0; dot <= 12; ++dot) {
                    const uint8_t ly = dot < (color || fast ? 5u : 3u) ? 153u : 0u;
                    assert(cupid_gb_read_u8(gb, 0xff44) == ly);
                    assert((cupid_gb_read_u8(gb, 0xff41) & 3u) == CUPID_GB_PPU_MODE_VBLANK);
                    if (dot >= 3 || (fast && !color)) {
                        const bool coincidence =
                            lyc == 0     ? dot >= 9
                            : lyc == 152 ? dot < 3
                                         : dot >= 3 && (dot < 5 || ((color || fast) && dot < 9));
                        assert(((cupid_gb_read_u8(gb, 0xff41) & 4u) != 0) == coincidence);
                    }
                    if (fast && !color) {
                        const bool requested = (lyc == 153 && dot >= 3) || (lyc == 0 && dot >= 9);
                        assert(((gb->interrupt_flags & 2u) != 0) == requested);
                    }
                    if (dot == 6 && fast && !color && lyc == 153) {
                        /* Both writes occur after LY is zero but before its
                         * comparison input changes. The second write raises IRQ. */
                        cupid_gb_write_u8(gb, 0xff45, 0);
                        assert(!gb->pixels.lyc_irq);
                        cupid_gb_write_u8(gb, 0xff0f, 0);
                        cupid_gb_write_u8(gb, 0xff45, 153);
                        assert(gb->pixels.lyc_irq && (gb->interrupt_flags & 2u));
                    }
                    if (dot < 12)
                        cupid_gb_tick_ppu_dots(gb, 1);
                }
                cupid_gb_tick_ppu_dots(gb, 443);
                assert(gb->pixels.line == 153 && gb->pixels.dot == 455);
                cupid_gb_tick_ppu_dots(gb, 1);
                assert(gb->pixels.line == 0 && gb->pixels.dot == 0);
                destroy(gb);
            }
        }
    }
    free(input.data);
}

static void test_retained_mapper_identity(void) {
    static const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    static const uint8_t banks[] = {0, 1, 2, 63, 127, 128, 129, 255};
    Fixture input = fixture(0, 0x400000);
    uint8_t *cartridge = calloc(1, 0x400000);
    assert(cartridge);
    cartridge[0x147] = 0x11;
    cartridge[0x148] = 7;
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model) {
        CupidGb *fresh = machine(models[model]);
        CupidGb *retained = machine(models[model]);
        assert(cupid_gb_load_gbs(fresh, input.data, input.length, NULL) == CUPID_GB_GBS_OK);
        assert(!fresh->mbc3_extended_history && !cupid_gb_has_extended_mbc3(fresh));
        assert(cupid_gb_load_rom(retained, cartridge, 0x400000));
        assert(retained->mbc3_extended_history && cupid_gb_has_extended_mbc3(retained));
        /* The host latch survives an intervening small cartridge; music does not
         * derive it from either the current cartridge size or its own payload. */
        assert(cupid_gb_load_rom(retained, cartridge, 0x8000));
        assert(retained->mbc3_extended_history && !cupid_gb_has_extended_mbc3(retained));
        assert(cupid_gb_load_gbs(retained, input.data, input.length, NULL) == CUPID_GB_GBS_OK);
        assert(retained->mbc3_extended_history && cupid_gb_has_extended_mbc3(retained));
        assert(fresh->rom_size == retained->rom_size);
        assert(!memcmp(fresh->rom, retained->rom, fresh->rom_size));
        assert(!memcmp(fresh->gbs_header, retained->gbs_header, sizeof(fresh->gbs_header)));
        for (unsigned i = 0; i < sizeof(banks); ++i) {
            cupid_gb_write_u8(fresh, 0x2000, banks[i]);
            cupid_gb_write_u8(retained, 0x2000, banks[i]);
            const unsigned narrow = banks[i] & 127u;
            assert(cupid_gb_selected_rom_bank(fresh, false) == (narrow ? narrow : 1u));
            assert(cupid_gb_selected_rom_bank(retained, false) == (banks[i] ? banks[i] : 1u));
        }
        CupidGbStateContext narrow_context, wide_context;
        assert(cupid_gb_state_context_init(&narrow_context, fresh) == CUPID_GB_STATE_OK);
        assert(cupid_gb_state_context_init(&wide_context, retained) == CUPID_GB_STATE_OK);
        assert(memcmp(narrow_context.rom_digest, wide_context.rom_digest, 32));
        size_t narrow_size, wide_size;
        uint8_t *narrow_state = save(fresh, &narrow_size);
        uint8_t *wide_state = save(retained, &wide_size);
        assert(cupid_gb_load_state(retained, narrow_state, narrow_size) ==
               CUPID_GB_STATE_WRONG_ROM);
        assert(cupid_gb_load_state_with_context(retained, narrow_state, narrow_size,
                                                &wide_context) == CUPID_GB_STATE_WRONG_ROM);
        assert(cupid_gb_load_state(fresh, wide_state, wide_size) == CUPID_GB_STATE_WRONG_ROM);
        assert(cupid_gb_load_state_with_context(fresh, narrow_state, narrow_size, &wide_context) ==
               CUPID_GB_STATE_WRONG_ROM);
        assert(cupid_gb_load_state_with_context(fresh, narrow_state, narrow_size,
                                                &narrow_context) == CUPID_GB_STATE_OK);
        assert(cupid_gb_reset(retained, CUPID_GB_RESET_QUICK));
        assert(retained->mbc3_extended_history);
        assert(cupid_gb_gbs_start_track(retained, 255) == CUPID_GB_GBS_OK);
        assert(cupid_gb_load_state_with_context(retained, wide_state, wide_size, &wide_context) ==
               CUPID_GB_STATE_OK);
        assert(cupid_gb_load_rom(fresh, cartridge, 0x8000));
        assert(cupid_gb_load_rom(retained, cartridge, 0x8000));
        assert(!fresh->mbc3_extended_history && retained->mbc3_extended_history);
        assert(cupid_gb_load_state_with_context(retained, wide_state, wide_size, &wide_context) ==
               CUPID_GB_STATE_WRONG_ROM);
        assert(cupid_gb_state_context_init(&narrow_context, fresh) == CUPID_GB_STATE_OK);
        assert(cupid_gb_state_context_init(&wide_context, retained) == CUPID_GB_STATE_OK);
        assert(!memcmp(narrow_context.rom_digest, wide_context.rom_digest, 32));
        free(wide_state);
        free(narrow_state);
        cupid_gb_cleanup(retained);
        assert(!retained->mbc3_extended_history);
        free(retained);
        destroy(fresh);
    }
    free(cartridge);
    free(input.data);
}

int main(void) {
    test_initialization();
    test_boundaries();
    test_audio_initialization();
    test_transactions_and_host_state();
    test_heap_source();
    test_state_continuation();
    test_state_identity_and_flags();
    test_halt_clock_domains();
    test_last_vblank_line();
    test_retained_mapper_identity();
    puts("GBS loading, track, ownership and native-state checks passed.");
    return 0;
}

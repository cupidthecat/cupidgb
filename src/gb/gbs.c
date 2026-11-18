/**
 * @file    gbs.c
 * @brief   Transactional GBS image preparation, native music entry, and program identity.
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

#include "cupid/gb/gbs.h"

#include "cartridge_internal.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/sgb.h"
#include "cupid/gb/sgb_startup.h"
#include "gbs_internal.h"
#include "startup_internal.h"

#include <stdlib.h>
#include <string.h>

enum { GBS_ENTRY_ADDRESS = 0x61, GBS_ENTRY_BYTES = 13 };

static uint16_t read_word(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8);
}

static bool header_valid(const uint8_t *header) {
    const uint16_t load = read_word(header + 6);
    return header[0] == 'G' && header[1] == 'B' && header[2] == 'S' && header[3] == 1 &&
           (!load || (load >= GBS_ENTRY_ADDRESS + GBS_ENTRY_BYTES && load < 0x8000));
}

static size_t prepared_size(size_t payload, uint16_t load) {
    if (payload > CUPID_GB_MAX_ROM_SIZE - (size_t)load)
        return 0;
    size_t size = CUPID_GB_MIN_ROM_CAPACITY;
    while (size < payload + load)
        size *= 2u;
    return size;
}

static bool overlaps(const void *a, size_t a_size, const void *b, size_t b_size) {
    if (!a || !b || !a_size || !b_size)
        return false;
    const uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;
    return first < second ? second - first < a_size : first - second < b_size;
}

static bool info_output_valid(const CupidGb *gb, CupidGbGbsInfo *info, const uint8_t *data,
                              size_t length) {
    return !info ||
           (!overlaps(info, sizeof(*info), gb, sizeof(*gb)) &&
            !overlaps(info, sizeof(*info), data, length) &&
            !overlaps(info, sizeof(*info), gb->rom, gb->rom_capacity) &&
            !overlaps(info, sizeof(*info), gb->cartridge_ram_heap, gb->cartridge_ram_capacity) &&
            !overlaps(info, sizeof(*info), gb->mbc6_flash, CUPID_GB_MBC6_STORAGE_SIZE));
}

static void decode_info(const uint8_t *header, CupidGbGbsInfo *info) {
    memset(info, 0, sizeof(*info));
    info->track_count = header[4];
    info->first_track = (uint8_t)(header[5] - 1u);
    info->load_address = read_word(header + 6);
    info->init_address = read_word(header + 8);
    info->play_address = read_word(header + 10);
    info->stack_pointer = read_word(header + 12);
    info->timer_modulo = header[14];
    info->timer_control = header[15];
    memcpy(info->title, header + 16, 32);
    memcpy(info->author, header + 48, 32);
    memcpy(info->copyright, header + 80, 32);
}

bool cupid_gb_program_valid(const CupidGb *gb) {
    if (!gb)
        return false;
    if (gb->program_kind == CUPID_GB_PROGRAM_CARTRIDGE) {
        static const uint8_t empty[CUPID_GB_GBS_HEADER_SIZE] = {0};
        return !gb->gbs_payload_size && !memcmp(gb->gbs_header, empty, sizeof(empty));
    }
    if (gb->program_kind != CUPID_GB_PROGRAM_GBS || !gb->loaded || !gb->rom ||
        !header_valid(gb->gbs_header))
        return false;
    const size_t size = prepared_size(gb->gbs_payload_size, read_word(gb->gbs_header + 6));
    return size && size == gb->rom_size && size <= gb->rom_capacity &&
           gb->rom_bank_count == size / 0x4000u && gb->header.mbc_type == CUPID_GB_MBC3 &&
           gb->header.cartridge_type == 0x11 && !gb->header.has_battery && !gb->header.has_timer &&
           !gb->header.has_rumble && !gb->cartridge_ram_size && !gb->cartridge_ram_heap &&
           !gb->cartridge_ram_capacity && !gb->ram_bank_count;
}

bool cupid_gb_gbs_info(const CupidGb *gb, CupidGbGbsInfo *info) {
    if (!gb || gb->program_kind != CUPID_GB_PROGRAM_GBS || !cupid_gb_program_valid(gb) ||
        !info_output_valid(gb, info, NULL, 0))
        return false;
    if (info)
        decode_info(gb->gbs_header, info);
    return true;
}

/* The bootstrap is executable guest code: it calls init, waits for the selected
 * interrupt, clears IF, calls play, and returns to HALT. Its exact placement is
 * part of the format's RST/interrupt entry convention. */
static void write_call(uint8_t *output, uint16_t address) {
    output[0] = 0xcdu;
    output[1] = (uint8_t)address;
    output[2] = (uint8_t)(address >> 8);
}

static void make_entry(const uint8_t *header, uint8_t entry[GBS_ENTRY_BYTES]) {
    write_call(entry, read_word(header + 8));
    entry[3] = 0x76;
    entry[4] = 0x00; /* The post-HALT fetch remains a real guest instruction. */
    entry[5] = 0xaf;
    entry[6] = 0xe0;
    entry[7] = 0x0f;
    write_call(entry + 8, read_word(header + 10));
    entry[11] = 0x18;
    entry[12] = 0xf6;
}

static void prepare_image(uint8_t *rom, size_t size, const uint8_t *header, const uint8_t *payload,
                          size_t length) {
    const uint16_t load = read_word(header + 6);
    memset(rom, 0xff, size);
    memcpy(rom + load, payload, length);
    if (!load)
        return;
    const unsigned last_jump = header[15] & 0x40u ? 0x50u : 0x38u;
    for (unsigned vector = 0; vector <= 0x60; vector += 8) {
        if (vector <= last_jump) {
            const uint16_t destination = (uint16_t)(load + vector);
            rom[vector] = 0xc3;
            rom[vector + 1] = (uint8_t)destination;
            rom[vector + 2] = (uint8_t)(destination >> 8);
        } else {
            rom[vector] = 0xc9;
        }
    }
    make_entry(header, rom + GBS_ENTRY_ADDRESS);
}

static void install_descriptor(CupidGb *next, const uint8_t *header, size_t payload, size_t size) {
    next->program_kind = CUPID_GB_PROGRAM_GBS;
    memcpy(next->gbs_header, header, sizeof(next->gbs_header));
    next->gbs_payload_size = payload;
    next->rom_size = size;
    next->rom_bank_count = size / 0x4000u;
    memset(&next->header, 0, sizeof(next->header));
    next->header.mbc_type = CUPID_GB_MBC3;
    next->header.cartridge_type = 0x11;
    next->header.rom_bank_count = next->rom_bank_count;
    memcpy(next->header.title, header + 16, 16);
    /* Capability bytes remain available for host model policy without feeding
     * music payload through cartridge-type, SRAM, clock or battery detection. */
    next->header.cgb_flag = next->rom[0x143];
    next->header.sgb_flag = next->rom[0x146];
    next->header.old_licensee_code = next->rom[0x14b];
    memcpy(next->header.new_licensee_code, next->rom + 0x144, 2);
    next->loaded = true;
    cupid_gb_cartridge_reset_controls(next);
}

static void initialize_track(CupidGb *next, const CupidGb *previous, uint8_t track) {
    cupid_gb_startup_hardware(next);
    next->apu.output = previous->apu.output;
    if (!previous->loaded) {
        /* An unloaded instance has only the approximate post-boot waveform.
         * Music executes no firmware, so begin with uncharged reconstruction.
         * Keep host policy and any already queued PCM independently of it. */
        const CupidGbAudioPolicy policy = next->apu.output.policy;
        const uint32_t rate = next->apu.output.sample_rate;
        memset(&next->apu.output, 0, offsetof(CupidGbAudioOutput, buf));
        next->apu.output.policy = policy;
        next->apu.output.sample_rate = rate;
    }
    /* These control writes must update the retained reconstruction history.
     * Restoring output afterward would erase their real voltage transitions. */
    cupid_gb_write_u8(next, 0xff40, 0x80);
    cupid_gb_write_u8(next, 0xff07, next->gbs_header[15]);
    cupid_gb_write_u8(next, 0xff06, next->gbs_header[14]);
    cupid_gb_write_u8(next, 0xff26, 0x80);
    cupid_gb_write_u8(next, 0xff25, 0xff);
    cupid_gb_write_u8(next, 0xff24, 0x77);
    memset(next->work_ram, 0, sizeof(next->work_ram));
    memset(next->high_ram, 0, sizeof(next->high_ram));
    memset(next->object_attribute_memory, 0, sizeof(next->object_attribute_memory));
    cupid_gb_write_u8(next, 0xffff, next->gbs_header[14] || next->gbs_header[15] ? 4 : 1);
    next->double_speed = (next->gbs_header[15] & 0x80u) != 0;
    const uint16_t stack = read_word(next->gbs_header + 12);
    if (read_word(next->gbs_header + 6)) {
        next->cpu.sp = stack;
        next->cpu.pc = GBS_ENTRY_ADDRESS;
    } else {
        uint8_t entry[GBS_ENTRY_BYTES];
        make_entry(next->gbs_header, entry);
        next->cpu.pc = next->cpu.sp = (uint16_t)(stack - GBS_ENTRY_BYTES);
        for (unsigned i = 0; i < sizeof(entry); ++i)
            cupid_gb_write_u8(next, (uint16_t)(next->cpu.pc + i), entry[i]);
    }
    next->boot_rom_enabled = false;
    next->cpu.a = track;
    /* Music starts immediately on adapter hardware and does not send a cartridge
     * permission/header stream or run the adapter's startup animation. */
    cupid_gb_sgb_startup_reset(next, false);
    if (cupid_gb_sgb_host_present(next))
        next->sgb.commands_disabled = true;
    next->interrupt_enable = next->gbs_header[15] & 4u ? 4 : 1;
}

static void prepare_candidate(CupidGb *next, const CupidGb *previous, uint8_t *image,
                              size_t capacity, const uint8_t *header, size_t payload, size_t size,
                              uint8_t track) {
    memset(next, 0, sizeof(*next));
    next->model = previous->model;
    next->rom = image;
    next->rom_capacity = capacity;
    next->rom_generation = previous->rom_generation;
    next->mbc3_extended_history = previous->mbc3_extended_history;
    next->power_on = previous->power_on;
    next->rtc_policy = previous->rtc_policy;
    /* Candidate initialization owns no observer, device, writable cartridge RAM,
     * or caller buffer. Once storage exists, preparation cannot affect the live session. */
    cupid_gb_init(next);
    memcpy(next->boot_rom, previous->boot_rom, sizeof(next->boot_rom));
    next->boot_rom_size = previous->boot_rom_size;
    install_descriptor(next, header, payload, size);
    initialize_track(next, previous, track);
    cupid_gb_reset_restore_host(next, previous);
}

CupidGbGbsResult cupid_gb_load_gbs_with_allocator(CupidGb *gb, const uint8_t *data, size_t length,
                                                  CupidGbGbsInfo *info, void *(*allocate)(size_t)) {
    if (!gb || !data || !allocate || gb->model < CUPID_GB_MODEL_DMG_ABC ||
        gb->model > CUPID_GB_MODEL_SGB_PAL || !cupid_gb_cartridge_storage_valid(gb) ||
        !info_output_valid(gb, info, data, length))
        return CUPID_GB_GBS_INVALID_ARGUMENT;
    if (length < CUPID_GB_GBS_HEADER_SIZE)
        return CUPID_GB_GBS_INVALID_HEADER;
    if (length - CUPID_GB_GBS_HEADER_SIZE > CUPID_GB_MAX_ROM_SIZE)
        return CUPID_GB_GBS_TOO_LARGE;
    if (!cupid_gb_cartridge_source_fits(gb, data, length))
        return CUPID_GB_GBS_INVALID_ARGUMENT;
    uint8_t header[CUPID_GB_GBS_HEADER_SIZE];
    memcpy(header, data, sizeof(header));
    if (!header_valid(header))
        return CUPID_GB_GBS_INVALID_HEADER;
    const size_t payload = length - sizeof(header);
    const size_t size = prepared_size(payload, read_word(header + 6));
    if (!size)
        return CUPID_GB_GBS_TOO_LARGE;
    uint8_t *image = allocate(size);
    if (!image)
        return CUPID_GB_GBS_OUT_OF_MEMORY;
    CupidGb *next = allocate(sizeof(*next));
    if (!next) {
        free(image);
        return CUPID_GB_GBS_OUT_OF_MEMORY;
    }
    prepare_image(image, size, header, data + sizeof(header), payload);
    prepare_candidate(next, gb, image, size, header, payload, size, (uint8_t)(header[5] - 1u));
    ++next->rom_generation;
    uint8_t *const previous_rom = gb->rom;
    cupid_gb_reset_commit(gb, next);
    free(previous_rom);
    if (info)
        decode_info(gb->gbs_header, info);
    return CUPID_GB_GBS_OK;
}

CupidGbGbsResult cupid_gb_load_gbs(CupidGb *gb, const uint8_t *data, size_t length,
                                   CupidGbGbsInfo *info) {
    return cupid_gb_load_gbs_with_allocator(gb, data, length, info, malloc);
}

CupidGbGbsResult cupid_gb_gbs_start_track_with_allocator(CupidGb *gb, uint8_t track,
                                                         void *(*allocate)(size_t)) {
    if (!gb || !allocate || gb->model < CUPID_GB_MODEL_DMG_ABC ||
        gb->model > CUPID_GB_MODEL_SGB_PAL)
        return CUPID_GB_GBS_INVALID_ARGUMENT;
    if (!cupid_gb_gbs_info(gb, NULL))
        return CUPID_GB_GBS_NOT_LOADED;
    CupidGb *next = allocate(sizeof(*next));
    if (!next)
        return CUPID_GB_GBS_OUT_OF_MEMORY;
    prepare_candidate(next, gb, gb->rom, gb->rom_capacity, gb->gbs_header, gb->gbs_payload_size,
                      gb->rom_size, track);
    cupid_gb_reset_commit(gb, next);
    return CUPID_GB_GBS_OK;
}

CupidGbGbsResult cupid_gb_gbs_start_track(CupidGb *gb, uint8_t track) {
    return cupid_gb_gbs_start_track_with_allocator(gb, track, malloc);
}

void cupid_gb_program_digest(const CupidGb *gb, uint8_t digest[32]) {
    if (gb->program_kind != CUPID_GB_PROGRAM_GBS) {
        cupid_sha256(gb->rom, gb->rom_size, digest);
        return;
    }
    static const char domain[] = "cupidgb/gbs-program/v1";
    const uint8_t wiring = gb->mbc3_extended_history ? 1u : 0u;
    uint8_t length[8];
    for (unsigned i = 0; i < sizeof(length); ++i)
        length[i] = (uint8_t)((uint64_t)gb->gbs_payload_size >> (i * 8u));
    CupidSha256 hash;
    cupid_sha256_init(&hash);
    cupid_sha256_update(&hash, domain, sizeof(domain));
    cupid_sha256_update(&hash, &wiring, sizeof(wiring));
    cupid_sha256_update(&hash, length, sizeof(length));
    cupid_sha256_update(&hash, gb->gbs_header, sizeof(gb->gbs_header));
    cupid_sha256_update(&hash, gb->rom, gb->rom_size);
    cupid_sha256_final(&hash, digest);
}

const char *cupid_gb_gbs_result_name(CupidGbGbsResult result) {
    switch (result) {
        case CUPID_GB_GBS_OK:
            return "GBS program loaded";
        case CUPID_GB_GBS_INVALID_ARGUMENT:
            return "Invalid GBS input or output buffer";
        case CUPID_GB_GBS_INVALID_HEADER:
            return "Invalid GBS header";
        case CUPID_GB_GBS_TOO_LARGE:
            return "GBS program exceeds supported image size";
        case CUPID_GB_GBS_OUT_OF_MEMORY:
            return "Could not allocate GBS program storage";
        case CUPID_GB_GBS_NOT_LOADED:
            return "No GBS program is loaded";
        default:
            return "Unknown GBS result";
    }
}

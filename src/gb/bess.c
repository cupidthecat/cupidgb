/**
 * @file    bess.c
 * @brief   Validated import of portable cartridge checkpoints.
 * @author  cupidthecat
 * @date    2026-11-19
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

#include "cupid/gb/bess.h"

#include "audio_output.h"
#include "audio_state.h"
#include "cartridge_internal.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/input.h"
#include "oam_bus.h"

#include <stdlib.h>
#include <string.h>

enum {
    BESS_HEADER_SIZE = 8,
    BESS_FOOTER_SIZE = 8,
    BESS_CORE_SIZE = 0xd0,
    BESS_INFO_SIZE = 18,
    BESS_RTC_SIZE = 48,
    BESS_XOAM_SIZE = 96,
    BESS_PORTS = 0x18,
    BESS_MEMORY = 0x98
};

typedef struct BessBlock {
    const uint8_t *bytes;
    size_t size;
} BessBlock;

typedef struct BessState {
    BessBlock info, core, mapper, clock, extra_oam;
    BessBlock regions[7];
    size_t block_begin, block_end;
} BessState;

static uint16_t bess_word(const uint8_t *data) {
    return (uint16_t)((unsigned)data[0] | (unsigned)data[1] << 8);
}

static uint32_t bess_long(const uint8_t *data) {
    return (uint32_t)bess_word(data) | (uint32_t)bess_word(data + 2) << 16;
}

bool cupid_gb_bess_has_footer(const uint8_t *data, size_t length) {
    return data && length >= BESS_FOOTER_SIZE && !memcmp(data + length - 4, "BESS", 4);
}

static BessBlock *known_block(BessState *state, const uint8_t *name) {
    if (!memcmp(name, "CORE", 4))
        return &state->core;
    if (!memcmp(name, "INFO", 4))
        return &state->info;
    if (!memcmp(name, "MBC ", 4))
        return &state->mapper;
    if (!memcmp(name, "RTC ", 4))
        return &state->clock;
    if (!memcmp(name, "XOAM", 4))
        return &state->extra_oam;
    return NULL;
}

static bool read_blocks(BessState *state, const uint8_t *data, size_t limit) {
    size_t cursor = bess_long(data + limit);
    state->block_begin = cursor;
    while (cursor <= limit && limit - cursor >= BESS_HEADER_SIZE) {
        const uint8_t *header = data + cursor;
        const size_t size = bess_long(header + 4);
        cursor += BESS_HEADER_SIZE;
        if (size > limit - cursor)
            return false;
        if (!memcmp(header, "END ", 4)) {
            state->block_end = cursor;
            return size == 0 && state->core.size >= BESS_CORE_SIZE;
        }
        BessBlock *block = known_block(state, header);
        if (block) {
            if (block->bytes)
                return false;
            *block = (BessBlock){data + cursor, size};
        }
        cursor += size;
    }
    return false;
}

static bool read_regions(BessState *state, const uint8_t *data, size_t limit) {
    for (unsigned index = 0; index < sizeof(state->regions) / sizeof(state->regions[0]); ++index) {
        const uint8_t *desc = state->core.bytes + BESS_MEMORY + index * 8u;
        const size_t size = bess_long(desc);
        const size_t offset = bess_long(desc + 4);
        if (!size)
            continue;
        if (offset > limit || size > limit - offset ||
            (offset < state->block_end && offset + size > state->block_begin))
            return false;
        state->regions[index] = (BessBlock){data + offset, size};
    }
    return true;
}

static bool matches_model(CupidGbModel model, const uint8_t *id) {
    if (id[3] != ' ')
        return false;
    if (cupid_gb_model_is_cgb(model))
        return id[0] == 'C' && (id[1] == 'C' || id[1] == ' ') &&
               (id[2] == ' ' || id[2] == '0' || (id[2] >= 'A' && id[2] <= 'E'));
    if (id[0] != 'G')
        return false;
    switch (id[1]) {
        case 'D':
            return id[2] == ' ' || id[2] == '0' || (id[2] >= 'A' && id[2] <= 'C');
        case 'M':
        case ' ':
            return id[2] == ' ';
        default:
            return false;
    }
}

static CupidGbStateResult validate(const CupidGb *gb, const BessState *state) {
    const uint8_t *core = state->core.bytes;
    if (bess_word(core) != 1)
        return CUPID_GB_STATE_UNSUPPORTED_VERSION;
    if (!matches_model(gb->model, core + 4))
        return CUPID_GB_STATE_WRONG_MODEL;
    if (state->info.bytes) {
        if (state->info.size != BESS_INFO_SIZE)
            return CUPID_GB_STATE_INVALID;
        if (memcmp(state->info.bytes, gb->rom + 0x134, 16) ||
            memcmp(state->info.bytes + 16, gb->rom + 0x14e, 2))
            return CUPID_GB_STATE_WRONG_ROM;
    }
    if (core[20] > 1 || core[22] > 2 || core[23] || core[BESS_PORTS + 0x50] > 1 ||
        state->mapper.size % 3 || (state->clock.bytes && state->clock.size != BESS_RTC_SIZE) ||
        (state->extra_oam.bytes && state->extra_oam.size != BESS_XOAM_SIZE) ||
        (!cupid_gb_model_is_cgb(gb->model) && (state->regions[5].size || state->regions[6].size)))
        return CUPID_GB_STATE_INVALID;
    if (!core[BESS_PORTS + 0x50])
        return CUPID_GB_STATE_UNSUPPORTED_FEATURE;
    for (size_t index = 0; index < state->mapper.size; index += 3) {
        const uint16_t address = bess_word(state->mapper.bytes + index);
        if (address >= 0x8000u && (address < 0xa000u || address >= 0xc000u))
            return CUPID_GB_STATE_INVALID;
    }
    return CUPID_GB_STATE_OK;
}

static void copy_region(uint8_t *dest, size_t capacity, BessBlock region) {
    const size_t count = region.size < capacity ? region.size : capacity;
    if (!capacity)
        return;
    if (count)
        memcpy(dest, region.bytes, count);
    memset(dest + count, 0, capacity - count);
}

static void restore_registers(CupidGb *gb, const uint8_t *core) {
    const uint8_t *io = core + BESS_PORTS;
    const bool color = cupid_gb_model_is_cgb(gb->model);
    gb->cgb_mode = color && !(io[0x4c] & 4u);
    gb->cgb_object_priority_x = color && !gb->cgb_mode;
    if (color) {
        gb->double_speed = gb->cgb_mode && (io[0x4d] & 0x80u) != 0;
        gb->speed_switch_armed = gb->cgb_mode && (io[0x4d] & 1u) != 0;
        gb->io_registers[0x4c] = (uint8_t)(io[0x4c] & 0x0cu);
        gb->io_registers[0x4d] = (uint8_t)gb->speed_switch_armed;
        gb->cgb_vram_bank = (uint8_t)(gb->cgb_mode ? io[0x4f] & 1u : 0);
        gb->io_registers[0x4f] = (uint8_t)(0xfeu | gb->cgb_vram_bank);
        gb->io_registers[0x70] = (uint8_t)(0xf8u | (io[0x70] & 7u));
        gb->cgb_wram_bank = io[0x70] & 7u;
        if (!gb->cgb_wram_bank)
            gb->cgb_wram_bank = 1;
        gb->io_registers[0x68] = (uint8_t)(io[0x68] & 0xbfu);
        gb->io_registers[0x6a] = (uint8_t)(io[0x6a] & 0xbfu);
        gb->io_registers[0x6c] = io[0x6c] & 1u;
        memcpy(gb->io_registers + 0x72, io + 0x72, 3);
        gb->io_registers[0x75] = io[0x75] & 0x70u;
        for (unsigned port = 0x51; port <= 0x54; ++port)
            cupid_gb_write_u8(gb, (uint16_t)(0xff00u + port), io[port]);
        cupid_gb_write_u8(gb, 0xff56, io[0x56]);
    }

    cupid_gb_write_u8(gb, 0xff40, 0);
    const uint8_t lcd_ports[] = {0x41, 0x42, 0x43, 0x45, 0x47, 0x48, 0x49, 0x4a, 0x4b};
    for (unsigned i = 0; i < sizeof(lcd_ports); ++i) {
        const uint8_t port = lcd_ports[i];
        cupid_gb_write_u8(gb, (uint16_t)(0xff00u + port), io[port]);
    }
    gb->io_registers[0x46] = io[0x46];
    cupid_gb_write_u8(gb, 0xff40, io[0x40]);

    /* The transfer and timer phases are not represented in the file. */
    gb->io_registers[0x00] = (uint8_t)(0xc0u | (io[0] & 0x30u) | 0x0fu);
    gb->io_registers[0x01] = io[1];
    gb->io_registers[0x02] = (uint8_t)(io[2] & (color ? 3u : 1u));
    gb->serial_fast_clock = color && (io[2] & 2u) != 0;
    gb->io_registers[0x04] = io[4];
    gb->div_counter = 0;
    memcpy(gb->io_registers + 5, io + 5, 2);
    gb->io_registers[0x07] = io[7] & 7u;
    gb->io_registers[0x50] = 1;

    /* Rebuild sound controls without issuing trigger writes or retaining
     * oscillator history from the cartridge that was previously running. */
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff26, io[0x26] & 0x80u);
    for (unsigned port = 0x10; port < 0x26; ++port) {
        const uint8_t value = port == 0x14 || port == 0x19 || port == 0x1e || port == 0x23
                                  ? io[port] & 0x7fu
                                  : io[port];
        cupid_gb_write_u8(gb, (uint16_t)(0xff00u + port), value);
    }
    memcpy(gb->io_registers + 0x30, io + 0x30, 16);
    gb->interrupt_flags = io[0x0f] & 0x1fu;
    gb->interrupt_enable = core[21];
    gb->cpu = (CupidGbCpu){.pc = bess_word(core + 8),
                           .sp = bess_word(core + 18),
                           .a = core[11],
                           .f = core[10] & 0xf0u,
                           .b = core[13],
                           .c = core[12],
                           .d = core[15],
                           .e = core[14],
                           .h = core[17],
                           .l = core[16],
                           .ime = core[20] != 0,
                           .halted = core[22] == 1,
                           .stopped = core[22] == 2};
}

static void restore_clock(CupidGb *gb, const uint8_t *rtc) {
    if (!rtc || gb->header.mbc_type != CUPID_GB_MBC3 || !gb->header.has_timer)
        return;
    const uint64_t time = bess_long(rtc + 40) | (uint64_t)bess_long(rtc + 44) << 32;
    const int64_t base = time <= INT64_MAX ? (int64_t)time : -1 - (int64_t)(UINT64_MAX - time);
    gb->rtc = (CupidGbRtc){.s = rtc[0] & 63u,
                           .m = rtc[4] & 63u,
                           .h = rtc[8] & 31u,
                           .dl = rtc[12],
                           .dh = rtc[16] & 0xc1u,
                           .ls = rtc[20] & 63u,
                           .lm = rtc[24] & 63u,
                           .lh = rtc[28] & 31u,
                           .ldl = rtc[32],
                           .ldh = rtc[36] & 0xc1u,
                           .latch_prev = gb->rtc.latch_prev,
                           .base_time = base};
}

static bool supported_mapper(const CupidGb *gb) {
    switch (gb->header.mbc_type) {
        case CUPID_GB_MBC_NONE:
        case CUPID_GB_MBC1:
        case CUPID_GB_MBC2:
        case CUPID_GB_MBC5:
            return true;
        case CUPID_GB_MBC3:
            return !cupid_gb_has_extended_mbc3(gb);
        default:
            return false;
    }
}

CupidGbStateResult cupid_gb_bess_load(CupidGb *gb, const uint8_t *data, size_t length) {
    if (!gb || !gb->loaded || !gb->rom)
        return CUPID_GB_STATE_NO_ROM;
    if (!cupid_gb_model_is_valid(gb->model) || cupid_gb_model_is_sgb(gb->model) ||
        gb->program_kind != CUPID_GB_PROGRAM_CARTRIDGE || !supported_mapper(gb))
        return CUPID_GB_STATE_UNSUPPORTED_FEATURE;
    if (gb->rom_size < 0x150u || gb->rom_size > gb->rom_capacity ||
        !cupid_gb_cartridge_storage_valid(gb) ||
        !cupid_gb_cartridge_source_fits(gb, data, length) ||
        !cupid_gb_bess_has_footer(data, length))
        return CUPID_GB_STATE_INVALID;
    BessState state = {0};
    const size_t limit = length - BESS_FOOTER_SIZE;
    if (!read_blocks(&state, data, limit) || !read_regions(&state, data, limit))
        return CUPID_GB_STATE_INVALID;
    const CupidGbStateResult status = validate(gb, &state);
    if (status != CUPID_GB_STATE_OK)
        return status;

    CupidGb *next = calloc(1, sizeof(*next));
    if (!next)
        return CUPID_GB_STATE_OUT_OF_MEMORY;
    next->model = gb->model;
    next->rom = gb->rom;
    next->rom_capacity = gb->rom_capacity;
    next->rom_generation = gb->rom_generation;
    next->power_on = gb->power_on;
    next->power_on.fixed_seed = true;
    next->power_on.seed = gb->power_on.generation_seed;
    next->rtc_policy.mode = CUPID_GB_RTC_ACCURATE;
    if (gb->cartridge_ram_heap) {
        next->cartridge_ram_heap = malloc(gb->cartridge_ram_capacity);
        if (!next->cartridge_ram_heap) {
            free(next);
            return CUPID_GB_STATE_OUT_OF_MEMORY;
        }
        next->cartridge_ram_capacity = gb->cartridge_ram_capacity;
    }
    cupid_gb_init(next);
    cupid_gb_reset_restore_cartridge(next, gb);
    cupid_gb_cartridge_reset_controls(next);
    next->power_on = gb->power_on;
    /* An omitted RTC block has no portable timestamp. Keep the receiver's
     * clock rather than treating its counters as having run since the epoch. */
    if (next->header.has_timer)
        next->rtc = gb->rtc;

    copy_region(next->work_ram, sizeof(next->work_ram), state.regions[0]);
    copy_region(next->video_ram, sizeof(next->video_ram), state.regions[1]);
    copy_region(cupid_gb_cartridge_ram(next), next->cartridge_ram_size, state.regions[2]);
    copy_region(next->object_attribute_memory, sizeof(next->object_attribute_memory),
                state.regions[3]);
    copy_region(next->high_ram, sizeof(next->high_ram), state.regions[4]);
    copy_region(next->cgb_bg_palette_ram, sizeof(next->cgb_bg_palette_ram), state.regions[5]);
    copy_region(next->cgb_obj_palette_ram, sizeof(next->cgb_obj_palette_ram), state.regions[6]);
    if (state.extra_oam.bytes && cupid_gb_extra_oam_size(next->model)) {
        /* XOAM stores bus addresses, including mirrors. Use the lowest
         * address when a file supplies inconsistent values for an alias. */
        for (unsigned offset = BESS_XOAM_SIZE; offset-- > 0;)
            cupid_gb_extra_oam_write(next, (uint8_t)(0xa0u + offset),
                                     state.extra_oam.bytes[offset]);
    }
    for (size_t index = 0; index < state.mapper.size; index += 3) {
        const uint8_t *entry = state.mapper.bytes + index;
        cupid_gb_write_u8(next, bess_word(entry), entry[2]);
    }
    if (next->header.mbc_type == CUPID_GB_MBC2) {
        uint8_t *ram = cupid_gb_cartridge_ram(next);
        for (size_t index = 0; index < next->cartridge_ram_size; ++index)
            ram[index] &= 0x0fu;
    }
    restore_clock(next, state.clock.bytes);
    restore_registers(next, state.core.bytes);

    next->rtc_policy = gb->rtc_policy;
    next->apu.output = gb->apu.output;
    next->apu.output.filter.startup_pending = false;
    next->apu.output.sample_fraction = 0;
    next->apu.output.fraction_half = false;
    if (!cupid_gb_audio_state_valid(next)) {
        free(next->cartridge_ram_heap);
        free(next);
        return CUPID_GB_STATE_INVALID;
    }
    cupid_gb_reset_restore_host(next, gb);
    cupid_gb_input_import_legacy(next);
    if (next->header.has_battery)
        ++next->battery_write_generation;
    cupid_gb_reset_commit(gb, next);
    return CUPID_GB_STATE_OK;
}

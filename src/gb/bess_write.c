/**
 * @file    bess_write.c
 * @brief   Portable cartridge checkpoint export with explicit BESS wire fields.
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

#include "cartridge_internal.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/mbc6.h"
#include "cupid/gb/sgb.h"
#include "oam_bus.h"
#include "printer_internal.h"

#include <string.h>

enum {
    BESS_CORE_BYTES = 0xd0,
    BESS_RTC_BYTES = 0x30,
    BESS_HUC3_BYTES = 17,
    BESS_MBC7_BYTES = 10,
    BESS_XOAM_BYTES = 0x60,
    BESS_SGB_BYTES = 0x39,
    BESS_SGB_MAP_BYTES = 0x800,
    BESS_SGB_MEMORY_BYTES = 0x2000 + BESS_SGB_MAP_BYTES + 0x80 + 0x20 + 0x1000 + 0x168 + 0xfd2
};

static const char writer_name[] = "CupidGB";

static void put_word(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
}

static void put_dword(uint8_t *target, uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte)
        target[byte] = (uint8_t)(value >> (byte * 8));
}

static void put_qword(uint8_t *target, uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte)
        target[byte] = (uint8_t)(value >> (byte * 8));
}

static bool overlaps(const void *first, size_t first_size, const void *second, size_t second_size) {
    if (!first || !second || !first_size || !second_size)
        return false;
    const uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
    return a <= b ? b - a < first_size : a - b < second_size;
}

static bool overlaps_session(const CupidGb *gb, const void *bytes, size_t size) {
    if (!gb)
        return false;
    const CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
    return overlaps(bytes, size, gb, sizeof(*gb)) ||
           overlaps(bytes, size, gb->rom, gb->rom_capacity) ||
           overlaps(bytes, size, gb->cartridge_ram_heap, gb->cartridge_ram_capacity) ||
           overlaps(bytes, size, gb->mbc6_flash, CUPID_GB_MBC6_STORAGE_SIZE) ||
           (printer && overlaps(bytes, size, printer, sizeof(*printer)));
}

static CupidGbStateResult supported(const CupidGb *gb) {
    if (!gb || !gb->loaded || !gb->rom)
        return CUPID_GB_STATE_NO_ROM;
    if (gb->rom_size < 0x150u || gb->rom_size > gb->rom_capacity ||
        !cupid_gb_model_is_valid(gb->model) || !cupid_gb_cartridge_storage_valid(gb))
        return CUPID_GB_STATE_INVALID;
    if (gb->program_kind != CUPID_GB_PROGRAM_CARTRIDGE || gb->boot_rom_enabled)
        return CUPID_GB_STATE_UNSUPPORTED_FEATURE;
    switch (gb->header.mbc_type) {
        case CUPID_GB_MBC_NONE:
        case CUPID_GB_MBC1:
        case CUPID_GB_MBC2:
        case CUPID_GB_MBC5:
            break;
        case CUPID_GB_MBC7:
            if (gb->current_rom_bank > 255u || !cupid_gb_mbc7_serial_valid(&gb->mbc7))
                return CUPID_GB_STATE_INVALID;
            break;
        case CUPID_GB_HUC3:
            if (gb->huc3_mode > 15u || gb->huc3.rtc_base_time < 0 ||
                gb->current_rom_bank > CUPID_GB_HUC3_STARTUP_BANK ||
                (gb->current_rom_bank == CUPID_GB_HUC3_STARTUP_BANK &&
                 (gb->huc3_mode || gb->current_ram_bank)))
                return CUPID_GB_STATE_INVALID;
            break;
        case CUPID_GB_MBC3:
            if (cupid_gb_has_extended_mbc3(gb))
                return CUPID_GB_STATE_UNSUPPORTED_FEATURE;
            break;
        default:
            return CUPID_GB_STATE_UNSUPPORTED_FEATURE;
    }
    if (gb->cpu.f & 0x0fu || gb->mbc1_bank_low5 > 31u || gb->mbc1_bank_high2 > 3u ||
        gb->mbc5_rom_bank > 511u || gb->current_ram_bank > 15u ||
        (gb->header.mbc_type == CUPID_GB_MBC2 && gb->current_rom_bank > 15u) ||
        (gb->header.mbc_type == CUPID_GB_MBC3 &&
         (gb->current_rom_bank > 127u || gb->current_ram_bank > 7u ||
          (gb->rtc_register && (gb->rtc_register < 8u || gb->rtc_register > 15u)))) ||
        gb->cgb_vram_bank > 1u || gb->cgb_wram_bank > 7u ||
        (!cupid_gb_model_is_cgb(gb->model) && gb->double_speed))
        return CUPID_GB_STATE_INVALID;
    if (cupid_gb_sgb_commands_enabled(gb) &&
        (!gb->sgb.player_count || gb->sgb.player_count > 4 ||
         (gb->sgb.player_count == 3 && (gb->sgb.current_player & 1u)) ||
         gb->sgb.current_player >= gb->sgb.player_count))
        return CUPID_GB_STATE_INVALID;
    return CUPID_GB_STATE_OK;
}

static size_t mapper_bytes(const CupidGb *gb) {
    switch (gb->header.mbc_type) {
        case CUPID_GB_MBC_NONE:
            return 0;
        case CUPID_GB_MBC2:
            return 6;
        case CUPID_GB_MBC3:
            return 15;
        case CUPID_GB_MBC7:
            return 9;
        case CUPID_GB_HUC3:
            /* An omitted mapper block leaves the initial bank 1 mapping and
             * zero raw latch intact until the first control write. */
            return gb->current_rom_bank == CUPID_GB_HUC3_STARTUP_BANK ? 0 : 9;
        default:
            return 12;
    }
}

size_t cupid_gb_bess_size(const CupidGb *gb) {
    if (supported(gb) != CUPID_GB_STATE_OK)
        return 0;
    const bool color = cupid_gb_model_is_cgb(gb->model);
    const size_t memory = (color ? 0x8000u + 0x4000u + 0x80u : 0x2000u + 0x2000u) +
                          gb->cartridge_ram_size + 0xa0u + 0x7fu;
    const size_t mbc = mapper_bytes(gb);
    return memory + 8u + sizeof(writer_name) - 1u + 8u + 0x12u + 8u + BESS_CORE_BYTES +
           (mbc ? 8u + mbc : 0) +
           (gb->header.mbc_type == CUPID_GB_MBC3 && gb->header.has_timer ? 8u + BESS_RTC_BYTES
                                                                         : 0) +
           (gb->header.mbc_type == CUPID_GB_MBC7 ? 8u + BESS_MBC7_BYTES : 0) +
           (gb->header.mbc_type == CUPID_GB_HUC3 ? 8u + BESS_HUC3_BYTES : 0) +
           (cupid_gb_extra_oam_size(gb->model) ? 8u + BESS_XOAM_BYTES : 0) +
           (cupid_gb_sgb_commands_enabled(gb) ? BESS_SGB_MEMORY_BYTES + 8u + BESS_SGB_BYTES : 0) +
           8u + 8u;
}

static void write_block(uint8_t *data, size_t *cursor, const char name[4], const uint8_t *payload,
                        size_t size) {
    memcpy(data + *cursor, name, 4);
    put_dword(data + *cursor + 4, (uint32_t)size);
    *cursor += 8;
    if (size)
        memcpy(data + *cursor, payload, size);
    *cursor += size;
}

static void write_region(uint8_t *data, size_t *cursor, uint8_t *descriptor, const uint8_t *source,
                         size_t size) {
    put_dword(descriptor, (uint32_t)size);
    put_dword(descriptor + 4, size ? (uint32_t)*cursor : 0);
    if (size)
        memcpy(data + *cursor, source, size);
    *cursor += size;
}

static void write_word_region(uint8_t *data, size_t *cursor, uint8_t *descriptor,
                              const uint16_t *source, size_t words, size_t wire_size) {
    put_dword(descriptor, (uint32_t)wire_size);
    put_dword(descriptor + 4, (uint32_t)*cursor);
    for (size_t word = 0; word < words; ++word)
        put_word(data + *cursor + word * 2, source[word]);
    memset(data + *cursor + words * 2, 0, wire_size - words * 2);
    *cursor += wire_size;
}

static void write_sgb_memory(uint8_t *data, size_t *cursor, uint8_t payload[BESS_SGB_BYTES],
                             const CupidGbSgb *sgb) {
    /* BESS has no border transition history. Preserve the latest captured
     * transfer as one border, including tile uploads awaiting PCT_TRN. */
    write_region(data, cursor, payload, sgb->transfer.tiles, sizeof(sgb->transfer.tiles));
    /* Only the displayed 28 rows are retained by this core. The four
     * undisplayed rows of the portable 32-row tilemap have no source history. */
    write_word_region(data, cursor, payload + 8, sgb->transfer.map,
                      sizeof(sgb->transfer.map) / sizeof(sgb->transfer.map[0]), BESS_SGB_MAP_BYTES);
    write_word_region(data, cursor, payload + 16, sgb->transfer.palettes,
                      CUPID_SGB_BORDER_PALETTE_COLORS, 0x80);
    write_word_region(data, cursor, payload + 24, sgb->screen_palettes,
                      sizeof(sgb->screen_palettes) / sizeof(sgb->screen_palettes[0]), 0x20);
    write_word_region(data, cursor, payload + 32, sgb->ram_palettes,
                      sizeof(sgb->ram_palettes) / sizeof(sgb->ram_palettes[0]), 0x1000);
    write_region(data, cursor, payload + 40, sgb->attribute_map, sizeof(sgb->attribute_map));
    write_region(data, cursor, payload + 48, sgb->attribute_files, sizeof(sgb->attribute_files));
    payload[56] = (uint8_t)((sgb->player_count << 4) | sgb->current_player);
}

static void model_name(CupidGbModel model, uint8_t name[4]) {
    if (cupid_gb_model_is_sgb(model)) {
        memcpy(name,
               model == CUPID_GB_MODEL_SGB_PAL ? "SP  "
               : model == CUPID_GB_MODEL_SGB2  ? "S2  "
                                               : "SN  ",
               4);
    } else if (cupid_gb_model_is_cgb(model)) {
        memcpy(name, "CC  ", 4);
        name[2] = model == CUPID_GB_MODEL_CGB_E   ? 'E'
                  : model == CUPID_GB_MODEL_CGB_0 ? '0'
                                                  : (uint8_t)('A' + model - CUPID_GB_MODEL_CGB_A);
    } else {
        memcpy(name, model == CUPID_GB_MODEL_MGB ? "GM  " : "GD  ", 4);
        if (model == CUPID_GB_MODEL_DMG0)
            name[2] = '0';
    }
}

static void mapper_pair(uint8_t *data, size_t *cursor, uint16_t address, uint8_t value) {
    put_word(data + *cursor, address);
    data[*cursor + 2] = value;
    *cursor += 3;
}

static void write_mapper(uint8_t *data, size_t *cursor, const CupidGb *gb) {
    uint8_t pairs[15];
    size_t count = 0;
    if (!mapper_bytes(gb))
        return;
    mapper_pair(pairs, &count, 0x0000,
                gb->header.mbc_type == CUPID_GB_HUC3 ? gb->huc3_mode
                : gb->ram_enabled                    ? 0x0a
                                                     : 0);
    switch (gb->header.mbc_type) {
        case CUPID_GB_MBC1:
            mapper_pair(pairs, &count, 0x2000, gb->mbc1_bank_low5);
            mapper_pair(pairs, &count, 0x4000, gb->mbc1_bank_high2);
            mapper_pair(pairs, &count, 0x6000, gb->mbc1_ram_banking_mode ? 1 : 0);
            break;
        case CUPID_GB_MBC2:
            mapper_pair(pairs, &count, 0x2100, (uint8_t)gb->current_rom_bank);
            break;
        case CUPID_GB_MBC3:
            mapper_pair(pairs, &count, 0x2000, (uint8_t)gb->current_rom_bank);
            mapper_pair(pairs, &count, 0x4000,
                        gb->rtc_register ? gb->rtc_register : (uint8_t)gb->current_ram_bank);
            /* A following RTC block restores the distinct saved snapshot after
             * any latch side effects of these ordered controller writes. */
            mapper_pair(pairs, &count, 0x6000, 0);
            mapper_pair(pairs, &count, 0x6000, gb->rtc.latch_prev);
            break;
        case CUPID_GB_MBC7:
            mapper_pair(pairs, &count, 0x2000, (uint8_t)gb->current_rom_bank);
            mapper_pair(pairs, &count, 0x4000, gb->mbc7.registers_enabled ? 0x40 : 0);
            break;
        case CUPID_GB_HUC3:
            mapper_pair(pairs, &count, 0x2000, (uint8_t)gb->current_rom_bank);
            mapper_pair(pairs, &count, 0x4000, (uint8_t)gb->current_ram_bank);
            break;
        default:
            mapper_pair(pairs, &count, 0x2000, (uint8_t)gb->mbc5_rom_bank);
            mapper_pair(pairs, &count, 0x3000, (uint8_t)(gb->mbc5_rom_bank >> 8));
            mapper_pair(pairs, &count, 0x4000,
                        (uint8_t)(gb->current_ram_bank |
                                  (gb->header.has_rumble && gb->mbc5_rumble_on ? 8u : 0)));
            break;
    }
    write_block(data, cursor, "MBC ", pairs, count);
}

static void write_mbc7(uint8_t *data, size_t *cursor, const CupidGb *gb) {
    const CupidGbMbc7 *device = &gb->mbc7;
    const unsigned flags = (unsigned)gb->mbc7_latch | ((device->pins & 3u) << 1) |
                           ((device->pins & 0xc0u) >> 3) |
                           ((unsigned)device->programming_enabled << 5);
    uint8_t payload[BESS_MBC7_BYTES] = {(uint8_t)flags, device->data_bits};
    put_word(payload + 2, device->command);
    put_word(payload + 4, device->output);
    put_word(payload + 6, gb->mbc7_sensor_x);
    put_word(payload + 8, gb->mbc7_sensor_y);
    write_block(data, cursor, "MBC7", payload, sizeof(payload));
}

static void write_huc3(uint8_t *data, size_t *cursor, const CupidGbHuc3 *clock) {
    uint8_t payload[BESS_HUC3_BYTES];
    put_qword(payload, (uint64_t)clock->rtc_base_time);
    put_word(payload + 8, clock->minutes);
    put_word(payload + 10, clock->days);
    put_word(payload + 12, clock->alarm_minutes);
    put_word(payload + 14, clock->alarm_days);
    payload[16] = clock->alarm_enabled;
    write_block(data, cursor, "HUC3", payload, sizeof(payload));
}

CupidGbStateResult cupid_gb_bess_save(const CupidGb *gb, uint8_t *data, size_t capacity,
                                      size_t *written) {
    if (written && (overlaps_session(gb, written, sizeof(*written)) ||
                    overlaps(data, capacity, written, sizeof(*written))))
        return CUPID_GB_STATE_INVALID;
    if (written)
        *written = 0;
    const CupidGbStateResult result = supported(gb);
    if (result != CUPID_GB_STATE_OK)
        return result;
    const size_t size = cupid_gb_bess_size(gb);
    if (overlaps_session(gb, data, size))
        return CUPID_GB_STATE_INVALID;
    if (!data || capacity < size)
        return CUPID_GB_STATE_BUFFER_TOO_SMALL;

    const bool color = cupid_gb_model_is_cgb(gb->model);
    uint8_t core[BESS_CORE_BYTES] = {0};
    put_word(core, 1);
    put_word(core + 2, 1);
    model_name(gb->model, core + 4);
    put_word(core + 8, gb->cpu.pc);
    put_word(core + 10, (uint16_t)((uint16_t)gb->cpu.a << 8 | gb->cpu.f));
    put_word(core + 12, (uint16_t)((uint16_t)gb->cpu.b << 8 | gb->cpu.c));
    put_word(core + 14, (uint16_t)((uint16_t)gb->cpu.d << 8 | gb->cpu.e));
    put_word(core + 16, (uint16_t)((uint16_t)gb->cpu.h << 8 | gb->cpu.l));
    put_word(core + 18, gb->cpu.sp);
    core[20] = gb->cpu.ime ? 1 : 0;
    core[21] = gb->interrupt_enable;
    core[22] = gb->cpu.stopped ? 2 : gb->cpu.halted ? 1 : 0;
    uint8_t *io = core + 24;
    /* Bus read masks hide writable APU bits, and peeking wave RAM can expose
     * only the current fetch byte. Export the underlying register cells. */
    memcpy(io, gb->io_registers, sizeof(gb->io_registers));
    io[0x0f] = gb->interrupt_flags;
    io[0x26] = (uint8_t)((gb->apu.apu_on ? 0x80u : 0u) | 0x70u | (gb->apu.ch1_on ? 1u : 0u) |
                         (gb->apu.ch2_on ? 2u : 0u) | (gb->apu.ch3_on ? 4u : 0u) |
                         (gb->apu.ch4_on ? 8u : 0u));
    io[0x4c] = gb->cgb_mode ? 0x80 : 4;
    io[0x4d] = (uint8_t)((io[0x4d] & 1u) | (gb->double_speed ? 0x80u : 0u));
    io[0x4f] = gb->cgb_vram_bank;
    io[0x50] = 1;
    /* SVBK zero aliases physical bank one; preserve zero when it already
     * describes the active bank, otherwise encode the actual bank latch. */
    const unsigned selected_wram = io[0x70] & 7u;
    if ((selected_wram ? selected_wram : 1u) != gb->cgb_wram_bank)
        io[0x70] = (uint8_t)((io[0x70] & 0xf8u) | gb->cgb_wram_bank);

    size_t cursor = 0;
    write_region(data, &cursor, core + 0x98, gb->work_ram, color ? 0x8000 : 0x2000);
    write_region(data, &cursor, core + 0xa0, gb->video_ram, color ? 0x4000 : 0x2000);
    write_region(data, &cursor, core + 0xa8, cupid_gb_cartridge_ram_const(gb),
                 gb->cartridge_ram_size);
    write_region(data, &cursor, core + 0xb0, gb->object_attribute_memory, 0xa0);
    write_region(data, &cursor, core + 0xb8, gb->high_ram, 0x7f);
    write_region(data, &cursor, core + 0xc0, gb->cgb_bg_palette_ram, color ? 0x40 : 0);
    write_region(data, &cursor, core + 0xc8, gb->cgb_obj_palette_ram, color ? 0x40 : 0);
    uint8_t sgb_payload[BESS_SGB_BYTES] = {0};
    const bool save_sgb = cupid_gb_sgb_commands_enabled(gb);
    if (save_sgb)
        write_sgb_memory(data, &cursor, sgb_payload, &gb->sgb);
    const size_t blocks_start = cursor;
    write_block(data, &cursor, "NAME", (const uint8_t *)writer_name, sizeof(writer_name) - 1);
    uint8_t info[0x12];
    memcpy(info, gb->rom + 0x134, 16);
    memcpy(info + 16, gb->rom + 0x14e, 2);
    write_block(data, &cursor, "INFO", info, sizeof(info));
    write_block(data, &cursor, "CORE", core, sizeof(core));
    write_mapper(data, &cursor, gb);
    if (gb->header.mbc_type == CUPID_GB_MBC3 && gb->header.has_timer) {
        uint8_t rtc[BESS_RTC_BYTES] = {0};
        const uint8_t fields[] = {gb->rtc.s,  gb->rtc.m,  gb->rtc.h,  gb->rtc.dl,  gb->rtc.dh,
                                  gb->rtc.ls, gb->rtc.lm, gb->rtc.lh, gb->rtc.ldl, gb->rtc.ldh};
        for (unsigned field = 0; field < sizeof(fields); ++field)
            rtc[field * 4] = fields[field];
        put_qword(rtc + 0x28, (uint64_t)gb->rtc.base_time);
        write_block(data, &cursor, "RTC ", rtc, sizeof(rtc));
    }
    if (cupid_gb_extra_oam_size(gb->model)) {
        uint8_t extra[BESS_XOAM_BYTES];
        for (unsigned cell = 0; cell < sizeof(extra); ++cell)
            extra[cell] = cupid_gb_extra_oam_read(gb, (uint8_t)(0xa0u + cell));
        write_block(data, &cursor, "XOAM", extra, sizeof(extra));
    }
    if (save_sgb)
        write_block(data, &cursor, "SGB ", sgb_payload, sizeof(sgb_payload));
    if (gb->header.mbc_type == CUPID_GB_MBC7)
        write_mbc7(data, &cursor, gb);
    if (gb->header.mbc_type == CUPID_GB_HUC3)
        write_huc3(data, &cursor, &gb->huc3);
    write_block(data, &cursor, "END ", NULL, 0);
    put_dword(data + cursor, (uint32_t)blocks_start);
    memcpy(data + cursor + 4, "BESS", 4);
    cursor += 8;
    if (written)
        *written = cursor;
    return CUPID_GB_STATE_OK;
}

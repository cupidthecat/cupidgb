/**
 * @file    vram_bus.c
 * @brief   VRAM address selection and held read data at the first HBlank access edge.
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

#include "vram_bus.h"

#include "cupid/gb/ppu.h"
#include "cupid/gbc/cgb.h"

static bool first_hblank_dot(const CupidGb *gb) {
    const CupidGbPixels *pixels = &gb->pixels;
    return cupid_gb_lcd_enabled(gb) && pixels->initialized && !pixels->active &&
           pixels->line < CUPID_GB_PPU_VISIBLE_SCANLINES && pixels->x == CUPID_GB_SCREEN_WIDTH &&
           gb->ppu_transfer_end_dot && pixels->dot == gb->ppu_transfer_end_dot;
}

uint8_t cupid_gb_vram_bus_read(const CupidGb *gb, CupidGb *bus, uint16_t address, bool dma_read) {
    if (!gb || (!dma_read && cupid_gb_ppu_vram_read_blocked(gb)))
        return 0xffu;

    uint16_t selected = address & 0x1fffu;
    bool held_read = false;
    if (first_hblank_dot(gb)) {
        const CupidGbVramBus *history = &gb->vram_bus;
        if (!cupid_gb_model_is_cgb(gb->model)) {
            /* The departing tile fetch can keep address line 12 low. */
            if (!(history->data_address & 0x1000u))
                selected &= 0x0fffu;
        } else if (!gb->double_speed) {
            /* The normal-speed CGB port selects a departing fetch address.
             * A low CPU address after high tile data instead exposes the prior
             * read byte, while the CGB-E circuit loads the newly requested byte. */
            if (selected & 0x1000u)
                selected = history->map_address;
            else if (history->data_address & 0x1000u)
                held_read = true;
            else
                selected = history->data_address;
        }
    }

    /* The shared mapper accepts CPU addresses; the fetch latches also contain
     * a PPU bank bit that must not select the CPU's physical VRAM bank. */
    const uint16_t bus_address = (uint16_t)(0x8000u | (selected & 0x1fffu));
    const uint8_t driven = gb->video_ram[cupid_cgb_vram_offset(gb, bus_address)];
    const uint8_t result = held_read ? gb->vram_bus.read_value : driven;
    if (bus)
        bus->vram_bus.read_value = driven;
    return result;
}

void cupid_gb_vram_bus_begin_transfer(CupidGb *gb) {
    if (!gb)
        return;
    /* A quiet row may finish without replacing the last bus-visible fetch.
     * Keep eager pixel work separate until synchronization selects fine work. */
    gb->vram_bus.fetch_map_address = gb->vram_bus.map_address;
    gb->vram_bus.fetch_data_address = gb->vram_bus.data_address;
}

void cupid_gb_vram_bus_commit_fetch(CupidGb *gb) {
    if (!gb)
        return;
    gb->vram_bus.map_address = gb->vram_bus.fetch_map_address;
    gb->vram_bus.data_address = gb->vram_bus.fetch_data_address;
}

static bool valid_addresses(uint16_t map, uint16_t data) {
    return (!map || (map >= 0x1800u && map < 0x2000u)) && data < 0x4000u;
}

static bool valid_history(const CupidGbVramBus *history) {
    return valid_addresses(history->map_address, history->data_address) &&
           valid_addresses(history->fetch_map_address, history->fetch_data_address);
}

bool cupid_gb_vram_bus_state_valid(const CupidGb *gb) {
    return gb && valid_history(&gb->vram_bus);
}

bool cupid_gb_vram_bus_state_encode(const CupidGb *gb, uint8_t *data, size_t size) {
    if (!data || size != CUPID_GB_VRAM_BUS_STATE_SIZE || !cupid_gb_vram_bus_state_valid(gb))
        return false;
    data[0] = (uint8_t)gb->vram_bus.map_address;
    data[1] = (uint8_t)(gb->vram_bus.map_address >> 8);
    data[2] = (uint8_t)gb->vram_bus.data_address;
    data[3] = (uint8_t)(gb->vram_bus.data_address >> 8);
    data[4] = gb->vram_bus.read_value;
    data[5] = 0;
    data[6] = (uint8_t)gb->vram_bus.fetch_map_address;
    data[7] = (uint8_t)(gb->vram_bus.fetch_map_address >> 8);
    data[8] = (uint8_t)gb->vram_bus.fetch_data_address;
    data[9] = (uint8_t)(gb->vram_bus.fetch_data_address >> 8);
    return true;
}

bool cupid_gb_vram_bus_state_decode(CupidGb *gb, const uint8_t *data, size_t size) {
    if (!gb || !data || size != CUPID_GB_VRAM_BUS_STATE_SIZE || data[5])
        return false;
    const CupidGbVramBus history = {
        .map_address = (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8),
        .data_address = (uint16_t)((uint16_t)data[2] | (uint16_t)data[3] << 8),
        .read_value = data[4],
        .fetch_map_address = (uint16_t)((uint16_t)data[6] | (uint16_t)data[7] << 8),
        .fetch_data_address = (uint16_t)((uint16_t)data[8] | (uint16_t)data[9] << 8),
    };
    if (!valid_history(&history))
        return false;
    gb->vram_bus = history;
    return true;
}

void cupid_gb_vram_bus_import_legacy(CupidGb *gb) {
    if (!gb)
        return;
    gb->vram_bus = (CupidGbVramBus){0};
    /* Older files retain only one working fetch address. Recover that address
     * according to its phase; the overwritten address and CPU byte are unknown. */
    const CupidGbPixels *pixels = &gb->pixels;
    if (pixels->phase == 1 || pixels->phase == 2) {
        if (pixels->address >= 0x1800u && pixels->address < 0x2000u)
            gb->vram_bus.map_address = pixels->address;
    } else if (pixels->address < 0x4000u) {
        gb->vram_bus.data_address = pixels->address;
    }
    cupid_gb_vram_bus_begin_transfer(gb);
}

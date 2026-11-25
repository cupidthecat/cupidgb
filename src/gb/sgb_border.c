/**
 * @file    sgb_border.c
 * @brief   Bounded cartridge border donors and outer-border RGB rendering.
 * @author  cupidthecat
 * @date    2026-11-24
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

#include "cupid/gb/sgb_border.h"

#include "cupid/gb/gb.h"
#include "cupid/gb/sgb.h"
#include "cupid/gb/startup.h"

#include <stdlib.h>
#include <string.h>

enum {
    BORROW_FRAME_LIMIT = 600,
    BORROW_STEP_LIMIT = BORROW_FRAME_LIMIT * 80000,
    BORDER_PIXEL_COUNT = CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT,
    BORDER_PALETTE_COLORS = 64,
};

typedef struct DonorBudget {
    unsigned frames;
} DonorBudget;

static bool overlaps(const void *first, size_t first_size, const void *second, size_t second_size) {
    if (!first || !second || !first_size || !second_size)
        return false;
    const uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
    return a <= b ? b - a < first_size : a - b < second_size;
}

static void donor_frame(const CupidGb *gb, uint8_t type, void *user_data) {
    (void)gb;
    (void)type;
    ++((DonorBudget *)user_data)->frames;
}

bool cupid_gb_sgb_borrow_border(const uint8_t *rom, size_t rom_size, const uint8_t *firmware,
                                size_t firmware_size, CupidGbSgbBorder *border) {
    if (!rom || !firmware || !border || rom_size < 0x150u ||
        rom_size > CUPID_GB_SGB_BORROW_MAX_ROM_SIZE || firmware_size != 0x100u ||
        overlaps(border, sizeof(*border), rom, rom_size) ||
        overlaps(border, sizeof(*border), firmware, firmware_size) || rom[0x146] != 3u)
        return false;

    CupidGb *donor = calloc(1, sizeof(*donor));
    if (!donor)
        return false;
    donor->model = CUPID_GB_MODEL_SGB;
    cupid_gb_set_power_on_seed(donor, 1);
    cupid_gb_init(donor);
    bool captured = false;
    if (donor->rom && cupid_gb_load_rom(donor, rom, rom_size)) {
        memcpy(donor->boot_rom, firmware, firmware_size);
        donor->boot_rom_size = firmware_size;
        cupid_gb_start_boot_rom(donor);
        /* Border discovery needs the donor's ordinary LCD and command receiver.
         * Begin immediately before the host intro's first visible phase; no live
         * session clock, output registration, device or cartridge storage is used. */
        donor->sgb_startup.phase = -1;
        DonorBudget budget = {0};
        cupid_gb_set_presentation_callback(donor, donor_frame, &budget);
        const uint64_t cycle_limit = (uint64_t)BORROW_FRAME_LIMIT * CUPID_SGB_FRAME_DOTS;
        for (unsigned steps = 0; steps < BORROW_STEP_LIMIT && budget.frames < BORROW_FRAME_LIMIT &&
                                 cupid_gb_presentation_cycles(donor) < cycle_limit;
             ++steps) {
            if (!cupid_gb_step(donor))
                break;
            if (donor->sgb.transfer.border_transition) {
                /* The displayed border can still be fading out. Capture the
                 * completed pending transfer, including its current backdrop. */
                memcpy(border->tiles, donor->sgb.transfer.tiles, sizeof(border->tiles));
                memcpy(border->map, donor->sgb.transfer.map, sizeof(border->map));
                memcpy(border->palettes, donor->sgb.transfer.palettes, sizeof(border->palettes));
                border->palettes[0] = donor->sgb.screen_palettes[0];
                captured = true;
                break;
            }
        }
    }
    cupid_gb_cleanup(donor);
    free(donor);
    return captured;
}

static uint32_t linear_color(uint16_t color) {
    const unsigned red = color & 31u;
    const unsigned green = (color >> 5u) & 31u;
    const unsigned blue = (color >> 10u) & 31u;
    return 0xff000000u | ((red << 3u) | (red >> 2u)) << 16u |
           ((green << 3u) | (green >> 2u)) << 8u | (blue << 3u) | (blue >> 2u);
}

void cupid_gb_sgb_render_borrowed_border(const CupidGbSgbBorder *border, uint32_t *pixels,
                                         size_t pixel_count, const uint32_t *color_table) {
    const size_t output_bytes = BORDER_PIXEL_COUNT * sizeof(*pixels);
    if (!border || !pixels || pixel_count < BORDER_PIXEL_COUNT ||
        overlaps(pixels, output_bytes, border, sizeof(*border)) ||
        (color_table && overlaps(pixels, output_bytes, color_table, 32768u * sizeof(*color_table))))
        return;

    uint32_t colors[BORDER_PALETTE_COLORS];
    for (unsigned index = 0; index < BORDER_PALETTE_COLORS; ++index) {
        const uint16_t color = border->palettes[index] & 0x7fffu;
        colors[index] = color_table ? color_table[color] : linear_color(color);
    }

    for (unsigned map_y = 0; map_y < CUPID_SGB_TILE_HEIGHT; ++map_y) {
        for (unsigned map_x = 0; map_x < CUPID_SGB_TILE_WIDTH; ++map_x) {
            if (map_x >= 6u && map_x < 26u && map_y >= 5u && map_y < 23u)
                continue;
            const uint16_t entry = border->map[map_y * CUPID_SGB_TILE_WIDTH + map_x];
            const uint8_t *tile = border->tiles + (entry & 0xffu) * 32u;
            const unsigned palette = ((entry >> 10u) & 3u) * 16u;
            for (unsigned y = 0; y < 8; ++y) {
                const unsigned row = ((entry & 0x8000u) ? 7u - y : y) * 2u;
                uint32_t *output = pixels + (map_y * 8u + y) * CUPID_SGB_SCREEN_WIDTH + map_x * 8u;
                for (unsigned x = 0; x < 8; ++x) {
                    const unsigned bit = (entry & 0x4000u) ? x : 7u - x;
                    const unsigned color = ((tile[row] >> bit) & 1u) |
                                           (((tile[row + 1u] >> bit) & 1u) << 1u) |
                                           (((tile[row + 16u] >> bit) & 1u) << 2u) |
                                           (((tile[row + 17u] >> bit) & 1u) << 3u);
                    output[x] = colors[color ? palette + color : 0];
                }
            }
        }
    }
}

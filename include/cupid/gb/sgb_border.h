/**
 * @file    sgb_border.h
 * @brief   Isolated cartridge border capture and handheld border composition.
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

#ifndef CUPID_GB_SGB_BORDER_H
#define CUPID_GB_SGB_BORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capture bounds the donor's independent ROM allocation as well as execution. */
#define CUPID_GB_SGB_BORROW_MAX_ROM_SIZE (32u * 1024u * 1024u)

typedef struct CupidGbSgbBorder {
    uint8_t tiles[256u * 32u];
    uint16_t map[32u * 28u];
    uint16_t palettes[4u * 16u];
} CupidGbSgbBorder;

/* Execute a fresh SGB donor with independently owned cartridge storage and no
 * host devices. The ROM must declare SGB support at 0146 and fit the bound above;
 * firmware must contain exactly 256 bytes. Firmware decides receiver permission.
 * Execution stops at the first completed border or after at most 600 frame spans
 * and 48 million hardware steps. The caller's input and output must not overlap.
 * Independent calls may run alongside other consoles. Inputs must remain stable
 * until return, and each call requires its own output storage.
 * Only success changes border. Its colors remain RGB555; host cache ownership,
 * retry policy, presentation settings and native snapshots belong to the caller. */
bool cupid_gb_sgb_borrow_border(const uint8_t *rom, size_t rom_size, const uint8_t *firmware,
                                size_t firmware_size, CupidGbSgbBorder *border);

/* Draw the border outside the (48,40,160,144) viewport of a 256x224 ARGB image.
 * Viewport pixels are untouched. Tile indices use eight bits, including entries
 * with unused upper index bits; pixel zero always uses palettes[0]. A supplied
 * 32768-entry table maps RGB555 colors; NULL expands RGB555 linearly to ARGB.
 * A short output or an output overlapping border/table storage is ignored. */
void cupid_gb_sgb_render_borrowed_border(const CupidGbSgbBorder *border, uint32_t *pixels,
                                         size_t pixel_count, const uint32_t *color_table);

#ifdef __cplusplus
}
#endif

#endif

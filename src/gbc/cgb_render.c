/**
 * @file    cgb_render.c
 * @brief   Direct color scanline rendering for callers of the core API.
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

/* Direct scanline rendering remains available to callers of the core API.
 * Ordinary execution uses the persistent dot-driven pixel pipeline. */
#include "cupid/gbc/cgb.h"

#include "cgb_internal.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"

/**
 * @brief Reads a single RGB555 color from a CGB palette RAM block.
 *
 * Returns the 16-bit little-endian color stored at the slot for
 * `palette_number` and `color_index` within @p palette_ram.
 *
 * @param palette_ram   Pointer to a 64-byte BG or OBJ palette RAM buffer.
 * @param palette_number Palette slot index (0–7).
 * @param color_index   Color index within the palette (0–3).
 *
 * @return The RGB555 color value.
 */
static uint16_t cupid_cgb_palette_color(const uint8_t *palette_ram, uint8_t palette_number,
                                        uint8_t color_index) {
    size_t color_offset = (size_t)palette_number * 8u + (size_t)color_index * 2u;

    return (uint16_t)(((uint16_t)palette_ram[color_offset] |
                       ((uint16_t)palette_ram[color_offset + 1u] << 8u)) &
                      0x7fffu);
}

/**
 * @brief Renders one CGB scanline into the frame buffer.
 *
 * Implements the full CGB background/window/sprite pipeline:
 *   - BG and Window tiles are fetched from the signed or unsigned tile
 *     data area (controlled by LCDC bit 4) using the tile attribute byte
 *     from VRAM bank 1 for flip flags, palette number, and tile bank.
 *   - The BG priority bit (attribute bit 7) and LCDC master priority
 *     (bit 0) are tracked per pixel to resolve BG-over-sprite conflicts.
 *   - Sprites read tile data from the bank selected by OAM attribute
 *     bit 3; color 0 is transparent; up to 10 sprites per scanline are
 *     processed with earlier OAM entries taking priority.
 *
 * Both `gb->frame_buffer` (shade indices) and `gb->frame_buffer_color`
 * (RGB555 colors from CGB palette RAM) are written.
 *
 * @param gb         Pointer to the Game Boy state.
 * @param lcdc       The LCDC register value for this scanline.
 * @param ly         The current scanline number (0–143).
 * @param base_index Starting offset into the frame buffers for this line
 *                   (`ly * CUPID_GB_SCREEN_WIDTH`).
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_cgb_render_scanline(CupidGb *gb, uint8_t lcdc, uint8_t ly, size_t base_index) {
    uint8_t bg_color_indices[CUPID_GB_SCREEN_WIDTH];
    uint8_t bg_priority[CUPID_GB_SCREEN_WIDTH];
    bool window_visible = false;
    uint8_t scx;
    uint8_t scy;
    uint8_t wy;
    uint8_t wx;
    unsigned int wx_screen;
    unsigned int x;

    if (gb == 0) {
        return;
    }

    scx = gb->io_registers[CUPID_GB_IO_SCX];
    scy = gb->io_registers[CUPID_GB_IO_SCY];
    wy = gb->io_registers[CUPID_GB_IO_WY];
    wx = gb->io_registers[CUPID_GB_IO_WX];
    wx_screen = (wx >= 7u) ? (unsigned int)(wx - 7u) : 0u;

    for (x = 0u; x < CUPID_GB_SCREEN_WIDTH; ++x) {
        bool use_window = false;
        size_t tile_map_base;
        size_t tile_map_index;
        uint8_t tile_number;
        uint8_t attributes;
        uint8_t pixel_x;
        uint8_t pixel_y;
        uint8_t tile_line;
        uint8_t bit_index;
        size_t tile_address;
        size_t tile_bank_base;
        uint8_t low;
        uint8_t high;
        uint8_t color_index;
        uint8_t palette_number;

        if ((lcdc & 0x20u) != 0u && (int)ly >= (int)wy && wx_screen < CUPID_GB_SCREEN_WIDTH &&
            x >= wx_screen) {
            use_window = true;
            window_visible = true;
        }

        if (use_window) {
            uint8_t win_x = (uint8_t)(x - wx_screen);
            uint8_t win_y = gb->window_line_counter;

            tile_map_base = (lcdc & 0x40u) != 0u ? 0x1c00u : 0x1800u;
            tile_map_index = tile_map_base + (size_t)((win_y >> 3u) * 32u) + (size_t)(win_x >> 3u);
            pixel_x = win_x;
            pixel_y = win_y;
        } else {
            uint8_t bg_x = (uint8_t)(x + scx);
            uint8_t bg_y = (uint8_t)(ly + scy);

            tile_map_base = (lcdc & 0x08u) != 0u ? 0x1c00u : 0x1800u;
            tile_map_index = tile_map_base + (size_t)((bg_y >> 3u) * 32u) + (size_t)(bg_x >> 3u);
            pixel_x = bg_x;
            pixel_y = bg_y;
        }

        tile_number = gb->video_ram[tile_map_index];
        attributes = gb->video_ram[0x2000u + tile_map_index];
        if ((attributes & 0x40u) != 0u) {
            pixel_y = (uint8_t)((pixel_y & 0xf8u) | (7u - (pixel_y & 0x07u)));
        }
        if ((attributes & 0x20u) != 0u) {
            pixel_x = (uint8_t)((pixel_x & 0xf8u) | (7u - (pixel_x & 0x07u)));
        }

        if ((lcdc & 0x10u) != 0u) {
            tile_address = (size_t)tile_number * 16u;
        } else {
            tile_address = (size_t)(0x1000 + ((int16_t)(int8_t)tile_number * 16));
        }

        tile_bank_base = ((attributes & 0x08u) != 0u) ? 0x2000u : 0u;
        tile_line = (uint8_t)((pixel_y & 0x07u) * 2u);
        low = gb->video_ram[tile_bank_base + tile_address + tile_line];
        high = gb->video_ram[tile_bank_base + tile_address + tile_line + 1u];
        bit_index = (uint8_t)(7u - (pixel_x & 0x07u));
        color_index = (uint8_t)(((((unsigned)high >> bit_index) & 0x01u) << 1u) |
                                (((unsigned)low >> bit_index) & 0x01u));
        palette_number = (uint8_t)(attributes & 0x07u);

        gb->frame_buffer[base_index + x] = color_index;
        gb->frame_buffer_color[base_index + x] =
            cupid_cgb_palette_color(gb->cgb_bg_palette_ram, palette_number, color_index);
        bg_color_indices[x] = color_index;
        bg_priority[x] = (uint8_t)((attributes & 0x80u) != 0u);
    }

    if (window_visible) {
        gb->window_line_counter = (uint8_t)(gb->window_line_counter + 1u);
    }

    if ((lcdc & 0x02u) != 0u) {
        int obj_height = ((lcdc & 0x04u) != 0u) ? 16 : 8;
        unsigned int sprite_count = 0u;
        unsigned int i;
        bool sprite_drawn[CUPID_GB_SCREEN_WIDTH] = {false};

        for (i = 0u; i < 40u && sprite_count < 10u; ++i) {
            int obj_y = (int)gb->object_attribute_memory[i * 4u] - 16;
            int obj_x = (int)gb->object_attribute_memory[i * 4u + 1u] - 8;
            uint8_t tile_idx = gb->object_attribute_memory[i * 4u + 2u];
            uint8_t attrs = gb->object_attribute_memory[i * 4u + 3u];
            int tile_row;
            size_t tile_addr;
            size_t tile_bank_base;
            uint8_t olow;
            uint8_t ohigh;
            int px;

            if ((int)ly < obj_y || (int)ly >= obj_y + obj_height) {
                continue;
            }
            ++sprite_count;

            tile_row = (int)ly - obj_y;

            if (obj_height == 16) {
                if ((attrs & 0x40u) != 0u) {
                    tile_row = obj_height - 1 - tile_row;
                }
                if (tile_row >= 8) {
                    tile_idx = (uint8_t)(tile_idx | 0x01u);
                    tile_row -= 8;
                } else {
                    tile_idx = (uint8_t)(tile_idx & 0xfeu);
                }
            } else if ((attrs & 0x40u) != 0u) {
                tile_row = 7 - tile_row;
            }

            tile_bank_base = ((attrs & 0x08u) != 0u) ? 0x2000u : 0u;
            tile_addr = tile_bank_base + (size_t)tile_idx * 16u + (size_t)(tile_row * 2);
            olow = gb->video_ram[tile_addr];
            ohigh = gb->video_ram[tile_addr + 1u];

            for (px = 0; px < 8; ++px) {
                int screen_x;
                uint8_t bit_pos;
                uint8_t color_index;
                bool bg_wins;

                if ((attrs & 0x20u) != 0u) {
                    screen_x = obj_x + (7 - px);
                    bit_pos = (uint8_t)px;
                } else {
                    screen_x = obj_x + px;
                    bit_pos = (uint8_t)px;
                }

                if (screen_x < 0 || screen_x >= (int)CUPID_GB_SCREEN_WIDTH ||
                    sprite_drawn[screen_x]) {
                    continue;
                }

                color_index = (uint8_t)(((((unsigned)ohigh >> (7u - bit_pos)) & 0x01u) << 1u) |
                                        (((unsigned)olow >> (7u - bit_pos)) & 0x01u));
                if (color_index == 0u) {
                    continue;
                }
                /* BG priority must not expose an otherwise obscured object. */
                sprite_drawn[screen_x] = true;

                bg_wins = bg_color_indices[screen_x] != 0u && (lcdc & 0x01u) != 0u &&
                          (bg_priority[screen_x] != 0u || (attrs & 0x80u) != 0u);
                if (bg_wins) {
                    continue;
                }

                gb->frame_buffer_color[base_index + (size_t)screen_x] = cupid_cgb_palette_color(
                    gb->cgb_obj_palette_ram, (uint8_t)(attrs & 0x07u), color_index);
            }
        }
    }
}

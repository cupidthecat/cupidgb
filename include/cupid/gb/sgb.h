/**
 * @file    sgb.h
 * @brief   Super Game Boy state and command, palette, border, and input interfaces.
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

#ifndef CUPID_GB_SGB_H
#define CUPID_GB_SGB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CUPID_SGB_SCREEN_WIDTH 256u
#define CUPID_SGB_SCREEN_HEIGHT 224u
#define CUPID_SGB_VIEWPORT_X 48u
#define CUPID_SGB_VIEWPORT_Y 40u
#define CUPID_SGB_TILE_WIDTH 32u
#define CUPID_SGB_TILE_HEIGHT 28u
#define CUPID_SGB_ATTR_WIDTH 20u
#define CUPID_SGB_ATTR_HEIGHT 18u
#define CUPID_SGB_PACKET_BYTES 16u
#define CUPID_SGB_MAX_PACKETS 7u
#define CUPID_SGB_MAX_COMMAND_BYTES (CUPID_SGB_PACKET_BYTES * CUPID_SGB_MAX_PACKETS)
#define CUPID_SGB_HEADER_BYTES 84u
#define CUPID_SGB_FRAME_DOTS 70224u
#define CUPID_SGB_TRANSFER_FRAMES 3u
#define CUPID_SGB_BORDER_TRANSITION_FRAMES 105u
#define CUPID_SGB_BORDER_PALETTE_COLORS (4u * 16u)

enum {
    CUPID_SGB_MASK_DISABLED = 0u,
    CUPID_SGB_MASK_FREEZE = 1u,
    CUPID_SGB_MASK_BLACK = 2u,
    CUPID_SGB_MASK_COLOR0 = 3u
};

enum {
    CUPID_SGB_TRANSFER_NONE,
    CUPID_SGB_TRANSFER_PALETTES,
    CUPID_SGB_TRANSFER_ATTRIBUTES,
    CUPID_SGB_TRANSFER_TILES_LOW,
    CUPID_SGB_TRANSFER_TILES_HIGH,
    CUPID_SGB_TRANSFER_BORDER
};

typedef struct CupidGbSgbTransfer {
    uint32_t frame_dots;
    uint8_t destination;
    uint8_t frames_remaining;
    uint8_t border_transition;
    uint8_t tiles[256u * 32u];
    uint16_t map[CUPID_SGB_TILE_WIDTH * CUPID_SGB_TILE_HEIGHT];
    uint16_t palettes[CUPID_SGB_BORDER_PALETTE_COLORS];
} CupidGbSgbTransfer;

typedef struct CupidGbSgb {
    bool enabled; /* Cartridge declaration; independent of physical packet reception. */
    bool commands_disabled;
    uint8_t received_header[CUPID_SGB_HEADER_BYTES];
    uint8_t player_count;
    uint8_t current_player;
    uint8_t mask_mode;

    uint8_t command[CUPID_SGB_MAX_COMMAND_BYTES];
    uint16_t command_write_index;
    bool ready_for_pulse;
    bool ready_for_write;
    bool ready_for_stop;

    uint8_t attribute_map[CUPID_SGB_ATTR_WIDTH * CUPID_SGB_ATTR_HEIGHT];
    uint8_t attribute_files[0x0fd2];
    uint16_t screen_palettes[4u * 4u];
    uint16_t ram_palettes[512u * 4u];

    uint8_t border_tiles[256u * 32u];
    uint16_t border_map[CUPID_SGB_TILE_WIDTH * CUPID_SGB_TILE_HEIGHT];
    uint16_t border_palettes[8u * 16u];

    uint8_t freeze_buffer[160u * 144u]; /* Last complete screen, held while masked. */
    CupidGbSgbTransfer transfer;
} CupidGbSgb;

struct CupidGb;

void cupid_gb_sgb_init(struct CupidGb *gb);
/* Firmware reentry clears packet history and pad selection while retaining
 * palette, border, mask and transfer storage. Cold/reset initialization clears all. */
void cupid_gb_sgb_begin_boot(struct CupidGb *gb);
void cupid_gb_sgb_apply_compatibility_palette(struct CupidGb *gb);
void cupid_gb_sgb_write_joyp(struct CupidGb *gb, uint8_t value);
void cupid_gb_sgb_clock(struct CupidGb *gb, uint32_t dots);
void cupid_gb_sgb_present(struct CupidGb *gb, bool complete, uint32_t dots_after_frame);
void cupid_gb_sgb_render_argb(const struct CupidGb *gb, uint32_t *pixels, size_t pixel_count);
/* A supplied color table maps all 32,768 RGB555 values to output pixels. */
void cupid_gb_sgb_render_with_color_table(const struct CupidGb *gb, uint32_t *pixels,
                                          size_t pixel_count, const uint32_t *color_table);
/* Cartridge declaration only; use commands_enabled for packet permission. */
bool cupid_gb_sgb_active(const struct CupidGb *gb);
/* Physical adapter receiver permission, including ordinary cartridges in firmware. */
bool cupid_gb_sgb_commands_enabled(const struct CupidGb *gb);

#endif

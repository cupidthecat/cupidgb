/**
 * @file sgb.c
 * @brief Super Game Boy (SGB/SGB2) enhancement emulation.
 *
 * Implements the SGB command protocol, attribute/palette management,
 * border rendering, and the JOYP-based serial packet interface.
 *
 * Supported SGB commands:
 *   - PAL01/23/03/12, PAL_SET, PAL_TRN  – palette programming
 *   - ATTR_BLK, ATTR_LIN, ATTR_DIV, ATTR_CHR, ATTR_TRN, ATTR_SET
 *                                         – per-tile attribute map editing
 *   - CHR_TRN, PCT_TRN                   – border tile and tilemap upload
 *   - MLT_REQ                            – multi-player controller select
 *   - MASK_EN                            – screen mask control
 *
 * The SGB command stream is clocked in via writes to the P1/JOYP register
 * (0xFF00). @ref cupid_gb_sgb_write_joyp should be called on every JOYP
 * write while the SGB is active.
 */

#include "cupid/gb/sgb.h"

#include <string.h>

#include "cupid/common/log.h"
#include "cupid/gb/gb.h"
#include "cupid/gbc/cgb.h"

enum {
    CUPID_SGB_CMD_PAL01 = 0x00,
    CUPID_SGB_CMD_PAL23 = 0x01,
    CUPID_SGB_CMD_PAL03 = 0x02,
    CUPID_SGB_CMD_PAL12 = 0x03,
    CUPID_SGB_CMD_ATTR_BLK = 0x04,
    CUPID_SGB_CMD_ATTR_LIN = 0x05,
    CUPID_SGB_CMD_ATTR_DIV = 0x06,
    CUPID_SGB_CMD_ATTR_CHR = 0x07,
    CUPID_SGB_CMD_PAL_SET = 0x0A,
    CUPID_SGB_CMD_PAL_TRN = 0x0B,
    CUPID_SGB_CMD_DATA_SND = 0x0F,
    CUPID_SGB_CMD_MLT_REQ = 0x11,
    CUPID_SGB_CMD_CHR_TRN = 0x13,
    CUPID_SGB_CMD_PCT_TRN = 0x14,
    CUPID_SGB_CMD_ATTR_TRN = 0x15,
    CUPID_SGB_CMD_ATTR_SET = 0x16,
    CUPID_SGB_CMD_MASK_EN = 0x17
};

/**
 * @brief Reads a little-endian 16-bit value from a byte buffer.
 *
 * @param data Pointer to at least 2 bytes of data.
 *
 * @return The 16-bit value at @p data, interpreted as little-endian.
 */
static uint16_t cupid_gb_sgb_read_le16(const uint8_t *data) {
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8u));
}

/**
 * @brief Converts a 15-bit RGB555 color to a 32-bit ARGB8888 value.
 *
 * The upper channel bits are replicated into the low bits of the
 * output byte. Alpha is always set to 0xFF.
 *
 * @param color The 15-bit RGB555 color (bits 14–0 = 0BBBBBGGGGGRRRRR).
 *
 * @return The corresponding 0xAARRGGBB value with full alpha.
 */
static uint32_t cupid_gb_sgb_rgb15_to_argb(uint16_t color) {
    uint32_t r = (uint32_t)(color & 0x1fu);
    uint32_t g = (uint32_t)((color >> 5u) & 0x1fu);
    uint32_t b = (uint32_t)((color >> 10u) & 0x1fu);

    r = (r << 3u) | (r >> 2u);
    g = (g << 3u) | (g >> 2u);
    b = (b << 3u) | (b >> 2u);

    return 0xff000000u | (r << 16u) | (g << 8u) | b;
}

/**
 * @brief Fills all four SGB screen palettes with the default greyscale ramp.
 *
 * Writes the four standard DMG shades (white → black) into every palette
 * slot so the screen has sensible colors before any PAL command is received.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_sgb_set_default_palettes(CupidGb *gb) {
    static const uint16_t default_palette[4] = {0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u};
    unsigned palette;

    for (palette = 0u; palette < 4u; ++palette) {
        memcpy(&gb->sgb.screen_palettes[palette * 4u], default_palette, sizeof(default_palette));
    }
}

/**
 * @brief Loads an attribute file from the SGB attribute file RAM into the live attribute map.
 *
 * Each attribute file is 90 bytes (360 2-bit entries) which covers the
 * full 20×18 attribute map. The selected file is unpacked into
 * `gb->sgb.attribute_map` as one byte per tile.
 *
 * @param gb         Pointer to the Game Boy state.
 * @param file_index The attribute file index (0–0x2C). Out-of-range values are ignored.
 */
static void cupid_gb_sgb_load_attr_file(CupidGb *gb, unsigned file_index) {
    uint8_t *output;
    unsigned i;

    if (file_index > 0x2cu) {
        return;
    }

    output = gb->sgb.attribute_map;
    for (i = 0u; i < 90u; ++i) {
        uint8_t byte = gb->sgb.attribute_files[file_index * 90u + i];
        unsigned j;

        for (j = 0u; j < 4u; ++j) {
            *(output++) = (uint8_t)(byte >> 6u);
            byte = (uint8_t)(byte << 2u);
        }
    }
}

/**
 * @brief Handles a PAL01/PAL23/PAL03/PAL12 palette command.
 *
 * Copies color 0 from the command data into all four screen palettes
 * (enforcing shared color 0), then writes the three unique colors of
 * the @p first and @p second palette from the command payload.
 *
 * @param gb     Pointer to the Game Boy state.
 * @param first  Index of the first palette to update (0–3).
 * @param second Index of the second palette to update (0–3).
 */
static void cupid_gb_sgb_pal_command(CupidGb *gb, unsigned first, unsigned second) {
    unsigned i;
    uint16_t color0 = cupid_gb_sgb_read_le16(&gb->sgb.command[1]);

    gb->sgb.screen_palettes[0] = color0;
    gb->sgb.screen_palettes[4] = color0;
    gb->sgb.screen_palettes[8] = color0;
    gb->sgb.screen_palettes[12] = color0;

    for (i = 0u; i < 3u; ++i) {
        gb->sgb.screen_palettes[first * 4u + i + 1u] =
            cupid_gb_sgb_read_le16(&gb->sgb.command[3u + i * 2u]);
        gb->sgb.screen_palettes[second * 4u + i + 1u] =
            cupid_gb_sgb_read_le16(&gb->sgb.command[9u + i * 2u]);
    }
}

/**
 * @brief Captures the first 4 KiB of VRAM into a caller-supplied buffer.
 *
 * Used by PAL_TRN, ATTR_TRN, CHR_TRN, and PCT_TRN to snapshot the
 * tile data that the game has pre-loaded into VRAM bank 0.
 *
 * @param gb     Pointer to the Game Boy state.
 * @param buffer Destination buffer; must be at least 0x1000 bytes.
 */
static void cupid_gb_sgb_capture_transfer_buffer(const CupidGb *gb, uint8_t *buffer) {
    memcpy(buffer, gb->video_ram, 0x1000u);
}

/**
 * @brief Executes the PAL_TRN command: uploads 512 palettes from VRAM.
 *
 * Reads 4 KiB from VRAM and interprets it as 512 palettes of 4 RGB555
 * colors each, storing them in `gb->sgb.ram_palettes`.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_sgb_apply_pal_trn(CupidGb *gb) {
    uint8_t buffer[0x1000u];
    unsigned i;

    cupid_gb_sgb_capture_transfer_buffer(gb, buffer);
    for (i = 0u; i < 512u * 4u; ++i) {
        gb->sgb.ram_palettes[i] = cupid_gb_sgb_read_le16(&buffer[i * 2u]);
    }
}

/**
 * @brief Executes the ATTR_TRN command: uploads attribute files from VRAM.
 *
 * Reads 4 KiB from VRAM and stores it verbatim into
 * `gb->sgb.attribute_files`, providing up to 45 attribute files of
 * 90 bytes each.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_sgb_apply_attr_trn(CupidGb *gb) {
    uint8_t buffer[0x1000u];

    cupid_gb_sgb_capture_transfer_buffer(gb, buffer);
    memcpy(gb->sgb.attribute_files, buffer, sizeof(gb->sgb.attribute_files));
}

/**
 * @brief Executes a CHR_TRN command: uploads one half of the border tile set from VRAM.
 *
 * The SGB border uses 256 tiles, transferred in two 4 KiB blocks.
 * The @p high flag selects which block is written:
 *   - `false` → lower 4 KiB (tiles 0–127) at offset 0x0000
 *   - `true`  → upper 4 KiB (tiles 128–255) at offset 0x1000
 *
 * @param gb   Pointer to the Game Boy state.
 * @param high `false` for the lower tile block, `true` for the upper block.
 */
static void cupid_gb_sgb_apply_chr_trn(CupidGb *gb, bool high) {
    uint8_t buffer[0x1000u];

    cupid_gb_sgb_capture_transfer_buffer(gb, buffer);
    memcpy(&gb->sgb.border_tiles[high ? 0x1000u : 0u], buffer, sizeof(buffer));
}

/**
 * @brief Executes the PCT_TRN command: uploads the border tilemap and palettes from VRAM.
 *
 * Reads 4 KiB from VRAM. The first 2 KiB are decoded as the
 * 32×28 border tilemap (one 16-bit entry per tile), and the following
 * 512 bytes are decoded as the 8 border palettes of 16 RGB555 colors each.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_sgb_apply_pct_trn(CupidGb *gb) {
    uint8_t buffer[0x1000u];
    unsigned i;

    cupid_gb_sgb_capture_transfer_buffer(gb, buffer);
    for (i = 0u; i < CUPID_SGB_TILE_WIDTH * CUPID_SGB_TILE_HEIGHT; ++i) {
        gb->sgb.border_map[i] = cupid_gb_sgb_read_le16(&buffer[i * 2u]);
    }
    for (i = 0u; i < 8u * 16u; ++i) {
        gb->sgb.border_palettes[i] = cupid_gb_sgb_read_le16(&buffer[0x800u + i * 2u]);
    }
}

/* Decode six-byte rectangle records without relying on host structure layout. */
static void cupid_gb_sgb_attribute_blocks(CupidGb *gb) {
    const uint8_t *payload = gb->sgb.command + 2;
    const unsigned records = gb->sgb.command[1];
    if (records > (sizeof(gb->sgb.command) - 2u) / 6u) {
        return;
    }

    for (unsigned record = 0; record < records; ++record, payload += 6) {
        const unsigned control = payload[0] & 7u;
        /* Region indices are interior, perimeter, exterior. A disabled region
         * keeps its current palette. Selecting only one side extends that
         * side's palette to the perimeter, including degenerate rectangles.
         */
        int palette[3] = {-1, -1, -1};
        for (unsigned region = 0; region < 3; ++region) {
            if (control & (1u << region)) {
                palette[region] = (payload[1] >> (2u * region)) & 3u;
            }
        }
        if (control == 1u || control == 4u) {
            palette[1] = palette[control == 1u ? 0 : 2];
        }

        const unsigned x0 = payload[2] & 31u, y0 = payload[3] & 31u;
        const unsigned x1 = payload[4] & 31u, y1 = payload[5] & 31u;
        for (unsigned cell = 0; cell < sizeof(gb->sgb.attribute_map); ++cell) {
            const unsigned column = cell % CUPID_SGB_ATTR_WIDTH;
            const unsigned row = cell / CUPID_SGB_ATTR_WIDTH;
            unsigned region = 2;
            if (column >= x0 && column <= x1 && row >= y0 && row <= y1) {
                const bool perimeter = column == x0 || column == x1 || row == y0 || row == y1;
                region = perimeter ? 1u : 0u;
            }
            if (palette[region] >= 0) {
                gb->sgb.attribute_map[cell] = (uint8_t)palette[region];
            }
        }
    }
}

static void cupid_gb_sgb_attribute_characters(CupidGb *gb) {
    const uint8_t *bytes = gb->sgb.command;
    const unsigned start_x = bytes[1], start_y = bytes[2];
    if (start_x >= CUPID_SGB_ATTR_WIDTH || start_y >= CUPID_SGB_ATTR_HEIGHT) {
        return;
    }

    const bool vertical = bytes[5] != 0;
    const unsigned stride = vertical ? CUPID_SGB_ATTR_HEIGHT : CUPID_SGB_ATTR_WIDTH;
    unsigned cursor = vertical ? start_x * stride + start_y : start_y * stride + start_x;
    unsigned count = cupid_gb_sgb_read_le16(bytes + 3);
    const unsigned capacity = (unsigned)sizeof(gb->sgb.attribute_map) - cursor;
    if (count > capacity) {
        count = capacity;
    }
    for (unsigned item = 0; item < count; ++item, ++cursor) {
        const unsigned offset =
            vertical ? (cursor % stride) * CUPID_SGB_ATTR_WIDTH + cursor / stride : cursor;
        const unsigned shift = 6u - 2u * (item % 4u);
        gb->sgb.attribute_map[offset] = (uint8_t)(((unsigned)bytes[6u + item / 4u] >> shift) & 3u);
    }
}

static void cupid_gb_sgb_handle_command(CupidGb *gb) {
    uint8_t command = (uint8_t)(gb->sgb.command[0] >> 3u);

    if ((gb->sgb.command[0] & 0x07u) == 0u) {
        return;
    }

    switch (command) {
        case CUPID_SGB_CMD_PAL01:
            cupid_gb_sgb_pal_command(gb, 0u, 1u);
            break;
        case CUPID_SGB_CMD_PAL23:
            cupid_gb_sgb_pal_command(gb, 2u, 3u);
            break;
        case CUPID_SGB_CMD_PAL03:
            cupid_gb_sgb_pal_command(gb, 0u, 3u);
            break;
        case CUPID_SGB_CMD_PAL12:
            cupid_gb_sgb_pal_command(gb, 1u, 2u);
            break;
        case CUPID_SGB_CMD_ATTR_BLK:
            cupid_gb_sgb_attribute_blocks(gb);
            break;
        case CUPID_SGB_CMD_ATTR_LIN: {
            uint8_t count = gb->sgb.command[1];
            unsigned i;

            if (count > sizeof(gb->sgb.command) - 2u) {
                return;
            }

            for (i = 0u; i < count; ++i) {
                bool horizontal = (gb->sgb.command[2u + i] & 0x80u) != 0u;
                uint8_t palette = (uint8_t)((gb->sgb.command[2u + i] >> 5u) & 0x03u);
                uint8_t line = (uint8_t)(gb->sgb.command[2u + i] & 0x1fu);
                unsigned j;

                if (horizontal) {
                    if (line >= CUPID_SGB_ATTR_HEIGHT) {
                        continue;
                    }
                    for (j = 0u; j < CUPID_SGB_ATTR_WIDTH; ++j) {
                        gb->sgb.attribute_map[j + line * CUPID_SGB_ATTR_WIDTH] = palette;
                    }
                } else {
                    if (line >= CUPID_SGB_ATTR_WIDTH) {
                        continue;
                    }
                    for (j = 0u; j < CUPID_SGB_ATTR_HEIGHT; ++j) {
                        gb->sgb.attribute_map[line + j * CUPID_SGB_ATTR_WIDTH] = palette;
                    }
                }
            }
            break;
        }
        case CUPID_SGB_CMD_ATTR_DIV: {
            uint8_t high_palette = (uint8_t)(gb->sgb.command[1] & 0x03u);
            uint8_t low_palette = (uint8_t)((gb->sgb.command[1] >> 2u) & 0x03u);
            uint8_t middle_palette = (uint8_t)((gb->sgb.command[1] >> 4u) & 0x03u);
            bool horizontal = (gb->sgb.command[1] & 0x40u) != 0u;
            uint8_t line = (uint8_t)(gb->sgb.command[2] & 0x1fu);
            unsigned x;
            unsigned y;

            for (y = 0u; y < CUPID_SGB_ATTR_HEIGHT; ++y) {
                for (x = 0u; x < CUPID_SGB_ATTR_WIDTH; ++x) {
                    unsigned pos = horizontal ? y : x;
                    if (pos < line) {
                        gb->sgb.attribute_map[x + y * CUPID_SGB_ATTR_WIDTH] = low_palette;
                    } else if (pos == line) {
                        gb->sgb.attribute_map[x + y * CUPID_SGB_ATTR_WIDTH] = middle_palette;
                    } else {
                        gb->sgb.attribute_map[x + y * CUPID_SGB_ATTR_WIDTH] = high_palette;
                    }
                }
            }
            break;
        }
        case CUPID_SGB_CMD_ATTR_CHR:
            cupid_gb_sgb_attribute_characters(gb);
            break;
        case CUPID_SGB_CMD_PAL_SET: {
            unsigned palette;
            for (palette = 0u; palette < 4u; ++palette) {
                unsigned offset = 1u + palette * 2u;
                unsigned palette_index = gb->sgb.command[offset] +
                                         ((unsigned)(gb->sgb.command[offset + 1u] & 0x01u) << 8u);
                memcpy(&gb->sgb.screen_palettes[palette * 4u],
                       &gb->sgb.ram_palettes[palette_index * 4u], 4u * sizeof(uint16_t));
            }
            gb->sgb.screen_palettes[12] = gb->sgb.screen_palettes[8] = gb->sgb.screen_palettes[4] =
                gb->sgb.screen_palettes[0];
            if ((gb->sgb.command[9] & 0x80u) != 0u) {
                cupid_gb_sgb_load_attr_file(gb, gb->sgb.command[9] & 0x3fu);
            }
            if ((gb->sgb.command[9] & 0x40u) != 0u) {
                gb->sgb.mask_mode = CUPID_SGB_MASK_DISABLED;
            }
            break;
        }
        case CUPID_SGB_CMD_PAL_TRN:
            cupid_gb_sgb_apply_pal_trn(gb);
            break;
        case CUPID_SGB_CMD_DATA_SND:
            break;
        case CUPID_SGB_CMD_MLT_REQ: {
            const unsigned selection = gb->sgb.command[1] & 3u;
            const unsigned active = gb->sgb.current_player;
            gb->sgb.player_count = (uint8_t)(selection + 1u);
            if (selection == 2u) {
                /* The irregular three-pad request selects pad 1 or 3 and
                 * disables subsequent rotation on the P15 rising edge. */
                gb->sgb.current_player = (active == 1u || active == 2u) ? 2u : 0u;
            } else {
                gb->sgb.current_player = (uint8_t)(active & selection);
            }
            break;
        }
        case CUPID_SGB_CMD_CHR_TRN:
            cupid_gb_sgb_apply_chr_trn(gb, (gb->sgb.command[1] & 0x01u) != 0u);
            break;
        case CUPID_SGB_CMD_PCT_TRN:
            cupid_gb_sgb_apply_pct_trn(gb);
            break;
        case CUPID_SGB_CMD_ATTR_TRN:
            cupid_gb_sgb_apply_attr_trn(gb);
            break;
        case CUPID_SGB_CMD_ATTR_SET:
            cupid_gb_sgb_load_attr_file(gb, gb->sgb.command[1] & 0x3fu);
            if ((gb->sgb.command[1] & 0x40u) != 0u) {
                gb->sgb.mask_mode = CUPID_SGB_MASK_DISABLED;
            }
            break;
        case CUPID_SGB_CMD_MASK_EN:
            gb->sgb.mask_mode = (uint8_t)(gb->sgb.command[1] & 0x03u);
            if (gb->sgb.mask_mode == CUPID_SGB_MASK_FREEZE) {
                memcpy(gb->sgb.freeze_buffer, gb->frame_buffer, sizeof(gb->sgb.freeze_buffer));
            }
            break;
        default:
            if (command == 0x08u && (gb->sgb.command[1] & (uint8_t)~0x80u) == 0u &&
                (gb->sgb.command[2] & (uint8_t)~0x80u) == 0u) {
                break;
            }
            cupid_log_infof("Ignoring unsupported SGB command 0x%02X", command);
            break;
    }
}

/**
 * @brief Initializes the SGB subsystem state.
 *
 * Zeroes all SGB fields and sets the default greyscale palettes.
 * The player count is reset to 1 (single-player mode).
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_gb_sgb_init(CupidGb *gb) {
    if (gb == NULL) {
        return;
    }

    memset(&gb->sgb, 0, sizeof(gb->sgb));
    gb->sgb.player_count = 1u;
    cupid_gb_sgb_set_default_palettes(gb);
}

/**
 * @brief Applies a CGB compatibility palette to the SGB screen palettes.
 *
 * Queries the CGB compatibility palette for the loaded ROM via
 * @ref cupid_cgb_get_compatibility_palette and writes the background
 * palette into all four SGB screen palette slots.
 *
 * Called after loading a ROM on an SGB/SGB2 when the cartridge does not
 * natively support SGB enhancements but should still display with
 * hardware-accurate colors.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL or no compatibility palette is found.
 */
void cupid_gb_sgb_apply_compatibility_palette(CupidGb *gb) {
    uint16_t bg[4];
    uint16_t obj0[4];
    uint16_t obj1[4];
    unsigned palette;

    if (gb == NULL) {
        return;
    }

    if (!cupid_cgb_get_compatibility_palette(gb, bg, obj0, obj1)) {
        return;
    }

    for (palette = 0u; palette < 4u; ++palette) {
        memcpy(&gb->sgb.screen_palettes[palette * 4u], bg, sizeof(bg));
    }
}

/**
 * @brief Returns whether the SGB enhancement layer is active.
 *
 * The SGB is considered active when the emulator is in SGB/SGB2 mode
 * and the cartridge declared SGB support (SGB flag byte = 0x03).
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return `true` if SGB enhancements are active, `false` otherwise.
 */
bool cupid_gb_sgb_active(const CupidGb *gb) {
    return gb != NULL && gb->sgb.enabled;
}

/* A data pulse is sampled when both select lines return high. The reset
 * handshake arms a packet; every 128 data pulses require a separate stop pulse.
 * Keeping the pending pulse in JOYP itself also preserves it across snapshots.
 */
void cupid_gb_sgb_write_joyp(CupidGb *gb, uint8_t value) {
    if (!cupid_gb_sgb_active(gb)) {
        return;
    }
    CupidGbSgb *receiver = &gb->sgb;
    const unsigned lines = (value >> 4) & 3u;
    const unsigned previous = (gb->io_registers[0] >> 4) & 3u;
    if ((lines & 2u) && !(previous & 2u) && !(receiver->player_count & 1u)) {
        receiver->current_player =
            (uint8_t)((receiver->current_player + 1u) & (receiver->player_count - 1u));
    }

    if (lines == 0) {
        receiver->ready_for_write = false;
        receiver->ready_for_pulse = false;
        /* A completed packet can continue into the next one. Resetting during
         * data or before its stop pulse instead discards the partial command.
         */
        const unsigned packet_bits = CUPID_SGB_PACKET_BYTES * 8u;
        if (!receiver->command_write_index || receiver->command_write_index % packet_bits ||
            receiver->ready_for_stop) {
            memset(receiver->command, 0, sizeof(receiver->command));
            receiver->command_write_index = 0;
            receiver->ready_for_stop = false;
        }
        return;
    }
    if (lines != 3) {
        return;
    }
    if (previous == 0) {
        receiver->ready_for_write = true;
        return;
    }
    if (previous == 3 || !receiver->ready_for_write) {
        return;
    }

    if (receiver->ready_for_stop) {
        unsigned packet_count = receiver->command[0] & 7u;
        if (!packet_count || (receiver->command[0] & 0xf1u) == 0xf1u) {
            packet_count = 1;
        }
        if (receiver->command_write_index == packet_count * CUPID_SGB_PACKET_BYTES * 8u) {
            cupid_gb_sgb_handle_command(gb);
            memset(receiver->command, 0, sizeof(receiver->command));
            receiver->command_write_index = 0;
        }
        receiver->ready_for_stop = false;
        receiver->ready_for_write = false;
        return;
    }

    const unsigned position = receiver->command_write_index;
    if (position < sizeof(receiver->command) * 8u) {
        const unsigned bit = previous == 1;
        receiver->command[position / 8u] |= (uint8_t)(bit << (position % 8u));
        receiver->command_write_index = (uint16_t)(position + 1u);
        receiver->ready_for_stop =
            receiver->command_write_index % (CUPID_SGB_PACKET_BYTES * 8u) == 0;
    }
}

/**
 * @brief Renders a full SGB frame into an ARGB8888 pixel buffer.
 *
 * Composites the Game Boy screen pixels (160×144) and the SGB border
 * (256×224) into @p pixels using the active attribute map, screen
 * palettes, border tilemap, border tiles, and border palettes.
 *
 * The Game Boy viewport is placed at (@ref CUPID_SGB_VIEWPORT_X,
 * @ref CUPID_SGB_VIEWPORT_Y) within the output buffer. Border tiles
 * that overlap the viewport with color index 0 are left transparent
 * (the screen pixels show through). Outside the viewport, valid tiles
 * use the shared screen backdrop for color index 0. Unavailable tile
 * entries leave the viewport intact and use black in the surrounding area.
 * The active mask mode is respected:
 *   - @ref CUPID_SGB_MASK_DISABLED: show the current screen
 *   - @ref CUPID_SGB_MASK_FREEZE: show the saved screen
 *   - @ref CUPID_SGB_MASK_BLACK: viewport filled with black
 *   - @ref CUPID_SGB_MASK_COLOR0: viewport filled with palette color 0
 *
 * @param gb          Pointer to the Game Boy state.
 * @param pixels      Output buffer of at least
 *                    `CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT`
 *                    `uint32_t` elements.
 * @param pixel_count Total number of elements in @p pixels; must be at least
 *                    `CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT`.
 *
 * @note Does nothing if @p gb or @p pixels is NULL, or @p pixel_count
 *       is too small.
 */
void cupid_gb_sgb_render_argb(const CupidGb *gb, uint32_t *pixels, size_t pixel_count) {
    if (!gb || !pixels || pixel_count < CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT) {
        return;
    }

    const CupidGbSgb *sgb = &gb->sgb;
    const uint32_t backdrop = cupid_gb_sgb_rgb15_to_argb(sgb->screen_palettes[0]);
    const uint8_t *screen =
        sgb->mask_mode == CUPID_SGB_MASK_FREEZE ? sgb->freeze_buffer : gb->frame_buffer;
    uint32_t screen_colors[4][4];
    uint32_t border_colors[4][16];
    for (unsigned palette = 0; palette < 4; ++palette) {
        for (unsigned color = 0; color < 4; ++color) {
            if (sgb->mask_mode == CUPID_SGB_MASK_BLACK) {
                screen_colors[palette][color] = 0xff000000u;
            } else if (sgb->mask_mode == CUPID_SGB_MASK_COLOR0 || color == 0) {
                screen_colors[palette][color] = backdrop;
            } else {
                screen_colors[palette][color] =
                    cupid_gb_sgb_rgb15_to_argb(sgb->screen_palettes[palette * 4u + color]);
            }
        }
        for (unsigned color = 1; color < 16; ++color) {
            border_colors[palette][color] =
                cupid_gb_sgb_rgb15_to_argb(sgb->border_palettes[palette * 16u + color]);
        }
    }

    const unsigned view_tile_x = CUPID_SGB_VIEWPORT_X / 8u;
    const unsigned view_tile_y = CUPID_SGB_VIEWPORT_Y / 8u;
    for (unsigned tile_y = 0; tile_y < CUPID_SGB_TILE_HEIGHT; ++tile_y) {
        for (unsigned tile_x = 0; tile_x < CUPID_SGB_TILE_WIDTH; ++tile_x) {
            const bool in_viewport =
                tile_x >= view_tile_x && tile_x < view_tile_x + CUPID_SGB_ATTR_WIDTH &&
                tile_y >= view_tile_y && tile_y < view_tile_y + CUPID_SGB_ATTR_HEIGHT;
            const uint8_t *source = NULL;
            const uint32_t *lcd_palette = NULL;
            if (in_viewport) {
                const unsigned lcd_x = tile_x - view_tile_x;
                const unsigned lcd_y = tile_y - view_tile_y;
                source = screen + 8u * (lcd_y * CUPID_GB_SCREEN_WIDTH + lcd_x);
                lcd_palette =
                    screen_colors[sgb->attribute_map[lcd_y * CUPID_SGB_ATTR_WIDTH + lcd_x] & 3u];
            }

            const uint16_t entry = sgb->border_map[tile_y * CUPID_SGB_TILE_WIDTH + tile_x];
            /* The map has a ten-bit tile index, but the adapter uploads only
             * 256 tiles. Entries outside that range do not draw an overlay. */
            const bool tile_available = (entry & 0x0300u) == 0;
            const uint8_t *tile = sgb->border_tiles + 32u * (entry & 0xffu);
            const uint32_t *palette = border_colors[(entry >> 10u) & 3u];
            const unsigned x_flip = (entry & 0x4000u) ? 7u : 0u;
            const unsigned y_flip = (entry & 0x8000u) ? 7u : 0u;
            uint32_t *output = pixels + tile_y * 8u * CUPID_SGB_SCREEN_WIDTH + tile_x * 8u;

            for (unsigned y = 0; y < 8; ++y) {
                uint32_t *output_row = output + y * CUPID_SGB_SCREEN_WIDTH;
                const uint8_t *lcd_row = source ? source + y * CUPID_GB_SCREEN_WIDTH : NULL;
                const unsigned row = 2u * (y ^ y_flip);
                for (unsigned x = 0; x < 8; ++x) {
                    unsigned color = 0;
                    if (tile_available) {
                        const unsigned shift = 7u - (x ^ x_flip);
                        color = (((unsigned)tile[row] >> shift) & 1u) |
                                ((((unsigned)tile[row + 1] >> shift) & 1u) << 1u) |
                                ((((unsigned)tile[row + 16] >> shift) & 1u) << 2u) |
                                ((((unsigned)tile[row + 17] >> shift) & 1u) << 3u);
                    }
                    if (color) {
                        output_row[x] = palette[color];
                    } else if (lcd_row) {
                        output_row[x] = lcd_palette[lcd_row[x] & 3u];
                    } else {
                        output_row[x] = tile_available ? backdrop : 0xff000000u;
                    }
                }
            }
        }
    }
}

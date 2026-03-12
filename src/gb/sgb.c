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

#include "cupid/gb/gb.h"
#include "cupid/gbc/cgb.h"
#include "cupid/common/log.h"

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
static uint16_t cupid_gb_sgb_read_le16(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8u));
}

/**
 * @brief Converts a 15-bit RGB555 color to a 32-bit ARGB8888 value.
 *
 * Each 5-bit channel is scaled to the full 8-bit range using the
 * formula `(c * 255 + 15) / 31`. Alpha is always set to 0xFF.
 *
 * @param color The 15-bit RGB555 color (bits 14–0 = 0BBBBBGGGGGRRRRR).
 *
 * @return The corresponding 0xAARRGGBB value with full alpha.
 */
static uint32_t cupid_gb_sgb_rgb15_to_argb(uint16_t color)
{
    uint32_t r = (uint32_t)(color & 0x1fu);
    uint32_t g = (uint32_t)((color >> 5u) & 0x1fu);
    uint32_t b = (uint32_t)((color >> 10u) & 0x1fu);

    r = (r * 255u + 15u) / 31u;
    g = (g * 255u + 15u) / 31u;
    b = (b * 255u + 15u) / 31u;

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
static void cupid_gb_sgb_set_default_palettes(CupidGb *gb)
{
    static const uint16_t default_palette[4] = {
        0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u
    };
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
static void cupid_gb_sgb_load_attr_file(CupidGb *gb, unsigned file_index)
{
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
            byte <<= 2u;
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
static void cupid_gb_sgb_pal_command(CupidGb *gb, unsigned first, unsigned second)
{
    unsigned i;
    uint16_t color0 = cupid_gb_sgb_read_le16(&gb->sgb.command[1]);

    gb->sgb.screen_palettes[0] = color0;
    gb->sgb.screen_palettes[4] = color0;
    gb->sgb.screen_palettes[8] = color0;
    gb->sgb.screen_palettes[12] = color0;

    for (i = 0u; i < 3u; ++i) {
        gb->sgb.screen_palettes[first * 4u + i + 1u] = cupid_gb_sgb_read_le16(&gb->sgb.command[3u + i * 2u]);
        gb->sgb.screen_palettes[second * 4u + i + 1u] = cupid_gb_sgb_read_le16(&gb->sgb.command[9u + i * 2u]);
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
static void cupid_gb_sgb_capture_transfer_buffer(const CupidGb *gb, uint8_t *buffer)
{
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
static void cupid_gb_sgb_apply_pal_trn(CupidGb *gb)
{
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
static void cupid_gb_sgb_apply_attr_trn(CupidGb *gb)
{
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
static void cupid_gb_sgb_apply_chr_trn(CupidGb *gb, bool high)
{
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
static void cupid_gb_sgb_apply_pct_trn(CupidGb *gb)
{
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

/**
 * @brief Dispatches a fully-received SGB command packet.
 *
 * Reads the command code from `gb->sgb.command[0]` (bits 7–3) and
 * executes the corresponding operation. Unknown or intentionally ignored
 * commands are logged at INFO level.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_sgb_handle_command(CupidGb *gb)
{
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
    case CUPID_SGB_CMD_ATTR_BLK: {
        struct {
            uint8_t count;
            struct {
                uint8_t control;
                uint8_t palettes;
                uint8_t left;
                uint8_t top;
                uint8_t right;
                uint8_t bottom;
            } data[18];
        } *blk = (void *)(gb->sgb.command + 1);
        unsigned i;

        if (blk->count > 18u) {
            return;
        }

        for (i = 0u; i < blk->count; ++i) {
            bool inside = (blk->data[i].control & 0x01u) != 0u;
            bool middle = (blk->data[i].control & 0x02u) != 0u;
            bool outside = (blk->data[i].control & 0x04u) != 0u;
            uint8_t inside_palette = (uint8_t)(blk->data[i].palettes & 0x03u);
            uint8_t middle_palette = (uint8_t)((blk->data[i].palettes >> 2u) & 0x03u);
            uint8_t outside_palette = (uint8_t)((blk->data[i].palettes >> 4u) & 0x03u);
            unsigned x;
            unsigned y;
            uint8_t left;
            uint8_t top;
            uint8_t right;
            uint8_t bottom;

            if (inside && !middle && !outside) {
                middle = true;
                middle_palette = inside_palette;
            } else if (outside && !middle && !inside) {
                middle = true;
                middle_palette = outside_palette;
            }

            left = (uint8_t)(blk->data[i].left & 0x1fu);
            top = (uint8_t)(blk->data[i].top & 0x1fu);
            right = (uint8_t)(blk->data[i].right & 0x1fu);
            bottom = (uint8_t)(blk->data[i].bottom & 0x1fu);

            for (y = 0u; y < CUPID_SGB_ATTR_HEIGHT; ++y) {
                for (x = 0u; x < CUPID_SGB_ATTR_WIDTH; ++x) {
                    if (x < left || x > right || y < top || y > bottom) {
                        if (outside) {
                            gb->sgb.attribute_map[x + y * CUPID_SGB_ATTR_WIDTH] = outside_palette;
                        }
                    } else if (x > left && x < right && y > top && y < bottom) {
                        if (inside) {
                            gb->sgb.attribute_map[x + y * CUPID_SGB_ATTR_WIDTH] = inside_palette;
                        }
                    } else if (middle) {
                        gb->sgb.attribute_map[x + y * CUPID_SGB_ATTR_WIDTH] = middle_palette;
                    }
                }
            }
        }
        break;
    }
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
    case CUPID_SGB_CMD_ATTR_CHR: {
        struct {
            uint8_t x;
            uint8_t y;
            uint16_t length;
            uint8_t direction;
            uint8_t data[CUPID_SGB_MAX_COMMAND_BYTES - 6u];
        } *chr = (void *)(gb->sgb.command + 1);
        uint16_t count = cupid_gb_sgb_read_le16((const uint8_t *)&chr->length);
        uint8_t x = chr->x;
        uint8_t y = chr->y;
        uint16_t i;

        if (x >= CUPID_SGB_ATTR_WIDTH || y >= CUPID_SGB_ATTR_HEIGHT) {
            break;
        }

        for (i = 0u; i < count; ++i) {
            uint8_t palette = (uint8_t)((chr->data[i / 4u] >> ((((uint16_t)(~i)) & 3u) << 1u)) & 0x03u);
            gb->sgb.attribute_map[x + y * CUPID_SGB_ATTR_WIDTH] = palette;
            if (chr->direction != 0u) {
                y = (uint8_t)(y + 1u);
                if (y == CUPID_SGB_ATTR_HEIGHT) {
                    x = (uint8_t)(x + 1u);
                    y = 0u;
                    if (x == CUPID_SGB_ATTR_WIDTH) {
                        break;
                    }
                }
            } else {
                x = (uint8_t)(x + 1u);
                if (x == CUPID_SGB_ATTR_WIDTH) {
                    y = (uint8_t)(y + 1u);
                    x = 0u;
                    if (y == CUPID_SGB_ATTR_HEIGHT) {
                        break;
                    }
                }
            }
        }
        break;
    }
    case CUPID_SGB_CMD_PAL_SET: {
        unsigned palette;
        for (palette = 0u; palette < 4u; ++palette) {
            unsigned offset = 1u + palette * 2u;
            unsigned palette_index = gb->sgb.command[offset] + ((unsigned)(gb->sgb.command[offset + 1u] & 0x01u) << 8u);
            memcpy(&gb->sgb.screen_palettes[palette * 4u],
                   &gb->sgb.ram_palettes[palette_index * 4u],
                   4u * sizeof(uint16_t));
        }
        gb->sgb.screen_palettes[12] = gb->sgb.screen_palettes[8] =
            gb->sgb.screen_palettes[4] = gb->sgb.screen_palettes[0];
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
    case CUPID_SGB_CMD_MLT_REQ:
        gb->sgb.player_count = (uint8_t)((gb->sgb.command[1] & 0x03u) + 1u);
        if (gb->sgb.player_count == 3u) {
            gb->sgb.player_count = 4u;
        }
        gb->sgb.current_player = (uint8_t)(gb->sgb.current_player & (gb->sgb.player_count - 1u));
        break;
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
        if (command == 0x08u &&
            (gb->sgb.command[1] & (uint8_t)~0x80u) == 0u &&
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
void cupid_gb_sgb_init(CupidGb *gb)
{
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
void cupid_gb_sgb_apply_compatibility_palette(CupidGb *gb)
{
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
bool cupid_gb_sgb_active(const CupidGb *gb)
{
    return gb != NULL && gb->sgb.enabled;
}

/**
 * @brief Processes a write to the P1/JOYP register (0xFF00) for SGB packet reception.
 *
 * Implements the 4-state SGB serial protocol driven by bits 5–4 of JOYP:
 *   - `11` (reset pulse): arms the receiver for a new packet
 *   - `01` (start bit): marks the beginning of a bit stream
 *   - `10` (zero bit): clocks in a 0 or finalizes the packet on stop
 *   - `01` (one bit): clocks in a 1 or resets state on stop
 *
 * When a complete packet (or multi-packet command) is received,
 * @ref cupid_gb_sgb_handle_command is called. Also handles multi-player
 * controller cycling when P15 transitions high.
 *
 * @param gb    Pointer to the Game Boy state.
 * @param value The value being written to JOYP.
 *
 * @note Does nothing if @p gb is NULL or SGB is not active.
 */
void cupid_gb_sgb_write_joyp(CupidGb *gb, uint8_t value)
{
    uint16_t command_size;

    if (gb == NULL || !cupid_gb_sgb_active(gb)) {
        return;
    }

    if ((value & 0x20u) != 0u && (gb->io_registers[0x00u] & 0x20u) == 0u) {
        if ((gb->sgb.player_count & 0x01u) == 0u) {
            gb->sgb.current_player = (uint8_t)((gb->sgb.current_player + 1u) & (gb->sgb.player_count - 1u));
        }
    }

    command_size = (uint16_t)((gb->sgb.command[0] & 0x07u) == 0u ? CUPID_SGB_PACKET_BYTES * 8u :
                              (gb->sgb.command[0] & 0x07u) * CUPID_SGB_PACKET_BYTES * 8u);

    switch ((value >> 4u) & 0x03u) {
    case 3u:
        gb->sgb.ready_for_pulse = true;
        break;
    case 2u: /* zero */
        if (!gb->sgb.ready_for_pulse || !gb->sgb.ready_for_write) {
            return;
        }
        if (gb->sgb.ready_for_stop) {
            if (gb->sgb.command_write_index == command_size) {
                cupid_gb_sgb_handle_command(gb);
                gb->sgb.command_write_index = 0u;
                memset(gb->sgb.command, 0, sizeof(gb->sgb.command));
            }
            gb->sgb.ready_for_pulse = false;
            gb->sgb.ready_for_write = false;
            gb->sgb.ready_for_stop = false;
        } else if (gb->sgb.command_write_index < sizeof(gb->sgb.command) * 8u) {
            gb->sgb.command_write_index = (uint16_t)(gb->sgb.command_write_index + 1u);
            gb->sgb.ready_for_pulse = false;
            if ((gb->sgb.command_write_index & (CUPID_SGB_PACKET_BYTES * 8u - 1u)) == 0u) {
                gb->sgb.ready_for_stop = true;
            }
        }
        break;
    case 1u: /* one */
        if (!gb->sgb.ready_for_pulse || !gb->sgb.ready_for_write) {
            return;
        }
        if (gb->sgb.ready_for_stop) {
            gb->sgb.command_write_index = 0u;
            memset(gb->sgb.command, 0, sizeof(gb->sgb.command));
            gb->sgb.ready_for_pulse = false;
            gb->sgb.ready_for_write = false;
            gb->sgb.ready_for_stop = false;
        } else if (gb->sgb.command_write_index < sizeof(gb->sgb.command) * 8u) {
            gb->sgb.command[gb->sgb.command_write_index / 8u] |= (uint8_t)(1u << (gb->sgb.command_write_index & 7u));
            gb->sgb.command_write_index = (uint16_t)(gb->sgb.command_write_index + 1u);
            gb->sgb.ready_for_pulse = false;
            if ((gb->sgb.command_write_index & (CUPID_SGB_PACKET_BYTES * 8u - 1u)) == 0u) {
                gb->sgb.ready_for_stop = true;
            }
        }
        break;
    case 0u:
        if (!gb->sgb.ready_for_pulse) {
            return;
        }
        gb->sgb.ready_for_write = true;
        gb->sgb.ready_for_pulse = false;
        if ((gb->sgb.command_write_index & (CUPID_SGB_PACKET_BYTES * 8u - 1u)) != 0u ||
            gb->sgb.command_write_index == 0u || gb->sgb.ready_for_stop) {
            gb->sgb.command_write_index = 0u;
            memset(gb->sgb.command, 0, sizeof(gb->sgb.command));
            gb->sgb.ready_for_stop = false;
        }
        break;
    default:
        break;
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
 * (the screen pixels show through). The active mask mode is respected:
 *   - @ref CUPID_SGB_MASK_DISABLED / FREEZE: normal screen rendering
 *   - @ref CUPID_SGB_MASK_BLACK: viewport filled with black
 *   - @ref CUPID_SGB_MASK_COLOR0: viewport filled with palette color 0
 *
 * @param gb          Pointer to the Game Boy state.
 * @param pixels      Output buffer of at least
 *                    `CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT`
 *                    `uint32_t` elements.
 * @param pixel_count Total number of elements in @p pixels; must be≥
 *                    `CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT`.
 *
 * @note Does nothing if @p gb or @p pixels is NULL, or @p pixel_count
 *       is too small.
 */
void cupid_gb_sgb_render_argb(const CupidGb *gb, uint32_t *pixels, size_t pixel_count)
{
    uint32_t border_colors[8u * 16u];
    const uint8_t *screen_source;
    unsigned i;
    unsigned y;
    unsigned x;

    if (gb == NULL || pixels == NULL || pixel_count < CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT) {
        return;
    }

    for (i = 0u; i < 8u * 16u; ++i) {
        border_colors[i] = cupid_gb_sgb_rgb15_to_argb(gb->sgb.border_palettes[i]);
    }

    if (gb->sgb.mask_mode == CUPID_SGB_MASK_FREEZE) {
        screen_source = gb->sgb.freeze_buffer;
    } else {
        screen_source = gb->frame_buffer;
    }

    for (i = 0u; i < CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT; ++i) {
        pixels[i] = 0xff000000u;
    }

    for (y = 0u; y < CUPID_GB_SCREEN_HEIGHT; ++y) {
        for (x = 0u; x < CUPID_GB_SCREEN_WIDTH; ++x) {
            uint32_t color;
            size_t out_index = (CUPID_SGB_VIEWPORT_Y + y) * CUPID_SGB_SCREEN_WIDTH + CUPID_SGB_VIEWPORT_X + x;

            switch (gb->sgb.mask_mode) {
            case CUPID_SGB_MASK_BLACK:
                color = 0xff000000u;
                break;
            case CUPID_SGB_MASK_COLOR0:
                color = cupid_gb_sgb_rgb15_to_argb(gb->sgb.screen_palettes[0]);
                break;
            case CUPID_SGB_MASK_FREEZE:
            case CUPID_SGB_MASK_DISABLED:
            default: {
                uint8_t shade = (uint8_t)(screen_source[y * CUPID_GB_SCREEN_WIDTH + x] & 0x03u);
                uint8_t palette = gb->sgb.attribute_map[(y >> 3u) * CUPID_SGB_ATTR_WIDTH + (x >> 3u)];
                color = cupid_gb_sgb_rgb15_to_argb(gb->sgb.screen_palettes[palette * 4u + shade]);
                break;
            }
            }

            pixels[out_index] = color;
        }
    }

    for (y = 0u; y < CUPID_SGB_TILE_HEIGHT; ++y) {
        for (x = 0u; x < CUPID_SGB_TILE_WIDTH; ++x) {
            uint16_t tile = gb->sgb.border_map[x + y * CUPID_SGB_TILE_WIDTH];
            uint8_t tile_index = (uint8_t)(tile & 0xffu);
            uint8_t palette = (uint8_t)((tile >> 10u) & 0x07u);
            uint8_t flip_x = (tile & 0x4000u) != 0u ? 0u : 7u;
            uint8_t flip_y = (tile & 0x8000u) != 0u ? 7u : 0u;
            unsigned py;

            for (py = 0u; py < 8u; ++py) {
                unsigned px;
                unsigned screen_y = y * 8u + py;
                size_t base = (size_t)tile_index * 32u + (size_t)((py ^ flip_y) * 2u);

                for (px = 0u; px < 8u; ++px) {
                    uint8_t bit = (uint8_t)(1u << (px ^ flip_x));
                    uint8_t color_index = 0u;
                    unsigned screen_x = x * 8u + px;
                    bool in_viewport = screen_x >= CUPID_SGB_VIEWPORT_X &&
                                       screen_x < CUPID_SGB_VIEWPORT_X + CUPID_GB_SCREEN_WIDTH &&
                                       screen_y >= CUPID_SGB_VIEWPORT_Y &&
                                       screen_y < CUPID_SGB_VIEWPORT_Y + CUPID_GB_SCREEN_HEIGHT;

                    color_index |= (gb->sgb.border_tiles[base] & bit) != 0u ? 1u : 0u;
                    color_index |= (gb->sgb.border_tiles[base + 1u] & bit) != 0u ? 2u : 0u;
                    color_index |= (gb->sgb.border_tiles[base + 16u] & bit) != 0u ? 4u : 0u;
                    color_index |= (gb->sgb.border_tiles[base + 17u] & bit) != 0u ? 8u : 0u;

                    if (in_viewport && color_index == 0u) {
                        continue;
                    }

                    pixels[screen_y * CUPID_SGB_SCREEN_WIDTH + screen_x] = border_colors[palette * 16u + color_index];
                }
            }
        }
    }
}

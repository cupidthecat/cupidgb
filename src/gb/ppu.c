/* =========================================================================
 * PPU – Pixel Processing Unit
 *   Shared between Game Boy (DMG) and Game Boy Color (CGB).
 *   Scanline renderer (BG, Window, Sprites), STAT IRQ, DMA transfer.
 * ========================================================================= */

#include "cupid/gb/ppu.h"

#include <string.h>

#include "cupid/gb/gb.h"
#include "cupid/gb/cpu.h"

/* ---------- helpers ---------- */

bool cupid_gb_lcd_enabled(const CupidGb *gb)
{
    return (gb->io_registers[CUPID_GB_IO_LCDC] & 0x80u) != 0u;
}

static uint8_t cupid_gb_ppu_mode(const CupidGb *gb)
{
    return (uint8_t)(gb->io_registers[CUPID_GB_IO_STAT] & 0x03u);
}

void cupid_gb_set_ppu_mode(CupidGb *gb, uint8_t mode)
{
    gb->io_registers[CUPID_GB_IO_STAT] =
        (uint8_t)((gb->io_registers[CUPID_GB_IO_STAT] & 0xfcu) | (mode & 0x03u));
}

static uint8_t cupid_gb_palette_lookup(uint8_t palette, uint8_t color_index)
{
    return (uint8_t)((palette >> (color_index * 2u)) & 0x03u);
}

/* ---------- scanline renderer ---------- */

static void cupid_gb_render_scanline(CupidGb *gb)
{
    uint8_t lcdc;
    uint8_t ly;
    uint8_t bg_palette;
    size_t base_index;
    unsigned int x;

    if (gb == 0) {
        return;
    }

    ly = gb->io_registers[CUPID_GB_IO_LY];
    if (ly >= CUPID_GB_SCREEN_HEIGHT) {
        return;
    }

    lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    base_index = (size_t)ly * CUPID_GB_SCREEN_WIDTH;
    bg_palette = gb->io_registers[CUPID_GB_IO_BGP];

    /* --- Background layer --- */
    if ((lcdc & 0x01u) == 0u) {
        memset(&gb->frame_buffer[base_index], 0u, CUPID_GB_SCREEN_WIDTH);
    } else {
        uint8_t scx = gb->io_registers[CUPID_GB_IO_SCX];
        uint8_t scy = gb->io_registers[CUPID_GB_IO_SCY];

        for (x = 0u; x < CUPID_GB_SCREEN_WIDTH; ++x) {
            uint8_t bg_x = (uint8_t)(x + scx);
            uint8_t bg_y = (uint8_t)(ly + scy);
            size_t tile_map_base = (lcdc & 0x08u) != 0u ? 0x1c00u : 0x1800u;
            size_t tile_map_index = tile_map_base + (size_t)((bg_y >> 3u) * 32u) + (size_t)(bg_x >> 3u);
            uint8_t tile_number = gb->video_ram[tile_map_index & 0x1fffu];
            size_t tile_address;
            uint8_t tile_line;
            uint8_t low;
            uint8_t high;
            uint8_t bit_index;
            uint8_t color_index;

            if ((lcdc & 0x10u) != 0u) {
                tile_address = (size_t)tile_number * 16u;
            } else {
                tile_address = (size_t)(0x1000 + ((int16_t)(int8_t)tile_number * 16));
            }

            tile_line = (uint8_t)((bg_y & 0x07u) * 2u);
            low = gb->video_ram[(tile_address + tile_line) & 0x1fffu];
            high = gb->video_ram[(tile_address + tile_line + 1u) & 0x1fffu];
            bit_index = (uint8_t)(7u - (bg_x & 0x07u));
            color_index = (uint8_t)((((high >> bit_index) & 0x01u) << 1u) |
                                    ((low >> bit_index) & 0x01u));
            gb->frame_buffer[base_index + x] = cupid_gb_palette_lookup(bg_palette, color_index);
        }
    }

    /* --- Window layer (LCDC bit 5 enables window, bit 0 must also be set) --- */
    if ((lcdc & 0x21u) == 0x21u) {
        uint8_t wy = gb->io_registers[CUPID_GB_IO_WY];
        uint8_t wx = gb->io_registers[CUPID_GB_IO_WX];
        unsigned int wx_screen = (wx >= 7u) ? (unsigned int)(wx - 7u) : 0u;

        if ((int)ly >= (int)wy && wx_screen < CUPID_GB_SCREEN_WIDTH) {
            size_t win_tile_map_base = (lcdc & 0x40u) != 0u ? 0x1c00u : 0x1800u;
            uint8_t win_y = gb->window_line_counter;

            for (x = wx_screen; x < CUPID_GB_SCREEN_WIDTH; ++x) {
                uint8_t win_x = (uint8_t)(x - wx_screen);
                size_t tile_map_idx = win_tile_map_base + (size_t)((win_y >> 3u) * 32u) + (size_t)(win_x >> 3u);
                uint8_t tile_num = gb->video_ram[tile_map_idx & 0x1fffu];
                size_t tile_address;
                uint8_t tile_line;
                uint8_t wlow;
                uint8_t whigh;
                uint8_t wbit;
                uint8_t wcolor;

                if ((lcdc & 0x10u) != 0u) {
                    tile_address = (size_t)tile_num * 16u;
                } else {
                    tile_address = (size_t)(0x1000 + ((int16_t)(int8_t)tile_num * 16));
                }

                tile_line = (uint8_t)((win_y & 0x07u) * 2u);
                wlow = gb->video_ram[(tile_address + tile_line) & 0x1fffu];
                whigh = gb->video_ram[(tile_address + tile_line + 1u) & 0x1fffu];
                wbit = (uint8_t)(7u - (win_x & 0x07u));
                wcolor = (uint8_t)((((whigh >> wbit) & 0x01u) << 1u) |
                                   ((wlow >> wbit) & 0x01u));
                gb->frame_buffer[base_index + x] = cupid_gb_palette_lookup(bg_palette, wcolor);
            }
            gb->window_line_counter = (uint8_t)(gb->window_line_counter + 1u);
        }
    }

    /* --- OBJ/Sprite layer (LCDC bit 1 enables sprites) --- */
    if ((lcdc & 0x02u) != 0u) {
        int obj_height = ((lcdc & 0x04u) != 0u) ? 16 : 8;
        unsigned int sprite_count = 0u;
        unsigned int i;

        for (i = 0u; i < 40u && sprite_count < 10u; ++i) {
            int obj_y = (int)gb->object_attribute_memory[i * 4u] - 16;
            int obj_x = (int)gb->object_attribute_memory[i * 4u + 1u] - 8;
            uint8_t tile_idx = gb->object_attribute_memory[i * 4u + 2u];
            uint8_t attrs = gb->object_attribute_memory[i * 4u + 3u];
            int tile_row;
            uint8_t palette;
            size_t tile_addr;
            uint8_t olow;
            uint8_t ohigh;
            int px;

            if ((int)ly < obj_y || (int)ly >= obj_y + obj_height) {
                continue;
            }
            ++sprite_count;

            tile_row = (int)ly - obj_y;

            if (obj_height == 16) {
                if ((attrs & 0x40u) != 0u) { /* Y-flip */
                    tile_row = obj_height - 1 - tile_row;
                }
                if (tile_row >= 8) {
                    tile_idx = (uint8_t)(tile_idx | 0x01u);
                    tile_row -= 8;
                } else {
                    tile_idx = (uint8_t)(tile_idx & 0xfeu);
                }
            } else {
                if ((attrs & 0x40u) != 0u) { /* Y-flip */
                    tile_row = 7 - tile_row;
                }
            }

            palette = ((attrs & 0x10u) != 0u)
                          ? gb->io_registers[CUPID_GB_IO_OBP1]
                          : gb->io_registers[CUPID_GB_IO_OBP0];
            tile_addr = (size_t)tile_idx * 16u + (size_t)(tile_row * 2);
            olow = gb->video_ram[tile_addr & 0x1fffu];
            ohigh = gb->video_ram[(tile_addr + 1u) & 0x1fffu];

            for (px = 0; px < 8; ++px) {
                int screen_x;
                uint8_t color_index;
                uint8_t bit_pos;

                if ((attrs & 0x20u) != 0u) { /* X-flip */
                    screen_x = obj_x + (7 - px);
                    bit_pos = (uint8_t)px;
                } else {
                    screen_x = obj_x + px;
                    bit_pos = (uint8_t)px;
                }

                if (screen_x < 0 || screen_x >= (int)CUPID_GB_SCREEN_WIDTH) {
                    continue;
                }

                color_index = (uint8_t)((((ohigh >> (7u - bit_pos)) & 0x01u) << 1u) |
                                        ((olow >> (7u - bit_pos)) & 0x01u));

                if (color_index == 0u) {
                    continue; /* color 0 is transparent for sprites */
                }

                /* BG priority: attr bit 7 set means sprite is behind BG colors 1-3 */
                if ((attrs & 0x80u) != 0u &&
                    gb->frame_buffer[base_index + (size_t)screen_x] != 0u) {
                    continue;
                }

                gb->frame_buffer[base_index + (size_t)screen_x] =
                    cupid_gb_palette_lookup(palette, color_index);
            }
        }
    }
}

/* ---------- STAT IRQ ---------- */

void cupid_gb_update_stat_irq(CupidGb *gb)
{
    bool coincidence;
    bool irq_signal;
    uint8_t stat;
    uint8_t mode;

    if (gb == 0) {
        return;
    }

    stat = gb->io_registers[CUPID_GB_IO_STAT];
    coincidence = gb->io_registers[CUPID_GB_IO_LY] == gb->io_registers[CUPID_GB_IO_LYC];
    if (coincidence) {
        stat = (uint8_t)(stat | 0x04u);
    } else {
        stat = (uint8_t)(stat & (uint8_t)~0x04u);
    }

    gb->io_registers[CUPID_GB_IO_STAT] = stat;
    mode = (uint8_t)(stat & 0x03u);
    irq_signal = (coincidence && (stat & 0x40u) != 0u) ||
                 (mode == CUPID_GB_PPU_MODE_HBLANK && (stat & 0x08u) != 0u) ||
                 (mode == CUPID_GB_PPU_MODE_VBLANK && (stat & 0x10u) != 0u) ||
                 (mode == CUPID_GB_PPU_MODE_OAM && (stat & 0x20u) != 0u);

    if (irq_signal && !gb->stat_irq_line) {
        cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_LCD_STAT);
    }

    gb->stat_irq_line = irq_signal;
}

/* ---------- PPU reset / DMA ---------- */

void cupid_gb_reset_ppu(CupidGb *gb)
{
    if (gb == 0) {
        return;
    }

    gb->ppu_counter = 0u;
    gb->frame_ready = false;
    gb->stat_irq_line = false;
    gb->window_line_counter = 0u;
    gb->io_registers[CUPID_GB_IO_LY] = 0u;
    /* On real DMG the PPU stays in mode 0 for ~76 T-cycles (19 M-cycles)
       after LCD enable before the first OAM scan begins. */
    gb->ppu_startup_delay = CUPID_GB_PPU_LCD_ON_DELAY;
    cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
    cupid_gb_update_stat_irq(gb);
}

void cupid_gb_run_dma_transfer(CupidGb *gb, uint8_t source_high)
{
    uint16_t source_base;
    size_t index;

    if (gb == 0) {
        return;
    }

    source_base = (uint16_t)((uint16_t)source_high << 8u);
    for (index = 0u; index < sizeof(gb->object_attribute_memory); ++index) {
        gb->object_attribute_memory[index] = cupid_gb_read_u8(gb, (uint16_t)(source_base + index));
    }
}

/* ---------- PPU tick ---------- */

void cupid_gb_tick_ppu(CupidGb *gb, uint16_t cycles)
{
    if (gb == 0) {
        return;
    }

    if (!cupid_gb_lcd_enabled(gb)) {
        gb->ppu_counter = 0u;
        gb->frame_ready = false;
        gb->window_line_counter = 0u;
        gb->io_registers[CUPID_GB_IO_LY] = 0u;
        cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
        cupid_gb_update_stat_irq(gb);
        return;
    }

    while (cycles > 0u) {
        uint8_t ly;
        uint16_t line_cycle;
        uint8_t next_mode;

        /* LCD-on warmup: stay in mode 0, don't advance the dot counter */
        if (gb->ppu_startup_delay > 0u) {
            gb->ppu_startup_delay = (uint8_t)(gb->ppu_startup_delay - 1u);
            --cycles;
            continue;
        }

        ly = gb->io_registers[CUPID_GB_IO_LY];
        line_cycle = gb->ppu_counter;

        if (ly >= CUPID_GB_PPU_VISIBLE_SCANLINES) {
            next_mode = CUPID_GB_PPU_MODE_VBLANK;
        } else if (line_cycle < CUPID_GB_PPU_OAM_CYCLES) {
            next_mode = CUPID_GB_PPU_MODE_OAM;
        } else if (line_cycle < CUPID_GB_PPU_OAM_CYCLES + CUPID_GB_PPU_TRANSFER_CYCLES) {
            next_mode = CUPID_GB_PPU_MODE_TRANSFER;
        } else {
            next_mode = CUPID_GB_PPU_MODE_HBLANK;
        }

        if (cupid_gb_ppu_mode(gb) != next_mode) {
            cupid_gb_set_ppu_mode(gb, next_mode);
            if (next_mode == CUPID_GB_PPU_MODE_HBLANK && ly < CUPID_GB_SCREEN_HEIGHT) {
                cupid_gb_render_scanline(gb);
            }
            cupid_gb_update_stat_irq(gb);
        }

        gb->ppu_counter = (uint16_t)(gb->ppu_counter + 1u);
        --cycles;

        if (gb->ppu_counter >= CUPID_GB_PPU_SCANLINE_CYCLES) {
            gb->ppu_counter = (uint16_t)(gb->ppu_counter - CUPID_GB_PPU_SCANLINE_CYCLES);
            ly = (uint8_t)(gb->io_registers[CUPID_GB_IO_LY] + 1u);

            if (ly >= CUPID_GB_PPU_TOTAL_SCANLINES) {
                gb->window_line_counter = 0u;
            }

            if (ly == CUPID_GB_PPU_VISIBLE_SCANLINES) {
                gb->frame_ready = true;
                cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_VBLANK);
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_VBLANK);
            } else if (ly >= CUPID_GB_PPU_TOTAL_SCANLINES) {
                ly = 0u;
                gb->frame_ready = false;
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_OAM);
            } else if (ly < CUPID_GB_PPU_VISIBLE_SCANLINES) {
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_OAM);
            }

            gb->io_registers[CUPID_GB_IO_LY] = ly;
            cupid_gb_update_stat_irq(gb);
        }
    }
}

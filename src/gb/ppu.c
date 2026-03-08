/* =========================================================================
 * PPU – Pixel Processing Unit
 *   Shared between Game Boy (DMG) and Game Boy Color (CGB).
 *   Scanline renderer (BG, Window, Sprites), STAT IRQ, DMA transfer.
 * ========================================================================= */

#include "cupid/gb/ppu.h"

#include <string.h>

#include "cupid/gb/gb.h"
#include "cupid/gb/cpu.h"
#include "cupid/gbc/cgb.h"

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

static int cupid_gb_floor_div8(int value)
{
    if (value >= 0) {
        return value / 8;
    }

    return -(((-value) + 7) / 8);
}

static uint16_t cupid_gb_sprite_penalty_cycles(const CupidGb *gb)
{
    struct SpriteCandidate {
        uint8_t x;
    } sprites[10];
    int considered_buckets[10];
    int bucket_waits[10];
    uint8_t count;
    uint8_t i;
    uint8_t ly;
    uint8_t lcdc;
    uint8_t scx;
    int obj_height;
    uint8_t active_buckets;
    uint8_t contributing_sprites;
    uint16_t penalty_dots;

    if (gb == 0) {
        return 0u;
    }

    lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    if ((lcdc & 0x02u) == 0u) {
        return 0u;
    }

    ly = gb->io_registers[CUPID_GB_IO_LY];
    if (ly >= CUPID_GB_PPU_VISIBLE_SCANLINES) {
        return 0u;
    }

    scx = gb->io_registers[CUPID_GB_IO_SCX];
    obj_height = ((lcdc & 0x04u) != 0u) ? 16 : 8;
    count = 0u;

    for (i = 0u; i < 40u && count < 10u; ++i) {
        int obj_y = (int)gb->object_attribute_memory[i * 4u] - 16;

        if ((int)ly < obj_y || (int)ly >= obj_y + obj_height) {
            continue;
        }

        sprites[count].x = gb->object_attribute_memory[i * 4u + 1u];
        ++count;
    }

    penalty_dots = 0u;
    active_buckets = 0u;
    contributing_sprites = 0u;
    for (i = 0u; i < count; ++i) {
        int x;
        int bucket_id;
        int wait_dots;
        bool bucket_seen;
        uint8_t j;

        x = (int)sprites[i].x;
        ++contributing_sprites;
        if (x >= (int)(CUPID_GB_SCREEN_WIDTH + 8u)) {
            continue;
        }

        penalty_dots = (uint16_t)(penalty_dots + 6u);

        bucket_id = cupid_gb_floor_div8(x + (int)scx);
        wait_dots = 5 - ((x + (int)scx) & 7);
        if (wait_dots < 0) {
            wait_dots = 0;
        }

        bucket_seen = false;
        for (j = 0u; j < active_buckets; ++j) {
            if (considered_buckets[j] == bucket_id) {
                if (bucket_waits[j] < wait_dots) {
                    bucket_waits[j] = wait_dots;
                }
                bucket_seen = true;
                break;
            }
        }

        if (!bucket_seen && active_buckets < 10u) {
            considered_buckets[active_buckets] = bucket_id;
            bucket_waits[active_buckets] = wait_dots;
            ++active_buckets;
        }
    }

    for (i = 0u; i < active_buckets; ++i) {
        penalty_dots = (uint16_t)(penalty_dots + (uint16_t)bucket_waits[i]);
    }

    if (contributing_sprites > 0u) {
        return (uint16_t)(penalty_dots / 4u);
    }

    return 0u;
}

static uint16_t cupid_gb_visible_transfer_cycles(const CupidGb *gb)
{
    uint8_t scx_mod;
    uint16_t cycles;

    if (gb == 0) {
        return CUPID_GB_PPU_TRANSFER_CYCLES;
    }

    scx_mod = (uint8_t)(gb->io_registers[CUPID_GB_IO_SCX] & 0x07u);
    cycles = CUPID_GB_PPU_TRANSFER_CYCLES;
    if (scx_mod == 0u) {
        cycles = CUPID_GB_PPU_TRANSFER_CYCLES;
    } else if (scx_mod <= 4u) {
        cycles = (uint16_t)(CUPID_GB_PPU_TRANSFER_CYCLES + 1u);
    } else {
        cycles = (uint16_t)(CUPID_GB_PPU_TRANSFER_CYCLES + 2u);
    }

    return (uint16_t)(cycles + cupid_gb_sprite_penalty_cycles(gb));
}

static uint16_t cupid_gb_scanline_cycles(const CupidGb *gb, uint8_t ly)
{
    if (gb != 0 && gb->ppu_lcd_startup && ly == 0u) {
        return 111u;
    }

    if (gb != 0 && !gb->cgb_mode && ly == (uint8_t)(CUPID_GB_PPU_TOTAL_SCANLINES - 1u)) {
        return (uint16_t)(CUPID_GB_PPU_SCANLINE_CYCLES - 1u);
    }

    return CUPID_GB_PPU_SCANLINE_CYCLES;
}

/* ---------- scanline renderer ---------- */

static void cupid_gb_render_scanline(CupidGb *gb)
{
    uint8_t lcdc;
    uint8_t ly;
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
    if (gb->cgb_mode) {
        cupid_cgb_render_scanline(gb, lcdc, ly, base_index);
        return;
    }

    {
        uint8_t bg_palette = gb->io_registers[CUPID_GB_IO_BGP];
        bool cgb_compat = cupid_cgb_compat_active(gb);

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
                if (cgb_compat) {
                    gb->frame_buffer_color[base_index + x] =
                        cupid_cgb_compat_bg_color(gb, gb->frame_buffer[base_index + x]);
                }
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
                    if (cgb_compat) {
                        gb->frame_buffer_color[base_index + x] =
                            cupid_cgb_compat_bg_color(gb, gb->frame_buffer[base_index + x]);
                    }
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
                if (cgb_compat) {
                    gb->frame_buffer_color[base_index + (size_t)screen_x] =
                        cupid_cgb_compat_obj_color(gb,
                                                   (attrs & 0x10u) != 0u ? 1u : 0u,
                                                   gb->frame_buffer[base_index + (size_t)screen_x]);
                }
            }
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

    if (!cupid_gb_lcd_enabled(gb)) {
        coincidence = (stat & 0x04u) != 0u;
        stat = (uint8_t)((stat & (uint8_t)~0x03u) | CUPID_GB_PPU_MODE_HBLANK);
        gb->io_registers[CUPID_GB_IO_STAT] = stat;
        irq_signal = coincidence && (stat & 0x40u) != 0u;

        if (irq_signal && !gb->stat_irq_line) {
            gb->stat_irq_delay = 0u;
            cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_LCD_STAT);
        } else if (!irq_signal) {
            gb->stat_irq_delay = 0u;
        }

        gb->stat_irq_line = irq_signal;
        return;
    }

    coincidence = !gb->ppu_line_boundary_hold &&
                  gb->io_registers[CUPID_GB_IO_LY] == gb->io_registers[CUPID_GB_IO_LYC];
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
        if (mode == CUPID_GB_PPU_MODE_HBLANK || mode == CUPID_GB_PPU_MODE_OAM) {
            gb->stat_irq_delay = 0u;
            cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_LCD_STAT);
        } else {
            gb->stat_irq_delay = 1u;
        }
    } else if (!irq_signal) {
        gb->stat_irq_delay = 0u;
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
    gb->ppu_lcd_warmup_lines = 2u;
    gb->ppu_lcd_startup = true;
    gb->ppu_line_boundary_hold = false;
    gb->window_line_counter = 0u;
    gb->io_registers[CUPID_GB_IO_LY] = 0u;
    cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
    cupid_gb_update_stat_irq(gb);
}

void cupid_gb_run_dma_transfer(CupidGb *gb, uint8_t source_high)
{
    uint16_t source_base;

    if (gb == 0) {
        return;
    }

    if (source_high >= 0xe0u) {
        source_high = (uint8_t)(source_high - 0x20u);
    }

    source_base = (uint16_t)((uint16_t)source_high << 8u);

    if (gb->dma_active) {
        gb->dma_restart_source_base = source_base;
        gb->dma_restart_delay = 2u;
        gb->dma_restart_pending = true;
        return;
    }

    gb->dma_source_base = source_base;
    gb->dma_index = 0u;
    gb->dma_start_delay = 2u;
    gb->dma_restart_delay = 0u;
    gb->dma_restart_pending = false;
    gb->dma_active = false;
}

void cupid_gb_tick_dma(CupidGb *gb)
{
    if (gb == 0) {
        return;
    }

    if (gb->dma_restart_pending && gb->dma_restart_delay > 0u) {
        gb->dma_restart_delay = (uint8_t)(gb->dma_restart_delay - 1u);
        if (gb->dma_restart_delay == 0u) {
            gb->dma_source_base = gb->dma_restart_source_base;
            gb->dma_index = 0u;
            gb->dma_restart_pending = false;
            return;
        }
    } else if (gb->dma_start_delay > 0u) {
        gb->dma_start_delay = (uint8_t)(gb->dma_start_delay - 1u);
        if (gb->dma_start_delay == 0u) {
            gb->dma_active = true;
            return;
        } else {
            return;
        }
    }

    if (!gb->dma_active) {
        return;
    }

    if (gb->dma_index < sizeof(gb->object_attribute_memory)) {
        gb->object_attribute_memory[gb->dma_index] =
            cupid_gb_read_u8(gb, (uint16_t)(gb->dma_source_base + gb->dma_index));
        gb->dma_index = (uint8_t)(gb->dma_index + 1u);
    }

    if (gb->dma_index >= sizeof(gb->object_attribute_memory)) {
        gb->dma_active = false;
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
        gb->ppu_lcd_warmup_lines = 0u;
        gb->ppu_lcd_startup = false;
        gb->ppu_line_boundary_hold = false;
        gb->window_line_counter = 0u;
        gb->io_registers[CUPID_GB_IO_LY] = 0u;
        cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
        cupid_gb_update_stat_irq(gb);
        return;
    }

    while (cycles > 0u) {
        uint8_t ly;
        uint16_t line_cycle;
        uint16_t scanline_cycles;
        uint16_t transfer_cycles;
        uint8_t next_mode;

        if (gb->ppu_line_boundary_hold) {
            gb->ppu_line_boundary_hold = false;
            cupid_gb_update_stat_irq(gb);
        }

        ly = gb->io_registers[CUPID_GB_IO_LY];
        line_cycle = gb->ppu_counter;
        scanline_cycles = cupid_gb_scanline_cycles(gb, ly);
        transfer_cycles = cupid_gb_visible_transfer_cycles(gb);

        if (gb->stat_irq_delay > 0u) {
            gb->stat_irq_delay = (uint8_t)(gb->stat_irq_delay - 1u);
            if (gb->stat_irq_delay == 0u && gb->stat_irq_line) {
                cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_LCD_STAT);
            }
        }

        if (gb->ppu_lcd_startup && ly == 0u) {
            if (line_cycle < 17u) {
                next_mode = CUPID_GB_PPU_MODE_HBLANK;
            } else if (line_cycle < (uint16_t)(17u + transfer_cycles)) {
                next_mode = CUPID_GB_PPU_MODE_TRANSFER;
            } else {
                next_mode = CUPID_GB_PPU_MODE_HBLANK;
            }
        } else if (ly >= CUPID_GB_PPU_VISIBLE_SCANLINES) {
            next_mode = CUPID_GB_PPU_MODE_VBLANK;
        } else if (line_cycle < CUPID_GB_PPU_OAM_CYCLES) {
            next_mode = CUPID_GB_PPU_MODE_OAM;
        } else if (line_cycle < CUPID_GB_PPU_OAM_CYCLES + transfer_cycles) {
            next_mode = CUPID_GB_PPU_MODE_TRANSFER;
        } else {
            next_mode = CUPID_GB_PPU_MODE_HBLANK;
        }

        if (cupid_gb_ppu_mode(gb) != next_mode) {
            cupid_gb_set_ppu_mode(gb, next_mode);
            if (next_mode == CUPID_GB_PPU_MODE_HBLANK && ly < CUPID_GB_SCREEN_HEIGHT) {
                cupid_gb_render_scanline(gb);
                cupid_cgb_tick_hdma(gb);
            }
            cupid_gb_update_stat_irq(gb);
        }

        gb->ppu_counter = (uint16_t)(gb->ppu_counter + 1u);
        --cycles;

        if (gb->ppu_counter >= scanline_cycles) {
            gb->ppu_counter = (uint16_t)(gb->ppu_counter - scanline_cycles);
            ly = (uint8_t)(gb->io_registers[CUPID_GB_IO_LY] + 1u);

            if (ly >= CUPID_GB_PPU_TOTAL_SCANLINES) {
                gb->window_line_counter = 0u;
            }

            if (ly == CUPID_GB_PPU_VISIBLE_SCANLINES) {
                gb->frame_ready = true;
                gb->ppu_line_boundary_hold = false;
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_VBLANK);
                cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_VBLANK);
                if (!gb->cgb_mode && (gb->io_registers[CUPID_GB_IO_STAT] & 0x20u) != 0u) {
                    cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_LCD_STAT);
                }
            } else if (ly >= CUPID_GB_PPU_TOTAL_SCANLINES) {
                ly = 0u;
                gb->frame_ready = false;
                gb->ppu_line_boundary_hold = false;
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_OAM);
            } else if (gb->ppu_lcd_warmup_lines > 0u) {
                gb->ppu_line_boundary_hold = true;
                gb->ppu_lcd_warmup_lines = (uint8_t)(gb->ppu_lcd_warmup_lines - 1u);
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
            }

            gb->io_registers[CUPID_GB_IO_LY] = ly;
            if (ly != 0u) {
                gb->ppu_lcd_startup = false;
            }
            cupid_gb_update_stat_irq(gb);
        }
    }
}

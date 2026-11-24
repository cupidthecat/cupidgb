/**
 * @file    ppu.h
 * @brief   LCD timing, presentation events, and pixel-pipeline interfaces.
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

#ifndef CUPID_GB_PPU_H
#define CUPID_GB_PPU_H

#include <stdbool.h>
#include <stdint.h>

#include "cupid/gb/gb.h"

enum {
    CUPID_GB_IO_LCDC = 0x40,
    CUPID_GB_IO_STAT = 0x41,
    CUPID_GB_IO_SCY = 0x42,
    CUPID_GB_IO_SCX = 0x43,
    CUPID_GB_IO_LY = 0x44,
    CUPID_GB_IO_LYC = 0x45,
    CUPID_GB_IO_DMA = 0x46,
    CUPID_GB_IO_BGP = 0x47,
    CUPID_GB_IO_OBP0 = 0x48,
    CUPID_GB_IO_OBP1 = 0x49,
    CUPID_GB_IO_WY = 0x4a,
    CUPID_GB_IO_WX = 0x4b,
    CUPID_GB_PPU_MODE_HBLANK = 0x00,
    CUPID_GB_PPU_MODE_VBLANK = 0x01,
    CUPID_GB_PPU_MODE_OAM = 0x02,
    CUPID_GB_PPU_MODE_TRANSFER = 0x03,
    CUPID_GB_PPU_OAM_CYCLES = 20,
    CUPID_GB_PPU_TRANSFER_CYCLES = 43,
    CUPID_GB_PPU_SCANLINE_CYCLES = 114,
    CUPID_GB_PPU_VISIBLE_SCANLINES = 144,
    CUPID_GB_PPU_TOTAL_SCANLINES = 154
};

bool cupid_gb_lcd_enabled(const CupidGb *gb);
bool cupid_gb_ppu_vram_read_blocked(const CupidGb *gb);
bool cupid_gb_ppu_vram_write_blocked(const CupidGb *gb);
/* STOP samples CPU port ownership once, then freezes the complementary PPU gates. */
void cupid_gb_ppu_enter_stop(CupidGb *gb);
void cupid_gb_ppu_leave_stop(CupidGb *gb);

enum {
    CUPID_GB_FRAME_NORMAL = 0,
    CUPID_GB_FRAME_LCD_OFF = 1,
    CUPID_GB_FRAME_ARTIFICIAL = 2,
    CUPID_GB_FRAME_BLANK_FIRST = 3,
    CUPID_GB_FRAME_REPEAT = 4
};

/* Called before a disabled LCD is enabled; any pending presentation stays latched. */
void cupid_gb_prepare_lcd_enable(CupidGb *gb);
void cupid_gb_set_ppu_mode(CupidGb *gb, uint8_t mode);
void cupid_gb_update_stat_irq(CupidGb *gb);
void cupid_gb_update_stat_write(CupidGb *gb, bool half_dot, bool stat_write);
void cupid_gb_ppu_write_lyc(CupidGb *gb, uint8_t value, bool half_dot);
void cupid_gb_sync_stat_half(CupidGb *gb);
bool cupid_gb_stat_boundary(const CupidGb *gb);
void cupid_gb_reset_ppu(CupidGb *gb);
/* Refresh CGB LCD-startup STAT latches at an advancing half-dot boundary without
 * advancing the pixel pipeline. Repeated calls at the same edge are harmless. */
void cupid_gb_sync_lcd_startup(CupidGb *gb);
void cupid_gb_run_dma_transfer(CupidGb *gb, uint8_t source_high);
void cupid_gb_tick_dma(CupidGb *gb);
/* Advance after the elapsed CPU segment and its LCD work, including partial M-cycles. */
void cupid_gb_tick_dma_tcycles(CupidGb *gb, unsigned cycles);
bool cupid_gb_oam_dma_blocks_bus(const CupidGb *gb);
void cupid_gb_tick_ppu(CupidGb *gb, uint16_t cycles);
/* Advances LCD dots without advancing the CPU, timer, DMA, or audio clocks. */
void cupid_gb_tick_ppu_dots(CupidGb *gb, uint16_t dots);
/* Changes subsequently emitted pixels while leaving LCD fetches and timing active.
 * Both layers start visible. The host choice survives resets and state loads. */
void cupid_gb_set_layer_visibility(CupidGb *gb, bool background, bool objects);
void cupid_gb_pixels_begin_line(CupidGb *gb);
void cupid_gb_pixels_scan_object(CupidGb *gb, unsigned index);
void cupid_gb_pixels_begin_transfer(CupidGb *gb);
bool cupid_gb_pixels_tick(CupidGb *gb);
void cupid_gb_pixels_finish_line(CupidGb *gb);
bool cupid_gb_pixels_abort_object(CupidGb *gb);
void cupid_gb_pixels_window_write(CupidGb *gb);

enum {
    CUPID_GB_PIXEL_FRACTIONAL = 1,
    CUPID_GB_PIXEL_WINDOW_FIRST = 2,
    CUPID_GB_PIXEL_WX_GLITCH = 4,
    CUPID_GB_PIXEL_WINDOW_HOLD = 8,
    CUPID_GB_PIXEL_BG_INSERT = 16,
    CUPID_GB_PIXEL_TIMING_FLAGS = 31,
    CUPID_GB_PIXEL_TIMING_STATE_SIZE = 4
};

/* This fixed record preserves pipeline history omitted by older native files.
 * Legacy import projects only the loaded prefix and never samples live input. */
bool cupid_gb_pixels_timing_valid(const CupidGb *gb);
bool cupid_gb_pixels_timing_encode(const CupidGb *gb, uint8_t *data, size_t size);
bool cupid_gb_pixels_timing_decode(CupidGb *gb, const uint8_t *data, size_t size);
void cupid_gb_pixels_import_legacy(CupidGb *gb);

#endif

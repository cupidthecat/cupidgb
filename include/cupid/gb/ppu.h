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
/* Advances one monochrome HALT M-cycle and samples enabled requests after two dots. */
uint8_t cupid_gb_tick_ppu_halt(CupidGb *gb);
void cupid_gb_pixels_begin_line(CupidGb *gb);
void cupid_gb_pixels_scan_object(CupidGb *gb, unsigned index);
void cupid_gb_pixels_begin_transfer(CupidGb *gb);
bool cupid_gb_pixels_tick(CupidGb *gb);

#endif

#ifndef CUPID_GB_PPU_H
#define CUPID_GB_PPU_H

#include <stdbool.h>
#include <stdint.h>

#include "cupid/gb/gb.h"

enum {
    CUPID_GB_IO_LCDC = 0x40,
    CUPID_GB_IO_STAT = 0x41,
    CUPID_GB_IO_SCY  = 0x42,
    CUPID_GB_IO_SCX  = 0x43,
    CUPID_GB_IO_LY   = 0x44,
    CUPID_GB_IO_LYC  = 0x45,
    CUPID_GB_IO_DMA  = 0x46,
    CUPID_GB_IO_BGP  = 0x47,
    CUPID_GB_IO_OBP0 = 0x48,
    CUPID_GB_IO_OBP1 = 0x49,
    CUPID_GB_IO_WY   = 0x4a,
    CUPID_GB_IO_WX   = 0x4b,
    CUPID_GB_PPU_MODE_HBLANK   = 0x00,
    CUPID_GB_PPU_MODE_VBLANK   = 0x01,
    CUPID_GB_PPU_MODE_OAM      = 0x02,
    CUPID_GB_PPU_MODE_TRANSFER = 0x03,
    CUPID_GB_PPU_OAM_CYCLES        = 20,
    CUPID_GB_PPU_TRANSFER_CYCLES   = 43,
    CUPID_GB_PPU_SCANLINE_CYCLES   = 114,
    CUPID_GB_PPU_VISIBLE_SCANLINES = 144,
    CUPID_GB_PPU_TOTAL_SCANLINES   = 154
};

bool    cupid_gb_lcd_enabled(const CupidGb *gb);
void    cupid_gb_set_ppu_mode(CupidGb *gb, uint8_t mode);
void    cupid_gb_update_stat_irq(CupidGb *gb);
void    cupid_gb_reset_ppu(CupidGb *gb);
void    cupid_gb_run_dma_transfer(CupidGb *gb, uint8_t source_high);
void    cupid_gb_tick_ppu(CupidGb *gb, uint16_t cycles);

#endif

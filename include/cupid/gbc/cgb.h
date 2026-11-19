/**
 * @file    cgb.h
 * @brief   Color hardware initialization, registers, palettes, rendering, and DMA interfaces.
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

#ifndef CUPID_GBC_CGB_H
#define CUPID_GBC_CGB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct CupidGb;

void cupid_cgb_init_state(struct CupidGb *gb);
size_t cupid_cgb_vram_offset(const struct CupidGb *gb, uint16_t address);
size_t cupid_cgb_wram_offset(const struct CupidGb *gb, uint16_t address);
bool cupid_cgb_get_compatibility_palette(const struct CupidGb *gb, uint16_t bg[4], uint16_t obj0[4],
                                         uint16_t obj1[4]);
void cupid_cgb_apply_compatibility_palette(struct CupidGb *gb);
bool cupid_cgb_compat_active(const struct CupidGb *gb);
uint16_t cupid_cgb_compat_bg_color(const struct CupidGb *gb, uint8_t shade);
uint16_t cupid_cgb_compat_obj_color(const struct CupidGb *gb, unsigned int palette, uint8_t shade);
/* CPU palette access differs from the gate sampled by a stopped PPU. */
bool cupid_cgb_palettes_cpu_blocked(const struct CupidGb *gb);
bool cupid_cgb_handle_read_register(const struct CupidGb *gb, uint16_t address, uint8_t *value);
bool cupid_cgb_handle_write_register(struct CupidGb *gb, uint16_t address, uint8_t value);
void cupid_cgb_render_scanline(struct CupidGb *gb, uint8_t lcdc, uint8_t ly, size_t base_index);
void cupid_cgb_tick_hdma(struct CupidGb *gb);
void cupid_cgb_advance_hdma(struct CupidGb *gb);
void cupid_cgb_hdma_lcd_off(struct CupidGb *gb, uint8_t previous_mode);
void cupid_cgb_hdma_sleep(struct CupidGb *gb);
void cupid_cgb_hdma_wake(struct CupidGb *gb);
/* The opcode has already been fetched. DMA may replace its backing memory,
 * but execution must retain that latched byte while all other clocks run. */
void cupid_cgb_run_pending_dma(struct CupidGb *gb);

#endif

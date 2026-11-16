/**
 * @file    cgb_internal.h
 * @brief   Internal color palette-port and DMA-status interfaces.
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

#ifndef CUPID_GBC_INTERNAL_H
#define CUPID_GBC_INTERNAL_H

#include "cupid/gbc/cgb.h"

uint8_t cupid_cgb_palette_read(const struct CupidGb *gb, bool object_palette);
void cupid_cgb_palette_write(struct CupidGb *gb, bool object_palette, uint8_t value);
uint8_t cupid_cgb_dma_status(const struct CupidGb *gb);

#endif

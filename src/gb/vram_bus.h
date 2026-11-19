/**
 * @file    vram_bus.h
 * @brief   CPU and DMA VRAM read ownership, retained fetch addresses, and native-state codec.
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

#ifndef CUPID_GB_VRAM_BUS_INTERNAL_H
#define CUPID_GB_VRAM_BUS_INTERNAL_H

#include "cupid/gb/gb.h"

#define CUPID_GB_VRAM_BUS_STATE_SIZE 10u

/* A null bus observes the current read result without driving the retained byte.
 * DMA bypasses CPU port ownership; the caller owns display synchronization. */
uint8_t cupid_gb_vram_bus_read(const CupidGb *gb, CupidGb *bus, uint16_t address, bool dma_read);
void cupid_gb_vram_bus_begin_transfer(CupidGb *gb);
void cupid_gb_vram_bus_commit_fetch(CupidGb *gb);
bool cupid_gb_vram_bus_state_valid(const CupidGb *gb);
bool cupid_gb_vram_bus_state_encode(const CupidGb *gb, uint8_t *data, size_t size);
bool cupid_gb_vram_bus_state_decode(CupidGb *gb, const uint8_t *data, size_t size);
void cupid_gb_vram_bus_import_legacy(CupidGb *gb);

#endif

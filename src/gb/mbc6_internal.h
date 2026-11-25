/**
 * @file    mbc6_internal.h
 * @brief   MBC6 bus access and native state serialization.
 * @author  cupidthecat
 * @date    2026-11-18
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#ifndef CUPID_GB_MBC6_INTERNAL_H
#define CUPID_GB_MBC6_INTERNAL_H

#include "cupid/gb/gb.h"

enum { CUPID_GB_MBC6_STATE_SIZE = CUPID_GB_MBC6_STORAGE_SIZE + CUPID_GB_MBC6_PAGE_SIZE + 18 };

struct StateIo;

bool cupid_gb_mbc6_read_program(const CupidGb *gb, uint16_t address, uint8_t *value);
unsigned cupid_gb_mbc6_rom_bank(const CupidGb *gb, uint16_t address, bool physical);
size_t cupid_gb_mbc6_ram_offset(const CupidGb *gb, uint16_t address);
void cupid_gb_mbc6_write(CupidGb *gb, uint16_t address, uint8_t value);
bool cupid_gb_mbc6_state_valid(const CupidGb *gb);
void cupid_gb_mbc6_state(struct StateIo *io, CupidGb *gb);

#endif

/**
 * @file    cartridge.h
 * @brief   Cartridge loading, mapper decoding, bank translation, and save-file interfaces.
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

#ifndef CUPID_GB_CARTRIDGE_H
#define CUPID_GB_CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cupid/gb/gb.h"

enum { CUPID_GB_ENTRY_POINT = 0x0100 };

size_t cupid_gb_ram_size_from_code(uint8_t code);
/* Contiguous physical SRAM. NULL is returned only for a NULL instance.
 * The pointer is borrowed and may change after cartridge loading, reset or state import. */
uint8_t *cupid_gb_cartridge_ram(CupidGb *gb);
const uint8_t *cupid_gb_cartridge_ram_const(const CupidGb *gb);
/* Original-file storage bank for the currently mapped ROM region. */
size_t cupid_gb_effective_rom_bank(const CupidGb *gb, bool lower_region);
/* Controller-selected bank before physical ROM address-line aliasing. */
size_t cupid_gb_selected_rom_bank(const CupidGb *gb, bool lower_region);
/* Translate a controller bank into original-file storage, including address-line aliases. */
size_t cupid_gb_rom_file_bank(const CupidGb *gb, size_t bank);
size_t cupid_gb_selected_ram_bank(const CupidGb *gb);
size_t cupid_gb_effective_ram_bank(const CupidGb *gb);
/* Extended MBC3 wiring exposes eight ROM address bits and eight RAM banks. */
bool cupid_gb_has_extended_mbc3(const CupidGb *gb);
/* Dispatch only HuC1 cartridge control writes below 0x8000. */
void cupid_gb_huc1_write(CupidGb *gb, uint16_t address, uint8_t value);
/* Dispatch only HuC3 cartridge control writes below 0x8000. */
void cupid_gb_huc3_write(CupidGb *gb, uint16_t address, uint8_t value);
void cupid_gb_mmm01_write(CupidGb *gb, uint16_t address, uint8_t value);

#endif

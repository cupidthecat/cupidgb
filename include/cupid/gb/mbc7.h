/**
 * @file    mbc7.h
 * @brief   MBC7 motion, serial EEPROM, and cartridge-access interfaces.
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

#ifndef CUPID_GB_MBC7_H
#define CUPID_GB_MBC7_H

#include "cupid/gb/gb.h"

enum { CUPID_GB_MBC7_EEPROM_SIZE = 256 };

/* Supply host motion relative to the neutral reading. Approximately 112
 * counts correspond to one g. Only samples latched by the guest enter saves. */
void cupid_gb_mbc7_set_motion(CupidGb *gb, int16_t x, int16_t y);

/* Internal cartridge bus entry points. */
void cupid_gb_mbc7_reset(CupidGb *gb);
void cupid_gb_mbc7_reset_serial(CupidGb *gb);
uint8_t cupid_gb_mbc7_read(const CupidGb *gb, uint16_t address);
void cupid_gb_mbc7_write(CupidGb *gb, uint16_t address, uint8_t value);

#endif

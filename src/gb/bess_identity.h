/**
 * @file    bess_identity.h
 * @brief   Logical cartridge header selection for portable checkpoint identity.
 * @author  cupidthecat
 * @date    2026-11-19
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

#ifndef CUPID_GB_BESS_IDENTITY_H
#define CUPID_GB_BESS_IDENTITY_H

#include "cupid/gb/cartridge.h"

static inline const uint8_t *cupid_gb_bess_header_bank(const CupidGb *gb) {
    if (!gb || !gb->rom || gb->rom_size < 0x150u || gb->rom_size > gb->rom_capacity)
        return NULL;
    if (gb->header.mbc_type != CUPID_GB_MMM01)
        return gb->rom;
    if (gb->rom_bank_count < 2 || gb->rom_bank_count > gb->rom_capacity / 0x4000u)
        return NULL;
    /* Identity belongs to the menu, even after its mapper has locked onto a
     * game. Original input bytes retain either supported dump layout. */
    return gb->rom + cupid_gb_rom_file_bank(gb, gb->rom_bank_count - 2u) * 0x4000u;
}

#endif

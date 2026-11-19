/**
 * @file    mbc6.h
 * @brief   MBC6 register and flash transaction state.
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

#ifndef CUPID_GB_MBC6_H
#define CUPID_GB_MBC6_H

#include <stdbool.h>
#include <stdint.h>

enum {
    CUPID_GB_MBC6_RAM_SIZE = 0x8000,
    CUPID_GB_MBC6_FLASH_SIZE = 0x100000,
    CUPID_GB_MBC6_HIDDEN_SIZE = 0x100,
    CUPID_GB_MBC6_STORAGE_SIZE = CUPID_GB_MBC6_FLASH_SIZE + CUPID_GB_MBC6_HIDDEN_SIZE,
    CUPID_GB_MBC6_PAGE_SIZE = 0x80
};

typedef struct CupidGbMbc6 {
    uint8_t rom_bank[2];
    uint8_t ram_bank[2];
    bool flash_selected[2];
    bool flash_enabled;
    bool write_enabled;
    bool sector_protected;
    uint8_t flash_mode;
    uint8_t sequence;
    uint8_t setup;
    uint8_t status;
    uint8_t page_count;
    uint32_t page_base;
    uint8_t page[CUPID_GB_MBC6_PAGE_SIZE];
} CupidGbMbc6;

#endif

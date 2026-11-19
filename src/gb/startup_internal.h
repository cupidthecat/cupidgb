/**
 * @file    startup_internal.h
 * @brief   Internal cold-memory and firmware-register initialization interfaces.
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

#ifndef CUPID_GB_STARTUP_INTERNAL_H
#define CUPID_GB_STARTUP_INTERNAL_H

#include "cupid/gb/gb.h"

enum {
    CUPID_GB_STARTUP_LOGO_BYTES = 48,
    CUPID_GB_CGB_BOOT_STACK_START = 0x72,
    CUPID_GB_CGB_BOOT_STACK_END = 0x7e,
    CUPID_GB_CGB_BOOT_WRAM_START = 0x2000,
    CUPID_GB_CGB_BOOT_WRAM_END = 0x3000,
};

void cupid_gb_startup_memory(CupidGb *gb);
void cupid_gb_startup_boot_io(CupidGb *gb);
/* The built-in handoff writes the logo onto cleared VRAM. */
void cupid_gb_startup_logo_tiles(CupidGb *gb, const uint8_t logo[CUPID_GB_STARTUP_LOGO_BYTES]);
void cupid_gb_startup_logo_tilemap(CupidGb *gb);
/* Reset CPU, I/O and hardware clocks before either firmware or a native music
 * entry point. The caller establishes memory and the final overlay ownership. */
void cupid_gb_startup_hardware(CupidGb *gb);

#endif

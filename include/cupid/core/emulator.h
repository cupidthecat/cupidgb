/**
 * @file    emulator.h
 * @brief   Public emulator instance and lifecycle interface.
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

#ifndef CUPID_CORE_EMULATOR_H
#define CUPID_CORE_EMULATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "cupid/core/system.h"
#include "cupid/gb/gb.h"

typedef struct CupidEmulator {
    CupidSystem target_system;
    bool initialized;
    CupidGb gb;
} CupidEmulator;

void cupid_emulator_init(CupidEmulator *emulator, CupidSystem target_system);
void cupid_emulator_shutdown(CupidEmulator *emulator);
bool cupid_emulator_load_rom(CupidEmulator *emulator, const unsigned char *rom_data,
                             size_t rom_size);
bool cupid_emulator_load_rom_file(CupidEmulator *emulator, const char *path);
/* Uses cupid_gb_step's hardware-completion contract, including illegal-opcode HALT. */
bool cupid_emulator_step(CupidEmulator *emulator);

#endif

/**
 * @file    bess.h
 * @brief   Portable cartridge save-state import and export.
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

#ifndef CUPID_GB_BESS_H
#define CUPID_GB_BESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cupid/gb/state.h"

bool cupid_gb_bess_has_footer(const uint8_t *data, size_t length);

/* Portable cartridge checkpoints carry registers and memory. The format has
 * no exact CPU, LCD, APU or transfer pipeline history. Load into the matching
 * cartridge on a compatible model. Failures leave the running machine intact. */
CupidGbStateResult cupid_gb_bess_load(CupidGb *gb, const uint8_t *data, size_t length);

/* Zero means that this program or boot state cannot be exported. */
size_t cupid_gb_bess_size(const CupidGb *gb);
CupidGbStateResult cupid_gb_bess_save(const CupidGb *gb, uint8_t *data, size_t capacity,
                                      size_t *written);

#ifdef __cplusplus
}
#endif

#endif

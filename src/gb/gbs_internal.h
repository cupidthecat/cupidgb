/**
 * @file    gbs_internal.h
 * @brief   Internal music-program identity and transactional allocation interfaces.
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

#ifndef CUPID_GB_GBS_INTERNAL_H
#define CUPID_GB_GBS_INTERNAL_H

#include "cupid/gb/gb.h"
#include "cupid/gb/gbs.h"

/* Allocations follow the malloc/free-compatible, disjoint ownership contract
 * in cartridge_internal.h. Failure never changes the live machine or output. */
CupidGbGbsResult cupid_gb_load_gbs_with_allocator(CupidGb *gb, const uint8_t *data, size_t length,
                                                  CupidGbGbsInfo *info, void *(*allocate)(size_t));
CupidGbGbsResult cupid_gb_gbs_start_track_with_allocator(CupidGb *gb, uint8_t track,
                                                         void *(*allocate)(size_t));

bool cupid_gb_program_valid(const CupidGb *gb);
void cupid_gb_program_digest(const CupidGb *gb, uint8_t digest[32]);

#endif

/**
 * @file    oam_bus.h
 * @brief   Internal object-memory bus conflict entry points.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#ifndef CUPID_GB_OAM_BUS_H
#define CUPID_GB_OAM_BUS_H

#include "cupid/gb/gb.h"

void cupid_gb_trigger_oam_bug_write(CupidGb *gb, uint16_t address);
void cupid_gb_trigger_oam_bug_read_access(CupidGb *gb, uint16_t address);

#endif

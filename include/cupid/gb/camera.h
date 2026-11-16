/**
 * @file    camera.h
 * @brief   Camera cartridge banking, capture, and timing interfaces.
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

#ifndef CUPID_GB_CAMERA_H
#define CUPID_GB_CAMERA_H

#include "cupid/gb/gb.h"

enum { CUPID_GB_CAMERA_MAX_BUSY_T_CYCLES = 129792u + 2048u + 65535u * 64u + 4u };

void cupid_gb_camera_mapper_write(CupidGb *gb, uint16_t address, uint8_t value);
void cupid_gb_camera_tick(CupidGbCamera *camera, unsigned cpu_tcycles);
uint8_t cupid_gb_camera_read_register(const CupidGbCamera *camera, uint16_t address);
void cupid_gb_camera_write_register(CupidGbCamera *camera, uint16_t address, uint8_t value);
uint8_t cupid_gb_camera_read_tile(const CupidGbCamera *camera, unsigned offset);

#endif

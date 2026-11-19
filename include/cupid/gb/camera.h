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

#ifdef __cplusplus
extern "C" {
#endif

enum { CUPID_GB_CAMERA_MAX_BUSY_T_CYCLES = 129792u + 2048u + 65535u * 64u + 4u };

/* A registered provider uses a retained grayscale image, initially black. The
 * callback may complete this request immediately, but must not step, reset or
 * destroy the machine. Asynchronous hosts must queue work and complete at a core
 * boundary on its owning thread. A changed registration cancels the previous
 * request and clears its image; registering the same pair is a no-op. Detaching
 * selects the deterministic generated sensor. Request identities never wrap. */
void cupid_gb_set_camera_callback(CupidGb *gb, CupidGbCameraRequestCallback callback,
                                  void *user_data);

/* Accept exactly one tightly packed 128 by 112 grayscale image, or NULL/0 for a
 * failed capture that keeps the previous image. A stale token or invalid payload
 * changes nothing. Validate the token before copying any pixels. The sequence
 * survives state loads, resets and registration changes within this instance;
 * hosts must also reject results belonging to a replaced or destroyed instance. */
bool cupid_gb_camera_complete(CupidGb *gb, uint64_t request, const uint8_t *pixels, size_t size);

/* Successful timeline transactions discard outstanding host work without
 * changing the retained image or rewinding the request sequence. Generated
 * countdowns are left alone when no provider is registered. */
void cupid_gb_camera_cancel_request(CupidGb *gb);

void cupid_gb_camera_mapper_write(CupidGb *gb, uint16_t address, uint8_t value);
void cupid_gb_camera_tick(CupidGbCamera *camera, unsigned cpu_tcycles);
uint8_t cupid_gb_camera_read_register(const CupidGbCamera *camera, uint16_t address);
void cupid_gb_camera_write_register(CupidGb *gb, uint16_t address, uint8_t value);
uint8_t cupid_gb_camera_read_tile(const CupidGbCamera *camera, const uint8_t *pixels,
                                  unsigned offset);

#ifdef __cplusplus
}
#endif

#endif

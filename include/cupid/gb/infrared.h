/**
 * @file    infrared.h
 * @brief   Host infrared input, output observation, and receiver timing interfaces.
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

#ifndef CUPID_GB_INFRARED_H
#define CUPID_GB_INFRARED_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { CUPID_GB_INFRARED_WARMUP_LIMIT = 34048, CUPID_GB_INFRARED_DECAY_LIMIT = 48048 };

/* Host input and callbacks are live connections, outside native snapshots.
 * Each emitting latch reports its own changed level. Cartridge callbacks see
 * the committed latch; color-port callbacks see the preceding register value.
 * The output query combines both currently latched LEDs. */
void cupid_gb_set_infrared_callback(CupidGb *gb, CupidGbInfraredCallback callback, void *user_data);
void cupid_gb_set_infrared_input(CupidGb *gb, bool active);
bool cupid_gb_infrared_output(const CupidGb *gb);
bool cupid_gb_infrared_detected(const CupidGb *gb);

/* Device entry points preserve LED latches when a guest closes the receive
 * window. The CGB output latch is writable in monochrome compatibility mode. */
void cupid_gb_infrared_write_cartridge(CupidGb *gb, uint8_t value);
void cupid_gb_infrared_write_port(CupidGb *gb, uint8_t value);
void cupid_gb_infrared_tick(CupidGb *gb, unsigned dots);

#ifdef __cplusplus
}
#endif

#endif

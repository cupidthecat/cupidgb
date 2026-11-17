/**
 * @file    reset.h
 * @brief   Native hard and quick reset contracts for a loaded Game Boy cartridge.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#ifndef CUPID_GB_RESET_H
#define CUPID_GB_RESET_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CupidGb;

typedef enum CupidGbResetKind {
    CUPID_GB_RESET_HARD = 0,
    CUPID_GB_RESET_QUICK = 1
} CupidGbResetKind;

/* Reset a loaded cartridge without changing its ownership or host registrations.
 * Quick reset keeps physical WRAM, HRAM, OAM, palette RAM and cartridge storage.
 * Both forms restart controller latches and guest execution, retain battery RAM,
 * preserve the handheld audio stream, and rebase the active RTC host timestamp.
 * Executed SGB firmware restarts host introduction and clears its queued sound.
 * HuC3 clock/alarm registers reset with its controller; MBC3/TPP1 clocks survive.
 * Host input masks and pending samples survive; JOYP starts with released contacts
 * until a pending host sample or a selection-changing write updates its latch.
 *
 * Installed firmware starts at PC 0. Without firmware, the explicit approximate
 * post-boot profile starts at PC 0x0100. Its color handoff clears WRAM bank 2 and OAM.
 * No guest execution or output callback occurs inside this operation. A built-in
 * printer's protocol resets only after the new machine has been committed.
 *
 * Invalid arguments, an invalid firmware size, or allocation failure return false
 * without changing the machine, attached printer, or calling its host clock source.
 * Call only between guest execution steps; the core is not reentrant. */
bool cupid_gb_reset(struct CupidGb *gb, CupidGbResetKind kind);

#ifdef __cplusplus
}
#endif

#endif

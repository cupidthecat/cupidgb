/**
 * @file    cartridge_internal.h
 * @brief   Shared cartridge storage, controller reset, and startup helpers.
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

#ifndef CUPID_GB_CARTRIDGE_INTERNAL_H
#define CUPID_GB_CARTRIDGE_INTERNAL_H

#include "cupid/gb/gb.h"
#include "cupid/gb/reset.h"

/* Validate the active SRAM span and its exclusive host ownership representation. */
bool cupid_gb_cartridge_storage_valid(const CupidGb *gb);

/* Inputs that start inside owned core, ROM or SRAM storage must remain within
 * that allocation. Bounded self-imports and independent caller buffers are valid. */
bool cupid_gb_cartridge_source_fits(const CupidGb *gb, const void *source, size_t length);

/* Physical header, layout and storage ownership must already be installed.
 * Controller reset does not erase cartridge storage or touch host callbacks. */
void cupid_gb_cartridge_reset_controls(CupidGb *gb);
void cupid_gb_cartridge_post_boot(CupidGb *gb);

/* A per-call allocator permits deterministic failure tests without global
 * allocation hooks. It must return NULL or a new non-overlapping allocation
 * of at least the requested size with ordinary malloc/free-compatible
 * ownership; the loader retains and later frees successful allocations.
 * Both ROM and SRAM preparation use this allocator. Failure leaves an already
 * loaded cartridge unchanged, including when a previous allocation succeeded. */
bool cupid_gb_load_rom_with_allocator(CupidGb *gb, const uint8_t *rom_data, size_t rom_size,
                                      void *(*allocate)(size_t));

/* Reset candidates own their writable SRAM and borrow only immutable ROM.
 * The allocator contract above also applies to the candidate structure. */
bool cupid_gb_reset_with_allocator(CupidGb *gb, CupidGbResetKind kind, void *(*allocate)(size_t));

/* Reattach host registrations after a detached candidate's complete preparation.
 * The caller restores audio at its required point relative to register writes.
 * Commit transfers candidate ownership and releases the old writable SRAM. The
 * caller separately releases an old ROM when replacing the program image. */
void cupid_gb_reset_restore_host(CupidGb *next, const CupidGb *previous);
void cupid_gb_reset_commit(CupidGb *gb, CupidGb *next);

#endif

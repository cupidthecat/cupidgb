/**
 * @file    state_internal.h
 * @brief   Native-state allocation seam for deterministic transaction-failure checks.
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

#ifndef CUPID_GB_STATE_INTERNAL_H
#define CUPID_GB_STATE_INTERNAL_H

#include "cupid/gb/state.h"

/* Each successful allocation must be new, non-overlapping storage compatible
 * with free(). The allocator is used only for this call, with no global hook.
 * Rejection or allocation failure preserves the core, its complete SRAM and
 * any live accessory. The ordinary public entry points use malloc(). */
CupidGbStateResult cupid_gb_load_state_with_allocator(CupidGb *gb, const uint8_t *data,
                                                      size_t length, bool live_audio,
                                                      void *(*allocate)(size_t));

#endif

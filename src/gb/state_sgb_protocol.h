/**
 * @file    state_sgb_protocol.h
 * @brief   Fixed-width adapter command eligibility and received-header state codec.
 * @author  cupidthecat
 * @date    2026-11-17
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

#ifndef CUPID_GB_STATE_SGB_PROTOCOL_H
#define CUPID_GB_STATE_SGB_PROTOCOL_H

#include "cupid/gb/gb.h"

enum { CUPID_GB_SGB_PROTOCOL_STATE_SIZE = 85 };

/* Physical adapters may retain any cached bytes, including incomplete or replaced
 * header sections. Other models require the complete extension to be zero. */
bool cupid_gb_sgb_protocol_state_valid(const CupidGb *gb);
bool cupid_gb_sgb_protocol_wire_valid(const CupidGb *gb, const uint8_t *data, size_t length);
bool cupid_gb_sgb_protocol_state_encode(const CupidGb *gb, uint8_t *data, size_t length);
bool cupid_gb_sgb_protocol_state_decode(CupidGb *gb, const uint8_t *data, size_t length);

/* Call after decoding the saved declaration. Older files have no received-header
 * history; preserve their declared receiver permission without borrowing a cache. */
void cupid_gb_sgb_protocol_state_import_legacy(CupidGb *gb);

#endif

/**
 * @file    input.h
 * @brief   Joypad masks, selected rows, input interrupts, and contact-settling interfaces.
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

#ifndef CUPID_GB_INPUT_H
#define CUPID_GB_INPUT_H

#include "cupid/gb/gb.h"

/* Bits 0..7 are Right, Left, Up, Down, A, B, Select, Start; zero means pressed.
 * Host changes, including unchanged resubmissions, wait for the next delivered
 * hardware span. Sampling then updates JOYP and detects selected falling edges.
 * External-host SGB models retain these masks without sampling their contacts. */
void cupid_gb_set_buttons(CupidGb *gb, uint8_t released_mask);
/* Players are numbered 0..3. Additional pads are selected by the SGB protocol;
 * changing an unselected pad does not produce a selected-line interrupt. */
bool cupid_gb_set_buttons_for_player(CupidGb *gb, unsigned player, uint8_t released_mask);

/* A registered host owns every raw P1 write, including repeated select values.
 * It may supply contact lines with cupid_gb_set_external_joyp, but must not run
 * or otherwise reenter the core. Registration survives initialization, resets,
 * cartridge swaps and state loading; cleanup clears it. NULL detaches it. */
void cupid_gb_set_joyp_write_callback(CupidGb *gb, CupidGbJoypWriteCallback callback,
                                      void *user_data);
/* External-host SGB contacts are driven independently of ordinary pad masks.
 * Replace P10..P13 (zero means closed), retain the select bits, and request the
 * joypad interrupt on falling contact edges. Other models ignore this call. */
void cupid_gb_set_external_joyp(CupidGb *gb, uint8_t lines);

/* Memory-map helpers. Monochrome row release is delayed while select-bit
 * readback changes immediately. Physical contact bounce is not modeled. */
uint8_t cupid_gb_input_read(const CupidGb *gb);
/* Passive inspection exposes the sampled active row, without guest readback overrides. */
uint8_t cupid_gb_input_peek(const CupidGb *gb);
void cupid_gb_input_select(CupidGb *gb, uint8_t value);
/* Deliver input sampling and row settling, including during HALT and STOP.
 * A zero-duration request never samples input. Color delivery can be half a dot. */
void cupid_gb_input_tick(CupidGb *gb, unsigned base_cycles);
void cupid_gb_input_tick_half_dots(CupidGb *gb, unsigned half_dots);
/* Older native files stored host masks but no sampled low nibble or pending flag.
 * Reconstruct their immediate read value without producing an interrupt. */
void cupid_gb_input_import_legacy(CupidGb *gb);

#endif

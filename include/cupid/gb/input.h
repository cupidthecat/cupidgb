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
 * Use this entry point instead of writing joypad directly so selected-line
 * transitions also request the hardware interrupt. */
void cupid_gb_set_buttons(CupidGb *gb, uint8_t released_mask);
/* Players are numbered 0..3. Additional pads are selected by the SGB protocol;
 * changing an unselected pad does not produce a selected-line interrupt. */
bool cupid_gb_set_buttons_for_player(CupidGb *gb, unsigned player, uint8_t released_mask);

/* Memory-map helpers. Monochrome row release is delayed while select-bit
 * readback changes immediately. Physical contact bounce is not modeled. */
uint8_t cupid_gb_input_read(const CupidGb *gb);
void cupid_gb_input_select(CupidGb *gb, uint8_t value);
/* Advance row settling by base-clock dots, including during HALT and STOP. */
void cupid_gb_input_tick(CupidGb *gb, unsigned base_cycles);

#endif

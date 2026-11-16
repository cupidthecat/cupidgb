/**
 * @file    cpu.h
 * @brief   CPU execution, interrupt requests, and OAM corruption interfaces.
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

#ifndef CUPID_GB_CPU_H
#define CUPID_GB_CPU_H

#include <stdbool.h>
#include <stdint.h>

#include "cupid/gb/gb.h"
#include "cupid/gb/timer.h"

enum {
    CUPID_GB_FLAG_Z = 0x80,
    CUPID_GB_FLAG_N = 0x40,
    CUPID_GB_FLAG_H = 0x20,
    CUPID_GB_FLAG_C = 0x10,
    CUPID_GB_INTERRUPT_VBLANK = 0x01,
    CUPID_GB_INTERRUPT_LCD_STAT = 0x02,
    CUPID_GB_INTERRUPT_TIMER = 0x04,
    CUPID_GB_INTERRUPT_SERIAL = 0x08,
    CUPID_GB_INTERRUPT_JOYPAD = 0x10,
    CUPID_GB_INTERRUPT_CYCLES = 5,
    CUPID_GB_HALT_CYCLES = 1,
    CUPID_GB_CPU_DECODE_ERROR = UINT8_MAX
};

void cupid_gb_request_interrupt(CupidGb *gb, uint8_t mask);
bool cupid_gb_service_interrupt(CupidGb *gb);
void cupid_gb_trigger_oam_bug_write_access(CupidGb *gb, uint16_t address);
/* Classify the primary byte only; every CB-prefixed sub-opcode is defined. */
bool cupid_gb_opcode_is_illegal(uint8_t opcode);
/* Returns remaining machine cycles, zero for timing completed inline, or
 * CUPID_GB_CPU_DECODE_ERROR. Check the failure sentinel before advancing clocks. */
uint8_t cupid_gb_execute_unprefixed(CupidGb *gb, uint8_t opcode, CupidGbCpuBus *bus);
uint8_t cupid_gb_fetch_u8(CupidGb *gb);

#endif

/**
 * @file    timer.h
 * @brief   Hardware clock, divider/timer, DMA, and serial scheduling interfaces.
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

#ifndef CUPID_GB_TIMER_H
#define CUPID_GB_TIMER_H

#include <stdint.h>

#include "cupid/gb/gb.h"

void cupid_gb_tick(CupidGb *gb, uint16_t cycles);
/* Advance one complete color-DMA slot in CPU T-cycles. The caller captures its
 * duration at transfer entry and carries the returned CPU phase between slots. */
unsigned cupid_gb_tick_color_dma_slot(CupidGb *gb, unsigned cycles, unsigned phase);
uint8_t cupid_gb_tick_halt(CupidGb *gb);
void cupid_gb_acknowledge_interrupt(CupidGb *gb, uint8_t mask);
/* Complete an interrupt after its initial four CPU T-cycles. Stack register
 * writes, request selection, vector assignment, and T18 acknowledgment share
 * one local timeline; no partial entry state escapes a hardware step. */
void cupid_gb_interrupt_tail(CupidGb *gb, uint16_t return_address);

/* A register write can settle before or after its ordinary machine-cycle
 * boundary. Negative credit leaves T-cycles for the next access or tail;
 * positive credit has already consumed part of that following span. */
typedef struct CupidGbCpuBus {
    int8_t credit;
} CupidGbCpuBus;

void cupid_gb_cpu_write(CupidGb *gb, uint16_t address, uint8_t value, uint16_t leading_mcycles,
                        CupidGbCpuBus *bus);
void cupid_gb_cpu_finish(CupidGb *gb, uint16_t mcycles, CupidGbCpuBus *bus);
/* Internal bus access. half_dot distinguishes the intervening CGB double-speed
 * edge; direct host writes use cupid_gb_write_u8 at a whole-dot boundary. */
void cupid_gb_write_u8_phase(CupidGb *gb, uint16_t address, uint8_t value, bool half_dot);
void cupid_gb_timer_apply_div_reset(CupidGb *gb);
void cupid_gb_timer_apply_tac_write(CupidGb *gb, uint8_t value);
void cupid_gb_timer_apply_tima_write(CupidGb *gb, uint8_t value);
void cupid_gb_timer_apply_tma_write(CupidGb *gb, uint8_t value);
void cupid_gb_serial_master_edge(CupidGb *gb);
void cupid_gb_serial_apply_control_write(CupidGb *gb, uint8_t value);

#endif

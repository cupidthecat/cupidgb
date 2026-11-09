#ifndef CUPID_GB_TIMER_H
#define CUPID_GB_TIMER_H

#include <stdint.h>

#include "cupid/gb/gb.h"

void cupid_gb_tick(CupidGb *gb, uint16_t cycles);
/* Advance one two-dot DMA bus slot. The caller marks CPU machine-cycle edges;
 * a complete transfer consumes its local half-cycle phase before returning. */
void cupid_gb_tick_color_dma_slot(CupidGb *gb, bool cpu_boundary);
uint8_t cupid_gb_tick_halt(CupidGb *gb);
void cupid_gb_acknowledge_interrupt(CupidGb *gb, uint8_t mask);
/* Complete an interrupt after its initial eight CPU T-cycles. Stack register
 * writes, request selection, vector assignment, and T18 acknowledgment share
 * one local timeline; no partial entry state escapes a hardware step. */
void cupid_gb_interrupt_tail(CupidGb *gb, uint16_t return_address);
void cupid_gb_cpu_write(CupidGb *gb, uint16_t address, uint8_t value, uint16_t leading_mcycles);
void cupid_gb_timer_apply_div_reset(CupidGb *gb);
void cupid_gb_timer_apply_tac_write(CupidGb *gb, uint8_t value);
void cupid_gb_timer_apply_tima_write(CupidGb *gb, uint8_t value);
void cupid_gb_timer_apply_tma_write(CupidGb *gb, uint8_t value);
void cupid_gb_serial_master_edge(CupidGb *gb);

#endif

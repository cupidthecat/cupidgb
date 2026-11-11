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

/* A write may settle one CPU T-cycle into the caller's following cycle. The
 * instruction owns this local credit until its next access or final tail. */
typedef struct CupidGbCpuBus {
    uint8_t credit;
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

#endif

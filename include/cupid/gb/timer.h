#ifndef CUPID_GB_TIMER_H
#define CUPID_GB_TIMER_H

#include <stdint.h>

#include "cupid/gb/gb.h"

void cupid_gb_tick(CupidGb *gb, uint16_t cycles);
void cupid_gb_timer_apply_div_reset(CupidGb *gb);
void cupid_gb_timer_apply_tac_write(CupidGb *gb, uint8_t value);
void cupid_gb_timer_apply_tima_write(CupidGb *gb, uint8_t value);
void cupid_gb_timer_apply_tma_write(CupidGb *gb, uint8_t value);

#endif

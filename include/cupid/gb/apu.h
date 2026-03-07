#ifndef CUPID_GB_APU_H
#define CUPID_GB_APU_H

#include <stdint.h>

#include "cupid/gb/gb.h"

void cupid_gb_apu_on_write(CupidGb *gb, uint8_t off, uint8_t val);
void cupid_gb_tick_apu(CupidGb *gb, uint16_t cycles);

#endif

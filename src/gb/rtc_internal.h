#ifndef CUPID_GB_RTC_INTERNAL_H
#define CUPID_GB_RTC_INTERNAL_H

#include "cupid/gb/gb.h"

int64_t cupid_gb_rtc_now(const CupidGb *gb);
/* Deliver one CPU/bus clock span after speed-switch freeze. Pixel/DMA
 * subdivisions must not create extra scheduler polls or consume extra debt. */
void cupid_gb_rtc_tick(CupidGb *gb, uint32_t half_cycles);
void cupid_gb_rtc_bind_cartridge(CupidGb *gb);
void cupid_gb_rtc_update_at(CupidGb *gb, int64_t now);

#endif

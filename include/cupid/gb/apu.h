#ifndef CUPID_GB_APU_H
#define CUPID_GB_APU_H

#include <stdint.h>

#include "cupid/gb/gb.h"

void cupid_gb_apu_on_write(CupidGb *gb, uint8_t off, uint8_t val);
void cupid_gb_tick_apu(CupidGb *gb, uint16_t cycles);
void cupid_gb_apu_div_event(CupidGb *gb);
void cupid_gb_apu_div_secondary_event(CupidGb *gb);

/* Output defaults to 44100 Hz. Rates from 8000 through 192000 Hz are accepted.
 * Changing the rate discards queued and partial output frames, preserving oscillator state.
 */
bool cupid_gb_apu_set_sample_rate(CupidGb *gb, uint32_t sample_rate);
uint32_t cupid_gb_apu_get_sample_rate(const CupidGb *gb);
/* Initialize newly modeled physical latches when importing an older snapshot or
 * an HLE boot profile. Output buffers, integration, and filters remain intact;
 * absent power/trigger history receives a deterministic approximation. */
void cupid_gb_apu_import_legacy(CupidGb *gb);
/* DC blocking is enabled by default. Disabling it clears the coupling capacitor state. */
void cupid_gb_apu_set_highpass(CupidGb *gb, bool enabled);
/* Color-hardware digital channel taps, before routing, master gain, and filtering.
 * These remain accessible in monochrome compatibility mode; other hardware reads 0xff.
 */
uint8_t cupid_gb_apu_read_pcm12(const CupidGb *gb);
uint8_t cupid_gb_apu_read_pcm34(const CupidGb *gb);

#endif

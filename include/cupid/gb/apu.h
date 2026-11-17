/**
 * @file    apu.h
 * @brief   Game Boy sound hardware, audio configuration, and sample interfaces.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */
#ifndef CUPID_GB_APU_H
#define CUPID_GB_APU_H

#include <stdint.h>

#include "cupid/gb/audio_types.h"
#include "cupid/gb/gb.h"

void cupid_gb_apu_on_write(CupidGb *gb, uint8_t off, uint8_t val);
void cupid_gb_tick_apu(CupidGb *gb, uint16_t cycles);
/* Timer delivery completes a pending envelope before the next divider event.
 * Clocks supplied by the current event preserve any envelope it just latched;
 * direct APU ticking retains its separate cycle-by-cycle delay semantics. */
void cupid_gb_apu_finish_divider_period(CupidGb *gb);
void cupid_gb_tick_apu_divider(CupidGb *gb, uint16_t cycles);
/* Deliver previously credited audio clocks. Ordinary delivery may retain a short
 * pending span; register and divider boundaries force its execution. */
void cupid_gb_apu_run(CupidGb *gb, bool force);
/* Seed missing reconstruction history when importing an older native snapshot. */
void cupid_gb_audio_import_legacy(CupidGb *gb);
void cupid_gb_apu_div_event(CupidGb *gb);
void cupid_gb_apu_div_secondary_event(CupidGb *gb);

/* Output defaults to 44100 Hz. Rates from 8000 through 192000 Hz are accepted.
 * Changing the rate retains reconstruction, queued samples, and filter history.
 * A different rate is rejected without mutation while startup audio is pending;
 * drain that output and retry. Selecting the current rate remains allowed.
 */
bool cupid_gb_apu_set_sample_rate(CupidGb *gb, uint32_t sample_rate);
uint32_t cupid_gb_apu_get_sample_rate(const CupidGb *gb);
/* Initialize newly modeled physical latches when importing an older snapshot or
 * an HLE boot profile. Output buffers, integration, and filters remain intact;
 * absent power/trigger history receives a deterministic approximation. */
void cupid_gb_apu_import_legacy(CupidGb *gb);
/* Reconstruct pulse clock and output latches absent from older monochrome engines.
 * This leaves oscillators, queued audio, integration and filter state intact. */
void cupid_gb_apu_import_monochrome_pulses(CupidGb *gb);
/* Older monochrome engines stored a combined noise timer, without its divider
 * or feedback latch. Seed a deterministic approximation of those missing clocks. */
void cupid_gb_apu_import_monochrome_noise(CupidGb *gb);
/* DC blocking is enabled by default. Disabling it clears the capacitors when the
 * next connected output frame is rendered. */
void cupid_gb_apu_set_highpass(CupidGb *gb, bool enabled);
/* Host audio choices do not write guest sound registers or advance clocks.
 * Selecting a filter retains its capacitors until the next connected frame.
 * Channel mute changes take effect at the next channel-output update. */
bool cupid_gb_apu_set_filter_mode(CupidGb *gb, CupidGbAudioFilter mode);
CupidGbAudioFilter cupid_gb_apu_get_filter_mode(const CupidGb *gb);
bool cupid_gb_apu_set_interference_volume(CupidGb *gb, double volume);
double cupid_gb_apu_get_interference_volume(const CupidGb *gb);
bool cupid_gb_apu_set_channel_muted(CupidGb *gb, unsigned channel, bool muted);
bool cupid_gb_apu_is_channel_muted(const CupidGb *gb, unsigned channel);
/* Color-hardware digital channel taps, before routing, master gain, and filtering.
 * These remain accessible in monochrome compatibility mode; other hardware reads 0xff.
 */
uint8_t cupid_gb_apu_read_pcm12(const CupidGb *gb);
uint8_t cupid_gb_apu_read_pcm34(const CupidGb *gb);

#endif

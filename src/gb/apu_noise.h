/**
 * @file    apu_noise.h
 * @brief   Internal sound-noise timing and register-transition interfaces.
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
#ifndef CUPID_GB_APU_NOISE_H
#define CUPID_GB_APU_NOISE_H

#include "cupid/gb/gb.h"

bool cupid_gb_noise_tick(CupidGb *gb, bool sample_pcm);
void cupid_gb_noise_prepare(CupidGb *gb, bool active);
void cupid_gb_noise_frequency_write(CupidGb *gb, uint8_t previous);
void cupid_gb_noise_disconnect(CupidGb *gb);

#endif

/**
 * @file    tpp1.h
 * @brief   TPP1 clock, mode, motor, and cartridge-register interfaces.
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

#ifndef CUPID_GB_TPP1_H
#define CUPID_GB_TPP1_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CUPID_GB_TPP1_MOTOR = 0x03,
    CUPID_GB_TPP1_CLOCK_RUNNING = 0x04,
    CUPID_GB_TPP1_CLOCK_OVERFLOW = 0x08
};

bool cupid_gb_tpp1_valid_mode(uint8_t mode);
void cupid_gb_tpp1_advance(CupidGbTpp1 *device, int64_t host_seconds);
uint8_t cupid_gb_tpp1_read(const CupidGb *gb, uint16_t address);
void cupid_gb_tpp1_write_window(CupidGb *gb, uint16_t address, uint8_t value);
void cupid_gb_tpp1_write_register(CupidGb *gb, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file    oam_bus.h
 * @brief   Internal object-memory bus conflict entry points.
 * @author  cupidthecat
 * @date    2026-11-18
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

#ifndef CUPID_GB_OAM_BUS_H
#define CUPID_GB_OAM_BUS_H

#include "cupid/gb/gb.h"

static inline size_t cupid_gb_extra_oam_size(CupidGbModel model) {
    if (model == CUPID_GB_MODEL_CGB_D)
        return 48u;
    return model >= CUPID_GB_MODEL_CGB_0 && model <= CUPID_GB_MODEL_CGB_C ? 24u : 0u;
}

uint8_t cupid_gb_extra_oam_read(const CupidGb *gb, uint8_t address);
void cupid_gb_extra_oam_write(CupidGb *gb, uint8_t address, uint8_t value);

static inline uint8_t cupid_gb_oam_read_raw(const CupidGb *gb, uint8_t address) {
    if (address < sizeof(gb->object_attribute_memory))
        return gb->object_attribute_memory[address];
    return cupid_gb_extra_oam_read(gb, address);
}

void cupid_gb_trigger_oam_bug_write(CupidGb *gb, uint16_t address);
void cupid_gb_trigger_oam_bug_read_access(CupidGb *gb, uint16_t address);

#endif

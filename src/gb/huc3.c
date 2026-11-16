/**
 * @file    huc3.c
 * @brief   HuC3 nibble commands, clock registers, alarms, and infrared access.
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

#include "cupid/gb/huc3.h"

enum { MINUTES_PER_DAY = 1440, SECONDS_PER_MINUTE = 60 };

typedef struct Huc3Register {
    uint16_t *value;
    unsigned shift;
} Huc3Register;

static Huc3Register clock_register(CupidGbHuc3 *device, unsigned index) {
    if (index < 3u)
        return (Huc3Register){&device->minutes, index * 4u};
    if (index < 7u)
        return (Huc3Register){&device->days, (index - 3u) * 4u};
    return (Huc3Register){0};
}

static void write_clock_register(CupidGb *gb, unsigned digit) {
    CupidGbHuc3 *device = &gb->huc3;
    const unsigned index = device->index;
    Huc3Register reg = clock_register(device, index);
    if (index >= 0x58u && index < 0x5bu)
        reg = (Huc3Register){&device->alarm_minutes, (index - 0x58u) * 4u};
    else if (index >= 0x5bu && index < 0x5fu)
        reg = (Huc3Register){&device->alarm_days, (index - 0x5bu) * 4u};

    if (reg.value) {
        const unsigned mask = 0x0fu << reg.shift;
        const uint16_t next = (uint16_t)((*reg.value & ~mask) | (digit << reg.shift));
        if (next != *reg.value)
            ++gb->battery_write_generation;
        *reg.value = next;
    } else if (index == 0x5fu) {
        const bool enabled = (digit & 1u) != 0;
        if (enabled != device->alarm_enabled)
            ++gb->battery_write_generation;
        device->alarm_enabled = enabled;
    }
}

static void command(CupidGb *gb, uint8_t value) {
    CupidGbHuc3 *device = &gb->huc3;
    const uint8_t digit = value & 0x0fu;
    switch (value >> 4) {
        case 1: {
            const Huc3Register reg = clock_register(device, device->index++);
            if (reg.value) {
                gb->huc3_value = (uint8_t)((*reg.value >> reg.shift) & 0x0fu);
            }
            break;
        }
        case 2:
        case 3:
            write_clock_register(gb, digit);
            if (value >> 4 == 3)
                ++device->index;
            break;
        case 4:
            device->index = (uint8_t)((device->index & 0xf0u) | digit);
            break;
        case 5:
            device->index = (uint8_t)((device->index & 0x0fu) | (digit << 4));
            break;
        case 6:
            device->flags = digit;
            break;
    }
}

uint8_t cupid_gb_huc3_read_window(const CupidGb *gb) {
    if (gb->huc3_mode == CUPID_GB_HUC3_RESPONSE)
        return gb->huc3.flags == 2 ? 1u : gb->huc3_value;
    if (gb->huc3_mode == CUPID_GB_HUC3_INFRARED)
        return cupid_gb_infrared_detected(gb) ? 1u : 0u;
    return 1u;
}

void cupid_gb_huc3_write_window(CupidGb *gb, uint8_t value) {
    if (gb->huc3_mode == CUPID_GB_HUC3_COMMAND)
        command(gb, value);
    else if (gb->huc3_mode == CUPID_GB_HUC3_INFRARED)
        cupid_gb_infrared_write_cartridge(gb, value);
}

void cupid_gb_huc3_tick_infrared(CupidGb *gb, unsigned dots) {
    cupid_gb_infrared_tick(gb, dots);
}

uint32_t cupid_gb_huc3_seconds_to_alarm(const CupidGb *gb, int64_t host_seconds) {
    enum { DAY_MASK = 0x1fff, ALARM_SET = 0x2000 };

    if (!gb || !gb->loaded || gb->header.mbc_type != CUPID_GB_HUC3 || host_seconds < 0 ||
        !gb->huc3.alarm_enabled || !(gb->huc3.alarm_days & ALARM_SET))
        return 0;
    const CupidGbHuc3 clock = gb->huc3;
    const uint32_t now =
        (((unsigned)clock.days & DAY_MASK) * MINUTES_PER_DAY + clock.minutes) * SECONDS_PER_MINUTE +
        (uint32_t)(host_seconds % SECONDS_PER_MINUTE);
    const uint32_t alarm =
        (((unsigned)clock.alarm_days & DAY_MASK) * MINUTES_PER_DAY + clock.alarm_minutes) *
        SECONDS_PER_MINUTE;
    return alarm > now ? alarm - now : 0;
}

void cupid_gb_huc3_set_infrared_input(CupidGb *gb, bool active) {
    cupid_gb_set_infrared_input(gb, active);
}

bool cupid_gb_huc3_infrared_output(const CupidGb *gb) {
    return gb && gb->loaded && gb->header.mbc_type == CUPID_GB_HUC3 && gb->huc3.ir_output;
}

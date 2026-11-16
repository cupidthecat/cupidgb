/**
 * @file    tpp1.c
 * @brief   TPP1 bank controls, clock-register commands, and cartridge motor state.
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

#include "cupid/gb/tpp1.h"
#include "rtc_internal.h"

#include <string.h>

enum {
    READ_REGISTERS = 0,
    READ_SRAM = 2,
    WRITE_SRAM = 3,
    RTC_REGISTERS = 5,
    RTC_LATCH = 0x10,
    RTC_COMMIT = 0x11,
    RTC_CLEAR_OVERFLOW = 0x14,
    RTC_STOP = 0x18,
    RTC_START = 0x19,
    MOTOR_OFF = 0x20,
    MOTOR_MAXIMUM = 0x23
};

bool cupid_gb_tpp1_valid_mode(uint8_t mode) {
    return mode == READ_REGISTERS || mode == READ_SRAM || mode == WRITE_SRAM ||
           mode == RTC_REGISTERS;
}

static size_t ram_offset(const CupidGb *gb, uint16_t address) {
    const size_t selected = (size_t)gb->tpp1.ram_bank * 0x2000u + (address & 0x1fffu);
    return selected & (gb->cartridge_ram_size - 1u);
}

uint8_t cupid_gb_tpp1_read(const CupidGb *gb, uint16_t address) {
    const CupidGbTpp1 *device = &gb->tpp1;
    const unsigned port = address & 3u;
    if (device->mode == READ_REGISTERS) {
        if (port == 0)
            return (uint8_t)device->rom_bank;
        if (port == 1)
            return (uint8_t)(device->rom_bank >> 8);
        if (port == 2)
            return device->ram_bank;
        return device->status | device->motor_strength;
    }
    if (device->mode == RTC_REGISTERS)
        return device->latched[port];
    return gb->cartridge_ram_size ? gb->cartridge_ram[ram_offset(gb, address)] : 0xffu;
}

void cupid_gb_tpp1_write_window(CupidGb *gb, uint16_t address, uint8_t value) {
    CupidGbTpp1 *device = &gb->tpp1;
    if (device->mode == WRITE_SRAM && gb->cartridge_ram_size) {
        const size_t offset = ram_offset(gb, address);
        if (gb->header.has_battery && gb->cartridge_ram[offset] != value)
            ++gb->battery_write_generation;
        gb->cartridge_ram[offset] = value;
    } else if (device->mode == RTC_REGISTERS) {
        device->latched[address & 3u] = value;
    }
}

static void clock_command(CupidGb *gb, uint8_t command) {
    CupidGbTpp1 *device = &gb->tpp1;
    if (command == RTC_LATCH) {
        memcpy(device->latched, device->live, sizeof(device->latched));
        gb->rtc.ldh = gb->rtc.dh;
        ++gb->battery_write_generation;
    } else if (command == RTC_COMMIT) {
        memcpy(device->live, device->latched, sizeof(device->live));
        gb->rtc.dh = gb->rtc.ldh;
        ++gb->battery_write_generation;
    } else if (command == RTC_CLEAR_OVERFLOW) {
        device->status &= (uint8_t)~CUPID_GB_TPP1_CLOCK_OVERFLOW;
    } else if (command == RTC_STOP) {
        if (gb->rtc_policy.mode == CUPID_GB_RTC_SYNC_TO_HOST)
            cupid_gb_tpp1_advance(device, cupid_gb_rtc_now(gb));
        device->status &= (uint8_t)~CUPID_GB_TPP1_CLOCK_RUNNING;
    } else {
        if (!(device->status & CUPID_GB_TPP1_CLOCK_RUNNING) &&
            gb->rtc_policy.mode == CUPID_GB_RTC_SYNC_TO_HOST)
            device->rtc_base_time = cupid_gb_rtc_now(gb);
        device->status |= CUPID_GB_TPP1_CLOCK_RUNNING;
    }
}

static void motor_command(CupidGb *gb, uint8_t command) {
    CupidGbTpp1 *device = &gb->tpp1;
    const uint8_t strength = command & CUPID_GB_TPP1_MOTOR;
    const bool was_active = device->motor_strength != 0;
    const uint8_t previous_strength = cupid_gb_rumble_strength(gb);
    device->motor_strength = strength;
    if (was_active != (strength != 0) && gb->rumble_callback)
        gb->rumble_callback(gb, strength != 0, gb->elapsed_cycles, gb->rumble_user_data);
    const uint8_t effective_strength = cupid_gb_rumble_strength(gb);
    if (effective_strength != previous_strength && gb->rumble_strength_callback)
        gb->rumble_strength_callback(gb, effective_strength, gb->elapsed_cycles,
                                     gb->rumble_strength_user_data);
}

void cupid_gb_tpp1_write_register(CupidGb *gb, uint16_t address, uint8_t value) {
    CupidGbTpp1 *device = &gb->tpp1;
    switch (address & 3u) {
        case 0:
            device->rom_bank = (uint16_t)((device->rom_bank & 0xff00u) | value);
            break;
        case 1:
            device->rom_bank = (uint16_t)((device->rom_bank & 0xffu) | (unsigned)value << 8);
            break;
        case 2:
            device->ram_bank = value;
            break;
        default:
            if (cupid_gb_tpp1_valid_mode(value)) {
                device->mode = value;
            } else if (value >= MOTOR_OFF && value <= MOTOR_MAXIMUM) {
                motor_command(gb, value);
            } else if (value == RTC_LATCH || value == RTC_COMMIT || value == RTC_CLEAR_OVERFLOW ||
                       value == RTC_STOP || value == RTC_START) {
                clock_command(gb, value);
            }
            break;
    }
    /* Reset leaves bank 1 mapped while control readback starts at zero. Every
     * control-port write, including a mode or clock command, commits the latches. */
    gb->current_rom_bank = device->rom_bank;
    gb->current_ram_bank = device->ram_bank;
    gb->ram_enabled = device->mode == READ_SRAM || device->mode == WRITE_SRAM;
}

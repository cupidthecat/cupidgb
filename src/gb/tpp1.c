#include "cupid/gb/tpp1.h"

#include <string.h>
#include <time.h>

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

/* RTC counters preserve out-of-range values written through the latch. Such
 * values wrap at the field's bit width before reaching its next carry point. */
static uint64_t clock_digit(uint8_t *value, unsigned width, unsigned limit, uint64_t ticks) {
    const unsigned capacity = 1u << width;
    const unsigned until_carry = limit + (*value < limit ? 0u : capacity) - *value;
    if (ticks < until_carry) {
        *value = (uint8_t)(((uint64_t)*value + ticks) & (capacity - 1u));
        return 0;
    }
    ticks -= until_carry;
    *value = (uint8_t)(ticks % limit);
    return 1 + ticks / limit;
}

void cupid_gb_tpp1_advance(CupidGbTpp1 *device, int64_t host_seconds) {
    if (host_seconds < 0)
        return;
    if ((device->status & CUPID_GB_TPP1_CLOCK_RUNNING) && device->rtc_base_time > 0) {
        if (host_seconds <= device->rtc_base_time)
            return;
        const uint64_t elapsed = (uint64_t)host_seconds - (uint64_t)device->rtc_base_time;
        const uint64_t minutes = clock_digit(&device->live[3], 8, 60, elapsed);
        const uint64_t hours = clock_digit(&device->live[2], 8, 60, minutes);
        uint8_t hour = device->live[1] & 0x1fu;
        uint8_t weekday = device->live[1] >> 5;
        const uint64_t days = clock_digit(&hour, 5, 24, hours);
        const uint64_t weeks = clock_digit(&weekday, 3, 7, days);
        device->live[1] = (uint8_t)((unsigned)weekday << 5 | hour);
        const uint64_t total_weeks = device->live[0] + weeks;
        device->live[0] = (uint8_t)total_weeks;
        if (total_weeks >= 256)
            device->status |= CUPID_GB_TPP1_CLOCK_OVERFLOW;
    }
    device->rtc_base_time = host_seconds;
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
        if (!(device->status & CUPID_GB_TPP1_CLOCK_RUNNING))
            return device->status;
        CupidGbTpp1 clock = *device;
        cupid_gb_tpp1_advance(&clock, (int64_t)time(NULL));
        return clock.status;
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
    cupid_gb_tpp1_advance(device, (int64_t)time(NULL));
    if (command == RTC_LATCH) {
        memcpy(device->latched, device->live, sizeof(device->latched));
        return;
    }
    bool changed = false;
    const uint8_t previous_status = device->status;
    if (command == RTC_COMMIT) {
        changed = memcmp(device->live, device->latched, sizeof(device->live)) != 0;
        memcpy(device->live, device->latched, sizeof(device->live));
    } else if (command == RTC_CLEAR_OVERFLOW) {
        device->status &= (uint8_t)~CUPID_GB_TPP1_CLOCK_OVERFLOW;
    } else if (command == RTC_STOP) {
        device->status &= (uint8_t)~CUPID_GB_TPP1_CLOCK_RUNNING;
    } else {
        device->status |= CUPID_GB_TPP1_CLOCK_RUNNING;
    }
    if (gb->header.has_battery && (changed || previous_status != device->status))
        ++gb->battery_write_generation;
}

static void motor_command(CupidGb *gb, uint8_t command) {
    CupidGbTpp1 *device = &gb->tpp1;
    const uint8_t strength = command & CUPID_GB_TPP1_MOTOR;
    const bool was_active = (device->status & CUPID_GB_TPP1_MOTOR) != 0;
    device->status = (uint8_t)((device->status & ~CUPID_GB_TPP1_MOTOR) | strength);
    if (was_active != (strength != 0) && gb->rumble_callback)
        gb->rumble_callback(gb, strength != 0, gb->elapsed_cycles, gb->rumble_user_data);
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
}

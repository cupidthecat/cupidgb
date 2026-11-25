/**
 * @file    tama5.c
 * @brief   TAMA5 guest command interface and TC8521 calendar behavior.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "tama5_internal.h"

#include "rtc_internal.h"
#include "state_io.h"

enum {
    CLOCK_SECONDS = 0,
    CLOCK_MINUTES = 2,
    CLOCK_HOURS = 4,
    CLOCK_WEEKDAY = 6,
    CLOCK_DAY = 7,
    CLOCK_MONTH = 9,
    CLOCK_YEAR = 11,
    CLOCK_MODE_24H = 10,
    CLOCK_LEAP_YEAR = 11,
    CLOCK_TIMER_ENABLED = 8,
    CLOCK_ALARM_ENABLED = 4
};

/* The timer and alarm are physically narrower than the free pages. */
static const uint8_t digit_mask[2][CUPID_GB_TAMA5_RTC_DIGITS] = {
    {15, 7, 15, 7, 15, 3, 7, 15, 3, 15, 1, 15, 15}, {0, 0, 15, 7, 15, 3, 7, 15, 3, 0, 1, 3, 0}};

static unsigned decimal_pair(const uint8_t *digits) {
    return (unsigned)digits[0] + (unsigned)digits[1] * 10u;
}

static uint8_t bcd(unsigned decimal) {
    return (uint8_t)((decimal / 10u) << 4u | decimal % 10u);
}

static void set_pair(uint8_t *digits, unsigned value) {
    digits[0] = (uint8_t)(value % 10u);
    digits[1] = (uint8_t)(value / 10u);
}

static unsigned hour(const CupidGbTama5Clock *clock) {
    const uint8_t *digits = &clock->pages[0][CLOCK_HOURS];
    if (clock->pages[1][CLOCK_MODE_24H])
        return decimal_pair(digits) % 24u;
    /* In 12-hour mode the upper tens bit is PM and midnight is 12 AM. */
    return (digits[0] + (digits[1] & 1u) * 10u) % 12u + (digits[1] & 2u) * 6u;
}

static void set_hour(CupidGbTama5Clock *clock, unsigned value) {
    uint8_t *digits = &clock->pages[0][CLOCK_HOURS];
    if (clock->pages[1][CLOCK_MODE_24H]) {
        set_pair(digits, value);
    } else {
        const unsigned half_day = value % 12u;
        set_pair(digits, half_day ? half_day : 12u);
        digits[1] |= (uint8_t)(value < 12u ? 0u : 2u);
    }
}

void cupid_gb_tama5_refresh_clock_cache(CupidGbTama5 *device) {
    const uint8_t *minutes = &device->clock.pages[0][CLOCK_MINUTES];
    device->cached_minute = (uint8_t)((minutes[1] << 4u) | minutes[0]);
    device->cached_hour = bcd(hour(&device->clock));
}

void cupid_gb_tama5_init(CupidGb *gb) {
    CupidGbTama5 *device = &gb->tama5;
    memset(device, 0, sizeof(*device));
    /* Use a valid calendar when there is no battery image. */
    device->clock.pages[0][CLOCK_DAY] = 1;
    device->clock.pages[0][CLOCK_MONTH] = 1;
    device->clock.pages[1][CLOCK_MODE_24H] = 1;
    gb->current_rom_bank = 0;
}

static unsigned month_length(unsigned month, unsigned leap) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1u] + (unsigned)(month == 2u && !leap);
}

static void add_days(CupidGbTama5Clock *clock, uint64_t elapsed) {
    uint8_t *timer = clock->pages[0];
    timer[CLOCK_WEEKDAY] = (uint8_t)((timer[CLOCK_WEEKDAY] + elapsed % 7u) % 7u);
    /* The two-digit year and the separate leap counter both repeat after 100
     * years. Keep battery imports bounded even for very old clock baselines. */
    elapsed %= 36525u;
    unsigned month = decimal_pair(timer + CLOCK_MONTH);
    if (month < 1 || month > 12)
        month = 1;
    unsigned leap = clock->pages[1][CLOCK_LEAP_YEAR];
    unsigned year = decimal_pair(timer + CLOCK_YEAR) % 100u;
    unsigned day = decimal_pair(timer + CLOCK_DAY);
    if (day < 1 || day > month_length(month, leap))
        day = 1;

    for (;;) {
        const unsigned remaining = month_length(month, leap) - day + 1u;
        if (elapsed < remaining)
            break;
        elapsed -= remaining;
        day = 1;
        if (++month == 13u) {
            month = 1;
            year = (year + 1u) % 100u;
            leap = (leap + 1u) & 3u;
        }
    }
    set_pair(timer + CLOCK_DAY, day + (unsigned)elapsed);
    set_pair(timer + CLOCK_MONTH, month);
    set_pair(timer + CLOCK_YEAR, year);
    clock->pages[1][CLOCK_LEAP_YEAR] = (uint8_t)leap;
}

static uint64_t add_minutes(uint8_t *digits, uint64_t elapsed) {
    uint64_t hours = 0;
    /* Direct writes can put invalid digits in the minute counter. The measured
     * circuit carries with a different remainder from decimal normalization. */
    while (elapsed && (digits[0] > 9u || digits[1] > 5u)) {
        --elapsed;
        if (digits[0] == 9u || (digits[0] > 9u && (digits[0] & 1u))) {
            digits[0] = digits[0] == 9u ? 0 : (uint8_t)(17u - digits[0]);
            if (digits[1] == 5u || digits[1] == 7u) {
                digits[1] = (uint8_t)(digits[1] - 5u);
                ++hours;
            } else {
                ++digits[1];
            }
        } else {
            ++digits[0];
        }
    }
    if (elapsed) {
        elapsed += decimal_pair(digits);
        set_pair(digits, (unsigned)(elapsed % 60u));
        hours += elapsed / 60u;
    }
    return hours;
}

void cupid_gb_tama5_advance(CupidGbTama5 *device, int64_t now) {
    CupidGbTama5Clock *clock = &device->clock;
    if (now <= clock->base_time)
        return;
    uint64_t elapsed = (uint64_t)(now - clock->base_time);
    clock->base_time = now;
    if (!(clock->control & CLOCK_TIMER_ENABLED))
        return;
    uint8_t *timer = clock->pages[0];
    elapsed += decimal_pair(timer + CLOCK_SECONDS);
    set_pair(timer + CLOCK_SECONDS, (unsigned)(elapsed % 60u));
    elapsed /= 60u;
    if (!elapsed)
        return;
    uint8_t *minutes = &timer[CLOCK_MINUTES];
    elapsed = add_minutes(minutes, elapsed);
    device->cached_minute = (uint8_t)((minutes[1] << 4u) | minutes[0]);
    if (!elapsed)
        return;
    elapsed += hour(clock);
    set_hour(clock, (unsigned)(elapsed % 24u));
    device->cached_hour = bcd((unsigned)(elapsed % 24u));
    if (elapsed >= 24u)
        add_days(clock, elapsed / 24u);
}

bool cupid_gb_tama5_read(const CupidGb *gb, uint16_t address, uint8_t *nibble) {
    const CupidGbTama5 *device = &gb->tama5;
    if (!device->ready || address != 0xa000u)
        return false;
    switch (device->selected) {
        case 0xa:
            *nibble = 1;
            return true;
        case 0xc:
            *nibble = device->response & 15u;
            return true;
        case 0xd:
            *nibble = device->response >> 4u;
            return true;
        default:
            return false;
    }
}

static void rtc_command(CupidGb *gb, unsigned command) {
    CupidGbTama5 *device = &gb->tama5;
    CupidGbTama5Clock *clock = &device->clock;
    const unsigned page = (command >> 1u) & 3u;
    const unsigned digit = device->parameter & 15u;
    if (command & 1u) {
        if (digit < CUPID_GB_TAMA5_RTC_DIGITS)
            device->response = clock->pages[page][digit];
        else
            device->response = digit == 13u ? clock->control | page : 0u;
        return;
    }
    /* The microcontroller filters direct writes to the shared control ports. */
    if (digit >= CUPID_GB_TAMA5_RTC_DIGITS)
        return;
    const unsigned mask = page < 2 ? digit_mask[page][digit] : 15u;
    const uint8_t value = (uint8_t)((device->parameter >> 4u) & mask);
    if (clock->pages[page][digit] != value) {
        clock->pages[page][digit] = value;
        ++gb->battery_write_generation;
    }
}

static void microcontroller_command(CupidGb *gb, unsigned command) {
    CupidGbTama5 *device = &gb->tama5;
    CupidGbTama5Clock *clock = &device->clock;
    uint8_t *timer = clock->pages[0];
    const unsigned decimal = (device->parameter >> 4u) * 10u + (device->parameter & 15u);
    switch (command) {
        case 0x40:
            clock->control &= (uint8_t)~CLOCK_TIMER_ENABLED;
            break;
        case 0x41:
            clock->control |= CLOCK_TIMER_ENABLED;
            set_pair(timer + CLOCK_SECONDS, 0);
            gb->rtc_cycles = 0;
            break;
        case 0x44:
            if (decimal >= 60)
                return;
            set_pair(timer + CLOCK_MINUTES, decimal);
            device->cached_minute = bcd(decimal);
            break;
        case 0x45:
            if (decimal >= 24)
                return;
            set_hour(clock, decimal);
            device->cached_hour = bcd(decimal);
            break;
        case 0x46:
            device->response = device->cached_minute;
            return;
        case 0x47:
            device->response = device->cached_hour;
            return;
        case 0x50:
            clock->control &= (uint8_t)~CLOCK_ALARM_ENABLED;
            break;
        case 0x51:
            clock->control |= CLOCK_ALARM_ENABLED;
            break;
        default:
            return;
    }
    ++gb->battery_write_generation;
}

static void commit_command(CupidGb *gb, unsigned low) {
    CupidGbTama5 *device = &gb->tama5;
    const unsigned command = ((unsigned)device->address_high << 4u) | low;
    if (command < 0x40u) {
        uint8_t *cell = &gb->cartridge_ram[command & 31u];
        if (command & 32u) {
            device->response = *cell;
        } else if (*cell != device->parameter) {
            *cell = device->parameter;
            ++gb->battery_write_generation;
        }
    } else if ((command >= 0x40u && command <= 0x51u) || (command >= 0x80u && command <= 0x87u)) {
        if (gb->rtc_policy.mode == CUPID_GB_RTC_SYNC_TO_HOST)
            cupid_gb_tama5_advance(device, cupid_gb_rtc_now(gb));
        if (command >= 0x80u)
            rtc_command(gb, low);
        else
            microcontroller_command(gb, command);
    }
}

void cupid_gb_tama5_write(CupidGb *gb, uint16_t address, uint8_t value) {
    CupidGbTama5 *device = &gb->tama5;
    const uint8_t nibble = value & 15u;
    if (address == 0xa001u) {
        device->selected = nibble;
        if (nibble == 0xau && !device->ready) {
            cupid_gb_tama5_refresh_clock_cache(device);
            device->ready = true;
        }
        return;
    }
    if (address != 0xa000u || !device->ready)
        return;
    switch (device->selected) {
        case 0:
            gb->current_rom_bank = (gb->current_rom_bank & 0x10u) | nibble;
            break;
        case 1:
            gb->current_rom_bank = (gb->current_rom_bank & 0x0fu) | ((nibble & 1u) << 4u);
            break;
        case 4:
            device->parameter = (uint8_t)((device->parameter & 0xf0u) | nibble);
            break;
        case 5:
            device->parameter = (uint8_t)((device->parameter & 15u) | (nibble << 4u));
            break;
        case 6:
            device->address_high = nibble;
            break;
        case 7:
            commit_command(gb, nibble);
            break;
    }
}

bool cupid_gb_tama5_clock_valid(const CupidGbTama5Clock *clock) {
    if (clock->base_time < 0 || (clock->control & ~(CLOCK_TIMER_ENABLED | CLOCK_ALARM_ENABLED)))
        return false;
    for (unsigned page = 0; page < CUPID_GB_TAMA5_RTC_PAGES; ++page) {
        for (unsigned digit = 0; digit < CUPID_GB_TAMA5_RTC_DIGITS; ++digit) {
            const unsigned mask = page < 2 ? digit_mask[page][digit] : 15u;
            if (clock->pages[page][digit] & ~mask)
                return false;
        }
    }
    return true;
}

bool cupid_gb_tama5_state_valid(const CupidGb *gb) {
    const CupidGbTama5 *device = &gb->tama5;
    return gb->current_rom_bank < 32u && !gb->current_ram_bank && !gb->ram_enabled &&
           device->selected <= 15u && device->address_high <= 15u &&
           device->cached_minute <= 0x7fu && device->cached_hour <= 0x23u &&
           cupid_gb_tama5_clock_valid(&device->clock);
}

void cupid_gb_tama5_clock_fields(StateIo *io, CupidGbTama5Clock *clock) {
    signed_quad_word(io, &clock->base_time);
    byte(io, &clock->control);
    for (unsigned page = 0; page < CUPID_GB_TAMA5_RTC_PAGES; ++page)
        bytes(io, clock->pages[page], CUPID_GB_TAMA5_RTC_DIGITS);
}

void cupid_gb_tama5_state(StateIo *io, CupidGb *gb) {
    CupidGbTama5 *device = &gb->tama5;
    cupid_gb_tama5_clock_fields(io, &device->clock);
    byte(io, &device->selected);
    byte(io, &device->parameter);
    byte(io, &device->address_high);
    byte(io, &device->response);
    byte(io, &device->cached_minute);
    byte(io, &device->cached_hour);
    boolean(io, &device->ready);
    if (!cupid_gb_tama5_state_valid(gb))
        io->invalid = true;
}

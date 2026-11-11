#include "cupid/gb/huc3.h"

#include <time.h>

enum { MINUTES_PER_DAY = 1440, SECONDS_PER_MINUTE = 60 };

void cupid_gb_huc3_advance(CupidGbHuc3 *device, int64_t host_seconds) {
    if (host_seconds <= 0)
        return;
    if (device->rtc_base_time <= 0) {
        device->rtc_base_time = host_seconds;
        return;
    }
    const int64_t elapsed =
        host_seconds / SECONDS_PER_MINUTE - device->rtc_base_time / SECONDS_PER_MINUTE;
    if (elapsed <= 0)
        return;

    const int64_t distance = elapsed * SECONDS_PER_MINUTE;
    device->rtc_base_time = distance <= INT64_MAX - device->rtc_base_time
                                ? device->rtc_base_time + distance
                                : host_seconds;
    /* Writes can leave the minute counter above its normal modulus. It must
     * wrap through zero before it can carry into the day counter again. */
    const uint32_t next_carry =
        (device->minutes < MINUTES_PER_DAY ? MINUTES_PER_DAY : 65536u + MINUTES_PER_DAY) -
        device->minutes;
    if ((uint64_t)elapsed < next_carry) {
        device->minutes = (uint16_t)(device->minutes + (uint64_t)elapsed);
        return;
    }
    const uint64_t remaining = (uint64_t)elapsed - next_carry;
    device->days = (uint16_t)(device->days + 1u + remaining / MINUTES_PER_DAY);
    device->minutes = (uint16_t)(remaining % MINUTES_PER_DAY);
}

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
                cupid_gb_huc3_advance(device, (int64_t)time(NULL));
                gb->huc3_value = (uint8_t)((*reg.value >> reg.shift) & 0x0fu);
            }
            break;
        }
        case 2:
        case 3:
            if (device->index < 7u)
                cupid_gb_huc3_advance(device, (int64_t)time(NULL));
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
        return gb->huc3.ir_detected ? 1u : 0u;
    return 1u;
}

void cupid_gb_huc3_write_window(CupidGb *gb, uint8_t value) {
    if (gb->huc3_mode == CUPID_GB_HUC3_COMMAND)
        command(gb, value);
    else if (gb->huc3_mode == CUPID_GB_HUC3_INFRARED)
        gb->huc3.ir_output = (value & 1u) != 0;
}

static uint16_t charge(uint16_t current, unsigned clocks, bool on, unsigned limit) {
    if (!on)
        return current > clocks ? (uint16_t)(current - clocks) : 0;
    const unsigned next = current + clocks;
    return (uint16_t)(next < limit ? next : limit);
}

void cupid_gb_huc3_tick_infrared(CupidGb *gb, unsigned dots) {
    enum { WARMUP_START = 32000, DECAY_START = 23000, NOISE_RANGE = 2048 };

    CupidGbHuc3 *device = &gb->huc3;
    const unsigned clocks = dots * 2u;
    const uint8_t port = gb->cgb_mode ? gb->io_registers[0x56] : 0;
    const bool sensing = gb->huc3_mode == CUPID_GB_HUC3_INFRARED || (port & 0xc0u) == 0xc0u;
    if (!sensing && !device->ir_warmup && !device->ir_decay)
        return;
    device->ir_warmup = charge(device->ir_warmup, clocks, sensing, CUPID_GB_HUC3_IR_WARMUP_LIMIT);
    bool detected = sensing && (gb->huc3_ir_input || device->ir_output || (port & 1u)) &&
                    device->ir_warmup >= WARMUP_START;
    /* Near both thresholds, the photodiode's output fluctuates. Keep the
     * transition repeatable by deriving its noise from emulated clock phase. */
    const unsigned noise = (unsigned)((gb->elapsed_cycles * 105067u) >> 7) % NOISE_RANGE;
    if (device->ir_warmup < CUPID_GB_HUC3_IR_WARMUP_LIMIT && detected)
        detected = noise < (unsigned)device->ir_warmup - WARMUP_START;
    device->ir_decay = charge(device->ir_decay, clocks, detected, CUPID_GB_HUC3_IR_DECAY_LIMIT);
    if (device->ir_decay >= DECAY_START)
        detected = detected && device->ir_decay < DECAY_START + NOISE_RANGE &&
                   noise > (unsigned)device->ir_decay - DECAY_START;
    device->ir_detected = detected;
}

uint32_t cupid_gb_huc3_seconds_to_alarm(const CupidGb *gb, int64_t host_seconds) {
    enum { DAY_MASK = 0x1fff, ALARM_SET = 0x2000 };

    if (!gb || !gb->loaded || gb->header.mbc_type != CUPID_GB_HUC3 || host_seconds < 0 ||
        !gb->huc3.alarm_enabled || !(gb->huc3.alarm_days & ALARM_SET))
        return 0;
    CupidGbHuc3 clock = gb->huc3;
    cupid_gb_huc3_advance(&clock, host_seconds);
    const uint32_t now =
        (((unsigned)clock.days & DAY_MASK) * MINUTES_PER_DAY + clock.minutes) * SECONDS_PER_MINUTE +
        (uint32_t)(host_seconds % SECONDS_PER_MINUTE);
    const uint32_t alarm =
        (((unsigned)clock.alarm_days & DAY_MASK) * MINUTES_PER_DAY + clock.alarm_minutes) *
        SECONDS_PER_MINUTE;
    return alarm > now ? alarm - now : 0;
}

void cupid_gb_huc3_set_infrared_input(CupidGb *gb, bool active) {
    if (gb)
        gb->huc3_ir_input = active;
}

bool cupid_gb_huc3_infrared_output(const CupidGb *gb) {
    return gb && gb->loaded && gb->header.mbc_type == CUPID_GB_HUC3 && gb->huc3.ir_output;
}

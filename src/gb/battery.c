#include "cupid/gb/battery.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/tpp1.h"

#include <limits.h>
#include <string.h>
#include <time.h>

enum { TPP1_TRAILER_SIZE = 20 };

static const uint8_t tpp1_trailer_header[] = {'T', 'P', 'P', '1', 1, 0};

static bool clock_present(const CupidGb *gb) {
    return gb->header.has_timer && gb->header.mbc_type == CUPID_GB_MBC3;
}

static bool valid_cartridge(const CupidGb *gb) {
    return gb && gb->loaded && gb->rom && gb->cartridge_ram_size <= CUPID_GB_MAX_RAM_SIZE;
}

/* Values above the ordinary modulus are observable clock register states. They
 * wrap at eight bits before reaching the next carry boundary; do not normalize
 * them merely because a battery image is being read or written.
 */
static uint64_t advance_digit(uint8_t *digit, unsigned modulus, uint64_t ticks) {
    uint64_t until_carry = *digit < modulus ? modulus - *digit : 256u + modulus - *digit;
    if (ticks < until_carry) {
        *digit = (uint8_t)((uint64_t)*digit + ticks);
        return 0;
    }
    uint64_t remaining = ticks - until_carry;
    *digit = (uint8_t)(remaining % modulus);
    return remaining / modulus + 1;
}

void cupid_gb_rtc_advance(CupidGbRtc *rtc, int64_t host_seconds) {
    if (!rtc)
        return;
    if (rtc->base_time && host_seconds > rtc->base_time && !(rtc->dh & 0x40u)) {
        /* Unsigned subtraction also represents positive distances spanning the
         * signed timestamp boundary. Carry counts shrink before day arithmetic.
         */
        uint64_t elapsed = (uint64_t)host_seconds - (uint64_t)rtc->base_time;
        uint64_t minutes = advance_digit(&rtc->s, 60, elapsed);
        uint64_t hours = advance_digit(&rtc->m, 60, minutes);
        uint64_t days = advance_digit(&rtc->h, 24, hours);
        uint64_t date = (uint64_t)rtc->dl + ((uint64_t)(rtc->dh & 1u) << 8) + days;
        rtc->dl = (uint8_t)date;
        rtc->dh = (uint8_t)((rtc->dh & 0xfeu) | ((date >> 8) & 1u));
        if (date >= 512)
            rtc->dh |= 0x80u;
    }
    rtc->base_time = host_seconds;
}

size_t cupid_gb_battery_size(const CupidGb *gb) {
    if (!valid_cartridge(gb) || !gb->header.has_battery)
        return 0;
    if (gb->header.mbc_type == CUPID_GB_TPP1)
        return gb->cartridge_ram_size + TPP1_TRAILER_SIZE;
    return gb->cartridge_ram_size + (clock_present(gb) ? 48u : 0u);
}

static bool overlaps(const void *left, size_t left_size, const void *right, size_t right_size) {
    if (!left_size || !right_size)
        return false;
    uintptr_t a = (uintptr_t)left, b = (uintptr_t)right;
    return a >= b ? a - b < right_size : b - a < left_size;
}

static void write_integer(uint8_t *destination, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        destination[i] = (uint8_t)(value >> (i * 8));
}

static uint64_t read_integer(const uint8_t *source, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
        value |= (uint64_t)source[i] << (i * 8);
    return value;
}

static void save_tpp1_clock(const CupidGb *gb, uint8_t *trailer, int64_t host_seconds) {
    CupidGbTpp1 clock = gb->tpp1;
    cupid_gb_tpp1_advance(&clock, host_seconds);
    memcpy(trailer, tpp1_trailer_header, sizeof(tpp1_trailer_header));
    trailer[6] = clock.status;
    trailer[7] = 0;
    write_integer(trailer + 8, (uint64_t)clock.rtc_base_time, 8);
    memcpy(trailer + 16, clock.live, sizeof(clock.live));
}

static CupidGbBatteryResult load_tpp1_battery(CupidGb *gb, const uint8_t *data, size_t size,
                                              int64_t host_seconds) {
    const size_t ram_size = gb->cartridge_ram_size;
    CupidGbTpp1 clock = gb->tpp1;
    clock.rtc_base_time = host_seconds > 0 ? host_seconds : 0;
    if (size == ram_size + TPP1_TRAILER_SIZE) {
        const uint8_t *trailer = data + ram_size;
        if (memcmp(trailer, tpp1_trailer_header, sizeof(tpp1_trailer_header)) != 0 ||
            trailer[6] > 0x0fu)
            return CUPID_GB_BATTERY_INVALID_RTC;
        clock.status = trailer[6];
        memcpy(clock.live, trailer + 16, sizeof(clock.live));
        const uint64_t saved_time = read_integer(trailer + 8, 8);
        if (host_seconds >= 0 && saved_time <= (uint64_t)host_seconds) {
            clock.rtc_base_time = (int64_t)saved_time;
            cupid_gb_tpp1_advance(&clock, host_seconds);
        }
    } else if (size != ram_size) {
        return CUPID_GB_BATTERY_INVALID_SIZE;
    }
    if (ram_size)
        memmove(gb->cartridge_ram, data, ram_size);
    gb->tpp1 = clock;
    return CUPID_GB_BATTERY_OK;
}

CupidGbBatteryResult cupid_gb_save_battery_at(const CupidGb *gb, uint8_t *data, size_t capacity,
                                              size_t *written, int64_t host_seconds) {
    if (written)
        *written = 0;
    if (!valid_cartridge(gb))
        return CUPID_GB_BATTERY_INVALID_ARGUMENT;
    const size_t size = cupid_gb_battery_size(gb);
    if (!size)
        return CUPID_GB_BATTERY_NO_DATA;
    if (capacity < size)
        return CUPID_GB_BATTERY_BUFFER_TOO_SMALL;
    if (!data || overlaps(data, size, gb, sizeof(*gb)) ||
        overlaps(data, size, gb->rom, gb->rom_size))
        return CUPID_GB_BATTERY_INVALID_ARGUMENT;
    CupidGbRtc rtc = gb->rtc;
    if (clock_present(gb))
        cupid_gb_rtc_advance(&rtc, host_seconds);
    memcpy(data, gb->cartridge_ram, gb->cartridge_ram_size);
    if (clock_present(gb)) {
        const uint8_t registers[] = {rtc.s,  rtc.m,  rtc.h,  rtc.dl,  rtc.dh,
                                     rtc.ls, rtc.lm, rtc.lh, rtc.ldl, rtc.ldh};
        uint8_t *trailer = data + gb->cartridge_ram_size;
        for (unsigned i = 0; i < sizeof(registers); ++i)
            write_integer(trailer + i * 4, registers[i], 4);
        write_integer(trailer + 40, (uint64_t)host_seconds, 8);
    } else if (gb->header.mbc_type == CUPID_GB_TPP1) {
        save_tpp1_clock(gb, data + gb->cartridge_ram_size, host_seconds);
    }
    if (written)
        *written = size;
    return CUPID_GB_BATTERY_OK;
}

CupidGbBatteryResult cupid_gb_save_battery(const CupidGb *gb, uint8_t *data, size_t capacity,
                                           size_t *written) {
    return cupid_gb_save_battery_at(gb, data, capacity, written, (int64_t)time(NULL));
}

CupidGbBatteryResult cupid_gb_load_battery_at(CupidGb *gb, const uint8_t *data, size_t size,
                                              int64_t host_seconds) {
    if (!valid_cartridge(gb) || (!data && size))
        return CUPID_GB_BATTERY_INVALID_ARGUMENT;
    const size_t expected = cupid_gb_battery_size(gb);
    if (!expected)
        return CUPID_GB_BATTERY_NO_DATA;
    if (gb->header.mbc_type == CUPID_GB_TPP1)
        return load_tpp1_battery(gb, data, size, host_seconds);
    const size_t ram_size = gb->cartridge_ram_size;
    const bool has_clock = clock_present(gb);
    const bool legacy_mbc7 = gb->header.mbc_type == CUPID_GB_MBC7 && size > ram_size &&
                             size == cupid_gb_ram_size_from_code(gb->header.ram_size_code);
    if (size != ram_size && !legacy_mbc7 &&
        (!has_clock || (size != ram_size + 44 && size != ram_size + 48)))
        return CUPID_GB_BATTERY_INVALID_SIZE;
    CupidGbRtc rtc = gb->rtc;
    if (has_clock) {
        bool reset_clock = size == ram_size;
        uint64_t saved_time = 0;
        if (!reset_clock) {
            const uint8_t *trailer = data + ram_size;
            uint8_t registers[10];
            for (unsigned i = 0; i < sizeof(registers); ++i) {
                uint64_t value = read_integer(trailer + i * 4, 4);
                if (value > UINT8_MAX)
                    return CUPID_GB_BATTERY_INVALID_RTC;
                registers[i] = (uint8_t)value;
            }
            rtc.s = registers[0];
            rtc.m = registers[1];
            rtc.h = registers[2];
            rtc.dl = registers[3];
            rtc.dh = registers[4];
            rtc.ls = registers[5];
            rtc.lm = registers[6];
            rtc.lh = registers[7];
            rtc.ldl = registers[8];
            rtc.ldh = registers[9];
            saved_time = read_integer(trailer + 40, size == ram_size + 48 ? 8 : 4);
            /* Earlier timestamps do not represent a valid clock initialization
             * for the cartridge generation. Future dates also need reinitialization.
             */
            reset_clock = saved_time < UINT64_C(852076800) || saved_time > INT64_MAX ||
                          host_seconds < 0 || saved_time > (uint64_t)host_seconds;
        }
        if (reset_clock) {
            rtc.base_time = host_seconds;
            rtc.dh |= 0x80u;
        } else {
            rtc.base_time = (int64_t)saved_time;
            cupid_gb_rtc_advance(&rtc, host_seconds);
        }
    }
    /* Decode and validate the complete trailer before either persistent region
     * changes. memmove permits a caller-owned input window inside cartridge RAM.
     */
    if (ram_size)
        memmove(gb->cartridge_ram, data, ram_size);
    if (has_clock)
        gb->rtc = rtc;
    return CUPID_GB_BATTERY_OK;
}

CupidGbBatteryResult cupid_gb_load_battery(CupidGb *gb, const uint8_t *data, size_t size) {
    return cupid_gb_load_battery_at(gb, data, size, (int64_t)time(NULL));
}

const char *cupid_gb_battery_result_name(CupidGbBatteryResult result) {
    switch (result) {
        case CUPID_GB_BATTERY_OK:
            return "Battery operation succeeded";
        case CUPID_GB_BATTERY_NO_DATA:
            return "Cartridge has no persistent battery data";
        case CUPID_GB_BATTERY_INVALID_ARGUMENT:
            return "Invalid battery buffer or cartridge";
        case CUPID_GB_BATTERY_INVALID_SIZE:
            return "Battery file size does not match the cartridge";
        case CUPID_GB_BATTERY_INVALID_RTC:
            return "Battery clock register encoding is invalid";
        case CUPID_GB_BATTERY_BUFFER_TOO_SMALL:
            return "Battery output buffer is too small";
        default:
            return "Unknown battery result";
    }
}

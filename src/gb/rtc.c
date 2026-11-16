#include "rtc_internal.h"

#include "cupid/gb/battery.h"
#include "cupid/gb/huc3.h"
#include "cupid/gb/tpp1.h"

#include <math.h>
#include <time.h>

enum { SECONDS_PER_MINUTE = 60, MINUTES_PER_DAY = 1440, SECONDS_PER_DAY = 86400 };

/* Native timestamps retain their unsigned history in signed storage. Avoid an
 * implementation-defined conversion when the high bit is set. */
static int64_t store_timestamp(uint64_t value) {
    if (value <= INT64_MAX)
        return (int64_t)value;
    return INT64_MIN + (int64_t)(value - (UINT64_C(1) << 63));
}

/* Return the first positive n for which (baseline + n * step) mod 2^64
 * reaches target. step has exactly shift trailing zero bits. Most histories
 * need one division; only a target in the final step seconds needs the orbit.
 * Zero means that the orbit has no stopping timestamp. */
static uint64_t first_reaching_step(uint64_t baseline, uint64_t target, unsigned step,
                                    unsigned shift) {
    const uint64_t next = baseline + step;
    if (next >= target)
        return 1;
    const uint64_t residue_mask = (UINT64_C(1) << shift) - 1;
    const uint64_t largest = (UINT64_MAX & ~residue_mask) | (baseline & residue_mask);
    if (target > largest)
        return 0;
    if (target <= UINT64_MAX - step)
        return 2 + (target - next - 1) / step;

    /* The odd part of the step has an inverse modulo the orbit length.
     * Each Newton iteration doubles the number of correct inverse bits. The
     * near-top interval has at most 675 candidates for days, 15 for minutes. */
    const uint64_t modulus = UINT64_C(1) << (64 - shift);
    const uint64_t mask = modulus - 1;
    const uint64_t odd_step = step >> shift;
    uint64_t inverse = 1;
    for (unsigned iteration = 0; iteration < 6; ++iteration)
        inverse *= 2 - odd_step * inverse;
    uint64_t first = modulus;
    const unsigned count = (unsigned)((largest - target) >> shift) + 1;
    for (unsigned index = 0; index < count; ++index) {
        const uint64_t stop = largest - ((uint64_t)index << shift);
        uint64_t distance = (((stop >> shift) - (baseline >> shift)) * inverse) & mask;
        if (!distance)
            distance = modulus;
        if (distance < first)
            first = distance;
    }
    return first;
}

/* Invalid writable digits wrap at their storage width before reaching the
 * next normal carry. A direct modulo normalization would lose that delay. */
static uint64_t advance_digit(unsigned *digit, unsigned width, unsigned boundary, uint64_t ticks) {
    const unsigned capacity = 1u << width;
    const unsigned first = boundary + (*digit < boundary ? 0u : capacity) - *digit;
    if (ticks < first) {
        *digit = (unsigned)((*digit + ticks) & (capacity - 1u));
        return 0;
    }
    ticks -= first;
    *digit = (unsigned)(ticks % boundary);
    return 1 + ticks / boundary;
}

static uint64_t advance_byte(uint8_t *digit, unsigned boundary, uint64_t ticks) {
    unsigned value = *digit;
    const uint64_t carry = advance_digit(&value, 8, boundary, ticks);
    *digit = (uint8_t)value;
    return carry;
}

static void mbc3_days(CupidGbRtc *rtc, uint64_t days) {
    const uint64_t total = rtc->dl + ((uint64_t)(rtc->dh & 1u) << 8) + days;
    rtc->dl = (uint8_t)total;
    rtc->dh = (uint8_t)((rtc->dh & 0xfeu) | ((total >> 8) & 1u));
    if (total >= 512)
        rtc->dh |= 0x80u;
}

static void tpp1_days(CupidGbTpp1 *clock, uint64_t days) {
    unsigned weekday = clock->live[1] >> 5;
    const uint64_t weeks = advance_digit(&weekday, 3, 7, days);
    const uint64_t total = clock->live[0] + weeks;
    clock->live[1] = (uint8_t)((clock->live[1] & 31u) | (weekday << 5));
    clock->live[0] = (uint8_t)total;
    if (total >= 256)
        clock->status |= CUPID_GB_TPP1_CLOCK_OVERFLOW;
}

void cupid_gb_rtc_advance(CupidGbRtc *rtc, int64_t host_seconds) {
    if (!rtc || (rtc->dh & 0x40u))
        return;
    const uint64_t now = (uint64_t)host_seconds;
    const uint64_t baseline = (uint64_t)rtc->base_time;
    const uint64_t stop = first_reaching_step(baseline, now, SECONDS_PER_DAY, 7);
    /* Some accepted raw timestamp histories have no stopping day in their
     * unsigned orbit. Keep the complete clock unchanged rather than hang.
     * The scheduler still consumes the delivery interval before reaching here. */
    if (!stop)
        return;
    const uint64_t days = stop - 1;
    const uint64_t after_days = baseline + days * SECONDS_PER_DAY;
    const uint64_t seconds = now > after_days ? now - after_days : 0;
    mbc3_days(rtc, days);
    const uint64_t minutes = advance_byte(&rtc->s, 60, seconds);
    const uint64_t hours = advance_byte(&rtc->m, 60, minutes);
    mbc3_days(rtc, advance_byte(&rtc->h, 24, hours));
    rtc->base_time = store_timestamp(after_days + seconds);
}

void cupid_gb_tpp1_advance(CupidGbTpp1 *clock, int64_t host_seconds) {
    if (!clock || !(clock->status & CUPID_GB_TPP1_CLOCK_RUNNING))
        return;
    const uint64_t now = (uint64_t)host_seconds;
    const uint64_t baseline = (uint64_t)clock->rtc_base_time;
    const uint64_t stop = first_reaching_step(baseline, now, SECONDS_PER_DAY, 7);
    if (!stop)
        return;
    const uint64_t days = stop - 1;
    const uint64_t after_days = baseline + days * SECONDS_PER_DAY;
    const uint64_t seconds = now > after_days ? now - after_days : 0;
    tpp1_days(clock, days);
    const uint64_t minutes = advance_byte(&clock->live[3], 60, seconds);
    const uint64_t hours = advance_byte(&clock->live[2], 60, minutes);
    unsigned hour = clock->live[1] & 31u;
    const uint64_t residual_days = advance_digit(&hour, 5, 24, hours);
    clock->live[1] = (uint8_t)((clock->live[1] & 0xe0u) | hour);
    tpp1_days(clock, residual_days);
    clock->rtc_base_time = store_timestamp(after_days + seconds);
}

void cupid_gb_huc3_advance(CupidGbHuc3 *clock, int64_t host_seconds) {
    if (!clock)
        return;
    const uint64_t now = (uint64_t)host_seconds;
    const uint64_t baseline = (uint64_t)clock->rtc_base_time;
    const uint64_t boundary = now - now % SECONDS_PER_MINUTE;
    if (baseline >= boundary)
        return;
    const uint64_t minutes = first_reaching_step(baseline, boundary, SECONDS_PER_MINUTE, 2);
    unsigned digit = clock->minutes;
    const uint64_t days = advance_digit(&digit, 16, MINUTES_PER_DAY, minutes);
    clock->minutes = (uint16_t)digit;
    clock->days = (uint16_t)(clock->days + days);
    /* Preserve the initial second remainder. Crossing a host minute can move
     * this baseline ahead of now, which is observable in battery exports. */
    clock->rtc_base_time = store_timestamp(baseline + minutes * SECONDS_PER_MINUTE);
}

static int64_t *active_baseline(CupidGb *gb) {
    switch (gb->header.mbc_type) {
        case CUPID_GB_MBC3:
            return &gb->rtc.base_time;
        case CUPID_GB_HUC3:
            return &gb->huc3.rtc_base_time;
        case CUPID_GB_TPP1:
            return &gb->tpp1.rtc_base_time;
        default:
            return NULL;
    }
}

int64_t cupid_gb_rtc_now(const CupidGb *gb) {
    const int64_t now = gb && gb->rtc_policy.time_source
                            ? gb->rtc_policy.time_source(gb->rtc_policy.time_user_data)
                            : (int64_t)time(NULL);
    return now < 0 ? 0 : now;
}

void cupid_gb_rtc_bind_cartridge(CupidGb *gb) {
    if (!gb)
        return;
    int64_t *baseline = active_baseline(gb);
    if (baseline)
        *baseline = cupid_gb_rtc_now(gb);
}

bool cupid_gb_set_rtc_mode(CupidGb *gb, CupidGbRtcMode mode) {
    if (!gb || (mode != CUPID_GB_RTC_SYNC_TO_HOST && mode != CUPID_GB_RTC_ACCURATE))
        return false;
    if (gb->rtc_policy.mode != mode) {
        gb->rtc_policy.mode = mode;
        gb->rtc_cycles = 0;
        cupid_gb_rtc_bind_cartridge(gb);
    }
    return true;
}

CupidGbRtcMode cupid_gb_get_rtc_mode(const CupidGb *gb) {
    return gb ? gb->rtc_policy.mode : CUPID_GB_RTC_SYNC_TO_HOST;
}

bool cupid_gb_set_rtc_multiplier(CupidGb *gb, double multiplier) {
    if (!gb || !isfinite(multiplier) || multiplier < 0)
        return false;
    const double scaled = 2.0 * cupid_gb_base_clock_hz(gb) * multiplier;
    if (!isfinite(scaled) || scaled >= 4294967296.0)
        return false;
    gb->rtc_policy.second_length = multiplier == 1.0 ? 0 : (uint32_t)scaled;
    return true;
}

void cupid_gb_set_rtc_time_source(CupidGb *gb, CupidGbRtcTimeSource source, void *user_data) {
    if (!gb)
        return;
    gb->rtc_policy.time_source = source;
    gb->rtc_policy.time_user_data = source ? user_data : NULL;
}

void cupid_gb_rtc_update_at(CupidGb *gb, int64_t now) {
    switch (gb->header.mbc_type) {
        case CUPID_GB_MBC3:
            cupid_gb_rtc_advance(&gb->rtc, now);
            break;
        case CUPID_GB_HUC3:
            cupid_gb_huc3_advance(&gb->huc3, now);
            break;
        case CUPID_GB_TPP1:
            cupid_gb_tpp1_advance(&gb->tpp1, now);
            break;
        default:
            break;
    }
}

void cupid_gb_rtc_tick(CupidGb *gb, uint32_t half_cycles) {
    if (!gb || !half_cycles)
        return;
    const bool minute_clock = gb->header.mbc_type == CUPID_GB_HUC3;
    const bool second_clock = gb->header.has_timer && (gb->header.mbc_type == CUPID_GB_MBC3 ||
                                                       gb->header.mbc_type == CUPID_GB_TPP1);
    if (!minute_clock && !second_clock)
        return;
    if (gb->rtc_policy.mode == CUPID_GB_RTC_ACCURATE && !minute_clock && (gb->rtc.dh & 0x40u))
        return;
    gb->rtc_cycles += half_cycles;
    const uint32_t frequency = cupid_gb_base_clock_hz(gb);
    const uint32_t interval = gb->rtc_policy.mode == CUPID_GB_RTC_SYNC_TO_HOST ? frequency / 16u
                              : gb->rtc_policy.second_length ? gb->rtc_policy.second_length
                                                             : frequency * 2u;
    if (gb->rtc_cycles < interval)
        return;
    /* One logical delivery consumes at most one interval, even with accumulated
     * debt or a newly shortened period. Fractional phase intentionally wraps. */
    gb->rtc_cycles -= interval;
    const int64_t now = gb->rtc_policy.mode == CUPID_GB_RTC_SYNC_TO_HOST
                            ? cupid_gb_rtc_now(gb)
                            : store_timestamp((uint64_t)*active_baseline(gb) + 1);
    cupid_gb_rtc_update_at(gb, now);
}

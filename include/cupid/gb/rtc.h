#ifndef CUPID_GB_RTC_H
#define CUPID_GB_RTC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CupidGb;

typedef enum CupidGbRtcMode {
    CUPID_GB_RTC_SYNC_TO_HOST = 0,
    CUPID_GB_RTC_ACCURATE = 1
} CupidGbRtcMode;

/* Unix seconds. The observer must not mutate or reenter the core. */
typedef int64_t (*CupidGbRtcTimeSource)(void *user_data);

/* Host preferences and registration, excluded from native execution states.
 * A zeroed record selects host synchronization and the system time source. */
typedef struct CupidGbRtcPolicy {
    CupidGbRtcMode mode;
    uint32_t second_length; /* Half-base-clock units; zero follows the model's default. */
    CupidGbRtcTimeSource time_source;
    void *time_user_data;
} CupidGbRtcPolicy;

/* Changing mode clears fractional history and rebases the active clock without
 * advancing its counters. Setting the current mode changes nothing. Invalid
 * modes and NULL return false without mutation; the NULL getter returns host mode. */
bool cupid_gb_set_rtc_mode(struct CupidGb *gb, CupidGbRtcMode mode);
CupidGbRtcMode cupid_gb_get_rtc_mode(const struct CupidGb *gb);

/* Scale the duration of an Accurate second: 0.5 doubles its rate; 2 halves it.
 * One, zero, and values truncating to zero select the default model period.
 * Finite nonnegative values are accepted when their scaled, truncated period
 * fits uint32_t. Invalid inputs and NULL return false without mutation.
 * Changing the period preserves phase, counters, baseline, and host mode. */
bool cupid_gb_set_rtc_multiplier(struct CupidGb *gb, double multiplier);

/* Register host time without polling or changing clock history. NULL source
 * restores the system clock and clears the context. Negative observations clamp
 * to zero. Registration survives init, ROM loading, boot entry, and native loads;
 * cleanup clears it. A NULL instance is a no-op. Native decode never calls it. */
void cupid_gb_set_rtc_time_source(struct CupidGb *gb, CupidGbRtcTimeSource source, void *user_data);

#ifdef __cplusplus
}
#endif

#endif

#ifndef CUPID_GB_BATTERY_H
#define CUPID_GB_BATTERY_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CupidGbBatteryResult {
    CUPID_GB_BATTERY_OK = 0,
    CUPID_GB_BATTERY_NO_DATA,
    CUPID_GB_BATTERY_INVALID_ARGUMENT,
    CUPID_GB_BATTERY_INVALID_SIZE,
    CUPID_GB_BATTERY_INVALID_RTC,
    CUPID_GB_BATTERY_BUFFER_TOO_SMALL
} CupidGbBatteryResult;

/* RAM is stored verbatim. Timer-equipped MBC3 cartridges append 48 bytes: ten
 * little-endian 32-bit register values followed by a 64-bit Unix timestamp.
 * The first five register values are live; the following five are latched.
 * Saving does not mutate the running cartridge or its latched clock.
 */
size_t cupid_gb_battery_size(const CupidGb *gb);
CupidGbBatteryResult cupid_gb_save_battery(const CupidGb *gb, uint8_t *data, size_t capacity,
                                           size_t *written);
/* Loading accepts exact RAM-only files and 44/48-byte MBC3 clock trailers.
 * Unknown sizes or invalid register encodings leave the complete instance unchanged.
 * Missing, future, or implausibly old timestamps reset the clock baseline and set
 * the live carry bit, which lets games detect a clock requiring initialization.
 */
CupidGbBatteryResult cupid_gb_load_battery(CupidGb *gb, const uint8_t *data, size_t size);
CupidGbBatteryResult cupid_gb_save_battery_at(const CupidGb *gb, uint8_t *data, size_t capacity,
                                              size_t *written, int64_t host_seconds);
CupidGbBatteryResult cupid_gb_load_battery_at(CupidGb *gb, const uint8_t *data, size_t size,
                                              int64_t host_seconds);
const char *cupid_gb_battery_result_name(CupidGbBatteryResult result);
/* Advances only the live clock. The latch and write-generation counter are independent. */
void cupid_gb_rtc_advance(CupidGbRtc *rtc, int64_t host_seconds);

#ifdef __cplusplus
}
#endif
#endif

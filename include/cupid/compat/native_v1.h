#ifndef CUPID_COMPAT_NATIVE_V1_H
#define CUPID_COMPAT_NATIVE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CUPID_NATIVE_V1_RAM_CAPACITY 131072u

typedef enum CupidNativeV1Result {
    CUPID_NATIVE_V1_OK,
    CUPID_NATIVE_V1_INVALID,
    CUPID_NATIVE_V1_UNSUPPORTED,
    CUPID_NATIVE_V1_WRONG_ROM,
    CUPID_NATIVE_V1_NO_BATTERY_DATA,
    CUPID_NATIVE_V1_OUT_OF_MEMORY
} CupidNativeV1Result;

typedef struct CupidNativeV1Battery {
    uint8_t ram[CUPID_NATIVE_V1_RAM_CAPACITY];
    size_t ram_size;
    uint8_t rtc_live[5];
    uint8_t rtc_latched[5];
    int64_t rtc_base_time;
    bool has_rtc;
} CupidNativeV1Battery;

/* This reader recovers battery data only. Native v1 has no fetched-pixel pipeline
 * state and cannot resume execution exactly in the current hardware engine.
 * The caller supplies the loaded cartridge bytes and its actual cartridge-RAM size.
 * Inputs and the output object remain unchanged on failure. Boot inputs are not
 * required because recovery never executes or restores a boot overlay.
 */
bool cupid_native_v1_is_state(const uint8_t *data, size_t length);
CupidNativeV1Result cupid_native_v1_recover_battery(const uint8_t *data, size_t length,
                                                    const uint8_t *rom, size_t rom_size,
                                                    size_t ram_size, CupidNativeV1Battery *output);
/* Padded RTC trailer matches the current MBC3 battery interchange format. Exporting
 * never advances the saved clock or changes the running engine. The importer applies
 * its normal handling for old, future, or invalid timestamps.
 */
size_t cupid_native_v1_battery_size(const CupidNativeV1Battery *battery);
CupidNativeV1Result cupid_native_v1_export_battery(const CupidNativeV1Battery *battery,
                                                   uint8_t *data, size_t capacity, size_t *written);
const char *cupid_native_v1_result_name(CupidNativeV1Result result);

#ifdef __cplusplus
}
#endif
#endif

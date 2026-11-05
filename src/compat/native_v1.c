#include "cupid/compat/native_v1.h"

#include "cupid/common/sha256.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    V1_HEADER_SIZE = 128,
    V1_ENCODED_SIZE = 300349,
    /* Frozen v1 schema: 17 CPU bytes, then 16 KiB VRAM, then cartridge RAM. */
    V1_RAM_OFFSET = V1_HEADER_SIZE + 17 + 16384,
    V1_RTC_OFFSET = 249927,
    V1_RTC_TIME_OFFSET = V1_RTC_OFFSET + 11
};

static const uint8_t magic[8] = {'C', 'U', 'P', 'I', 'D', 'G', 'B', 0};

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) | ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static int64_t read_i64(const uint8_t *bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= (uint64_t)bytes[i] << (i * 8u);
    }
    return value <= INT64_MAX ? (int64_t)value : -1 - (int64_t)(UINT64_MAX - value);
}

bool cupid_native_v1_is_state(const uint8_t *data, size_t length) {
    return data && length >= 12 && !memcmp(data, magic, sizeof(magic)) && read_u32(data + 8) == 1;
}

CupidNativeV1Result cupid_native_v1_recover_battery(const uint8_t *data, size_t length,
                                                    const uint8_t *rom, size_t rom_size,
                                                    size_t ram_size, CupidNativeV1Battery *output) {
    if (!data || !rom || !output || length < V1_HEADER_SIZE || rom_size < 0x150 ||
        rom_size > UINT32_MAX || memcmp(data, magic, sizeof(magic))) {
        return CUPID_NATIVE_V1_INVALID;
    }
    if (read_u32(data + 8) != 1 || ram_size > CUPID_NATIVE_V1_RAM_CAPACITY) {
        return CUPID_NATIVE_V1_UNSUPPORTED;
    }
    if (length != V1_ENCODED_SIZE || read_u32(data + 12) != length || read_u32(data + 16) > 5) {
        return CUPID_NATIVE_V1_INVALID;
    }
    uint32_t boot_size = read_u32(data + 24), flags = read_u32(data + 28);
    if (flags > 1 || (!flags && boot_size) || (flags && (!boot_size || boot_size > 0x900))) {
        return CUPID_NATIVE_V1_INVALID;
    }
    uint8_t digest[32];
    CupidSha256 hash;
    cupid_sha256_init(&hash);
    cupid_sha256_update(&hash, data, 96);
    cupid_sha256_update(&hash, data + V1_HEADER_SIZE, length - V1_HEADER_SIZE);
    cupid_sha256_final(&hash, digest);
    if (memcmp(digest, data + 96, sizeof(digest))) {
        return CUPID_NATIVE_V1_INVALID;
    }
    if (read_u32(data + 20) != rom_size) {
        return CUPID_NATIVE_V1_WRONG_ROM;
    }
    cupid_sha256(rom, rom_size, digest);
    if (memcmp(digest, data + 32, sizeof(digest))) {
        return CUPID_NATIVE_V1_WRONG_ROM;
    }
    /* Only MBC3 native-v1 clocks used these RTC fields. Other clock/EEPROM/flash
     * formats need separate recovery support, rather than an invented conversion. */
    bool has_rtc = rom[0x147] == 0x0f || rom[0x147] == 0x10;
    if (!ram_size && !has_rtc) {
        return CUPID_NATIVE_V1_NO_BATTERY_DATA;
    }
    CupidNativeV1Battery *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) {
        return CUPID_NATIVE_V1_OUT_OF_MEMORY;
    }
    candidate->ram_size = ram_size;
    memcpy(candidate->ram, data + V1_RAM_OFFSET, ram_size);
    candidate->has_rtc = has_rtc;
    if (has_rtc) {
        memcpy(candidate->rtc_live, data + V1_RTC_OFFSET, sizeof(candidate->rtc_live));
        memcpy(candidate->rtc_latched, data + V1_RTC_OFFSET + 5, sizeof(candidate->rtc_latched));
        candidate->rtc_base_time = read_i64(data + V1_RTC_TIME_OFFSET);
    }
    *output = *candidate;
    free(candidate);
    return CUPID_NATIVE_V1_OK;
}

size_t cupid_native_v1_battery_size(const CupidNativeV1Battery *battery) {
    if (!battery || battery->ram_size > CUPID_NATIVE_V1_RAM_CAPACITY) {
        return 0;
    }
    return battery->ram_size + (battery->has_rtc ? 48u : 0u);
}

CupidNativeV1Result cupid_native_v1_export_battery(const CupidNativeV1Battery *battery,
                                                   uint8_t *data, size_t capacity,
                                                   size_t *written) {
    if (written) {
        *written = 0;
    }
    size_t size = cupid_native_v1_battery_size(battery);
    if (!size || !data || capacity < size) {
        return CUPID_NATIVE_V1_INVALID;
    }
    memcpy(data, battery->ram, battery->ram_size);
    if (battery->has_rtc) {
        uint8_t *clock = data + battery->ram_size;
        memset(clock, 0, 48);
        for (unsigned i = 0; i < 5; ++i) {
            clock[i * 4] = battery->rtc_live[i];
            clock[20 + i * 4] = battery->rtc_latched[i];
        }
        uint64_t timestamp = (uint64_t)battery->rtc_base_time;
        for (unsigned i = 0; i < 8; ++i) {
            clock[40 + i] = (uint8_t)(timestamp >> (i * 8));
        }
    }
    if (written) {
        *written = size;
    }
    return CUPID_NATIVE_V1_OK;
}

const char *cupid_native_v1_result_name(CupidNativeV1Result result) {
    switch (result) {
        case CUPID_NATIVE_V1_OK:
            return "Battery data recovered; execution state was not restored";
        case CUPID_NATIVE_V1_INVALID:
            return "Invalid or damaged native-v1 state";
        case CUPID_NATIVE_V1_UNSUPPORTED:
            return "Unsupported legacy state or cartridge RAM size";
        case CUPID_NATIVE_V1_WRONG_ROM:
            return "Legacy state belongs to a different cartridge";
        case CUPID_NATIVE_V1_NO_BATTERY_DATA:
            return "Legacy state contains no supported battery data";
        case CUPID_NATIVE_V1_OUT_OF_MEMORY:
            return "Cannot allocate recovery storage";
        default:
            return "Unknown legacy recovery error";
    }
}

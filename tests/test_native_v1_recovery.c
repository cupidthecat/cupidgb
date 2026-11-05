#include "cupid/common/sha256.h"
#include "cupid/compat/native_v1.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ENCODED_SIZE = 300349, RAM_START = 16529, CLOCK_START = 249927 };

static void put_u32(uint8_t *target, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        target[i] = (uint8_t)(value >> (8 * i));
    }
}

static void digest(uint8_t *state) {
    CupidSha256 context;
    cupid_sha256_init(&context);
    cupid_sha256_update(&context, state, 96);
    cupid_sha256_update(&context, state + 128, ENCODED_SIZE - 128);
    cupid_sha256_final(&context, state + 96);
}

int main(int argc, char **argv) {
    assert(argc == 1 || (argc == 3 && !strcmp(argv[1], "--write-fixture")));
    uint8_t *rom = calloc(1, 32768), *state = calloc(1, ENCODED_SIZE);
    CupidNativeV1Battery *recovered = calloc(1, sizeof(*recovered));
    CupidNativeV1Battery *before = malloc(sizeof(*before));
    uint8_t *battery = malloc(8192 + 48);
    assert(rom && state && recovered && before && battery);
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    rom[0x147] = 0x10;
    rom[0x149] = 2;
    for (size_t i = 0x134; i <= 0x14c; ++i) {
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    }
    memcpy(state, "CUPIDGB", 8);
    put_u32(state + 8, 1);
    put_u32(state + 12, ENCODED_SIZE);
    put_u32(state + 20, 32768);
    cupid_sha256(rom, 32768, state + 32);
    for (size_t i = 0; i < 131072; ++i) {
        state[RAM_START + i] = (uint8_t)(i * 17u + (i >> 8));
    }
    const uint8_t live[5] = {17, 22, 12, 20, 1};
    const uint8_t latched[5] = {16, 21, 11, 19, 0};
    memcpy(state + CLOCK_START, live, sizeof(live));
    memcpy(state + CLOCK_START + 5, latched, sizeof(latched));
    uint64_t timestamp = UINT64_C(1700000000);
    for (unsigned i = 0; i < 8; ++i) {
        state[CLOCK_START + 11 + i] = (uint8_t)(timestamp >> (8 * i));
    }
    /* Valid v1 payload domain: one SGB player, even when SGB is disabled. */
    state[250136] = 1;
    digest(state);
    assert(cupid_native_v1_is_state(state, ENCODED_SIZE));
    assert(cupid_native_v1_recover_battery(state, ENCODED_SIZE, rom, 32768, 8192, recovered) ==
           CUPID_NATIVE_V1_OK);
    assert(recovered->ram_size == 8192 && recovered->has_rtc);
    assert(!memcmp(recovered->ram, state + RAM_START, 8192));
    assert(!memcmp(recovered->rtc_live, live, sizeof(live)));
    assert(!memcmp(recovered->rtc_latched, latched, sizeof(latched)));
    assert(recovered->rtc_base_time == (int64_t)timestamp);
    size_t written = 0;
    assert(cupid_native_v1_battery_size(recovered) == 8240);
    assert(cupid_native_v1_export_battery(recovered, battery, 8240, &written) ==
           CUPID_NATIVE_V1_OK);
    assert(written == 8240 && !memcmp(battery, recovered->ram, 8192));
    for (unsigned i = 0; i < 5; ++i) {
        assert(battery[8192 + i * 4] == live[i]);
        assert(battery[8192 + 20 + i * 4] == latched[i]);
        for (unsigned pad = 1; pad < 4; ++pad) {
            assert(battery[8192 + i * 4 + pad] == 0);
            assert(battery[8192 + 20 + i * 4 + pad] == 0);
        }
    }
    for (unsigned i = 0; i < 8; ++i) {
        assert(battery[8192 + 40 + i] == (uint8_t)(timestamp >> (8 * i)));
    }
    memcpy(before, recovered, sizeof(*before));
    for (size_t length = 0; length < ENCODED_SIZE; ++length) {
        assert(cupid_native_v1_recover_battery(state, length, rom, 32768, 8192, recovered) !=
               CUPID_NATIVE_V1_OK);
        assert(!memcmp(recovered, before, sizeof(*before)));
    }
    state[RAM_START + 3] ^= 1;
    assert(cupid_native_v1_recover_battery(state, ENCODED_SIZE, rom, 32768, 8192, recovered) ==
           CUPID_NATIVE_V1_INVALID);
    assert(!memcmp(recovered, before, sizeof(*before)));
    state[RAM_START + 3] ^= 1;
    rom[0x200] ^= 1;
    assert(cupid_native_v1_recover_battery(state, ENCODED_SIZE, rom, 32768, 8192, recovered) ==
           CUPID_NATIVE_V1_WRONG_ROM);
    assert(!memcmp(recovered, before, sizeof(*before)));
    rom[0x200] ^= 1;
    assert(cupid_native_v1_recover_battery(state, ENCODED_SIZE, rom, 32768, 131073, recovered) ==
           CUPID_NATIVE_V1_UNSUPPORTED);
    for (unsigned i = 0; i < 8; ++i) {
        state[CLOCK_START + 11 + i] = (uint8_t)(UINT64_C(0x8000000000000000) >> (8 * i));
    }
    digest(state);
    assert(cupid_native_v1_recover_battery(state, ENCODED_SIZE, rom, 32768, 8192, recovered) ==
           CUPID_NATIVE_V1_OK);
    assert(recovered->rtc_base_time == INT64_MIN);
    assert(cupid_native_v1_export_battery(recovered, battery, 8240, &written) ==
           CUPID_NATIVE_V1_OK);
    assert(battery[8239] == 0x80);
    if (argc == 3) {
        FILE *file = fopen(argv[2], "wb");
        assert(file && fwrite(state, 1, ENCODED_SIZE, file) == ENCODED_SIZE && fclose(file) == 0);
    }
    free(battery);
    free(before);
    free(recovered);
    free(state);
    free(rom);
    puts("Native-v1 battery recovery checks passed");
    return 0;
}

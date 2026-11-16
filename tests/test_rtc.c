#include "../src/gb/rtc_internal.h"
#include "cupid/gb/battery.h"
#include "cupid/gb/huc3.h"
#include "cupid/gb/tpp1.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestClock {
    int64_t now;
    unsigned calls;
} TestClock;

static int64_t observed_time(void *context) {
    TestClock *clock = context;
    ++clock->calls;
    return clock->now;
}

static void make_rom(uint8_t *rom, uint8_t type) {
    memset(rom, 0, 0x8000);
    rom[0x143] = 0x80;
    rom[0x147] = type;
    rom[0x149] = 2;
    if (type == 0xbc) {
        rom[0x149] = 0xc1;
        rom[0x14a] = 0x65;
        rom[0x152] = 1;
        rom[0x153] = 8;
    }
    for (unsigned i = 0x134; i < 0x14d; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
}

static CupidGb *cartridge(uint8_t type, CupidGbModel model, TestClock *clock) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t rom[0x8000];
    assert(gb);
    make_rom(rom, type);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    cupid_gb_set_rtc_time_source(gb, observed_time, clock);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_policy_and_lifecycle(void) {
    assert(cupid_gb_get_rtc_mode(NULL) == CUPID_GB_RTC_SYNC_TO_HOST);
    assert(!cupid_gb_set_rtc_mode(NULL, CUPID_GB_RTC_ACCURATE));
    assert(!cupid_gb_set_rtc_multiplier(NULL, 1));
    cupid_gb_set_rtc_time_source(NULL, observed_time, NULL);
    TestClock clock = {1700000000, 0};
    CupidGb *gb = cartridge(0x10, CUPID_GB_MODEL_DMG_ABC, &clock);
    assert(clock.calls == 1 && gb->rtc.base_time == clock.now);
    assert(cupid_gb_get_rtc_mode(gb) == CUPID_GB_RTC_SYNC_TO_HOST);
    assert(cupid_gb_set_rtc_multiplier(gb, 2));
    gb->rtc = (CupidGbRtc){.s = 13, .ls = 29, .base_time = clock.now - 75};
    gb->rtc_cycles = 123;
    const CupidGbRtc before = gb->rtc;
    assert(!cupid_gb_set_rtc_mode(gb, (CupidGbRtcMode)-1));
    assert(!cupid_gb_set_rtc_mode(gb, (CupidGbRtcMode)2));
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_SYNC_TO_HOST));
    assert(memcmp(&gb->rtc, &before, sizeof(before)) == 0);
    assert(gb->rtc_cycles == 123 && clock.calls == 1);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(gb->rtc_cycles == 0);
    assert(gb->rtc.s == 13 && gb->rtc.ls == 29 && gb->rtc.base_time == clock.now);
    assert(clock.calls == 2 && gb->rtc_policy.second_length == 16777216u);

    TestClock other = {-1, 0};
    const int64_t baseline = gb->rtc.base_time;
    cupid_gb_set_rtc_time_source(gb, observed_time, &other);
    assert(!other.calls && gb->rtc.base_time == baseline);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_SYNC_TO_HOST));
    assert(gb->rtc.base_time == 0 && other.calls == 1);
    cupid_gb_set_rtc_time_source(gb, NULL, &other);
    assert(!gb->rtc_policy.time_source && !gb->rtc_policy.time_user_data);
    cupid_gb_set_rtc_time_source(gb, observed_time, &clock);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    const CupidGbRtcPolicy policy = gb->rtc_policy;
    const unsigned calls = clock.calls;
    cupid_gb_init(gb);
    assert(gb->rtc_policy.mode == policy.mode &&
           gb->rtc_policy.second_length == policy.second_length &&
           gb->rtc_policy.time_source == policy.time_source &&
           gb->rtc_policy.time_user_data == policy.time_user_data);
    assert(!gb->rtc_cycles && clock.calls == calls);
    uint8_t rom[0x8000];
    make_rom(rom, 0x10);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(gb->rtc.base_time == clock.now && clock.calls == calls + 1);
    gb->rtc_cycles = 77;
    gb->rtc.s = 18;
    gb->boot_rom_size = 0x100;
    cupid_gb_start_boot_rom(gb);
    assert(gb->rtc.s == 18 && gb->rtc.base_time == clock.now && gb->rtc_cycles == 77);
    assert(gb->rtc_policy.mode == CUPID_GB_RTC_ACCURATE);
    assert(gb->rtc_policy.time_source == observed_time && clock.calls == calls + 1);
    cupid_gb_cleanup(gb);
    assert(gb->rtc_policy.mode == CUPID_GB_RTC_SYNC_TO_HOST && !gb->rtc_policy.second_length &&
           !gb->rtc_policy.time_source && !gb->rtc_policy.time_user_data);
    free(gb);
}

static void test_multiplier_bounds(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_DMG_ABC,
                                   CUPID_GB_MODEL_MGB,  CUPID_GB_MODEL_CGB,
                                   CUPID_GB_MODEL_SGB,  CUPID_GB_MODEL_SGB2};
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        gb->model = models[i];
        const double frequency = cupid_gb_base_clock_hz(gb);
        gb->rtc_cycles = 12345;
        assert(cupid_gb_set_rtc_multiplier(gb, 0.5));
        assert(gb->rtc_policy.second_length == (uint32_t)frequency);
        assert(cupid_gb_set_rtc_multiplier(gb, 2));
        assert(gb->rtc_policy.second_length == (uint32_t)(frequency * 4));
        assert(cupid_gb_set_rtc_multiplier(gb, (4294967295.75 / 2) / frequency));
        assert(gb->rtc_policy.second_length == UINT32_MAX);
        const double bad[] = {-1, NAN, INFINITY, -INFINITY, 4294967296.0 / (2 * frequency)};
        for (unsigned j = 0; j < sizeof(bad) / sizeof(bad[0]); ++j) {
            assert(!cupid_gb_set_rtc_multiplier(gb, bad[j]));
            assert(gb->rtc_policy.second_length == UINT32_MAX && gb->rtc_cycles == 12345);
        }
        const double defaults[] = {1, 0, 0.25 / frequency};
        for (unsigned j = 0; j < sizeof(defaults) / sizeof(defaults[0]); ++j) {
            assert(cupid_gb_set_rtc_multiplier(gb, defaults[j]));
            assert(gb->rtc_policy.second_length == 0 && gb->rtc_cycles == 12345);
        }
    }
    free(gb);
}

static void test_scheduler_policy(void) {
    const uint8_t types[] = {0x10, 0xfe, 0xbc};
    for (unsigned i = 0; i < sizeof(types); ++i) {
        TestClock clock = {60000, 0};
        CupidGb *gb = cartridge(types[i], CUPID_GB_MODEL_DMG_ABC, &clock);
        gb->tpp1.status = CUPID_GB_TPP1_CLOCK_RUNNING;
        const unsigned calls = clock.calls;
        const uint32_t interval = cupid_gb_base_clock_hz(gb) / 16;
        gb->rtc_cycles = interval - 1;
        clock.now = 60061;
        cupid_gb_rtc_tick(gb, 0);
        assert(gb->rtc_cycles == interval - 1 && clock.calls == calls);
        cupid_gb_rtc_tick(gb, 1);
        assert(!gb->rtc_cycles && clock.calls == calls + 1);
        if (types[i] == 0x10)
            assert(gb->rtc.s == 1 && gb->rtc.m == 1 && gb->rtc.base_time == 60061);
        else if (types[i] == 0xfe)
            assert(gb->huc3.minutes == 1 && gb->huc3.rtc_base_time == 60060);
        else
            assert(gb->tpp1.live[3] == 1 && gb->tpp1.live[2] == 1 &&
                   gb->tpp1.rtc_base_time == 60061);
        assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
        const unsigned accurate_calls = clock.calls;
        clock.now += 100000;
        gb->rtc_cycles = cupid_gb_base_clock_hz(gb) * 2 - 1;
        cupid_gb_rtc_tick(gb, 1);
        assert(!gb->rtc_cycles && clock.calls == accurate_calls);
        if (types[i] == 0x10)
            assert(gb->rtc.s == 2 && gb->rtc.base_time == 60062);
        else if (types[i] == 0xfe)
            assert(gb->huc3.minutes == 1 && gb->huc3.rtc_base_time == 60061);
        else
            assert(gb->tpp1.live[3] == 2 && gb->tpp1.rtc_base_time == 60062);
        destroy(gb);
    }

    TestClock clock = {60000, 0};
    CupidGb *gb = cartridge(0x10, CUPID_GB_MODEL_DMG_ABC, &clock);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    gb->rtc_cycles = 100;
    gb->rtc.dh = 0x40;
    cupid_gb_rtc_tick(gb, UINT32_MAX);
    assert(gb->rtc_cycles == 100 && gb->rtc.base_time == 60000);
    gb->rtc.dh = 0;
    gb->rtc_cycles = UINT32_MAX - 3;
    cupid_gb_rtc_tick(gb, 8);
    assert(gb->rtc_cycles == 4 && gb->rtc.base_time == 60000);
    assert(cupid_gb_set_rtc_multiplier(gb, 1.0 / 8388608.0));
    gb->rtc_cycles = 9;
    cupid_gb_rtc_tick(gb, 1);
    assert(gb->rtc_cycles == 9 && gb->rtc.s == 1 && gb->rtc.base_time == 60001);
    gb->header.has_timer = false;
    cupid_gb_rtc_tick(gb, 100);
    assert(gb->rtc_cycles == 9 && gb->rtc.s == 1);
    gb->header.has_timer = true;
    gb->header.mbc_type = CUPID_GB_MBC5;
    cupid_gb_rtc_tick(gb, 100);
    assert(gb->rtc_cycles == 9);
    destroy(gb);
}

static void test_register_clock_boundaries(void) {
    TestClock clock = {60000, 0};
    CupidGb *gb = cartridge(0x10, CUPID_GB_MODEL_DMG_ABC, &clock);
    cupid_gb_write_u8(gb, 0, 0x0a);
    gb->rtc.s = 17;
    clock.now = 60061;
    const unsigned calls = clock.calls;
    for (unsigned value = 0; value < 4; ++value) {
        gb->rtc.s = (uint8_t)(17 + value);
        cupid_gb_write_u8(gb, 0x6000, (uint8_t)value);
        assert(gb->rtc.ls == gb->rtc.s && gb->rtc.base_time == 60000 && clock.calls == calls);
    }
    cupid_gb_write_u8(gb, 0x4000, 8);
    gb->rtc_cycles = 123;
    cupid_gb_write_u8(gb, 0xa000, 9);
    assert(gb->rtc.s == 9 && !gb->rtc_cycles && gb->rtc.base_time == 60000);
    assert(clock.calls == calls);
    cupid_gb_write_u8(gb, 0x4000, 12);
    gb->rtc_cycles = 77;
    cupid_gb_write_u8(gb, 0xa000, 0x40);
    assert(gb->rtc.s == 10 && gb->rtc.m == 1 && gb->rtc.base_time == 60061);
    assert(gb->rtc_cycles == 77 && clock.calls == calls + 1);
    clock.now = 70000;
    cupid_gb_write_u8(gb, 0xa000, 0);
    assert(gb->rtc.s == 10 && gb->rtc.base_time == 70000 && clock.calls == calls + 2);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    const unsigned accurate_calls = clock.calls;
    gb->rtc_cycles = 99;
    cupid_gb_write_u8(gb, 0xa000, 0x40);
    cupid_gb_write_u8(gb, 0xa000, 0);
    assert(gb->rtc_cycles == 99 && gb->rtc.base_time == 70000 && clock.calls == accurate_calls);
    destroy(gb);
}

static void test_huc3_remainders(void) {
    const uint16_t starts[] = {0, 1439, 4095, 65535};
    for (unsigned remainder = 0; remainder < 60; ++remainder) {
        for (unsigned initial = 0; initial < sizeof(starts) / sizeof(starts[0]); ++initial) {
            CupidGbHuc3 clock = {
                .rtc_base_time = 60000 + remainder, .minutes = starts[initial], .days = 65535};
            uint16_t minutes = clock.minutes, days = clock.days;
            for (unsigned tick = 0; tick < 61; ++tick) {
                cupid_gb_huc3_advance(&clock, clock.rtc_base_time + 1);
                if (remainder == 59) {
                    ++minutes;
                    if (minutes == 1440) {
                        minutes = 0;
                        ++days;
                    }
                }
                assert(clock.minutes == minutes && clock.days == days);
                assert(clock.rtc_base_time ==
                       60000 + (int64_t)remainder + (remainder == 59 ? (tick + 1) * 60 : 0));
            }
        }
    }
    CupidGbHuc3 epoch = {0};
    cupid_gb_huc3_advance(&epoch, 60);
    assert(epoch.minutes == 1 && epoch.rtc_base_time == 60);
    cupid_gb_huc3_advance(&epoch, 0);
    assert(epoch.minutes == 1 && epoch.rtc_base_time == 60);
}

static void test_strict_days_and_unsigned_history(void) {
    CupidGbRtc clock = {.s = 63, .base_time = 60000};
    cupid_gb_rtc_advance(&clock, 60000 + 86400);
    assert(clock.s == 47 && clock.m == 56 && clock.h == 23 && clock.dl == 0);
    clock = (CupidGbRtc){.s = 63, .base_time = 60000};
    cupid_gb_rtc_advance(&clock, 60000 + 86401);
    assert(clock.s == 64 && clock.m == 0 && clock.h == 0 && clock.dl == 1);
    clock = (CupidGbRtc){.base_time = INT64_MIN};
    cupid_gb_rtc_advance(&clock, INT64_MAX);
    assert(clock.base_time == INT64_MIN && !clock.s && !clock.m && !clock.h && !clock.dh);
    clock = (CupidGbRtc){.base_time = -1};
    cupid_gb_rtc_advance(&clock, 86400);
    assert(clock.base_time == 86400 && clock.s == 1 && clock.dl == 1);
    /* Finite top-of-domain orbits require bounded arithmetic: these expected
     * values are mathematical counter results, not completed host-time loops. */
    clock = (CupidGbRtc){.base_time = -129};
    cupid_gb_rtc_advance(&clock, -128);
    assert(clock.base_time == -128 && clock.s == 53 && clock.m == 57 && clock.h == 23 &&
           clock.dl == 10 && clock.dh == 129);
    CupidGbTpp1 tpp = {.status = 4, .rtc_base_time = -129};
    cupid_gb_tpp1_advance(&tpp, -128);
    const uint8_t expected[] = {184, 87, 57, 53};
    assert(memcmp(tpp.live, expected, sizeof(expected)) == 0 && tpp.status == 12 &&
           tpp.rtc_base_time == -128);
    for (int64_t baseline = -128; baseline <= -2; ++baseline) {
        clock = (CupidGbRtc){.s = 63, .ls = 45, .dh = 0x81, .base_time = baseline};
        const CupidGbRtc before = clock;
        cupid_gb_rtc_advance(&clock, baseline + 1);
        assert(memcmp(&clock, &before, sizeof(clock)) == 0);
    }
    TestClock host = {60000, 0};
    CupidGb *gb = cartridge(0x10, CUPID_GB_MODEL_DMG_ABC, &host);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    gb->rtc = (CupidGbRtc){.s = 19, .ls = 45, .base_time = -2};
    const CupidGbRtc before = gb->rtc;
    gb->rtc_cycles = 8388607;
    cupid_gb_rtc_tick(gb, 1);
    assert(!gb->rtc_cycles && memcmp(&gb->rtc, &before, sizeof(before)) == 0);
    destroy(gb);
}

int main(void) {
    test_policy_and_lifecycle();
    test_multiplier_bounds();
    test_scheduler_policy();
    test_register_clock_boundaries();
    test_huc3_remainders();
    test_strict_days_and_unsigned_history();
    puts("RTC policy, counter and lifecycle regressions passed.");
    return 0;
}

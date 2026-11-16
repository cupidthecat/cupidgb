#include "cupid/gb/gb.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestClock {
    int64_t now;
    unsigned reads;
} TestClock;

static int64_t read_clock(void *context) {
    TestClock *clock = context;
    ++clock->reads;
    return clock->now;
}

static CupidGb *machine(TestClock *clock, bool fast, unsigned kind) {
    uint8_t rom[0x8000] = {0};
    memcpy(rom + 0x134, "RTC DMA", 7);
    rom[0x143] = 0x80;
    rom[0x147] = 0x10;
    rom[0x149] = 2;
    for (size_t offset = 0x134; offset < 0x14d; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);

    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, CUPID_GB_MODEL_CGB);
    cupid_gb_set_power_on_seed(gb, 0);
    cupid_gb_set_rtc_time_source(gb, read_clock, clock);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->double_speed = fast;
    gb->cpu.pc = 0x150;
    gb->cpu.ime = false;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff4d, 0);
    cupid_gb_write_u8(gb, 0xff4f, 0);
    memset(gb->video_ram, 0, sizeof(gb->video_ram));
    for (unsigned byte = 0; byte < 64; ++byte)
        cupid_gb_write_u8(gb, (uint16_t)(0xc100u + byte), (uint8_t)(byte * 13u + 7u));
    cupid_gb_write_u8(gb, 0xff51, 0xc1);
    cupid_gb_write_u8(gb, 0xff52, 0);
    cupid_gb_write_u8(gb, 0xff53, 0);
    cupid_gb_write_u8(gb, 0xff54, 0);
    cupid_gb_write_u8(gb, 0xff55, kind == 0 ? 0 : kind == 1 ? 1 : 0x81);
    if (kind == 3) {
        gb->cpu.halted = true;
        gb->cpu.just_halted = false;
    }
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    gb->rtc.base_time = clock->now;
    gb->rtc.s = 10;
    gb->rtc.m = 5;
    gb->rtc.h = 2;
    gb->rtc.dl = 1;
    gb->rtc.dh = 0;
    clock->reads = 0;
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void period(CupidGb *gb, unsigned ticks, uint32_t phase) {
    assert(cupid_gb_set_rtc_multiplier(gb, ((double)ticks + 0.25) /
                                               ((double)cupid_gb_base_clock_hz(gb) * 2.0)));
    assert(gb->rtc_policy.second_length == ticks);
    gb->rtc_cycles = phase;
}

static void check_clock(const CupidGb *gb, const TestClock *clock, unsigned updates,
                        uint32_t phase) {
    assert(gb->rtc.base_time == clock->now + updates);
    assert(gb->rtc.s == 10u + updates && gb->rtc.m == 5 && gb->rtc.h == 2);
    assert(gb->rtc.dl == 1 && gb->rtc.dh == 0);
    assert(gb->rtc_cycles == phase);
    assert(clock->reads == 0);
}

static void check_transfer(const CupidGb *gb, unsigned kind) {
    const unsigned copied = kind == 3 ? 0 : kind == 1 ? 32 : 16;
    for (unsigned byte = 0; byte < 64; ++byte)
        assert(gb->video_ram[byte] == (byte < copied ? (uint8_t)(byte * 13u + 7u) : 0));
    assert(gb->hdma_source == 0xc100u + copied);
    assert(gb->hdma_destination == copied);
    assert(gb->hdma_blocks_remaining == (kind == 3 ? 2 : kind == 2 ? 1 : 0));
    assert(gb->hdma_active == (kind >= 2));
    assert(gb->hdma_pending == (kind == 3));
    assert(!gb->hdma_running);
}

static void test_transfer_modes(void) {
    static const struct {
        unsigned half, updates;
    } expected[2][4] = {{{80, 19}, {144, 35}, {80, 19}, {8, 1}},
                        {{72, 18}, {136, 34}, {72, 18}, {4, 1}}};

    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned kind = 0; kind < 4; ++kind) {
            TestClock clock = {1700000040, 0};
            CupidGb *gb = machine(&clock, fast != 0, kind);
            period(gb, 1, 0);
            const uint64_t before = gb->elapsed_cycles;
            assert(cupid_gb_step(gb));
            check_clock(gb, &clock, expected[fast][kind].updates,
                        expected[fast][kind].half - expected[fast][kind].updates);
            check_transfer(gb, kind);
            assert(gb->elapsed_cycles - before == expected[fast][kind].half / 2u);
            assert(gb->cpu.pc == (kind == 3 ? 0x150 : 0x151));
            assert(gb->cpu.halted == (kind == 3));
            assert(cupid_gb_read_u8(gb, 0xff55) == (kind == 3 ? 1 : kind == 2 ? 0 : 0xff));
            assert(cupid_gb_step(gb));
            check_transfer(gb, kind);
            check_clock(gb, &clock, expected[fast][kind].updates + 1u,
                        expected[fast][kind].half - expected[fast][kind].updates +
                            (fast ? 3u : 7u));
            release(gb);
        }
    }
}

static void test_speed_boundaries(void) {
    static const struct {
        uint8_t toggle, freeze;
        uint16_t half[2];
        uint8_t updates[2];
    } cases[] = {{0, 0, {80, 72}, {19, 18}},  {1, 0, {39, 147}, {19, 20}},
                 {2, 1, {37, 145}, {18, 20}}, {4, 0, {40, 148}, {18, 19}},
                 {6, 3, {36, 139}, {17, 20}}, {8, 5, {34, 138}, {16, 18}}};

    for (unsigned fast = 0; fast < 2; ++fast) {
        for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); ++index) {
            TestClock clock = {1700000040, 0};
            CupidGb *gb = machine(&clock, fast != 0, 0);
            period(gb, 1, 0);
            gb->speed_switch_toggle_cycles = cases[index].toggle;
            gb->speed_switch_freeze_cycles = cases[index].freeze;
            assert(cupid_gb_step(gb));
            const unsigned phase = cases[index].half[fast] - cases[index].updates[fast];
            check_clock(gb, &clock, cases[index].updates[fast], phase);
            check_transfer(gb, 0);
            const bool final_fast = cases[index].toggle ? !fast : fast != 0;
            assert(gb->double_speed == final_fast);
            assert(gb->speed_switch_toggle_cycles == 0 && gb->speed_switch_freeze_cycles == 0);
            assert((cupid_gb_read_u8(gb, 0xff4d) & 0x80u) == (final_fast ? 0x80u : 0));
            /* The next fetch is a separate delivery after the completed transfer. */
            assert(cupid_gb_step(gb));
            check_transfer(gb, 0);
            check_clock(gb, &clock, cases[index].updates[fast] + 1u,
                        phase + (final_fast ? 3u : 7u));
            release(gb);
        }
    }
}

static void test_accumulated_debt(void) {
    static const struct {
        bool fast;
        uint8_t toggle, freeze, initial, phase, updates;
    } cases[] = {{false, 0, 0, 7, 7, 10},  {false, 0, 0, 31, 7, 13}, {false, 1, 0, 31, 6, 8},
                 {false, 8, 5, 31, 1, 8},  {true, 0, 0, 7, 7, 9},    {true, 0, 0, 31, 7, 12},
                 {true, 1, 0, 31, 18, 20}, {true, 8, 5, 31, 25, 18}};

    for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); ++index) {
        TestClock clock = {1700000040, 0};
        CupidGb *gb = machine(&clock, cases[index].fast, 0);
        period(gb, 8, cases[index].initial);
        gb->speed_switch_toggle_cycles = cases[index].toggle;
        gb->speed_switch_freeze_cycles = cases[index].freeze;
        assert(cupid_gb_step(gb));
        check_clock(gb, &clock, cases[index].updates, cases[index].phase);
        check_transfer(gb, 0);
        release(gb);
    }
}

int main(void) {
    test_transfer_modes();
    test_speed_boundaries();
    test_accumulated_debt();
    return 0;
}

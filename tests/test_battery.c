#include "cupid/gb/battery.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const int64_t test_time = INT64_C(1700000000);

static CupidGb *cartridge(uint8_t type, uint8_t ram_size) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t rom[32768] = {0};
    assert(gb);
    memcpy(rom + 0x134, "BATTERY TEST", 12);
    rom[0x147] = type;
    rom[0x149] = ram_size;
    for (unsigned i = 0x134; i < 0x14d; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    cupid_gb_init(gb);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    for (size_t i = 0; i < gb->cartridge_ram_size; ++i)
        gb->cartridge_ram[i] = (uint8_t)(i * 13u + 7u);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static uint8_t *saved(CupidGb *gb, size_t *size) {
    *size = cupid_gb_battery_size(gb);
    uint8_t *bytes = malloc(*size);
    size_t written = 0;
    assert(bytes);
    assert(cupid_gb_save_battery_at(gb, bytes, *size, &written, test_time) == CUPID_GB_BATTERY_OK);
    assert(written == *size);
    return bytes;
}

static void rejected(CupidGb *gb, const uint8_t *data, size_t size, CupidGbBatteryResult result) {
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, gb, sizeof(*before));
    assert(cupid_gb_load_battery_at(gb, data, size, test_time) == result);
    assert(memcmp(gb, before, sizeof(*gb)) == 0);
    free(before);
}

static void test_raw_ram(void) {
    const uint8_t types[] = {0x03, 0x06, 0x09, 0x13, 0x1b};
    for (size_t i = 0; i < sizeof(types); ++i) {
        CupidGb *gb = cartridge(types[i], 2);
        size_t size;
        uint8_t *bytes = saved(gb, &size);
        assert(size == gb->cartridge_ram_size);
        assert(memcmp(bytes, gb->cartridge_ram, size) == 0);
        rejected(gb, bytes, size - 1, CUPID_GB_BATTERY_INVALID_SIZE);
        rejected(gb, bytes, size + 1, CUPID_GB_BATTERY_INVALID_SIZE);
        memset(gb->cartridge_ram, 0, size);
        assert(cupid_gb_load_battery_at(gb, bytes, size, test_time) == CUPID_GB_BATTERY_OK);
        assert(memcmp(bytes, gb->cartridge_ram, size) == 0);
        assert(gb->battery_write_generation == 0);
        free(bytes);
        destroy(gb);
    }
}

static void test_rtc_format(void) {
    CupidGb *gb = cartridge(0x10, 2);
    gb->rtc = (CupidGbRtc){.s = 57,
                           .m = 34,
                           .h = 12,
                           .dl = 52,
                           .dh = 64,
                           .ls = 56,
                           .lm = 34,
                           .lh = 12,
                           .ldl = 52,
                           .ldh = 64,
                           .base_time = test_time - 900};
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, gb, sizeof(*before));
    size_t size;
    uint8_t *bytes = saved(gb, &size);
    assert(memcmp(before, gb, sizeof(*gb)) == 0);
    assert(size == 8192 + 48);
    const uint8_t values[] = {57, 34, 12, 52, 64, 56, 34, 12, 52, 64};
    for (unsigned i = 0; i < 10; ++i) {
        assert(bytes[8192 + i * 4] == values[i]);
        assert(!bytes[8192 + i * 4 + 1] && !bytes[8192 + i * 4 + 2] && !bytes[8192 + i * 4 + 3]);
    }
    for (unsigned i = 0; i < 8; ++i)
        assert(bytes[8192 + 40 + i] == (uint8_t)((uint64_t)test_time >> (i * 8)));
    memset(&gb->rtc, 0, sizeof(gb->rtc));
    gb->rtc.latch_prev = 0x5a;
    memset(gb->cartridge_ram, 0, 8192);
    assert(cupid_gb_load_battery_at(gb, bytes, size, test_time) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.s == 57 && gb->rtc.ls == 56 && gb->rtc.m == 34 && gb->rtc.lm == 34);
    assert(gb->rtc.h == 12 && gb->rtc.dl == 52 && gb->rtc.dh == 64 && gb->rtc.ldh == 64);
    assert(gb->rtc.base_time == test_time && gb->rtc.latch_prev == 0x5a);
    assert(memcmp(gb->cartridge_ram, before->cartridge_ram, 8192) == 0);
    assert(cupid_gb_load_battery_at(gb, bytes, size - 4, test_time) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.s == 57 && gb->rtc.ls == 56);
    for (size_t cut = 0; cut < size; ++cut)
        if (cut != 8192 && cut != size - 4)
            rejected(gb, bytes, cut, CUPID_GB_BATTERY_INVALID_SIZE);
    uint8_t *bad = malloc(size + 1);
    assert(bad);
    memcpy(bad, bytes, size);
    for (unsigned field = 0; field < 10; ++field) {
        bad[8192 + field * 4 + 1] = 1;
        rejected(gb, bad, size, CUPID_GB_BATTERY_INVALID_RTC);
        bad[8192 + field * 4 + 1] = 0;
    }
    rejected(gb, bad, size + 1, CUPID_GB_BATTERY_INVALID_SIZE);
    memset(bad, 0xa5, size + 1);
    size_t written = 99;
    assert(cupid_gb_save_battery_at(gb, bad, size - 1, &written, test_time) ==
           CUPID_GB_BATTERY_BUFFER_TOO_SMALL);
    assert(written == 0);
    for (size_t i = 0; i < size + 1; ++i)
        assert(bad[i] == 0xa5);
    assert(cupid_gb_save_battery_at(gb, gb->cartridge_ram, size, &written, test_time) ==
           CUPID_GB_BATTERY_INVALID_ARGUMENT);
    free(before);
    free(bad);
    free(bytes);
    destroy(gb);
}

static void test_clock_advance(void) {
    CupidGbRtc rtc = {.s = 59,
                      .m = 59,
                      .h = 23,
                      .dl = 255,
                      .dh = 1,
                      .ls = 42,
                      .lm = 43,
                      .lh = 44,
                      .ldl = 45,
                      .ldh = 46,
                      .base_time = test_time};
    cupid_gb_rtc_advance(&rtc, test_time + 1);
    assert(rtc.s == 0 && rtc.m == 0 && rtc.h == 0 && rtc.dl == 0 && rtc.dh == 0x80);
    assert(rtc.ls == 42 && rtc.lm == 43 && rtc.lh == 44 && rtc.ldl == 45 && rtc.ldh == 46);
    rtc.dh |= 0x40;
    rtc.s = 7;
    cupid_gb_rtc_advance(&rtc, test_time + 100000);
    assert(rtc.s == 7 && rtc.dh == 0xc0 && rtc.base_time == test_time + 100000);
    rtc.dh &= 0xbfu;
    cupid_gb_rtc_advance(&rtc, test_time);
    assert(rtc.s == 7 && rtc.dh == 0x80 && rtc.base_time == test_time);
    rtc = (CupidGbRtc){.s = 63, .m = 63, .h = 31, .base_time = test_time};
    cupid_gb_rtc_advance(&rtc, test_time + 192);
    assert(rtc.s == 255 && rtc.m == 63 && rtc.h == 31);
    cupid_gb_rtc_advance(&rtc, test_time + 252);
    assert(rtc.s == 59 && rtc.m == 63 && rtc.h == 31);
    cupid_gb_rtc_advance(&rtc, test_time + 253);
    assert(rtc.s == 0 && rtc.m == 64 && rtc.h == 31);
    rtc = (CupidGbRtc){.base_time = INT64_MIN};
    cupid_gb_rtc_advance(&rtc, INT64_MAX);
    uint64_t distance = UINT64_MAX;
    assert(rtc.s == distance % 60 && rtc.m == distance / 60 % 60 && rtc.h == distance / 3600 % 24 &&
           rtc.dl == (uint8_t)(distance / 86400));
    assert(rtc.dh == (uint8_t)(0x80u | ((distance / 86400 >> 8) & 1)));
}

static void test_running_and_legacy_clock(void) {
    CupidGb *gb = cartridge(0x10, 2);
    gb->rtc = (CupidGbRtc){.s = 59, .ls = 17, .base_time = test_time - 1};
    size_t size;
    uint8_t *bytes = saved(gb, &size);
    assert(gb->rtc.s == 59 && gb->rtc.ls == 17);
    assert(bytes[8192] == 0 && bytes[8196] == 1 && bytes[8212] == 17);
    assert(cupid_gb_load_battery_at(gb, bytes, size, test_time + 125) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.s == 5 && gb->rtc.m == 3 && gb->rtc.ls == 17);
    for (unsigned i = 0; i < 8; ++i)
        bytes[8232 + i] = 0xff;
    assert(cupid_gb_load_battery_at(gb, bytes, size, test_time) == CUPID_GB_BATTERY_OK);
    assert((gb->rtc.dh & 0x80) && gb->rtc.base_time == test_time && gb->rtc.ls == 17);
    gb->rtc.s = 29;
    gb->rtc.ls = 31;
    gb->rtc.dh = 0x40;
    assert(cupid_gb_load_battery_at(gb, bytes, 8192, test_time + 1) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.s == 29 && gb->rtc.ls == 31 && gb->rtc.dh == 0xc0);
    free(bytes);
    destroy(gb);
    gb = cartridge(0x0f, 0);
    assert(cupid_gb_battery_size(gb) == 48);
    assert(cupid_gb_load_battery_at(gb, NULL, 0, test_time) == CUPID_GB_BATTERY_OK);
    assert(gb->rtc.dh & 0x80);
    destroy(gb);
    gb = cartridge(0, 0);
    assert(!cupid_gb_battery_size(gb));
    assert(cupid_gb_save_battery_at(gb, NULL, 0, NULL, test_time) == CUPID_GB_BATTERY_NO_DATA);
    destroy(gb);
}

static void test_write_generation(void) {
    CupidGb *gb = cartridge(0x10, 2);
    cupid_gb_write_u8(gb, 0xa000, 0x55);
    assert(gb->battery_write_generation == 0);
    cupid_gb_write_u8(gb, 0x0000, 0x0a);
    cupid_gb_write_u8(gb, 0xa000, 0x55);
    assert(gb->battery_write_generation == 1);
    cupid_gb_write_u8(gb, 0xa000, 0x55);
    assert(gb->battery_write_generation == 1);
    gb->rtc = (CupidGbRtc){.s = 3, .ls = 42, .dh = 0x40, .base_time = (int64_t)time(NULL)};
    cupid_gb_write_u8(gb, 0x4000, 8);
    cupid_gb_write_u8(gb, 0xa000, 63);
    assert(gb->rtc.s == 63 && gb->rtc.ls == 42 && gb->battery_write_generation == 2);
    cupid_gb_write_u8(gb, 0xa000, 63);
    assert(gb->battery_write_generation == 2);
    gb->rtc.latch_prev = 1;
    cupid_gb_write_u8(gb, 0x6000, 0);
    cupid_gb_write_u8(gb, 0x6000, 1);
    assert(gb->rtc.ls == 63 && gb->battery_write_generation == 2);
    gb->rtc.ls = gb->rtc.lm = gb->rtc.lh = gb->rtc.ldh = 0xff;
    assert(cupid_gb_read_u8(gb, 0xa000) == 63);
    cupid_gb_write_u8(gb, 0x4000, 9);
    assert(cupid_gb_read_u8(gb, 0xa000) == 63);
    cupid_gb_write_u8(gb, 0x4000, 10);
    assert(cupid_gb_read_u8(gb, 0xa000) == 31);
    cupid_gb_write_u8(gb, 0x4000, 12);
    assert(cupid_gb_read_u8(gb, 0xa000) == 0xc1);
    assert(gb->battery_write_generation == 2);
    cupid_gb_rtc_advance(&gb->rtc, gb->rtc.base_time + 100);
    assert(gb->battery_write_generation == 2);
    destroy(gb);
    gb = cartridge(0x13, 3);
    cupid_gb_write_u8(gb, 0, 0x0a);
    cupid_gb_write_u8(gb, 0x4000, 0x0a);
    cupid_gb_write_u8(gb, 0xa000, 0x77);
    assert(gb->cartridge_ram[0x4000] == 0x77 && cupid_gb_read_u8(gb, 0xa000) == 0x77);
    assert(gb->battery_write_generation == 1);
    destroy(gb);
    gb = cartridge(0x06, 0);
    cupid_gb_write_u8(gb, 0, 0x0a);
    cupid_gb_write_u8(gb, 0xa000, 0x3a);
    assert(gb->battery_write_generation == 1 && gb->cartridge_ram[0] == 10);
    cupid_gb_write_u8(gb, 0xa000, 0xfa);
    assert(gb->battery_write_generation == 1);
    destroy(gb);
}

static void test_legacy_files(void) {
    CupidGb *gb = cartridge(0x10, 2);
    struct timespec now;
    assert(timespec_get(&now, TIME_UTC));
    FILE *file = NULL;
    for (unsigned i = 0; i < 128 && !file; ++i) {
        snprintf(gb->save_path, sizeof(gb->save_path), "cupidgb-battery-%llu-%lu-%u.sav",
                 (unsigned long long)now.tv_sec, (unsigned long)now.tv_nsec, i);
        file = fopen(gb->save_path, "wbx");
        assert(file || errno == EEXIST);
    }
    assert(file && fclose(file) == 0);
    gb->rtc = (CupidGbRtc){.s = 9, .ls = 7, .dh = 0x40, .base_time = test_time};
    cupid_gb_save(gb);
    file = fopen(gb->save_path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0 && ftell(file) == 8192 + 48);
    assert(fclose(file) == 0);
    gb->cartridge_ram[0] = 0x5a;
    gb->rtc.s = 11;
    cupid_gb_save(gb);
    memset(gb->cartridge_ram, 0, gb->cartridge_ram_size);
    memset(&gb->rtc, 0, sizeof(gb->rtc));
    cupid_gb_load_save(gb);
    assert(gb->cartridge_ram[0] == 0x5a && gb->rtc.s == 11 && gb->rtc.ls == 7);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, gb, sizeof(*before));
    file = fopen(gb->save_path, "wb");
    assert(file && fwrite("bad", 1, 3, file) == 3 && fclose(file) == 0);
    cupid_gb_load_save(gb);
    assert(memcmp(before, gb, sizeof(*gb)) == 0);
    assert(remove(gb->save_path) == 0);
    free(before);
    destroy(gb);
}

static void test_observed_fixture(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    uint8_t bytes[8192 + 48];
    assert(fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fgetc(file) == EOF && fclose(file) == 0);
    uint64_t encoded_time = 0;
    for (unsigned i = 0; i < 8; ++i)
        encoded_time |= (uint64_t)bytes[8232 + i] << (i * 8);
    assert(encoded_time <= INT64_MAX);
    int64_t timestamp = (int64_t)encoded_time;
    CupidGb *gb = cartridge(0x10, 2);
    assert(cupid_gb_load_battery_at(gb, bytes, sizeof(bytes), timestamp) == CUPID_GB_BATTERY_OK);
    assert(gb->cartridge_ram[0] == 0x5a);
    assert(gb->rtc.s == 57 && gb->rtc.ls == 56 && gb->rtc.m == 34 && gb->rtc.h == 12 &&
           gb->rtc.dl == 52 && gb->rtc.dh == 64 && gb->rtc.ldh == 64);
    uint8_t output[sizeof(bytes)];
    size_t written = 0;
    assert(cupid_gb_save_battery_at(gb, output, sizeof(output), &written, timestamp) ==
           CUPID_GB_BATTERY_OK);
    assert(written == sizeof(bytes) && memcmp(bytes, output, sizeof(bytes)) == 0);
    destroy(gb);
}

int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    test_raw_ram();
    test_write_generation();
    test_legacy_files();
    test_rtc_format();
    test_clock_advance();
    test_running_and_legacy_clock();
    if (argc == 2)
        test_observed_fixture(argv[1]);
    puts("Portable battery and RTC checks passed.");
    return 0;
}

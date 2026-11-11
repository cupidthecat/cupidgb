#include "cupid/common/sha256.h"
#include "cupid/gb/battery.h"
#include "cupid/gb/mbc7.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum {
    PORT = 0xa487, /* Address aliases omit bits 8–11 and 0–3. */
    SELECT = 0x80,
    CLOCK = 0x40,
    INPUT = 2,
    OUTPUT = 1,
    EWEN = 0x0c0,
    EWDS = 0x000,
    WRAL = 0x040,
    ERAL = 0x080,
    WRITE = 0x100,
    READ = 0x200,
    ERASE = 0x300,
    V4_SIZE = 300572,
};

static CupidGb *machine(CupidGbModel model) {
    const size_t size = 256u * 16384u;
    uint8_t *rom = calloc(1, size);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(rom && gb);
    for (unsigned bank = 0; bank < 256; ++bank)
        rom[bank * 16384u] = (uint8_t)bank;
    rom[0x143] = 0x80;
    rom[0x147] = 0x22;
    rom[0x148] = 7;
    rom[0x149] = 1; /* The built-in EEPROM ignores this legacy SRAM size. */
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, size));
    free(rom);
    return gb;
}

static void enable_registers(CupidGb *gb) {
    cupid_gb_write_u8(gb, 0x1fff, 0x0a);
    cupid_gb_write_u8(gb, 0x5fff, 0x40);
}

static unsigned pulse(CupidGb *gb, unsigned input) {
    const uint8_t low = (uint8_t)(SELECT | (input ? INPUT : 0));
    cupid_gb_write_u8(gb, PORT, low);
    cupid_gb_write_u8(gb, PORT, low | CLOCK);
    return cupid_gb_read_u8(gb, PORT) & OUTPUT;
}

static uint16_t shift(CupidGb *gb, unsigned value, unsigned count) {
    uint16_t output = 0;
    for (unsigned bit = count; bit > 0; --bit)
        output = (uint16_t)((unsigned)output * 2 + pulse(gb, (value >> (bit - 1)) & 1u));
    return output;
}

static void command(CupidGb *gb, unsigned value) {
    cupid_gb_write_u8(gb, PORT, 0);
    pulse(gb, 0);
    shift(gb, 0x400u | value, 11);
}

static uint16_t read_eeprom(CupidGb *gb, unsigned address) {
    command(gb, READ | address);
    return shift(gb, 0, 16);
}

static void test_map(CupidGb *gb) {
    assert(cupid_gb_battery_size(gb) == 256 && !gb->header.has_rumble);
    assert(cupid_gb_read_u8(gb, 0x4000) == 0);
    for (unsigned bank = 0; bank < 256; ++bank) {
        cupid_gb_write_u8(gb, 0x38ff, (uint8_t)bank);
        assert(cupid_gb_read_u8(gb, 0) == 0 && cupid_gb_read_u8(gb, 0x4000) == bank);
    }
    assert(cupid_gb_read_u8(gb, PORT) == 0xff);
    cupid_gb_write_u8(gb, 0x1fff, 0x1a);
    cupid_gb_write_u8(gb, 0x5fff, 0x40);
    assert(cupid_gb_read_u8(gb, PORT) == 0xff);
    cupid_gb_write_u8(gb, 0x5fff, 0xc0);
    cupid_gb_write_u8(gb, 0x1fff, 0x0a);
    assert(cupid_gb_read_u8(gb, PORT) == 0xff);
    enable_registers(gb);
    assert(cupid_gb_read_u8(gb, PORT) == 1 && cupid_gb_read_u8(gb, 0xb080) == 0xff);
    assert(cupid_gb_read_u8(gb, 0xa637) == 0x80 && cupid_gb_read_u8(gb, 0xaf60) == 0);
    cupid_gb_mbc7_set_motion(gb, 56, -112);
    cupid_gb_write_u8(gb, 0xb010, 0xaa);
    assert(cupid_gb_read_u8(gb, 0xa020) == 0);
    cupid_gb_write_u8(gb, 0xa417, 0xaa);
    assert(cupid_gb_read_u8(gb, 0xa22c) == 0x08 && cupid_gb_read_u8(gb, 0xa93e) == 0x82);
    assert(cupid_gb_read_u8(gb, 0xa242) == 0x60 && cupid_gb_read_u8(gb, 0xa352) == 0x81);
    cupid_gb_write_u8(gb, 0xa407, 0x55);
    assert(cupid_gb_read_u8(gb, 0xa020) == 0 && cupid_gb_read_u8(gb, 0xa030) == 0x80);
}

static void test_serial(CupidGb *gb) {
    assert(read_eeprom(gb, 0x12) == 0xffff);
    command(gb, EWEN);
    command(gb, WRITE | 0x92);
    shift(gb, 0x1234, 16);
    assert(shift(gb, 0, 3) == 1);
    assert(read_eeprom(gb, 0x12) == 0x1234);
    command(gb, EWDS);
    const uint64_t generation = gb->battery_write_generation;
    command(gb, ERASE | 0x12);
    assert(read_eeprom(gb, 0x12) == 0x1234);
    command(gb, WRITE | 0x12);
    shift(gb, 0, 16);
    assert(gb->battery_write_generation == generation && read_eeprom(gb, 0x12) == 0x1234);
    /* The observed serial shifter still sets input-one bits without the initial erase. */
    command(gb, WRITE | 0x12);
    shift(gb, 0x80c1, 16);
    assert(read_eeprom(gb, 0x12) == 0x92f5);
    command(gb, EWEN);
    command(gb, ERASE | 0x12);
    assert(shift(gb, 0, 3) == 1 && read_eeprom(gb, 0x12) == 0xffff);
    command(gb, WRAL);
    shift(gb, 0x45a6, 16);
    for (unsigned address = 0; address < 127; ++address)
        assert(read_eeprom(gb, address) == 0x45a6);
    assert(read_eeprom(gb, 127) == 0);
    command(gb, ERAL);
    assert(shift(gb, 0, 9) == 1);
    for (unsigned address = 0; address < 128; ++address)
        assert(read_eeprom(gb, address) == 0xffff);
}

static void test_battery(CupidGb *gb) {
    uint8_t legacy[2048], output[256];
    for (unsigned i = 0; i < sizeof(legacy); ++i)
        legacy[i] = (uint8_t)i;
    assert(cupid_gb_load_battery(gb, legacy, sizeof(legacy) - 1) == CUPID_GB_BATTERY_INVALID_SIZE);
    assert(read_eeprom(gb, 3) == 0xffff);
    assert(cupid_gb_load_battery(gb, legacy, sizeof(legacy)) == CUPID_GB_BATTERY_OK);
    assert(read_eeprom(gb, 3) == 0x0706);
    size_t written = 0;
    assert(cupid_gb_save_battery(gb, output, sizeof(output), &written) == CUPID_GB_BATTERY_OK);
    assert(written == sizeof(output) && memcmp(legacy, output, written) == 0);
}

static void rehash(uint8_t *bytes, size_t size) {
    cupid_sha256_parts(bytes, 96, bytes + 128, size - 128, bytes + 96);
}

static void test_resume(CupidGb *first, CupidGb *second) {
    command(first, EWEN);
    command(first, WRITE | 0x7f);
    shift(first, 0x6e, 8);
    const size_t size = cupid_gb_state_size(first);
    assert(size == V4_SIZE + 8 + 4);
    uint8_t *saved = malloc(size), *restored = malloc(size);
    assert(saved && restored);
    size_t written = 0;
    assert(cupid_gb_save_state(first, saved, size, &written) == CUPID_GB_STATE_OK &&
           written == size && saved[8] == 6);
    cupid_gb_mbc7_set_motion(second, -112, 112);
    assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_read_u8(first, PORT) == cupid_gb_read_u8(second, PORT));
    shift(first, 0xad, 8);
    shift(second, 0xad, 8);
    assert(read_eeprom(first, 0x7f) == 0x6ead && read_eeprom(second, 0x7f) == 0x6ead);
    assert(cupid_gb_save_state(first, saved, size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_save_state(second, restored, size, NULL) == CUPID_GB_STATE_OK);
    assert(memcmp(saved, restored, size) == 0);

    saved[V4_SIZE + 4] = 17;
    rehash(saved, size);
    assert(cupid_gb_load_state(second, saved, size) == CUPID_GB_STATE_INVALID);
    assert(cupid_gb_save_state(second, saved, size, NULL) == CUPID_GB_STATE_OK);
    assert(memcmp(saved, restored, size) == 0);

    saved[8] = 4;
    for (unsigned byte = 0; byte < 4; ++byte)
        saved[12 + byte] = (uint8_t)(V4_SIZE >> (8 * byte));
    rehash(saved, V4_SIZE);
    assert(cupid_gb_load_state(second, saved, V4_SIZE) == CUPID_GB_STATE_OK);
    assert(cupid_gb_read_u8(second, PORT) == 0xff);
    enable_registers(second);
    assert(cupid_gb_read_u8(second, PORT) == 1 && read_eeprom(second, 0x7f) == 0x6ead);
    cupid_gb_write_u8(second, 0xa010, 0xaa);
    assert(cupid_gb_read_u8(second, 0xa020) == 0x60);
    free(restored);
    free(saved);
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        CupidGb *first = machine(models[i]), *second = machine(models[i]);
        test_map(first);
        test_serial(first);
        test_battery(first);
        test_resume(first, second);
        cupid_gb_cleanup(first);
        cupid_gb_cleanup(second);
        free(first);
        free(second);
    }
}

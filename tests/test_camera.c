#include "cupid/gb/battery.h"
#include "cupid/gb/debugger.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void load_camera(CupidGb *gb, CupidGbModel model) {
    enum { ROM_SIZE = 8 * 0x4000 };

    uint8_t *rom = malloc(ROM_SIZE);
    assert(rom);
    for (unsigned bank = 0; bank < 8; ++bank)
        memset(rom + bank * 0x4000, (int)bank, 0x4000);
    rom[0x147] = 0xfc;
    rom[0x148] = 2;
    rom[0x149] = 4;
    uint8_t checksum = 0;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        checksum = (uint8_t)(checksum - rom[address] - 1u);
    rom[0x14d] = checksum;
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, ROM_SIZE));
    free(rom);
    assert(gb->header.mbc_type == CUPID_GB_CAMERA && gb->header.has_battery);
    assert(cupid_gb_battery_size(gb) == 0x20000);
}

static void test_banking(CupidGbModel model) {
    CupidGb gb = {0};
    load_camera(&gb, model);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
    cupid_gb_write_u8(&gb, 0x3fff, 0);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 0);
    cupid_gb_write_u8(&gb, 0x2000, 0x86);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 6);
    assert(cupid_gb_read_u8(&gb, 0x0000) == 0);

    cupid_gb_write_u8(&gb, 0x1fff, 0x1a);
    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0xa020) == 0xff);
        cupid_gb_write_u8(&gb, 0xa020, (uint8_t)(bank + 0x80u));
    }
    cupid_gb_write_u8(&gb, 0x0000, 0);
    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x5fff, (uint8_t)(bank | 0x80u));
        assert(cupid_gb_read_u8(&gb, 0xa020) == bank + 0x80u);
        cupid_gb_write_u8(&gb, 0xa020, 0xaa);
        assert(cupid_gb_read_u8(&gb, 0xa020) == bank + 0x80u);
    }
    cupid_gb_cleanup(&gb);
}

static void test_capture(CupidGbModel model) {
    CupidGb gb = {0};
    load_camera(&gb, model);
    cupid_gb_write_u8(&gb, 0x0000, 0x0a);
    cupid_gb_write_u8(&gb, 0x4000, 2);
    cupid_gb_write_u8(&gb, 0xa020, 0x51);
    cupid_gb_write_u8(&gb, 0x0000, 0);
    cupid_gb_write_u8(&gb, 0x4000, 0x10);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 0);
    cupid_gb_write_u8(&gb, 0xa001, 4);
    cupid_gb_write_u8(&gb, 0xa002, 1);
    cupid_gb_write_u8(&gb, 0xa003, 0);
    assert(cupid_gb_read_u8(&gb, 0xa001) == 0);
    for (unsigned cell = 0; cell < 16; ++cell) {
        uint16_t address = (uint16_t)(0xa006u + cell * 3u);
        cupid_gb_write_u8(&gb, address, 2);
        cupid_gb_write_u8(&gb, (uint16_t)(address + 1u), 4);
        cupid_gb_write_u8(&gb, (uint16_t)(address + 2u), 6);
    }

    cupid_gb_tick(&gb, 1);
    cupid_gb_write_u8(&gb, 0xa080, 0xff);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 7);
    assert(cupid_gb_debug_peek(&gb, 0xa000) == 7);
    assert(!cupid_gb_debug_poke(&gb, 0xa000, 0));
    cupid_gb_write_u8(&gb, 0xb000, 6);
    assert(cupid_gb_read_u8(&gb, 0xa080) == 7);
    gb.cpu.halted = true;
    cupid_gb_tick(&gb, 40000);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 7);
    gb.cpu.halted = false;

    cupid_gb_write_u8(&gb, 0x0000, 0x0a);
    cupid_gb_write_u8(&gb, 0x4000, 2);
    assert(cupid_gb_read_u8(&gb, 0xa020) == 0);
    cupid_gb_write_u8(&gb, 0xa020, 0x99);
    const size_t size = cupid_gb_state_size(&gb);
    uint8_t *before = malloc(size), *after = malloc(size), *replayed = malloc(size);
    assert(before && after && replayed);
    assert(cupid_gb_save_state(&gb, before, size, NULL) == CUPID_GB_STATE_OK && before[8] == 5);
    /* The first machine cycle leaves PHI in its longer shutter phase. */
    cupid_gb_tick(&gb, 37056);
    assert(cupid_gb_read_u8(&gb, 0xa020) == 0);
    cupid_gb_tick(&gb, 1);
    assert(cupid_gb_read_u8(&gb, 0xa020) == 0x51);
    cupid_gb_write_u8(&gb, 0x4000, 0x10);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 6);
    cupid_gb_write_u8(&gb, 0x4000, 0);
    /* Eight neighboring pixels exercise all four 2-bit output levels. */
    assert(cupid_gb_read_u8(&gb, 0xa100) == 0x9c);
    assert(cupid_gb_debug_peek(&gb, 0xa100) == 0x9c);
    assert(cupid_gb_read_u8(&gb, 0xa101) == 0xe0);
    assert(cupid_gb_read_u8(&gb, 0xaf00) == 0xff);
    assert(cupid_gb_save_state(&gb, after, size, NULL) == CUPID_GB_STATE_OK);

    assert(cupid_gb_load_state(&gb, before, size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_read_u8(&gb, 0xa020) == 0);
    cupid_gb_tick(&gb, 37057);
    cupid_gb_write_u8(&gb, 0x4000, 0);
    assert(cupid_gb_read_u8(&gb, 0xa100) == 0x9c);
    assert(cupid_gb_save_state(&gb, replayed, size, NULL) == CUPID_GB_STATE_OK);
    assert(memcmp(after, replayed, size) == 0);
    free(replayed);
    free(after);
    free(before);
    cupid_gb_cleanup(&gb);
}

int main(void) {
    test_banking(CUPID_GB_MODEL_DMG_ABC);
    test_banking(CUPID_GB_MODEL_CGB);
    test_capture(CUPID_GB_MODEL_DMG_ABC);
    test_capture(CUPID_GB_MODEL_CGB);
    test_capture(CUPID_GB_MODEL_SGB);
    puts("Camera banking, capture, and state regressions passed.");
    return 0;
}

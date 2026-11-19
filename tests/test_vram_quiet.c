/**
 * @file    test_vram_quiet.c
 * @brief   VRAM read bytes after quiet display rows and interrupted fetch batches.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "cupid/gb/gb.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct Profile {
    CupidGbModel model;
    bool color_cartridge;
} Profile;

typedef struct Fixture {
    CupidGb *gb;
    unsigned cycles;
    unsigned phase;
} Fixture;

static void write_byte(Fixture *fixture, uint16_t address, uint8_t value) {
    cupid_gb_write_u8(fixture->gb, address, value);
}

static Fixture create_fixture(Profile profile, unsigned type, unsigned scroll) {
    Fixture fixture = {0};
    fixture.gb = calloc(1, sizeof(*fixture.gb));
    assert(fixture.gb);
    uint8_t rom[32768] = {0};
    rom[0x143] = profile.color_cartridge ? 0x80 : 0;
    rom[0x147] = (uint8_t)type;
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[index] - 1u);
    cupid_gb_set_power_on_seed(fixture.gb, 0);
    cupid_gb_set_model(fixture.gb, profile.model);
    assert(cupid_gb_load_rom(fixture.gb, rom, sizeof(rom)));
    write_byte(&fixture, 0xff40, 0);
    write_byte(&fixture, 0xff04, 0);
    write_byte(&fixture, 0xff41, 0);
    write_byte(&fixture, 0xff42, 0);
    write_byte(&fixture, 0xff43, (uint8_t)scroll);
    write_byte(&fixture, 0xff45, 0);
    write_byte(&fixture, 0xff4a, 0);
    write_byte(&fixture, 0xff4b, 200);
    memset(fixture.gb->video_ram, 0, sizeof(fixture.gb->video_ram));
    for (unsigned index = 0; index < 160; ++index)
        write_byte(&fixture, (uint16_t)(0xfe00u + index), 0);
    for (unsigned row = 0; row < 8; ++row) {
        write_byte(&fixture, (uint16_t)(0x8000u + row * 2u), 0xff);
        write_byte(&fixture, (uint16_t)(0x9000u + row * 2u), 0xa5);
        write_byte(&fixture, (uint16_t)(0x9001u + row * 2u), 0x5a);
    }
    write_byte(&fixture, 0x8030, 0x5a);
    write_byte(&fixture, 0x87e0, 0xb6);
    assert(cupid_gb_read_bus_u8(fixture.gb, 0x87e0) == 0xb6);
    return fixture;
}

static void step(Fixture *fixture) {
    fixture->phase = cupid_gb_tick_color_dma_slot(fixture->gb, 1, fixture->phase);
    ++fixture->cycles;
}

static void check_history(Profile profile, unsigned type, unsigned scroll, bool high_tiles,
                          unsigned row, unsigned history) {
    static const uint16_t addresses[] = {0x8030, 0x9000, 0x9801};
    static const uint8_t monochrome[2][3] = {{0x5a, 0xff, 0}, {0x5a, 0xa5, 0}};
    static const uint8_t color[3][3] = {{0xff, 0xff, 0xff}, {0, 0, 0}, {0xb6, 0, 0}};
    static const uint8_t ordinary[3] = {0x5a, 0xa5, 0};
    const bool physical_color = profile.model == CUPID_GB_MODEL_CGB;
    Fixture fixture = create_fixture(profile, type, scroll);
    write_byte(&fixture, 0xff40, high_tiles ? 0x81 : 0x91);
    const unsigned edge = (physical_color ? 251u : 252u) + scroll + row * 454u;
    while (fixture.cycles < edge) {
        step(&fixture);
        /* History0 leaves both rows quiet. History1 synchronizes every dot.
         * History2 interrupts the first row, then leaves the second row quiet. */
        if (history == 1 || (history == 2 && fixture.cycles == 100u))
            (void)cupid_gb_read_bus_u8(fixture.gb, 0xff41);
    }

    /* Read VRAM before STAT: a display-register access could itself change the
     * history under test. The first HBlank byte depends on prior bus work. */
    const uint8_t *expected = physical_color ? color[history ? (high_tiles ? 2 : 1) : 0]
                                             : monochrome[history && high_tiles];
    for (unsigned index = 0; index < 3; ++index)
        assert(cupid_gb_read_bus_u8(fixture.gb, addresses[index]) == expected[index]);

    step(&fixture);
    for (unsigned index = 0; index < 3; ++index)
        assert(cupid_gb_read_bus_u8(fixture.gb, addresses[index]) == ordinary[index]);
    cupid_gb_cleanup(fixture.gb);
    free(fixture.gb);
}

int main(void) {
    static const Profile profiles[] = {
        {CUPID_GB_MODEL_DMG_ABC, false}, {CUPID_GB_MODEL_MGB, false}, {CUPID_GB_MODEL_SGB, false},
        {CUPID_GB_MODEL_SGB2, false},    {CUPID_GB_MODEL_CGB, true},  {CUPID_GB_MODEL_CGB, false},
    };
    for (unsigned profile = 0; profile < sizeof(profiles) / sizeof(*profiles); ++profile)
        for (unsigned type = 0; type < 2; ++type)
            for (unsigned scroll = 0; scroll < 8; scroll += 7)
                for (unsigned high_tiles = 0; high_tiles < 2; ++high_tiles)
                    for (unsigned row = 0; row < 2; ++row)
                        for (unsigned history = 0; history < 3; ++history)
                            check_history(profiles[profile], type, scroll, high_tiles != 0, row,
                                          history);
    return 0;
}

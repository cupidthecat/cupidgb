/**
 * @file    test_serial_helpers.h
 * @brief   Original cartridge fixtures and passive host observers for serial regressions.
 * @author  cupidthecat
 * @date    2026-11-17
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */
#ifndef CUPID_TEST_SERIAL_HELPERS_H
#define CUPID_TEST_SERIAL_HELPERS_H

#include "cupid/gb/gb.h"
#include "cupid/gb/startup.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct SerialWitness {
    unsigned launches, samples, completions;
    bool last_launch_fast, last_sample_fast;
    uint8_t transmitted;
} SerialWitness;

static void serial_test_launch(const CupidGb *gb, bool outgoing, void *context) {
    SerialWitness *witness = context;
    (void)outgoing;
    ++witness->launches;
    witness->last_launch_fast = gb->serial_fast_clock;
}

static bool serial_test_sample(const CupidGb *gb, void *context) {
    SerialWitness *witness = context;
    ++witness->samples;
    witness->last_sample_fast = gb->serial_fast_clock;
    return true;
}

static void serial_test_complete(const CupidGb *gb, uint8_t transmitted, void *context) {
    SerialWitness *witness = context;
    assert(!gb->serial_bits_remaining && !(gb->io_registers[2] & 0x80));
    assert(gb->interrupt_flags & 8);
    ++witness->completions;
    witness->transmitted = transmitted;
}

static void serial_test_observe(CupidGb *gb, SerialWitness *witness) {
    cupid_gb_set_serial_link(gb, (CupidGbSerialLink){.launch = serial_test_launch,
                                                     .sample = serial_test_sample,
                                                     .user_data = witness});
    cupid_gb_set_serial_output_callback(gb, serial_test_complete, witness);
}

static CupidGb *serial_test_new(CupidGbModel model, bool color_cartridge, bool boot,
                                uint8_t cartridge_type) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    uint8_t rom[32768] = {0};
    memcpy(rom + 0x134, "SERIAL TEST", 11);
    rom[0x143] = color_cartridge ? 0x80 : 0;
    rom[0x147] = cartridge_type;
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    if (boot) {
        memset(gb->boot_rom, 0, sizeof(gb->boot_rom));
        gb->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900u : 0x100u;
        cupid_gb_start_boot_rom(gb);
    }
    return gb;
}

static void serial_test_free(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

#endif

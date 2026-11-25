/**
 * @file    test_camera_helpers.h
 * @brief   Original camera fixtures and capture witnesses for core regressions.
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

#ifndef CUPID_TEST_CAMERA_HELPERS_H
#define CUPID_TEST_CAMERA_HELPERS_H

#include "cupid/gb/camera.h"
#include "cupid/gb/startup.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct CameraWitness {
    unsigned calls;
    uint64_t request;
    unsigned immediate;
    const uint8_t *pixels;
} CameraWitness;

static inline void camera_test_request(CupidGb *gb, uint64_t request, void *context) {
    CameraWitness *witness = context;
    assert(request && gb->camera_host.pending_request == request);
    assert((gb->camera.registers[0] & 1u) && !gb->camera.busy_tcycles);
    ++witness->calls;
    witness->request = request;
    if (witness->immediate) {
        const bool success = witness->immediate == 2;
        assert(cupid_gb_camera_complete(gb, request, success ? witness->pixels : NULL,
                                        success ? CUPID_GB_CAMERA_SENSOR_SIZE : 0));
    }
}

static inline CupidGb *camera_test_new(CupidGbModel model) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    uint8_t rom[32768] = {0};
    memcpy(rom + 0x134, "CAMERA HOST", 11);
    rom[0x147] = 0xfc;
    rom[0x149] = 4;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(gb->header.mbc_type == CUPID_GB_CAMERA);
    cupid_gb_write_u8(gb, 0, 0x0a);
    cupid_gb_write_u8(gb, 0x4000, 0x10);
    cupid_gb_write_u8(gb, 0xa001, 4);
    cupid_gb_write_u8(gb, 0xa002, 0x10);
    for (unsigned cell = 0; cell < 16; ++cell) {
        cupid_gb_write_u8(gb, (uint16_t)(0xa006 + cell * 3), 32);
        cupid_gb_write_u8(gb, (uint16_t)(0xa007 + cell * 3), 96);
        cupid_gb_write_u8(gb, (uint16_t)(0xa008 + cell * 3), 160);
    }
    return gb;
}

static inline uint64_t camera_test_begin(CupidGb *gb, unsigned mode) {
    cupid_gb_write_u8(gb, 0x4000, 0x10);
    cupid_gb_write_u8(gb, 0xa000, (uint8_t)((mode & 6u) | 1u));
    return gb->camera_host.pending_request;
}

static inline void camera_test_free(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

#endif

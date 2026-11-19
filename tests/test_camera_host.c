/**
 * @file    test_camera_host.c
 * @brief   Host camera request lifetime, retained pixels and transactional reset tests.
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

#include "cupid/gb/reset.h"
#include "test_camera_helpers.h"

#include <stdio.h>

static void assert_pixels(const CupidGb *gb, uint8_t value) {
    for (unsigned i = 0; i < CUPID_GB_CAMERA_SENSOR_SIZE; ++i)
        assert(gb->camera_host.image[i] == value);
}

static void test_pending_and_pixels(CupidGbModel model) {
    CupidGb *gb = camera_test_new(model);
    CameraWitness witness = {0};
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);
    assert_pixels(gb, 0);
    cupid_gb_write_u8(gb, 0x4000, 0);
    assert(cupid_gb_read_u8(gb, 0xa100) == 0xff);
    assert(cupid_gb_read_u8(gb, 0xa101) == 0xff);

    gb->cartridge_ram[0x20] = 0x5a;
    const uint64_t request = camera_test_begin(gb, 6);
    assert(request && witness.calls == 1 && witness.request == request);
    assert(gb->camera.capture_number == 1 && gb->camera.registers[0] == 7);
    cupid_gb_camera_tick(&gb->camera, CUPID_GB_CAMERA_MAX_BUSY_T_CYCLES);
    assert(gb->camera.registers[0] == 7 && !gb->camera.busy_tcycles);
    cupid_gb_write_u8(gb, 0xa000, 2);
    assert(gb->camera.registers[0] == 3 && witness.calls == 1);
    cupid_gb_write_u8(gb, 0xa000, 5);
    assert(gb->camera.registers[0] == 5 && witness.calls == 1);
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);
    assert(gb->camera_host.pending_request == request);

    cupid_gb_write_u8(gb, 0x4000, 0);
    assert(cupid_gb_read_u8(gb, 0xa020) == 0);
    cupid_gb_write_u8(gb, 0xa020, 0x99);
    assert(gb->cartridge_ram[0x20] == 0x5a);

    uint8_t pixels[CUPID_GB_CAMERA_SENSOR_SIZE];
    for (unsigned y = 0; y < CUPID_GB_CAMERA_SENSOR_HEIGHT; ++y)
        for (unsigned x = 0; x < CUPID_GB_CAMERA_SENSOR_WIDTH; ++x)
            pixels[y * CUPID_GB_CAMERA_SENSOR_WIDTH + x] = (uint8_t)((x & 3u) * 64u);
    assert(!cupid_gb_camera_complete(gb, 0, pixels, sizeof(pixels)));
    assert(!cupid_gb_camera_complete(gb, request + 1, pixels, sizeof(pixels)));
    assert(!cupid_gb_camera_complete(gb, request, pixels, sizeof(pixels) - 1));
    assert(!cupid_gb_camera_complete(gb, request, NULL, 1));
    assert_pixels(gb, 0);
    assert(gb->camera_host.pending_request == request);
    assert(cupid_gb_camera_complete(gb, request, pixels, sizeof(pixels)));
    assert(!gb->camera_host.pending_request && gb->camera.registers[0] == 4);
    assert(!memcmp(gb->camera_host.image, pixels, sizeof(pixels)));
    assert(cupid_gb_read_u8(gb, 0xa020) == 0x5a);
    /* Gray levels 0, 64, 128, 192 become shades 3, 2, 1, 0. */
    assert(cupid_gb_read_u8(gb, 0xa100) == 0xaa);
    assert(cupid_gb_read_u8(gb, 0xa101) == 0xcc);
    memset(pixels, 0xe1, sizeof(pixels));
    assert(!cupid_gb_camera_complete(gb, request, pixels, sizeof(pixels)));
    assert(cupid_gb_read_u8(gb, 0xa100) == 0xaa);

    const uint64_t next = camera_test_begin(gb, 0);
    assert(next > request && witness.calls == 2);
    assert(!cupid_gb_camera_complete(gb, request, pixels, sizeof(pixels)));
    assert(cupid_gb_camera_complete(gb, next, NULL, 0));
    cupid_gb_write_u8(gb, 0x4000, 0);
    assert(cupid_gb_read_u8(gb, 0xa100) == 0xaa);
    assert(cupid_gb_read_u8(gb, 0xa101) == 0xcc);
    camera_test_free(gb);
}

static void test_immediate(CupidGbModel model) {
    CupidGb *gb = camera_test_new(model);
    uint8_t pixels[CUPID_GB_CAMERA_SENSOR_SIZE];
    memset(pixels, 0x5c, sizeof(pixels));
    CameraWitness witness = {.immediate = 2, .pixels = pixels};
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);
    camera_test_begin(gb, 6);
    assert(witness.calls == 1 && !gb->camera_host.pending_request);
    assert(gb->camera.registers[0] == 6 && !gb->camera.busy_tcycles);
    assert_pixels(gb, 0x5c);
    witness.immediate = 1;
    camera_test_begin(gb, 2);
    assert(witness.calls == 2 && !gb->camera_host.pending_request);
    assert(gb->camera.registers[0] == 2 && !gb->camera.busy_tcycles);
    assert_pixels(gb, 0x5c);
    camera_test_free(gb);
}

static void test_image_inversion(void) {
    CupidGb *gb = camera_test_new(CUPID_GB_MODEL_CGB_E);
    uint8_t pixels[CUPID_GB_CAMERA_SENSOR_SIZE];
    for (unsigned index = 0; index < sizeof(pixels); ++index)
        pixels[index] = (uint8_t)((index & 3u) * 64u);
    CameraWitness witness = {.immediate = 2, .pixels = pixels};
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);

    const struct {
        uint8_t exposure_high, sensor_mode, low_plane, high_plane;
    } cases[] = {
        {0x10, 0x07, 0xaa, 0xcc},
        {0x10, 0x0f, 0x22, 0x11},
        {0x08, 0x00, 0x99, 0xee},
        {0x08, 0x08, 0x11, 0x00},
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        cupid_gb_write_u8(gb, 0x4000, 0x10);
        cupid_gb_write_u8(gb, 0xa002, cases[index].exposure_high);
        cupid_gb_write_u8(gb, 0xa004, cases[index].sensor_mode);
        camera_test_begin(gb, 2);
        assert(!gb->camera_host.pending_request && cupid_gb_read_u8(gb, 0xa000) == 2);
        cupid_gb_write_u8(gb, 0x4000, 0);
        /* Half exposure distinguishes signal inversion from inverting the input image. */
        for (uint16_t address = 0xa100; address < 0xaf00; address += 2) {
            assert(cupid_gb_read_u8(gb, address) == cases[index].low_plane);
            assert(cupid_gb_read_u8(gb, (uint16_t)(address + 1u)) == cases[index].high_plane);
        }
    }
    assert(witness.calls == sizeof(cases) / sizeof(cases[0]));
    assert(!memcmp(gb->camera_host.image, pixels, sizeof(pixels)));

    /* With 2-D edge gain 0.5, inverting the center and neighbors before the
     * signed filter yields 225 here. Inverting only the filtered result yields 227. */
    memset(pixels, 33, sizeof(pixels));
    pixels[CUPID_GB_CAMERA_SENSOR_WIDTH + 1] = 32;
    cupid_gb_write_u8(gb, 0x4000, 0x10);
    cupid_gb_write_u8(gb, 0xa001, 0xe4);
    cupid_gb_write_u8(gb, 0xa002, 0x10);
    cupid_gb_write_u8(gb, 0xa004, 0x08);
    for (uint16_t address = 0xa015; address < 0xa018; ++address)
        cupid_gb_write_u8(gb, address, 226);
    camera_test_begin(gb, 2);
    cupid_gb_write_u8(gb, 0x4000, 0);
    assert(cupid_gb_read_u8(gb, 0xa102) & 0x40);
    assert(cupid_gb_read_u8(gb, 0xa103) & 0x40);
    camera_test_free(gb);
}

static void test_registration_and_exhaustion(void) {
    CupidGb *gb = camera_test_new(CUPID_GB_MODEL_CGB);
    CameraWitness witness = {0};
    /* Attaching a provider cancels a generated exposure immediately. */
    camera_test_begin(gb, 4);
    assert(gb->camera.busy_tcycles && (gb->camera.registers[0] & 1u));
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);
    assert(!gb->camera.busy_tcycles && gb->camera.registers[0] == 4);
    const uint64_t first = camera_test_begin(gb, 0);
    cupid_gb_set_camera_callback(gb, NULL, NULL);
    assert(!gb->camera_host.pending_request && !gb->camera.registers[0]);
    assert(gb->camera_host.request_sequence == first);
    camera_test_begin(gb, 4);
    const CupidGbCamera generated = gb->camera;
    cupid_gb_set_camera_callback(gb, NULL, &witness);
    assert(!gb->camera_host.user_data);
    assert(!memcmp(&gb->camera, &generated, sizeof(generated)));
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);
    const uint64_t second = camera_test_begin(gb, 0);
    assert(second > first);
    assert(!cupid_gb_camera_complete(gb, first, NULL, 0));
    assert(cupid_gb_camera_complete(gb, second, NULL, 0));
    gb->camera_host.request_sequence = UINT64_MAX - 1;
    assert(camera_test_begin(gb, 4) == UINT64_MAX);
    assert(cupid_gb_camera_complete(gb, UINT64_MAX, NULL, 0));
    const unsigned calls = witness.calls;
    assert(!camera_test_begin(gb, 2));
    assert(witness.calls == calls && gb->camera_host.request_sequence == UINT64_MAX);
    assert(gb->camera.registers[0] == 2 && !gb->camera.busy_tcycles);
    cupid_gb_set_camera_callback(gb, NULL, NULL);
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);
    assert(!camera_test_begin(gb, 0) && witness.calls == calls);
    assert(gb->camera_host.request_sequence == UINT64_MAX);
    camera_test_free(gb);
}

static void test_transactions(CupidGbModel model) {
    CupidGb *gb = camera_test_new(model);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    uint8_t pixels[CUPID_GB_CAMERA_SENSOR_SIZE];
    memset(pixels, 0x73, sizeof(pixels));
    CameraWitness witness = {0};
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);
    uint64_t request = camera_test_begin(gb, 0);
    assert(cupid_gb_camera_complete(gb, request, pixels, sizeof(pixels)));
    request = camera_test_begin(gb, 4);
    *before = *gb;
    assert(!cupid_gb_reset(gb, (CupidGbResetKind)99));
    assert(!memcmp(gb, before, sizeof(*gb)));
    assert(!cupid_gb_load_rom(gb, NULL, 0));
    assert(!memcmp(gb, before, sizeof(*gb)));

    for (unsigned kind = 0; kind < 2; ++kind) {
        const unsigned calls = witness.calls;
        const uint64_t sequence = gb->camera_host.request_sequence;
        assert(cupid_gb_reset(gb, kind ? CUPID_GB_RESET_QUICK : CUPID_GB_RESET_HARD));
        assert(witness.calls == calls && gb->camera_host.request_callback == camera_test_request);
        assert(gb->camera_host.user_data == &witness && !gb->camera_host.pending_request);
        assert(gb->camera_host.request_sequence == sequence);
        assert(!gb->camera.busy_tcycles && !(gb->camera.registers[0] & 1u));
        assert_pixels(gb, 0x73);
        assert(!cupid_gb_camera_complete(gb, request, pixels, sizeof(pixels)));
        request = camera_test_begin(gb, 0);
        assert(request > sequence);
    }

    const uint64_t sequence = gb->camera_host.request_sequence;
    const unsigned calls = witness.calls;
    assert(cupid_gb_load_rom(gb, gb->rom, gb->rom_size));
    assert(witness.calls == calls && !gb->camera_host.pending_request);
    assert(gb->camera_host.request_sequence == sequence);
    assert(gb->camera_host.request_callback == camera_test_request);
    assert_pixels(gb, 0x73);
    assert(!cupid_gb_camera_complete(gb, request, pixels, sizeof(pixels)));
    assert(camera_test_begin(gb, 0) > sequence);
    free(before);
    camera_test_free(gb);
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB,
                                   CUPID_GB_MODEL_SGB2};
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        test_pending_and_pixels(models[i]);
        test_immediate(models[i]);
        test_transactions(models[i]);
    }
    test_image_inversion();
    test_registration_and_exhaustion();
    puts("Host camera request, pixel and reset regressions passed.");
    return 0;
}

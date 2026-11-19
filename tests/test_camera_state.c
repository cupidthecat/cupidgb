/**
 * @file    test_camera_state.c
 * @brief   Strict camera snapshot projection and transactional host-state import regressions.
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

#include "cupid/common/sha256.h"
#include "cupid/gb/state.h"
#include "test_camera_helpers.h"

#include <stdio.h>

typedef struct CameraBlob {
    uint8_t *data;
    size_t size;
} CameraBlob;

static CameraBlob capture(const CupidGb *gb, bool context_mode) {
    CameraBlob blob = {.size = cupid_gb_state_size(gb)};
    blob.data = malloc(blob.size);
    assert(blob.data && blob.size);
    size_t written = 0;
    CupidGbStateResult result;
    if (context_mode) {
        CupidGbStateContext context;
        assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
        result = cupid_gb_save_state_with_context(gb, blob.data, blob.size, &written, &context);
    } else {
        result = cupid_gb_save_state(gb, blob.data, blob.size, &written);
    }
    assert(result == CUPID_GB_STATE_OK && written == blob.size);
    assert(blob.data[8] == CUPID_GB_STATE_VERSION && !blob.data[9] && !blob.data[10] &&
           !blob.data[11]);
    return blob;
}

static CupidGbStateResult restore(CupidGb *gb, const CameraBlob *blob, unsigned variant) {
    CupidGbStateContext context;
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    switch (variant) {
        case 0:
            return cupid_gb_load_state(gb, blob->data, blob->size);
        case 1:
            return cupid_gb_load_state_with_context(gb, blob->data, blob->size, &context);
        case 2:
            return cupid_gb_load_state_live(gb, blob->data, blob->size);
        default:
            return cupid_gb_load_state_live_with_context(gb, blob->data, blob->size, &context);
    }
}

static size_t camera_offset(CupidGb *gb, const CameraBlob *original) {
    /* Locate the documented 64-byte section through one distinct register byte.
     * This avoids coupling the test to unrelated additions before that section. */
    gb->camera.registers[53] ^= 0x5a;
    CameraBlob changed = capture(gb, false);
    gb->camera.registers[53] ^= 0x5a;
    assert(changed.size == original->size);
    size_t difference = 0, count = 0;
    for (size_t i = 128; i < changed.size; ++i) {
        if (original->data[i] != changed.data[i]) {
            difference = i;
            ++count;
        }
    }
    free(changed.data);
    assert(count == 1 && difference >= 128 + 53);
    assert(difference - 53 + 64 <= original->size);
    return difference - 53;
}

static void refresh_integrity(CameraBlob *blob) {
    cupid_sha256_parts(blob->data, 96, blob->data + 128, blob->size - 128, blob->data + 96);
}

static void assert_host_image(const CupidGb *gb, uint8_t gray) {
    for (unsigned i = 0; i < CUPID_GB_CAMERA_SENSOR_SIZE; ++i)
        assert(gb->camera_host.image[i] == gray);
}

static void test_import_matrix(CupidGbModel model) {
    uint8_t pixels[CUPID_GB_CAMERA_SENSOR_SIZE];
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    for (unsigned source_host = 0; source_host < 2; ++source_host) {
        CupidGb *source = camera_test_new(model);
        cupid_gb_write_u8(source, 0xa002, 0);
        cupid_gb_write_u8(source, 0xa004, 0x08);
        CameraWitness source_witness = {0};
        if (source_host) {
            cupid_gb_set_camera_callback(source, camera_test_request, &source_witness);
            memset(pixels, 0x22, sizeof(pixels));
            const uint64_t first = camera_test_begin(source, 0);
            assert(cupid_gb_camera_complete(source, first, pixels, sizeof(pixels)));
        }
        camera_test_begin(source, 6);
        assert(source->camera.registers[0] == 7);
        const uint32_t countdown = source->camera.busy_tcycles;
        const uint32_t capture_number = source->camera.capture_number;
        *before = *source;
        CameraBlob blob = capture(source, false);
        assert(!memcmp(source, before, sizeof(*source)));
        CameraBlob cached = capture(source, true);
        assert(!memcmp(source, before, sizeof(*source)));
        assert(blob.size == cached.size && !memcmp(blob.data, cached.data, blob.size));
        free(cached.data);
        const size_t offset = camera_offset(source, &blob);
        assert(blob.data[offset] == (source_host ? 6 : 7));
        assert(blob.data[offset + 4] == 0x08);
        const uint32_t saved_countdown =
            (uint32_t)blob.data[offset + 56] | ((uint32_t)blob.data[offset + 57] << 8) |
            ((uint32_t)blob.data[offset + 58] << 16) | ((uint32_t)blob.data[offset + 59] << 24);
        assert(saved_countdown == countdown);

        for (unsigned receiving_host = 0; receiving_host < 2; ++receiving_host) {
            for (unsigned variant = 0; variant < 4; ++variant) {
                CupidGb *target = camera_test_new(model);
                CameraWitness target_witness = {0};
                uint64_t obsolete = 0;
                if (receiving_host) {
                    cupid_gb_set_camera_callback(target, camera_test_request, &target_witness);
                    memset(pixels, 0xa5, sizeof(pixels));
                    const uint64_t first = camera_test_begin(target, 0);
                    assert(cupid_gb_camera_complete(target, first, pixels, sizeof(pixels)));
                    obsolete = camera_test_begin(target, 2);
                } else {
                    camera_test_begin(target, 2);
                }
                const uint64_t sequence = target->camera_host.request_sequence;
                const unsigned calls = target_witness.calls;
                assert(restore(target, &blob, variant) == CUPID_GB_STATE_OK);
                assert(target->camera.capture_number == capture_number);
                assert(target->camera.registers[4] == 0x08);
                assert(target_witness.calls == calls);
                assert(target->camera_host.request_sequence == sequence);
                assert(!target->camera_host.pending_request);
                if (receiving_host) {
                    assert(target->camera.registers[0] == 6 && !target->camera.busy_tcycles);
                    assert(target->camera_host.request_callback == camera_test_request);
                    assert(target->camera_host.user_data == &target_witness);
                    assert_host_image(target, 0xa5);
                    memset(pixels, 0xee, sizeof(pixels));
                    assert(!cupid_gb_camera_complete(target, obsolete, pixels, sizeof(pixels)));
                    assert_host_image(target, 0xa5);
                    const uint64_t fresh = camera_test_begin(target, 0);
                    assert(fresh > sequence);
                    assert(!cupid_gb_camera_complete(target, obsolete, pixels, sizeof(pixels)));
                    assert_host_image(target, 0xa5);
                    assert(cupid_gb_camera_complete(target, fresh, NULL, 0));
                } else {
                    assert(!target->camera_host.request_callback);
                    assert(target->camera.registers[0] == (source_host ? 6 : 7));
                    assert(target->camera.busy_tcycles == countdown);
                    if (source_host) {
                        /* This file contains no portable host pixels or pending
                         * token. Its next capture uses the generated countdown. */
                        camera_test_begin(target, 0);
                        assert(target->camera.busy_tcycles);
                    }
                    cupid_gb_camera_tick(&target->camera, target->camera.busy_tcycles);
                }
                cupid_gb_write_u8(target, 0x4000, 0);
                /* Zero exposure gives a zero signal, which becomes 255 after inversion. */
                assert(cupid_gb_read_u8(target, 0xa100) == 0);
                assert(cupid_gb_read_u8(target, 0xaeff) == 0);
                camera_test_free(target);
            }
        }
        free(blob.data);
        camera_test_free(source);
    }
    free(before);
}

static void test_rejected_wire_preserves_host(CupidGbModel model) {
    CupidGb *source = camera_test_new(model);
    CameraBlob valid = capture(source, false);
    const size_t offset = camera_offset(source, &valid);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    uint8_t pixels[CUPID_GB_CAMERA_SENSOR_SIZE];
    memset(pixels, 0x87, sizeof(pixels));
    for (unsigned host = 0; host < 2; ++host) {
        for (unsigned variant = 0; variant < 4; ++variant) {
            CupidGb *target = camera_test_new(model);
            CameraWitness witness = {0};
            if (host) {
                cupid_gb_set_camera_callback(target, camera_test_request, &witness);
                uint64_t request = camera_test_begin(target, 0);
                assert(cupid_gb_camera_complete(target, request, pixels, sizeof(pixels)));
            }
            camera_test_begin(target, 2);
            *before = *target;
            const unsigned calls = witness.calls;
            for (unsigned fault = 0; fault < 6; ++fault) {
                CameraBlob damaged = {.data = malloc(valid.size), .size = valid.size};
                assert(damaged.data);
                memcpy(damaged.data, valid.data, valid.size);
                switch (fault) {
                    case 0:
                        damaged.data[offset] = 1; /* busy with no countdown */
                        break;
                    case 1:
                        damaged.data[offset + 56] = 1; /* countdown without busy */
                        break;
                    case 2:
                        damaged.data[offset] = 0x80; /* reserved shutter bit */
                        break;
                    case 3:
                        damaged.data[offset + 54] = 8; /* invalid PHI phase */
                        break;
                    case 4:
                        damaged.data[offset] = 1;
                        memset(damaged.data + offset + 56, 0xff, 4); /* unbounded countdown */
                        break;
                    default:
                        damaged.data[offset + 55] = 2; /* noncanonical boolean */
                        break;
                }
                refresh_integrity(&damaged);
                assert(restore(target, &damaged, variant) == CUPID_GB_STATE_INVALID);
                assert(!memcmp(target, before, sizeof(*target)));
                assert(witness.calls == calls);
                free(damaged.data);
            }
            CameraBlob truncated = {.data = valid.data, .size = valid.size - 1};
            assert(restore(target, &truncated, variant) == CUPID_GB_STATE_INVALID);
            assert(!memcmp(target, before, sizeof(*target)));
            if (host) {
                assert(
                    cupid_gb_camera_complete(target, target->camera_host.pending_request, NULL, 0));
                assert_host_image(target, 0x87);
            }
            camera_test_free(target);
        }
    }
    free(before);
    free(valid.data);
    camera_test_free(source);
}

static void test_invalid_live_projection(void) {
    CupidGb *gb = camera_test_new(CUPID_GB_MODEL_CGB);
    CameraWitness witness = {0};
    cupid_gb_set_camera_callback(gb, camera_test_request, &witness);
    camera_test_begin(gb, 0);
    CupidGb *valid = malloc(sizeof(*valid));
    CupidGb *before = malloc(sizeof(*before));
    assert(valid && before);
    *valid = *gb;
    const size_t size = cupid_gb_state_size(gb);
    uint8_t *data = malloc(size);
    assert(data);
    for (unsigned fault = 0; fault < 5; ++fault) {
        *gb = *valid;
        switch (fault) {
            case 0:
                gb->camera.busy_tcycles = 1;
                break;
            case 1:
                gb->camera_host.pending_request = 0;
                break;
            case 2:
                ++gb->camera_host.request_sequence;
                break;
            case 3:
                gb->camera.registers[0] |= 0x80;
                break;
            default:
                gb->camera_host.request_callback = NULL;
                break;
        }
        *before = *gb;
        size_t written = 77;
        assert(cupid_gb_save_state(gb, data, size, &written) == CUPID_GB_STATE_INVALID);
        assert(!written && !memcmp(gb, before, sizeof(*gb)));
    }
    *gb = *valid;
    *before = *gb;
    size_t written = 77;
    assert(cupid_gb_save_state(gb, data, size - 1, &written) == CUPID_GB_STATE_BUFFER_TOO_SMALL);
    assert(!written && !memcmp(gb, before, sizeof(*gb)));
    free(data);
    free(before);
    free(valid);
    camera_test_free(gb);
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB,
                                   CUPID_GB_MODEL_SGB2};
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        test_import_matrix(models[i]);
        test_rejected_wire_preserves_host(models[i]);
    }
    test_invalid_live_projection();
    puts("Camera snapshot projection and transactional import regressions passed.");
    return 0;
}

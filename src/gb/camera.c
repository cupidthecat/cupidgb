/**
 * @file    camera.c
 * @brief   Camera cartridge banking, exposure timing, and sensor-to-tile conversion.
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

#include "cupid/gb/camera.h"

#include "cartridge_internal.h"

#include <string.h>

enum {
    CAMERA_SHUTTER = 0,
    CAMERA_GAIN = 1,
    CAMERA_EXPOSURE_HIGH = 2,
    CAMERA_EXPOSURE_LOW = 3,
    CAMERA_SENSOR_MODE = 4,
    CAMERA_THRESHOLDS = 6
};

void cupid_gb_camera_cancel_request(CupidGb *gb) {
    if (!gb)
        return;
    gb->camera_host.pending_request = 0;
    if (gb->camera_host.request_callback) {
        gb->camera.busy_tcycles = 0;
        gb->camera.registers[CAMERA_SHUTTER] &= 0xfeu;
    }
}

void cupid_gb_set_camera_callback(CupidGb *gb, CupidGbCameraRequestCallback callback,
                                  void *user_data) {
    if (!callback)
        user_data = NULL;
    if (!gb ||
        (gb->camera_host.request_callback == callback && gb->camera_host.user_data == user_data))
        return;
    gb->camera_host.pending_request = 0;
    gb->camera.busy_tcycles = 0;
    gb->camera.registers[CAMERA_SHUTTER] &= 0xfeu;
    memset(gb->camera_host.image, 0, sizeof(gb->camera_host.image));
    gb->camera_host.request_callback = callback;
    gb->camera_host.user_data = user_data;
}

bool cupid_gb_camera_complete(CupidGb *gb, uint64_t request, const uint8_t *pixels, size_t size) {
    if (!gb || !gb->camera_host.request_callback || !request ||
        gb->camera_host.pending_request != request || gb->camera_host.request_sequence != request ||
        !(gb->camera.registers[CAMERA_SHUTTER] & 1u) || gb->camera.busy_tcycles ||
        (pixels ? size != CUPID_GB_CAMERA_SENSOR_SIZE : size != 0))
        return false;
    /* Publication and busy completion are one boundary operation. In particular,
     * an obsolete frame must not replace pixels before its token is rejected. */
    if (pixels)
        memmove(gb->camera_host.image, pixels, sizeof(gb->camera_host.image));
    cupid_gb_camera_cancel_request(gb);
    return true;
}

void cupid_gb_camera_mapper_write(CupidGb *gb, uint16_t address, uint8_t value) {
    if (address < 0x8000u && cupid_gb_has_startup_bank_alias(gb))
        gb->current_rom_bank = 0;
    if (address < 0x2000u) {
        gb->ram_enabled = (value & 0x0fu) == 0x0au;
    } else if (address < 0x4000u) {
        gb->mbc5_rom_bank = value;
        gb->current_rom_bank = value;
    } else if (address < 0x6000u) {
        gb->current_ram_bank = value & 0x0fu;
        gb->camera.mapped = (value & 0x10u) != 0u;
    }
}

void cupid_gb_camera_tick(CupidGbCamera *camera, unsigned cpu_tcycles) {
    camera->clock_phase = (uint8_t)((camera->clock_phase + cpu_tcycles) & 7u);
    if (!camera->busy_tcycles)
        return;
    if (camera->busy_tcycles > cpu_tcycles) {
        camera->busy_tcycles -= cpu_tcycles;
        return;
    }
    camera->busy_tcycles = 0;
    camera->registers[CAMERA_SHUTTER] &= 6u;
}

uint8_t cupid_gb_camera_read_register(const CupidGbCamera *camera, uint16_t address) {
    return (address & 0x7fu) == CAMERA_SHUTTER ? camera->registers[CAMERA_SHUTTER] : 0;
}

static unsigned exposure(const CupidGbCamera *camera) {
    return (unsigned)camera->registers[CAMERA_EXPOSURE_HIGH] * 256u +
           camera->registers[CAMERA_EXPOSURE_LOW];
}

void cupid_gb_camera_write_register(CupidGb *gb, uint16_t address, uint8_t value) {
    CupidGbCamera *camera = &gb->camera;
    unsigned reg = address & 0x7fu;
    if (reg >= sizeof(camera->registers))
        return;
    if (reg != CAMERA_SHUTTER) {
        camera->registers[reg] = value;
        return;
    }

    /* Mode writes do not cancel a capture. Host requests retain the busy latch
     * until completion; the generated sensor instead consumes PHI clocks. */
    const bool busy = (camera->registers[CAMERA_SHUTTER] & 1u) != 0;
    camera->registers[CAMERA_SHUTTER] = (uint8_t)((value & 6u) | busy);
    if (busy || !(value & 1u))
        return;
    ++camera->capture_number;
    if (gb->camera_host.request_callback) {
        /* Exhaustion behaves like a failed capture. Reusing an identity could
         * let an old device completion publish into a later guest timeline. */
        if (gb->camera_host.request_sequence == UINT64_MAX)
            return;
        const uint64_t request = ++gb->camera_host.request_sequence;
        gb->camera_host.pending_request = request;
        camera->registers[CAMERA_SHUTTER] |= 1u;
        gb->camera_host.request_callback(gb, request, gb->camera_host.user_data);
        /* Immediate success or failure may already have released the latch. */
        return;
    }
    unsigned setup = (camera->registers[CAMERA_GAIN] & 0x80u) ? 129792u : 131840u;
    camera->busy_tcycles = setup + 64u * exposure(camera) + (camera->clock_phase & 4u);
    camera->registers[CAMERA_SHUTTER] |= 1u;
}

/* The synthetic sensor uses only coordinates and the capture number. It can
 * reproduce a paused or restored exposure without a host camera or image. */
static int sensor_pixel(const CupidGbCamera *camera, const uint8_t *pixels, int x, int y) {
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x > 127)
        x = 127;
    if (y > 111)
        y = 111;

    unsigned gray;
    if (pixels) {
        gray = pixels[y * CUPID_GB_CAMERA_SENSOR_WIDTH + x];
    } else {
        unsigned coordinates = (unsigned)(13 * x + 17 * y);
        unsigned pattern = coordinates + camera->capture_number * 29u;
        gray = (pattern ^ (unsigned)(x * y / 4)) & 255u;
    }
    unsigned index = camera->registers[CAMERA_GAIN] & 31u;
    /* These Q15 coefficients preserve all 256 quantized sensor levels at each
     * gain setting. Truncate the gain stage before applying exposure. */
    static const uint16_t gain_q15[] = {28865, 29981, 30990, 31915, 32768, 33558, 34297, 34988,
                                        35638, 36832, 37909, 38888, 39785, 40619, 41758, 43112,
                                        44320, 45406, 46393, 47298, 48134, 48912, 49637, 50320,
                                        50958, 51562, 52133, 52681, 53200, 53695, 54169, 54635};
    gray = (gray * gain_q15[index]) >> 15u;
    const int exposed = (int)(gray * exposure(camera) / 4096u);
    /* A004 bit 3 inverts the exposed signal, including samples used by the edge filter. */
    return (camera->registers[CAMERA_SENSOR_MODE] & 0x08u) ? UINT8_MAX - exposed : exposed;
}

static unsigned sensor_level(const CupidGbCamera *camera, const uint8_t *pixels, int x, int y) {
    int gray = sensor_pixel(camera, pixels, x, y);
    if ((camera->registers[CAMERA_GAIN] & 0xe0u) == 0xe0u) {
        static const uint8_t edge_gain_quarters[] = {2, 3, 4, 5, 8, 12, 16, 20};
        unsigned index = (camera->registers[CAMERA_SENSOR_MODE] >> 4u) & 7u;
        const int strength = edge_gain_quarters[index];
        static const int8_t dx[] = {-1, 1, 0, 0};
        static const int8_t dy[] = {0, 0, -1, 1};
        gray *= 1 + strength;
        /* Each neighbor is a separate signed, quantized stage. Combining the
         * terms changes the shade near a threshold, especially below zero. */
        for (unsigned neighbor = 0; neighbor < 4; ++neighbor) {
            const int adjacent = sensor_pixel(camera, pixels, x + dx[neighbor], y + dy[neighbor]);
            gray = (4 * gray - adjacent * strength) / 4;
        }
    }
    unsigned cell = (unsigned)((y & 3) * 4 + (x & 3));
    const uint8_t *limits = &camera->registers[CAMERA_THRESHOLDS + cell * 3u];
    for (unsigned shade = 0; shade < 3; ++shade) {
        if (gray < limits[shade])
            return 3u - shade;
    }
    return 0;
}

uint8_t cupid_gb_camera_read_tile(const CupidGbCamera *camera, const uint8_t *pixels,
                                  unsigned offset) {
    unsigned tile = offset / 16u;
    int column = (int)((tile % 16u) * 8u);
    int row = (int)((tile / 16u) * 8u + (offset % 16u) / 2u);
    unsigned plane = offset & 1u;
    unsigned packed = 0;
    for (int pixel = 0; pixel < 8; ++pixel) {
        unsigned shade = sensor_level(camera, pixels, column + pixel, row);
        packed = (packed << 1u) | ((shade >> plane) & 1u);
    }
    return (uint8_t)packed;
}

#include "cupid/gb/camera.h"

enum {
    CAMERA_SHUTTER = 0,
    CAMERA_GAIN = 1,
    CAMERA_EXPOSURE_HIGH = 2,
    CAMERA_EXPOSURE_LOW = 3,
    CAMERA_EDGE = 4,
    CAMERA_THRESHOLDS = 6
};

void cupid_gb_camera_mapper_write(CupidGb *gb, uint16_t address, uint8_t value) {
    if (address < 0x2000u) {
        gb->ram_enabled = (value & 0x0fu) == 0x0au;
    } else if (address < 0x4000u) {
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

void cupid_gb_camera_write_register(CupidGbCamera *camera, uint16_t address, uint8_t value) {
    unsigned reg = address & 0x7fu;
    if (reg >= sizeof(camera->registers))
        return;
    if (reg != CAMERA_SHUTTER) {
        camera->registers[reg] = value;
        return;
    }

    /* A second shutter write can change the mode bits during a capture.
     * The busy latch clears when the camera has consumed its PHI clocks. */
    if (!camera->busy_tcycles && (value & 1u)) {
        unsigned setup = (camera->registers[CAMERA_GAIN] & 0x80u) ? 129792u : 131840u;
        camera->busy_tcycles = setup + 64u * exposure(camera) + (camera->clock_phase & 4u);
        ++camera->capture_number;
    }
    camera->registers[CAMERA_SHUTTER] = (uint8_t)((value & 6u) | (camera->busy_tcycles != 0));
}

/* The synthetic sensor uses only coordinates and the capture number. It can
 * reproduce a paused or restored exposure without a host camera or image. */
static int sensor_pixel(const CupidGbCamera *camera, int x, int y) {
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x > 127)
        x = 127;
    if (y > 111)
        y = 111;

    unsigned coordinates = (unsigned)(13 * x + 17 * y);
    unsigned pattern = coordinates + camera->capture_number * 29u;
    unsigned gray = (pattern ^ (unsigned)(x * y / 4)) & 255u;
    unsigned index = camera->registers[CAMERA_GAIN] & 31u;
    /* Approximate the analog gain in fixed point, with unity at index 4. */
    unsigned gain = index < 4 ? 224u + 8u * index : 256u + (171u * (index - 4u) + 13u) / 27u;
    gray = gray * gain / 256u;
    return (int)(gray * exposure(camera) / 4096u);
}

static unsigned sensor_level(const CupidGbCamera *camera, int x, int y) {
    int gray = sensor_pixel(camera, x, y);
    if ((camera->registers[CAMERA_GAIN] & 0xe0u) == 0xe0u) {
        static const uint8_t edge_gain_quarters[] = {2, 3, 4, 5, 8, 12, 16, 20};
        unsigned index = (camera->registers[CAMERA_EDGE] >> 4u) & 7u;
        int neighbors = sensor_pixel(camera, x - 1, y) + sensor_pixel(camera, x + 1, y) +
                        sensor_pixel(camera, x, y - 1) + sensor_pixel(camera, x, y + 1);
        gray += (4 * gray - neighbors) * edge_gain_quarters[index] / 4;
    }
    unsigned cell = (unsigned)((y & 3) * 4 + (x & 3));
    const uint8_t *limits = &camera->registers[CAMERA_THRESHOLDS + cell * 3u];
    for (unsigned shade = 0; shade < 3; ++shade) {
        if (gray < limits[shade])
            return 3u - shade;
    }
    return 0;
}

uint8_t cupid_gb_camera_read_tile(const CupidGbCamera *camera, unsigned offset) {
    unsigned tile = offset / 16u;
    int column = (int)((tile % 16u) * 8u);
    int row = (int)((tile / 16u) * 8u + (offset % 16u) / 2u);
    unsigned plane = offset & 1u;
    unsigned packed = 0;
    for (int pixel = 0; pixel < 8; ++pixel) {
        unsigned shade = sensor_level(camera, column + pixel, row);
        packed = (packed << 1u) | ((shade >> plane) & 1u);
    }
    return (uint8_t)packed;
}

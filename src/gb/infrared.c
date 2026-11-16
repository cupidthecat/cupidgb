/**
 * @file    infrared.c
 * @brief   Timed infrared sensing and host input/output callbacks for supported devices.
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

#include "cupid/gb/infrared.h"

#include "cupid/gb/huc3.h"

static bool cartridge_sensor(const CupidGb *gb) {
    return gb->header.mbc_type == CUPID_GB_HUC1 || gb->header.mbc_type == CUPID_GB_HUC3;
}

static bool sensor_available(const CupidGb *gb) {
    return (gb->model == CUPID_GB_MODEL_CGB && gb->cgb_mode) || cartridge_sensor(gb);
}

void cupid_gb_set_infrared_callback(CupidGb *gb, CupidGbInfraredCallback callback,
                                    void *user_data) {
    if (gb) {
        gb->infrared_callback = callback;
        gb->infrared_user_data = callback ? user_data : NULL;
    }
}

void cupid_gb_set_infrared_input(CupidGb *gb, bool active) {
    if (gb)
        gb->huc3_ir_input = active;
}

bool cupid_gb_infrared_output(const CupidGb *gb) {
    return gb && ((cartridge_sensor(gb) && gb->huc3.ir_output) ||
                  (gb->model == CUPID_GB_MODEL_CGB && (gb->io_registers[0x56] & 1u)));
}

bool cupid_gb_infrared_detected(const CupidGb *gb) {
    return gb && sensor_available(gb) && gb->huc3.ir_detected;
}

static void notify_output(CupidGb *gb, bool active) {
    if (gb->infrared_callback)
        gb->infrared_callback(gb, active, gb->elapsed_cycles, gb->infrared_user_data);
}

void cupid_gb_infrared_write_cartridge(CupidGb *gb, uint8_t value) {
    if (!gb || !cartridge_sensor(gb))
        return;
    const bool active = (value & 1u) != 0;
    if (active != gb->huc3.ir_output) {
        gb->huc3.ir_output = active;
        notify_output(gb, active);
    }
}

void cupid_gb_infrared_write_port(CupidGb *gb, uint8_t value) {
    if (!gb || gb->model != CUPID_GB_MODEL_CGB)
        return;
    const uint8_t changed = gb->io_registers[0x56] ^ value;
    /* This port exposes its old latch during the output-edge callback. */
    if (changed & 1u)
        notify_output(gb, (value & 1u) != 0);
    gb->io_registers[0x56] = value;
}

static uint16_t update_charge(uint16_t charge, uint64_t clocks, bool active, unsigned limit) {
    if (!active)
        return charge > clocks ? (uint16_t)(charge - clocks) : 0;
    const uint64_t next = charge + clocks;
    return (uint16_t)(next < limit ? next : limit);
}

static void tick_receiver(CupidGb *gb, uint64_t clocks) {
    enum { WARMUP_START = 32000, DECAY_START = 23000, NOISE_RANGE = 2048 };

    if (!gb || !sensor_available(gb))
        return;
    /* These fields retain their original snapshot location. The photodiode
     * timing is shared by cartridge receivers and the built-in color port. */
    CupidGbHuc3 *sensor = &gb->huc3;
    const uint8_t port = gb->io_registers[0x56];
    const bool sensing =
        (port & 0xc0u) == 0xc0u || (gb->header.mbc_type == CUPID_GB_HUC1 && gb->huc1_ir_mode) ||
        (gb->header.mbc_type == CUPID_GB_HUC3 && gb->huc3_mode == CUPID_GB_HUC3_INFRARED);
    if (!sensing && !sensor->ir_warmup && !sensor->ir_decay)
        return;
    sensor->ir_warmup =
        update_charge(sensor->ir_warmup, clocks, sensing, CUPID_GB_INFRARED_WARMUP_LIMIT);
    bool detected = sensing && (gb->huc3_ir_input || sensor->ir_output || (port & 1u)) &&
                    sensor->ir_warmup >= WARMUP_START;
    /* Threshold noise depends on the current divider and the last LCD wait,
     * including quiet batches interrupted by a guest display access. Unsigned
     * products preserve the low clock bits without signed overflow or shifts. */
    const uint32_t divider =
        (uint32_t)gb->io_registers[4] * 256u + (uint32_t)(gb->div_counter & 63u) * 4u;
    const uint32_t display = (uint32_t)gb->display_clock.credit_half_dots;
    const unsigned noise = (((divider + 9u) * 127u) ^ (display * 137u)) >> 3 & (NOISE_RANGE - 1u);
    if (sensor->ir_warmup < CUPID_GB_INFRARED_WARMUP_LIMIT && detected)
        detected = noise < (unsigned)sensor->ir_warmup - WARMUP_START;
    sensor->ir_decay =
        update_charge(sensor->ir_decay, clocks, detected, CUPID_GB_INFRARED_DECAY_LIMIT);
    if (sensor->ir_decay >= DECAY_START)
        detected = detected && sensor->ir_decay < DECAY_START + NOISE_RANGE &&
                   noise > (unsigned)sensor->ir_decay - DECAY_START;
    sensor->ir_detected = detected;
}

void cupid_gb_infrared_tick(CupidGb *gb, unsigned dots) {
    tick_receiver(gb, (uint64_t)dots * 2u);
}

void cupid_gb_infrared_tick_half_dots(CupidGb *gb, unsigned half_dots) {
    tick_receiver(gb, half_dots);
}

/**
 * @file    emulator.c
 * @brief   Core instance lifecycle, cartridge loading, and GB/GBC stepping.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * The public entry points operate on a CupidEmulator instance and delegate
 * Game Boy and Game Boy Color hardware operations to the core.
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

#include "cupid/core/emulator.h"

#include "cupid/common/log.h"

void cupid_emulator_init(CupidEmulator *emulator, CupidSystem target_system) {
    if (emulator == 0) {
        cupid_log_error("Cannot initialize emulator: null instance.");
        return;
    }

    emulator->target_system = target_system;
    emulator->initialized = true;

    if (target_system == CUPID_SYSTEM_GB || target_system == CUPID_SYSTEM_GBC) {
        if (target_system == CUPID_SYSTEM_GBC) {
            cupid_gb_set_model(&emulator->gb, CUPID_GB_MODEL_CGB);
        }
        cupid_gb_init(&emulator->gb);
    }

    cupid_log_info("CupidGB core initialized.");
}

void cupid_emulator_shutdown(CupidEmulator *emulator) {
    if (emulator == 0 || !emulator->initialized) {
        return;
    }

    cupid_gb_cleanup(&emulator->gb);
    emulator->initialized = false;
    cupid_log_info("CupidGB core shut down.");
}

bool cupid_emulator_load_rom(CupidEmulator *emulator, const unsigned char *rom_data,
                             size_t rom_size) {
    if (emulator == 0 || !emulator->initialized || rom_data == 0) {
        return false;
    }

    if (emulator->target_system == CUPID_SYSTEM_GB || emulator->target_system == CUPID_SYSTEM_GBC) {
        return cupid_gb_load_rom(&emulator->gb, rom_data, rom_size);
    }

    cupid_log_error("ROM loading is not implemented for this target system.");
    return false;
}

bool cupid_emulator_load_rom_file(CupidEmulator *emulator, const char *path) {
    if (emulator == 0 || !emulator->initialized || path == 0) {
        return false;
    }

    if (emulator->target_system == CUPID_SYSTEM_GB || emulator->target_system == CUPID_SYSTEM_GBC) {
        return cupid_gb_load_rom_file(&emulator->gb, path);
    }

    cupid_log_error("ROM file loading is not implemented for this target system.");
    return false;
}

bool cupid_emulator_step(CupidEmulator *emulator) {
    if (emulator == 0 || !emulator->initialized) {
        return false;
    }

    if (emulator->target_system == CUPID_SYSTEM_GB || emulator->target_system == CUPID_SYSTEM_GBC) {
        return cupid_gb_step(&emulator->gb);
    }

    cupid_log_error("Stepping is not implemented for this target system.");
    return false;
}

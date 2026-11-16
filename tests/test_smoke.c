/**
 * @file    test_smoke.c
 * @brief   Smoke test for emulator initialization, model selection, and shutdown.
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

#include <assert.h>

#include "cupid/core/emulator.h"
#include "cupid/core/system.h"

int main(void) {
    CupidEmulator emulator = {0};

    cupid_emulator_init(&emulator, CUPID_SYSTEM_GBC);

    assert(emulator.initialized);
    assert(emulator.target_system == CUPID_SYSTEM_GBC);
    assert(emulator.gb.model == CUPID_GB_MODEL_CGB);

    cupid_emulator_shutdown(&emulator);
    assert(!emulator.initialized);

    return 0;
}

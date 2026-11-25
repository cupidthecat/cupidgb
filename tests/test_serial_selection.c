/**
 * @file    test_serial_selection.c
 * @brief   Guest-visible serial selection, compatibility-mode readback and reset regressions.
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
#include "cupid/gb/timer.h"
#include "test_serial_helpers.h"

#include <stdio.h>

typedef struct GuestCase {
    uint8_t control, mode, early_sb, early_sc, early_if;
    bool reselect, final_fast;
} GuestCase;

static void emit(CupidGb *gb, size_t *pc, const uint8_t *bytes, size_t count) {
    assert(*pc >= 0x200 && *pc + count < 0x900);
    memcpy(gb->boot_rom + *pc, bytes, count);
    *pc += count;
}

static void guest_write(CupidGb *gb, size_t *pc, uint8_t port, uint8_t value) {
    const uint8_t bytes[] = {0x3e, value, 0xe0, port};
    emit(gb, pc, bytes, sizeof(bytes));
}

static void guest_sample(CupidGb *gb, size_t *pc, unsigned sample) {
    const uint8_t ports[] = {1, 2, 15, 4};
    for (unsigned index = 0; index < sizeof(ports); ++index) {
        const uint8_t bytes[] = {0xf0, ports[index], 0xe0, (uint8_t)(0x80 + 4 * sample + index)};
        emit(gb, pc, bytes, sizeof(bytes));
    }
}

static unsigned step_to(CupidGb *gb, size_t pc) {
    unsigned steps = 0;
    while (gb->cpu.pc != pc && steps++ < 1200)
        assert(cupid_gb_step(gb));
    assert(gb->cpu.pc == pc && steps < 1200);
    return steps;
}

static void test_guest_mode_transitions(void) {
    const GuestCase cases[] = {
        {0x83, 0, 0xff, 0x7f, 0xe8, false, true},  {0x83, 4, 0xff, 0x7f, 0xe8, false, true},
        {0x83, 4, 0x00, 0xff, 0xe0, true, false},  {0x02, 0, 0x00, 0x7e, 0xe0, false, true},
        {0x02, 4, 0x00, 0x7e, 0xe0, false, true},  {0x02, 4, 0x00, 0x7e, 0xe0, true, false},
        {0x00, 0, 0x00, 0x7c, 0xe0, false, false}, {0x00, 4, 0x00, 0x7c, 0xe0, false, false},
        {0x00, 4, 0x00, 0x7e, 0xe0, true, false},
    };
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const GuestCase *scenario = &cases[index];
        CupidGb *gb = serial_test_new(CUPID_GB_MODEL_CGB, true, true, 0);
        gb->boot_rom[0] = 0xc3;
        gb->boot_rom[1] = 0;
        gb->boot_rom[2] = 2;
        const uint8_t prefix[] = {0xf3, 0x31, 0xfe, 0xff, 0x3e, 0,    0xe0, 0xff, 0xe0, 0x0f,
                                  0xe0, 0x40, 0xe0, 2,    0xe0, 1,    0xe0, 4,    0x3e, 0x80,
                                  0xe0, 0x26, 0x3e, 0,    0xe0, 0x0f, 0xe0, 4};
        size_t pc = 0x200;
        emit(gb, &pc, prefix, sizeof(prefix));
        guest_write(gb, &pc, 2, scenario->control);
        guest_write(gb, &pc, 0x4c, scenario->mode);
        if (scenario->reselect) {
            if (scenario->control & 0x80) {
                /* Abort first: the SC write can sample the old high phase.
                 * Clear that byte before measuring a fresh slow transfer. */
                guest_write(gb, &pc, 2, 0);
                const uint8_t clear[] = {0xe0, 1, 0xe0, 15, 0xe0, 4};
                emit(gb, &pc, clear, sizeof(clear));
            }
            guest_write(gb, &pc, 2, scenario->control);
        }
        pc += 48; /* Exactly 192 CPU T-cycles of initialized NOP firmware. */
        guest_sample(gb, &pc, 0);
        const size_t early = pc;
        pc += 1024; /* A full slow-byte interval before the second sample. */
        guest_sample(gb, &pc, 1);
        unsigned steps = step_to(gb, early);
        const uint8_t expected_early[] = {scenario->early_sb, scenario->early_sc,
                                          scenario->early_if, 1};
        assert(memcmp(gb->high_ram, expected_early, sizeof(expected_early)) == 0);
        steps += step_to(gb, pc);
        const bool active = (scenario->control & 0x80) != 0;
        const uint8_t expected_late[] = {active ? 0xff : 0, active ? 0x7f : scenario->early_sc,
                                         active ? 0xe8 : 0xe0, 17};
        assert(memcmp(gb->high_ram + 4, expected_late, sizeof(expected_late)) == 0);
        const unsigned extra_steps = scenario->reselect ? (active ? 7u : 2u) : 0u;
        const unsigned extra_clocks = scenario->reselect ? (active ? 76u : 20u) : 0u;
        assert(steps == 1107u + extra_steps && gb->elapsed_cycles == 4684u + extra_clocks);
        assert(gb->serial_fast_clock == scenario->final_fast);
        assert(gb->cgb_mode == !scenario->mode && gb->boot_rom_enabled && !gb->double_speed);
        serial_test_free(gb);
    }
}

static void test_idle_selection_and_rewrite_order(void) {
    CupidGb *gb = serial_test_new(CUPID_GB_MODEL_CGB, true, true, 0);
    SerialWitness witness = {0};
    serial_test_observe(gb, &witness);
    assert(cupid_gb_read_u8(gb, 0xff02) == 0x7e && !gb->serial_fast_clock);
    assert(cupid_gb_step(gb) && cupid_gb_step(gb));
    assert(!gb->serial_master_clock && !witness.launches && !witness.samples);
    cupid_gb_write_u8(gb, 0xff02, 2);
    assert(gb->serial_fast_clock && !gb->serial_bits_remaining);
    cupid_gb_write_u8(gb, 0xff04, 0);
    cupid_gb_tick(gb, 2);
    assert(gb->serial_master_clock && !witness.launches && !witness.samples);
    cupid_gb_write_u8(gb, 0xff02, 0);
    assert(!gb->serial_fast_clock && !gb->serial_master_clock);

    cupid_gb_write_u8(gb, 0xff04, 0);
    cupid_gb_write_u8(gb, 0xff01, 0);
    cupid_gb_write_u8(gb, 0xff02, 0x83);
    cupid_gb_tick(gb, 2);
    assert(gb->serial_master_clock && witness.launches == 1);
    cupid_gb_write_u8(gb, 0xff02, 0x81);
    assert(witness.samples == 1 && witness.last_sample_fast);
    assert(witness.launches == 3 && !witness.last_launch_fast);
    assert(!gb->serial_fast_clock && gb->serial_bits_remaining == 7);
    assert(cupid_gb_read_u8(gb, 0xff01) == 1);
    serial_test_free(gb);
}

static void test_div_reset_unmap_and_speed(void) {
    for (unsigned speed = 0; speed < 2; ++speed) {
        CupidGb *gb = serial_test_new(CUPID_GB_MODEL_CGB, true, true, 0);
        cupid_gb_write_u8(gb, 0xff02, 0);
        cupid_gb_write_u8(gb, 0xff04, 0);
        cupid_gb_write_u8(gb, 0xff01, 0);
        cupid_gb_write_u8(gb, 0xff02, 0x83);
        cupid_gb_write_u8(gb, 0xff4c, 4);
        cupid_gb_write_u8(gb, 0xff50, 1);
        gb->double_speed = speed != 0;
        assert(gb->serial_fast_clock && !gb->cgb_mode && !gb->boot_rom_enabled);
        cupid_gb_tick(gb, 3);
        assert(gb->io_registers[1] == 0 && gb->serial_master_clock);
        cupid_gb_write_u8(gb, 0xff04, 0);
        assert(gb->serial_fast_clock && gb->io_registers[1] == 1);
        cupid_gb_tick(gb, 28);
        assert(gb->io_registers[1] == 0xff && !(gb->io_registers[2] & 0x80));
        assert(gb->interrupt_flags & 8);
        serial_test_free(gb);
    }

    CupidGb *gb = serial_test_new(CUPID_GB_MODEL_CGB, true, true, 0);
    gb->boot_rom[0] = 0x10;
    cupid_gb_write_u8(gb, 0xff02, 2);
    for (unsigned change = 0; change < 2; ++change) {
        const bool before = gb->double_speed;
        gb->cpu.pc = 0;
        cupid_gb_write_u8(gb, 0xff4d, 1);
        assert(cupid_gb_step(gb));
        unsigned guard = 0;
        while (gb->double_speed == before && guard++ < 40000) {
            assert(cupid_gb_step(gb));
            assert(gb->serial_fast_clock);
        }
        assert(guard < 40000 && gb->double_speed != before && gb->serial_fast_clock);
        /* Let any remaining speed-switch hold finish before the next STOP. */
        while ((gb->speed_switch_halt_cycles || gb->speed_switch_toggle_cycles ||
                gb->speed_switch_freeze_cycles) &&
               guard++ < 80000)
            assert(cupid_gb_step(gb));
        assert(guard < 80000);
    }
    serial_test_free(gb);
}

static void test_reset_and_approximate_handoff(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0,
                                   CUPID_GB_MODEL_MGB,     CUPID_GB_MODEL_CGB,
                                   CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned color = 0; color < 2; ++color) {
            CupidGb *gb = serial_test_new(models[model], color != 0, false, 0);
            assert(!gb->serial_fast_clock && cupid_gb_read_u8(gb, 0xff02) == 0x7e);
            assert(gb->cgb_mode == (models[model] == CUPID_GB_MODEL_CGB && color != 0));
            serial_test_free(gb);
        }
    }
    for (unsigned boot = 0; boot < 2; ++boot) {
        for (unsigned kind = 0; kind < 2; ++kind) {
            CupidGb *gb = serial_test_new(CUPID_GB_MODEL_CGB, true, boot != 0, 0);
            SerialWitness witness = {0};
            serial_test_observe(gb, &witness);
            cupid_gb_write_u8(gb, 0xff02, 2);
            assert(gb->serial_fast_clock);
            assert(cupid_gb_reset(gb, kind ? CUPID_GB_RESET_QUICK : CUPID_GB_RESET_HARD));
            assert(!gb->serial_fast_clock && !gb->serial_master_clock);
            assert(cupid_gb_read_u8(gb, 0xff02) == 0x7e);
            assert(gb->cpu.pc == (boot ? 0 : 0x100));
            assert(!witness.launches && !witness.samples && !witness.completions);
            assert(gb->serial_link.user_data == &witness &&
                   gb->serial_output_user_data == &witness);
            serial_test_free(gb);
        }
    }
    for (unsigned entry = 0; entry < 3; ++entry) {
        CupidGb *gb = serial_test_new(CUPID_GB_MODEL_CGB, true, true, 0);
        SerialWitness witness = {0};
        serial_test_observe(gb, &witness);
        cupid_gb_write_u8(gb, 0xff02, 2);
        if (entry == 0)
            cupid_gb_init(gb);
        else if (entry == 1)
            cupid_gb_enter_boot_rom(gb);
        else
            cupid_gb_start_boot_rom(gb);
        assert(!gb->serial_fast_clock && !gb->serial_master_clock);
        assert(!witness.launches && !witness.samples && !witness.completions);
        serial_test_free(gb);
    }
}

static void test_physical_read_masks(void) {
    for (unsigned color = 0; color < 2; ++color) {
        CupidGb *gb = serial_test_new(color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC,
                                      color != 0, false, 0);
        for (unsigned mode = 0; mode < 2; ++mode) {
            gb->cgb_mode = color && mode;
            for (unsigned raw = 0; raw < 256; ++raw) {
                gb->io_registers[2] = (uint8_t)raw;
                assert(cupid_gb_read_u8(gb, 0xff02) == (uint8_t)(raw | (color ? 0x7c : 0x7e)));
                assert(gb->io_registers[2] == raw);
            }
        }
        serial_test_free(gb);
    }
}

int main(void) {
    test_guest_mode_transitions();
    test_idle_selection_and_rewrite_order();
    test_div_reset_unmap_and_speed();
    test_reset_and_approximate_handoff();
    test_physical_read_masks();
    puts("Serial selection checks passed.");
    return 0;
}

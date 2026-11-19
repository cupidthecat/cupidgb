/**
 * @file    test_link.c
 * @brief   Guest link exchanges, physical clock interleave, and connection lifetime regressions.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "cupid/gb/infrared.h"
#include "cupid/gb/link.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/sgb_startup.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CupidGb *machine(CupidGbModel model, bool double_speed, const uint8_t *program,
                        size_t length) {
    uint8_t rom[0x8000] = {0};
    const uint8_t idle[] = {0x18, 0xfe};
    rom[0x100] = 0xc3;
    rom[0x101] = 0x50;
    rom[0x102] = 1;
    rom[0x143] = 0x80;
    memcpy(rom + 0x150, program ? program : idle, program ? length : sizeof(idle));
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->double_speed = double_speed;
    if (model == CUPID_GB_MODEL_CGB)
        gb->io_registers[0x4d] = double_speed ? 0x80 : 0;
    cupid_gb_write_u8(gb, 0xff0f, 0);
    cupid_gb_write_u8(gb, 0xc000, 0);
    cupid_gb_write_u8(gb, 0xc001, 0);
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static CupidGb *transfer_machine(CupidGbModel model, bool double_speed, uint8_t data,
                                 uint8_t control, bool delayed) {
    /* The clock source waits long enough for the slower peer to arm its input.
     * Both programs then poll SC and publish their received byte through WRAM. */
    const uint8_t program[] = {0x06,    delayed ? 64 : 1,
                               0x05,    0x20,
                               0xfd,    0x3e,
                               0,       0xe0,
                               0x0f,    0x3e,
                               data,    0xe0,
                               0x01,    0x3e,
                               control, 0xe0,
                               0x02,    0xf0,
                               0x02,    0xe6,
                               0x80,    0x20,
                               0xfa,    0xf0,
                               0x01,    0xea,
                               0x00,    0xc0,
                               0x3e,    1,
                               0xea,    0x01,
                               0xc0,    0x18,
                               0xfe};
    return machine(model, double_speed, program, sizeof(program));
}

static bool completed(const CupidGb *gb) {
    return cupid_gb_read_u8(gb, 0xc001) == 1;
}

static void finish_exchange(CupidGbLinkPair *pair) {
    for (unsigned step = 0; step < 20000; ++step) {
        if (completed(pair->endpoints[0]) && completed(pair->endpoints[1]))
            return;
        assert(cupid_gb_link_step(pair, NULL));
    }
    assert(!"Guest serial exchange did not complete");
}

static void test_guest_transfers(void) {
    static const struct {
        CupidGbModel model[2];
        bool double_speed[2];
    } cases[] = {
        {{CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG_ABC}, {false, false}},
        {{CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_DMG_ABC}, {false, false}},
        {{CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB}, {false, false}},
        {{CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_CGB}, {false, false}},
        {{CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_CGB}, {true, false}},
        {{CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_CGB}, {false, true}},
        {{CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_CGB}, {true, true}},
    };

    for (unsigned profile = 0; profile < sizeof(cases) / sizeof(cases[0]); ++profile) {
        for (unsigned source = 0; source < 2; ++source) {
            for (unsigned fast = 0; fast < 2; ++fast) {
                if (fast && cases[profile].model[source] != CUPID_GB_MODEL_CGB)
                    continue;
                CupidGb *ends[2];
                for (unsigned side = 0; side < 2; ++side) {
                    const uint8_t control =
                        (uint8_t)(0x80u | (side == source ? 1u : 0u) | (fast ? 2u : 0u));
                    ends[side] = transfer_machine(cases[profile].model[side],
                                                  cases[profile].double_speed[side],
                                                  side ? 0x3c : 0xa5, control, side == source);
                }
                CupidGbLinkPair pair = {0};
                assert(cupid_gb_link_attach(&pair, ends[0], ends[1]));
                finish_exchange(&pair);
                assert(cupid_gb_read_u8(ends[0], 0xc000) == 0x3c);
                assert(cupid_gb_read_u8(ends[1], 0xc000) == 0xa5);
                for (unsigned side = 0; side < 2; ++side) {
                    assert(!(cupid_gb_read_u8(ends[side], 0xff02) & 0x80));
                    assert(cupid_gb_read_u8(ends[side], 0xff0f) & 8);
                }
                cupid_gb_link_detach(&pair);
                release(ends[0]);
                release(ends[1]);
            }
        }
    }
}

static void test_clock_sources(void) {
    CupidGb *first = transfer_machine(CUPID_GB_MODEL_DMG_ABC, false, 0x69, 0x80, false);
    CupidGb *second = transfer_machine(CUPID_GB_MODEL_CGB, true, 0x96, 0x80, false);
    CupidGbLinkPair pair = {0};
    assert(cupid_gb_link_attach(&pair, first, second));
    for (unsigned step = 0; step < 5000; ++step)
        assert(cupid_gb_link_step(&pair, NULL));
    assert(!completed(first) && !completed(second));
    assert(first->serial_bits_remaining == 8 && second->serial_bits_remaining == 8);
    assert(cupid_gb_read_u8(first, 0xff01) == 0x69 && cupid_gb_read_u8(second, 0xff01) == 0x96);
    assert(!(first->interrupt_flags & 8) && !(second->interrupt_flags & 8));
    cupid_gb_write_u8(first, 0xff02, 0x81);
    finish_exchange(&pair);
    assert(cupid_gb_read_u8(first, 0xc000) == 0x96 && cupid_gb_read_u8(second, 0xc000) == 0x69);
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);

    first = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    second = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    pair = (CupidGbLinkPair){0};
    assert(cupid_gb_link_attach(&pair, first, second));
    cupid_gb_write_u8(first, 0xff01, 0);
    cupid_gb_write_u8(second, 0xff01, 0);
    cupid_gb_write_u8(first, 0xff02, 0x81);
    cupid_gb_write_u8(second, 0xff02, 0x81);
    for (unsigned step = 0; step < 2000; ++step) {
        assert(cupid_gb_link_step(&pair, NULL));
        if (first->serial_bits_remaining <= 5 && second->serial_bits_remaining <= 5)
            break;
    }
    /* An internally clocked peer drives high and ignores the other clock.
     * Neither transfer may shift twice per edge by also acting as a slave. */
    assert(first->serial_bits_remaining == 5 && second->serial_bits_remaining == 5);
    assert((cupid_gb_read_u8(first, 0xff01) & 7) == 7);
    assert((cupid_gb_read_u8(second, 0xff01) & 7) == 7);
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);
}

static void test_mid_transfer_attachment(void) {
    CupidGb *first = machine(CUPID_GB_MODEL_CGB, false, NULL, 0);
    CupidGb *second = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    cupid_gb_write_u8(first, 0xff01, 0xff);
    cupid_gb_write_u8(first, 0xff02, 0x81);
    cupid_gb_write_u8(second, 0xff01, 0);
    cupid_gb_write_u8(second, 0xff02, 0x80);
    CupidGbLinkPair pair = {0};
    assert(cupid_gb_link_attach(&pair, first, second));
    assert(!pair.outgoing[0]);
    for (unsigned step = 0; first->serial_bits_remaining && step < 5000; ++step)
        assert(cupid_gb_link_step(&pair, NULL));
    assert(!first->serial_bits_remaining && !second->serial_bits_remaining);
    /* Attachment cannot recover a launch that happened before its callback.
     * The initial low host latch replaces only that first outgoing one bit. */
    assert(cupid_gb_read_u8(second, 0xff01) == 0x7f);
    assert(cupid_gb_read_u8(first, 0xff01) == 0);
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);
}

static void test_native_latch_lifetime(void) {
    CupidGb *first = machine(CUPID_GB_MODEL_CGB, false, NULL, 0);
    CupidGb *second = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    CupidGbLinkPair pair = {0};
    assert(cupid_gb_link_attach(&pair, first, second));
    cupid_gb_write_u8(second, 0xff01, 0);
    cupid_gb_write_u8(second, 0xff02, 0x80);
    cupid_gb_write_u8(first, 0xff01, 0);
    cupid_gb_write_u8(first, 0xff02, 0x81);
    const size_t length = cupid_gb_state_size(first);
    uint8_t *saved = malloc(length);
    assert(saved && cupid_gb_save_state(first, saved, length, NULL) == CUPID_GB_STATE_OK);
    cupid_gb_write_u8(first, 0xff01, 0x80);
    cupid_gb_write_u8(first, 0xff02, 0x81);
    assert(pair.outgoing[0]);
    assert(cupid_gb_load_state(first, saved, length) == CUPID_GB_STATE_OK);
    assert(cupid_gb_read_u8(first, 0xff01) == 0);
    cupid_gb_link_rebase(&pair);
    assert(pair.outgoing[0]);
    for (unsigned step = 0; first->serial_bits_remaining == 8 && step < 1000; ++step)
        assert(cupid_gb_link_step(&pair, NULL));
    assert(first->serial_bits_remaining == 7 && second->serial_bits_remaining == 7);
    assert((cupid_gb_read_u8(second, 0xff01) & 1) == 1);
    free(saved);
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);
}

static void test_detach_during_transfer(void) {
    CupidGb *first = transfer_machine(CUPID_GB_MODEL_DMG_ABC, false, 0xa5, 0x81, true);
    CupidGb *second = transfer_machine(CUPID_GB_MODEL_CGB, false, 0x3c, 0x80, false);
    CupidGbLinkPair pair = {0};
    assert(cupid_gb_link_attach(&pair, first, second));
    for (unsigned step = 0; step < 5000; ++step) {
        assert(cupid_gb_link_step(&pair, NULL));
        if (first->serial_bits_remaining == 5 && second->serial_bits_remaining == 5)
            break;
    }
    const unsigned remaining = first->serial_bits_remaining;
    assert(remaining == 5 && second->serial_bits_remaining == 5);
    const uint8_t expected = (uint8_t)((unsigned)cupid_gb_read_u8(first, 0xff01) << remaining |
                                       ((1u << remaining) - 1u));
    const uint8_t slave_data = cupid_gb_read_u8(second, 0xff01);
    const bool launched = pair.outgoing[0];
    cupid_gb_link_detach(&pair);
    assert(pair.outgoing[0] == launched && cupid_gb_link_next(&pair) == -1);
    for (unsigned step = 0; step < 5000 && !completed(first); ++step) {
        assert(cupid_gb_step(first));
        assert(cupid_gb_step(second));
    }
    assert(completed(first) && cupid_gb_read_u8(first, 0xc000) == expected);
    assert(!completed(second) && second->serial_bits_remaining == remaining);
    assert(cupid_gb_read_u8(second, 0xff01) == slave_data);
    assert(cupid_gb_link_attach(&pair, first, second));
    assert(pair.outgoing[0] == launched && pair.clock_balance == 0);
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);
}

static void test_rebase_skew(void) {
    for (unsigned side = 0; side < 2; ++side) {
        CupidGb *ends[] = {machine(CUPID_GB_MODEL_CGB, false, NULL, 0),
                           machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0)};
        CupidGbLinkPair pair = {0};
        assert(cupid_gb_link_attach(&pair, ends[0], ends[1]));
        const size_t length = cupid_gb_state_size(ends[side]);
        uint8_t *saved = malloc(length);
        assert(saved && cupid_gb_save_state(ends[side], saved, length, NULL) == CUPID_GB_STATE_OK);
        assert(cupid_gb_step(ends[side]) && cupid_gb_link_advance(&pair, side));
        const int64_t skew = pair.clock_balance;
        assert(side ? skew < 0 : skew > 0);
        const int selected = cupid_gb_link_next(&pair);
        assert(selected == (int)(1u - side));
        assert(cupid_gb_load_state(ends[side], saved, length) == CUPID_GB_STATE_OK);
        cupid_gb_link_rebase(&pair);
        assert(pair.clock_balance == skew && cupid_gb_link_next(&pair) == selected);
        assert(cupid_gb_reset(ends[side], CUPID_GB_RESET_QUICK));
        cupid_gb_link_rebase(&pair);
        assert(pair.clock_balance == skew && cupid_gb_link_next(&pair) == selected);
        assert(cupid_gb_link_step(&pair, NULL));
        const int64_t next_skew = pair.clock_balance;
        /* A reset outside the explicit host path is detected from its clock.
         * The automatic refresh still retains the signed interleave offset. */
        assert(cupid_gb_reset(ends[1u - side], CUPID_GB_RESET_HARD));
        (void)cupid_gb_link_next(&pair);
        assert(pair.clock_balance == next_skew);
        free(saved);
        cupid_gb_link_detach(&pair);
        release(ends[0]);
        release(ends[1]);
    }
}

static void assert_physical_skew(const CupidGbLinkPair *pair, const uint64_t origins[2],
                                 unsigned largest_step) {
    const uint64_t first = cupid_gb_presentation_cycles(pair->endpoints[0]) - origins[0];
    const uint64_t second = cupid_gb_presentation_cycles(pair->endpoints[1]) - origins[1];
    const int64_t skew =
        (int64_t)(first * pair->clock_hz[1]) - (int64_t)(second * pair->clock_hz[0]);
    const uint32_t largest_clock =
        pair->clock_hz[0] > pair->clock_hz[1] ? pair->clock_hz[0] : pair->clock_hz[1];
    const int64_t bound = (int64_t)largest_step * largest_clock;
    assert(skew >= -bound && skew <= bound);
    assert(pair->clock_balance == skew);
}

static void test_physical_clocks(void) {
    CupidGb *first = machine(CUPID_GB_MODEL_SGB, false, NULL, 0);
    CupidGb *second = machine(CUPID_GB_MODEL_CGB, true, NULL, 0);
    first->elapsed_cycles = (UINT64_C(1) << 60) + 12345;
    second->elapsed_cycles = (UINT64_C(1) << 59) + 98765;
    const uint64_t origins[] = {cupid_gb_presentation_cycles(first),
                                cupid_gb_presentation_cycles(second)};
    CupidGbLinkPair pair = {0};
    assert(cupid_gb_link_attach(&pair, first, second));
    for (unsigned step = 0; step < 20000; ++step) {
        assert(cupid_gb_link_step(&pair, NULL));
        assert_physical_skew(&pair, origins, 16);
    }
    assert(first->elapsed_cycles > origins[0] && second->elapsed_cycles > origins[1]);
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);

    first = machine(CUPID_GB_MODEL_SGB, false, NULL, 0);
    second = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    first->boot_rom_size = 0x100;
    memset(first->boot_rom, 0, sizeof(first->boot_rom));
    cupid_gb_start_boot_rom(first);
    assert(cupid_gb_sgb_startup_held(first));
    const uint64_t held_origins[] = {cupid_gb_presentation_cycles(first),
                                     cupid_gb_presentation_cycles(second)};
    pair = (CupidGbLinkPair){0};
    assert(cupid_gb_link_attach(&pair, first, second));
    for (unsigned step = 0; step < 2000; ++step) {
        assert(cupid_gb_link_step(&pair, NULL));
        assert_physical_skew(&pair, held_origins, CUPID_GB_SGB_STARTUP_STEP_DOTS);
    }
    assert(first->elapsed_cycles == 0 && first->sgb_startup.elapsed_cycles > 1000);
    assert(second->elapsed_cycles > 1000 && cupid_gb_sgb_startup_held(first));
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);
}

static void test_guest_speed_switch(void) {
    const uint8_t program[] = {0x3e, 1, 0xe0, 0x4d, 0x10, 0, 0x3e, 0xa5, 0xea, 0, 0xc0, 0x18, 0xfe};
    CupidGb *first = machine(CUPID_GB_MODEL_CGB, false, program, sizeof(program));
    CupidGb *second = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    CupidGbLinkPair pair = {0};
    assert(cupid_gb_link_attach(&pair, first, second));
    unsigned frozen_steps = 0;
    for (unsigned step = 0; step < 200000 && cupid_gb_read_u8(first, 0xc000) != 0xa5; ++step) {
        const int side = cupid_gb_link_next(&pair);
        assert(side >= 0);
        CupidGb *selected = pair.endpoints[side];
        const uint64_t before = cupid_gb_presentation_cycles(selected);
        const uint8_t freeze = selected->speed_switch_freeze_cycles;
        assert(cupid_gb_step(selected));
        if (cupid_gb_presentation_cycles(selected) == before) {
            assert(selected == first && freeze > selected->speed_switch_freeze_cycles);
            ++frozen_steps;
        }
        assert(cupid_gb_link_advance(&pair, (unsigned)side));
    }
    assert(frozen_steps && first->double_speed);
    assert(cupid_gb_read_u8(first, 0xc000) == 0xa5 && second->elapsed_cycles > 1000);
    assert(!cupid_gb_link_advance(&pair, 0));
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);
}

static void foreign_launch(const CupidGb *gb, bool outgoing, void *user) {
    (void)gb;
    (void)outgoing;
    (void)user;
}

static void foreign_infrared(const CupidGb *gb, bool enabled, uint64_t cycles, void *user) {
    (void)gb;
    (void)enabled;
    (void)cycles;
    (void)user;
}

static void test_ownership_and_replacement(void) {
    CupidGb *first = machine(CUPID_GB_MODEL_CGB, false, NULL, 0);
    CupidGb *second = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    CupidGb *replacement = machine(CUPID_GB_MODEL_SGB, false, NULL, 0);
    CupidGbLinkPair pair = {0};
    assert(!cupid_gb_link_attach(&pair, first, first));
    cupid_gb_set_serial_link(first, (CupidGbSerialLink){.launch = foreign_launch});
    assert(!cupid_gb_link_attach(&pair, first, second));
    assert(first->serial_link.launch == foreign_launch && !second->serial_link.launch);
    cupid_gb_set_serial_link(first, (CupidGbSerialLink){0});
    assert(cupid_gb_link_attach(&pair, first, second));
    assert(cupid_gb_link_step(&pair, NULL));
    cupid_gb_write_u8(first, 0xff01, 0x80);
    cupid_gb_write_u8(first, 0xff02, 0x81);
    cupid_gb_set_infrared_input(first, true);
    assert(pair.outgoing[0]);
    CupidGbLinkPair original;
    memcpy(&original, &pair, sizeof(pair));
    cupid_gb_set_infrared_callback(replacement, foreign_infrared, replacement);
    assert(!cupid_gb_link_replace(&pair, 0, replacement));
    assert(memcmp(&original, &pair, sizeof(pair)) == 0);
    assert(replacement->infrared_callback == foreign_infrared);
    assert(!cupid_gb_link_replace(&pair, 0, second));
    cupid_gb_set_infrared_callback(replacement, first->infrared_callback,
                                   first->infrared_user_data);
    cupid_gb_set_serial_link(replacement, first->serial_link);
    assert(cupid_gb_link_replace(&pair, 0, replacement));
    assert(pair.endpoints[0] == replacement && pair.outgoing[0]);
    assert(pair.clock_balance == original.clock_balance && pair.clock_hz[0] == 4295454u);
    assert(!first->serial_link.launch && !first->infrared_callback);
    assert(first->huc3_ir_input && replacement->huc3_ir_input);
    assert(cupid_gb_link_next(&pair) == 1);
    release(first);

    cupid_gb_set_serial_link(replacement,
                             (CupidGbSerialLink){.launch = foreign_launch, .user_data = second});
    cupid_gb_set_infrared_callback(replacement, foreign_infrared, second);
    cupid_gb_link_rebase(&pair);
    assert(cupid_gb_link_next(&pair) == -1);
    cupid_gb_link_detach(&pair);
    assert(replacement->serial_link.launch == foreign_launch &&
           replacement->serial_link.user_data == second);
    assert(replacement->infrared_callback == foreign_infrared &&
           replacement->infrared_user_data == second && replacement->huc3_ir_input);
    assert(!second->serial_link.launch && !second->infrared_callback);
    release(replacement);
    release(second);
}

static void test_infrared_edges(void) {
    CupidGb *first = machine(CUPID_GB_MODEL_CGB, false, NULL, 0);
    CupidGb *second = machine(CUPID_GB_MODEL_CGB, true, NULL, 0);
    CupidGbLinkPair pair = {0};
    assert(cupid_gb_link_attach(&pair, first, second));
    cupid_gb_write_u8(first, 0xff56, 0xc1);
    assert(second->huc3_ir_input && !first->huc3_ir_input);
    cupid_gb_write_u8(second, 0xff56, 0xc1);
    assert(first->huc3_ir_input);
    cupid_gb_link_rebase(&pair);
    assert(first->huc3_ir_input && second->huc3_ir_input);
    cupid_gb_link_detach(&pair);
    assert(first->huc3_ir_input && second->huc3_ir_input);
    cupid_gb_write_u8(first, 0xff56, 0xc0);
    assert(second->huc3_ir_input);
    assert(cupid_gb_link_attach(&pair, first, second));
    assert(second->huc3_ir_input);
    cupid_gb_write_u8(first, 0xff56, 0xc1);
    cupid_gb_write_u8(first, 0xff56, 0xc0);
    assert(!second->huc3_ir_input);
    /* A guest instruction must emit through the same live connection. */
    first->rom[0x150] = 0x3e;
    first->rom[0x151] = 0xc1;
    first->rom[0x152] = 0xe0;
    first->rom[0x153] = 0x56;
    first->rom[0x154] = 0x18;
    first->rom[0x155] = 0xfe;
    for (unsigned step = 0; step < 10 && !second->huc3_ir_input; ++step)
        assert(cupid_gb_link_step(&pair, NULL));
    assert(second->huc3_ir_input);
    cupid_gb_link_detach(&pair);
    release(first);
    release(second);
}

static void test_accounting_rejection(void) {
    CupidGb *first = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    CupidGb *second = machine(CUPID_GB_MODEL_DMG_ABC, false, NULL, 0);
    CupidGbLinkPair pair = {0};
    assert(cupid_gb_link_attach(&pair, first, second));
    CupidGbLinkPair original;
    memcpy(&original, &pair, sizeof(pair));
    assert(!cupid_gb_link_advance(&pair, 0));
    assert(!cupid_gb_link_advance(&pair, 2));
    assert(memcmp(&original, &pair, sizeof(pair)) == 0);
    first->elapsed_cycles = UINT64_MAX / 4u;
    assert(!cupid_gb_link_advance(&pair, 0));
    assert(memcmp(&original, &pair, sizeof(pair)) == 0);
    first->elapsed_cycles = 0;
    assert(cupid_gb_step(first) && cupid_gb_step(second));
    assert(!cupid_gb_link_advance(&pair, 0));
    assert(memcmp(&original, &pair, sizeof(pair)) == 0);
    cupid_gb_link_rebase(&pair);
    assert(cupid_gb_link_step(&pair, NULL));
    cupid_gb_link_detach(&pair);
    assert(!cupid_gb_link_step(&pair, NULL));
    release(first);
    release(second);
}

int main(void) {
    test_guest_transfers();
    test_clock_sources();
    test_mid_transfer_attachment();
    test_native_latch_lifetime();
    test_detach_during_transfer();
    test_rebase_skew();
    test_physical_clocks();
    test_guest_speed_switch();
    test_ownership_and_replacement();
    test_infrared_edges();
    test_accounting_rejection();
    puts("Paired link regressions passed.");
    return 0;
}

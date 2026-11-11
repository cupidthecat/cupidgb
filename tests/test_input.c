#include "cupid/gb/input.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t selected_lines(unsigned mask, unsigned rows) {
    /* Independent truth table for the two multiplexed four-contact rows. */
    uint8_t result = 0x0fu;
    for (unsigned line = 0; line < 4; ++line) {
        bool direction = (mask & (1u << line)) == 0u;
        if (rows == 2u && (line & 1u) && !(mask & (1u << (line - 1u)))) {
            direction = false;
        }
        const bool action = (mask & (1u << (line + 4u))) == 0u;
        if ((!(rows & 1u) && direction) || (!(rows & 2u) && action)) {
            result &= (uint8_t)~(1u << line);
        }
    }
    return (uint8_t)(0xc0u | (rows << 4u) | result);
}

static void all_masks_and_selection_edges(CupidGb *gb) {
    for (unsigned mask = 0; mask < 256; ++mask) {
        for (unsigned old_rows = 0; old_rows < 4; ++old_rows) {
            for (unsigned new_rows = 0; new_rows < 4; ++new_rows) {
                gb->joypad = (uint8_t)mask;
                gb->io_registers[0] = (uint8_t)(old_rows << 4u);
                const uint8_t before = selected_lines(mask, old_rows);
                const uint8_t after = selected_lines(mask, new_rows);
                gb->interrupt_flags = 5u;
                cupid_gb_write_u8(gb, 0xff00u, (uint8_t)((new_rows << 4u) | 0xcfu));
                assert(cupid_gb_read_u8(gb, 0xff00u) == after);
                assert(gb->joypad == mask);
                const bool edge = (before & (uint8_t)~after & 0x0fu) != 0u;
                assert(gb->interrupt_flags == (edge ? 0x15u : 5u));
                gb->interrupt_flags = 0u;
                cupid_gb_write_u8(gb, 0xff00u, (uint8_t)(new_rows << 4u));
                assert(gb->interrupt_flags == 0u);
            }
        }
    }
}

static void all_button_transitions(CupidGb *gb) {
    for (unsigned rows = 0; rows < 4; ++rows) {
        gb->io_registers[0] = (uint8_t)(rows << 4u);
        for (unsigned before = 0; before < 256; ++before) {
            for (unsigned after = 0; after < 256; ++after) {
                gb->joypad = (uint8_t)before;
                gb->interrupt_flags = 2u;
                cupid_gb_set_buttons(gb, (uint8_t)after);
                const uint8_t old_lines = selected_lines(before, rows);
                const uint8_t new_lines = selected_lines(after, rows);
                const bool edge = (old_lines & (uint8_t)~new_lines & 0x0fu) != 0u;
                assert(cupid_gb_read_u8(gb, 0xff00u) == new_lines);
                assert(gb->interrupt_flags == (edge ? 0x12u : 2u));
            }
        }
    }
}

static void adapter_player_identity(CupidGb *gb) {
    gb->sgb.enabled = true;
    gb->sgb.player_count = 4u;
    for (unsigned player = 0; player < 4; ++player) {
        gb->sgb.current_player = (uint8_t)player;
        gb->io_registers[0] = 0x30u;
        cupid_gb_set_buttons(gb, 0u);
        assert(cupid_gb_read_u8(gb, 0xff00u) == 0xffu - player);
    }
    gb->sgb.enabled = false;
}

static void adapter_player_buttons(CupidGb *gb) {
    gb->sgb.enabled = true;
    gb->sgb.player_count = 4;
    for (unsigned selected = 0; selected < 4; ++selected) {
        gb->sgb.current_player = (uint8_t)selected;
        for (unsigned rows = 0; rows < 3; ++rows) {
            gb->io_registers[0] = (uint8_t)(rows << 4);
            for (unsigned player = 0; player < 4; ++player) {
                assert(cupid_gb_set_buttons_for_player(gb, player, 0xff));
            }
            for (unsigned player = 0; player < 4; ++player) {
                const uint8_t mask = (uint8_t)~(0x11u << player);
                gb->interrupt_flags = 0;
                assert(cupid_gb_set_buttons_for_player(gb, player, mask));
                const uint8_t expected = selected_lines(player == selected ? mask : 0xff, rows);
                assert(cupid_gb_input_read(gb) == expected);
                assert(gb->interrupt_flags == (player == selected ? 0x10 : 0));
                assert(cupid_gb_set_buttons_for_player(gb, player, 0xff));
            }
        }
    }
    /* The protocol advances the pad on P15's rising edge, with the identifier
     * and selected buttons belonging to the newly selected pad. */
    gb->sgb.current_player = 0;
    gb->io_registers[0] = 0x30;
    for (unsigned player = 1; player <= 4; ++player) {
        cupid_gb_input_select(gb, 0x10);
        cupid_gb_input_select(gb, 0x30);
        assert(gb->sgb.current_player == (player & 3));
        assert(cupid_gb_input_read(gb) == 0xff - (player & 3));
    }
    gb->sgb.enabled = false;
    gb->io_registers[0] = 0x20;
    gb->sgb.current_player = 3;
    assert(cupid_gb_set_buttons_for_player(gb, 3, 0));
    assert(cupid_gb_input_read(gb) == 0xef);
    const uint8_t old = gb->joypad;
    assert(!cupid_gb_set_buttons_for_player(gb, 4, 0));
    assert(gb->joypad == old);
    assert(!cupid_gb_set_buttons_for_player(NULL, 0, 0));
}

static void prepare_rows(CupidGb *gb, CupidGbModel model, uint8_t rows, uint8_t buttons) {
    gb->model = model;
    gb->sgb.enabled = false;
    gb->io_registers[0] = rows;
    gb->joypad = buttons;
    gb->joyp_select_delay = 0;
    gb->joyp_select_value = 0;
    gb->interrupt_flags = 0;
}

static void monochrome_row_release(CupidGb *gb) {
    const struct {
        CupidGbModel model;
        uint8_t old_rows, requested, delay, during, settled;
    } cases[] = {
        {CUPID_GB_MODEL_DMG_ABC, 0x00, 0x10, 24, 0xd0, 0xd5},
        {CUPID_GB_MODEL_DMG_ABC, 0x00, 0x20, 12, 0xe0, 0xea},
        {CUPID_GB_MODEL_DMG_ABC, 0x00, 0x30, 24, 0xf0, 0xff},
        {CUPID_GB_MODEL_DMG_ABC, 0x10, 0x20, 12, 0xe0, 0xea},
        {CUPID_GB_MODEL_DMG_ABC, 0x20, 0x10, 24, 0xd0, 0xd5},
        {CUPID_GB_MODEL_DMG_ABC, 0x10, 0x30, 12, 0xf5, 0xff},
        {CUPID_GB_MODEL_DMG_ABC, 0x20, 0x30, 24, 0xfa, 0xff},
        {CUPID_GB_MODEL_MGB, 0x00, 0x10, 16, 0xd0, 0xd5},
        {CUPID_GB_MODEL_MGB, 0x00, 0x20, 4, 0xe0, 0xea},
        {CUPID_GB_MODEL_MGB, 0x20, 0x10, 20, 0xd0, 0xd5},
        {CUPID_GB_MODEL_MGB, 0x10, 0x30, 4, 0xf5, 0xff},
        {CUPID_GB_MODEL_DMG0, 0x20, 0x30, 24, 0xfa, 0xff},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        prepare_rows(gb, cases[i].model, cases[i].old_rows, 0x5a);
        cupid_gb_input_select(gb, cases[i].requested);
        assert(cupid_gb_input_read(gb) == cases[i].during);
        assert(gb->joyp_select_delay == cases[i].delay);
        cupid_gb_input_tick(gb, 0);
        assert(gb->joyp_select_delay == cases[i].delay);
        cupid_gb_input_tick(gb, cases[i].delay - 1u);
        assert(cupid_gb_input_read(gb) == cases[i].during);
        cupid_gb_input_tick(gb, 1);
        assert(cupid_gb_input_read(gb) == cases[i].settled);
        assert(!gb->joyp_select_delay && !gb->joyp_select_value);
        gb->interrupt_flags = 0;
        cupid_gb_input_tick(gb, 100);
        assert(!gb->interrupt_flags);
    }
}

static void pending_row_writes(CupidGb *gb) {
    prepare_rows(gb, CUPID_GB_MODEL_DMG_ABC, 0x20, 0xfe);
    cupid_gb_input_select(gb, 0x30);
    assert(cupid_gb_input_read(gb) == 0xfe);
    cupid_gb_input_tick(gb, 10);
    /* Writing the still-active row is ignored, even though FF00 already
     * reports the pending deselection. It must not cancel its release. */
    cupid_gb_input_select(gb, 0x20);
    assert(gb->joyp_select_delay == 14 && gb->joyp_select_value == 0x30);
    cupid_gb_input_tick(gb, 14);
    assert(cupid_gb_input_read(gb) == 0xff);

    prepare_rows(gb, CUPID_GB_MODEL_DMG_ABC, 0x00, 0x5a);
    cupid_gb_input_select(gb, 0x30);
    cupid_gb_input_tick(gb, 10);
    cupid_gb_input_select(gb, 0x20);
    assert(gb->joyp_select_delay == 14);
    assert(gb->joyp_select_value == 0x20 && cupid_gb_input_read(gb) == 0xea);
    cupid_gb_input_tick(gb, 14);
    assert(cupid_gb_input_read(gb) == 0xea);

    prepare_rows(gb, CUPID_GB_MODEL_DMG_ABC, 0x10, 0x5a);
    cupid_gb_input_select(gb, 0x20);
    cupid_gb_input_tick(gb, 8);
    cupid_gb_input_select(gb, 0x30);
    assert(gb->joyp_select_delay == 24);
    assert(cupid_gb_input_read(gb) == 0xfa);
    cupid_gb_input_tick(gb, 23);
    assert(cupid_gb_input_read(gb) == 0xfa);
    cupid_gb_input_tick(gb, 1);
    assert(cupid_gb_input_read(gb) == 0xff);
}

static void settling_interrupts(CupidGb *gb) {
    /* A row handover briefly selects both rows. Opposing direction contacts
     * then use their electrical AND rather than the direction-only policy. */
    prepare_rows(gb, CUPID_GB_MODEL_DMG_ABC, 0x20, 0xfc);
    assert(cupid_gb_input_read(gb) == 0xee);
    cupid_gb_input_select(gb, 0x10);
    assert(cupid_gb_input_read(gb) == 0xdc && gb->interrupt_flags == 0x10);
    gb->interrupt_flags = 0;
    cupid_gb_input_tick(gb, 24);
    assert(cupid_gb_input_read(gb) == 0xdf && !gb->interrupt_flags);

    /* Input changes still reach a row whose select readback is already high.
     * Its eventual release only raises lines and cannot request another IRQ. */
    prepare_rows(gb, CUPID_GB_MODEL_DMG_ABC, 0x10, 0xff);
    cupid_gb_input_select(gb, 0x30);
    cupid_gb_set_buttons(gb, 0xef);
    assert(cupid_gb_input_read(gb) == 0xfe && gb->interrupt_flags == 0x10);
    gb->interrupt_flags = 0;
    cupid_gb_input_tick(gb, 12);
    assert(cupid_gb_input_read(gb) == 0xff && !gb->interrupt_flags);
}

static void model_and_clock_boundaries(CupidGb *gb) {
    const CupidGbModel immediate[] = {CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2};
    for (size_t i = 0; i < sizeof(immediate) / sizeof(immediate[0]); ++i) {
        prepare_rows(gb, immediate[i], 0x20, 0xfe);
        gb->cgb_mode = false;
        cupid_gb_input_select(gb, 0x30);
        assert(cupid_gb_input_read(gb) == 0xff && !gb->joyp_select_delay);
    }
    for (unsigned sleep = 0; sleep < 3; ++sleep) {
        prepare_rows(gb, CUPID_GB_MODEL_DMG_ABC, 0x20, 0xfe);
        gb->cpu.halted = sleep == 1;
        gb->cpu.stopped = sleep == 2;
        cupid_gb_input_select(gb, 0x30);
        cupid_gb_tick(gb, 5);
        assert(cupid_gb_input_read(gb) == 0xfe && gb->joyp_select_delay == 4);
        cupid_gb_tick(gb, 1);
        assert(cupid_gb_input_read(gb) == 0xff && !gb->joyp_select_delay);
    }
    gb->cpu.halted = gb->cpu.stopped = false;
}

static void instruction_row_readback(void) {
    uint8_t rom[0x8000] = {0};
    /* The second instruction samples before the monochrome P15 release but
     * finishes on that release. Pocket and color hardware have already settled. */
    rom[0x100] = 0xe2; /* LD (FF00+C),A */
    rom[0x101] = 0xf2; /* LD A,(FF00+C) */
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB, CUPID_GB_MODEL_CGB};
    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        CupidGb *gb = calloc(1, sizeof(*gb));
        assert(gb);
        cupid_gb_set_model(gb, models[i]);
        rom[0x143] = models[i] == CUPID_GB_MODEL_CGB ? 0x80 : 0;
        rom[0x14d] = 0;
        for (unsigned address = 0x134; address <= 0x14c; ++address)
            rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);
        assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
        prepare_rows(gb, models[i], 0, 0);
        gb->io_registers[0x40] = 0;
        gb->cpu.pc = 0x100;
        gb->cpu.c = 0;
        gb->cpu.a = 0x20;
        gb->cpu.ime = false;
        gb->interrupt_enable = 0;
        assert(cupid_gb_step(gb) && cupid_gb_step(gb));
        assert(gb->cpu.a == (models[i] == CUPID_GB_MODEL_DMG_ABC ? 0xe0 : 0xea));
        assert(cupid_gb_input_read(gb) == 0xea && !gb->joyp_select_delay);
        assert(gb->elapsed_cycles == 16);
        cupid_gb_cleanup(gb);
        free(gb);
    }
}

int main(void) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_init(gb);
    for (unsigned player = 0; player < 3; ++player) {
        assert(gb->joypad_extra[player] == 0xff);
    }
    gb->model = CUPID_GB_MODEL_CGB;
    all_masks_and_selection_edges(gb);
    gb->model = CUPID_GB_MODEL_DMG_ABC;
    all_button_transitions(gb);
    adapter_player_identity(gb);
    adapter_player_buttons(gb);
    monochrome_row_release(gb);
    pending_row_writes(gb);
    settling_interrupts(gb);
    model_and_clock_boundaries(gb);
    instruction_row_readback();
    cupid_gb_set_buttons(NULL, 0u);
    cupid_gb_input_select(NULL, 0u);
    cupid_gb_input_tick(NULL, 1u);
    assert(cupid_gb_input_read(NULL) == 0xffu);
    cupid_gb_cleanup(gb);
    free(gb);
    puts("Input rows, settling and interrupt edges passed.");
    return 0;
}

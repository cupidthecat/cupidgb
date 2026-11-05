#include "cupid/gb/input.h"

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

int main(void) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_init(gb);
    all_masks_and_selection_edges(gb);
    all_button_transitions(gb);
    adapter_player_identity(gb);
    cupid_gb_set_buttons(NULL, 0u);
    cupid_gb_input_select(NULL, 0u);
    assert(cupid_gb_input_read(NULL) == 0xffu);
    cupid_gb_cleanup(gb);
    free(gb);
    puts("Input rows and interrupt edges passed.");
    return 0;
}

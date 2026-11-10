#include "cupid/gb/input.h"

#include "cupid/gb/cpu.h"
#include "cupid/gb/sgb.h"

uint8_t cupid_gb_input_read(const CupidGb *gb) {
    if (!gb) {
        return 0xffu;
    }
    const uint8_t rows = gb->io_registers[0] & 0x30u;
    const unsigned player = cupid_gb_sgb_active(gb) ? gb->sgb.current_player & 3u : 0u;
    const uint8_t buttons = player ? gb->joypad_extra[player - 1u] : gb->joypad;
    uint8_t lines = 0x0fu;
    if (rows == 0x30u && cupid_gb_sgb_active(gb) && gb->sgb.player_count > 1u) {
        lines = (uint8_t)(0x0fu - (gb->sgb.current_player & 3u));
    } else {
        if (!(rows & 0x10u)) {
            uint8_t directions = buttons & 0x0fu;
            if (rows == 0x20u) {
                /* A digital pad cannot normally close both opposing contacts.
                 * Resolve each axis consistently, but leave the electrical
                 * wired-AND result intact when both button rows are selected. */
                if (!(directions & 1u)) {
                    directions |= 2u;
                }
                if (!(directions & 4u)) {
                    directions |= 8u;
                }
            }
            lines &= directions;
        }
        if (!(rows & 0x20u)) {
            lines &= buttons >> 4u;
        }
    }
    return (uint8_t)(0xc0u | rows | lines);
}

static void request_line_edges(CupidGb *gb, uint8_t before) {
    const uint8_t after = cupid_gb_input_read(gb);
    /* P10..P13 share an edge detector. Selecting an already-held button can
     * request an interrupt; only falling selected lines count, including a
     * held opposite direction exposed when its preferred contact is released. */
    if ((before & (uint8_t)~after & 0x0fu) != 0u) {
        cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_JOYPAD);
    }
}

void cupid_gb_set_buttons(CupidGb *gb, uint8_t released_mask) {
    (void)cupid_gb_set_buttons_for_player(gb, 0u, released_mask);
}

bool cupid_gb_set_buttons_for_player(CupidGb *gb, unsigned player, uint8_t released_mask) {
    if (!gb || player >= 4u) {
        return false;
    }
    const uint8_t before = cupid_gb_input_read(gb);
    if (player == 0u) {
        gb->joypad = released_mask;
    } else {
        gb->joypad_extra[player - 1u] = released_mask;
    }
    request_line_edges(gb, before);
    return true;
}

void cupid_gb_input_select(CupidGb *gb, uint8_t value) {
    if (gb) {
        const uint8_t before = cupid_gb_input_read(gb);
        /* SGB packet reception must see the old select lines before the write. */
        cupid_gb_sgb_write_joyp(gb, value);
        gb->io_registers[0] = (uint8_t)((gb->io_registers[0] & 0x0fu) | (value & 0x30u));
        request_line_edges(gb, before);
    }
}

#include "cupid/gb/input.h"

#include "cupid/gb/cpu.h"
#include "cupid/gb/sgb.h"

static uint8_t electrical_lines(const CupidGb *gb) {
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

uint8_t cupid_gb_input_read(const CupidGb *gb) {
    if (!gb)
        return 0xffu;
    const uint8_t lines = electrical_lines(gb);
    /* The select register is readable before the row's pull-up has settled.
     * Contact data and its interrupt detector still use the active row. */
    return gb->joyp_select_delay ? (uint8_t)((lines & (uint8_t)~0x30u) | gb->joyp_select_value)
                                 : lines;
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
    if (!gb)
        return;
    const uint8_t selected = value & 0x30u;
    uint8_t active = gb->io_registers[0] & 0x30u;
    if (active == selected)
        return;
    const uint8_t before = electrical_lines(gb);
    if (gb->model != CUPID_GB_MODEL_CGB && gb->model != CUPID_GB_MODEL_SGB &&
        gb->model != CUPID_GB_MODEL_SGB2 && !cupid_gb_sgb_active(gb)) {
        /* A second write starts from the previous requested row but cannot
         * shorten an outstanding release. Newly selected rows pull low at
         * once; deselected rows take longer to float high. */
        if (gb->joyp_select_delay)
            active = gb->joyp_select_value;
        const unsigned released = selected & (uint8_t)~active;
        unsigned delay = (released & 0x10u) ? 24u : (released & 0x20u) ? 12u : 0u;
        if (delay && gb->model == CUPID_GB_MODEL_MGB) {
            delay -= 8u;
            if (active == 0x20u && selected == 0x10u)
                delay += 4u;
        }
        if (delay > gb->joyp_select_delay)
            gb->joyp_select_delay = (uint8_t)delay;
        gb->joyp_select_value = gb->joyp_select_delay ? selected : 0u;
        if (gb->joyp_select_delay)
            active &= selected;
        else
            active = selected;
    } else {
        active = selected;
    }
    /* SGB packet reception must see the old select lines before the write. */
    cupid_gb_sgb_write_joyp(gb, active);
    gb->io_registers[0] = (uint8_t)((gb->io_registers[0] & 0x0fu) | active);
    request_line_edges(gb, before);
}

void cupid_gb_input_tick(CupidGb *gb, unsigned base_cycles) {
    if (!gb || !gb->joyp_select_delay)
        return;
    if (base_cycles < gb->joyp_select_delay) {
        gb->joyp_select_delay = (uint8_t)(gb->joyp_select_delay - base_cycles);
        return;
    }
    const uint8_t before = electrical_lines(gb);
    gb->io_registers[0] = (uint8_t)((gb->io_registers[0] & 0x0fu) | gb->joyp_select_value);
    gb->joyp_select_delay = 0;
    gb->joyp_select_value = 0;
    request_line_edges(gb, before);
}

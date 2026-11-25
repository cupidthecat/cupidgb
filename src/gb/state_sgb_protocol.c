/**
 * @file    state_sgb_protocol.c
 * @brief   Portable adapter command-disable and boot-header snapshot fields.
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

#include "state_sgb_protocol.h"

#include <string.h>

_Static_assert(CUPID_SGB_HEADER_BYTES + 1u == CUPID_GB_SGB_PROTOCOL_STATE_SIZE,
               "The version 22 adapter protocol wire size is fixed");

static bool all_zero(const uint8_t *data, size_t length) {
    for (size_t offset = 0; offset < length; ++offset)
        if (data[offset])
            return false;
    return true;
}

bool cupid_gb_sgb_protocol_state_valid(const CupidGb *gb) {
    return gb && (cupid_gb_sgb_host_present(gb) ||
                  (!gb->sgb.commands_disabled &&
                   all_zero(gb->sgb.received_header, sizeof(gb->sgb.received_header))));
}

bool cupid_gb_sgb_protocol_wire_valid(const CupidGb *gb, const uint8_t *data, size_t length) {
    if (!gb || !data || length != CUPID_GB_SGB_PROTOCOL_STATE_SIZE || data[0] > 1)
        return false;
    return cupid_gb_sgb_host_present(gb) || all_zero(data, length);
}

bool cupid_gb_sgb_protocol_state_encode(const CupidGb *gb, uint8_t *data, size_t length) {
    if (!data || length != CUPID_GB_SGB_PROTOCOL_STATE_SIZE ||
        !cupid_gb_sgb_protocol_state_valid(gb))
        return false;
    data[0] = gb->sgb.commands_disabled ? 1u : 0u;
    memcpy(data + 1, gb->sgb.received_header, sizeof(gb->sgb.received_header));
    return true;
}

bool cupid_gb_sgb_protocol_state_decode(CupidGb *gb, const uint8_t *data, size_t length) {
    if (!cupid_gb_sgb_protocol_wire_valid(gb, data, length))
        return false;
    gb->sgb.commands_disabled = data[0] != 0;
    memcpy(gb->sgb.received_header, data + 1, sizeof(gb->sgb.received_header));
    return true;
}

void cupid_gb_sgb_protocol_state_import_legacy(CupidGb *gb) {
    if (!gb)
        return;
    /* The old receiver was gated by the saved cartridge declaration. This is a
     * compatibility projection, not a reconstruction of firmware packet history. */
    gb->sgb.commands_disabled = cupid_gb_sgb_host_present(gb) && !gb->sgb.enabled;
    memset(gb->sgb.received_header, 0, sizeof(gb->sgb.received_header));
}

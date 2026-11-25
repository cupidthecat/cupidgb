/**
 * @file    link.c
 * @brief   Paired serial pins, infrared forwarding, and exact physical-clock interleave.
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

#include "cupid/gb/link.h"

#include "cupid/gb/infrared.h"
#include "cupid/gb/sgb_startup.h"

static int endpoint_index(const CupidGbLinkPair *pair, const CupidGb *gb) {
    if (pair && gb) {
        if (pair->endpoints[0] == gb)
            return 0;
        if (pair->endpoints[1] == gb)
            return 1;
    }
    return -1;
}

static void launch_bit(const CupidGb *gb, bool outgoing, void *user) {
    CupidGbLinkPair *pair = user;
    const int endpoint = endpoint_index(pair, gb);
    if (endpoint >= 0)
        pair->outgoing[endpoint] = outgoing;
}

static bool sample_bit(const CupidGb *gb, void *user) {
    CupidGbLinkPair *pair = user;
    const int endpoint = endpoint_index(pair, gb);
    if (endpoint < 0)
        return true;
    CupidGb *peer = pair->endpoints[1 - endpoint];
    if (!peer)
        return true;
    /* Sample before shifting the peer. The launched bit is independent of a
     * later SB write, and an externally clocked peer never reenters this host. */
    const bool incoming = cupid_gb_serial_external_bit(peer);
    cupid_gb_serial_external_clock(peer, pair->outgoing[endpoint]);
    return incoming;
}

static void infrared_changed(const CupidGb *gb, bool enabled, uint64_t cycles, void *user) {
    (void)cycles;
    CupidGbLinkPair *pair = user;
    const int endpoint = endpoint_index(pair, gb);
    if (endpoint >= 0)
        cupid_gb_set_infrared_input(pair->endpoints[1 - endpoint], enabled);
}

static bool owns_serial(const CupidGbLinkPair *pair, const CupidGb *gb) {
    return gb && gb->serial_link.launch == launch_bit && gb->serial_link.sample == sample_bit &&
           gb->serial_link.user_data == pair &&
           gb->serial_link.device == CUPID_GB_SERIAL_DEVICE_NONE;
}

static bool owns_infrared(const CupidGbLinkPair *pair, const CupidGb *gb) {
    return gb && gb->infrared_callback == infrared_changed && gb->infrared_user_data == pair;
}

static bool connected(const CupidGbLinkPair *pair) {
    if (!pair || !pair->endpoints[0] || !pair->endpoints[1] ||
        pair->endpoints[0] == pair->endpoints[1])
        return false;
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
        const CupidGb *gb = pair->endpoints[endpoint];
        if (!gb->loaded || !gb->rom || !owns_serial(pair, gb) || !owns_infrared(pair, gb))
            return false;
    }
    return true;
}

static bool available(const CupidGbLinkPair *pair, const CupidGb *gb) {
    if (!gb || !gb->loaded || !gb->rom)
        return false;
    const bool serial_free = !gb->serial_link.launch && !gb->serial_link.sample &&
                             gb->serial_link.device == CUPID_GB_SERIAL_DEVICE_NONE;
    return (serial_free || owns_serial(pair, gb)) &&
           (!gb->infrared_callback || owns_infrared(pair, gb));
}

static void install(CupidGbLinkPair *pair, CupidGb *gb) {
    cupid_gb_set_serial_link(
        gb, (CupidGbSerialLink){.launch = launch_bit, .sample = sample_bit, .user_data = pair});
    cupid_gb_set_infrared_callback(gb, infrared_changed, pair);
}

static void uninstall(CupidGbLinkPair *pair, CupidGb *gb) {
    if (owns_serial(pair, gb))
        cupid_gb_set_serial_link(gb, (CupidGbSerialLink){0});
    if (owns_infrared(pair, gb)) {
        cupid_gb_set_infrared_callback(gb, NULL, NULL);
    }
}

void cupid_gb_link_rebase(CupidGbLinkPair *pair) {
    if (!connected(pair))
        return;
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
        const CupidGb *gb = pair->endpoints[endpoint];
        pair->observed_cycles[endpoint] = cupid_gb_presentation_cycles(gb);
        pair->rom_generation[endpoint] = gb->rom_generation;
        pair->clock_hz[endpoint] = cupid_gb_base_clock_hz(gb);
        pair->models[endpoint] = gb->model;
        pair->freeze_cycles[endpoint] = gb->speed_switch_freeze_cycles;
    }
}

bool cupid_gb_link_attach(CupidGbLinkPair *pair, CupidGb *first, CupidGb *second) {
    if (!pair || pair->endpoints[0] || pair->endpoints[1] || first == second ||
        !available(pair, first) || !available(pair, second))
        return false;
    pair->endpoints[0] = first;
    pair->endpoints[1] = second;
    pair->clock_balance = 0;
    install(pair, first);
    install(pair, second);
    cupid_gb_link_rebase(pair);
    return true;
}

void cupid_gb_link_detach(CupidGbLinkPair *pair) {
    if (!pair)
        return;
    uninstall(pair, pair->endpoints[0]);
    uninstall(pair, pair->endpoints[1]);
    const bool first = pair->outgoing[0];
    const bool second = pair->outgoing[1];
    *pair = (CupidGbLinkPair){.outgoing = {first, second}};
}

bool cupid_gb_link_replace(CupidGbLinkPair *pair, unsigned endpoint, CupidGb *replacement) {
    if (endpoint >= 2 || !connected(pair) || replacement == pair->endpoints[1u - endpoint] ||
        !available(pair, replacement))
        return false;
    CupidGb *previous = pair->endpoints[endpoint];
    if (previous != replacement) {
        const bool incoming = previous->huc3_ir_input;
        uninstall(pair, previous);
        pair->endpoints[endpoint] = replacement;
        install(pair, replacement);
        cupid_gb_set_infrared_input(replacement, incoming);
    }
    cupid_gb_link_rebase(pair);
    return true;
}

static bool identity_matches(const CupidGbLinkPair *pair, unsigned endpoint) {
    const CupidGb *gb = pair->endpoints[endpoint];
    return gb->model == pair->models[endpoint] &&
           gb->rom_generation == pair->rom_generation[endpoint] &&
           cupid_gb_base_clock_hz(gb) == pair->clock_hz[endpoint];
}

int cupid_gb_link_next(CupidGbLinkPair *pair) {
    if (!connected(pair))
        return -1;
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
        if (!identity_matches(pair, endpoint) ||
            cupid_gb_presentation_cycles(pair->endpoints[endpoint]) !=
                pair->observed_cycles[endpoint]) {
            cupid_gb_link_rebase(pair);
            break;
        }
    }
    return pair->clock_balance <= 0 ? 0 : 1;
}

bool cupid_gb_link_advance(CupidGbLinkPair *pair, unsigned endpoint) {
    if (endpoint >= 2 || !connected(pair) || !identity_matches(pair, 0) ||
        !identity_matches(pair, 1))
        return false;
    const unsigned peer = 1u - endpoint;
    CupidGb *gb = pair->endpoints[endpoint];
    const uint64_t now = cupid_gb_presentation_cycles(gb);
    if (now < pair->observed_cycles[endpoint] ||
        cupid_gb_presentation_cycles(pair->endpoints[peer]) != pair->observed_cycles[peer])
        return false;
    const uint64_t elapsed = now - pair->observed_cycles[endpoint];
    /* A speed switch may consume a whole CPU step while its LCD oscillator is
     * frozen. The decreasing finite countdown proves progress without inventing
     * shared physical time or moving the other console ahead. */
    if (!elapsed && gb->speed_switch_freeze_cycles >= pair->freeze_cycles[endpoint])
        return false;
    const uint32_t frequency = pair->clock_hz[peer];
    if (!frequency || elapsed > (uint64_t)INT64_MAX / frequency)
        return false;
    const int64_t weighted = (int64_t)(elapsed * frequency);
    if ((!endpoint && pair->clock_balance > INT64_MAX - weighted) ||
        (endpoint && pair->clock_balance < INT64_MIN + weighted))
        return false;
    /* Compare t0/f0 with t1/f1 by cross multiplication. Keeping only their
     * signed difference avoids cumulative products and preserves every fraction.
     * Ordinary next/advance interleaving bounds it by one complete guest step. */
    pair->clock_balance += endpoint ? -weighted : weighted;
    pair->observed_cycles[endpoint] = now;
    pair->freeze_cycles[endpoint] = gb->speed_switch_freeze_cycles;
    return true;
}

bool cupid_gb_link_step(CupidGbLinkPair *pair, unsigned *endpoint) {
    const int selected = cupid_gb_link_next(pair);
    if (selected < 0)
        return false;
    if (endpoint)
        *endpoint = (unsigned)selected;
    return cupid_gb_step(pair->endpoints[selected]) &&
           cupid_gb_link_advance(pair, (unsigned)selected);
}

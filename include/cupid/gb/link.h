/**
 * @file    link.h
 * @brief   Paired console serial, infrared, and shared-clock scheduling interfaces.
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

#ifndef CUPID_GB_LINK_H
#define CUPID_GB_LINK_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize to zero before first use. The pair and both consoles must retain
 * stable addresses until detached or replaced. All operations run on one host
 * thread between complete guest steps; neither console may run independently.
 * This connection state is owned by the host and is never serialized. */
typedef struct CupidGbLinkPair {
    CupidGb *endpoints[2];
    uint64_t observed_cycles[2];
    uint64_t rom_generation[2];
    uint32_t clock_hz[2];
    CupidGbModel models[2];
    int64_t clock_balance;
    uint8_t freeze_cycles[2];
    bool outgoing[2];
} CupidGbLinkPair;

/* Both cartridges must be loaded, distinct, and free of serial/infrared
 * callbacks. Failure preserves the pair and both consoles. A fresh pair's
 * outgoing bits start low; attaching during a transfer does not invent a launch.
 * Detach preserves these host latches for reconnection of the same pair. */
bool cupid_gb_link_attach(CupidGbLinkPair *pair, CupidGb *first, CupidGb *second);
void cupid_gb_link_detach(CupidGbLinkPair *pair);

/* Commit a prepared replacement before destroying the previous console.
 * Registrations copied from this pair are accepted; foreign callbacks are not.
 * The side's launched bit and incoming infrared level remain host history. */
bool cupid_gb_link_replace(CupidGbLinkPair *pair, unsigned endpoint, CupidGb *replacement);

/* Refresh observed clocks after a committed reset, cartridge replacement,
 * model change, or native state load. The small signed scheduling offset,
 * launched serial bits, and infrared input stay live, including when a guest
 * rewrote SB after launching its current bit. Only attach resets the offset.
 * Do not rebase for pause/resume. Foreign registrations are never overwritten. */
void cupid_gb_link_rebase(CupidGbLinkPair *pair);

/* Return 0 or 1 for the next complete guest step, or -1 for a detached/invalid
 * connection. Equal physical times select endpoint zero. Unexpected clock,
 * cartridge-generation, or model changes refresh observed clocks automatically. */
int cupid_gb_link_next(CupidGbLinkPair *pair);

/* Call exactly once after executing the selected console's single step. The
 * sequence is next(), the caller's instrumented step, then advance(); do not
 * call next() between execution and accounting. Explicit debugger steps may
 * name either endpoint, after which next() lets its peer catch up.
 * False reports changed ownership, an unaccounted peer/timeline transition,
 * arithmetic overflow, or no progress. A decreasing oscillator-freeze countdown
 * counts as progress even when presentation time has not advanced. */
bool cupid_gb_link_advance(CupidGbLinkPair *pair, unsigned endpoint);

/* Convenience for hosts without per-step instrumentation. If supplied, endpoint
 * receives the selected side before execution, including a failed core step. */
bool cupid_gb_link_step(CupidGbLinkPair *pair, unsigned *endpoint);

#ifdef __cplusplus
}
#endif

#endif

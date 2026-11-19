/**
 * @file    state_startup.h
 * @brief   Bounded native continuation for adapter startup clocks and ordered audio.
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

#ifndef CUPID_GB_STATE_STARTUP_H
#define CUPID_GB_STATE_STARTUP_H

#include "cupid/gb/gb.h"

enum {
    CUPID_GB_STARTUP_STATE_HEADER_SIZE = 65,
    CUPID_GB_STARTUP_STATE_BATCH_SIZE = 10,
    CUPID_GB_STARTUP_STATE_MAX_SIZE =
        CUPID_GB_STARTUP_STATE_HEADER_SIZE +
        CUPID_GB_SGB_AUDIO_BATCHES * CUPID_GB_STARTUP_STATE_BATCH_SIZE +
        2 * CUPID_GB_SGB_AUDIO_BATCHES * CUPID_GB_SGB_AUDIO_BATCH_FRAMES
};

/* Output validation deliberately ignores restored hardware phase and elapsed time.
 * Live imports retain this complete owner alongside ordinary output. */
bool cupid_gb_sgb_startup_output_valid(const CupidGb *gb);
bool cupid_gb_sgb_startup_state_valid(const CupidGb *gb);

/* A malformed owner has no wire size. Non-adapter models have zero extension bytes. */
bool cupid_gb_sgb_startup_state_size(const CupidGb *gb, size_t *length);

/* Preflight reads only the receiving physical model, not its phase, rate, queued
 * output, or guest clocks. Available may include later extensions and the accessory.
 * The caller validates those fields after the exact returned startup length. */
bool cupid_gb_sgb_startup_state_measure(const CupidGb *gb, const uint8_t *data, size_t available,
                                        size_t *length);

bool cupid_gb_sgb_startup_state_encode(const CupidGb *gb, uint8_t *data, size_t length);

/* Writes only the candidate's startup owner, and only after complete validation.
 * The candidate already contains the decoded ordinary output and guest clocks. */
bool cupid_gb_sgb_startup_state_decode(CupidGb *gb, const uint8_t *data, size_t length);

/* Canonical absence: completed phase, zero extra time, empty output, noise seed 1.
 * This does not modify the decoded guest time or presentation age. */
void cupid_gb_sgb_startup_state_import_legacy(CupidGb *gb);

#endif

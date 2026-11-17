/**
 * @file    test_state_layout.h
 * @brief   Independent native extension widths for historical wire fixtures.
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

#ifndef CUPID_TEST_STATE_LAYOUT_H
#define CUPID_TEST_STATE_LAYOUT_H

#include "cupid/gb/gb.h"

#include <assert.h>
#include <stddef.h>

enum { TEST_STATE_SGB_PROTOCOL_BYTES = 85 };

/* These wire widths stay explicit so older fixture lengths do not depend on
 * the current encoder's own size calculation or the in-memory struct layout. */
static size_t test_state_startup_size(const CupidGb *gb) {
    if (gb->model != CUPID_GB_MODEL_SGB && gb->model != CUPID_GB_MODEL_SGB2)
        return 0;
    const CupidGbSgbStartupAudio *audio = &gb->sgb_startup.output;
    assert(audio->batch_count <= 2);
    size_t length = 65u + 10u * audio->batch_count;
    for (unsigned index = 0; index < audio->batch_count; ++index) {
        const CupidGbSgbStartupBatch *batch = &audio->batches[index];
        assert(batch->cursor < batch->length && batch->length <= 3214);
        length += 2u * (batch->length - batch->cursor);
    }
    return length;
}

/* Current bytes after the SRAM extension and before the accessory descriptor.
 * Keep the v21 startup measurement separate from the fixed v22 protocol tail. */
static size_t test_state_post_ram_size(const CupidGb *gb) {
    return test_state_startup_size(gb) + TEST_STATE_SGB_PROTOCOL_BYTES;
}

/* Expected projection for pre-v22 equality checks. Preserve the original ROM
 * fixture and its digest; only newly absent receiver history gets a default. */
static inline void test_state_legacy_protocol(CupidGb *gb) {
    gb->sgb.commands_disabled =
        (gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2) && !gb->sgb.enabled;
    for (unsigned index = 0; index < 84; ++index)
        gb->sgb.received_header[index] = 0;
}

#endif

/**
 * @file    gbs.h
 * @brief   Owned GBS music programs, metadata, and native track initialization.
 * @author  cupidthecat
 * @date    2026-11-18
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

#ifndef CUPID_GB_GBS_H
#define CUPID_GB_GBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CupidGb;

#define CUPID_GB_GBS_HEADER_SIZE 112u
#define CUPID_GB_GBS_FILE_PAYLOAD_LIMIT 0x400000u

typedef enum CupidGbGbsResult {
    CUPID_GB_GBS_OK = 0,
    CUPID_GB_GBS_INVALID_ARGUMENT,
    CUPID_GB_GBS_INVALID_HEADER,
    CUPID_GB_GBS_TOO_LARGE,
    CUPID_GB_GBS_OUT_OF_MEMORY,
    CUPID_GB_GBS_NOT_LOADED
} CupidGbGbsResult;

typedef struct CupidGbGbsInfo {
    uint8_t track_count;
    /* Zero-based byte conversion of the header's initial track. Zero in the
     * file becomes 255; it is not clamped to the declared count. */
    uint8_t first_track;
    uint16_t load_address;
    uint16_t init_address;
    uint16_t play_address;
    uint16_t stack_pointer;
    uint8_t timer_modulo;
    uint8_t timer_control;
    /* Raw Latin-1 metadata with an additional guaranteed NUL terminator. */
    char title[33];
    char author[33];
    char copyright[33];
} CupidGbGbsInfo;

/* The instance must be initialized and idle at an instruction boundary. The
 * buffer API consumes the complete supplied span, subject to checked allocation
 * bounds. File adapters separately cap payload reads at the named file limit.
 * Load prepares all owned storage and the initial native track before commit.
 * On failure, the instance, callbacks, audio queues, and optional info stay intact.
 * On success, the core owns its program image and a by-value immutable header;
 * it borrows no caller memory. Successful load advances rom_generation.
 * A fresh unloaded instance starts uncharged audio reconstruction, retaining its
 * listening settings and queued PCM. A loaded session retains its live history.
 * Output info storage, when supplied, must not overlap input or instance storage.
 */
CupidGbGbsResult cupid_gb_load_gbs(struct CupidGb *gb, const uint8_t *data, size_t length,
                                   CupidGbGbsInfo *info);

/* Native reset and initial register setup for one raw zero-based track byte.
 * The track is not restricted to track_count. Failure is transactional.
 * Host program metadata, ROM ownership, callbacks, listening policy, and the
 * content generation are retained. Track selection remains a frontend choice.
 * Queued PCM stays intact; initial sound-register writes update retained output
 * reconstruction normally rather than replacing it with a new stream.
 */
CupidGbGbsResult cupid_gb_gbs_start_track(struct CupidGb *gb, uint8_t track);

/* Returns false for an ordinary cartridge. A non-NULL output receives a value
 * copy only on success; no pointer into the instance escapes this API. Passing
 * NULL queries only whether a GBS program is loaded. */
bool cupid_gb_gbs_info(const struct CupidGb *gb, CupidGbGbsInfo *info);

const char *cupid_gb_gbs_result_name(CupidGbGbsResult result);

#ifdef __cplusplus
}
#endif

#endif

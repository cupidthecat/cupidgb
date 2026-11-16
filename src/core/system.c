/**
 * @file    system.c
 * @brief   Display names for supported Game Boy system targets.
 * @author  cupidthecat
 * @date    2026-11-16
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

/**
 * @file system.c
 * @brief System identification utilities for supported emulation targets.
 *
 * Provides helper functions for querying metadata about supported
 * target systems (e.g., GB, GBC).
 */
#include "cupid/core/system.h"

/**
 * @brief Returns a human-readable name for a given target system.
 *
 * Maps a @ref CupidSystem enum value to its corresponding display name
 * string. Useful for logging, UI labels, and diagnostics.
 *
 * @param system The target system to query.
 *
 * @return A null-terminated string with the system name:
 *         - `"Game Boy"` for @ref CUPID_SYSTEM_GB
 *         - `"Game Boy Color"` for @ref CUPID_SYSTEM_GBC
 *         - `"Unknown"` for any unrecognized value
 *
 * @note The returned string is a string literal and must not be
 *       modified or freed by the caller.
 */
const char *cupid_system_name(CupidSystem system) {
    switch (system) {
        case CUPID_SYSTEM_GB:
            return "Game Boy";
        case CUPID_SYSTEM_GBC:
            return "Game Boy Color";
        default:
            return "Unknown";
    }
}

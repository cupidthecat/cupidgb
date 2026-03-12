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
const char *cupid_system_name(CupidSystem system)
{
    switch (system) {
    case CUPID_SYSTEM_GB:
        return "Game Boy";
    case CUPID_SYSTEM_GBC:
        return "Game Boy Color";
    default:
        return "Unknown";
    }
}

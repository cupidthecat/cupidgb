#include "cupid/core/system.h"

const char *cupid_system_name(CupidSystem system)
{
    switch (system) {
    case CUPID_SYSTEM_GB:
        return "Game Boy";
    case CUPID_SYSTEM_GBC:
        return "Game Boy Color";
    case CUPID_SYSTEM_GBA:
        return "Game Boy Advance";
    default:
        return "Unknown";
    }
}

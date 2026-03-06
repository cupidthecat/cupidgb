#include "cupid/core/emulator.h"

#include "cupid/common/log.h"

void cupid_emulator_init(CupidEmulator *emulator, CupidSystem target_system)
{
    if (emulator == 0) {
        cupid_log_error("Cannot initialize emulator: null instance.");
        return;
    }

    emulator->target_system = target_system;
    emulator->initialized = true;
    cupid_log_info("Cupid emulator core initialized.");
}

void cupid_emulator_shutdown(CupidEmulator *emulator)
{
    if (emulator == 0 || !emulator->initialized) {
        return;
    }

    emulator->initialized = false;
    cupid_log_info("Cupid emulator core shut down.");
}

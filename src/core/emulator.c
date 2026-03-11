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

    if (target_system == CUPID_SYSTEM_GB || target_system == CUPID_SYSTEM_GBC) {
        cupid_gb_init(&emulator->gb);
    }

    cupid_log_info("Cupid emulator core initialized.");
}

void cupid_emulator_shutdown(CupidEmulator *emulator)
{
    if (emulator == 0 || !emulator->initialized) {
        return;
    }

    cupid_gb_cleanup(&emulator->gb);
    emulator->initialized = false;
    cupid_log_info("Cupid emulator core shut down.");
}

bool cupid_emulator_load_rom(CupidEmulator *emulator,
                             const unsigned char *rom_data,
                             size_t rom_size)
{
    if (emulator == 0 || !emulator->initialized || rom_data == 0) {
        return false;
    }

    if (emulator->target_system == CUPID_SYSTEM_GB ||
        emulator->target_system == CUPID_SYSTEM_GBC) {
        return cupid_gb_load_rom(&emulator->gb, rom_data, rom_size);
    }

    cupid_log_error("ROM loading is not implemented for this target system.");
    return false;
}

bool cupid_emulator_load_rom_file(CupidEmulator *emulator, const char *path)
{
    if (emulator == 0 || !emulator->initialized || path == 0) {
        return false;
    }

    if (emulator->target_system == CUPID_SYSTEM_GB ||
        emulator->target_system == CUPID_SYSTEM_GBC) {
        return cupid_gb_load_rom_file(&emulator->gb, path);
    }

    cupid_log_error("ROM file loading is not implemented for this target system.");
    return false;
}

bool cupid_emulator_step(CupidEmulator *emulator)
{
    if (emulator == 0 || !emulator->initialized) {
        return false;
    }

    if (emulator->target_system == CUPID_SYSTEM_GB ||
        emulator->target_system == CUPID_SYSTEM_GBC) {
        return cupid_gb_step(&emulator->gb);
    }

    cupid_log_error("Stepping is not implemented for this target system.");
    return false;
}

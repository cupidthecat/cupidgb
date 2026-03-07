#ifndef CUPID_CORE_EMULATOR_H
#define CUPID_CORE_EMULATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "cupid/gb/gb.h"
#include "cupid/core/system.h"

typedef struct CupidEmulator {
    CupidSystem target_system;
    bool initialized;
    CupidGb gb;
} CupidEmulator;

void cupid_emulator_init(CupidEmulator *emulator, CupidSystem target_system);
void cupid_emulator_shutdown(CupidEmulator *emulator);
bool cupid_emulator_load_rom(CupidEmulator *emulator,
                             const unsigned char *rom_data,
                             size_t rom_size);
bool cupid_emulator_load_rom_file(CupidEmulator *emulator, const char *path);
bool cupid_emulator_step(CupidEmulator *emulator);

#endif

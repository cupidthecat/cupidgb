#ifndef CUPID_CORE_EMULATOR_H
#define CUPID_CORE_EMULATOR_H

#include <stdbool.h>

#include "cupid/core/system.h"

typedef struct CupidEmulator {
    CupidSystem target_system;
    bool initialized;
} CupidEmulator;

void cupid_emulator_init(CupidEmulator *emulator, CupidSystem target_system);
void cupid_emulator_shutdown(CupidEmulator *emulator);

#endif

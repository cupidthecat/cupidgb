#include <assert.h>

#include "cupid/core/emulator.h"
#include "cupid/core/system.h"

int main(void)
{
    CupidEmulator emulator = {0};

    cupid_emulator_init(&emulator, CUPID_SYSTEM_GBC);

    assert(emulator.initialized);
    assert(emulator.target_system == CUPID_SYSTEM_GBC);

    cupid_emulator_shutdown(&emulator);
    assert(!emulator.initialized);

    return 0;
}

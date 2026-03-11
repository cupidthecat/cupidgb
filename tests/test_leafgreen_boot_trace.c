/* Quick diagnostic: load Leaf Green and trace boot - both HLE and BIOS modes. */
#include "cupid/gba/gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void run_test(const char *label, int use_bios, int nframes)
{
    CupidGba gba;
    int frame, steps;

    memset(&gba, 0, sizeof(gba));
    cupid_gba_init(&gba);

    if (use_bios) {
        if (!cupid_gba_load_bios_file(&gba, "bootroms/gba_bios.bin")) {
            fprintf(stderr, "[%s] BIOS not found\n", label);
            free(gba.rom);
            return;
        }
    }

    if (!cupid_gba_load_rom_file(&gba, "Pokemon - Leaf Green Version (U) (V1.1).gba")) {
        fprintf(stderr, "[%s] ROM not found\n", label);
        free(gba.rom);
        return;
    }

    fprintf(stderr, "\n=== %s: PC=0x%08X CPSR=0x%08X bios=%d ===\n",
            label, gba.cpu.r[15], gba.cpu.cpsr, gba.bios_loaded);

    for (frame = 0; frame < nframes; frame++) {
        for (steps = 0; steps < 2000000; steps++) {
            cupid_gba_step(&gba);
            if (gba.frame_ready) {
                gba.frame_ready = false;
                break;
            }
        }
        if (frame < 10 || frame % 50 == 0 ||
            (gba.display_control != 0x0080 && frame > 5)) {
            fprintf(stderr, "[%s] Frame %d: DISPCNT=0x%04X fb=%d mode=%d pc=0x%08X halted=%d steps=%d\n",
                    label, frame, gba.display_control,
                    (gba.display_control & 0x80) ? 1 : 0,
                    gba.display_control & 7,
                    gba.cpu.r[15], gba.halted, steps);
        }
    }

    free(gba.rom);
}

int main(void)
{
    setenv("CUPID_GBA_BOOT_TRACE", "0", 1);

    /* Test HLE mode (no real BIOS) */
    run_test("HLE", 0, 300);

    /* Test BIOS mode */
    run_test("BIOS", 1, 600);

    return 0;
}


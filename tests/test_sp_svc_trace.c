/* Track SP_svc through BIOS boot and game transition */
#include "cupid/gba/gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SVC bank index = 2 from the enum */
#define SVC_BANK 2
#define IRQ_BANK 4

int main(void)
{
    CupidGba gba;
    int frame, steps;

    memset(&gba, 0, sizeof(gba));
    cupid_gba_init(&gba);

    if (!cupid_gba_load_bios_file(&gba, "bootroms/gba_bios.bin")) {
        fprintf(stderr, "BIOS not found\n"); return 1;
    }
    if (!cupid_gba_load_rom_file(&gba, "Pokemon - Leaf Green Version (U) (V1.1).gba")) {
        fprintf(stderr, "ROM not found\n"); return 1;
    }

    fprintf(stderr, "Initial: SP_svc=0x%08X SP_irq=0x%08X SP_sys=0x%08X\n",
            gba.cpu.sp_bank[SVC_BANK], gba.cpu.sp_bank[IRQ_BANK],
            gba.cpu.sp_bank[0]);

    for (frame = 0; frame < 275; frame++) {
        uint32_t sp_svc_start = gba.cpu.sp_bank[SVC_BANK];

        for (steps = 0; steps < 2000000; steps++) {
            cupid_gba_step(&gba);
            if (gba.frame_ready) { gba.frame_ready = false; break; }
        }

        uint32_t sp_svc_end = gba.cpu.sp_bank[SVC_BANK];
        uint32_t sp_irq_end = gba.cpu.sp_bank[IRQ_BANK];

        /* Log if SP_svc changed, or at key frames */
        if (sp_svc_start != sp_svc_end || frame < 3 || frame == 266 ||
            frame == 267 || frame == 268 || frame == 269 || frame == 270) {
            fprintf(stderr, "Frame %d: SP_svc=0x%08X SP_irq=0x%08X "
                    "DISPCNT=0x%04X PC=0x%08X R13=0x%08X mode=0x%02X%s\n",
                    frame, sp_svc_end, sp_irq_end,
                    gba.display_control, gba.cpu.r[15], gba.cpu.r[13],
                    gba.cpu.cpsr & 0x1F,
                    sp_svc_start != sp_svc_end ? " *** CHANGED" : "");
        }
    }

    free(gba.rom);
    return 0;
}

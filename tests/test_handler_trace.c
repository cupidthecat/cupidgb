/* Trace IRQ handler pointer (0x03007FFC) changes and IWRAM 0x3580 writes */
#include "cupid/gba/gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_step = 0;
static int g_frame = 0;

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

    /* Run to frame 266 quickly */
    for (frame = 0; frame < 266; frame++) {
        for (steps = 0; steps < 2000000; steps++) {
            cupid_gba_step(&gba);
            if (gba.frame_ready) { gba.frame_ready = false; break; }
        }
    }

    /* Now step-by-step from frame 266 to 270, monitoring handler pointer */
    uint32_t prev_handler = (uint32_t)gba.iwram[0x7FFC]
                          | ((uint32_t)gba.iwram[0x7FFD] << 8)
                          | ((uint32_t)gba.iwram[0x7FFE] << 16)
                          | ((uint32_t)gba.iwram[0x7FFF] << 24);
    fprintf(stderr, "Frame 266 start: handler=0x%08X\n", prev_handler);

    for (frame = 266; frame < 271; frame++) {
        int frame_step = 0;
        for (steps = 0; steps < 2000000; steps++) {
            uint32_t pc_before = gba.cpu.r[15];
            cupid_gba_step(&gba);
            g_step++;
            frame_step++;

            /* Check handler pointer change */
            uint32_t handler = (uint32_t)gba.iwram[0x7FFC]
                             | ((uint32_t)gba.iwram[0x7FFD] << 8)
                             | ((uint32_t)gba.iwram[0x7FFE] << 16)
                             | ((uint32_t)gba.iwram[0x7FFF] << 24);
            if (handler != prev_handler) {
                fprintf(stderr, "[frame %d, step %d] HANDLER CHANGE: 0x%08X→0x%08X "
                        "PC=0x%08X→0x%08X CPSR=0x%08X dma=%d\n",
                        frame, frame_step, prev_handler, handler,
                        pc_before, gba.cpu.r[15], gba.cpu.cpsr, gba.dma_processing);

                /* When handler points to IWRAM, dump the code there */
                if (handler >= 0x03000000u && handler < 0x04000000u) {
                    uint32_t ioff = (handler - 0x03000000) & 0x7FFF;
                    fprintf(stderr, "  IWRAM at handler:");
                    for (int i = 0; i < 32; i++) {
                        if (i % 16 == 0) fprintf(stderr, "\n  [0x%04X]:", ioff + i);
                        fprintf(stderr, " %02X", gba.iwram[ioff + i]);
                    }
                    fprintf(stderr, "\n");
                }
                prev_handler = handler;
            }

            if (gba.frame_ready) {
                gba.frame_ready = false;
                fprintf(stderr, "Frame %d done: handler=0x%08X DISPCNT=0x%04X "
                        "PC=0x%08X CPSR=0x%08X\n",
                        frame, handler, gba.display_control,
                        gba.cpu.r[15], gba.cpu.cpsr);
                break;
            }
        }
    }

    free(gba.rom);
    return 0;
}

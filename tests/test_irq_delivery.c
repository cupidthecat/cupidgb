/* Trace IRQ delivery and handler execution after game transition */
#include "cupid/gba/gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* Run to frame 268 (first game frame with IntrWait active) */
    for (frame = 0; frame < 269; frame++) {
        for (steps = 0; steps < 2000000; steps++) {
            cupid_gba_step(&gba);
            if (gba.frame_ready) { gba.frame_ready = false; break; }
        }
    }

    fprintf(stderr, "=== Starting frame 269 trace ===\n");
    fprintf(stderr, "PC=0x%08X CPSR=0x%08X mode=0x%02X I=%d\n",
            gba.cpu.r[15], gba.cpu.cpsr, gba.cpu.cpsr & 0x1F,
            (gba.cpu.cpsr >> 7) & 1);
    fprintf(stderr, "IE=0x%04X IF=0x%04X IME=%d halted=%d\n",
            gba.interrupt_enable, gba.interrupt_flags, gba.interrupt_master, gba.halted);

    uint32_t irq_handler = (uint32_t)gba.iwram[0x7FFC]
                         | ((uint32_t)gba.iwram[0x7FFD] << 8)
                         | ((uint32_t)gba.iwram[0x7FFE] << 16)
                         | ((uint32_t)gba.iwram[0x7FFF] << 24);
    fprintf(stderr, "IRQ handler: 0x%08X\n", irq_handler);

    /* Now step through and log:
       1. Halt state changes
       2. IRQ vector hits (PC going to 0x18)
       3. When game IRQ handler runs
       4. BIOS_IF changes */
    int halt_enter = 0, halt_exit = 0, irq_vec = 0;
    int irq_handler_called = 0;
    uint32_t prev_bios_if = 0;
    int step = 0;

    for (steps = 0; steps < 2000000; steps++) {
        uint32_t pc_before = gba.cpu.r[15];
        uint32_t cpsr_before = gba.cpu.cpsr;
        int halted_before = gba.halted;

        uint32_t bios_if_before = (uint32_t)gba.iwram[0x7FF8]
                                | ((uint32_t)gba.iwram[0x7FF9] << 8);

        cupid_gba_step(&gba);
        step++;

        if (gba.frame_ready) {
            gba.frame_ready = false;
            fprintf(stderr, "Frame done at step %d\n", step);
            break;
        }

        /* Log halt state changes */
        if (!halted_before && gba.halted && halt_enter < 5) {
            halt_enter++;
            fprintf(stderr, "[step %d] HALT ENTER: PC=0x%08X CPSR=0x%08X mode=0x%02X I=%d\n",
                    step, gba.cpu.r[15], gba.cpu.cpsr, gba.cpu.cpsr & 0x1F,
                    (gba.cpu.cpsr >> 7) & 1);
        }
        if (halted_before && !gba.halted && halt_exit < 10) {
            halt_exit++;
            fprintf(stderr, "[step %d] HALT EXIT: PC=0x%08X CPSR=0x%08X mode=0x%02X I=%d "
                    "IE=0x%04X IF=0x%04X IME=%d\n",
                    step, gba.cpu.r[15], gba.cpu.cpsr, gba.cpu.cpsr & 0x1F,
                    (gba.cpu.cpsr >> 7) & 1,
                    gba.interrupt_enable, gba.interrupt_flags, gba.interrupt_master);
        }

        /* Log IRQ vector hits */
        if (gba.cpu.r[15] == 0x0000001C && pc_before != 0x00000018 &&
            (gba.cpu.cpsr & 0x1F) == 0x12 && irq_vec < 5) {
            irq_vec++;
            fprintf(stderr, "[step %d] IRQ VECTOR: PC=0x%08X→0x%08X CPSR=0x%08X LR=0x%08X "
                    "IE=0x%04X IF=0x%04X\n",
                    step, pc_before, gba.cpu.r[15], gba.cpu.cpsr, gba.cpu.r[14],
                    gba.interrupt_enable, gba.interrupt_flags);
        }

        /* Log when game IRQ handler starts executing  */
        if (pc_before >= 0x00000128u && pc_before <= 0x0000016Cu &&
            (gba.cpu.r[15] >= 0x08000000u || gba.cpu.r[15] >= 0x03000000u) &&
            irq_handler_called < 5) {
            irq_handler_called++;
            fprintf(stderr, "[step %d] IRQ HANDLER CALL: PC=0x%08X→0x%08X "
                    "CPSR=0x%08X mode=0x%02X\n",
                    step, pc_before, gba.cpu.r[15], gba.cpu.cpsr, gba.cpu.cpsr & 0x1F);
        }

        /* Log BIOS_IF changes */
        uint32_t bios_if_after = (uint32_t)gba.iwram[0x7FF8]
                               | ((uint32_t)gba.iwram[0x7FF9] << 8);
        if (bios_if_after != bios_if_before && bios_if_after != prev_bios_if) {
            fprintf(stderr, "[step %d] BIOS_IF CHANGE: 0x%04X→0x%04X PC=0x%08X\n",
                    step, bios_if_before, bios_if_after, gba.cpu.r[15]);
            prev_bios_if = bios_if_after;
        }
    }

    fprintf(stderr, "\nSummary: halt_enter=%d halt_exit=%d irq_vec=%d handler_calls=%d\n",
            halt_enter, halt_exit, irq_vec, irq_handler_called);

    free(gba.rom);
    return 0;
}

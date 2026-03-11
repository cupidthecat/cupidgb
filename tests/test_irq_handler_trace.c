/* Focused diagnostic: check game's IRQ handler and trace first SWI */
#include "cupid/gba/gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump_iwram_at(CupidGba *gba, uint32_t offset, int count) {
    fprintf(stderr, "  IWRAM[0x%04X]:", offset);
    for (int i = 0; i < count && (offset + i) < CUPID_GBA_IWRAM_SIZE; i++) {
        if (i % 16 == 0 && i > 0) fprintf(stderr, "\n               ");
        fprintf(stderr, " %02X", gba->iwram[offset + i]);
    }
    fprintf(stderr, "\n");
}

int main(void)
{
    CupidGba gba;
    int frame, steps, total_steps = 0;

    memset(&gba, 0, sizeof(gba));
    cupid_gba_init(&gba);

    if (!cupid_gba_load_bios_file(&gba, "bootroms/gba_bios.bin")) {
        fprintf(stderr, "BIOS not found\n"); return 1;
    }
    if (!cupid_gba_load_rom_file(&gba, "Pokemon - Leaf Green Version (U) (V1.1).gba")) {
        fprintf(stderr, "ROM not found\n"); return 1;
    }

    /* Run through BIOS intro to frame 266 */
    for (frame = 0; frame < 267; frame++) {
        for (steps = 0; steps < 2000000; steps++) {
            cupid_gba_step(&gba);
            if (gba.frame_ready) { gba.frame_ready = false; break; }
        }
    }
    fprintf(stderr, "=== After BIOS intro (frame 266) ===\n");
    fprintf(stderr, "DISPCNT=0x%04X PC=0x%08X CPSR=0x%08X\n",
            gba.display_control, gba.cpu.r[15], gba.cpu.cpsr);

    /* Now trace frame 267 step by step, logging key events */
    fprintf(stderr, "\n=== Tracing frame 267 (game transition) ===\n");
    int logged = 0;
    int first_swi_logged = 0;
    uint32_t prev_dispcnt = gba.display_control;

    for (steps = 0; steps < 2000000; steps++) {
        uint32_t pc_before = gba.cpu.r[15];
        uint32_t cpsr_before = gba.cpu.cpsr;

        cupid_gba_step(&gba);
        total_steps++;

        if (gba.frame_ready) {
            gba.frame_ready = false;
            fprintf(stderr, "Frame 267 complete at step %d\n", steps);
            break;
        }

        /* Log DISPCNT changes */
        if (gba.display_control != prev_dispcnt) {
            fprintf(stderr, "[step %d] DISPCNT change: 0x%04X→0x%04X PC=0x%08X\n",
                    steps, prev_dispcnt, gba.display_control, pc_before);
            prev_dispcnt = gba.display_control;
        }

        /* Log when PC enters 0x08xxxxxx (game code) first time */
        if (pc_before < 0x08000000u && gba.cpu.r[15] >= 0x08000000u &&
            gba.cpu.r[15] < 0x10000000u && logged == 0) {
            fprintf(stderr, "[step %d] GAME ENTRY: PC 0x%08X→0x%08X CPSR=0x%08X\n",
                    steps, pc_before, gba.cpu.r[15], gba.cpu.cpsr);
            logged = 1;
        }

        /* Log first SWI vector (PC goes to 0x08) from game code */
        if (gba.cpu.r[15] >= 0x00000008u && gba.cpu.r[15] <= 0x0000000Cu &&
            pc_before >= 0x03000000u && !first_swi_logged) {
            fprintf(stderr, "[step %d] FIRST SWI from game code!\n", steps);
            fprintf(stderr, "  PC: 0x%08X→0x%08X CPSR_before: 0x%08X CPSR_after: 0x%08X\n",
                    pc_before, gba.cpu.r[15], cpsr_before, gba.cpu.cpsr);
            fprintf(stderr, "  Mode: 0x%02X I=%d T=%d\n",
                    gba.cpu.cpsr & 0x1F,
                    (gba.cpu.cpsr >> 7) & 1,
                    (gba.cpu.cpsr >> 5) & 1);
            /* Log SVC banked registers */
            fprintf(stderr, "  R0=0x%08X R1=0x%08X R12=0x%08X LR=0x%08X\n",
                    gba.cpu.r[0], gba.cpu.r[1], gba.cpu.r[12], gba.cpu.r[14]);
            first_swi_logged = 1;
        }

        /* Log first SWI from ROM */
        if (gba.cpu.r[15] >= 0x00000008u && gba.cpu.r[15] <= 0x0000000Cu &&
            pc_before >= 0x08000000u && pc_before < 0x10000000u && first_swi_logged < 2) {
            fprintf(stderr, "[step %d] SWI from ROM code!\n", steps);
            fprintf(stderr, "  PC: 0x%08X→0x%08X CPSR: 0x%08X\n",
                    pc_before, gba.cpu.r[15], gba.cpu.cpsr);
            fprintf(stderr, "  R0=0x%08X R1=0x%08X LR=0x%08X\n",
                    gba.cpu.r[0], gba.cpu.r[1], gba.cpu.r[14]);
            if (first_swi_logged == 1) first_swi_logged = 2;
        }
    }

    /* Dump game state after frame 267 */
    fprintf(stderr, "\n=== State after frame 267 ===\n");
    uint32_t irq_handler = (uint32_t)gba.iwram[0x7FFC]
                         | ((uint32_t)gba.iwram[0x7FFD] << 8)
                         | ((uint32_t)gba.iwram[0x7FFE] << 16)
                         | ((uint32_t)gba.iwram[0x7FFF] << 24);
    uint32_t bios_if = (uint32_t)gba.iwram[0x7FF8]
                     | ((uint32_t)gba.iwram[0x7FF9] << 8);
    fprintf(stderr, "IRQ_handler=0x%08X BIOS_IF=0x%04X\n", irq_handler, bios_if);
    fprintf(stderr, "IE=0x%04X IF=0x%04X IME=%d\n",
            gba.interrupt_enable, gba.interrupt_flags, gba.interrupt_master);
    fprintf(stderr, "PC=0x%08X CPSR=0x%08X mode=0x%02X\n",
            gba.cpu.r[15], gba.cpu.cpsr, gba.cpu.cpsr & 0x1F);

    /* Dump IRQ handler code */
    if (irq_handler >= 0x03000000u && irq_handler < 0x04000000u) {
        uint32_t iwram_offset = (irq_handler - 0x03000000u) & (CUPID_GBA_IWRAM_SIZE - 1);
        fprintf(stderr, "\nIRQ handler code at IWRAM 0x%04X:\n", iwram_offset);
        dump_iwram_at(&gba, iwram_offset, 64);
    }

    /* Continue to frame 269 and check again */
    for (frame = 268; frame < 270; frame++) {
        for (steps = 0; steps < 2000000; steps++) {
            cupid_gba_step(&gba);
            if (gba.frame_ready) { gba.frame_ready = false; break; }
        }
    }

    fprintf(stderr, "\n=== State after frame 269 ===\n");
    irq_handler = (uint32_t)gba.iwram[0x7FFC]
                | ((uint32_t)gba.iwram[0x7FFD] << 8)
                | ((uint32_t)gba.iwram[0x7FFE] << 16)
                | ((uint32_t)gba.iwram[0x7FFF] << 24);
    bios_if = (uint32_t)gba.iwram[0x7FF8]
            | ((uint32_t)gba.iwram[0x7FF9] << 8);
    fprintf(stderr, "IRQ_handler=0x%08X BIOS_IF=0x%04X\n", irq_handler, bios_if);
    fprintf(stderr, "IE=0x%04X IF=0x%04X IME=%d\n",
            gba.interrupt_enable, gba.interrupt_flags, gba.interrupt_master);
    fprintf(stderr, "PC=0x%08X CPSR=0x%08X mode=0x%02X\n",
            gba.cpu.r[15], gba.cpu.cpsr, gba.cpu.cpsr & 0x1F);

    if (irq_handler >= 0x03000000u && irq_handler < 0x04000000u) {
        uint32_t iwram_offset = (irq_handler - 0x03000000u) & (CUPID_GBA_IWRAM_SIZE - 1);
        fprintf(stderr, "\nIRQ handler code at IWRAM 0x%04X (frame 269):\n", iwram_offset);
        dump_iwram_at(&gba, iwram_offset, 64);
    }

    free(gba.rom);
    return 0;
}

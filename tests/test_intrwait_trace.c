/* Focused diagnostic: trace halt/IRQ behavior after BIOS intro ends */
#include "cupid/gba/gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    CupidGba gba;
    int frame, steps;
    int transition_frame = -1;

    memset(&gba, 0, sizeof(gba));
    cupid_gba_init(&gba);

    if (!cupid_gba_load_bios_file(&gba, "bootroms/gba_bios.bin")) {
        fprintf(stderr, "BIOS not found\n");
        return 1;
    }
    if (!cupid_gba_load_rom_file(&gba, "Pokemon - Leaf Green Version (U) (V1.1).gba")) {
        fprintf(stderr, "ROM not found\n");
        return 1;
    }

    fprintf(stderr, "Running BIOS mode to find game transition...\n");

    for (frame = 0; frame < 310; frame++) {
        uint16_t dispcnt_start = gba.display_control;

        for (steps = 0; steps < 2000000; steps++) {
            cupid_gba_step(&gba);
            if (gba.frame_ready) {
                gba.frame_ready = false;
                break;
            }
        }

        /* Detect transition from BIOS intro to game */
        if (dispcnt_start != 0x0080 && gba.display_control == 0x0080 &&
            frame > 200 && transition_frame < 0) {
            transition_frame = frame;
            fprintf(stderr, "\n=== TRANSITION at frame %d ===\n", frame);
        }

        if (frame >= 268 && frame <= 272) {
            uint32_t irq_handler = 0;
            uint32_t bios_if = 0;

            /* Read IRQ handler from IWRAM 0x7FFC */
            irq_handler = (uint32_t)gba.iwram[0x7FFC]
                        | ((uint32_t)gba.iwram[0x7FFD] << 8)
                        | ((uint32_t)gba.iwram[0x7FFE] << 16)
                        | ((uint32_t)gba.iwram[0x7FFF] << 24);

            /* Read BIOS_IF from IWRAM 0x7FF8 */
            bios_if = (uint32_t)gba.iwram[0x7FF8]
                    | ((uint32_t)gba.iwram[0x7FF9] << 8)
                    | ((uint32_t)gba.iwram[0x7FFA] << 16)
                    | ((uint32_t)gba.iwram[0x7FFB] << 24);

            fprintf(stderr,
                "Frame %d: DISPCNT=0x%04X PC=0x%08X CPSR=0x%08X mode=0x%02X "
                "I=%d T=%d halted=%d steps=%d\n"
                "  IME=%d IE=0x%04X IF=0x%04X IRQ_handler=0x%08X BIOS_IF=0x%08X\n",
                frame, gba.display_control, gba.cpu.r[15],
                gba.cpu.cpsr, gba.cpu.cpsr & 0x1F,
                (gba.cpu.cpsr >> 7) & 1, (gba.cpu.cpsr >> 5) & 1,
                gba.halted, steps,
                gba.interrupt_master, gba.interrupt_enable,
                gba.interrupt_flags, irq_handler, bios_if);
        }

        /* After frame 270, do a detailed step trace for ~50 steps */
        if (frame == 270) {
            fprintf(stderr, "\n=== STEP TRACE: 50 steps in frame 271 ===\n");
            int last_halted = -1;
            int halt_transitions = 0;

            for (steps = 0; steps < 500 && halt_transitions < 20; steps++) {
                uint32_t pc_before = gba.cpu.r[15];
                int halted_before = gba.halted;
                uint32_t cpsr_before = gba.cpu.cpsr;

                cupid_gba_step(&gba);

                if (gba.frame_ready) {
                    gba.frame_ready = false;
                    fprintf(stderr, "  [step %d] FRAME READY\n", steps);
                    break;
                }

                /* Log halt transitions and first 20 non-halted steps */
                if (halted_before != (int)gba.halted) {
                    halt_transitions++;
                    fprintf(stderr,
                        "  [step %d] HALT %s: PC=0x%08X→0x%08X CPSR=0x%08X "
                        "mode=0x%02X I=%d IME=%d IE=0x%04X IF=0x%04X\n",
                        steps,
                        gba.halted ? "ENTER" : "EXIT",
                        pc_before, gba.cpu.r[15], gba.cpu.cpsr,
                        gba.cpu.cpsr & 0x1F,
                        (gba.cpu.cpsr >> 7) & 1,
                        gba.interrupt_master,
                        gba.interrupt_enable, gba.interrupt_flags);
                }

                if (!gba.halted && steps < 50) {
                    fprintf(stderr,
                        "  [step %d] PC=0x%08X CPSR=0x%08X mode=0x%02X "
                        "I=%d IME=%d IE=0x%04X IF=0x%04X\n",
                        steps, gba.cpu.r[15], gba.cpu.cpsr,
                        gba.cpu.cpsr & 0x1F,
                        (gba.cpu.cpsr >> 7) & 1,
                        gba.interrupt_master,
                        gba.interrupt_enable, gba.interrupt_flags);
                }
            }
            fprintf(stderr, "  Total steps traced: %d, halt transitions: %d\n",
                    steps, halt_transitions);
        }
    }

    free(gba.rom);
    return 0;
}

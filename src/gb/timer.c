/* =========================================================================
 * Timer – DIV / TIMA / TAC
 *   Shared between Game Boy (DMG) and Game Boy Color (CGB).
 *   Also dispatches PPU and APU tick.
 * ========================================================================= */

#include "cupid/gb/timer.h"

#include "cupid/gb/gb.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/apu.h"

static void cupid_gb_tick_system_mcycle(CupidGb *gb)
{
    uint8_t  tac;
    uint16_t period;

    /* --- DIV (internal 16-bit counter; DIV register = upper byte) --- */
    gb->div_counter = (uint16_t)(gb->div_counter + 1u);
    if (gb->div_counter >= 64u) {
        gb->div_counter = (uint16_t)(gb->div_counter - 64u);
        gb->io_registers[0x04u] = (uint8_t)(gb->io_registers[0x04u] + 1u);
    }

    /* --- TIMA reload delay --- */
    if (gb->tima_overflow_delay > 0u) {
        gb->tima_overflow_delay = (uint8_t)(gb->tima_overflow_delay - 1u);
        if (gb->tima_overflow_delay == 0u) {
            gb->io_registers[0x05u] = gb->io_registers[0x06u];
            cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_TIMER);
        }
    }

    /* --- TIMA --- */
    tac = gb->io_registers[0x07u];
    if ((tac & 0x04u) != 0u) {
        switch (tac & 0x03u) {
        case 0u:  period = 256u; break;
        case 1u:  period = 4u;   break;
        case 2u:  period = 16u;  break;
        default:  period = 64u;  break;
        }

        gb->timer_counter = (uint16_t)(gb->timer_counter + 1u);
        if (gb->timer_counter >= period) {
            gb->timer_counter = (uint16_t)(gb->timer_counter - period);
            if (gb->io_registers[0x05u] == 0xffu) {
                gb->io_registers[0x05u] = 0x00u;
                gb->tima_overflow_delay  = 1u;
            } else {
                gb->io_registers[0x05u] = (uint8_t)(gb->io_registers[0x05u] + 1u);
            }
        }
    }

}

void cupid_gb_tick(CupidGb *gb, uint16_t cycles)
{
    uint16_t i;

    for (i = 0u; i < cycles; ++i) {
        cupid_gb_tick_system_mcycle(gb);

        if (!gb->double_speed) {
            cupid_gb_tick_ppu(gb, 1u);
            cupid_gb_tick_apu(gb, 4u);
        } else {
            gb->speed_phase = !gb->speed_phase;
            if (!gb->speed_phase) {
                cupid_gb_tick_ppu(gb, 1u);
                cupid_gb_tick_apu(gb, 4u);
            }
        }
    }
}

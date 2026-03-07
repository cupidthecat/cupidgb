/* =========================================================================
 * Timer – DIV / TIMA / TAC
 *   Shared between Game Boy (DMG) and Game Boy Color (CGB).
 *   Also dispatches PPU and APU tick.
 * ========================================================================= */

#include "cupid/gb/timer.h"

#include <stdio.h>

#include "cupid/gb/gb.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/apu.h"

static uint16_t cupid_gb_timer_internal_counter(const CupidGb *gb)
{
    return (uint16_t)(((uint16_t)gb->io_registers[0x04u] << 6u) | (gb->div_counter & 0x003fu));
}

static bool cupid_gb_timer_input_signal(uint16_t counter, uint8_t tac)
{
    uint16_t mask;

    if ((tac & 0x04u) == 0u) {
        return false;
    }

    switch (tac & 0x03u) {
    case 0u: mask = 1u << 7u; break;
    case 1u: mask = 1u << 1u; break;
    case 2u: mask = 1u << 3u; break;
    default: mask = 1u << 5u; break;
    }

    return (counter & mask) != 0u;
}

static void cupid_gb_timer_increment_tima(CupidGb *gb)
{
    if (gb->io_registers[0x05u] == 0xffu) {
        gb->io_registers[0x05u] = 0x00u;
        gb->tima_overflow_delay = 1u;
    } else {
        gb->io_registers[0x05u] = (uint8_t)(gb->io_registers[0x05u] + 1u);
    }
}

void cupid_gb_timer_apply_tima_write(CupidGb *gb, uint8_t value)
{
    if (gb == 0) {
        return;
    }

    if (gb->tima_reload_just_happened) {
        return;
    }

    gb->io_registers[0x05u] = value;
    if (gb->tima_overflow_delay > 0u) {
        gb->tima_overflow_delay = 0u;
    }
}

void cupid_gb_timer_apply_tma_write(CupidGb *gb, uint8_t value)
{
    if (gb == 0) {
        return;
    }

    gb->io_registers[0x06u] = value;
    if (gb->tima_reload_just_happened) {
        gb->io_registers[0x05u] = value;
    }
}

void cupid_gb_timer_apply_div_reset(CupidGb *gb)
{
    uint16_t old_counter;
    uint8_t tac;

    if (gb == 0) {
        return;
    }

    old_counter = cupid_gb_timer_internal_counter(gb);
    tac = gb->io_registers[0x07u];
    if (cupid_gb_timer_input_signal(old_counter, tac) &&
        !cupid_gb_timer_input_signal(0u, tac)) {
        cupid_gb_timer_increment_tima(gb);
    }

    gb->io_registers[0x04u] = 0u;
    gb->div_counter = 0u;
}

void cupid_gb_timer_apply_tac_write(CupidGb *gb, uint8_t value)
{
    uint16_t counter;
    uint8_t old_tac;
    uint8_t new_tac;

    if (gb == 0) {
        return;
    }

    counter = cupid_gb_timer_internal_counter(gb);
    old_tac = gb->io_registers[0x07u];
    new_tac = (uint8_t)(value & 0x07u);

    if (cupid_gb_timer_input_signal(counter, old_tac) &&
        !cupid_gb_timer_input_signal(counter, new_tac)) {
        cupid_gb_timer_increment_tima(gb);
    }

    gb->io_registers[0x07u] = new_tac;
}

static void cupid_gb_tick_serial(CupidGb *gb)
{
    uint8_t old_counter;

    if (gb == 0) {
        return;
    }

    old_counter = gb->serial_counter;
    gb->serial_counter = (uint8_t)((gb->serial_counter + 1u) & 0x7fu);

    if (old_counter != 0x7fu) {
        return;
    }

    if ((gb->io_registers[0x02u] & 0x81u) != 0x81u || gb->serial_bits_remaining == 0u) {
        return;
    }

    gb->io_registers[0x01u] = (uint8_t)((gb->io_registers[0x01u] << 1u) | 0x01u);
    gb->serial_bits_remaining = (uint8_t)(gb->serial_bits_remaining - 1u);

    if (gb->serial_bits_remaining == 0u) {
        gb->io_registers[0x02u] = (uint8_t)(gb->io_registers[0x02u] & (uint8_t)~0x80u);
        fputc((int)gb->serial_tx_latch, stdout);
        fflush(stdout);
        cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_SERIAL);
    }
}

static void cupid_gb_tick_system_mcycle(CupidGb *gb)
{
    uint16_t old_counter;
    uint16_t new_counter;
    uint8_t  tac;

    gb->tima_reload_just_happened = false;

    /* --- DIV (internal 16-bit counter; DIV register = upper byte) --- */
    old_counter = cupid_gb_timer_internal_counter(gb);
    gb->div_counter = (uint16_t)(gb->div_counter + 1u);
    if (gb->div_counter >= 64u) {
        gb->div_counter = (uint16_t)(gb->div_counter - 64u);
        gb->io_registers[0x04u] = (uint8_t)(gb->io_registers[0x04u] + 1u);
    }
    new_counter = cupid_gb_timer_internal_counter(gb);

    /* --- TIMA reload delay --- */
    if (gb->tima_overflow_delay > 0u) {
        gb->tima_overflow_delay = (uint8_t)(gb->tima_overflow_delay - 1u);
        if (gb->tima_overflow_delay == 0u) {
            gb->io_registers[0x05u] = gb->io_registers[0x06u];
            gb->tima_reload_just_happened = true;
            cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_TIMER);
        }
    }

    /* --- TIMA --- */
    tac = gb->io_registers[0x07u];
    if (cupid_gb_timer_input_signal(old_counter, tac) &&
        !cupid_gb_timer_input_signal(new_counter, tac)) {
        cupid_gb_timer_increment_tima(gb);
    }

    cupid_gb_tick_serial(gb);

}

void cupid_gb_tick(CupidGb *gb, uint16_t cycles)
{
    uint16_t i;

    for (i = 0u; i < cycles; ++i) {
        cupid_gb_tick_system_mcycle(gb);
        cupid_gb_tick_dma(gb);

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

/**
 * @file timer.c
 * @brief Game Boy timer subsystem (DIV / TIMA / TMA / TAC).
 *
 * Implements the 16-bit internal counter that drives the DIV register,
 * TIMA increment and overflow / reload logic (including the one M-cycle
 * reload delay), and TAC / DIV obscure glitch behavior (falling-edge
 * detection on the selected multiplexer bit).
 *
 * Also provides the top-level @ref cupid_gb_tick entry point that drives
 * one M-cycle of the entire system: timer, DMA, PPU, APU, and serial,
 * with correct CGB double-speed gating.
 *
 * Shared by DMG and CGB.
 */

#include "cupid/gb/timer.h"

#include <stdio.h>

#include "cupid/gb/gb.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/apu.h"

/**
 * @brief Returns the full 16-bit internal timer counter.
 *
 * The hardware maintains a 16-bit free-running counter; the DIV register
 * (0xFF04) exposes only the upper 8 bits. This helper reconstructs
 * the full value from the high byte in the I/O register and the low
 * 6 bits held in `gb->div_counter`.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return The 16-bit internal counter value.
 */
static uint16_t cupid_gb_timer_internal_counter(const CupidGb *gb)
{
    return (uint16_t)(((uint16_t)gb->io_registers[0x04u] << 6u) | (gb->div_counter & 0x003fu));
}

/**
 * @brief Returns the current TIMA input signal derived from the internal counter and TAC.
 *
 * The timer is implemented as a falling-edge detector on one bit of the
 * internal 16-bit counter, selected by TAC bits 1–0:
 *   - `00` → bit 7  (4096 Hz)
 *   - `01` → bit 1  (262144 Hz)
 *   - `10` → bit 3  (65536 Hz)
 *   - `11` → bit 5  (16384 Hz)
 *
 * If the timer enable bit (TAC bit 2) is clear, the signal is always `false`.
 *
 * @param counter The current 16-bit internal counter value.
 * @param tac     The current TAC register value (0xFF07).
 *
 * @return `true` if the selected counter bit is set and the timer is enabled.
 */
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

/**
 * @brief Increments TIMA by one, scheduling an overflow reload if it wraps.
 *
 * When TIMA overflows (0xFF → 0x00), instead of immediately reloading
 * from TMA and requesting an interrupt, a one M-cycle delay
 * (`gb->tima_overflow_delay`) is set. The reload and the TIMER interrupt
 * are delivered at the end of that delay cycle by
 * @ref cupid_gb_tick_system_mcycle.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_timer_increment_tima(CupidGb *gb)
{
    if (gb->io_registers[0x05u] == 0xffu) {
        gb->io_registers[0x05u] = 0x00u;
        gb->tima_overflow_delay = 1u;
    } else {
        gb->io_registers[0x05u] = (uint8_t)(gb->io_registers[0x05u] + 1u);
    }
}

/**
 * @brief Handles a CPU write to the TIMA register (0xFF05).
 *
 * Writes are intercepted to implement hardware quirks:
 * - If the TMA reload has already been latched into TIMA during the
 *   current M-cycle (`tima_reload_just_happened`), the write is silently
 *   discarded.
 * - If an overflow is pending but has not yet been serviced
 *   (`tima_overflow_delay > 0`), the write cancels the pending reload
 *   and interrupt and stores the new value directly.
 *
 * @param gb    Pointer to the Game Boy state.
 * @param value The value to write to TIMA.
 *
 * @note Does nothing if @p gb is NULL.
 */
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

/**
 * @brief Handles a CPU write to the TMA register (0xFF06).
 *
 * Stores the new modulo value. If the TMA reload was latched into TIMA
 * during the current M-cycle (`tima_reload_just_happened`), TIMA is
 * also updated immediately to reflect the new modulo, matching hardware
 * behavior.
 *
 * @param gb    Pointer to the Game Boy state.
 * @param value The value to write to TMA.
 *
 * @note Does nothing if @p gb is NULL.
 */
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

/**
 * @brief Handles a CPU write to the DIV register (0xFF04), which resets it to zero.
 *
 * Resetting DIV resets the entire 16-bit internal counter to 0. If this
 * causes a falling edge on the selected TIMA input bit (the bit was 1
 * before the reset and would be 0), TIMA is incremented immediately,
 * matching the hardware glitch.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
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

/**
 * @brief Handles a CPU write to the TAC register (0xFF07).
 *
 * Only bits 2–0 are writable. Changing the selected multiplexer bit or
 * the enable flag can cause a spurious TIMA increment: if the old TAC
 * produced a high input signal and the new TAC produces a low signal
 * (falling edge), TIMA increments immediately.
 *
 * @param gb    Pointer to the Game Boy state.
 * @param value The value to write to TAC (only bits 2–0 are used).
 *
 * @note Does nothing if @p gb is NULL.
 */
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

/**
 * @brief Advances the internal serial clock by one step and transfers a bit if due.
 *
 * The serial controller runs off a 7-bit counter that wraps every 128
 * steps. On each wrap, if the internal clock is selected (SC bit 0)
 * and the transfer-start flag is set (SC bit 7), one bit is shifted
 * out of SB (the outgoing bit is replaced by 1, simulating an
 * unconnected cable). When all 8 bits have been transferred, the
 * transfer-start flag is cleared, the byte is printed to stdout, and
 * a SERIAL interrupt is requested.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
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

/**
 * @brief Advances all timer and serial state by one M-cycle (4 T-cycles).
 *
 * Performs, in order:
 *   1. Clears the one-cycle TIMA-reload flag.
 *   2. Increments the 16-bit internal counter; the DIV register (high
 *      byte) is updated when the low 6 bits overflow.
 *   3. Services any pending TIMA overflow delay: reloads TIMA from TMA
 *      and requests the TIMER interrupt after the one-cycle delay.
 *   4. Checks for a TIMA falling edge and increments TIMA if detected.
 *   5. Calls @ref cupid_gb_tick_serial.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_tick_system_mcycle(CupidGb *gb)
{
    uint16_t old_counter;
    uint16_t new_counter;
    uint8_t  tac;

    gb->tima_reload_just_happened = false;

    // DIV (internal 16-bit counter; DIV register = upper byte)
    old_counter = cupid_gb_timer_internal_counter(gb);
    gb->div_counter = (uint16_t)(gb->div_counter + 1u);
    if (gb->div_counter >= 64u) {
        gb->div_counter = (uint16_t)(gb->div_counter - 64u);
        gb->io_registers[0x04u] = (uint8_t)(gb->io_registers[0x04u] + 1u);
    }
    new_counter = cupid_gb_timer_internal_counter(gb);

    // TIMA reload delay
    if (gb->tima_overflow_delay > 0u) {
        gb->tima_overflow_delay = (uint8_t)(gb->tima_overflow_delay - 1u);
        if (gb->tima_overflow_delay == 0u) {
            gb->io_registers[0x05u] = gb->io_registers[0x06u];
            gb->tima_reload_just_happened = true;
            cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_TIMER);
        }
    }

    // TIMA
    tac = gb->io_registers[0x07u];
    if (cupid_gb_timer_input_signal(old_counter, tac) &&
        !cupid_gb_timer_input_signal(new_counter, tac)) {
        cupid_gb_timer_increment_tima(gb);
    }

    cupid_gb_tick_serial(gb);

}

/**
 * @brief Advances the entire Game Boy system by @p cycles M-cycles.
 *
 * For each M-cycle this function:
 *   1. Ticks the timer/serial subsystem (@ref cupid_gb_tick_system_mcycle).
 *   2. Ticks OAM DMA (@ref cupid_gb_tick_dma).
 *   3. In normal-speed mode: ticks the PPU (1 M-cycle) and APU (4 T-cycles).
 *   4. In CGB double-speed mode: ticks the PPU and APU only on every
 *      second M-cycle (using `gb->speed_phase` as a toggle).
 *
 * @param gb     Pointer to the Game Boy state.
 * @param cycles Number of M-cycles to advance.
 */
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

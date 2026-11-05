#include "cupid/gb/timer.h"

#include <stdio.h>

#include "cupid/gb/apu.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/ppu.h"

enum {
    SERIAL_DATA = 0x01,
    SERIAL_CONTROL = 0x02,
    DIVIDER = 0x04,
    TIMER_VALUE = 0x05,
    TIMER_MODULO = 0x06,
    TIMER_CONTROL = 0x07
};

/* The stored divider counts machine cycles. DIV exposes its upper eight bits;
 * the six remaining bits live in div_counter. This preserves divider phase
 * across direct register writes, save states, HALT, and speed changes. */
static uint16_t divider_phase(const CupidGb *gb) {
    return (uint16_t)(gb->io_registers[DIVIDER] * 64u + (gb->div_counter & 63u));
}

static bool timer_level(uint16_t phase, uint8_t control) {
    static const uint8_t shifts[4] = {7, 1, 3, 5};
    return (control & 4u) != 0u && (((unsigned)phase >> shifts[control & 3u]) & 1u) != 0u;
}

static void timer_pulse(CupidGb *gb) {
    gb->io_registers[TIMER_VALUE]++;
    if (gb->io_registers[TIMER_VALUE] == 0u) {
        /* The overflow cycle exposes zero. Reload and IRQ occur one machine
         * cycle later, unless a TIMA write cancels that pending operation. */
        gb->tima_overflow_delay = 1u;
    }
}

void cupid_gb_serial_master_edge(CupidGb *gb) {
    if (!gb) {
        return;
    }

    const bool falling = gb->serial_master_clock;
    gb->serial_master_clock = !falling;
    if (!falling || gb->serial_bits_remaining == 0u ||
        (gb->io_registers[SERIAL_CONTROL] & 0x81u) != 0x81u) {
        return;
    }

    /* An unplugged serial input is high. SB remains readable during transfer. */
    gb->io_registers[SERIAL_DATA] = (uint8_t)(gb->io_registers[SERIAL_DATA] * 2u + 1u);
    --gb->serial_bits_remaining;
    if (gb->serial_bits_remaining != 0u) {
        return;
    }

    gb->io_registers[SERIAL_CONTROL] &= 0x7fu;
    cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_SERIAL);
    fputc(gb->serial_tx_latch, stdout);
    fflush(stdout);
}

/* DIV changes affect three independent edge consumers. Resetting DIV uses this
 * same path, so a register write can clock TIMA, serial, or the audio sequencer. */
static void divider_change(CupidGb *gb, uint16_t phase) {
    const uint16_t before = divider_phase(gb);
    const uint8_t control = gb->io_registers[TIMER_CONTROL];
    if (timer_level(before, control) && !timer_level(phase, control)) {
        timer_pulse(gb);
    }

    const unsigned serial_shift = gb->cgb_mode && (gb->io_registers[SERIAL_CONTROL] & 2u) ? 0u : 5u;
    if ((((unsigned)before >> serial_shift) & 1u) && !(((unsigned)phase >> serial_shift) & 1u)) {
        cupid_gb_serial_master_edge(gb);
    }

    const unsigned audio_shift = gb->double_speed ? 11u : 10u;
    if ((((unsigned)before >> audio_shift) & 1u) && !(((unsigned)phase >> audio_shift) & 1u)) {
        cupid_gb_apu_div_event(gb);
    }

    gb->io_registers[DIVIDER] = (uint8_t)(phase >> 6u);
    gb->div_counter = phase & 63u;
}

void cupid_gb_timer_apply_div_reset(CupidGb *gb) {
    if (gb) {
        divider_change(gb, 0u);
    }
}

void cupid_gb_timer_apply_tac_write(CupidGb *gb, uint8_t value) {
    if (!gb) {
        return;
    }

    const uint16_t phase = divider_phase(gb);
    const bool old_level = timer_level(phase, gb->io_registers[TIMER_CONTROL]);
    gb->io_registers[TIMER_CONTROL] = value & 7u;
    if (old_level && !timer_level(phase, value)) {
        timer_pulse(gb);
    }
}

void cupid_gb_timer_apply_tima_write(CupidGb *gb, uint8_t value) {
    if (gb && !gb->tima_reload_just_happened) {
        gb->io_registers[TIMER_VALUE] = value;
        gb->tima_overflow_delay = 0u;
    }
}

void cupid_gb_timer_apply_tma_write(CupidGb *gb, uint8_t value) {
    if (gb) {
        gb->io_registers[TIMER_MODULO] = value;
        if (gb->tima_reload_just_happened) {
            gb->io_registers[TIMER_VALUE] = value;
        }
    }
}

static void clock_cpu_domain(CupidGb *gb) {
    if (gb->cpu.stopped) {
        return;
    }

    gb->tima_reload_just_happened = false;
    if (gb->tima_overflow_delay != 0u && --gb->tima_overflow_delay == 0u) {
        gb->io_registers[TIMER_VALUE] = gb->io_registers[TIMER_MODULO];
        gb->tima_reload_just_happened = true;
        cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_TIMER);
    }
    divider_change(gb, (uint16_t)(divider_phase(gb) + 1u));
}

void cupid_gb_tick(CupidGb *gb, uint16_t cycles) {
    if (!gb) {
        return;
    }

    while (cycles-- != 0u) {
        clock_cpu_domain(gb);
        cupid_gb_tick_dma(gb);

        /* CPU timers and OAM DMA run twice as fast in double speed. The LCD and
         * sound generators stay in the base clock domain, including during HALT. */
        if (gb->double_speed) {
            gb->speed_phase = !gb->speed_phase;
            if (gb->speed_phase) {
                continue;
            }
        }
        gb->elapsed_cycles += 4u;
        cupid_gb_tick_ppu(gb, 1u);
        cupid_gb_tick_apu(gb, 4u);
    }
}

uint8_t cupid_gb_tick_halt(CupidGb *gb) {
    if (!gb) {
        return 0;
    }
    const uint8_t before = (uint8_t)(gb->interrupt_enable & gb->interrupt_flags & 0x1fu);
    const bool first_cycle = gb->cpu.just_halted;
    gb->cpu.just_halted = false;
    if (gb->model == CUPID_GB_MODEL_CGB || first_cycle) {
        /* Color hardware and the first cycle after HALT poll before the cycle. */
        cupid_gb_tick(gb, 1u);
        return before;
    }

    clock_cpu_domain(gb);
    cupid_gb_tick_dma(gb);
    const uint8_t middle = cupid_gb_tick_ppu_halt(gb);
    gb->elapsed_cycles += 4u;
    cupid_gb_tick_apu(gb, 4u);
    /* Monochrome hardware polls after the first two LCD dots. Timer and serial
     * divider transitions occupy the third T-cycle, beyond that polling point. */
    return (uint8_t)((middle & (uint8_t)~0x0cu) | (before & 0x0cu));
}

static void clock_lcd_dot(CupidGb *gb) {
    ++gb->elapsed_cycles;
    cupid_gb_tick_ppu_dots(gb, 1u);
    cupid_gb_tick_apu(gb, 1u);
}

void cupid_gb_cpu_write(CupidGb *gb, uint16_t address, uint8_t value, uint16_t leading_mcycles) {
    if (address != 0xff40u || gb->model == CUPID_GB_MODEL_CGB || leading_mcycles == 0u) {
        cupid_gb_tick(gb, leading_mcycles);
        cupid_gb_write_u8(gb, address, value);
        return;
    }

    cupid_gb_tick(gb, (uint16_t)(leading_mcycles - 1u));
    const uint8_t previous = gb->io_registers[CUPID_GB_IO_LCDC];
    clock_lcd_dot(gb);
    clock_lcd_dot(gb);
    /* LCD control lines settle before the ordinary write boundary. Background
     * enable is already visible after two dots; the complete value follows one
     * dot later. Keep the CPU-cycle total unchanged while exposing those edges. */
    if (gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2) {
        cupid_gb_write_u8(gb, address, value);
        cupid_gb_write_u8(gb, address, previous);
    } else {
        cupid_gb_write_u8(gb, address, (uint8_t)(previous | (value & 1u)));
    }
    clock_cpu_domain(gb);
    cupid_gb_tick_dma(gb);
    clock_lcd_dot(gb);
    cupid_gb_write_u8(gb, address, value);
    clock_lcd_dot(gb);
}

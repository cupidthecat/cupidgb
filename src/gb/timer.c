#include "cupid/gb/timer.h"

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
    if (gb->serial_output_callback) {
        gb->serial_output_callback(gb, gb->serial_tx_latch, gb->serial_output_user_data);
    }
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
    if (!(((unsigned)before >> audio_shift) & 1u) && (((unsigned)phase >> audio_shift) & 1u)) {
        cupid_gb_apu_div_secondary_event(gb);
    }

    gb->io_registers[DIVIDER] = (uint8_t)(phase >> 6u);
    gb->div_counter = phase & 63u;
}

void cupid_gb_timer_apply_div_reset(CupidGb *gb) {
    if (gb) {
        gb->stop_divider_hold_cycles = 0;
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

static void switch_clock_segment(CupidGb *gb, unsigned cycles, unsigned *half_dot) {
    const unsigned frozen =
        cycles < gb->speed_switch_freeze_cycles ? cycles : gb->speed_switch_freeze_cycles;
    gb->speed_switch_freeze_cycles = (uint8_t)(gb->speed_switch_freeze_cycles - frozen);
    const unsigned ticks = (cycles - frozen) * (gb->double_speed ? 1u : 2u);
    if (cupid_gb_lcd_enabled(gb))
        gb->speed_switch_alignment = (uint8_t)((gb->speed_switch_alignment + ticks) & 7u);
    const unsigned dots = (*half_dot + ticks) / 2u;
    *half_dot = (*half_dot + ticks) & 1u;
    gb->elapsed_cycles += dots;
    if (dots)
        cupid_gb_tick_ppu_dots(gb, (uint16_t)dots);
}

static void tick_switch_cycle(CupidGb *gb) {
    const bool old_speed = gb->double_speed;
    const unsigned toggle = gb->speed_switch_toggle_cycles;
    const bool divider_speed = old_speed != (toggle != 0 && toggle <= 4);
    const bool hold = !gb->cpu.stopped && gb->stop_divider_hold_cycles != 0;
    if (hold) {
        gb->stop_divider_hold_cycles =
            gb->stop_divider_hold_cycles > 4 ? (uint8_t)(gb->stop_divider_hold_cycles - 4) : 0;
    } else {
        gb->double_speed = divider_speed;
        clock_cpu_domain(gb);
        gb->double_speed = old_speed;
    }
    if (gb->speed_switch_halt_cycles) {
        if (gb->speed_switch_halt_cycles <= 4) {
            gb->speed_switch_halt_cycles = 0;
            gb->cpu.halted = false;
        } else {
            gb->speed_switch_halt_cycles -= 4;
        }
    }
    cupid_gb_tick_dma(gb);
    if (divider_speed)
        gb->speed_phase = !gb->speed_phase;

    unsigned half_dot = 0;
    if (toggle && toggle <= 4) {
        gb->speed_switch_toggle_cycles = 0;
        if (toggle < 4)
            switch_clock_segment(gb, toggle, &half_dot);
        gb->double_speed = !old_speed;
        switch_clock_segment(gb, toggle < 4 ? 4u - toggle : 4u, &half_dot);
        gb->io_registers[0x4d] =
            (uint8_t)((gb->io_registers[0x4d] & 1u) | (gb->double_speed ? 0x80u : 0));
    } else {
        if (toggle)
            gb->speed_switch_toggle_cycles = (uint8_t)(toggle - 4);
        switch_clock_segment(gb, 4, &half_dot);
    }
    /* DIV's reset hold also stops its audio clock. The short oscillator freeze
     * suppresses LCD time only; an ordinary DIV clock still reaches the APU. */
    if (!hold)
        cupid_gb_tick_apu(gb, divider_speed ? 2u : 4u);
}

void cupid_gb_tick(CupidGb *gb, uint16_t cycles) {
    if (!gb) {
        return;
    }

    while (cycles-- != 0u) {
        if (gb->speed_switch_halt_cycles || gb->speed_switch_toggle_cycles ||
            gb->speed_switch_freeze_cycles || gb->stop_divider_hold_cycles) {
            tick_switch_cycle(gb);
            continue;
        }
        clock_cpu_domain(gb);
        cupid_gb_tick_dma(gb);

        /* Each CPU machine cycle occupies two LCD dots in double speed.
         * Batching pairs of cycles would move reads and interrupts by two dots. */
        if (gb->double_speed) {
            gb->speed_phase = !gb->speed_phase;
            if (cupid_gb_lcd_enabled(gb))
                gb->speed_switch_alignment ^= 4u;
        }
        const uint16_t dots = gb->double_speed ? 2u : 4u;
        gb->elapsed_cycles += dots;
        cupid_gb_tick_ppu_dots(gb, dots);
        cupid_gb_tick_apu(gb, dots);
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

void cupid_gb_tick_color_dma_slot(CupidGb *gb, bool cpu_boundary) {
    if (!gb)
        return;
    if (cpu_boundary) {
        clock_cpu_domain(gb);
        cupid_gb_tick_dma(gb);
        if (gb->double_speed)
            gb->speed_phase = !gb->speed_phase;
    }
    if (cupid_gb_lcd_enabled(gb))
        gb->speed_switch_alignment ^= 4u;
    clock_lcd_dot(gb);
    clock_lcd_dot(gb);
}

void cupid_gb_acknowledge_interrupt(CupidGb *gb, uint8_t mask) {
    if (!gb)
        return;

    /* Acknowledge halfway through the final entry cycle, at CPU T18.
     * Earlier PPU edges are cleared; later edges must remain pending.
     * The divider's third-T-cycle event follows the acknowledge edge. */
    const unsigned half = gb->double_speed ? 1u : 2u;
    if (gb->double_speed && cupid_gb_lcd_enabled(gb))
        gb->speed_switch_alignment ^= 4u;
    for (unsigned dot = 0; dot < half; ++dot)
        clock_lcd_dot(gb);
    gb->interrupt_flags &= (uint8_t)~mask;
    clock_cpu_domain(gb);
    cupid_gb_tick_dma(gb);
    if (gb->double_speed)
        gb->speed_phase = !gb->speed_phase;
    for (unsigned dot = 0; dot < half; ++dot)
        clock_lcd_dot(gb);
}

typedef enum InterruptWriteKind {
    INTERRUPT_WRITE_ORDINARY,
    INTERRUPT_WRITE_STAT,
    INTERRUPT_WRITE_LCDC,
    INTERRUPT_WRITE_LCDC_DOUBLE
} InterruptWriteKind;

typedef struct InterruptWrite {
    uint16_t address;
    uint8_t value, previous;
    unsigned early, commit, sample;
    InterruptWriteKind kind;
} InterruptWrite;

static InterruptWrite interrupt_write_plan(const CupidGb *gb, uint16_t address, uint8_t value,
                                           unsigned boundary, bool first) {
    InterruptWrite write = {address, value, 0, 0, boundary, boundary, INTERRUPT_WRITE_ORDINARY};
    if (address == 0xff41u) {
        write.previous = gb->io_registers[CUPID_GB_IO_STAT];
        write.kind = INTERRUPT_WRITE_STAT;
        write.early = boundary;
        write.commit = write.sample = boundary + 1;
    } else if (address == 0xff40u) {
        write.previous = gb->io_registers[CUPID_GB_IO_LCDC];
        if (gb->model != CUPID_GB_MODEL_CGB) {
            write.kind = INTERRUPT_WRITE_LCDC;
            write.early = boundary - 2;
            write.commit = write.sample = boundary - 1;
        } else if (gb->double_speed) {
            write.kind = INTERRUPT_WRITE_LCDC_DOUBLE;
            write.early = boundary - 2;
        } else if ((write.previous & (uint8_t)~value & 0x10u) != 0u) {
            /* Tile selection has a trailing one-T-cycle conflict window. */
            write.sample = boundary + 1;
        }
    } else if (address == 0xff0fu && first) {
        /* A first stack write to IF wins over that cycle's hardware request.
         * The second stack write to IF has its separate old-value sample. */
        write.commit = write.sample = boundary + 1;
    }
    return write;
}

static void interrupt_write_early(CupidGb *gb, const InterruptWrite *write) {
    uint8_t value;
    if (write->kind == INTERRUPT_WRITE_STAT) {
        if (gb->model != CUPID_GB_MODEL_CGB) {
            value = 0xff;
        } else if (gb->double_speed) {
            value = (uint8_t)((write->value & (uint8_t)~8u) | (write->previous & 8u));
        } else {
            value = (uint8_t)((write->value & (uint8_t)~0x40u) | (write->previous & 0x40u));
        }
        cupid_gb_write_u8(gb, write->address, value);
    } else if (write->kind == INTERRUPT_WRITE_LCDC_DOUBLE) {
        value = (uint8_t)((write->value & (uint8_t)~0x81u) | (write->previous & 0x81u));
        cupid_gb_write_u8(gb, write->address, value);
    } else if (gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2) {
        /* Object fetching observes the pulse even though the old register value
         * is restored before the next LCD dot. */
        cupid_gb_write_u8(gb, write->address, write->value);
        cupid_gb_write_u8(gb, write->address, write->previous);
    } else {
        cupid_gb_write_u8(gb, write->address, (uint8_t)(write->previous | (write->value & 1u)));
    }
}

void cupid_gb_interrupt_tail(CupidGb *gb, uint16_t return_address) {
    const uint16_t high_address = --gb->cpu.sp;
    const uint16_t low_address = (uint16_t)(high_address - 1u);
    const InterruptWrite high =
        interrupt_write_plan(gb, high_address, (uint8_t)(return_address >> 8), 12, true);
    InterruptWrite low = interrupt_write_plan(gb, low_address, (uint8_t)return_address, 16, false);
    uint8_t enabled = 0, selected = 0, low_old_if = 0;

    for (unsigned phase = 9; phase <= 20; ++phase) {
        if (cupid_gb_lcd_enabled(gb))
            gb->speed_switch_alignment =
                (uint8_t)((gb->speed_switch_alignment + (gb->double_speed ? 1u : 2u)) & 7u);
        /* Odd double-speed T-cycles fall between LCD dots. This local phase is
         * fully consumed before returning; native states need no half-dot field. */
        if (!gb->double_speed || !(phase & 1u)) {
            clock_lcd_dot(gb);
        } else {
            /* CGB LCD-start comparison settles on the first half-dot. No
             * oscillator or sample clock advances until the complete dot. */
            cupid_gb_sync_lcd_startup(gb);
        }
        if (high.early == phase) {
            interrupt_write_early(gb, &high);
        }
        if (high.commit == phase) {
            cupid_gb_write_u8(gb, high.address, high.value);
        }
        if (high.sample == phase) {
            enabled = gb->interrupt_enable;
            --gb->cpu.sp;
            /* The second register access sees the completed first write. */
            low = interrupt_write_plan(gb, low_address, (uint8_t)return_address, 16, false);
        }
        if (low.early == phase) {
            interrupt_write_early(gb, &low);
        }
        if (low.commit == phase) {
            low_old_if = gb->interrupt_flags;
            cupid_gb_write_u8(gb, low.address, low.value);
        }
        if (low.sample == phase) {
            uint8_t requested = low.address == 0xff0fu ? low_old_if : gb->interrupt_flags;
            uint8_t pending = (uint8_t)(enabled & requested & 0x1fu);
            selected = (uint8_t)(pending & (uint8_t)(0u - pending));
        }
        if (phase == 18) {
            /* Low IF writes replace the register, but acknowledgment still clears
             * the request chosen from its pre-write value. Later requests survive. */
            gb->interrupt_flags &= (uint8_t)~selected;
            gb->cpu.pc = 0;
            for (unsigned bit = 0; bit < 5; ++bit) {
                if (selected == (1u << bit)) {
                    gb->cpu.pc = (uint16_t)(0x40u + 8u * bit);
                }
            }
        }
        if ((phase & 3u) == 2u) {
            clock_cpu_domain(gb);
            cupid_gb_tick_dma(gb);
            if (gb->double_speed) {
                gb->speed_phase = !gb->speed_phase;
            }
        }
    }
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

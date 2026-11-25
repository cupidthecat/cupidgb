/**
 * @file    timer.c
 * @brief   Game Boy timer, serial, divider, and shared peripheral clock delivery.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */
#include "cupid/gb/timer.h"

#include "cupid/gb/apu.h"
#include "cupid/gb/camera.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/infrared.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "display_clock.h"
#include "rtc_internal.h"

enum {
    SERIAL_DATA = 0x01,
    SERIAL_CONTROL = 0x02,
    DIVIDER = 0x04,
    TIMER_VALUE = 0x05,
    TIMER_MODULO = 0x06,
    TIMER_CONTROL = 0x07
};

/* DIV and div_counter store completed four-T-cycle periods. The separate
 * divider_subcycle retains unheld CPU clocks between those periods, including
 * a partial period left by a speed change during a color DMA transfer. */
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

static bool serial_shift(CupidGb *gb) {
    gb->io_registers[SERIAL_DATA] <<= 1;
    if (--gb->serial_bits_remaining == 0u) {
        gb->io_registers[SERIAL_CONTROL] &= 0x7fu;
        cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_SERIAL);
        return true;
    }
    return false;
}

static void serial_launch(CupidGb *gb) {
    if (gb->serial_link.launch) {
        const bool outgoing = (gb->io_registers[SERIAL_DATA] & 0x80u) != 0u;
        gb->serial_link.launch(gb, outgoing, gb->serial_link.user_data);
    }
}

void cupid_gb_serial_master_edge(CupidGb *gb) {
    if (!gb) {
        return;
    }

    gb->serial_master_clock = !gb->serial_master_clock;
    if (gb->serial_master_clock || gb->serial_bits_remaining == 0u ||
        (gb->io_registers[SERIAL_CONTROL] & 0x81u) != 0x81u) {
        return;
    }

    const bool complete = serial_shift(gb);
    /* A disconnected input is pulled high. Sampling may clock the peer. */
    const bool incoming =
        !gb->serial_link.sample || gb->serial_link.sample(gb, gb->serial_link.user_data);
    gb->io_registers[SERIAL_DATA] |= incoming;
    if (!complete) {
        serial_launch(gb);
    } else if (gb->serial_output_callback) {
        gb->serial_output_callback(gb, gb->serial_tx_latch, gb->serial_output_user_data);
    }
}

void cupid_gb_serial_apply_control_write(CupidGb *gb, uint8_t value) {
    /* SC restarts the byte counter before driving the old clock low. A bit
     * sampled on that edge counts toward the next armed transfer. */
    gb->serial_bits_remaining = 8u;
    if (gb->serial_master_clock) {
        cupid_gb_serial_master_edge(gb);
    }

    const uint8_t writable = gb->cgb_mode ? 0x83u : 0x81u;
    gb->io_registers[SERIAL_CONTROL] = (uint8_t)((value & writable) | (uint8_t)~writable);
    /* Retain the selected tap even while idle or after a compatibility-mode
     * change. The old high phase above still belongs to the previous control. */
    gb->serial_fast_clock = gb->cgb_mode && (value & 2u);
    if (!(value & 0x80u)) {
        gb->serial_bits_remaining = 0u;
        return;
    }
    gb->serial_tx_latch = gb->io_registers[SERIAL_DATA];
    if (value & 1u) {
        serial_launch(gb);
    }
}

bool cupid_gb_serial_external_bit(const CupidGb *gb) {
    return gb && (gb->io_registers[SERIAL_CONTROL] & 0x80u) &&
           ((gb->io_registers[SERIAL_CONTROL] & 1u) || (gb->io_registers[SERIAL_DATA] & 0x80u));
}

void cupid_gb_serial_external_clock(CupidGb *gb, bool incoming) {
    if (gb && gb->serial_bits_remaining && (gb->io_registers[SERIAL_CONTROL] & 0x81u) == 0x80u) {
        serial_shift(gb);
        gb->io_registers[SERIAL_DATA] |= incoming;
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

    const unsigned serial_shift = gb->serial_fast_clock ? 0u : 5u;
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
        /* Software resets restart the divider period even when its audio
         * tap was already low. Late color hardware holds a pending envelope
         * through the startup period and the following fast period. */
        if (gb->apu.envelope_delay)
            gb->apu.envelope_delay = 4;
        gb->divider_subcycle = 0;
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

    cupid_gb_apu_finish_divider_period(gb);
    gb->tima_reload_just_happened = false;
    if (gb->tima_overflow_delay != 0u && --gb->tima_overflow_delay == 0u) {
        gb->io_registers[TIMER_VALUE] = gb->io_registers[TIMER_MODULO];
        gb->tima_reload_just_happened = true;
        cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_TIMER);
    }
    divider_change(gb, (uint16_t)(divider_phase(gb) + 1u));
    cupid_gb_apu_credit_divider(gb, gb->double_speed ? 2u : 4u);
}

static unsigned consume_divider_cycles(CupidGb *gb, unsigned cycles) {
    if (gb->cpu.stopped)
        return 0;
    const unsigned held =
        cycles < gb->stop_divider_hold_cycles ? cycles : gb->stop_divider_hold_cycles;
    gb->stop_divider_hold_cycles = (uint8_t)(gb->stop_divider_hold_cycles - held);
    const unsigned elapsed = (unsigned)gb->divider_subcycle + cycles - held;
    gb->divider_subcycle = (uint8_t)(elapsed & 3u);
    return elapsed / 4u;
}

static void clock_camera_phi(CupidGb *gb, unsigned cycles) {
    /* PHI follows CPU clocks, independently of DIV reset and oscillator freeze. */
    if (gb->header.mbc_type == CUPID_GB_CAMERA && !gb->cpu.halted && !gb->cpu.stopped)
        cupid_gb_camera_tick(&gb->camera, cycles);
}

static void clock_cpu_span(CupidGb *gb, unsigned cycles) {
    /* Each new delivery clears the previous PCM readback. Divider edges in
     * this span can put a new value on the bus before the next access. */
    gb->apu.pcm_read_inhibit = 0;
    if (gb->cpu.stopped) {
        /* STOP keeps output sampling alive once per delivered CPU span. The
         * color APU also keeps running; monochrome oscillators remain held. */
        cupid_gb_apu_credit_divider(gb, gb->double_speed ? 2u : 4u);
    } else {
        unsigned edges = consume_divider_cycles(gb, cycles);
        while (edges--)
            clock_cpu_domain(gb);
    }
    clock_camera_phi(gb, cycles);
}

static void advance_base_time(CupidGb *gb, unsigned dots) {
    gb->elapsed_cycles += dots;
}

static void sample_rumble_delivery(CupidGb *gb, uint32_t pending_half_dots) {
    /* A delivery contributes one motor sample before its first PPU edge.
     * All subsequent subdivisions share that sample until the delivery ends. */
    if (!pending_half_dots && gb->rumble_sample_callback)
        gb->rumble_sample_callback(gb, cupid_gb_rumble_strength(gb), gb->rumble_sample_user_data);
}

static void finish_display_span(CupidGb *gb, unsigned half_dots) {
    if (!half_dots)
        return;
    /* Input samples belong to completed CPU deliveries, including a lone color
     * half-dot. Internal pixel/DMA subdivision must not sample host changes early. */
    if (gb->joyp_sample_pending || gb->joyp_select_delay)
        cupid_gb_input_tick_half_dots(gb, half_dots);
    if (!cupid_gb_display_clock_capable(gb))
        return;
    /* Internal dot/M-cycle subdivision serves DMA arbitration. The receiver
     * samples one completed CPU delivery, after its display and DMA work. */
    cupid_gb_display_clock_advance(gb, half_dots);
    cupid_gb_infrared_tick_half_dots(gb, half_dots);
}

static inline void sample_window_edge(CupidGb *gb, unsigned before, unsigned after) {
    if (cupid_gb_display_clock_window_pending(gb))
        cupid_gb_display_clock_sample_window(gb, before, after);
}

static void clock_lcd_dot(CupidGb *gb) {
    advance_base_time(gb, 1u);
    cupid_gb_tick_ppu_dots(gb, 1u);
}

static void clock_lcd_span(CupidGb *gb, unsigned dots, unsigned before, unsigned first_dot) {
    if (!dots)
        return;
    if (!cupid_gb_display_clock_window_pending(gb)) {
        advance_base_time(gb, dots);
        cupid_gb_tick_ppu_dots(gb, (uint16_t)dots);
        return;
    }

    unsigned end = before + first_dot;
    for (unsigned dot = 0; dot < dots; ++dot) {
        clock_lcd_dot(gb);
        sample_window_edge(gb, before, end);
        before = end;
        end += 2u;
    }
}

/* A batched LCD span must expose each DMA edge to tile and attribute reads
 * during transfer, even when DMA reads its source outside VRAM. Single CPU
 * T-cycles already expose those OAM updates. Only VRAM needs half-dot pairing. */
static bool shared_lcd_dma(const CupidGb *gb, bool batched) {
    return (gb->dma_active || gb->dma_start_delay) &&
           ((gb->dma_source_base & 0xe000u) == 0x8000u || (batched && gb->pixels.active)) &&
           cupid_gb_lcd_enabled(gb) && !gb->cpu.halted && !gb->cpu.stopped;
}

static void switch_clock_segment(CupidGb *gb, unsigned cycles, unsigned *half_dot,
                                 uint32_t *rtc_half_cycles, uint32_t *display_half_cycles) {
    clock_cpu_span(gb, cycles);
    if (gb->speed_switch_halt_cycles) {
        if (gb->speed_switch_halt_cycles <= cycles) {
            gb->speed_switch_halt_cycles = 0;
            gb->cpu.halted = false;
        } else {
            gb->speed_switch_halt_cycles -= cycles;
        }
    }
    const unsigned frozen =
        cycles < gb->speed_switch_freeze_cycles ? cycles : gb->speed_switch_freeze_cycles;
    gb->speed_switch_freeze_cycles = (uint8_t)(gb->speed_switch_freeze_cycles - frozen);
    const unsigned ticks = (cycles - frozen) * (gb->double_speed ? 1u : 2u);
    if (cupid_gb_lcd_enabled(gb))
        gb->speed_switch_alignment = (uint8_t)((gb->speed_switch_alignment + ticks) & 7u);
    const unsigned first_dot = 2u - *half_dot;
    const unsigned dots = (*half_dot + ticks) / 2u;
    *half_dot = (*half_dot + ticks) & 1u;
    if (frozen == cycles)
        return;
    sample_rumble_delivery(gb, *display_half_cycles);
    cupid_gb_apu_run(gb, false);
    const unsigned before = *display_half_cycles;
    *rtc_half_cycles += ticks;
    *display_half_cycles += ticks;
    /* A partly frozen segment still releases its complete CPU-side DMA budget;
     * only the LCD oscillator loses the frozen prefix. A completely frozen
     * segment has no video or DMA edge. */
    if (shared_lcd_dma(gb, true)) {
        unsigned credit = cycles - frozen;
        unsigned start = before;
        unsigned end = start + first_dot;
        for (unsigned dot = 0; dot < dots; ++dot) {
            clock_lcd_dot(gb);
            const unsigned part = gb->double_speed && credit >= 2u ? 2u : 1u;
            cupid_gb_tick_dma_tcycles(gb, part);
            credit -= part;
            sample_window_edge(gb, start, end);
            start = end;
            end += 2u;
        }
        /* The oscillator-only prefix remains in the end-of-span DMA flush;
         * it must not reach an earlier resumed fetch ahead of that flush. */
        cupid_gb_tick_dma_tcycles(gb, credit + frozen);
    } else {
        clock_lcd_span(gb, dots, before, first_dot);
        cupid_gb_tick_dma_tcycles(gb, cycles);
    }
    if (*half_dot)
        sample_window_edge(gb, *display_half_cycles - 1u, *display_half_cycles);
}

static void tick_switch_cycle(CupidGb *gb, uint32_t *rtc_half_cycles,
                              uint32_t *display_half_cycles) {
    const bool old_speed = gb->double_speed;
    const unsigned toggle = gb->speed_switch_toggle_cycles;
    if (old_speed != (toggle != 0 && toggle <= 4))
        gb->speed_phase = !gb->speed_phase;

    unsigned half_dot = 0;
    if (toggle && toggle <= 4) {
        gb->speed_switch_toggle_cycles = 0;
        if (toggle < 4) {
            switch_clock_segment(gb, toggle, &half_dot, rtc_half_cycles, display_half_cycles);
            finish_display_span(gb, *display_half_cycles);
            *display_half_cycles = 0;
            cupid_gb_rtc_tick(gb, *rtc_half_cycles);
            *rtc_half_cycles = 0;
        }
        gb->double_speed = !old_speed;
        switch_clock_segment(gb, toggle < 4 ? 4u - toggle : 4u, &half_dot, rtc_half_cycles,
                             display_half_cycles);
        gb->io_registers[0x4d] =
            (uint8_t)((gb->io_registers[0x4d] & 1u) | (gb->double_speed ? 0x80u : 0));
    } else {
        if (toggle)
            gb->speed_switch_toggle_cycles = (uint8_t)(toggle - 4);
        switch_clock_segment(gb, 4, &half_dot, rtc_half_cycles, display_half_cycles);
    }
}

static void tick_mcycles(CupidGb *gb, uint16_t cycles, bool cpu_delivery) {
    if (!gb || !cycles) {
        return;
    }

    const unsigned toggle = gb->speed_switch_toggle_cycles;
    const unsigned duration = (unsigned)cycles * 4u;
    if (toggle && !(toggle & 3u) && toggle <= duration) {
        /* Equality belongs to this complete CPU span. An internal M-cycle
         * boundary must not turn an old-speed prefix into the equality case. */
        gb->speed_switch_toggle_cycles = 0;
        if (toggle < duration)
            tick_mcycles(gb, (uint16_t)(toggle / 4u), cpu_delivery);
        gb->double_speed = !gb->double_speed;
        gb->io_registers[0x4d] =
            (uint8_t)((gb->io_registers[0x4d] & 1u) | (gb->double_speed ? 0x80u : 0));
        if (toggle < duration) {
            tick_mcycles(gb, (uint16_t)((duration - toggle) / 4u), cpu_delivery);
            return;
        }
    }

    /* The caller owns delivery boundaries. A host bulk wait retains its
     * M-cycle cadence independently of whether it is short or long. */
    const bool bulk = !cpu_delivery;
    uint32_t half_cycles = 0;
    uint32_t display_half_cycles = 0;
    while (cycles-- != 0u) {
        if (gb->speed_switch_halt_cycles || gb->speed_switch_toggle_cycles ||
            gb->speed_switch_freeze_cycles || gb->stop_divider_hold_cycles) {
            tick_switch_cycle(gb, &half_cycles, &display_half_cycles);
            if (bulk) {
                finish_display_span(gb, display_half_cycles);
                display_half_cycles = 0;
            }
            continue;
        }
        clock_cpu_span(gb, 4u);
        cupid_gb_apu_run(gb, false);

        /* Each CPU machine cycle occupies two LCD dots in double speed.
         * Batching pairs of cycles would move reads and interrupts by two dots. */
        if (gb->double_speed) {
            gb->speed_phase = !gb->speed_phase;
            if (cupid_gb_lcd_enabled(gb))
                gb->speed_switch_alignment ^= 4u;
        }
        const uint16_t dots = gb->double_speed ? 2u : 4u;
        sample_rumble_delivery(gb, display_half_cycles);
        const unsigned before = display_half_cycles;
        half_cycles += (uint32_t)dots * 2u;
        display_half_cycles += (uint32_t)dots * 2u;
        const bool shared = shared_lcd_dma(gb, true);
        if (shared) {
            for (unsigned dot = 0; dot < dots; ++dot) {
                clock_lcd_dot(gb);
                cupid_gb_tick_dma_tcycles(gb, gb->double_speed ? 2u : 1u);
                sample_window_edge(gb, before + dot * 2u, before + (dot + 1u) * 2u);
            }
        } else {
            clock_lcd_span(gb, dots, before, 2u);
        }
        if (!shared)
            cupid_gb_tick_dma(gb);
        if (bulk) {
            finish_display_span(gb, display_half_cycles);
            display_half_cycles = 0;
        }
    }
    finish_display_span(gb, display_half_cycles);
    cupid_gb_rtc_tick(gb, half_cycles);
}

void cupid_gb_tick(CupidGb *gb, uint16_t cycles) {
    tick_mcycles(gb, cycles, false);
}

void cupid_gb_deliver_cpu_cycles(CupidGb *gb, uint16_t cycles) {
    tick_mcycles(gb, cycles, true);
}

uint8_t cupid_gb_tick_halt(CupidGb *gb) {
    if (!gb) {
        return 0;
    }
    const uint8_t before = (uint8_t)(gb->interrupt_enable & gb->interrupt_flags & 0x1fu);
    const bool first_cycle = gb->cpu.just_halted;
    gb->cpu.just_halted = false;
    if (cupid_gb_model_is_cgb(gb->model) || first_cycle) {
        /* Color hardware and the first cycle after HALT poll before the cycle. */
        cupid_gb_deliver_cpu_cycles(gb, 1u);
        return before;
    }

    uint8_t middle = before;
    const uint16_t dots = gb->double_speed ? 1u : 2u;
    if (gb->double_speed)
        gb->speed_phase = !gb->speed_phase;
    for (unsigned half = 0; half < 2; ++half) {
        /* Monochrome HALT polls between two complete two-T-cycle deliveries.
         * GBS can double this CPU clock: DIV and DMA still receive two T-cycles,
         * while display and cartridge clocks receive only one base dot. */
        clock_cpu_span(gb, 2u);
        cupid_gb_apu_run(gb, false);
        if (gb->double_speed && cupid_gb_lcd_enabled(gb))
            gb->speed_switch_alignment = (uint8_t)((gb->speed_switch_alignment + 2u) & 7u);
        sample_rumble_delivery(gb, 0);
        clock_lcd_span(gb, dots, 0, 2u);
        cupid_gb_tick_dma_tcycles(gb, 2u);
        finish_display_span(gb, (unsigned)dots * 2u);
        cupid_gb_rtc_tick(gb, (uint32_t)dots * 2u);
        if (!half)
            middle = (uint8_t)(gb->interrupt_enable & gb->interrupt_flags & 0x1fu);
    }
    return middle;
}

void cupid_gb_acknowledge_interrupt(CupidGb *gb, uint8_t mask) {
    if (!gb)
        return;

    /* Acknowledge halfway through the final entry cycle, at CPU T18. Each
     * prefix finishes its timer and display events before the request clears. */
    const unsigned half = gb->double_speed ? 1u : 2u;
    if (gb->double_speed && cupid_gb_lcd_enabled(gb))
        gb->speed_switch_alignment ^= 4u;
    const bool shared = shared_lcd_dma(gb, true);
    clock_cpu_span(gb, 2u);
    cupid_gb_apu_run(gb, false);
    sample_rumble_delivery(gb, 0);
    for (unsigned dot = 0; dot < half; ++dot) {
        clock_lcd_dot(gb);
        if (shared)
            cupid_gb_tick_dma_tcycles(gb, gb->double_speed ? 2u : 1u);
        sample_window_edge(gb, dot * 2u, (dot + 1u) * 2u);
    }
    if (!shared)
        cupid_gb_tick_dma_tcycles(gb, 2u);
    finish_display_span(gb, half * 2u);
    cupid_gb_rtc_tick(gb, (uint32_t)half * 2u);
    gb->interrupt_flags &= (uint8_t)~mask;
    clock_cpu_span(gb, 2u);
    cupid_gb_apu_run(gb, false);
    sample_rumble_delivery(gb, 0);
    if (gb->double_speed)
        gb->speed_phase = !gb->speed_phase;
    for (unsigned dot = 0; dot < half; ++dot) {
        clock_lcd_dot(gb);
        if (shared)
            cupid_gb_tick_dma_tcycles(gb, gb->double_speed ? 2u : 1u);
        sample_window_edge(gb, dot * 2u, (dot + 1u) * 2u);
    }
    if (!shared)
        cupid_gb_tick_dma_tcycles(gb, 2u);
    finish_display_span(gb, half * 2u);
    cupid_gb_rtc_tick(gb, (uint32_t)half * 2u);
}

static uint8_t stat_transient(const CupidGb *gb, uint8_t value, uint8_t previous,
                              bool double_speed) {
    if (!cupid_gb_model_is_cgb(gb->model)) {
        bool hblank_only = (previous & 8u) && !(previous & 32u);
        /* On the LY boundary, an existing HBlank source masks the transient
         * OAM enable. Other interrupt enables still receive their pulse. */
        return hblank_only && cupid_gb_stat_boundary(gb) ? 0xdf : 0xff;
    }
    uint8_t held = double_speed ? 8u : 0x40u;
    return (uint8_t)((value & (uint8_t)~held) | (previous & held));
}

/* Subcycle work is confined to one instruction. In particular, a late STAT
 * commit consumes the next cycle's first T-cycle, not an additional cycle. */
static void bus_clock_dot(CupidGb *gb, unsigned phase) {
    if (cupid_gb_lcd_enabled(gb))
        gb->speed_switch_alignment =
            (uint8_t)((gb->speed_switch_alignment + (gb->double_speed ? 1u : 2u)) & 7u);
    if (!gb->double_speed || !(phase & 1u))
        clock_lcd_dot(gb);
    else
        cupid_gb_sync_stat_half(gb);
    /* Pixel work consumes whole dots. Retain the first half's source-bus
     * credit until that dot's fetch, then release the pair after the fetch. */
    if (gb->double_speed && shared_lcd_dma(gb, false)) {
        if (!(phase & 1u))
            cupid_gb_tick_dma_tcycles(gb, 2u);
    } else {
        cupid_gb_tick_dma_tcycles(gb, 1u);
    }
}

static unsigned bus_clock_span(CupidGb *gb, unsigned phase, unsigned cycles) {
    gb->apu.pcm_read_inhibit = 0;
    uint32_t half_cycles = 0;
    const unsigned toggle = gb->speed_switch_toggle_cycles;
    const unsigned old_cycles = toggle && toggle < cycles ? toggle : 0;
    const bool switching = gb->speed_switch_halt_cycles || toggle ||
                           gb->speed_switch_freeze_cycles || gb->stop_divider_hold_cycles;
    /* A prepared switch can outlive STOP when an enabled request suppresses
     * its wait. Preserve the old/new speed portions of each late-write span;
     * the borrowed T-cycle must count toward that pending switch too. */
    if (toggle && toggle <= cycles) {
        gb->speed_switch_toggle_cycles = 0;
        if (!old_cycles)
            gb->double_speed = !gb->double_speed;
    } else if (toggle) {
        gb->speed_switch_toggle_cycles = (uint8_t)(toggle - cycles);
    }
    clock_camera_phi(gb, old_cycles ? old_cycles : cycles);
    if (gb->cpu.stopped)
        cupid_gb_tick_apu_divider(gb, gb->double_speed ? 2u : 4u);
    for (unsigned tick = 0; tick < cycles; ++tick) {
        if (old_cycles && tick == old_cycles) {
            finish_display_span(gb, half_cycles);
            cupid_gb_rtc_tick(gb, half_cycles);
            half_cycles = 0;
            gb->double_speed = !gb->double_speed;
            clock_camera_phi(gb, cycles - old_cycles);
            if (gb->cpu.stopped)
                cupid_gb_tick_apu_divider(gb, gb->double_speed ? 2u : 4u);
        }
        const unsigned before = half_cycles;
        phase = (phase + 1u) & 3u;
        const bool divider_edge = consume_divider_cycles(gb, 1u) != 0;
        if (!switching) {
            sample_rumble_delivery(gb, half_cycles);
            bus_clock_dot(gb, phase);
            half_cycles += gb->double_speed ? 1u : 2u;
        } else {
            if (gb->speed_switch_halt_cycles && --gb->speed_switch_halt_cycles == 0)
                gb->cpu.halted = false;
            const bool frozen = gb->speed_switch_freeze_cycles != 0;
            if (frozen)
                --gb->speed_switch_freeze_cycles;
            const bool dot = !gb->double_speed || !(phase & 1u);
            if (!frozen) {
                sample_rumble_delivery(gb, half_cycles);
                half_cycles += gb->double_speed ? 1u : 2u;
                if (cupid_gb_lcd_enabled(gb))
                    gb->speed_switch_alignment =
                        (uint8_t)((gb->speed_switch_alignment + (gb->double_speed ? 1u : 2u)) & 7u);
                if (dot) {
                    advance_base_time(gb, 1u);
                    cupid_gb_tick_ppu_dots(gb, 1u);
                } else {
                    cupid_gb_sync_stat_half(gb);
                }
            }
            if (!frozen)
                cupid_gb_tick_dma_tcycles(gb, 1u);
        }
        if (half_cycles != before)
            sample_window_edge(gb, before, half_cycles);
        if (phase == 2) {
            /* Retain the local machine-cycle marker independently of the
             * divider period, which may have started at a different T-cycle. */
            const bool phase_speed =
                gb->double_speed != (old_cycles > tick && old_cycles - tick <= 3u);
            if (phase_speed)
                gb->speed_phase = !gb->speed_phase;
        }
        if (divider_edge) {
            clock_cpu_domain(gb);
            if (!switching || !gb->speed_switch_freeze_cycles)
                cupid_gb_apu_run(gb, false);
        }
    }
    if (switching)
        gb->io_registers[0x4d] =
            (uint8_t)((gb->io_registers[0x4d] & 1u) | (gb->double_speed ? 0x80u : 0));
    finish_display_span(gb, half_cycles);
    cupid_gb_rtc_tick(gb, half_cycles);
    return phase;
}

unsigned cupid_gb_tick_color_dma_slot(CupidGb *gb, unsigned cycles, unsigned phase) {
    if (!gb || !cycles)
        return phase;
    return bus_clock_span(gb, phase, cycles);
}

void cupid_gb_cpu_finish(CupidGb *gb, uint16_t mcycles, CupidGbCpuBus *bus) {
    if (!bus->credit) {
        tick_mcycles(gb, mcycles, true);
        return;
    }
    const unsigned phase = (unsigned)((bus->credit + 4) & 3);
    const unsigned remaining = (unsigned)((int)mcycles * 4 - bus->credit);
    (void)bus_clock_span(gb, phase, remaining);
    bus->credit = 0;
}

typedef enum CpuWriteKind {
    CPU_WRITE_ORDINARY,
    CPU_WRITE_STAT,
    CPU_WRITE_LCDC,
    CPU_WRITE_LCDC_DOUBLE,
    CPU_WRITE_PALETTE,
    CPU_WRITE_SWEEP
} CpuWriteKind;

typedef struct CpuWrite {
    uint16_t address;
    uint8_t value, previous;
    unsigned early, commit, sample;
    CpuWriteKind kind;
    bool double_speed;
} CpuWrite;

static CpuWrite cpu_write_plan(CupidGb *gb, uint16_t address, uint8_t value, unsigned boundary,
                               bool first) {
    CpuWrite write = {address,         value, 0, 0, boundary, boundary, CPU_WRITE_ORDINARY,
                      gb->double_speed};
    if (address == 0xff41u) {
        write.previous = gb->io_registers[CUPID_GB_IO_STAT];
        write.kind = CPU_WRITE_STAT;
        write.early = boundary;
        write.commit = write.sample = boundary + 1;
    } else if (address == 0xff40u) {
        /* Monochrome LCDC pulses sample the old value before their leading clocks.
         * The bus read also resolves retained display work; a raw register read
         * would shift the receiver's first threshold crossing during this write. */
        write.previous = cupid_gb_model_is_cgb(gb->model) ? gb->io_registers[CUPID_GB_IO_LCDC]
                                                          : cupid_gb_read_bus_u8(gb, address);
        if (!cupid_gb_model_is_cgb(gb->model)) {
            write.kind = CPU_WRITE_LCDC;
            write.early = boundary - 2;
            write.commit = write.sample = boundary - 1;
        } else if (gb->double_speed) {
            write.kind = CPU_WRITE_LCDC_DOUBLE;
            write.early = boundary - 2;
        } else if ((write.previous & (uint8_t)~value & 0x10u) != 0u) {
            /* Tile selection has a trailing one-T-cycle conflict window. */
            write.sample = boundary + 1;
        }
    } else if (address == 0xff0fu && first) {
        /* A first stack write to IF wins over that cycle's hardware request.
         * The second stack write to IF has its separate old-value sample. */
        write.commit = write.sample = boundary + 1;
    } else if (address >= 0xff47u && address <= 0xff49u) {
        if (cupid_gb_model_is_cgb(gb->model)) {
            if (!gb->double_speed)
                write.commit = write.sample = boundary - 2;
        } else if (cupid_gb_model_is_sgb(gb->model)) {
            write.commit = write.sample = boundary - 1;
        } else {
            write.kind = CPU_WRITE_PALETTE;
            write.early = boundary - 2;
            write.commit = write.sample = boundary - 1;
        }
    } else if (address == 0xff4bu) {
        if (!cupid_gb_model_is_cgb(gb->model))
            write.sample = boundary + 1;
        else if (!gb->double_speed)
            write.commit = write.sample = boundary + 1;
    } else if (address == 0xff10u && cupid_gb_model_is_cgb(gb->model) && gb->double_speed) {
        /* The sweep register has a separate final T-cycle even on revisions
         * without the older sweep-disable conflict. */
        write.kind = CPU_WRITE_SWEEP;
        write.early = boundary - 1;
    } else if (address == 0xff42u && !cupid_gb_model_is_cgb(gb->model)) {
        write.commit = write.sample = boundary - 1;
    } else if (address == 0xff43u && (!cupid_gb_model_is_cgb(gb->model) || gb->double_speed)) {
        write.commit = write.sample = boundary - 2;
    } else if (address == 0xff45u && cupid_gb_model_is_cgb(gb->model) && !gb->double_speed) {
        write.commit = write.sample = boundary + 1;
    }
    return write;
}

static void cpu_write_early(CupidGb *gb, const CpuWrite *write) {
    uint8_t value;
    if (write->kind == CPU_WRITE_STAT) {
        value = stat_transient(gb, write->value, write->previous, write->double_speed);
        cupid_gb_write_u8(gb, write->address, value);
    } else if (write->kind == CPU_WRITE_LCDC_DOUBLE) {
        value = (uint8_t)((write->value & (uint8_t)~0x81u) | (write->previous & 0x81u));
        cupid_gb_write_u8(gb, write->address, value);
    } else if (write->kind == CPU_WRITE_PALETTE) {
        value = (uint8_t)(cupid_gb_read_u8(gb, write->address) | write->value);
        cupid_gb_write_u8(gb, write->address, value);
    } else if (write->kind == CPU_WRITE_SWEEP) {
        return;
    } else if (cupid_gb_model_is_sgb(gb->model)) {
        /* Object fetching observes the pulse even though the old register value
         * is restored before the next LCD dot. */
        cupid_gb_write_u8(gb, write->address, write->value);
        cupid_gb_write_u8(gb, write->address, write->previous);
    } else {
        cupid_gb_write_u8(gb, write->address, (uint8_t)(write->previous | (write->value & 1u)));
    }
}

static uint8_t run_cpu_write_plan(CupidGb *gb, const CpuWrite *write, unsigned boundary,
                                  CupidGbCpuBus *bus, bool interrupt) {
    unsigned phase = (unsigned)((bus->credit + 4) & 3);
    unsigned elapsed = 0;
    uint8_t previous_if = 0;
    const unsigned events[3] = {write->early, write->commit, write->sample};
    for (unsigned index = 0; index < 3; ++index) {
        const unsigned event = events[index];
        if (!event)
            continue;
        const unsigned duration = event - elapsed;
        if (duration) {
            if (!interrupt && !phase && !(duration & 3u))
                cupid_gb_deliver_cpu_cycles(gb, (uint16_t)(duration / 4u));
            else
                phase = bus_clock_span(gb, phase, duration);
        }
        elapsed = event;
        if (!index)
            cpu_write_early(gb, write);
        else if (index == 1) {
            previous_if = gb->interrupt_flags;
            cupid_gb_write_u8_phase(gb, write->address, write->value,
                                    gb->double_speed && (phase & 1u));
        }
    }
    bus->credit = (int8_t)((int)write->sample - (int)boundary);
    return previous_if;
}

void cupid_gb_interrupt_tail(CupidGb *gb, uint16_t return_address) {
    CupidGbCpuBus bus = {0};
    const CpuWrite high = cpu_write_plan(gb, --gb->cpu.sp, (uint8_t)(return_address >> 8), 8, true);
    (void)run_cpu_write_plan(gb, &high, 8, &bus, true);
    const uint8_t enabled = gb->interrupt_enable;

    /* The low-byte conflict is selected after the high write, including any
     * speed change or register side effect during that complete delivery. */
    const unsigned boundary = (unsigned)(4 - bus.credit);
    const CpuWrite low = cpu_write_plan(gb, --gb->cpu.sp, (uint8_t)return_address, boundary, false);
    const uint8_t previous_if = run_cpu_write_plan(gb, &low, boundary, &bus, true);
    const uint8_t requested = low.address == 0xff0fu ? previous_if : gb->interrupt_flags;
    const uint8_t pending = (uint8_t)(enabled & requested & 0x1fu);
    const uint8_t selected = (uint8_t)(pending & (uint8_t)(0u - pending));
    unsigned phase = (unsigned)((bus.credit + 4) & 3);
    if (selected) {
        phase = bus_clock_span(gb, phase, (unsigned)(2 - bus.credit));
        /* Finish the T18 prefix before acknowledging. With two divider clocks
         * already carried into entry, a reload lands exactly on this boundary
         * and is cleared; a later reload in the final two clocks survives. */
        gb->interrupt_flags &= (uint8_t)~selected;
        for (unsigned bit = 0; bit < 5; ++bit) {
            if (selected == (1u << bit))
                gb->cpu.pc = (uint16_t)(0x40u + 8u * bit);
        }
        (void)bus_clock_span(gb, phase, 2);
    } else {
        gb->cpu.pc = 0;
        (void)bus_clock_span(gb, phase, (unsigned)(4 - bus.credit));
    }
}

void cupid_gb_cpu_write(CupidGb *gb, uint16_t address, uint8_t value, uint16_t leading_mcycles,
                        CupidGbCpuBus *bus) {
    if (!leading_mcycles && !bus->credit) {
        cupid_gb_write_u8(gb, address, value);
        return;
    }

    /* Select the control conflict before advancing: a pending speed toggle
     * changes the clock rate, not the register's already selected access path. */
    const unsigned boundary = (unsigned)((int)leading_mcycles * 4 - bus->credit);
    const CpuWrite write = cpu_write_plan(gb, address, value, boundary, true);
    (void)run_cpu_write_plan(gb, &write, boundary, bus, false);
}

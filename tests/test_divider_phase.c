/**
 * @file    test_divider_phase.c
 * @brief   Regression tests for divider phase across partial clocks, STOP, and DMA.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "cupid/gb/apu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/timer.h"
#include "cupid/gbc/cgb.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum { ROM_SIZE = 0x8000 };

static int64_t fixed_time(void *context) {
    (void)context;
    return INT64_C(1700000040);
}

static void checksum(uint8_t *rom) {
    rom[0x14d] = 0;
    for (unsigned offset = 0x134; offset < 0x14d; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
}

static void make_rom(uint8_t *rom, bool color, uint8_t type) {
    memset(rom, 0, ROM_SIZE);
    memcpy(rom + 0x134, "DIVIDER TEST", 12);
    rom[0x143] = color ? 0x80 : 0;
    rom[0x147] = type;
    rom[0x149] = type == 0xfc ? 4 : type == 0x10 ? 2 : 0;
    checksum(rom);
}

static CupidGb *load_machine(CupidGbModel model, const uint8_t *rom) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    cupid_gb_set_rtc_time_source(gb, fixed_time, NULL);
    assert(cupid_gb_load_rom(gb, rom, ROM_SIZE));
    return gb;
}

static CupidGb *machine(CupidGbModel model, bool fast, uint8_t type) {
    uint8_t rom[ROM_SIZE];
    make_rom(rom, model == CUPID_GB_MODEL_CGB, type);
    CupidGb *gb = load_machine(model, rom);
    gb->double_speed = fast;
    gb->cpu.ime = false;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff02, 0);
    cupid_gb_write_u8(gb, 0xff07, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static unsigned divider(const CupidGb *gb) {
    /* Include DIV's hidden low bits; the mapped byte alone misses early clocks. */
    return (unsigned)gb->io_registers[4] * 256u + (unsigned)gb->div_counter * 4u;
}

static void test_partial_spans(void) {
    static const unsigned chunks[] = {1, 2, 1, 3, 5, 2, 2, 7, 1};
    for (unsigned profile = 0; profile < 3; ++profile) {
        const CupidGbModel model = profile ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
        CupidGb *gb = machine(model, profile == 2, 0);
        unsigned phase = 0, elapsed = 0;
        for (unsigned index = 0; index < sizeof(chunks) / sizeof(*chunks); ++index) {
            phase = cupid_gb_tick_color_dma_slot(gb, chunks[index], phase);
            elapsed += chunks[index];
            assert(divider(gb) == (elapsed & ~3u));
        }
        release(gb);
    }
}

static void test_div_reset_mid_span(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned previous = 1; previous <= 3; ++previous) {
            CupidGb *gb = machine(CUPID_GB_MODEL_CGB, fast != 0, 0);
            unsigned phase = cupid_gb_tick_color_dma_slot(gb, previous, 0);
            cupid_gb_write_u8(gb, 0xff04, 0xff);
            /* Reset restarts the divider without moving the display/bus timeline. */
            for (unsigned tick = 1; tick <= 8; ++tick) {
                phase = cupid_gb_tick_color_dma_slot(gb, 1, phase);
                assert(divider(gb) == (tick & ~3u));
            }
            release(gb);
        }
    }
}

static void test_stop_hold(void) {
    uint8_t rom[ROM_SIZE];
    make_rom(rom, true, 0);
    rom[0x100] = 0x10;
    CupidGb *gb = load_machine(CUPID_GB_MODEL_CGB, rom);
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff07, 0);
    cupid_gb_write_u8(gb, 0xff02, 0);
    cupid_gb_write_u8(gb, 0xff4d, 1);
    gb->cpu.ime = false;
    gb->interrupt_enable = gb->interrupt_flags = 1;
    assert(cupid_gb_step(gb));
    assert(!gb->cpu.halted && !gb->cpu.stopped && gb->cpu.pc == 0x101);
    assert(divider(gb) == 0 && gb->stop_divider_hold_cycles == 4);
    unsigned phase = 0;
    for (unsigned tick = 1; tick <= 12; ++tick) {
        phase = cupid_gb_tick_color_dma_slot(gb, 1, phase);
        /* IME-off STOP holds reset for four T-cycles before a fresh divider period. */
        const unsigned unheld = tick > 4 ? tick - 4 : 0;
        assert(divider(gb) == (unheld & ~3u));
    }
    assert(gb->double_speed && !gb->speed_switch_toggle_cycles && !gb->speed_switch_freeze_cycles);
    release(gb);
}

static void test_freeze_and_audio_clocks(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        CupidGb *gb = machine(CUPID_GB_MODEL_CGB, fast != 0, 0);
        assert(cupid_gb_apu_set_sample_rate(gb, 32000));
        const uint32_t initial_audio = gb->apu.output.sample_acc;
        const uint64_t initial_time = gb->elapsed_cycles;
        gb->speed_switch_freeze_cycles = 8;
        unsigned phase = cupid_gb_tick_color_dma_slot(gb, 8, 0);
        assert(divider(gb) == 8 && gb->elapsed_cycles == initial_time);
        assert(gb->apu.output.sample_acc == initial_audio + (fast ? 4u : 8u) * 32000u);
        assert(gb->speed_switch_freeze_cycles == 0);

        cupid_gb_write_u8(gb, 0xff04, 0);
        gb->stop_divider_hold_cycles = 4;
        const uint32_t held_audio = gb->apu.output.sample_acc;
        phase = cupid_gb_tick_color_dma_slot(gb, 4, phase);
        assert(divider(gb) == 0 && gb->apu.output.sample_acc == held_audio);
        (void)cupid_gb_tick_color_dma_slot(gb, 4, phase);
        assert(divider(gb) == 4);
        assert(gb->apu.output.sample_acc == held_audio + (fast ? 2u : 4u) * 32000u);
        release(gb);
    }
}

static void test_delayed_envelope(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_CGB, true, 0);
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff12, 0x91);
    cupid_gb_write_u8(gb, 0xff14, 0x80);
    gb->apu.pulse_timing[0].envelope.clock = true;
    gb->apu.pulse_timing[0].envelope.locked = false;
    gb->apu.pulse_timing[0].envelope.lock_pending = false;
    gb->io_registers[4] = 0x3f;
    gb->div_counter = 63;
    assert(gb->apu.ch1_vol == 9);
    unsigned phase = cupid_gb_tick_color_dma_slot(gb, 4, 0);
    assert(divider(gb) == 0x4000);
    assert(gb->apu.envelope_delay && gb->apu.ch1_vol == 9);
    /* Double-speed color hardware latches this envelope edge for the next
     * divider event. The current event's APU credit cannot consume its delay. */
    phase = cupid_gb_tick_color_dma_slot(gb, 3, phase);
    assert(gb->apu.envelope_delay && gb->apu.ch1_vol == 9);
    (void)cupid_gb_tick_color_dma_slot(gb, 1, phase);
    assert(divider(gb) == 0x4004);
    assert(!gb->apu.envelope_delay && gb->apu.ch1_vol == 8);
    release(gb);
}

static void test_camera_phi(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        CupidGb *gb = machine(CUPID_GB_MODEL_CGB, fast != 0, 0xfc);
        cupid_gb_write_u8(gb, 0x4000, 0x10);
        cupid_gb_write_u8(gb, 0xa000, 1);
        const uint32_t busy = gb->camera.busy_tcycles;
        const unsigned camera_phase = gb->camera.clock_phase;
        assert(busy > 16);
        gb->stop_divider_hold_cycles = 4;
        gb->speed_switch_freeze_cycles = 4;
        unsigned phase = 0;
        for (unsigned tick = 1; tick <= 4; ++tick) {
            phase = cupid_gb_tick_color_dma_slot(gb, 1, phase);
            assert(gb->camera.busy_tcycles == busy - tick);
            assert(gb->camera.clock_phase == ((camera_phase + tick) & 7u));
            assert(divider(gb) == 0);
        }
        /* Camera PHI is independent of DIV's reset signal and oscillator freeze. */
        cupid_gb_write_u8(gb, 0xff04, 0);
        assert(gb->camera.busy_tcycles == busy - 4);
        assert(gb->camera.clock_phase == ((camera_phase + 4u) & 7u));
        gb->cpu.halted = true;
        phase = cupid_gb_tick_color_dma_slot(gb, 4, phase);
        assert(gb->camera.busy_tcycles == busy - 4 && divider(gb) == 4);
        gb->cpu.halted = false;
        gb->cpu.stopped = true;
        (void)cupid_gb_tick_color_dma_slot(gb, 4, phase);
        assert(gb->camera.busy_tcycles == busy - 4 && divider(gb) == 4);
        release(gb);
    }
}

static void test_dma_remainder(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_CGB, false, 0);
    for (unsigned byte = 0; byte < 16; ++byte)
        cupid_gb_write_u8(gb, (uint16_t)(0xc000u + byte), 0xa5);
    cupid_gb_write_u8(gb, 0xff51, 0xc0);
    cupid_gb_write_u8(gb, 0xff52, 0);
    cupid_gb_write_u8(gb, 0xff53, 0);
    cupid_gb_write_u8(gb, 0xff54, 0);
    cupid_gb_write_u8(gb, 0xff55, 0);
    gb->stop_divider_hold_cycles = 4;
    gb->speed_switch_toggle_cycles = 6;
    gb->speed_switch_freeze_cycles = 1;
    cupid_cgb_run_pending_dma(gb);
    /* Seventeen captured two-T-cycle slots leave two unheld T-cycles after the
     * last divider edge. The switched transfer has no normal-speed tail. */
    assert(gb->double_speed && !gb->hdma_pending && !gb->hdma_running);
    assert(divider(gb) == 28);
    for (unsigned byte = 0; byte < 16; ++byte)
        assert(gb->video_ram[byte] == 0xa5);
    assert(cupid_gb_step(gb));
    assert(divider(gb) == 32);
    unsigned phase = cupid_gb_tick_color_dma_slot(gb, 1, 0);
    assert(divider(gb) == 32);
    (void)cupid_gb_tick_color_dma_slot(gb, 1, phase);
    assert(divider(gb) == 36);
    release(gb);
}

static void test_interrupt_acknowledgment(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned phase = 0; phase < 4; ++phase) {
            CupidGb *gb = machine(CUPID_GB_MODEL_CGB, fast != 0, 0);
            (void)cupid_gb_tick_color_dma_slot(gb, phase, 0);
            cupid_gb_write_u8(gb, 0xff05, 0xff);
            cupid_gb_write_u8(gb, 0xff06, 0x73);
            cupid_gb_write_u8(gb, 0xff07, 5);
            gb->cpu.pc = 0x150;
            gb->cpu.sp = 0xdff0;
            gb->cpu.ime = true;
            gb->interrupt_enable = gb->interrupt_flags = 4;
            CupidGbStepTrace trace = {0};
            assert(cupid_gb_step_observed(gb, &trace));
            assert(trace.event == CUPID_GB_STEP_INTERRUPT);
            assert(gb->cpu.pc == 0x50 && gb->cpu.sp == 0xdfee);
            assert(divider(gb) == 20 && gb->io_registers[5] == 0x73);
            /* Reloads at T20/T19 survive the T18 acknowledgment. Reloads at
             * T18/T17 are already pending when that request is cleared. */
            assert(gb->interrupt_flags == (phase < 2 ? 4 : 0));
            release(gb);
        }
    }
}

static void test_halt_poll_phase(void) {
    static const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB,
                                          CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2,
                                          CUPID_GB_MODEL_CGB,     CUPID_GB_MODEL_CGB};
    for (unsigned profile = 0; profile < sizeof(models) / sizeof(*models); ++profile) {
        for (unsigned first = 0; first < 2; ++first) {
            for (unsigned phase = 0; phase < 4; ++phase) {
                for (unsigned ime = 0; ime < 2; ++ime) {
                    const bool fast = profile == 5;
                    CupidGb *gb = machine(models[profile], fast, 0);
                    cupid_gb_write_u8(gb, 0xff26, 0);
                    unsigned bus_phase = cupid_gb_tick_color_dma_slot(gb, phase, 0);
                    cupid_gb_write_u8(gb, 0xff05, 0xff);
                    cupid_gb_write_u8(gb, 0xff06, 0x73);
                    cupid_gb_write_u8(gb, 0xff07, 5);
                    (void)cupid_gb_tick_color_dma_slot(gb, 16, bus_phase);
                    assert(divider(gb) == 16 && cupid_gb_read_u8(gb, 0xff05) == 0);
                    cupid_gb_write_u8(gb, 0xff0f, 0);
                    gb->interrupt_enable = 4;
                    gb->cpu.pc = 0x150;
                    gb->cpu.sp = 0xdff0;
                    gb->cpu.halted = true;
                    gb->cpu.just_halted = first != 0;
                    gb->cpu.ime = ime != 0;
                    const uint64_t initial_time = gb->elapsed_cycles;

                    /* Steady monochrome HALT polls after two T-cycles. A pending
                     * reload at phases two or three reaches that poll; color
                     * hardware and the first HALT interval poll before it. */
                    const bool midpoint = profile < 4 && !first && phase >= 2;
                    const bool serviced = midpoint && ime;
                    assert(cupid_gb_step(gb));
                    assert(gb->cpu.halted == !midpoint && !gb->cpu.just_halted);
                    assert(gb->cpu.pc == (serviced ? 0x50 : 0x150));
                    assert(gb->cpu.sp == (serviced ? 0xdfee : 0xdff0));
                    assert(gb->cpu.ime == (ime && !midpoint));
                    assert(gb->interrupt_flags == (serviced ? 0 : 4));
                    assert(divider(gb) == (serviced ? 40u : 20u));
                    assert(cupid_gb_read_u8(gb, 0xff05) == (serviced ? 0x74 : 0x73));

                    /* A late request wakes on the next interval. An early one
                     * has already woken, so its next step executes a NOP. */
                    assert(cupid_gb_step(gb));
                    assert(!gb->cpu.halted && !gb->cpu.just_halted && !gb->cpu.ime);
                    assert(gb->cpu.pc == (ime ? 0x50u : 0x150u) + (midpoint ? 1u : 0u));
                    assert(gb->cpu.sp == (ime ? 0xdfee : 0xdff0));
                    assert(gb->interrupt_flags == (ime ? 0 : 4));
                    assert(divider(gb) == (ime ? 44u : 24u));
                    assert(cupid_gb_read_u8(gb, 0xff05) == (ime ? 0x74 : 0x73));
                    assert(gb->elapsed_cycles - initial_time ==
                           (ime ? 28u : 8u) / (fast ? 2u : 1u));
                    release(gb);
                }
            }
        }
    }
}

static void emit(uint8_t *rom, size_t *at, uint8_t byte) {
    rom[(*at)++] = byte;
}

static void load_a(uint8_t *rom, size_t *at, uint8_t value) {
    emit(rom, at, 0x3e);
    emit(rom, at, value);
}

static void store_io(uint8_t *rom, size_t *at, uint8_t port) {
    emit(rom, at, 0xe0);
    emit(rom, at, port);
}

static uint16_t make_guest_program(uint8_t *rom) {
    make_rom(rom, true, 0x10);
    memcpy(rom + 0x134, "PHASE REACH", 11);
    rom[0x13f] = 0;
    rom[0x100] = 0xc3;
    rom[0x101] = 0x50;
    rom[0x102] = 1;
    size_t at = 0x150;
    static const uint8_t setup[] = {0xf3, 0x31, 0xfe, 0xff, 0xaf, 0xe0, 0x40, 0x21, 0x00,
                                    0xc0, 0x06, 0x10, 0x3e, 0xa5, 0x22, 0x05, 0x20, 0xfc};
    memcpy(rom + at, setup, sizeof(setup));
    at += sizeof(setup);
    load_a(rom, &at, 0x80);
    store_io(rom, &at, 0x26);
    load_a(rom, &at, 0x77);
    store_io(rom, &at, 0x24);
    load_a(rom, &at, 0x11);
    store_io(rom, &at, 0x25);
    load_a(rom, &at, 0xf0);
    store_io(rom, &at, 0x12);
    store_io(rom, &at, 0x13);
    load_a(rom, &at, 0x87);
    store_io(rom, &at, 0x14);
    load_a(rom, &at, 0x3c);
    store_io(rom, &at, 0x06);
    store_io(rom, &at, 0x01);
    load_a(rom, &at, 0xf0);
    store_io(rom, &at, 0x05);
    load_a(rom, &at, 5);
    store_io(rom, &at, 0x07);
    load_a(rom, &at, 1);
    emit(rom, &at, 0xea);
    emit(rom, &at, 0xff);
    emit(rom, &at, 0xff);
    store_io(rom, &at, 0x0f);
    store_io(rom, &at, 0x4d);
    load_a(rom, &at, 0xc0);
    store_io(rom, &at, 0x51);
    emit(rom, &at, 0xaf);
    store_io(rom, &at, 0x52);
    store_io(rom, &at, 0x53);
    store_io(rom, &at, 0x54);
    load_a(rom, &at, 0x91);
    store_io(rom, &at, 0x40);
    at += 20; /* Arm the transfer after the initial LCD mode-zero window. */
    load_a(rom, &at, 0x80);
    store_io(rom, &at, 0x55);
    at += 32;
    load_a(rom, &at, 0x83);
    store_io(rom, &at, 0x02);
    const uint16_t stop = (uint16_t)at;
    emit(rom, &at, 0x10);
    at += 9; /* STOP's trailing byte and eight further NOPs. */
    emit(rom, &at, 0xf0);
    emit(rom, &at, 0x04);
    emit(rom, &at, 0x18);
    emit(rom, &at, 0xfe);
    checksum(rom);
    return stop;
}

static void test_guest_stop_hblank(void) {
    uint8_t rom[ROM_SIZE];
    const uint16_t stop = make_guest_program(rom);
    CupidGb *gb = load_machine(CUPID_GB_MODEL_CGB, rom);
    memset(gb->boot_rom, 0, 0x900);
    static const uint8_t entry[] = {0xc3, 0xfc, 0x00};
    static const uint8_t handoff[] = {0x3e, 0x01, 0xe0, 0x50};
    memcpy(gb->boot_rom, entry, sizeof(entry));
    memcpy(gb->boot_rom + 0xfc, handoff, sizeof(handoff));
    gb->boot_rom_size = 0x900;
    cupid_gb_start_boot_rom(gb);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_apu_set_sample_rate(gb, 44100));
    unsigned steps = 0;
    while (gb->cpu.pc != stop && steps++ < 1000)
        assert(cupid_gb_step(gb));
    assert(gb->cpu.pc == stop && stop == 0x1da);
    CupidGbStepTrace trace = {0};
    assert(cupid_gb_step_observed(gb, &trace));
    assert(trace.event == CUPID_GB_STEP_INSTRUCTION && trace.opcode == 0x10 &&
           trace.address == stop);
    assert(gb->hdma_pending && gb->hdma_active && gb->hdma_blocks_remaining == 1);
    assert(!gb->cpu.stopped && !gb->cpu.halted && !gb->cpu.ime);
    assert(gb->speed_switch_toggle_cycles == 6 && gb->speed_switch_freeze_cycles == 1);
    assert(divider(gb) == 0 && gb->stop_divider_hold_cycles == 4);
    assert(gb->io_registers[5] == 69 && gb->io_registers[1] == 0x3c);

    static const struct {
        uint16_t pc, div;
        uint8_t tima, sb, serial_clock;
        uint16_t half_cycles;
    } expected[] = {{0x1dc, 32, 71, 0xf3, 1, 2152}, {0x1dd, 36, 71, 0xf3, 1, 2156},
                    {0x1de, 40, 71, 0xe7, 0, 2160}, {0x1df, 44, 71, 0xe7, 0, 2164},
                    {0x1e0, 48, 72, 0xe7, 1, 2168}, {0x1e1, 52, 72, 0xe7, 1, 2172},
                    {0x1e2, 56, 72, 0xcf, 0, 2176}, {0x1e3, 60, 72, 0xcf, 0, 2180},
                    {0x1e4, 64, 73, 0xcf, 1, 2184}, {0x1e6, 76, 73, 0x9f, 0, 2196},
                    {0x1e6, 88, 74, 0x3f, 0, 2208}, {0x1e6, 100, 75, 0x3f, 1, 2220}};

    for (unsigned index = 0; index < sizeof(expected) / sizeof(*expected); ++index) {
        assert(cupid_gb_step(gb));
        assert(gb->cpu.pc == expected[index].pc && divider(gb) == expected[index].div);
        assert(gb->io_registers[5] == expected[index].tima);
        assert(gb->io_registers[1] == expected[index].sb);
        assert(gb->serial_master_clock == (expected[index].serial_clock != 0));
        assert(gb->elapsed_cycles * 2u == expected[index].half_cycles);
        assert(gb->rtc_cycles == expected[index].half_cycles);
        assert(gb->double_speed && !gb->hdma_pending && !gb->hdma_active);
        for (unsigned byte = 0; byte < 16; ++byte)
            assert(gb->video_ram[byte] == 0xa5);
    }
    release(gb);
}

int main(void) {
    test_partial_spans();
    test_div_reset_mid_span();
    test_stop_hold();
    test_freeze_and_audio_clocks();
    test_delayed_envelope();
    test_camera_phi();
    test_dma_remainder();
    test_interrupt_acknowledgment();
    test_halt_poll_phase();
    test_guest_stop_hblank();
    return 0;
}

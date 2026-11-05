#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void prepare(CupidGb *gb, uint8_t opcode, uint8_t flags) {
    uint8_t rom[32768] = {0};
    memcpy(rom + 0x134, "CPU TIMING", 10);
    rom[0x100] = opcode;
    rom[0x102] = 2;
    for (unsigned i = 0x134; i <= 0x14c; ++i) {
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    }
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->cpu.f = flags;
    gb->cpu.sp = 0xc100;
    gb->cpu.ime = false;
    gb->interrupt_enable = 0;
    gb->io_registers[0x40] = 0;
    gb->io_registers[0x04] = 0;
    gb->div_counter = 0;
    gb->work_ram[0x100] = 0;
    gb->work_ram[0x101] = 2;
}

static void step_cycles(CupidGb *gb, unsigned tcycles) {
    uint64_t before = gb->elapsed_cycles;
    assert(cupid_gb_step(gb));
    assert(gb->elapsed_cycles - before == tcycles);
}

static void test_conditional_control_flow(void) {
    CupidGb gb = {0};
    /* NZ, Z, NC, C: exercise both outcomes with the other flags held clear. */
    const uint8_t flags_taken[] = {0, 0x80, 0, 0x10};
    const uint8_t flags_skipped[] = {0x80, 0, 0x10, 0};
    for (unsigned condition = 0; condition < 4; ++condition) {
        uint8_t jump = (uint8_t)(0xc2 + 8 * condition);
        uint8_t call = (uint8_t)(0xc4 + 8 * condition);
        uint8_t ret = (uint8_t)(0xc0 + 8 * condition);
        prepare(&gb, jump, flags_taken[condition]);
        step_cycles(&gb, 16);
        assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc100);
        prepare(&gb, jump, flags_skipped[condition]);
        step_cycles(&gb, 12);
        assert(gb.cpu.pc == 0x103 && gb.cpu.sp == 0xc100);

        prepare(&gb, call, flags_taken[condition]);
        step_cycles(&gb, 24);
        assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc0fe);
        assert(gb.work_ram[0xfe] == 3 && gb.work_ram[0xff] == 1);
        prepare(&gb, call, flags_skipped[condition]);
        gb.work_ram[0xfe] = 0xa5;
        gb.work_ram[0xff] = 0x5a;
        step_cycles(&gb, 12);
        assert(gb.cpu.pc == 0x103 && gb.cpu.sp == 0xc100);
        assert(gb.work_ram[0xfe] == 0xa5 && gb.work_ram[0xff] == 0x5a);

        prepare(&gb, ret, flags_taken[condition]);
        step_cycles(&gb, 20);
        assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc102);
        prepare(&gb, ret, flags_skipped[condition]);
        step_cycles(&gb, 8);
        assert(gb.cpu.pc == 0x101 && gb.cpu.sp == 0xc100);
    }
    cupid_gb_cleanup(&gb);
}

static void test_unconditional_control_flow(void) {
    CupidGb gb = {0};
    prepare(&gb, 0xc3, 0);
    step_cycles(&gb, 16);
    assert(gb.cpu.pc == 0x200);
    prepare(&gb, 0xcd, 0);
    step_cycles(&gb, 24);
    assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc0fe);
    prepare(&gb, 0xc9, 0);
    step_cycles(&gb, 16);
    assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc102);
    prepare(&gb, 0xd9, 0);
    step_cycles(&gb, 16);
    assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc102 && gb.cpu.ime);
    cupid_gb_cleanup(&gb);
}

static void test_stack_bus_phases(void) {
    CupidGb gb = {0};
    /* A low stack byte read from DIV observes the divider after M1 for RET,
     * and after the extra condition cycle for a taken conditional return. */
    prepare(&gb, 0xc9, 0);
    gb.cpu.sp = 0xff04;
    gb.div_counter = 63;
    gb.io_registers[5] = 2;
    step_cycles(&gb, 16);
    assert(gb.cpu.pc == 0x201);
    prepare(&gb, 0xc0, 0);
    gb.cpu.sp = 0xff04;
    gb.div_counter = 62;
    gb.io_registers[5] = 2;
    step_cycles(&gb, 20);
    assert(gb.cpu.pc == 0x201);

    /* CALL writes stack bytes in M5 then M6. Writing DIV resets its phase,
     * exposing the number of cycles remaining after each write. */
    prepare(&gb, 0xcd, 0);
    gb.cpu.sp = 0xff05;
    step_cycles(&gb, 24);
    assert(gb.div_counter == 2);
    prepare(&gb, 0xcd, 0);
    gb.cpu.sp = 0xff06;
    step_cycles(&gb, 24);
    assert(gb.div_counter == 1);
    cupid_gb_cleanup(&gb);
}

static void test_sp_offset_operand_bus_phase(void) {
    CupidGb gb = {0};
    const uint8_t opcodes[] = {0xe8, 0xf8};
    for (unsigned i = 0; i < sizeof(opcodes); ++i) {
        prepare(&gb, opcodes[i], 0);
        /* Execute from the WRAM echo's final byte. Its displacement lives in
         * OAM and becomes readable as the PPU enters HBlank during M1. */
        cupid_gb_write_u8(&gb, 0xfdff, opcodes[i]);
        gb.cpu.pc = 0xfdff;
        gb.cpu.sp = 0x100;
        gb.object_attribute_memory[0] = 1;
        cupid_gb_write_u8(&gb, 0xff40, 0x91);
        bool at_boundary = false;
        for (unsigned t = 0; t < 70224; t += 4) {
            if (gb.io_registers[CUPID_GB_IO_LY] == 1 && gb.pixels.active && gb.pixels.x >= 156) {
                at_boundary = true;
                break;
            }
            cupid_gb_tick_ppu(&gb, 1);
        }
        assert(at_boundary);
        assert(cupid_gb_read_u8(&gb, 0xfe00) == 0xff);
        step_cycles(&gb, i == 0 ? 16 : 12);
        if (i == 0) {
            assert(gb.cpu.sp == 0x101);
        } else {
            assert(gb.cpu.sp == 0x100);
            assert(gb.cpu.h == 1 && gb.cpu.l == 1);
        }
        assert(gb.cpu.pc == 0xfe01 && gb.cpu.f == 0);
    }
    cupid_gb_cleanup(&gb);
}

static void test_interrupt_stack_if_acknowledgment(void) {
    CupidGb gb = {0};
    /* The low return byte can overwrite IF during entry. Interrupt selection
     * uses the pre-write request, then acknowledges that request in the new IF. */
    const uint8_t return_bytes[] = {0, 4, 0x1f};
    for (unsigned i = 0; i < sizeof(return_bytes); ++i) {
        prepare(&gb, 0, 0);
        gb.cpu.pc = (uint16_t)(0x1200 | return_bytes[i]);
        gb.cpu.sp = 0xff11;
        gb.cpu.ime = true;
        gb.interrupt_enable = 4;
        gb.interrupt_flags = 4;
        step_cycles(&gb, 20);
        assert(gb.cpu.pc == 0x50 && gb.cpu.sp == 0xff0f);
        assert(gb.interrupt_flags == (return_bytes[i] & 0x1b));
        assert(!gb.cpu.ime);
    }
    cupid_gb_cleanup(&gb);
}

static void test_interrupt_stack_bus_phase(void) {
    CupidGb gb = {0};
    for (unsigned high = 0; high < 2; ++high) {
        prepare(&gb, 0, 0);
        gb.cpu.pc = 0x1204;
        gb.cpu.sp = (uint16_t)(0xff05 + high);
        gb.cpu.ime = true;
        gb.interrupt_enable = gb.interrupt_flags = 4;
        step_cycles(&gb, 20);
        assert(gb.cpu.pc == 0x50);
        /* High and low stack writes occur at T12 and T16 respectively. A
         * write to DIV exposes the time remaining before entry completes. */
        assert(gb.div_counter == (high ? 1 : 2));
    }
    cupid_gb_cleanup(&gb);
}

static void test_halt_pending_enable_and_fetch_phase(void) {
    CupidGb gb = {0};
    prepare(&gb, 0x76, 0);
    gb.cpu.ime_delay = 1;
    gb.interrupt_enable = gb.interrupt_flags = 4;
    step_cycles(&gb, 4);
    assert(gb.cpu.ime && !gb.cpu.halted && !gb.cpu.halt_bug && !gb.cpu.just_halted);
    assert(gb.cpu.pc == 0x100);
    step_cycles(&gb, 20);
    assert(gb.cpu.pc == 0x50);
    assert(gb.work_ram[0xfe] == 0 && gb.work_ram[0xff] == 1);

    /* A timer request arriving during the opcode fetch is already visible to
     * HALT. With IME clear this suppresses the following PC increment. */
    prepare(&gb, 0x76, 0);
    gb.interrupt_enable = 4;
    gb.interrupt_flags = 0;
    gb.tima_overflow_delay = 1;
    step_cycles(&gb, 4);
    assert(!gb.cpu.halted && gb.cpu.halt_bug && !gb.cpu.just_halted && gb.cpu.pc == 0x101);
    cupid_gb_cleanup(&gb);
}

static void test_stop_clock_and_selected_input(void) {
    CupidGb gb = {0};
    prepare(&gb, 0x10, 0);
    gb.joypad = 0xff;
    cupid_gb_write_u8(&gb, 0xff00, 0x20); /* Select directions. */
    gb.div_counter = 7;
    step_cycles(&gb, 8);
    assert(gb.cpu.stopped && gb.cpu.pc == 0x102);
    assert(gb.div_counter == 0 && gb.io_registers[4] == 0);
    gb.io_registers[5] = 0;
    gb.io_registers[6] = 0x42;
    gb.tima_overflow_delay = 1;
    gb.serial_master_clock = true;
    gb.serial_bits_remaining = 8;
    gb.io_registers[2] = 0x81;
    for (unsigned i = 0; i < 256; ++i)
        step_cycles(&gb, 4);
    assert(gb.div_counter == 0 && gb.io_registers[4] == 0);
    assert(gb.io_registers[5] == 0 && gb.tima_overflow_delay == 1);
    assert(gb.serial_master_clock && gb.serial_bits_remaining == 8);
    gb.interrupt_enable = gb.interrupt_flags = 4;
    gb.cpu.ime = true;
    step_cycles(&gb, 4);
    assert(gb.cpu.stopped && gb.cpu.pc == 0x102 && gb.cpu.ime);
    gb.joypad = 0xef; /* A is held, but its P1 row is not selected. */
    step_cycles(&gb, 4);
    assert(gb.cpu.stopped);
    gb.joypad = 0xfe;
    step_cycles(&gb, 12);
    assert(!gb.cpu.stopped && gb.cpu.pc == 0x102);
    assert(gb.div_counter == 2 && gb.io_registers[5] == 0x42);
    step_cycles(&gb, 20);
    assert(gb.cpu.pc == 0x50);

    prepare(&gb, 0x10, 0);
    cupid_gb_write_u8(&gb, 0xff00, 0x20);
    gb.joypad = 0xfe;
    gb.div_counter = 7;
    step_cycles(&gb, 8);
    assert(!gb.cpu.stopped && gb.cpu.halted && gb.cpu.just_halted && gb.cpu.pc == 0x102 &&
           gb.div_counter == 9);

    prepare(&gb, 0x10, 0);
    gb.interrupt_enable = gb.interrupt_flags = 4;
    gb.joypad = 0xff;
    step_cycles(&gb, 4);
    assert(gb.cpu.stopped && gb.cpu.pc == 0x101);
    cupid_gb_cleanup(&gb);
}

static void test_stop_key_prevents_speed_switch(void) {
    CupidGb gb = {0};
    prepare(&gb, 0x10, 0);
    gb.cgb_mode = true;
    gb.speed_switch_armed = true;
    gb.interrupt_enable = gb.interrupt_flags = 4;
    cupid_gb_write_u8(&gb, 0xff00, 0x20);
    gb.joypad = 0xfe;
    step_cycles(&gb, 4);
    assert(!gb.double_speed && gb.speed_switch_armed);
    assert(!gb.cpu.stopped && !gb.cpu.halted && gb.cpu.pc == 0x101);
    cupid_gb_cleanup(&gb);
}

static void test_halt_wake_clock_cycle(void) {
    CupidGb gb = {0};
    for (unsigned color = 0; color < 2; ++color) {
        for (unsigned enabled = 0; enabled < 2; ++enabled) {
            for (unsigned delayed = 0; delayed < 2; ++delayed) {
                prepare(&gb, 0, 0);
                gb.model = color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
                gb.cgb_mode = color != 0;
                gb.cpu.halted = true;
                gb.cpu.ime = enabled != 0;
                gb.interrupt_enable = 4;
                gb.interrupt_flags = delayed ? 0 : 4;
                gb.tima_overflow_delay = (uint8_t)delayed;
                /* A pending request needs the wake cycle plus five entry
                 * cycles. A request arriving after the polling point first
                 * needs one idle cycle. IME clear also executes the next NOP. */
                if (delayed) {
                    step_cycles(&gb, 4);
                    assert(gb.cpu.halted && gb.cpu.pc == 0x100);
                }
                step_cycles(&gb, enabled ? 24u : 8u);
                assert(!gb.cpu.halted && gb.cpu.pc == (enabled ? 0x50 : 0x101));
                assert((gb.interrupt_flags & 4u) == (enabled ? 0u : 4u));
                assert(gb.cpu.sp == (enabled ? 0xc0fe : 0xc100));
                if (enabled) {
                    assert(gb.work_ram[0xfe] == 0 && gb.work_ram[0xff] == 1);
                }
            }
        }
    }
    cupid_gb_cleanup(&gb);
}

static void test_first_halt_poll_precedes_midpoint(void) {
    CupidGb gb = {0};
    for (unsigned first = 0; first < 2; ++first) {
        prepare(&gb, 0, 0);
        gb.cpu.halted = true;
        gb.cpu.just_halted = first != 0;
        gb.cpu.ime = true;
        gb.interrupt_enable = 1;
        gb.interrupt_flags = 0;
        gb.io_registers[CUPID_GB_IO_LCDC] = 0x80;
        gb.io_registers[CUPID_GB_IO_LY] = 144;
        gb.pixels.initialized = true;
        gb.pixels.line = 144;
        gb.pixels.dot = 0;
        if (first) {
            /* VBlank arrives in the first half, after the initial polling
             * point. The next HALT cycle sees it at the usual midpoint. */
            step_cycles(&gb, 4);
            assert(gb.cpu.halted && !gb.cpu.just_halted && gb.cpu.pc == 0x100);
            assert(gb.interrupt_flags & 1);
        }
        step_cycles(&gb, 24);
        assert(!gb.cpu.halted && !gb.cpu.just_halted && gb.cpu.pc == 0x40);
        assert(!(gb.interrupt_flags & 1));
    }

    prepare(&gb, 0x76, 0);
    step_cycles(&gb, 4);
    assert(gb.cpu.halted && gb.cpu.just_halted);
    gb.boot_rom_size = 256;
    cupid_gb_enter_boot_rom(&gb);
    assert(!gb.cpu.halted && !gb.cpu.just_halted);
    cupid_gb_cleanup(&gb);
}

static void test_timer_periods_and_register_edges(void) {
    const uint16_t periods[] = {256, 4, 16, 64};
    for (unsigned selection = 0; selection < 4; ++selection) {
        CupidGb gb = {0};
        gb.io_registers[7] = (uint8_t)(4u | selection);
        cupid_gb_tick(&gb, (uint16_t)(periods[selection] - 1u));
        assert(gb.io_registers[5] == 0);
        cupid_gb_tick(&gb, 1);
        assert(gb.io_registers[5] == 1);

        /* Resetting or disabling while the selected divider input is high
         * clocks TIMA once. Repeating either write at a low input does not. */
        cupid_gb_tick(&gb, periods[selection] / 2u);
        cupid_gb_write_u8(&gb, 0xff04, 0xff);
        assert(gb.io_registers[5] == 2 && gb.io_registers[4] == 0 && gb.div_counter == 0);
        cupid_gb_write_u8(&gb, 0xff04, 0);
        assert(gb.io_registers[5] == 2);
        cupid_gb_tick(&gb, periods[selection] / 2u);
        cupid_gb_write_u8(&gb, 0xff07, 0);
        assert(gb.io_registers[5] == 3);
        cupid_gb_write_u8(&gb, 0xff07, 0);
        assert(gb.io_registers[5] == 3);
    }
}

static void test_timer_reload_write_window(void) {
    CupidGb gb = {0};
    gb.io_registers[7] = 5;
    gb.io_registers[5] = 0xff;
    gb.io_registers[6] = 0x42;
    cupid_gb_tick(&gb, 4);
    assert(gb.io_registers[5] == 0 && !(gb.interrupt_flags & 4));
    cupid_gb_write_u8(&gb, 0xff06, 0x73);
    cupid_gb_tick(&gb, 1);
    assert(gb.io_registers[5] == 0x73 && (gb.interrupt_flags & 4));
    cupid_gb_write_u8(&gb, 0xff05, 0x12);
    assert(gb.io_registers[5] == 0x73);
    cupid_gb_write_u8(&gb, 0xff06, 0x38);
    assert(gb.io_registers[5] == 0x38);
    cupid_gb_tick(&gb, 1);
    cupid_gb_write_u8(&gb, 0xff05, 0x12);
    assert(gb.io_registers[5] == 0x12);

    gb = (CupidGb){0};
    gb.io_registers[7] = 5;
    gb.io_registers[5] = 0xff;
    cupid_gb_tick(&gb, 4);
    cupid_gb_write_u8(&gb, 0xff05, 0x9a);
    cupid_gb_tick(&gb, 1);
    assert(gb.io_registers[5] == 0x9a && !(gb.interrupt_flags & 4));
}

static void test_pending_interrupt_precedes_di(void) {
    CupidGb gb = {0};
    prepare(&gb, 0xf3, 0);
    gb.io_registers[CUPID_GB_IO_LY] = 144;
    gb.cpu.ime = true;
    gb.interrupt_enable = gb.interrupt_flags = 1;
    step_cycles(&gb, 20);
    assert(gb.cpu.pc == 0x40 && !gb.cpu.ime);
    assert(gb.work_ram[0xfe] == 0 && gb.work_ram[0xff] == 1);
    cupid_gb_cleanup(&gb);
}

static void test_io_read_before_final_machine_cycle(void) {
    CupidGb gb = {0};
    for (unsigned phase = 61; phase <= 63; ++phase) {
        prepare(&gb, 0xf0, 0);
        gb.rom[0x101] = 4;
        gb.io_registers[4] = 0x12;
        gb.div_counter = (uint16_t)phase;
        step_cycles(&gb, 12);
        assert(gb.cpu.a == (phase == 61 ? 0x12 : 0x13));
        assert(gb.io_registers[4] == 0x13 && gb.div_counter == phase - 61);
    }
    cupid_gb_cleanup(&gb);
}

static void test_lcd_enable_during_io_write_cycle(void) {
    CupidGb gb = {0};
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB};
    const uint8_t stores[] = {0x02, 0x12, 0x22, 0x32, 0x36, 0x70, 0x71,
                              0x72, 0x73, 0x77, 0xe0, 0xe2, 0xea};
    for (size_t m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (size_t i = 0; i < sizeof(stores); ++i) {
            const uint8_t opcode = stores[i];
            prepare(&gb, opcode, 0);
            gb.model = models[m];
            gb.cgb_mode = models[m] == CUPID_GB_MODEL_CGB;
            gb.cpu.a = gb.cpu.b = gb.cpu.c = gb.cpu.d = gb.cpu.e = 0x91;
            gb.cpu.h = 0xff;
            gb.cpu.l = 0x40;
            if (opcode == 0x02) {
                gb.cpu.b = 0xff;
                gb.cpu.c = 0x40;
            } else if (opcode == 0x12) {
                gb.cpu.d = 0xff;
                gb.cpu.e = 0x40;
            } else if (opcode == 0xe2) {
                gb.cpu.c = 0x40;
            }
            gb.rom[0x101] = opcode == 0x36 ? 0x91 : 0x40;
            gb.rom[0x102] = 0xff;
            unsigned cycles = opcode == 0xea ? 16u : opcode == 0xe0 || opcode == 0x36 ? 12u : 8u;
            step_cycles(&gb, cycles);
            /* Monochrome LCDC settles one dot before the ordinary bus boundary.
             * LDH's full value appears at T7, leaving five LCD dots by T12. */
            assert(gb.pixels.initialized && gb.pixels.line == 0);
            assert(gb.pixels.dot == (models[m] == CUPID_GB_MODEL_CGB ? 4 : 5));
            assert(gb.io_registers[0x40] == 0x91 && gb.div_counter == cycles / 4u);
        }
    }

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG_ABC);
    prepare(&gb, 0x08, 0);
    gb.rom[0x101] = 0x40;
    gb.rom[0x102] = 0xff;
    gb.cpu.sp = 0x0091;
    step_cycles(&gb, 20);
    assert(gb.pixels.initialized && gb.pixels.dot == 9);
    prepare(&gb, 0x08, 0);
    gb.rom[0x101] = 0x3f;
    gb.rom[0x102] = 0xff;
    gb.cpu.sp = 0x9100;
    step_cycles(&gb, 20);
    assert(gb.pixels.initialized && gb.pixels.dot == 5);
    cupid_gb_cleanup(&gb);
}

static void test_lcd_enable_during_read_modify_write(void) {
    static const struct {
        uint8_t opcode, before, carry, after;
    } cases[] = {
        {0x06, 0x40, 0, 0x80}, /* RLC (HL) */
        {0x0e, 0x01, 0, 0x80}, /* RRC (HL) */
        {0x16, 0x40, 0, 0x80}, /* RL (HL) */
        {0x1e, 0x00, 1, 0x80}, /* RR (HL) */
        {0x26, 0x40, 0, 0x80}, /* SLA (HL) */
        {0x36, 0x08, 0, 0x80}, /* SWAP (HL) */
        {0xfe, 0x11, 0, 0x91}, /* SET 7,(HL) */
    };

    static const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0,
                                          CUPID_GB_MODEL_MGB,     CUPID_GB_MODEL_SGB,
                                          CUPID_GB_MODEL_SGB2,    CUPID_GB_MODEL_CGB};
    CupidGb gb = {0};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            prepare(&gb, 0xcb, cases[i].carry ? 0x10 : 0);
            gb.model = models[m];
            gb.cgb_mode = gb.model == CUPID_GB_MODEL_CGB;
            gb.rom[0x101] = cases[i].opcode;
            gb.cpu.h = 0xff;
            gb.cpu.l = 0x40;
            gb.io_registers[CUPID_GB_IO_LCDC] = cases[i].before;
            step_cycles(&gb, 16);
            assert(gb.io_registers[CUPID_GB_IO_LCDC] == cases[i].after);
            /* These operations read during M3 and settle LCDC during M4.
             * The final LCD power edge leaves five dots on monochrome hardware. */
            assert(gb.pixels.initialized && gb.pixels.line == 0);
            assert(gb.pixels.dot == (gb.model == CUPID_GB_MODEL_CGB ? 4 : 5));
            assert(gb.div_counter == 4 && gb.cpu.pc == 0x102);
        }
        for (unsigned decrement = 0; decrement < 2; ++decrement) {
            prepare(&gb, decrement ? 0x35 : 0x34, 0);
            gb.model = models[m];
            gb.cgb_mode = gb.model == CUPID_GB_MODEL_CGB;
            gb.cpu.h = 0xff;
            gb.cpu.l = 0x40;
            gb.io_registers[CUPID_GB_IO_LCDC] = decrement ? 0 : 0x7f;
            step_cycles(&gb, 12);
            assert(gb.io_registers[CUPID_GB_IO_LCDC] == (decrement ? 0xff : 0x80));
            assert(gb.pixels.initialized && gb.pixels.line == 0);
            assert(gb.pixels.dot == (gb.model == CUPID_GB_MODEL_CGB ? 4 : 5));
            assert(gb.div_counter == 3 && gb.cpu.pc == 0x101);
        }
    }
    cupid_gb_cleanup(&gb);
}

static void test_lcd_enable_during_stack_writes(void) {
    static const uint8_t pushes[] = {0xc5, 0xd5, 0xe5, 0xf5};
    static const uint8_t calls[] = {0xc4, 0xcc, 0xd4, 0xdc, 0xcd};
    static const uint8_t taken[] = {0, 0x80, 0, 0x10, 0};
    static const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_SGB,
                                          CUPID_GB_MODEL_CGB};
    CupidGb gb = {0};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned low = 0; low < 2; ++low) {
            unsigned expected = (low ? 4u : 8u) + (models[m] == CUPID_GB_MODEL_CGB ? 0u : 1u);
            for (unsigned i = 0; i < sizeof(pushes); ++i) {
                prepare(&gb, pushes[i], 0x90);
                gb.model = models[m];
                gb.cgb_mode = gb.model == CUPID_GB_MODEL_CGB;
                gb.cpu.a = gb.cpu.b = gb.cpu.c = gb.cpu.d = gb.cpu.e = gb.cpu.h = gb.cpu.l = 0x91;
                gb.cpu.sp = (uint16_t)(0xff41 + low);
                step_cycles(&gb, 16);
                assert(gb.pixels.initialized && gb.pixels.dot == expected);
                assert(gb.div_counter == 4 && gb.cpu.sp == (uint16_t)(0xff3f + low));
                assert(gb.io_registers[CUPID_GB_IO_LCDC] ==
                       (pushes[i] == 0xf5 && low ? 0x90 : 0x91));
            }
            for (unsigned i = 0; i < sizeof(calls); ++i) {
                prepare(&gb, 0, taken[i]);
                gb.model = models[m];
                gb.cgb_mode = gb.model == CUPID_GB_MODEL_CGB;
                gb.cpu.pc = 0xc090;
                gb.work_ram[0x90] = calls[i];
                gb.work_ram[0x91] = 0;
                gb.work_ram[0x92] = 2;
                gb.cpu.sp = (uint16_t)(0xff41 + low);
                step_cycles(&gb, 24);
                assert(gb.pixels.initialized && gb.pixels.dot == expected);
                assert(gb.div_counter == 6 && gb.cpu.pc == 0x200);
                assert(gb.io_registers[CUPID_GB_IO_LCDC] == (low ? 0x93 : 0xc0));
            }
            for (unsigned i = 0; i < 8; ++i) {
                prepare(&gb, 0, 0);
                gb.model = models[m];
                gb.cgb_mode = gb.model == CUPID_GB_MODEL_CGB;
                gb.cpu.pc = 0xc090;
                gb.work_ram[0x90] = (uint8_t)(0xc7 + 8u * i);
                gb.cpu.sp = (uint16_t)(0xff41 + low);
                step_cycles(&gb, 16);
                assert(gb.pixels.initialized && gb.pixels.dot == expected);
                assert(gb.div_counter == 4 && gb.cpu.pc == 8u * i);
                assert(gb.io_registers[CUPID_GB_IO_LCDC] == (low ? 0x91 : 0xc0));
            }
        }

        prepare(&gb, 0, 0);
        gb.model = models[m];
        gb.cgb_mode = gb.model == CUPID_GB_MODEL_CGB;
        gb.cpu.pc = 0x8091;
        gb.cpu.sp = 0xff41;
        gb.cpu.ime = true;
        gb.interrupt_enable = gb.interrupt_flags = 4;
        step_cycles(&gb, 20);
        assert(gb.cpu.pc == 0x50 && gb.cpu.sp == 0xff3f);
        assert(gb.pixels.initialized && gb.pixels.dot == (gb.cgb_mode ? 8 : 9));
    }
    cupid_gb_cleanup(&gb);
}

static void test_register_indirect_bus_phase(void) {
    CupidGb gb = {0};
    const uint8_t reads[] = {0x0a, 0x1a, 0x2a, 0x3a, 0x7e, 0xf2};
    const uint8_t writes[] = {0x02, 0x12, 0x22, 0x32, 0x77, 0xe2};
    for (unsigned i = 0; i < sizeof(reads); ++i) {
        prepare(&gb, reads[i], 0);
        gb.cpu.b = gb.cpu.d = gb.cpu.h = 0xff;
        gb.cpu.c = gb.cpu.e = gb.cpu.l = 4;
        gb.io_registers[4] = 0x12;
        gb.div_counter = 63;
        step_cycles(&gb, 8);
        assert(gb.cpu.a == 0x13);

        prepare(&gb, writes[i], 0);
        gb.cpu.b = gb.cpu.d = gb.cpu.h = 0xff;
        gb.cpu.c = gb.cpu.e = gb.cpu.l = 4;
        gb.div_counter = 7;
        step_cycles(&gb, 8);
        assert(gb.div_counter == 1 && gb.io_registers[4] == 0);
    }
    cupid_gb_cleanup(&gb);
}

int main(void) {
    test_conditional_control_flow();
    test_unconditional_control_flow();
    test_stack_bus_phases();
    test_sp_offset_operand_bus_phase();
    test_interrupt_stack_if_acknowledgment();
    test_interrupt_stack_bus_phase();
    test_halt_pending_enable_and_fetch_phase();
    test_stop_clock_and_selected_input();
    test_stop_key_prevents_speed_switch();
    test_halt_wake_clock_cycle();
    test_first_halt_poll_precedes_midpoint();
    test_timer_periods_and_register_edges();
    test_timer_reload_write_window();
    test_pending_interrupt_precedes_di();
    test_io_read_before_final_machine_cycle();
    test_lcd_enable_during_io_write_cycle();
    test_lcd_enable_during_read_modify_write();
    test_lcd_enable_during_stack_writes();
    test_register_indirect_bus_phase();
    puts("CPU control-flow timing checks passed.");
    return 0;
}

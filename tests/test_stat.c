/**
 * @file    test_stat.c
 * @brief   Regression tests for LCD status timing and interrupt conditions.
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
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Store {
    uint8_t bytes[3];
    unsigned boundary, cycles;
    bool decrement;
} Store;

static CupidGb *prepare(CupidGbModel model, bool double_speed, const uint8_t *program, size_t count,
                        uint8_t old_mask, uint8_t lyc, unsigned line, unsigned boundary) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t rom[32768] = {0};
    assert(gb);
    memcpy(rom + 0x100, program, count);
    rom[0x143] = cupid_gb_model_is_cgb(model) ? 0x80 : 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->double_speed = double_speed;
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff07, 0);
    cupid_gb_write_u8(gb, 0xff42, 0);
    cupid_gb_write_u8(gb, 0xff43, 0);
    cupid_gb_write_u8(gb, 0xff4b, 255);
    cupid_gb_write_u8(gb, 0xff45, lyc);
    cupid_gb_write_u8(gb, 0xff41, old_mask);
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    unsigned edge = (cupid_gb_model_is_cgb(model) ? 452u : 453u) + (line - 1u) * 456u;
    cupid_gb_tick_ppu_dots(gb, (uint16_t)(edge - boundary / (double_speed ? 2u : 1u)));
    gb->cpu.pc = 0x100;
    gb->cpu.sp = 0xc100;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
    gb->interrupt_enable = 6;
    gb->interrupt_flags = 4;
    gb->cpu.a = 0;
    gb->cpu.b = 0xff;
    gb->cpu.c = 0x41;
    gb->cpu.d = 0xff;
    gb->cpu.e = 0x41;
    gb->cpu.h = 0xff;
    gb->cpu.l = 0x41;
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_cpu_store_routes(void) {
    const Store stores[] = {
        {{0xe0, 0x41}, 8, 12, false},  {{0xe2}, 4, 8, false}, {{0xea, 0x41, 0xff}, 12, 16, false},
        {{0x77}, 4, 8, false},         {{0x02}, 4, 8, false}, {{0x12}, 4, 8, false},
        {{0x22}, 4, 8, false},         {{0x32}, 4, 8, false}, {{0x36, 0}, 8, 12, false},
        {{0x34}, 8, 12, false},        {{0x35}, 8, 12, true}, {{0xcb, 0x06}, 12, 16, false},
        {{0xcb, 0xbe}, 12, 16, false},
    };
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned speed = 0; speed <= (models[m] == CUPID_GB_MODEL_CGB); ++speed) {
            for (unsigned i = 0; i < sizeof(stores) / sizeof(stores[0]); ++i) {
                const Store *store = &stores[i];
                CupidGb *gb = prepare(models[m], speed != 0, store->bytes, sizeof(store->bytes), 0,
                                      255, 1, store->boundary);
                const uint64_t start = gb->elapsed_cycles;
                assert(cupid_gb_step(gb));
                assert(gb->elapsed_cycles - start == store->cycles / (speed ? 2u : 1u));
                bool pulse = models[m] != CUPID_GB_MODEL_CGB || store->decrement;
                assert(gb->interrupt_flags == (pulse ? 6 : 4));
                assert((cupid_gb_read_u8(gb, 0xff41) & 3) == CUPID_GB_PPU_MODE_OAM);
                destroy(gb);
            }
        }
    }
}

static void test_interrupt_matching_coincidence(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB, CUPID_GB_MODEL_SGB,
                                   CUPID_GB_MODEL_SGB2};
    const unsigned lines[] = {1, 2, 143};
    const uint8_t values[] = {0, 0x20, 0x40, 0x60, 0x78};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned line = 0; line < sizeof(lines) / sizeof(lines[0]); ++line) {
            for (unsigned low = 0; low < 2; ++low) {
                for (unsigned value = 0; value < sizeof(values); ++value) {
                    const uint8_t program[] = {0};
                    CupidGb *gb = prepare(models[m], false, program, sizeof(program), 0x48,
                                          (uint8_t)lines[line], lines[line], low ? 16 : 12);
                    gb->cpu.sp = low ? 0xff43 : 0xff42;
                    gb->cpu.pc = low ? values[value] : (uint16_t)((values[value] << 8) | 0x91);
                    gb->cpu.ime = true;
                    const uint64_t start = gb->elapsed_cycles;
                    assert(cupid_gb_step(gb));
                    assert(gb->elapsed_cycles - start == 20);
                    assert(gb->cpu.pc == 0x48 && gb->interrupt_flags == 4);
                    assert(gb->cpu.sp == (low ? 0xff41 : 0xff40));
                    assert(cupid_gb_read_u8(gb, 0xff41) == (uint8_t)(0x86 | values[value]));
                    destroy(gb);
                }
            }
        }
    }
}

static void check_snapshot_continuation(CupidGb *gb) {
    const size_t length = cupid_gb_state_size(gb);
    uint8_t *before = malloc(length), *after = malloc(length), *replayed = malloc(length);
    size_t written = 0;
    assert(before && after && replayed);
    assert(cupid_gb_save_state(gb, before, length, &written) == CUPID_GB_STATE_OK);
    assert(written == length);
    for (unsigned step = 0; step < 4; ++step)
        assert(cupid_gb_step(gb));
    assert(cupid_gb_save_state(gb, after, length, &written) == CUPID_GB_STATE_OK);
    assert(cupid_gb_load_state(gb, before, length) == CUPID_GB_STATE_OK);
    for (unsigned step = 0; step < 4; ++step)
        assert(cupid_gb_step(gb));
    assert(cupid_gb_save_state(gb, replayed, length, &written) == CUPID_GB_STATE_OK);
    assert(memcmp(after, replayed, length) == 0);
    assert(cupid_gb_load_state(gb, before, length) == CUPID_GB_STATE_OK);
    free(replayed);
    free(after);
    free(before);
}

static void test_stack_and_word_stores(void) {
    const uint8_t programs[][3] = {{0xc5}, {0xc5}, {0xcd, 0x34, 0x12}, {0xcd, 0x34, 0x12},
                                   {0xc7}, {0xc7}, {0x08, 0x41, 0xff}, {0x08, 0x40, 0xff}};
    const unsigned boundary[] = {8, 12, 16, 20, 8, 12, 12, 16};
    const unsigned cycles[] = {16, 16, 24, 24, 16, 16, 20, 20};
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB, CUPID_GB_MODEL_SGB,
                                   CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned speed = 0; speed <= (models[m] == CUPID_GB_MODEL_CGB); ++speed) {
            for (unsigned route = 0; route < sizeof(boundary) / sizeof(boundary[0]); ++route) {
                CupidGb *gb =
                    prepare(models[m], speed != 0, programs[route], 3, 0, 255, 2, boundary[route]);
                if (route < 6) {
                    gb->cpu.sp = route & 1u ? 0xff43 : 0xff42;
                    gb->cpu.b = 0;
                    gb->cpu.c = 0x91;
                    /* The return address keeps LCDC enabled when the second
                     * stack byte lands below STAT. Both bytes use the CPU bus. */
                    if (route >= 2) {
                        gb->cpu.pc = route < 4 ? 0x18e : 0x190;
                        memcpy(gb->rom + gb->cpu.pc, programs[route], 3);
                    }
                } else {
                    gb->cpu.sp = route == 6 ? 0 : 0x91;
                }
                const uint64_t start = gb->elapsed_cycles;
                assert(cupid_gb_step(gb));
                assert(gb->elapsed_cycles - start == cycles[route] / (speed ? 2u : 1u));
                assert(gb->interrupt_flags == (models[m] == CUPID_GB_MODEL_CGB ? 4 : 6));
                assert((cupid_gb_read_u8(gb, 0xff41) & 3) == CUPID_GB_PPU_MODE_OAM);
                check_snapshot_continuation(gb);
                destroy(gb);
            }
        }
    }
}

static void test_color_first_oam_write_edge(void) {
    const uint8_t program[] = {0xe2};
    const uint8_t expected[2][5] = {{6, 6, 6, 4, 4}, {6, 6, 6, 4, 6}};
    for (unsigned speed = 0; speed < 2; ++speed) {
        for (int offset = -2; offset <= 2; ++offset) {
            CupidGb *gb = prepare(CUPID_GB_MODEL_CGB, speed != 0, program, sizeof(program), 0, 255,
                                  2, (unsigned)(4 - offset * (speed ? 2 : 1)));
            gb->cpu.a = 0x20;
            check_snapshot_continuation(gb);
            assert(cupid_gb_step(gb));
            assert(gb->interrupt_flags == expected[speed][offset + 2]);
            destroy(gb);
        }
    }
}

static void test_stat_during_delayed_speed_switch(void) {
    const uint8_t program[] = {0x10, 0xe2, 0};
    CupidGb *gb = prepare(CUPID_GB_MODEL_CGB, false, program, sizeof(program), 0, 255, 2, 16);
    gb->interrupt_enable = 4;
    cupid_gb_write_u8(gb, 0xff4d, 1);
    assert(cupid_gb_step(gb));
    assert(gb->cpu.pc == 0x101 && !gb->double_speed);
    assert(gb->speed_switch_toggle_cycles == 6 && gb->speed_switch_freeze_cycles == 1);
    check_snapshot_continuation(gb);
    uint64_t start = gb->elapsed_cycles;
    uint32_t sample_phase = gb->apu.output.sample_acc;
    assert(cupid_gb_step(gb));
    assert(gb->cpu.pc == 0x102 && gb->double_speed);
    /* DIV clocks only two audio dots across this six-dot LCD interval. */
    assert(gb->apu.output.sample_acc - sample_phase == 2u * cupid_gb_apu_get_sample_rate(gb));
    assert(gb->speed_switch_toggle_cycles == 0 && gb->speed_switch_freeze_cycles == 0);
    assert(gb->elapsed_cycles - start == 6);
    assert(gb->div_counter == 1 && (cupid_gb_read_u8(gb, 0xff4d) & 0x80));
    start = gb->elapsed_cycles;
    assert(cupid_gb_step(gb));
    assert(gb->elapsed_cycles - start == 2);
    destroy(gb);
}

static void test_invalid_comparison_latches(void) {
    const uint8_t program[] = {0};
    const CupidGbModel models[] = {
        CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB,   CUPID_GB_MODEL_SGB,   CUPID_GB_MODEL_SGB2,
        CUPID_GB_MODEL_CGB_0,   CUPID_GB_MODEL_CGB_A, CUPID_GB_MODEL_CGB_B, CUPID_GB_MODEL_CGB_C,
        CUPID_GB_MODEL_CGB_D,   CUPID_GB_MODEL_CGB_E};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned speed = 0; speed <= cupid_gb_model_is_cgb(models[m]); ++speed) {
            CupidGb *gb = prepare(models[m], speed != 0, program, sizeof(program), 0x40, 0, 1, 0);
            assert(gb->pixels.line == 1 && gb->pixels.dot == 0);
            assert(cupid_gb_read_u8(gb, 0xff44) == 1);
            const bool retained_bit =
                cupid_gb_model_is_cgb(models[m]) &&
                (speed || models[m] == CUPID_GB_MODEL_CGB_D || models[m] == CUPID_GB_MODEL_CGB_E);
            const uint8_t boundary_stat = retained_bit ? 4 : 0;
            assert((cupid_gb_read_u8(gb, 0xff41) & 7) == boundary_stat);
            assert(gb->pixels.lyc_irq);
            gb->interrupt_flags = 0;
            cupid_gb_write_u8(gb, 0xff45, 1);
            assert((cupid_gb_read_u8(gb, 0xff41) & 7) == boundary_stat);
            assert(gb->pixels.lyc_irq && gb->interrupt_flags == 0);
            cupid_gb_tick_ppu_dots(gb, 1);
            assert((cupid_gb_read_u8(gb, 0xff41) & 7) == 6);
            assert(gb->pixels.lyc_irq && gb->interrupt_flags == 0);
            check_snapshot_continuation(gb);
            destroy(gb);
        }
    }
}

static void test_double_speed_ly_half_edge(void) {
    const uint8_t program[] = {0};
    CupidGb *gb = prepare(CUPID_GB_MODEL_CGB, true, program, sizeof(program), 0x40, 0, 1, 2);
    assert(gb->pixels.line == 0 && gb->pixels.dot == 451);
    const uint64_t elapsed = gb->elapsed_cycles;
    assert(cupid_gb_read_u8(gb, 0xff44) == 0);
    gb->interrupt_flags = 0;
    cupid_gb_sync_stat_half(gb);
    assert(gb->pixels.line == 0 && gb->pixels.dot == 451 && gb->elapsed_cycles == elapsed);
    assert(cupid_gb_read_u8(gb, 0xff44) == 1);
    assert((cupid_gb_read_u8(gb, 0xff41) & 7) == 4);
    assert(gb->pixels.lyc_irq && gb->interrupt_flags == 0);
    cupid_gb_tick_ppu_dots(gb, 1);
    assert(gb->pixels.line == 1 && gb->pixels.dot == 0);
    assert((cupid_gb_read_u8(gb, 0xff41) & 7) == 4);
    cupid_gb_sync_stat_half(gb);
    assert((cupid_gb_read_u8(gb, 0xff41) & 7) == 2);
    assert(!gb->pixels.lyc_irq && gb->interrupt_flags == 0);
    destroy(gb);
}

static void compare_lcd_progress(CupidGb *written, CupidGb *control) {
    /* Reading the display registers settles deferred work on both machines. */
    assert(cupid_gb_read_u8(written, 0xff44) == cupid_gb_read_u8(control, 0xff44));
    assert(cupid_gb_read_u8(written, 0xff41) == cupid_gb_read_u8(control, 0xff41));
    assert(cupid_gb_read_u8(written, 0xff0f) == cupid_gb_read_u8(control, 0xff0f));
    assert(written->elapsed_cycles == control->elapsed_cycles);
    assert(written->vblank_events == control->vblank_events);
}

static void advance_lcd_dots(CupidGb *gb, unsigned dots) {
    while (dots) {
        const unsigned count = dots > UINT16_MAX ? UINT16_MAX : dots;
        cupid_gb_tick_ppu_dots(gb, (uint16_t)count);
        dots -= count;
    }
}

static void test_ly_ignores_cpu_stores(void) {
    const Store stores[] = {
        {{0xe0, 0x44}, 0, 12, false}, {{0xe2}, 0, 8, false},  {{0xea, 0x44, 0xff}, 0, 16, false},
        {{0x77}, 0, 8, false},        {{0x02}, 0, 8, false},  {{0x12}, 0, 8, false},
        {{0x22}, 0, 8, false},        {{0x32}, 0, 8, false},  {{0x36, 0xff}, 0, 12, false},
        {{0x34}, 0, 12, false},       {{0x35}, 0, 12, false}, {{0xcb, 0x86}, 0, 16, false},
        {{0xcb, 0xc6}, 0, 16, false},
    };
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB};
    /* Begin on line 2, then cover OAM, transfer, HBlank, line boundaries,
     * VBlank and the shortened LY=153 interval. The final case disables LCD. */
    const unsigned phases[] = {
        0, 76, 80, 172, 248, 448, 452, 456, 142u * 456u + 8u, 151u * 456u + 4u, 0};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned speed = 0; speed <= (models[model] == CUPID_GB_MODEL_CGB); ++speed) {
            for (unsigned phase = 0; phase < sizeof(phases) / sizeof(phases[0]); ++phase) {
                for (unsigned route = 0; route < sizeof(stores) / sizeof(stores[0]); ++route) {
                    uint8_t control_program[3];
                    memcpy(control_program, stores[route].bytes, sizeof(control_program));
                    if (control_program[0] == 0xe0 || control_program[0] == 0xea)
                        control_program[1] = 0x7f;
                    CupidGb *written =
                        prepare(models[model], speed != 0, stores[route].bytes, 3, 0x78, 2, 2, 0);
                    CupidGb *control =
                        prepare(models[model], speed != 0, control_program, 3, 0x78, 2, 2, 0);
                    written->cpu.a = control->cpu.a = 0xff;
                    written->cpu.c = written->cpu.e = written->cpu.l = 0x44;
                    control->cpu.c = control->cpu.e = control->cpu.l = 0x7f;
                    advance_lcd_dots(written, phases[phase]);
                    advance_lcd_dots(control, phases[phase]);
                    if (phase == sizeof(phases) / sizeof(phases[0]) - 1) {
                        cupid_gb_write_u8(written, 0xff40, 0);
                        cupid_gb_write_u8(control, 0xff40, 0);
                    }
                    const uint64_t start = written->elapsed_cycles;
                    assert(cupid_gb_step(written) && cupid_gb_step(control));
                    assert(written->elapsed_cycles - start ==
                           stores[route].cycles / (speed ? 2u : 1u));
                    compare_lcd_progress(written, control);
                    /* NOP continuation exposes a disturbed line counter or
                     * interrupt latch even when LY readback was already zero. */
                    written->cpu.pc = control->cpu.pc = 0x200;
                    for (unsigned step = 0; step < 128; ++step) {
                        assert(cupid_gb_step(written) && cupid_gb_step(control));
                        compare_lcd_progress(written, control);
                    }
                    destroy(control);
                    destroy(written);
                }
            }
        }
    }
}

static void test_ly_ignores_all_host_values(void) {
    const uint8_t program[] = {0};
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb *gb = prepare((CupidGbModel)model, false, program, sizeof(program), 0x78, 2, 2, 0);
        const uint8_t ly = cupid_gb_read_u8(gb, 0xff44);
        const uint8_t stat = cupid_gb_read_u8(gb, 0xff41);
        const uint8_t requests = cupid_gb_read_u8(gb, 0xff0f);
        const uint64_t elapsed = gb->elapsed_cycles;
        for (unsigned value = 0; value <= 0xff; ++value) {
            cupid_gb_write_u8(gb, 0xff44, (uint8_t)value);
            assert(cupid_gb_read_u8(gb, 0xff44) == ly);
            assert(cupid_gb_read_u8(gb, 0xff41) == stat);
            assert(cupid_gb_read_u8(gb, 0xff0f) == requests);
            assert(gb->elapsed_cycles == elapsed);
        }
        destroy(gb);
    }
}

static const CupidGbModel color_revisions[] = {
    CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A, CUPID_GB_MODEL_CGB_B,
    CUPID_GB_MODEL_CGB_C, CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB_E,
};

static CupidGb *prepare_color_last_line(CupidGbModel model, bool fast, uint8_t lyc) {
    const uint8_t program[] = {0};
    CupidGb *gb = prepare(model, fast, program, sizeof(program), 0, lyc, 1, 0);
    for (unsigned dot = 0; dot < 70000; ++dot) {
        if (cupid_gb_read_u8(gb, 0xff44) == 153)
            return gb;
        cupid_gb_tick_ppu_dots(gb, 1);
    }
    assert(!"LY never reached the last scanline");
    return gb;
}

static void test_color_last_line_stat_writes(void) {
    /* Each bit is one dot after the first readable LY=153. The visible flag
     * and the retained interrupt input have independent measured intervals. */
    static const uint32_t visible[3][3] = {
        {0xfffc00u, 0x000000u, 0x000030u},
        {0xfffc00u, 0x00000fu, 0x0003f0u},
        {0xfffc00u, 0x000003u, 0x0003fcu},
    };
    static const uint32_t latched[3][3] = {
        {0xfffc00u, 0x00000fu, 0x0003f0u},
        {0xfffc00u, 0x00000fu, 0x0003f0u},
        {0xfffc00u, 0x000003u, 0x0003fcu},
    };
    static const uint8_t lycs[] = {0, 152, 153};
    static const uint8_t before_masks[] = {0, 0x40, 0x40, 0x10, 0x50, 0};
    static const uint8_t after_masks[] = {0x40, 0x40, 0, 0x50, 0x40, 0x10};
    CupidGb *written = malloc(sizeof(*written));
    assert(written);
    for (unsigned model = 0; model < sizeof(color_revisions) / sizeof(color_revisions[0]);
         ++model) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            const unsigned group = model < 4 ? speed : 2;
            for (unsigned value = 0; value < sizeof(lycs); ++value) {
                CupidGb *gb =
                    prepare_color_last_line(color_revisions[model], speed != 0, lycs[value]);
                for (unsigned phase = 0; phase < 24; ++phase) {
                    const bool flag = ((visible[group][value] >> phase) & 1u) != 0;
                    const bool input = ((latched[group][value] >> phase) & 1u) != 0;
                    for (unsigned route = 0; route < sizeof(before_masks); ++route) {
                        /* These probes only write inline LCD/IF registers. The
                         * baseline retains ownership of every borrowed input. */
                        *written = *gb;
                        cupid_gb_write_u8(written, 0xff41, before_masks[route]);
                        cupid_gb_write_u8(written, 0xff0f, 0);
                        assert(((cupid_gb_read_u8(written, 0xff41) & 4u) != 0) == flag);
                        const bool previous = (before_masks[route] & 0x10u) != 0 ||
                                              (input && (before_masks[route] & 0x40u) != 0);
                        const bool next = (after_masks[route] & 0x10u) != 0 ||
                                          (input && (after_masks[route] & 0x40u) != 0);
                        cupid_gb_write_u8(written, 0xff41, after_masks[route]);
                        assert(((cupid_gb_read_u8(written, 0xff41) & 4u) != 0) == flag);
                        assert(((cupid_gb_read_u8(written, 0xff0f) & 2u) != 0) ==
                               (next && !previous));
                    }
                    cupid_gb_tick_ppu_dots(gb, 1);
                }
                destroy(gb);
            }
        }
    }
    free(written);
}

static void test_color_last_line_lyc_writes(void) {
    static const uint8_t values[] = {0, 152, 153, 200};
    static const uint8_t phases[] = {10, 21, 22};
    CupidGb *written = malloc(sizeof(*written));
    assert(written);
    for (unsigned model = 0; model < sizeof(color_revisions) / sizeof(color_revisions[0]);
         ++model) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            for (unsigned old = 0; old < sizeof(values); ++old) {
                CupidGb *gb =
                    prepare_color_last_line(color_revisions[model], speed != 0, values[old]);
                unsigned elapsed = 0;
                for (unsigned phase = 0; phase < sizeof(phases); ++phase) {
                    cupid_gb_tick_ppu_dots(gb, (uint16_t)(phases[phase] - elapsed));
                    elapsed = phases[phase];
                    for (unsigned overlap = 0; overlap < 2; ++overlap) {
                        for (unsigned next = 0; next < sizeof(values); ++next) {
                            *written = *gb;
                            cupid_gb_write_u8(written, 0xff41, overlap ? 0x50 : 0x40);
                            cupid_gb_write_u8(written, 0xff0f, 0);
                            cupid_gb_write_u8(written, 0xff45, values[next]);
                            const bool flag = values[next] == 0;
                            /* During the twelve-dot conflict window, the old
                             * LYC=153 can produce an edge before the new byte. */
                            const bool conflict = phases[phase] < 22;
                            const bool edge =
                                conflict ? values[old] == 153 || flag : flag && values[old] != 0;
                            assert(((cupid_gb_read_u8(written, 0xff41) & 4u) != 0) == flag);
                            assert(((cupid_gb_read_u8(written, 0xff0f) & 2u) != 0) ==
                                   (edge && !overlap));
                        }
                    }
                }
                destroy(gb);
            }
        }
    }
    free(written);
}

static void test_color_lyc_conflict_restore(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_E};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            CupidGb *gb = prepare_color_last_line(models[model], speed != 0, 153);
            cupid_gb_tick_ppu_dots(gb, 21);
            cupid_gb_write_u8(gb, 0xff41, 0x40);
            cupid_gb_write_u8(gb, 0xff0f, 0);
            const size_t size = cupid_gb_state_size(gb);
            uint8_t *before = malloc(size), *after = malloc(size), *restored = malloc(size);
            size_t written = 0;
            assert(before && after && restored);
            assert(cupid_gb_save_state(gb, before, size, &written) == CUPID_GB_STATE_OK);
            assert(written == size);
            cupid_gb_write_u8(gb, 0xff45, 152);
            assert(cupid_gb_read_u8(gb, 0xff0f) & 2u);
            cupid_gb_tick_ppu_dots(gb, 10);
            assert(cupid_gb_save_state(gb, after, size, &written) == CUPID_GB_STATE_OK);
            assert(cupid_gb_load_state(gb, before, size) == CUPID_GB_STATE_OK);
            cupid_gb_write_u8(gb, 0xff45, 152);
            assert(cupid_gb_read_u8(gb, 0xff0f) & 2u);
            cupid_gb_tick_ppu_dots(gb, 10);
            assert(cupid_gb_save_state(gb, restored, size, &written) == CUPID_GB_STATE_OK);
            assert(!memcmp(after, restored, size));
            free(restored);
            free(after);
            free(before);
            destroy(gb);
        }
    }
}

static void test_early_color_lyc_then_stat(void) {
    for (unsigned model = 0; model < 4; ++model) {
        for (unsigned phase = 1; phase < 4; ++phase) {
            CupidGb *gb = prepare_color_last_line(color_revisions[model], false, 200);
            cupid_gb_tick_ppu_dots(gb, (uint16_t)phase);
            cupid_gb_write_u8(gb, 0xff45, 153);
            assert(cupid_gb_read_u8(gb, 0xff41) & 4u);
            cupid_gb_write_u8(gb, 0xff0f, 0);
            /* STAT resamples the visible bit while the comparator is invalid.
             * The match from the LYC write continues to feed the IRQ input. */
            cupid_gb_write_u8(gb, 0xff41, 0x40);
            assert(!(cupid_gb_read_u8(gb, 0xff41) & 4u));
            assert(cupid_gb_read_u8(gb, 0xff0f) & 2u);
            destroy(gb);
        }
    }
}

static void test_color_vblank_comparison_latches(void) {
    const uint8_t program[] = {0};
    for (unsigned model = 0; model < sizeof(color_revisions) / sizeof(color_revisions[0]);
         ++model) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            for (unsigned line = 144; line <= 153; ++line) {
                CupidGb *gb = prepare(color_revisions[model], speed != 0, program, sizeof(program),
                                      0x40, (uint8_t)(line - 1u), 1, 0);
                for (unsigned row = 1; row < line - 1u; ++row)
                    cupid_gb_tick_ppu_dots(gb, 456);
                const bool retained = speed || model >= 4;
                cupid_gb_tick_ppu_dots(gb, 452);
                assert(cupid_gb_read_u8(gb, 0xff44) == line - 1u);
                assert(cupid_gb_read_u8(gb, 0xff41) & 4u);
                assert(gb->pixels.lyc_irq);
                cupid_gb_tick_ppu_dots(gb, 1);
                assert(cupid_gb_read_u8(gb, 0xff44) == line - 1u);
                assert(((cupid_gb_read_u8(gb, 0xff41) & 4u) != 0u) == retained);
                assert(gb->pixels.lyc_irq);
                cupid_gb_tick_ppu_dots(gb, 3);
                assert(gb->pixels.line == line && gb->pixels.dot == 0);
                if (line == 153) {
                    destroy(gb);
                    continue;
                }
                const unsigned mode = line == 144 ? 0 : 1;
                assert((cupid_gb_read_u8(gb, 0xff41) & 7u) == (mode | (retained ? 4u : 0u)));
                assert(gb->pixels.lyc_irq);
                gb->interrupt_flags = 0;
                check_snapshot_continuation(gb);
                cupid_gb_tick_ppu_dots(gb, 1);
                assert(!gb->pixels.lyc_irq);
                assert(!(cupid_gb_read_u8(gb, 0xff41) & 4u));
                assert(!(gb->interrupt_flags & 2u));
                destroy(gb);
            }
        }
    }
}

int main(void) {
    test_color_vblank_comparison_latches();
    test_early_color_lyc_then_stat();
    test_color_last_line_stat_writes();
    test_color_last_line_lyc_writes();
    test_color_lyc_conflict_restore();
    test_ly_ignores_all_host_values();
    test_ly_ignores_cpu_stores();
    test_cpu_store_routes();
    test_interrupt_matching_coincidence();
    test_stack_and_word_stores();
    test_color_first_oam_write_edge();
    test_stat_during_delayed_speed_switch();
    test_invalid_comparison_latches();
    test_double_speed_ly_half_edge();
    puts("STAT CPU route and interrupt boundary checks passed.");
    return 0;
}

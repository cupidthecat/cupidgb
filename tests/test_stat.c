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
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
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
    unsigned edge = (model == CUPID_GB_MODEL_CGB ? 452u : 453u) + (line - 1u) * 456u;
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
    uint32_t sample_phase = gb->apu.sample_acc;
    assert(cupid_gb_step(gb));
    assert(gb->cpu.pc == 0x102 && gb->double_speed);
    /* DIV clocks only two audio dots across this six-dot LCD interval. */
    assert(gb->apu.sample_acc - sample_phase == 2u * cupid_gb_apu_get_sample_rate(gb));
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
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB, CUPID_GB_MODEL_SGB,
                                   CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned speed = 0; speed <= (models[m] == CUPID_GB_MODEL_CGB); ++speed) {
            CupidGb *gb = prepare(models[m], speed != 0, program, sizeof(program), 0x40, 0, 1, 0);
            assert(gb->pixels.line == 1 && gb->pixels.dot == 0);
            assert(cupid_gb_read_u8(gb, 0xff44) == 1);
            const uint8_t boundary_stat = models[m] == CUPID_GB_MODEL_CGB ? 4 : 0;
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

int main(void) {
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

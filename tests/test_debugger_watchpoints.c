/**
 * @file    test_debugger_watchpoints.c
 * @brief   Scoped debugger memory watchpoint and guest bus observation regressions.
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

#include "cupid/gb/debugger.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cupid/gb/ppu.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"

static CupidGb *machine(CupidGbModel model) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x736d38332d776174));
    uint8_t rom[32768] = {0};
    /* LD HL,$C000; LD A,$5A; LD (HL),A; LD A,(HL); JR -2. */
    const uint8_t program[] = {0x21, 0x00, 0xc0, 0x3e, 0x5a, 0x77, 0x7e, 0x18, 0xfe};
    memcpy(rom + 0x100, program, sizeof(program));
    memcpy(rom + 0x134, "WATCHPOINT TEST", 15);
    if (model == CUPID_GB_MODEL_CGB)
        rom[0x143] = 0x80;
    for (unsigned offset = 0x134; offset < 0x14d; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->cpu.pc = 0x100;
    gb->cpu.sp = 0xdff0;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    cupid_gb_write_u8(gb, 0xff40u, 0);
    gb->work_ram[0] = 0x11;
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static CupidGbDebugStop watched_step(CupidGbDebugger *debugger, CupidGb *gb) {
    cupid_gb_debugger_watch_begin(debugger, gb);
    assert(cupid_gb_step(gb));
    return cupid_gb_debugger_watch_end(debugger, gb);
}

static void compare_hardware(const CupidGb *first, const CupidGb *second) {
    const size_t size = cupid_gb_state_size(first);
    assert(size && size == cupid_gb_state_size(second));
    uint8_t *a = malloc(size);
    uint8_t *b = malloc(size);
    assert(a && b);
    assert(cupid_gb_save_state(first, a, size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_save_state(second, b, size, NULL) == CUPID_GB_STATE_OK);
    assert(!memcmp(a, b, size));
    free(b);
    free(a);
}

static void test_cpu_access_and_conditions(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_DMG_ABC);
    CupidGbDebugger debugger;
    cupid_gb_debugger_init(&debugger);
    assert(!cupid_gb_debugger_watchpoint_add(&debugger, gb, -2, 0xc000, 0xc000,
                                             CUPID_GB_DEBUG_ACCESS_WRITE, NULL));
    assert(!cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc001, 0xc000,
                                             CUPID_GB_DEBUG_ACCESS_WRITE, NULL));
    assert(!cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000, 0, NULL));
    assert(!cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000,
                                             CUPID_GB_DEBUG_ACCESS_WRITE, "new = 5"));
    assert(debugger.wp_count == 0);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000,
                                            CUPID_GB_DEBUG_ACCESS_WRITE,
                                            "(old == $11) && (new == $5a) && (a == $5a)"));
    assert(debugger.wp_count == 1 && debugger.watchpoints[0].id == 1);
    assert(watched_step(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    assert(gb->cpu.pc == 0x103);
    assert(watched_step(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    assert(gb->cpu.pc == 0x105);
    assert(watched_step(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(gb->cpu.pc == 0x106 && gb->work_ram[0] == 0x5a);
    assert(debugger.last_watch_hit.id == 1 && debugger.last_watch_hit.address == 0xc000);
    assert(debugger.last_watch_hit.access == CUPID_GB_DEBUG_ACCESS_WRITE);
    assert(debugger.last_watch_hit.old_value == 0x11 && debugger.last_watch_hit.new_value == 0x5a);
    assert(gb->memory_access_callback == NULL);
    cupid_gb_debugger_reset_execution(&debugger);
    assert(debugger.wp_count == 1 && debugger.last_watch_hit.id == 1);
    cupid_gb_debugger_watchpoints_clear(&debugger);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000,
                                            CUPID_GB_DEBUG_ACCESS_READ, "[hl] == $5a"));
    assert(watched_step(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.id == 1 && debugger.last_watch_hit.address == 0xc000);
    assert(debugger.last_watch_hit.access == CUPID_GB_DEBUG_ACCESS_READ);
    assert(debugger.last_watch_hit.old_value == 0 && debugger.last_watch_hit.new_value == 0);
    assert(gb->cpu.pc == 0x107 && gb->cpu.a == 0x5a);
    cupid_gb_debugger_watchpoints_clear(&debugger);
    gb->cpu.pc = 0x100;
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0x100, 0x102,
                                            CUPID_GB_DEBUG_ACCESS_READ, NULL));
    assert(watched_step(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.address == 0x100 && debugger.last_watch_hit.id == 1);
    assert(gb->cpu.pc == 0x103);
    cupid_gb_debugger_cleanup(&debugger);
    destroy(gb);
}

static void test_access_banks_ranges_and_ids(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_CGB);
    CupidGbDebugger debugger;
    cupid_gb_debugger_init(&debugger);
    gb->cgb_mode = true;
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, 2, 0xd000, 0xd001,
                                            CUPID_GB_DEBUG_ACCESS_WRITE, NULL));
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, 2, 0xd000, 0xd001,
                                            CUPID_GB_DEBUG_ACCESS_WRITE, NULL));
    assert(debugger.wp_count == 2 && debugger.watchpoints[0].id == 1 &&
           debugger.watchpoints[1].id == 2);
    assert(cupid_gb_debugger_watchpoint_remove(&debugger, 1));
    assert(!cupid_gb_debugger_watchpoint_remove(&debugger, 1));
    assert(debugger.wp_count == 1 && debugger.watchpoints[0].id == 2);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, 2, 0xd000, 0xd001,
                                            CUPID_GB_DEBUG_ACCESS_WRITE, NULL));
    assert(debugger.watchpoints[1].id == 3);
    assert(cupid_gb_debugger_watchpoint_remove(&debugger, 3));
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, 2, 0xd000, 0xd001,
                                            CUPID_GB_DEBUG_ACCESS_WRITE, NULL));
    assert(debugger.watchpoints[1].id == 3);
    assert(cupid_gb_debugger_watchpoint_remove(&debugger, 3));
    gb->cgb_wram_bank = 1;
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xd000, 0x31);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    gb->cgb_wram_bank = 2;
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xd002, 0x32);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xd001, 0x33);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.bank == 2 && debugger.last_watch_hit.id == 2 &&
           debugger.last_watch_hit.address == 0xd001);
    debugger.watchpoints[0].enabled = false;
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xd000, 0x34);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    cupid_gb_debugger_watchpoints_clear(&debugger);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xfea0, 0xfea0,
                                            CUPID_GB_DEBUG_ACCESS_RW, NULL));
    assert(debugger.watchpoints[0].id == 1);
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xfea0, 0x55);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.address == 0xfea0 && debugger.last_watch_hit.new_value == 0x55);
    cupid_gb_debugger_cleanup(&debugger);
    destroy(gb);
}

typedef struct AccessObserver {
    unsigned reads;
    unsigned writes;
} AccessObserver;

static void count_access(void *user_data, CupidGb *gb, uint16_t address, bool writing,
                         uint8_t value) {
    AccessObserver *observer = user_data;
    (void)gb;
    (void)address;
    (void)value;
    if (writing)
        ++observer->writes;
    else
        ++observer->reads;
}

static void test_observer_lifecycle_and_passive_inspection(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_DMG_ABC);
    CupidGbDebugger debugger;
    cupid_gb_debugger_init(&debugger);
    AccessObserver observer = {0};
    cupid_gb_set_memory_access_callback(gb, count_access, &observer);
    cupid_gb_debugger_watch_begin(&debugger, gb);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    assert(gb->memory_access_callback == count_access && gb->memory_access_user_data == &observer);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000,
                                            CUPID_GB_DEBUG_ACCESS_READ, NULL));
    cupid_gb_debugger_watch_begin(&debugger, gb);
    assert(cupid_gb_debug_peek(gb, 0xc000) == 0x11);
    assert(cupid_gb_debug_poke(gb, 0xff04, 0));
    assert(observer.reads == 0 && observer.writes == 0);
    assert(cupid_gb_debugger_watch_end(&debugger, NULL) == CUPID_GB_DEBUG_STOP_NONE);
    assert(gb->memory_access_callback == count_access && gb->memory_access_user_data == &observer);
    cupid_gb_debugger_watch_begin(&debugger, gb);
    assert(cupid_gb_read_bus_u8(gb, 0xc000) == 0x11);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(observer.reads == 1 && observer.writes == 0);
    assert(gb->memory_access_callback == count_access && gb->memory_access_user_data == &observer);
    cupid_gb_debugger_watchpoints_clear(&debugger);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000,
                                            CUPID_GB_DEBUG_ACCESS_WRITE,
                                            "(old + old == $22) && (new == $5a)"));
    observer.reads = observer.writes = 0;
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xc000, 0x5a);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(observer.reads == 2 && observer.writes == 1);
    assert(debugger.last_watch_hit.old_value == 0x11);
    const size_t state_size = cupid_gb_state_size(gb);
    uint8_t *before = malloc(state_size), *during = malloc(state_size);
    assert(state_size && before && during);
    assert(cupid_gb_save_state(gb, before, state_size, NULL) == CUPID_GB_STATE_OK);
    cupid_gb_debugger_watch_begin(&debugger, gb);
    assert(cupid_gb_save_state(gb, during, state_size, NULL) == CUPID_GB_STATE_OK);
    assert(!memcmp(before, during, state_size));
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    free(during);
    free(before);
    assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
    assert(gb->memory_access_callback == count_access && gb->memory_access_user_data == &observer);
    assert(debugger.wp_count == 1);
    cupid_gb_debugger_watch_begin(&debugger, gb);
    assert(gb->memory_access_callback != count_access);
    cupid_gb_debugger_cleanup(&debugger);
    assert(gb->memory_access_callback == count_access && gb->memory_access_user_data == &observer);
    destroy(gb);
}

static void test_dma_source_and_original_address(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_DMG_ABC);
    CupidGbDebugger debugger;
    cupid_gb_debugger_init(&debugger);
    gb->dma_active = true;
    gb->dma_start_delay = 0;
    gb->dma_index = 0;
    gb->dma_phase = 0;
    gb->dma_source_base = 0xc000;
    gb->io_registers[CUPID_GB_IO_DMA] = 0xc0;
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000,
                                            CUPID_GB_DEBUG_ACCESS_READ, NULL));
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_tick_dma(gb);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.address == 0xc000 &&
           debugger.last_watch_hit.access == CUPID_GB_DEBUG_ACCESS_READ);
    assert(gb->dma_index == 1 && gb->object_attribute_memory[0] == 0x11);

    /* ROM and VRAM sources must also produce read events. The VRAM source
     * uses the video port, unlike the WRAM and ROM paths above and below. */
    cupid_gb_debugger_watchpoints_clear(&debugger);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0x100, 0x100,
                                            CUPID_GB_DEBUG_ACCESS_READ, NULL));
    gb->dma_index = 0;
    gb->dma_phase = 0;
    gb->dma_source_base = 0x100;
    gb->io_registers[CUPID_GB_IO_DMA] = 0x01;
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_tick_dma(gb);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.address == 0x100 && gb->dma_index == 1);

    cupid_gb_debugger_watchpoints_clear(&debugger);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0x8000, 0x8000,
                                            CUPID_GB_DEBUG_ACCESS_READ, NULL));
    gb->video_ram[0] = 0x77;
    gb->dma_index = 0;
    gb->dma_phase = 0;
    gb->dma_source_base = 0x8000;
    gb->io_registers[CUPID_GB_IO_DMA] = 0x80;
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_tick_dma(gb);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.address == 0x8000 && gb->dma_index == 1);

    /* Contended CPU writes must remain indexed by their original address.
     * On monochrome OAM DMA this attempted WRAM write corrupts OAM instead. */
    gb->dma_index = 1;
    gb->dma_phase = 0;
    gb->dma_source_base = 0xc000;
    gb->io_registers[CUPID_GB_IO_DMA] = 0xc0;

    cupid_gb_debugger_watchpoints_clear(&debugger);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc123, 0xc123,
                                            CUPID_GB_DEBUG_ACCESS_WRITE, NULL));
    gb->work_ram[0x123] = 0x22;
    const uint8_t old_oam = gb->object_attribute_memory[0];
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xc123, 0xaa);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.address == 0xc123 &&
           debugger.last_watch_hit.access == CUPID_GB_DEBUG_ACCESS_WRITE);
    assert(gb->work_ram[0x123] == 0x22 &&
           gb->object_attribute_memory[0] == (uint8_t)(old_oam & 0xaau));
    cupid_gb_debugger_watchpoints_clear(&debugger);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000,
                                            CUPID_GB_DEBUG_ACCESS_WRITE, NULL));
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xc123, 0xa9);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    cupid_gb_debugger_cleanup(&debugger);
    destroy(gb);
}

static void test_bus_old_and_bad_conditions(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_CGB);
    CupidGbDebugger debugger;
    cupid_gb_debugger_init(&debugger);
    gb->cgb_mode = true;
    gb->cgb_vram_bank = 1;
    gb->video_ram[0x2000] = 0x22;
    gb->io_registers[0x40] = 0x80;
    gb->io_registers[0x41] = 3;
    assert(cupid_gb_debug_peek(gb, 0x8000) == 0x22);
    assert(cupid_gb_read_bus_u8(gb, 0x8000) == 0xff);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, 1, 0x8000, 0x8000,
                                            CUPID_GB_DEBUG_ACCESS_WRITE,
                                            "(old == $ff) && (new == $66)"));
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0x8000, 0x66);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(debugger.last_watch_hit.old_value == 0xff && debugger.last_watch_hit.new_value == 0x66);
    assert(gb->video_ram[0x2000] == 0x22);
    cupid_gb_debugger_watchpoints_clear(&debugger);
    assert(cupid_gb_debugger_load_symbols(&debugger, "00:C000 Watched", 15, NULL));
    assert(cupid_gb_debugger_watchpoint_add(&debugger, gb, -1, 0xc000, 0xc000,
                                            CUPID_GB_DEBUG_ACCESS_WRITE, "Watched == $c000"));
    cupid_gb_debugger_symbols_clear(&debugger);
    cupid_gb_debugger_watch_begin(&debugger, gb);
    cupid_gb_write_u8(gb, 0xc000, 0x55);
    assert(cupid_gb_debugger_watch_end(&debugger, gb) == CUPID_GB_DEBUG_STOP_ERROR);
    assert(debugger.last_watch_hit.address == 0xc000);
    cupid_gb_debugger_cleanup(&debugger);
    destroy(gb);
}

static void test_observed_core_matches_unobserved(void) {
    CupidGb *watched = machine(CUPID_GB_MODEL_DMG_ABC);
    CupidGb *baseline = machine(CUPID_GB_MODEL_DMG_ABC);
    CupidGbDebugger debugger;
    cupid_gb_debugger_init(&debugger);
    assert(cupid_gb_debugger_watchpoint_add(&debugger, watched, -1, 0x100, 0x100,
                                            CUPID_GB_DEBUG_ACCESS_READ, NULL));
    assert(watched_step(&debugger, watched) == CUPID_GB_DEBUG_STOP_WATCHPOINT);
    assert(cupid_gb_step(baseline));
    compare_hardware(watched, baseline);
    cupid_gb_debugger_cleanup(&debugger);
    destroy(baseline);
    destroy(watched);
}

int main(void) {
    test_cpu_access_and_conditions();
    test_access_banks_ranges_and_ids();
    test_observer_lifecycle_and_passive_inspection();
    test_dma_source_and_original_address();
    test_bus_old_and_bad_conditions();
    test_observed_core_matches_unobserved();
    puts("Debugger memory watchpoints passed.");
    return 0;
}

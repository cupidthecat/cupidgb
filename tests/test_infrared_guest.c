/**
 * @file    test_infrared_guest.c
 * @brief   Instruction-level infrared threshold and display-clock timing regressions.
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

#include "cupid/gb/gb.h"
#include "cupid/gb/infrared.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Profile {
    CupidGbModel model;
    uint8_t cartridge;
    unsigned history;
    bool fast;
} Profile;

typedef struct Action {
    uint16_t address;
    uint8_t opcode, value;
} Action;

typedef struct History {
    int16_t credit[6];
    uint16_t low_decay[5];
    uint8_t detected[2];
} History;

static const Profile profiles[] = {{CUPID_GB_MODEL_DMG_ABC, 0xff, 0, false},
                                   {CUPID_GB_MODEL_DMG_ABC, 0xfe, 0, false},
                                   {CUPID_GB_MODEL_CGB, 0, 1, false},
                                   {CUPID_GB_MODEL_CGB, 0, 2, true}};

static const Action actions[] = {
    {0xff40, 0xea, 0x91}, {0xff40, 0xfa, 0}, {0x8000, 0xea, 0}, {0xc000, 0xea, 0}};

/* Each history starts at an instruction boundary, followed by one absolute
 * load/store and four NOPs. Detection masks cover the five completed instructions.
 * The monochrome LCDC write samples its old value before its leading clocks.
 * That read resolves pending display work even when the written latch is unchanged. */
static const History dmg_write_29 = {{64, 0, 0, 0, 0, 0}, {16, 24, 32, 40, 48}, {0x1f, 0}};
static const History dmg_sync_29 = {{64, 0, 0, 0, 0, 0}, {8, 16, 24, 32, 40}, {0x1f, 0}};
static const History dmg_ram_29 = {{64, 96, 104, 112, 120, 128}, {0, 0, 0, 8, 16}, {0x18, 0x07}};
static const History dmg_write_114 = {{6, -2, -2, -2, -2, -2}, {12, 20, 28, 36, 44}, {0x1f, 0}};
static const History dmg_sync_114 = {{6, -2, -2, -2, -2, -2}, {8, 16, 24, 32, 40}, {0x1f, 0}};
static const History dmg_ram_114 = {{6, 38, 46, 54, 62, 70}, {0, 0, 0, 8, 16}, {0x18, 0x07}};
static const History cgb_sync_29 = {{66, 0, 0, 0, 0, 0}, {8, 16, 24, 32, 40}, {0x1f, 0}};
static const History cgb_ram_29 = {{66, 98, 106, 114, 122, 130}, {0, 0, 0, 8, 16}, {0x18, 0x07}};
static const History cgb_sync_114 = {{8, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0x1f}};
static const History cgb_ram_114 = {{8, 40, 48, 56, 64, 72}, {0, 0, 0, 8, 16}, {0x18, 0x07}};
static const History fast_29 = {{-36, -20, -16, -12, -8, -4}, {0, 0, 0, 0, 0}, {0, 0x1f}};
static const History fast_sync_114 = {{290, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0x1f}};
static const History fast_ram_114 = {
    {290, 306, 310, 314, 318, 322}, {8, 12, 16, 20, 24}, {0x1f, 0}};

static const History *const histories[3][2][4] = {
    {{&dmg_write_29, &dmg_sync_29, &dmg_sync_29, &dmg_ram_29},
     {&dmg_write_114, &dmg_sync_114, &dmg_sync_114, &dmg_ram_114}},
    {{&cgb_sync_29, &cgb_sync_29, &cgb_sync_29, &cgb_ram_29},
     {&cgb_sync_114, &cgb_sync_114, &cgb_sync_114, &cgb_ram_114}},
    {{&fast_29, &fast_29, &fast_29, &fast_29},
     {&fast_sync_114, &fast_sync_114, &fast_sync_114, &fast_ram_114}}};

static int64_t fixture_time(void *data) {
    (void)data;
    return INT64_C(1800000000);
}

static CupidGb *create_machine(const Profile *profile, unsigned warmup) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, profile->model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x49522d494e535452));
    cupid_gb_set_rtc_time_source(gb, fixture_time, NULL);
    uint8_t rom[32768] = {0};
    memcpy(rom + 0x134, "IR GUEST", 8);
    rom[0x143] = profile->model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x147] = profile->cartridge;
    rom[0x149] = 3;
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[index] - 1u);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    assert(!gb->elapsed_cycles && !gb->display_cycles_since_frame);
    memset(gb->work_ram, 0, sizeof(gb->work_ram));
    memset(gb->video_ram, 0, sizeof(gb->video_ram));
    memset(gb->object_attribute_memory, 0, sizeof(gb->object_attribute_memory));
    memset(gb->high_ram, 0, sizeof(gb->high_ram));
    memset(gb->cgb_bg_palette_ram, 0xff, sizeof(gb->cgb_bg_palette_ram));
    memset(gb->cgb_obj_palette_ram, 0xff, sizeof(gb->cgb_obj_palette_ram));
    gb->io_registers[0x46] = 0x3c;
    cupid_gb_write_u8(gb, 0xff00, 0x30);
    cupid_gb_write_u8(gb, 0xffff, 0);
    cupid_gb_write_u8(gb, 0xff0f, 0);
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff42, 0);
    cupid_gb_write_u8(gb, 0xff43, 0);
    cupid_gb_write_u8(gb, 0xff4a, 0xff);
    cupid_gb_write_u8(gb, 0xff4b, 7);
    cupid_gb_write_u8(gb, 0xff47, 0xe4);
    cupid_gb_write_u8(gb, 0xff48, 0xe4);
    cupid_gb_write_u8(gb, 0xff49, 0xe4);
    cupid_gb_write_u8(gb, 0xff56, 0xc0);
    if (profile->cartridge)
        cupid_gb_write_u8(gb, 0, 0x0e);
    cupid_gb_set_infrared_input(gb, true);
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    cupid_gb_write_u8(gb, 0xff04, 0);
    gb->double_speed = profile->fast;
    for (unsigned cycle = 0; cycle < warmup; ++cycle)
        cupid_gb_tick(gb, 1);
    return gb;
}

static void prepare_instruction(CupidGb *gb, const Action *action, unsigned threshold) {
    gb->huc3.ir_warmup = threshold ? 34048 : 33000;
    gb->huc3.ir_decay = threshold ? 24000 : 0;
    gb->huc3.ir_detected = false;
    memset(gb->rom + 0x100, 0, 24);
    gb->rom[0x100] = action->opcode;
    gb->rom[0x101] = (uint8_t)action->address;
    gb->rom[0x102] = (uint8_t)(action->address >> 8);
    gb->cpu.pc = 0x100;
    gb->cpu.sp = 0xdff0;
    gb->cpu.a = action->value;
    gb->cpu.f = 0;
    gb->cpu.b = 0x12;
    gb->cpu.c = 0x34;
    gb->cpu.d = 0x56;
    gb->cpu.e = 0x78;
    gb->cpu.h = (uint8_t)(action->address >> 8);
    gb->cpu.l = (uint8_t)action->address;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
}

static bool observe(const CupidGb *gb, const Profile *profile, const Action *action,
                    const History *history, unsigned warmup, unsigned threshold, unsigned step) {
    const unsigned instruction_cycles = step ? 16u + (step - 1u) * 4u : 0;
    const unsigned receiver_clocks = instruction_cycles * (profile->fast ? 1u : 2u);
    const unsigned divider = ((unsigned)gb->io_registers[4] << 8) | ((gb->div_counter & 63u) << 2) |
                             gb->divider_subcycle;
    const unsigned expected_divider = warmup * 4u + instruction_cycles;
    const uint64_t expected_clocks = (uint64_t)expected_divider * (profile->fast ? 1u : 2u);
    const unsigned expected_warm = threshold ? 34048 : 33000 + receiver_clocks;
    const unsigned expected_decay = threshold ? 24000 + receiver_clocks
                                    : step    ? history->low_decay[step - 1u]
                                              : 0;
    const bool expected_detected = step && (history->detected[threshold] & (1u << (step - 1u)));
    const unsigned expected_pc = step ? 0x102 + step : 0x100;
    const unsigned expected_a = step && action->opcode == 0xfa ? 0x91 : action->value;
    const bool cpu_matches = gb->cpu.pc == expected_pc && gb->cpu.sp == 0xdff0 &&
                             gb->cpu.a == expected_a && !gb->cpu.f && gb->cpu.b == 0x12 &&
                             gb->cpu.c == 0x34 && gb->cpu.d == 0x56 && gb->cpu.e == 0x78 &&
                             gb->cpu.h == (uint8_t)(action->address >> 8) &&
                             gb->cpu.l == (uint8_t)action->address && !gb->cpu.ime &&
                             !gb->cpu.halted && !gb->cpu.stopped;
    bool ports_match =
        cupid_gb_read_u8(gb, 0xff56) ==
        (profile->model == CUPID_GB_MODEL_CGB ? (expected_detected ? 0xec : 0xee) : 0xff);
    if (profile->cartridge)
        ports_match &= cupid_gb_read_u8(gb, 0xa000) == (profile->cartridge == 0xff
                                                            ? (expected_detected ? 0xc1 : 0xc0)
                                                            : (expected_detected ? 1 : 0));
    const bool matches =
        cpu_matches && ports_match && divider == expected_divider &&
        gb->elapsed_cycles * 2u == expected_clocks && gb->huc3.ir_warmup == expected_warm &&
        gb->huc3.ir_decay == expected_decay && gb->huc3.ir_detected == expected_detected &&
        gb->display_clock.credit_half_dots == history->credit[step];
    if (!matches)
        fprintf(stderr,
                "IR guest model=%u cart=%02x fast=%u offset=%u opcode=%02x address=%04x "
                "threshold=%u step=%u: clocks=%llu/%llu warm=%u/%u decay=%u/%u "
                "detected=%u/%u phase=%d/%d cpu=%u ports=%u DIV=%u/%u\n",
                (unsigned)profile->model, profile->cartridge, profile->fast, warmup, action->opcode,
                action->address, threshold, step, (unsigned long long)(gb->elapsed_cycles * 2u),
                (unsigned long long)expected_clocks, gb->huc3.ir_warmup, expected_warm,
                gb->huc3.ir_decay, expected_decay, gb->huc3.ir_detected, expected_detected,
                gb->display_clock.credit_half_dots, history->credit[step], cpu_matches, ports_match,
                divider, expected_divider);
    return matches;
}

int main(void) {
    static const unsigned offsets[] = {29, 114};
    unsigned cases = 0, observations = 0, failed_cases = 0;
    for (unsigned profile = 0; profile < sizeof(profiles) / sizeof(*profiles); ++profile) {
        const Profile *spec = &profiles[profile];
        for (unsigned offset = 0; offset < sizeof(offsets) / sizeof(*offsets); ++offset) {
            for (unsigned action = 0; action < sizeof(actions) / sizeof(*actions); ++action) {
                const History *history = histories[spec->history][offset][action];
                for (unsigned threshold = 0; threshold < 2; ++threshold) {
                    CupidGb *gb = create_machine(spec, offsets[offset]);
                    prepare_instruction(gb, &actions[action], threshold);
                    bool matches = true;
                    for (unsigned step = 0; step < 6; ++step) {
                        if (step)
                            assert(cupid_gb_step(gb));
                        /* Passive reads keep observation from resolving pending display work. */
                        matches &= observe(gb, spec, &actions[action], history, offsets[offset],
                                           threshold, step);
                        ++observations;
                    }
                    ++cases;
                    failed_cases += !matches;
                    cupid_gb_cleanup(gb);
                    free(gb);
                }
            }
        }
    }
    printf("IR instruction checks: %u cases, %u observations, %u failed cases\n", cases,
           observations, failed_cases);
    return failed_cases ? EXIT_FAILURE : EXIT_SUCCESS;
}

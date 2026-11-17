/**
 * @file    test_sgb_startup.c
 * @brief   Adapter startup holds, presentation order, reset policy, and pure redraw tests.
 * @author  cupidthecat
 * @date    2026-11-17
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

#include "../src/gb/display_clock.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/debugger.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/sgb_startup.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Presentation {
    unsigned count;
    int initial_phase;
    bool held;
} Presentation;

static CupidGb *machine(CupidGbModel model, bool enhanced, bool firmware) {
    uint8_t rom[0x8000] = {0};
    memcpy(rom + 0x134, "CUPID HOST", 10);
    rom[0x146] = enhanced ? 3 : 0;
    rom[0x14b] = enhanced ? 0x33 : 0;
    rom[0x147] = 0x10; /* MBC3 timer, SRAM and battery. */
    rom[0x149] = 2;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    if (firmware) {
        /* A fresh synthetic firmware image starts with NOP. It carries no logo. */
        gb->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900u : 0x100u;
        memset(gb->boot_rom, 0, sizeof(gb->boot_rom));
        cupid_gb_start_boot_rom(gb);
    }
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void presentation(const CupidGb *gb, uint8_t type, void *user) {
    Presentation *seen = user;
    ++seen->count;
    const int expected = seen->initial_phase + (int)seen->count;
    assert(gb->sgb_startup.phase == (expected < CUPID_GB_SGB_STARTUP_COMPLETE_PHASE
                                         ? expected
                                         : CUPID_GB_SGB_STARTUP_COMPLETE_PHASE));
    assert(gb->frame_ready);
    if (seen->held) {
        assert(type == CUPID_GB_FRAME_LCD_OFF);
        assert(gb->elapsed_cycles == 0 && gb->cpu.pc == 0);
        assert(gb->sgb_startup.elapsed_cycles == (uint64_t)seen->count * CUPID_SGB_FRAME_DOTS);
    }
    const CupidGbAudioPending pending = cupid_gb_apu_pending(gb);
    assert(pending.startup_frames > 0);
    const CupidGbSgbStartupAudio *output = &gb->sgb_startup.output;
    assert(output->batches[output->batch_count - 1u].generation_phase == expected - 1);
}

static uint64_t drain(CupidGb *gb, bool silence) {
    int16_t samples[254];
    uint64_t frames = 0;
    uint32_t count;
    while ((count = cupid_gb_apu_drain(gb, samples, 127)) != 0) {
        frames += count;
        for (uint32_t index = 0; index < count * 2u; index += 2u) {
            assert(samples[index] == samples[index + 1u]);
            if (silence)
                assert(samples[index] == 0);
        }
    }
    return frames;
}

static void require_held_hardware(const CupidGb *gb, const CupidGb *before) {
    assert(memcmp(&gb->cpu, &before->cpu, sizeof(gb->cpu)) == 0);
    assert(gb->elapsed_cycles == before->elapsed_cycles);
    assert(gb->div_counter == before->div_counter &&
           gb->divider_subcycle == before->divider_subcycle);
    assert(gb->io_registers[0x05] == before->io_registers[0x05]);
    assert(gb->serial_master_clock == before->serial_master_clock &&
           gb->serial_fast_clock == before->serial_fast_clock);
    assert(gb->io_registers[0x01] == before->io_registers[0x01] &&
           gb->io_registers[0x02] == before->io_registers[0x02]);
    assert(gb->dma_active == before->dma_active && gb->dma_start_delay == before->dma_start_delay &&
           gb->dma_phase == before->dma_phase && gb->dma_index == before->dma_index);
    assert(gb->rtc_cycles == before->rtc_cycles);
    assert(memcmp(&gb->rtc, &before->rtc, sizeof(gb->rtc)) == 0);
    assert(memcmp(&gb->apu, &before->apu, offsetof(CupidGbApu, output)) == 0);
    assert(memcmp(&gb->power_on, &before->power_on, sizeof(gb->power_on)) == 0);
    assert(gb->joyp_select_delay == before->joyp_select_delay &&
           gb->joyp_sample_pending == before->joyp_sample_pending);
    assert(memcmp(gb->work_ram, before->work_ram, sizeof(gb->work_ram)) == 0);
    assert(memcmp(gb->cartridge_ram, before->cartridge_ram, sizeof(gb->cartridge_ram)) == 0);
}

static void test_hold(CupidGbModel model, bool enhanced, unsigned reset) {
    CupidGb *gb = machine(model, enhanced, true);
    if (reset) {
        gb->sgb_startup.phase = 123;
        gb->work_ram[0x123] = 0x59;
        assert(cupid_gb_reset(gb, reset == 1 ? CUPID_GB_RESET_HARD : CUPID_GB_RESET_QUICK));
        if (reset == 2)
            assert(gb->work_ram[0x123] == 0x59);
    }
    assert(cupid_gb_sgb_host_present(gb));
    assert(gb->sgb.enabled == enhanced);
    assert(gb->sgb_startup.phase == -10 && gb->sgb_startup.elapsed_cycles == 0);
    gb->io_registers[0x05] = 0x9a;
    gb->io_registers[0x07] = 5;
    gb->io_registers[0x01] = 0x53;
    gb->io_registers[0x02] = 0x81;
    gb->dma_active = true;
    gb->dma_start_delay = 3;
    gb->dma_phase = 2;
    gb->dma_index = 17;
    gb->rtc_cycles = 1234;
    gb->joyp_select_delay = 3;
    gb->joyp_sample_pending = true;
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    *before = *gb;
    Presentation seen = {.initial_phase = -10, .held = true};
    cupid_gb_set_presentation_callback(gb, presentation, &seen);
    uint64_t frames = 0;
    unsigned calls = 0;
    while (cupid_gb_sgb_startup_held(gb) && calls < 66000u) {
        CupidGbStepTrace trace;
        const unsigned prior = seen.count;
        assert(cupid_gb_step_observed(gb, &trace));
        assert(trace.event == CUPID_GB_STEP_IDLE && trace.address == 0);
        ++calls;
        frames += drain(gb, seen.count <= 10u);
        if (seen.count != prior) {
            assert(seen.count == prior + 1u);
            require_held_hardware(gb, before);
        }
    }
    assert(calls == 65296u && seen.count == 106u);
    assert(!cupid_gb_sgb_startup_held(gb) && cupid_gb_sgb_startup_active(gb));
    assert(gb->sgb_startup.phase == 96);
    assert(gb->sgb_startup.elapsed_cycles == UINT64_C(7443744));
    assert(cupid_gb_presentation_cycles(gb) == UINT64_C(7443744));
    assert(frames == 106u * cupid_gb_sgb_startup_audio_frame_count(cupid_gb_apu_get_sample_rate(gb),
                                                                   cupid_gb_base_clock_hz(gb)));
    assert(gb->sgb_startup.output.dropped_frames == 0);
    require_held_hardware(gb, before);
    assert(cupid_gb_display_clock_state_valid(gb));
    cupid_gb_set_presentation_callback(gb, NULL, NULL);
    gb->dma_active = false;
    gb->dma_start_delay = 0;
    CupidGbStepTrace trace;
    assert(cupid_gb_step_observed(gb, &trace));
    assert(trace.event == CUPID_GB_STEP_INSTRUCTION && gb->cpu.pc == 1);
    assert(gb->elapsed_cycles == 4);
    assert(cupid_gb_presentation_cycles(gb) == UINT64_C(7443748));
    free(before);
    release(gb);
}

static void test_model_and_reentry(void) {
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb *gb = machine((CupidGbModel)model, false, false);
        assert(!cupid_gb_sgb_startup_held(gb) && !cupid_gb_sgb_startup_active(gb));
        assert(gb->sgb_startup.phase == 200 && gb->sgb_startup.output.noise_state == 1);
        assert(cupid_gb_presentation_cycles(gb) == gb->elapsed_cycles);
        gb->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900u : 0x100u;
        gb->display_cycles_since_frame = 12345;
        gb->sgb_startup.output.dropped_frames = 4;
        const uint64_t seed = gb->power_on.generation_seed;
        gb->work_ram[0x234] = 0x67;
        cupid_gb_enter_boot_rom(gb);
        assert(gb->work_ram[0x234] == 0x67 && gb->power_on.generation_seed == seed);
        assert(gb->sgb_startup.elapsed_cycles == 0 && gb->sgb_startup.output.dropped_frames == 0 &&
               gb->sgb_startup.output.batch_count == 0);
        if (model == CUPID_GB_MODEL_SGB || model == CUPID_GB_MODEL_SGB2) {
            assert(cupid_gb_sgb_startup_held(gb) && gb->sgb_startup.phase == -10);
            assert(gb->display_cycles_since_frame == 0);
        } else {
            assert(!cupid_gb_sgb_host_present(gb) && !cupid_gb_sgb_startup_active(gb));
            assert(cupid_gb_step(gb) && gb->elapsed_cycles == 4);
        }
        release(gb);
    }
}

static void test_presentation_and_redraw(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_SGB2, false, true);
    CupidGb *before = malloc(sizeof(*before));
    const size_t count = CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT;
    uint32_t *image = calloc(count, sizeof(*image));
    uint32_t *repeated = calloc(count, sizeof(*repeated));
    assert(before && image && repeated);
    gb->sgb_startup.phase = 80;
    *before = *gb;
    cupid_gb_sgb_render_argb(gb, image, count);
    for (unsigned pass = 0; pass < 8; ++pass) {
        cupid_gb_sgb_render_argb(gb, repeated, count);
        assert(memcmp(image, repeated, count * sizeof(*image)) == 0);
        assert(memcmp(before, gb, sizeof(*gb)) == 0);
    }
    bool colored = false;
    for (unsigned y = 40; y < 184; ++y)
        for (unsigned x = 48; x < 208; ++x)
            colored |= image[y * 256u + x] != UINT32_C(0xff000000);
    assert(colored);

    gb->sgb_startup.phase = 199;
    gb->display_cycles_since_frame = CUPID_SGB_FRAME_DOTS - 4u;
    gb->sgb.transfer.frame_dots = 91; /* Independent retained age is valid. */
    Presentation seen = {.initial_phase = 199};
    cupid_gb_set_presentation_callback(gb, presentation, &seen);
    assert(cupid_gb_step(gb));
    assert(seen.count == 1 && gb->sgb_startup.phase == 200);
    assert(!cupid_gb_sgb_startup_active(gb));
    assert(gb->sgb.transfer.frame_dots == 95);
    assert(gb->sgb_startup.output.batches[0].generation_phase == 199);
    assert(drain(gb, false) > 0);
    free(before);
    free(image);
    free(repeated);
    release(gb);
}

static void test_debugger_hold(void) {
    CupidGb *gb = machine(CUPID_GB_MODEL_SGB, false, true);
    CupidGbDebugger debugger;
    cupid_gb_debugger_init(&debugger);
    assert(cupid_gb_debugger_breakpoint_add(&debugger, -1, 0));
    gb->sgb_startup.phase = 95;
    gb->display_cycles_since_frame = CUPID_SGB_FRAME_DOTS - CUPID_GB_SGB_STARTUP_STEP_DOTS;
    cupid_gb_debugger_begin(&debugger, gb, CUPID_GB_DEBUG_RUN);
    assert(cupid_gb_debugger_run(&debugger, gb, 1) == CUPID_GB_DEBUG_STOP_BUDGET);
    assert(debugger.steps == 0 && gb->cpu.pc == 0 && gb->sgb_startup.phase == 96);
    assert(cupid_gb_debugger_step(&debugger, gb) == CUPID_GB_DEBUG_STOP_BREAKPOINT);
    assert(debugger.steps == 0);
    (void)drain(gb, false);
    cupid_gb_enter_boot_rom(gb);
    gb->sgb_startup.phase = 95;
    gb->display_cycles_since_frame = CUPID_SGB_FRAME_DOTS - CUPID_GB_SGB_STARTUP_STEP_DOTS;
    cupid_gb_debugger_begin(&debugger, gb, CUPID_GB_DEBUG_STEP);
    assert(debugger.skip_breakpoint);
    assert(cupid_gb_debugger_step(&debugger, gb) == CUPID_GB_DEBUG_STOP_NONE);
    assert(debugger.skip_breakpoint && debugger.steps == 0 && gb->cpu.pc == 0);
    (void)drain(gb, false);
    assert(cupid_gb_debugger_step(&debugger, gb) == CUPID_GB_DEBUG_STOP_STEP);
    assert(!debugger.skip_breakpoint && debugger.steps == 1 && gb->cpu.pc == 1);
    cupid_gb_debugger_cleanup(&debugger);
    release(gb);
}

int main(void) {
    for (unsigned model = CUPID_GB_MODEL_SGB; model <= CUPID_GB_MODEL_SGB2; ++model)
        for (unsigned enhanced = 0; enhanced < 2; ++enhanced)
            for (unsigned reset = 0; reset < 3; ++reset)
                test_hold((CupidGbModel)model, enhanced != 0, reset);
    test_model_and_reentry();
    test_presentation_and_redraw();
    test_debugger_hold();
    puts("Adapter startup timing, reset, presentation and debugger checks passed.");
    return 0;
}

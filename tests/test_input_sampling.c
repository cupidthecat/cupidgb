/**
 * @file    test_input_sampling.c
 * @brief   Sampled JOYP, native reset, and transactional input-history regressions.
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

#include "../src/gb/display_clock.h"
#include "../src/gb/printer_internal.h"
#include "../src/gb/vram_bus.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Host {
    unsigned clock_calls, images, completed;
} Host;

typedef struct Rig {
    CupidGb *gb;
    CupidGbPrinter *printer;
    CupidGbStateContext context;
    Host host;
} Rig;

typedef struct Blob {
    uint8_t *bytes;
    size_t size;
} Blob;

enum {
    HEADER_BYTES = 128,
    DESCRIPTOR_BYTES = 8,
    INPUT_SAMPLE_BYTES = 1,
    V15_TIMING_BYTES = CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
                       CUPID_GB_VRAM_BUS_STATE_SIZE
};

static unsigned reset_cases, continuation_cases, legacy_cases, rejected_cases;

static int64_t fixture_time(void *data) {
    ++((Host *)data)->clock_calls;
    return INT64_C(1800000000);
}

static void image_ready(const CupidGbPrinterImage *image, void *data) {
    assert(image);
    ++((Host *)data)->images;
}

static void print_done(void *data) {
    ++((Host *)data)->completed;
}

static Rig *create_rig(CupidGbModel model, bool firmware, bool printer) {
    Rig *rig = calloc(1, sizeof(*rig));
    assert(rig);
    rig->gb = calloc(1, sizeof(*rig->gb));
    assert(rig->gb);
    CupidGb *gb = rig->gb;
    uint8_t rom[0x8000] = {0};
    memcpy(rom + 0x134, "INPUT SAMPLE", 12);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x146] = 3;
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[index] - 1u);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x4a4f59502d535441));
    cupid_gb_set_rtc_time_source(gb, fixture_time, &rig->host);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    if (firmware) {
        gb->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100;
        memset(gb->boot_rom, 0, sizeof(gb->boot_rom));
        cupid_gb_start_boot_rom(gb);
    }
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    cupid_gb_write_u8(gb, 0xff0f, 0);
    if (printer) {
        rig->printer = cupid_gb_printer_create(image_ready, print_done, &rig->host);
        assert(rig->printer);
        cupid_gb_set_serial_link(gb, cupid_gb_printer_link(rig->printer));
    }
    assert(cupid_gb_state_context_init(&rig->context, gb) == CUPID_GB_STATE_OK);
    return rig;
}

static void destroy_rig(Rig *rig) {
    cupid_gb_set_serial_link(rig->gb, (CupidGbSerialLink){0});
    cupid_gb_printer_destroy(rig->printer);
    cupid_gb_cleanup(rig->gb);
    free(rig->gb);
    free(rig);
}

static unsigned instruction(CupidGb *gb, bool read) {
    gb->high_ram[0] = read ? 0xf0 : 0x00;
    gb->high_ram[1] = 0;
    gb->cpu.pc = 0xff80;
    const uint64_t before = gb->elapsed_cycles;
    const unsigned dots = (read ? 12u : 4u) / (gb->double_speed ? 2u : 1u);
    assert(cupid_gb_step(gb));
    assert(gb->elapsed_cycles - before == dots);
    assert(gb->cpu.pc == (read ? 0xff82 : 0xff81));
    return dots;
}

static uint8_t irq(const CupidGb *gb) {
    return cupid_gb_read_u8(gb, 0xff0f) & 0x10u;
}

static Blob save_machine(const CupidGb *gb) {
    Blob blob = {NULL, cupid_gb_state_size(gb)};
    blob.bytes = malloc(blob.size);
    assert(blob.bytes);
    assert(cupid_gb_save_state(gb, blob.bytes, blob.size, NULL) == CUPID_GB_STATE_OK);
    assert(blob.bytes[8] == CUPID_GB_STATE_VERSION);
    return blob;
}

static CupidGbStateResult restore(Rig *rig, const Blob *blob, unsigned api) {
    switch (api) {
        case 0:
            return cupid_gb_load_state(rig->gb, blob->bytes, blob->size);
        case 1:
            return cupid_gb_load_state_with_context(rig->gb, blob->bytes, blob->size,
                                                    &rig->context);
        case 2:
            return cupid_gb_load_state_live(rig->gb, blob->bytes, blob->size);
        default:
            assert(api == 3);
            return cupid_gb_load_state_live_with_context(rig->gb, blob->bytes, blob->size,
                                                         &rig->context);
    }
}

static void resign(Blob *blob) {
    for (unsigned index = 0; index < 4; ++index)
        blob->bytes[12 + index] = (uint8_t)(blob->size >> (index * 8));
    cupid_sha256_parts(blob->bytes, 96, blob->bytes + HEADER_BYTES, blob->size - HEADER_BYTES,
                       blob->bytes + 96);
}

static size_t pending_offset(const Rig *rig, const Blob *blob) {
    return blob->size - DESCRIPTOR_BYTES - INPUT_SAMPLE_BYTES -
           (rig->printer ? PRINTER_STATE_WIRE_SIZE : 0);
}

static void host_updates_wait_for_delivery(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB, CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model) {
        Rig *rig = create_rig(models[model], false, false);
        CupidGb *gb = rig->gb;
        cupid_gb_set_buttons(gb, 0xef);
        assert(gb->joyp_sample_pending && cupid_gb_read_u8(gb, 0xff00) == 0xcf && !irq(gb));
        assert(cupid_gb_read_bus_u8(gb, 0xff00) == 0xcf && gb->joyp_sample_pending);
        cupid_gb_tick(gb, 0);
        cupid_gb_input_tick(gb, 0);
        cupid_gb_input_tick_half_dots(gb, 0);
        assert(gb->joyp_sample_pending && !irq(gb));
        (void)instruction(gb, true);
        assert(gb->cpu.a == 0xce && !gb->joyp_sample_pending && irq(gb) == 0x10);
        cupid_gb_write_u8(gb, 0xff0f, 0);
        cupid_gb_set_buttons(gb, 0xef);
        assert(gb->joyp_sample_pending && cupid_gb_read_u8(gb, 0xff00) == 0xce && !irq(gb));
        (void)instruction(gb, false);
        assert(!gb->joyp_sample_pending && !irq(gb));
        cupid_gb_set_buttons(gb, 0xff);
        assert(cupid_gb_read_u8(gb, 0xff00) == 0xce && gb->joyp_sample_pending);
        (void)instruction(gb, true);
        assert(gb->cpu.a == 0xcf && !irq(gb));
        /* An unsampled press followed by release must never create a falling edge. */
        cupid_gb_set_buttons(gb, 0xef);
        cupid_gb_set_buttons(gb, 0xff);
        (void)instruction(gb, false);
        assert(cupid_gb_read_u8(gb, 0xff00) == 0xcf && !irq(gb));
        destroy_rig(rig);
    }
}

static void reset_retains_host_sampling(void) {
    const uint8_t masks[] = {0xff, 0xef, 0xfe};
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < 2; ++model)
        for (unsigned firmware = 0; firmware < 2; ++firmware)
            for (unsigned kind = 0; kind < 2; ++kind)
                for (unsigned key = 0; key < 3; ++key)
                    for (unsigned pending = 0; pending < 2; ++pending) {
                        Rig *rig = create_rig(models[model], firmware != 0, false);
                        CupidGb *gb = rig->gb;
                        cupid_gb_set_buttons(gb, masks[key]);
                        if (!pending)
                            (void)instruction(gb, false);
                        assert(cupid_gb_reset(gb, (CupidGbResetKind)kind));
                        assert(gb->joypad == masks[key] &&
                               gb->joyp_sample_pending == (pending != 0));
                        assert(cupid_gb_read_u8(gb, 0xff00) == 0xcf && !irq(gb));
                        cupid_gb_write_u8(gb, 0xff00, 0);
                        assert(cupid_gb_read_u8(gb, 0xff00) == 0xcf);
                        assert(gb->joyp_sample_pending == (pending != 0));
                        (void)instruction(gb, true);
                        assert(gb->cpu.a == (pending && key ? 0xce : 0xcf));
                        assert(irq(gb) == (pending && key ? 0x10 : 0));
                        assert(!gb->joyp_sample_pending);
                        cupid_gb_write_u8(gb, 0xff0f, 0);
                        cupid_gb_set_buttons(gb, masks[key]);
                        (void)instruction(gb, false);
                        assert(cupid_gb_read_u8(gb, 0xff00) == (key ? 0xce : 0xcf));
                        assert(irq(gb) == (!pending && key ? 0x10 : 0));
                        ++reset_cases;
                        destroy_rig(rig);
                    }
    /* Immediate row sampling must not consume the host update before a reset. */
    for (unsigned kind = 0; kind < 2; ++kind) {
        Rig *rig = create_rig(CUPID_GB_MODEL_DMG_ABC, true, false);
        CupidGb *gb = rig->gb;
        cupid_gb_set_buttons(gb, 0xef);
        cupid_gb_write_u8(gb, 0xff00, 0x10);
        assert(gb->joyp_sample_pending && cupid_gb_read_u8(gb, 0xff00) == 0xce);
        assert(cupid_gb_reset(gb, (CupidGbResetKind)kind) && gb->joyp_sample_pending);
        assert(cupid_gb_read_u8(gb, 0xff00) == 0xcf && !irq(gb));
        (void)instruction(gb, true);
        assert(gb->cpu.a == 0xce && irq(gb) == 0x10 && !gb->joyp_sample_pending);
        destroy_rig(rig);
    }
}

static void passive_and_guest_rows(void) {
    Rig *rig = create_rig(CUPID_GB_MODEL_DMG_ABC, true, false);
    CupidGb *gb = rig->gb;
    cupid_gb_set_buttons(gb, 0xfe);
    (void)instruction(gb, false);
    cupid_gb_write_u8(gb, 0xff00, 0x10);
    assert(gb->joyp_select_delay == 24);
    assert(cupid_gb_read_u8(gb, 0xff00) == 0xce);
    assert(cupid_gb_read_bus_u8(gb, 0xff00) == 0xde);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, gb, sizeof(*before));
    for (unsigned repeat = 0; repeat < 4; ++repeat)
        assert(cupid_gb_read_u8(gb, 0xff00) == 0xce);
    assert(!memcmp(before, gb, sizeof(*before)));
    free(before);
    cupid_gb_tick(gb, 5);
    assert(gb->joyp_select_delay == 4 && cupid_gb_read_u8(gb, 0xff00) == 0xce);
    cupid_gb_tick(gb, 1);
    assert(!gb->joyp_select_delay && cupid_gb_read_u8(gb, 0xff00) == 0xdf);
    assert(cupid_gb_read_bus_u8(gb, 0xff00) == 0xdf);
    destroy_rig(rig);
}

static void partial_and_sleep_deliveries(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        Rig *rig = create_rig(CUPID_GB_MODEL_CGB, false, false);
        CupidGb *gb = rig->gb;
        gb->double_speed = fast != 0;
        cupid_gb_set_buttons(gb, 0xef);
        const uint64_t before = gb->elapsed_cycles;
        unsigned phase = cupid_gb_tick_color_dma_slot(gb, 0, gb->divider_subcycle);
        assert(gb->joyp_sample_pending && phase == gb->divider_subcycle);
        phase = cupid_gb_tick_color_dma_slot(gb, 1, phase);
        assert(phase == 1 && gb->elapsed_cycles == before + (fast ? 0u : 1u));
        assert(!gb->joyp_sample_pending && cupid_gb_read_u8(gb, 0xff00) == 0xce && irq(gb) == 0x10);
        cupid_gb_set_buttons(gb, 0xff);
        (void)cupid_gb_tick_color_dma_slot(gb, 1, phase);
        assert(!gb->joyp_sample_pending && cupid_gb_read_u8(gb, 0xff00) == 0xcf);
        destroy_rig(rig);
    }
    Rig *frozen = create_rig(CUPID_GB_MODEL_CGB, false, false);
    CupidGb *gb = frozen->gb;
    gb->double_speed = true;
    gb->speed_switch_freeze_cycles = 2;
    cupid_gb_set_buttons(gb, 0xef);
    unsigned phase = cupid_gb_tick_color_dma_slot(gb, 1, 0);
    phase = cupid_gb_tick_color_dma_slot(gb, 1, phase);
    assert(gb->joyp_sample_pending && cupid_gb_read_u8(gb, 0xff00) == 0xcf && !irq(gb));
    (void)cupid_gb_tick_color_dma_slot(gb, 1, phase);
    assert(!gb->joyp_sample_pending && cupid_gb_read_u8(gb, 0xff00) == 0xce && irq(gb) == 0x10);
    destroy_rig(frozen);
    for (unsigned color = 0; color < 2; ++color) {
        Rig *rig = create_rig(color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC, false, false);
        gb = rig->gb;
        gb->cpu.halted = true;
        gb->interrupt_enable = 0x10;
        cupid_gb_set_buttons(gb, 0xef);
        const uint8_t observed = cupid_gb_tick_halt(gb);
        assert(observed == (color ? 0 : 0x10));
        assert(!gb->joyp_sample_pending && irq(gb) == 0x10);
        gb->cpu.halted = false;
        gb->cpu.stopped = true;
        cupid_gb_set_buttons(gb, 0xff);
        cupid_gb_tick(gb, 1);
        assert(!gb->joyp_sample_pending && cupid_gb_read_u8(gb, 0xff00) == 0xcf);
        destroy_rig(rig);
    }
}

static void prepare_checkpoint(Rig *rig, unsigned scenario, bool fast) {
    CupidGb *gb = rig->gb;
    gb->double_speed = fast;
    cupid_gb_set_buttons(gb, 0xef);
    if (scenario == 1 || scenario == 2 || scenario == 3) {
        (void)instruction(gb, false);
        cupid_gb_write_u8(gb, 0xff0f, 0);
    }
    if (scenario == 1)
        cupid_gb_set_buttons(gb, 0xff);
    else if (scenario == 2)
        assert(cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
    else if (scenario == 3) {
        cupid_gb_write_u8(gb, 0xff00, 0x30);
        cupid_gb_set_buttons(gb, 0xff);
    } else if (scenario == 4) {
        (void)cupid_gb_tick_color_dma_slot(gb, 1, gb->divider_subcycle);
        cupid_gb_set_buttons(gb, 0xff);
    }
    cupid_gb_apu_run(gb, true);
}

static void compare_guest(Rig *expected, Rig *actual, bool live) {
    /* Both timelines were executed independently before capture. Normalize only
     * the listening stream for live loads and remove host attachment pointers. */
    cupid_gb_apu_run(expected->gb, true);
    cupid_gb_apu_run(actual->gb, true);
    CupidGb *left = malloc(sizeof(*left));
    CupidGb *right = malloc(sizeof(*right));
    assert(left && right);
    *left = *expected->gb;
    *right = *actual->gb;
    left->serial_link = right->serial_link = (CupidGbSerialLink){0};
    if (live)
        right->apu.output = left->apu.output;
    Blob a = save_machine(left), b = save_machine(right);
    assert(a.size == b.size && !memcmp(a.bytes, b.bytes, a.size));
    free(a.bytes);
    free(b.bytes);
    free(left);
    free(right);
}

static void native_continuation(void) {
    for (unsigned color = 0; color < 2; ++color)
        for (unsigned attached = 0; attached < 2; ++attached)
            for (unsigned scenario = 0; scenario < 5; ++scenario)
                for (unsigned api = 0; api < 4; ++api) {
                    const CupidGbModel model = color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
                    Rig *source = create_rig(model, false, attached != 0);
                    Rig *expected = create_rig(model, false, attached != 0);
                    Rig *target = create_rig(model, false, attached != 0);
                    prepare_checkpoint(source, scenario, color != 0);
                    prepare_checkpoint(expected, scenario, color != 0);
                    Blob blob = save_machine(source->gb);
                    assert(blob.bytes[pending_offset(source, &blob)] ==
                           source->gb->joyp_sample_pending);
                    cupid_gb_set_buttons(target->gb, 0xdf);
                    cupid_gb_tick(target->gb, 3);
                    assert(restore(target, &blob, api) == CUPID_GB_STATE_OK);
                    assert(target->gb->joyp_sample_pending == expected->gb->joyp_sample_pending);
                    if (api < 2) {
                        Blob roundtrip = save_machine(target->gb);
                        assert(roundtrip.size == blob.size &&
                               !memcmp(roundtrip.bytes, blob.bytes, blob.size));
                        free(roundtrip.bytes);
                    }
                    compare_guest(expected, target, api >= 2);
                    (void)instruction(expected->gb, true);
                    (void)instruction(target->gb, true);
                    compare_guest(expected, target, api >= 2);
                    cupid_gb_set_buttons(expected->gb, 0xef);
                    cupid_gb_set_buttons(target->gb, 0xef);
                    cupid_gb_write_u8(expected->gb, 0xff00, 0x10);
                    cupid_gb_write_u8(target->gb, 0xff00, 0x10);
                    cupid_gb_tick(expected->gb, 7);
                    cupid_gb_tick(target->gb, 7);
                    compare_guest(expected, target, api >= 2);
                    cupid_gb_set_buttons(expected->gb, 0xff);
                    cupid_gb_set_buttons(target->gb, 0xff);
                    (void)instruction(expected->gb, false);
                    (void)instruction(target->gb, false);
                    compare_guest(expected, target, api >= 2);
                    ++continuation_cases;
                    free(blob.bytes);
                    destroy_rig(target);
                    destroy_rig(expected);
                    destroy_rig(source);
                }
}

static Blob legacy_blob(const Rig *source, const Blob *current, unsigned version) {
    assert(version == 14 || version == 15);
    const size_t removed = INPUT_SAMPLE_BYTES + (version == 14 ? V15_TIMING_BYTES : 0);
    const size_t descriptor = pending_offset(source, current) + INPUT_SAMPLE_BYTES;
    Blob legacy = {malloc(current->size - removed), current->size - removed};
    assert(legacy.bytes);
    memcpy(legacy.bytes, current->bytes, descriptor - removed);
    memcpy(legacy.bytes + descriptor - removed, current->bytes + descriptor,
           current->size - descriptor);
    legacy.bytes[8] = (uint8_t)version;
    resign(&legacy);
    return legacy;
}

static void legacy_sample_projection(void) {
    for (unsigned adapter = 0; adapter < 2; ++adapter)
        for (unsigned version = 14; version <= 15; ++version)
            for (unsigned api = 0; api < 4; ++api) {
                const CupidGbModel model = adapter ? CUPID_GB_MODEL_SGB : CUPID_GB_MODEL_DMG_ABC;
                Rig *source = create_rig(model, false, false);
                Rig *target = create_rig(model, false, false);
                CupidGb *gb = source->gb;
                if (adapter) {
                    gb->sgb.player_count = 4;
                    gb->sgb.current_player = 2;
                    cupid_gb_write_u8(gb, 0xff00, 0x10);
                    assert(cupid_gb_set_buttons_for_player(gb, 2, 0xef));
                } else {
                    cupid_gb_set_buttons(gb, 0xef);
                }
                assert(gb->joyp_sample_pending && !irq(gb));
                Blob current = save_machine(gb);
                Blob legacy = legacy_blob(source, &current, version);
                cupid_gb_set_buttons(target->gb, 0);
                cupid_gb_tick(target->gb, 2);
                assert(restore(target, &legacy, api) == CUPID_GB_STATE_OK);
                assert(!target->gb->joyp_sample_pending && !irq(target->gb));
                assert(cupid_gb_read_u8(target->gb, 0xff00) == (adapter ? 0xde : 0xce));
                assert(target->gb->joypad == source->gb->joypad);
                assert(!memcmp(target->gb->joypad_extra, source->gb->joypad_extra, 3));
                (void)instruction(target->gb, true);
                assert(target->gb->cpu.a == (adapter ? 0xde : 0xce) && !irq(target->gb));
                ++legacy_cases;
                free(legacy.bytes);
                free(current.bytes);
                destroy_rig(target);
                destroy_rig(source);
            }
}

static void reject_unchanged(Rig *target, const Blob *bad) {
    CupidGb *before = malloc(sizeof(*before));
    CupidGbPrinter *printer = target->printer ? malloc(sizeof(*printer)) : NULL;
    assert(before && (!target->printer || printer));
    for (unsigned api = 0; api < 4; ++api) {
        memcpy(before, target->gb, sizeof(*before));
        if (printer)
            memcpy(printer, target->printer, sizeof(*printer));
        const CupidGbStateContext context = target->context;
        const Host host = target->host;
        assert(restore(target, bad, api) == CUPID_GB_STATE_INVALID);
        assert(!memcmp(before, target->gb, sizeof(*before)));
        assert(!memcmp(&context, &target->context, sizeof(context)));
        assert(!memcmp(&host, &target->host, sizeof(host)));
        if (printer)
            assert(!memcmp(printer, target->printer, sizeof(*printer)));
        ++rejected_cases;
    }
    free(printer);
    free(before);
}

static void malformed_pending_sample(void) {
    const uint8_t invalid[] = {2, 0x80, 0xff};
    for (unsigned attached = 0; attached < 2; ++attached) {
        Rig *source = create_rig(CUPID_GB_MODEL_DMG_ABC, false, attached != 0);
        Rig *target = create_rig(CUPID_GB_MODEL_DMG_ABC, false, attached != 0);
        cupid_gb_set_buttons(source->gb, 0xef);
        Blob valid = save_machine(source->gb);
        const size_t offset = pending_offset(source, &valid);
        for (unsigned index = 0; index <= sizeof(invalid); ++index) {
            Blob bad = {malloc(valid.size), valid.size};
            assert(bad.bytes);
            memcpy(bad.bytes, valid.bytes, valid.size);
            if (index < sizeof(invalid))
                bad.bytes[offset] = invalid[index];
            else {
                memmove(bad.bytes + offset, bad.bytes + offset + 1, bad.size - offset - 1);
                --bad.size;
            }
            resign(&bad);
            reject_unchanged(target, &bad);
            free(bad.bytes);
        }
        free(valid.bytes);
        destroy_rig(target);
        destroy_rig(source);
    }
}

int main(void) {
    host_updates_wait_for_delivery();
    reset_retains_host_sampling();
    passive_and_guest_rows();
    partial_and_sleep_deliveries();
    native_continuation();
    legacy_sample_projection();
    malformed_pending_sample();
    printf("Input samples passed: %u resets, %u continuations, %u legacy imports, %u rejections.\n",
           reset_cases, continuation_cases, legacy_cases, rejected_cases);
    return 0;
}

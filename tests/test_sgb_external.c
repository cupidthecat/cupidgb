/**
 * @file    test_sgb_external.c
 * @brief   External-host adapter model, input ownership, and hardware regressions.
 * @author  cupidthecat
 * @date    2026-11-24
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

#include "../src/gb/cartridge_internal.h"
#include "../src/gb/oam_bus.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/sgb.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct Host {
    uint8_t writes[16];
    unsigned count;
    bool drive;
} Host;

static const CupidGbModel models[] = {CUPID_GB_MODEL_SGB_EXTERNAL, CUPID_GB_MODEL_SGB_PAL_EXTERNAL,
                                      CUPID_GB_MODEL_SGB2_EXTERNAL};

static void make_rom(uint8_t *rom) {
    memset(rom, 0, 0x8000);
    const uint8_t code[] = {0x3e, 0x42, 0xe0, 0x88, 0x18, 0xfe};
    memcpy(rom + 0x100, code, sizeof(code));
    rom[0x146] = 3;
    rom[0x14b] = 0x33;
    for (unsigned offset = 0x134; offset < 0x14d; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
}

static CupidGb *machine(CupidGbModel model) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t *rom = malloc(0x8000);
    assert(gb && rom);
    make_rom(rom);
    gb->model = model;
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x1268beef));
    cupid_gb_init(gb);
    if (cupid_gb_model_is_sgb_external(model))
        assert(cupid_gb_read_u8(gb, 0xff00) == 0xcf);
    assert(cupid_gb_load_rom(gb, rom, 0x8000));
    free(rom);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void observe(CupidGb *gb, uint8_t value, void *context) {
    Host *host = context;
    assert(host->count < sizeof(host->writes));
    host->writes[host->count++] = value;
    if (host->drive)
        cupid_gb_set_external_joyp(gb, 0x0e);
}

static void write_p1(CupidGb *gb, uint8_t value) {
    cupid_gb_write_u8(gb, 0xff00, value);
}

static void send_multiplayer(CupidGb *gb) {
    const uint8_t packet[16] = {0x89, 3};
    write_p1(gb, 0);
    write_p1(gb, 0x30);
    for (unsigned byte = 0; byte < sizeof(packet); ++byte)
        for (unsigned bit = 0; bit < 8; ++bit) {
            write_p1(gb, packet[byte] & (1u << bit) ? 0x10 : 0x20);
            write_p1(gb, 0x30);
        }
    write_p1(gb, 0x20);
    write_p1(gb, 0x30);
}

static void test_input(CupidGbModel model) {
    CupidGb *gb = machine(model);
    assert(cupid_gb_read_u8(gb, 0xff00) == 0x3f);
    send_multiplayer(gb);
    assert(gb->sgb.player_count == 1 && !cupid_gb_sgb_commands_enabled(gb));
    const uint8_t select[] = {0, 0x10, 0x20, 0x30, 0x10, 0x30};
    for (unsigned index = 0; index < sizeof(select); ++index) {
        write_p1(gb, select[index]);
        assert(cupid_gb_read_u8(gb, 0xff00) == (select[index] | 0x0f));
        assert(cupid_gb_input_peek(gb) == cupid_gb_read_u8(gb, 0xff00));
        assert(gb->joyp_select_delay == 0);
    }
    gb->interrupt_flags = 0;
    cupid_gb_set_buttons(gb, 0);
    assert(cupid_gb_set_buttons_for_player(gb, 1, 0));
    cupid_gb_tick(gb, 8);
    assert(cupid_gb_read_u8(gb, 0xff00) == 0x3f && !(gb->interrupt_flags & 0x10));
    const uint8_t contacts[] = {15, 14, 14, 13};
    const bool irq[] = {false, true, false, true};
    for (unsigned index = 0; index < sizeof(contacts); ++index) {
        gb->interrupt_flags = 0;
        cupid_gb_set_external_joyp(gb, contacts[index]);
        assert(cupid_gb_read_u8(gb, 0xff00) == (0xf0u | contacts[index]));
        assert(((gb->interrupt_flags & 0x10) != 0) == irq[index]);
    }
    write_p1(gb, 0x10);
    assert(cupid_gb_read_u8(gb, 0xff00) == 0x1d);
    write_p1(gb, 0xd0);
    assert(cupid_gb_read_u8(gb, 0xff00) == 0x1d);
    cupid_gb_input_import_legacy(gb);
    assert(cupid_gb_read_u8(gb, 0xff00) == 0x1d);

    Host host = {0};
    cupid_gb_set_joyp_write_callback(gb, observe, &host);
    const uint8_t raw[] = {0, 0x10, 0x20, 0x30, 0x30, 0xff};
    for (unsigned index = 0; index < sizeof(raw); ++index)
        write_p1(gb, raw[index]);
    assert(host.count == sizeof(raw) && !memcmp(host.writes, raw, sizeof(raw)));
    assert(cupid_gb_read_u8(gb, 0xff00) == 0x1d);
    host.drive = true;
    write_p1(gb, 0x30);
    assert(cupid_gb_read_u8(gb, 0xff00) == 0xde);
    cupid_gb_set_joyp_write_callback(gb, NULL, &host);
    assert(!gb->joyp_write_callback && !gb->joyp_write_user_data);
    destroy(gb);
}

static void *fail_allocation(size_t size) {
    (void)size;
    return NULL;
}

static void test_lifecycle(CupidGbModel model) {
    CupidGb *gb = machine(model);
    Host host = {0};
    cupid_gb_set_joyp_write_callback(gb, observe, &host);
    cupid_gb_init(gb);
    assert(gb->joyp_write_callback == observe && gb->joyp_write_user_data == &host);
    uint8_t *rom = malloc(0x8000);
    CupidGb *before = malloc(sizeof(*before));
    CupidGb *candidate = calloc(1, sizeof(*candidate));
    assert(rom && before && candidate);
    make_rom(rom);
    assert(cupid_gb_load_rom(gb, rom, 0x8000));
    assert(host.count == 0 && gb->joyp_write_callback == observe);
    for (unsigned reset = CUPID_GB_RESET_HARD; reset <= CUPID_GB_RESET_QUICK; ++reset) {
        memcpy(before, gb, sizeof(*before));
        assert(!cupid_gb_reset_with_allocator(gb, (CupidGbResetKind)reset, fail_allocation));
        assert(!memcmp(gb, before, sizeof(*gb)) && host.count == 0);
        assert(cupid_gb_reset(gb, (CupidGbResetKind)reset));
        assert(gb->joyp_write_callback == observe && gb->joyp_write_user_data == &host);
        assert(!cupid_gb_sgb_startup_held(gb) && !gb->sgb_startup.elapsed_cycles);
    }
    cupid_gb_set_external_joyp(gb, 5);
    const uint8_t port = cupid_gb_read_u8(gb, 0xff00);
    assert(cupid_gb_prepare_cartridge_swap(candidate, gb, rom, 0x8000));
    assert(host.count == 0 && cupid_gb_read_u8(gb, 0xff00) == port);
    cupid_gb_commit_cartridge_swap(gb, candidate);
    assert(cupid_gb_read_u8(gb, 0xff00) == port);
    assert(gb->joyp_write_callback == observe && gb->joyp_write_user_data == &host);
    assert(host.count == 0);
    cupid_gb_cleanup(candidate);
    cupid_gb_cleanup(gb);
    assert(!gb->joyp_write_callback && !gb->joyp_write_user_data);
    free(candidate);
    free(before);
    free(rom);
    free(gb);
}

static void test_board_identity(CupidGbModel model) {
    const CupidGbModel base = cupid_gb_model_base(model);
    CupidGb *external = machine(model), *integrated = machine(base);
    assert(cupid_gb_model_is_valid(model) && cupid_gb_model_is_sgb(model));
    assert(cupid_gb_model_is_sgb_external(model) && !cupid_gb_model_is_cgb(model));
    assert(!cupid_gb_sgb_host_present(external) && cupid_gb_sgb_host_present(integrated));
    const uint32_t clock = base == CUPID_GB_MODEL_SGB       ? 4295454u
                           : base == CUPID_GB_MODEL_SGB_PAL ? 4256274u
                                                            : 4194304u;
    assert(cupid_gb_base_clock_hz(external) == clock);
    assert(!memcmp(&external->cpu, &integrated->cpu, sizeof(external->cpu)));
    assert(!memcmp(external->work_ram, integrated->work_ram, sizeof(external->work_ram)));
    assert(!memcmp(external->high_ram, integrated->high_ram, sizeof(external->high_ram)));
    assert(!memcmp(external->object_attribute_memory, integrated->object_attribute_memory,
                   sizeof(external->object_attribute_memory)));
    assert(cupid_gb_read_u8(external, 0xff26) == cupid_gb_read_u8(integrated, 0xff26));
    /* Every search row uses the same board-specific OAM corruption, independently
     * of whether the adapter's presentation and command processor are attached. */
    for (unsigned dot = 0; dot <= 80; ++dot) {
        for (unsigned index = 0; index < sizeof(external->object_attribute_memory); ++index)
            external->object_attribute_memory[index] = integrated->object_attribute_memory[index] =
                (uint8_t)(index * 73u + dot * 19u);
        external->pixels.initialized = integrated->pixels.initialized = true;
        external->pixels.line = integrated->pixels.line = 0;
        external->pixels.dot = integrated->pixels.dot = (uint16_t)dot;
        cupid_gb_trigger_oam_bug_read_access(external, 0xfe82);
        cupid_gb_trigger_oam_bug_read_access(integrated, 0xfe82);
        assert(!memcmp(external->object_attribute_memory, integrated->object_attribute_memory,
                       sizeof(external->object_attribute_memory)));
    }
    destroy(external);
    destroy(integrated);
}

static void test_startup_and_lcd(CupidGbModel model) {
    CupidGb *gb = machine(model);
    const bool external = cupid_gb_model_is_sgb_external(model);
    gb->boot_rom_size = 0x100;
    gb->boot_rom[0] = 0x18;
    gb->boot_rom[1] = 0xfe;
    cupid_gb_start_boot_rom(gb);
    assert(cupid_gb_sgb_startup_held(gb) != external);
    assert(cupid_gb_read_u8(gb, 0xff00) == 0xcf);
    cupid_gb_write_u8(gb, 0xff50, 1);
    gb->cpu.pc = 0x100;
    cupid_gb_write_u8(gb, 0xff88, 0);
    for (unsigned step = 0; step < 8; ++step)
        assert(cupid_gb_step(gb));
    assert(cupid_gb_read_u8(gb, 0xff88) == (external ? 0x42 : 0));
    if (external) {
        assert(gb->sgb_startup.phase == 200 && !gb->sgb_startup.elapsed_cycles);
        assert(!gb->sgb_startup.output.batch_count);
    }
    gb->frame_buffer[0] = 2;
    const bool parity = gb->lcd_frame_odd;
    cupid_gb_write_u8(gb, 0xff40, 0);
    assert(gb->lcd_frame_odd == parity);
    cupid_gb_tick_ppu_dots(gb, 35112);
    cupid_gb_tick_ppu_dots(gb, 35112);
    assert(gb->frame_buffer[0] == 2 && gb->lcd_frame_odd == parity);
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    assert(!gb->lcd_first_frame);
    destroy(gb);
}

int main(void) {
    for (unsigned index = 0; index < sizeof(models) / sizeof(models[0]); ++index) {
        test_input(models[index]);
        test_lifecycle(models[index]);
        test_board_identity(models[index]);
        test_startup_and_lcd(models[index]);
        test_startup_and_lcd(cupid_gb_model_base(models[index]));
    }
    CupidGb *gb = machine(CUPID_GB_MODEL_DMG_ABC);
    const uint8_t previous = cupid_gb_read_u8(gb, 0xff00);
    cupid_gb_set_external_joyp(gb, 0);
    assert(cupid_gb_read_u8(gb, 0xff00) == previous);
    cupid_gb_set_external_joyp(NULL, 0);
    cupid_gb_set_joyp_write_callback(NULL, NULL, NULL);
    destroy(gb);
    return 0;
}

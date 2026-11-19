/**
 * @file    test_reset.c
 * @brief   Reset boundaries, controller storage, firmware effects, and rejection preservation.
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

#include "cupid/gb/reset.h"

#include "../src/gb/audio_state.h"
#include "../src/gb/cartridge_internal.h"
#include "../src/gb/display_clock.h"
#include "../src/gb/printer_internal.h"
#include "../src/gb/vram_bus.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/infrared.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"
#include "test_state_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sampled JOYP, serial selection and the CH3 fetch byte precede the SRAM length. */
enum { IO_HISTORY_BYTES = 3, SRAM_LENGTH_BYTES = 4 };

typedef struct Cartridge {
    uint8_t type, ram;
} Cartridge;

typedef struct Host {
    unsigned clock_calls, serial_calls, rumble_calls, infrared_calls, image_calls, done_calls;
    int64_t now;
} Host;

enum {
    MAPPED_ROM_OFFSET = 249906,
    ACCESSORY_DESCRIPTOR_BYTES = 8,
    TIMING_BYTES = CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
                   CUPID_GB_VRAM_BUS_STATE_SIZE
};

static unsigned current_model, current_type, current_kind, current_firmware;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Reset model=%u type=%02x kind=%u firmware=%u line=%d: %s\n",          \
                    current_model, current_type, current_kind, current_firmware, __LINE__,         \
                    #condition);                                                                   \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static int64_t clock_time(void *context) {
    Host *host = context;
    ++host->clock_calls;
    return host->now;
}

static void serial_output(const CupidGb *gb, uint8_t byte, void *context) {
    (void)gb;
    (void)byte;
    ++((Host *)context)->serial_calls;
}

static void rumble_output(const CupidGb *gb, bool enabled, uint64_t cycles, void *context) {
    (void)gb;
    (void)enabled;
    (void)cycles;
    ++((Host *)context)->rumble_calls;
}

static void infrared_output(const CupidGb *gb, bool enabled, uint64_t cycles, void *context) {
    (void)gb;
    (void)enabled;
    (void)cycles;
    ++((Host *)context)->infrared_calls;
}

static void printer_image(const CupidGbPrinterImage *image, void *context) {
    (void)image;
    ++((Host *)context)->image_calls;
}

static void printer_done(void *context) {
    ++((Host *)context)->done_calls;
}

static void fill_pattern(uint8_t *bytes, size_t count, unsigned salt) {
    for (size_t index = 0; index < count; ++index)
        bytes[index] = (uint8_t)(index * 37u + salt);
}

static bool filled(const uint8_t *bytes, size_t count, uint8_t value) {
    for (size_t index = 0; index < count; ++index)
        if (bytes[index] != value)
            return false;
    return true;
}

static void install_firmware(CupidGb *gb) {
    static const uint16_t addresses[] = {0xc000, 0xff80, 0x8000, 0xfe00, 0xff47,
                                         0xff30, 0xff70, 0xd000, 0xff70};
    static const uint8_t values[] = {0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 2, 0x97, 1};
    static const uint8_t prefix[] = {0xf3, 0x31, 0xfe, 0xff};
    memset(gb->boot_rom, 0, sizeof(gb->boot_rom));
    memcpy(gb->boot_rom, prefix, sizeof(prefix));
    unsigned at = sizeof(prefix);
    const unsigned writes = gb->model == CUPID_GB_MODEL_CGB ? 9 : 6;
    for (unsigned index = 0; index < writes; ++index) {
        const uint8_t code[] = {0x3e, values[index], 0xea, (uint8_t)addresses[index],
                                (uint8_t)(addresses[index] >> 8)};
        memcpy(gb->boot_rom + at, code, sizeof(code));
        at += sizeof(code);
    }
    const uint8_t jump[] = {0xc3, 0xfc, 0};
    const uint8_t handoff[] = {0x3e, 1, 0xe0, 0x50};
    memcpy(gb->boot_rom + at, jump, sizeof(jump));
    memcpy(gb->boot_rom + 0xfc, handoff, sizeof(handoff));
    gb->boot_rom_size = gb->model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100;
}

static CupidGb *create_machine(CupidGbModel model, Cartridge cartridge, Host *host) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t *rom = calloc(1, 0x20000);
    const size_t rom_size =
        cartridge.type == 0 || cartridge.type == 8 || cartridge.type == 9 ? 0x8000u : 0x20000u;
    CHECK(gb && rom);
    for (unsigned bank = 0; bank < 8; ++bank)
        memset(rom + bank * 0x4000, (int)bank, 0x4000);
    memcpy(rom + 0x134, "CUPID RESET", 11);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x146] = 3;
    rom[0x147] = cartridge.type;
    rom[0x148] = rom_size == 0x8000u ? 0 : 2;
    rom[0x149] = cartridge.ram;
    rom[0x14b] = 0x33;
    if (cartridge.type == 0xbc) {
        rom[0x14a] = 0x65;
        rom[0x150] = 1;
        rom[0x151] = 0;
        rom[0x152] = 3;
        rom[0x153] = 0;
    }
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x5265736574436f72));
    cupid_gb_set_rtc_time_source(gb, clock_time, host);
    CHECK(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    CHECK(cupid_gb_load_rom(gb, rom, rom_size));
    free(rom);
    cupid_gb_set_serial_output_callback(gb, serial_output, host);
    cupid_gb_set_rumble_callback(gb, rumble_output, host);
    cupid_gb_set_infrared_callback(gb, infrared_output, host);
    return gb;
}

static void install_history(CupidGb *gb, unsigned rate) {
    CHECK(cupid_gb_apu_set_sample_rate(gb, rate));
    CHECK(cupid_gb_apu_set_filter_mode(gb, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM));
    CHECK(cupid_gb_apu_set_interference_volume(gb, 0.35));
    CHECK(cupid_gb_apu_set_channel_muted(gb, 1, true));
    cupid_gb_tick(gb, 129);
    fill_pattern(gb->work_ram, sizeof(gb->work_ram), 0x35);
    fill_pattern(gb->high_ram, sizeof(gb->high_ram), 0x46);
    fill_pattern(gb->object_attribute_memory, sizeof(gb->object_attribute_memory), 0x57);
    fill_pattern(gb->cgb_bg_palette_ram, sizeof(gb->cgb_bg_palette_ram), 0x68);
    fill_pattern(gb->cgb_obj_palette_ram, sizeof(gb->cgb_obj_palette_ram), 0x79);
    fill_pattern(gb->cartridge_ram, sizeof(gb->cartridge_ram), 0x8a);
    memset(gb->video_ram, 0xab, sizeof(gb->video_ram));
    gb->pixel_timing =
        (CupidGbPixelTiming){.logical_x = 37, .flags = CUPID_GB_PIXEL_FRACTIONAL, .fetch_y = 201};
    gb->vram_bus = (CupidGbVramBus){.map_address = 0x1823,
                                    .data_address = 0x2456,
                                    .read_value = 0xab,
                                    .fetch_map_address = 0x1834,
                                    .fetch_data_address = 0x3456};
    memset(gb->io_registers + 0x30, 0xcd, 16);
    gb->io_registers[0x46] = 0x12;
    gb->io_registers[0x48] = 0x34;
    gb->io_registers[0x49] = 0x56;
    gb->cpu.pc = 0x160;
    gb->cpu.sp = 0xcafe;
    gb->cpu.ime = true;
    gb->cpu.halted = true;
    gb->cpu.stopped = true;
    gb->cpu.halt_bug = true;
    gb->cpu.ime_delay = 1;
    gb->double_speed = gb->model == CUPID_GB_MODEL_CGB;
    gb->speed_switch_armed = true;
    gb->interrupt_enable = 0x1f;
    gb->interrupt_flags = 0x1f;
    gb->tima_overflow_delay = 1;
    gb->dma_active = true;
    gb->dma_index = 17;
    gb->serial_bits_remaining = 7;
    gb->hdma_active = true;
    gb->hdma_pending = true;
    gb->current_rom_bank = 3;
    gb->current_ram_bank = 2;
    gb->ram_enabled = true;
    gb->mbc1_bank_high2 = 3;
    gb->mbc1_bank_low5 = 7;
    gb->mbc1_ram_banking_mode = true;
    gb->mmm01_locked = true;
    gb->mmm01_rom_select = 0x73;
    gb->huc1_ir_mode = true;
    gb->huc3_mode = 0x0e;
    gb->huc3_value = 0x5a;
    gb->huc3 = (CupidGbHuc3){.rtc_base_time = 100,
                             .minutes = 75,
                             .days = 456,
                             .alarm_minutes = 111,
                             .alarm_days = 789,
                             .alarm_enabled = true,
                             .index = 12,
                             .flags = 2,
                             .ir_warmup = 33000,
                             .ir_decay = 500,
                             .ir_output = true,
                             .ir_detected = true};
    gb->huc3_ir_input = true;
    gb->rtc = (CupidGbRtc){3, 4, 5, 6, 0x80, 7, 8, 9, 10, 0x40, 1, 100};
    gb->rtc_cycles = 1234;
    gb->tpp1.rom_bank = 3;
    gb->tpp1.ram_bank = 2;
    gb->tpp1.mode = 3;
    gb->tpp1.status = 0x0d;
    gb->tpp1.motor_strength = 3;
    gb->tpp1.rtc_base_time = 100;
    fill_pattern(gb->tpp1.live, sizeof(gb->tpp1.live), 2);
    fill_pattern(gb->tpp1.latched, sizeof(gb->tpp1.latched), 3);
    memset(gb->camera.registers, 0x9a, sizeof(gb->camera.registers));
    gb->camera.mapped = true;
    gb->camera.busy_tcycles = 400;
    gb->sgb.current_player = 2;
    gb->sgb.player_count = 4;
    gb->sgb.mask_mode = 2;
    gb->mbc7.motion_x = 123;
    gb->mbc7.motion_y = -234;
    gb->joypad = 0xe7;
    gb->joypad_extra[1] = 0x7f;
}

static void check_boundary(const CupidGb *gb, const CupidGb *before, CupidGbResetKind kind,
                           bool firmware, Host *host, unsigned clock_calls) {
    const bool quick = kind == CUPID_GB_RESET_QUICK;
    CHECK(gb->rom == before->rom && gb->rom_size == before->rom_size &&
          gb->rom_capacity == before->rom_capacity &&
          gb->rom_generation == before->rom_generation && gb->loaded);
    CHECK(!memcmp(&gb->header, &before->header, sizeof(gb->header)));
    CHECK(gb->cpu.pc == (firmware ? 0 : 0x100));
    CHECK(!gb->cpu.ime && !gb->cpu.halted && !gb->cpu.stopped && !gb->cpu.halt_bug);
    CHECK(!gb->cpu.ime_delay && !gb->elapsed_cycles && !gb->double_speed &&
          !gb->speed_switch_armed);
    CHECK(!gb->dma_active && !gb->hdma_active && !gb->hdma_pending && !gb->serial_bits_remaining &&
          !gb->tima_overflow_delay);
    CHECK(filled(gb->video_ram, sizeof(gb->video_ram), 0));
    CHECK(!gb->huc3.minutes && !gb->huc3.days && !gb->huc3.alarm_minutes && !gb->huc3.alarm_days &&
          !gb->huc3.alarm_enabled && !gb->huc3.index && !gb->huc3.flags && !gb->huc3.ir_warmup &&
          !gb->huc3.ir_decay && !gb->huc3.ir_output && !gb->huc3.ir_detected && !gb->huc3_ir_input);
    CHECK(!memcmp(&gb->rtc, &before->rtc, 10) && !gb->rtc.latch_prev &&
          gb->rtc_cycles == before->rtc_cycles);
    CHECK(gb->tpp1.status == before->tpp1.status &&
          !memcmp(gb->tpp1.live, before->tpp1.live, sizeof(gb->tpp1.live)) &&
          !memcmp(gb->tpp1.latched, before->tpp1.latched, sizeof(gb->tpp1.latched)));
    CHECK(!gb->tpp1.rom_bank && !gb->tpp1.ram_bank && !gb->tpp1.mode && !gb->tpp1.motor_strength);
    CHECK(!gb->camera.mapped && !gb->camera.busy_tcycles &&
          filled(gb->camera.registers, sizeof(gb->camera.registers), 0));
    CHECK(!gb->huc1_ir_mode && !gb->huc3_mode && !gb->huc3_value);
    CHECK(!gb->mmm01_locked && gb->mmm01_ram_mask == 3 && !gb->mmm01_rom_select);
    CHECK(gb->sgb.player_count == 1 && !gb->sgb.current_player && !gb->sgb.mask_mode);
    CHECK(!memcmp(&gb->apu.output, &before->apu.output, sizeof(gb->apu.output)));
    CHECK(cupid_gb_audio_state_valid(gb));
    CHECK(cupid_gb_display_clock_state_valid(gb) && cupid_gb_pixels_timing_valid(gb) &&
          cupid_gb_vram_bus_state_valid(gb));
    CHECK(before->display_clock.frame_age_dots && !gb->display_clock.frame_age_dots &&
          !gb->display_clock.batch_half_dots && !gb->display_clock.fine_wait_half_dots);
    CHECK(!gb->pixel_timing.logical_x && !gb->pixel_timing.flags && !gb->pixel_timing.fetch_y &&
          !gb->vram_bus.map_address && !gb->vram_bus.data_address && !gb->vram_bus.read_value &&
          !gb->vram_bus.fetch_map_address && !gb->vram_bus.fetch_data_address);
    if (firmware)
        CHECK(gb->display_clock.phase == CUPID_GB_DISPLAY_CLOCK_RESET &&
              !gb->display_clock.credit_half_dots && !gb->display_clock.row &&
              !gb->display_clock.flags);
    else
        CHECK((gb->display_clock.flags & CUPID_GB_DISPLAY_CLOCK_FORCED_FINE) &&
              gb->display_clock.row == gb->io_registers[0x44]);
    CHECK(gb->serial_output_callback == serial_output && gb->serial_output_user_data == host &&
          gb->rumble_callback == rumble_output && gb->rumble_user_data == host &&
          gb->infrared_callback == infrared_output && gb->infrared_user_data == host);
    CHECK(gb->joypad == before->joypad &&
          !memcmp(gb->joypad_extra, before->joypad_extra, sizeof(gb->joypad_extra)) &&
          gb->mbc7.motion_x == 123 && gb->mbc7.motion_y == -234);
    CHECK(!host->serial_calls && !host->rumble_calls && !host->infrared_calls &&
          !host->image_calls && !host->done_calls);
    const bool clock = gb->header.mbc_type == CUPID_GB_MBC3 ||
                       gb->header.mbc_type == CUPID_GB_HUC3 || gb->header.mbc_type == CUPID_GB_TPP1;
    CHECK(host->clock_calls == clock_calls + (unsigned)clock);
    if (gb->header.mbc_type == CUPID_GB_MBC3)
        CHECK(gb->rtc.base_time == host->now);
    if (gb->header.mbc_type == CUPID_GB_HUC3)
        CHECK(gb->huc3.rtc_base_time == host->now);
    if (gb->header.mbc_type == CUPID_GB_TPP1)
        CHECK(gb->tpp1.rtc_base_time == host->now);
    if (quick || gb->header.has_battery)
        CHECK(!memcmp(gb->cartridge_ram, before->cartridge_ram, sizeof(gb->cartridge_ram)));
    else
        CHECK(filled(cupid_gb_cartridge_ram_const(gb), gb->cartridge_ram_size, 0xff));
    if (quick) {
        CHECK(!memcmp(gb->high_ram, before->high_ram, sizeof(gb->high_ram)));
        CHECK(!memcmp(gb->cgb_bg_palette_ram, before->cgb_bg_palette_ram,
                      sizeof(gb->cgb_bg_palette_ram)) &&
              !memcmp(gb->cgb_obj_palette_ram, before->cgb_obj_palette_ram,
                      sizeof(gb->cgb_obj_palette_ram)));
        CHECK(gb->io_registers[0x46] == 0x12 && gb->io_registers[0x48] == 0x34 &&
              gb->io_registers[0x49] == 0x56);
        if (!firmware && gb->model == CUPID_GB_MODEL_CGB) {
            CHECK(!memcmp(gb->work_ram, before->work_ram, 0x2000) &&
                  filled(gb->work_ram + 0x2000, 0x1000, 0) &&
                  !memcmp(gb->work_ram + 0x3000, before->work_ram + 0x3000, 0x5000));
            CHECK(filled(gb->object_attribute_memory, sizeof(gb->object_attribute_memory), 0));
        } else {
            CHECK(!memcmp(gb->work_ram, before->work_ram, sizeof(gb->work_ram)));
            CHECK(!memcmp(gb->object_attribute_memory, before->object_attribute_memory,
                          sizeof(gb->object_attribute_memory)));
        }
        CHECK(!memcmp(&gb->power_on, &before->power_on, sizeof(gb->power_on)));
        if (firmware)
            CHECK(filled(gb->io_registers + 0x30, 16, 0));
    } else {
        CHECK(memcmp(gb->work_ram, before->work_ram, sizeof(gb->work_ram)));
        CHECK(memcmp(gb->high_ram, before->high_ram, sizeof(gb->high_ram)));
    }
    if (firmware) {
        CHECK(!gb->cpu.a && !gb->cpu.f && !gb->cpu.b && !gb->cpu.c && !gb->cpu.d && !gb->cpu.e &&
              !gb->cpu.h && !gb->cpu.l && !gb->cpu.sp);
        CHECK(gb->boot_rom_enabled && gb->div_counter == 2 && !gb->divider_subcycle &&
              !gb->interrupt_flags && !gb->interrupt_enable);
    }
    const size_t selected = cupid_gb_selected_rom_bank(gb, false);
    CHECK(selected == (gb->header.mbc_type == CUPID_GB_MBC7    ? 0u
                       : gb->header.mbc_type == CUPID_GB_MMM01 ? 0xffffu
                                                               : 1u));
    if (gb->header.mbc_type == CUPID_GB_MBC7)
        CHECK(gb->mbc7_sensor_x == 0x8000 && gb->mbc7_sensor_y == 0x8000 && gb->mbc7_latch &&
              gb->mbc7.output == 0xffff && !gb->mbc7.programming_enabled &&
              !gb->mbc7.registers_enabled);
}

static void execute_firmware(CupidGb *gb) {
    unsigned host_calls = 0;
    while (cupid_gb_sgb_startup_held(gb) && host_calls < 66000u) {
        CHECK(cupid_gb_step(gb));
        ++host_calls;
        int16_t samples[256];
        while (cupid_gb_apu_drain(gb, samples, 128u)) {
        }
    }
    CHECK(!cupid_gb_sgb_startup_held(gb));
    unsigned instructions = 0;
    while (gb->boot_rom_enabled && instructions < 32) {
        CHECK(cupid_gb_step(gb));
        ++instructions;
    }
    const bool color = gb->model == CUPID_GB_MODEL_CGB;
    CHECK(!gb->boot_rom_enabled && gb->cpu.pc == 0x100 && gb->cpu.sp == 0xfffe && gb->cpu.a == 1);
    CHECK(instructions == (color ? 23u : 17u));
    CHECK(gb->elapsed_cycles == (color ? 268u : 196u));
    CHECK(gb->work_ram[0] == 0x31 && gb->high_ram[0] == 0x42 && gb->video_ram[0] == 0x53 &&
          gb->object_attribute_memory[0] == 0x64 && gb->io_registers[0x47] == 0x75 &&
          gb->io_registers[0x30] == 0x86);
    if (color)
        CHECK(gb->work_ram[0x2000] == 0x97 && gb->cgb_wram_bank == 1);
    CHECK(cupid_gb_display_clock_state_valid(gb) && cupid_gb_pixels_timing_valid(gb) &&
          cupid_gb_vram_bus_state_valid(gb));
}

static void test_rejection_and_printer(void) {
    Host host = {.now = 1800000000};
    CupidGb *gb = create_machine(CUPID_GB_MODEL_CGB, (Cartridge){0x10, 3}, &host);
    CupidGbPrinter *printer = cupid_gb_printer_create(printer_image, printer_done, &host);
    CHECK(printer);
    cupid_gb_set_serial_link(gb, cupid_gb_printer_link(printer));
    printer->state.phase = PRINTER_HEADER;
    printer->state.index = 2;
    printer->state.header[0] = PRINTER_DATA;
    printer->state.incoming = 3;
    printer->state.incoming_bits = 2;
    CupidGb *before = malloc(sizeof(*before));
    CHECK(before);
    *before = *gb;
    const CupidGbPrinter before_printer = *printer;
    const Host before_host = host;
    CHECK(!cupid_gb_reset(NULL, CUPID_GB_RESET_HARD));
    CHECK(!cupid_gb_reset(gb, (CupidGbResetKind)99));
    CHECK(!memcmp(gb, before, sizeof(*gb)) && !memcmp(printer, &before_printer, sizeof(*printer)) &&
          !memcmp(&host, &before_host, sizeof(host)));
    gb->boot_rom_size = 255;
    *before = *gb;
    CHECK(!cupid_gb_reset(gb, CUPID_GB_RESET_QUICK));
    CHECK(!memcmp(gb, before, sizeof(*gb)) && !memcmp(printer, &before_printer, sizeof(*printer)) &&
          !memcmp(&host, &before_host, sizeof(host)));
    gb->boot_rom_size = 0;
    CHECK(cupid_gb_reset(gb, CUPID_GB_RESET_HARD));
    CHECK(cupid_gb_printer_attached(gb) == printer && printer->on_image == printer_image &&
          printer->on_done == printer_done && printer->user_data == &host);
    CHECK(printer->state.phase == PRINTER_SYNC_FIRST && !printer->state.index &&
          !printer->state.incoming_bits && !printer->state.last_cycle && !host.image_calls &&
          !host.done_calls);
    free(before);
    cupid_gb_set_serial_link(gb, (CupidGbSerialLink){0});
    cupid_gb_printer_destroy(printer);
    cupid_gb_cleanup(gb);
    free(gb);
}

static void put_u64(uint8_t *bytes, uint64_t value) {
    for (unsigned index = 0; index < 8; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8));
}

static void resign(uint8_t *bytes, size_t size) {
    for (unsigned index = 0; index < 4; ++index)
        bytes[12 + index] = (uint8_t)(size >> (index * 8));
    cupid_sha256_parts(bytes, 96, bytes + 128, size - 128, bytes + 96);
}

static CupidGbStateResult restore(CupidGb *gb, const uint8_t *bytes, size_t size,
                                  CupidGbStateContext *context, unsigned api) {
    switch (api) {
        case 0:
            return cupid_gb_load_state(gb, bytes, size);
        case 1:
            return cupid_gb_load_state_with_context(gb, bytes, size, context);
        case 2:
            return cupid_gb_load_state_live(gb, bytes, size);
        default:
            return cupid_gb_load_state_live_with_context(gb, bytes, size, context);
    }
}

static void test_tpp1_mapping_states(void) {
    /* The historical prefix stores size_t as an eight-byte little-endian value.
     * Its mapped-ROM slot is independent of the later controller readback bytes. */
    Host host = {.now = 1800000000};
    CupidGb *gb = create_machine(CUPID_GB_MODEL_DMG_ABC, (Cartridge){0xbc, 0xc1}, &host);
    CupidGbStateContext context;
    CHECK(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    const size_t size = cupid_gb_state_size(gb);
    uint8_t *saved = malloc(size), *again = malloc(size);
    CHECK(saved && again);
    for (unsigned kind = 0; kind < 2; ++kind) {
        CHECK(cupid_gb_reset(gb, (CupidGbResetKind)kind));
        CHECK(gb->current_rom_bank == 1 && gb->tpp1.rom_bank == 0);
        CHECK(cupid_gb_save_state(gb, saved, size, NULL) == CUPID_GB_STATE_OK);
        CHECK(saved[8] == CUPID_GB_STATE_VERSION && saved[MAPPED_ROM_OFFSET] == 1);
        for (unsigned api = 0; api < 4; ++api) {
            cupid_gb_write_u8(gb, 0, 0xff);
            cupid_gb_write_u8(gb, 1, 0xff);
            CHECK(gb->current_rom_bank == 0xffff && gb->tpp1.rom_bank == 0xffff);
            CHECK(restore(gb, saved, size, &context, api) == CUPID_GB_STATE_OK);
            CHECK(gb->current_rom_bank == 1 && gb->tpp1.rom_bank == 0 &&
                  cupid_gb_selected_rom_bank(gb, false) == 1 && cupid_gb_read_u8(gb, 0xa000) == 0);
            if (api < 2) {
                CHECK(cupid_gb_save_state(gb, again, size, NULL) == CUPID_GB_STATE_OK);
                CHECK(!memcmp(saved, again, size));
            }
            cupid_gb_write_u8(gb, 3, 0);
            CHECK(gb->current_rom_bank == 0 && cupid_gb_selected_rom_bank(gb, false) == 0);
        }
    }
    cupid_gb_write_u8(gb, 0, 0xff);
    cupid_gb_write_u8(gb, 1, 0xff);
    CHECK(cupid_gb_save_state(gb, saved, size, NULL) == CUPID_GB_STATE_OK);
    for (unsigned api = 0; api < 4; ++api) {
        cupid_gb_write_u8(gb, 0, 3);
        CHECK(restore(gb, saved, size, &context, api) == CUPID_GB_STATE_OK);
        CHECK(gb->current_rom_bank == 0xffff && gb->tpp1.rom_bank == 0xffff &&
              cupid_gb_selected_rom_bank(gb, false) == 0xffff);
        if (api < 2) {
            CHECK(cupid_gb_save_state(gb, again, size, NULL) == CUPID_GB_STATE_OK);
            CHECK(!memcmp(saved, again, size));
        }
    }
    free(again);
    free(saved);
    cupid_gb_cleanup(gb);
    free(gb);
}

static void advance_dot(CupidGb *gb) {
    /* These normal-speed fixtures do not reset or hold DIV after setup. Its
     * retained subcycle is the CPU-T delivery phase across the snapshot. */
    const uint64_t before = gb->elapsed_cycles;
    do {
        const unsigned phase = cupid_gb_tick_color_dma_slot(gb, 1, gb->divider_subcycle);
        CHECK(phase == gb->divider_subcycle);
    } while (gb->elapsed_cycles == before);
}

static bool quiet_timing(const CupidGb *gb) {
    return gb->pixels.line == 1 && gb->pixels.active &&
           gb->display_clock.phase == CUPID_GB_DISPLAY_CLOCK_TRANSFER_QUIET &&
           gb->display_clock.batch_half_dots && gb->display_clock.credit_half_dots >= 40 &&
           gb->vram_bus.fetch_map_address != gb->vram_bus.map_address &&
           gb->vram_bus.fetch_data_address != gb->vram_bus.data_address;
}

static void prepare_legacy_tpp1(CupidGb *gb) {
    cupid_gb_write_u8(gb, 0, 0xdc);
    cupid_gb_write_u8(gb, 1, 0xfe);
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    memset(gb->object_attribute_memory, 0, sizeof(gb->object_attribute_memory));
    fill_pattern(gb->video_ram, sizeof(gb->video_ram), 19);
    memset(gb->video_ram + 0x1800, 0x20, 0x800);
    cupid_gb_write_u8(gb, 0xff43, 3);
    cupid_gb_write_u8(gb, 0xff4a, 0);
    cupid_gb_write_u8(gb, 0xff4b, 167);
    cupid_gb_tick(gb, 4500);
    CHECK(cupid_gb_read_bus_u8(gb, 0x8007) == gb->video_ram[7]);
    cupid_gb_write_u8(gb, 0xff40, 0x83);
    for (unsigned dots = 0; !quiet_timing(gb) && dots < 2000; ++dots)
        advance_dot(gb);
    CHECK(quiet_timing(gb));
    cupid_gb_apu_run(gb, true);
}

static void compare_timing(const CupidGb *left, const CupidGb *right) {
    CHECK(left->elapsed_cycles == right->elapsed_cycles &&
          left->div_counter == right->div_counter &&
          left->divider_subcycle == right->divider_subcycle &&
          left->ppu_counter == right->ppu_counter &&
          left->ppu_transfer_end_dot == right->ppu_transfer_end_dot &&
          left->object_fetch_stage == right->object_fetch_stage &&
          left->window_line_counter == right->window_line_counter);
    CHECK(!memcmp(&left->pixels, &right->pixels, sizeof(left->pixels)) &&
          !memcmp(left->io_registers, right->io_registers, sizeof(left->io_registers)) &&
          !memcmp(left->frame_buffer, right->frame_buffer, sizeof(left->frame_buffer)) &&
          !memcmp(left->frame_buffer_color, right->frame_buffer_color,
                  sizeof(left->frame_buffer_color)));
    uint8_t a[TIMING_BYTES], b[TIMING_BYTES];
    CHECK(cupid_gb_display_clock_state_encode(left, a, CUPID_GB_DISPLAY_CLOCK_STATE_SIZE) &&
          cupid_gb_display_clock_state_encode(right, b, CUPID_GB_DISPLAY_CLOCK_STATE_SIZE));
    CHECK(cupid_gb_pixels_timing_encode(left, a + CUPID_GB_DISPLAY_CLOCK_STATE_SIZE,
                                        CUPID_GB_PIXEL_TIMING_STATE_SIZE) &&
          cupid_gb_pixels_timing_encode(right, b + CUPID_GB_DISPLAY_CLOCK_STATE_SIZE,
                                        CUPID_GB_PIXEL_TIMING_STATE_SIZE));
    CHECK(cupid_gb_vram_bus_state_encode(left, a + TIMING_BYTES - CUPID_GB_VRAM_BUS_STATE_SIZE,
                                         CUPID_GB_VRAM_BUS_STATE_SIZE) &&
          cupid_gb_vram_bus_state_encode(right, b + TIMING_BYTES - CUPID_GB_VRAM_BUS_STATE_SIZE,
                                         CUPID_GB_VRAM_BUS_STATE_SIZE));
    CHECK(!memcmp(a, b, sizeof(a)));
}

static size_t legacy_tpp1(const CupidGb *gb, uint8_t *legacy, const uint8_t *saved, size_t size,
                          unsigned version) {
    CHECK(version == 14 || version == 15);
    memcpy(legacy, saved, size);
    const size_t descriptor = size - ACCESSORY_DESCRIPTOR_BYTES;
    CHECK(filled(saved + descriptor, ACCESSORY_DESCRIPTOR_BYTES, 0));
    if (version == 15)
        legacy[descriptor - test_state_post_ram_size(gb) - SRAM_LENGTH_BYTES - IO_HISTORY_BYTES -
               CUPID_GB_VRAM_BUS_STATE_SIZE - 1u] = 0;
    /* Both versions precede the protocol, startup, I/O and SRAM extensions;
     * V14 also predates display timing. */
    const size_t removed = test_state_post_ram_size(gb) + SRAM_LENGTH_BYTES + IO_HISTORY_BYTES +
                           (version == 14 ? TIMING_BYTES : 0);
    memmove(legacy + descriptor - removed, legacy + descriptor, ACCESSORY_DESCRIPTOR_BYTES);
    size -= removed;
    legacy[8] = (uint8_t)version;
    /* Both older writers mapped the control latch while this generic cache stayed stale. */
    put_u64(legacy + MAPPED_ROM_OFFSET, 1);
    resign(legacy, size);
    return size;
}

static void test_tpp1_legacy_states(void) {
    for (unsigned version = 14; version <= 15; ++version)
        for (unsigned api = 0; api < 4; ++api) {
            Host host = {.now = 1800000000}, expected_host = {.now = 1800000000};
            CupidGb *gb = create_machine(CUPID_GB_MODEL_DMG_ABC, (Cartridge){0xbc, 0xc1}, &host);
            CupidGb *expected =
                create_machine(CUPID_GB_MODEL_DMG_ABC, (Cartridge){0xbc, 0xc1}, &expected_host);
            CupidGb *before = malloc(sizeof(*before));
            CHECK(before);
            /* Recreate the expected machine by execution; it never reads the fixture. */
            prepare_legacy_tpp1(gb);
            prepare_legacy_tpp1(expected);
            compare_timing(expected, gb);
            CupidGbStateContext context;
            CHECK(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
            const size_t size = cupid_gb_state_size(gb);
            uint8_t *saved = malloc(size), *legacy = malloc(size);
            CHECK(saved && legacy);
            CHECK(cupid_gb_save_state(gb, saved, size, NULL) == CUPID_GB_STATE_OK);
            const size_t legacy_size = legacy_tpp1(gb, legacy, saved, size, version);
            if (version == 14) {
                cupid_gb_pixels_import_legacy(expected);
                cupid_gb_display_clock_import_legacy(expected);
                cupid_gb_vram_bus_import_legacy(expected);
                CHECK(!expected->display_clock.batch_half_dots &&
                      (expected->display_clock.flags & CUPID_GB_DISPLAY_CLOCK_FORCED_FINE));
            } else {
                /* V15 did not store the row latch. This fixture selects line 1,
                 * SCY zero, with no active window, so import projects row 1. */
                CHECK(expected->pixels.line == 1 && !expected->io_registers[CUPID_GB_IO_SCY] &&
                      !expected->pixels.window_active);
                expected->pixel_timing.fetch_y = 1;
            }
            cupid_gb_tick(gb, 211);
            cupid_gb_write_u8(gb, 0, 3);
            CHECK(restore(gb, legacy, legacy_size, &context, api) == CUPID_GB_STATE_OK);
            CHECK(gb->tpp1.rom_bank == 0xfedc && gb->current_rom_bank == 0xfedc &&
                  cupid_gb_selected_rom_bank(gb, false) == 0xfedc);
            compare_timing(expected, gb);
            if (version == 15)
                CHECK(quiet_timing(gb));
            for (unsigned dot = 0; dot < 384; ++dot) {
                if (dot == 32 || dot == 128)
                    CHECK(cupid_gb_read_bus_u8(gb, 0x8007) ==
                          cupid_gb_read_bus_u8(expected, 0x8007));
                advance_dot(gb);
                advance_dot(expected);
                compare_timing(expected, gb);
            }
            /* Validation precedes migration and must preserve the entire live transaction. */
            put_u64(legacy + MAPPED_ROM_OFFSET, 512);
            resign(legacy, legacy_size);
            *before = *gb;
            const CupidGbStateContext original_context = context;
            const Host original_host = host;
            CHECK(restore(gb, legacy, legacy_size, &context, api) == CUPID_GB_STATE_INVALID);
            CHECK(!memcmp(before, gb, sizeof(*gb)) &&
                  !memcmp(&context, &original_context, sizeof(context)) &&
                  !memcmp(&host, &original_host, sizeof(host)));
            put_u64(saved + MAPPED_ROM_OFFSET, UINT64_C(0x10000));
            resign(saved, size);
            CHECK(restore(gb, saved, size, &context, api) == CUPID_GB_STATE_INVALID);
            CHECK(!memcmp(before, gb, sizeof(*gb)) &&
                  !memcmp(&context, &original_context, sizeof(context)) &&
                  !memcmp(&host, &original_host, sizeof(host)));
            free(legacy);
            free(saved);
            free(before);
            cupid_gb_cleanup(expected);
            free(expected);
            cupid_gb_cleanup(gb);
            free(gb);
        }
}

static unsigned reset_allocation_calls, reset_fail_at;

static void *reset_allocator(size_t bytes) {
    ++reset_allocation_calls;
    return reset_allocation_calls == reset_fail_at ? NULL : malloc(bytes);
}

static void test_extended_sram_reset_case(CupidGbModel model, unsigned code, bool battery,
                                          CupidGbResetKind kind, bool firmware) {
    current_model = model;
    current_type = 0xbc;
    current_kind = kind;
    current_firmware = firmware;
    Host host = {.now = 1800000000};
    CupidGb *gb = create_machine(model, (Cartridge){0xbc, 0xc1}, &host);
    gb->rom[0x152] = (uint8_t)code;
    gb->rom[0x153] = battery ? 8 : 0;
    CHECK(cupid_gb_load_rom(gb, gb->rom, gb->rom_size));
    CHECK(gb->cartridge_ram_heap && cupid_gb_cartridge_storage_valid(gb));
    if (firmware)
        install_firmware(gb);
    install_history(gb, 48000);
    for (size_t offset = 0; offset < gb->cartridge_ram_size; ++offset)
        gb->cartridge_ram_heap[offset] = (uint8_t)(offset * 37u + (offset >> 13u) * 73u + 0x19u);
    CupidGbPrinter *printer = cupid_gb_printer_create(printer_image, printer_done, &host);
    CHECK(printer);
    cupid_gb_set_serial_link(gb, cupid_gb_printer_link(printer));
    printer->state.phase = PRINTER_HEADER;
    printer->state.index = 2;
    printer->state.header[0] = PRINTER_DATA;
    CupidGb *before = malloc(sizeof(*before));
    uint8_t *ram_before = malloc(gb->cartridge_ram_capacity);
    uint8_t *rom_before = malloc(gb->rom_capacity);
    CHECK(before && ram_before && rom_before);
    memcpy(before, gb, sizeof(*gb));
    memcpy(ram_before, gb->cartridge_ram_heap, gb->cartridge_ram_capacity);
    memcpy(rom_before, gb->rom, gb->rom_capacity);
    const Host host_before = host;
    const CupidGbPrinter printer_before = *printer;

    for (unsigned fail_at = 1; fail_at <= 2; ++fail_at) {
        reset_allocation_calls = 0;
        reset_fail_at = fail_at;
        CHECK(!cupid_gb_reset_with_allocator(gb, kind, reset_allocator));
        CHECK(reset_allocation_calls == fail_at);
        CHECK(!memcmp(gb, before, sizeof(*gb)));
        CHECK(!memcmp(gb->cartridge_ram_heap, ram_before, gb->cartridge_ram_capacity));
        CHECK(!memcmp(gb->rom, rom_before, gb->rom_capacity));
        CHECK(!memcmp(&host, &host_before, sizeof(host)));
        CHECK(!memcmp(printer, &printer_before, sizeof(*printer)));
    }

    const unsigned calls = host.clock_calls;
    CHECK(cupid_gb_reset(gb, kind));
    CHECK(gb->cartridge_ram_heap != before->cartridge_ram_heap);
    CHECK(cupid_gb_cartridge_storage_valid(gb));
    CHECK(gb->cartridge_ram_capacity == before->cartridge_ram_capacity);
    if (kind == CUPID_GB_RESET_QUICK || battery)
        CHECK(!memcmp(gb->cartridge_ram_heap, ram_before, gb->cartridge_ram_size));
    else
        CHECK(filled(gb->cartridge_ram_heap, gb->cartridge_ram_size, 0xff));
    check_boundary(gb, before, kind, firmware, &host, calls);
    CHECK(gb->rom_generation == before->rom_generation &&
          !memcmp(gb->rom, rom_before, gb->rom_capacity));
    if (firmware)
        execute_firmware(gb);
    free(before); /* This snapshot borrowed the released pre-reset SRAM pointer. */
    free(ram_before);
    free(rom_before);
    cupid_gb_set_serial_link(gb, (CupidGbSerialLink){0});
    cupid_gb_printer_destroy(printer);
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_extended_sram_reset(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model)
        for (unsigned code = 6; code <= 9; ++code)
            for (unsigned battery = 0; battery < 2; ++battery)
                for (unsigned kind = 0; kind < 2; ++kind)
                    for (unsigned firmware = 0; firmware < 2; ++firmware)
                        test_extended_sram_reset_case(models[model], code, battery != 0,
                                                      (CupidGbResetKind)kind, firmware != 0);
}

int main(void) {
    static const Cartridge cartridges[] = {
        {0, 0},    {8, 2},    {9, 2},    {3, 3},    {2, 3},    {6, 0},    {5, 0},      {0x10, 3},
        {0x1e, 3}, {0x0d, 3}, {0x22, 0}, {0xfc, 3}, {0xff, 3}, {0xfe, 3}, {0xbc, 0xc1}};
    static const unsigned rates[] = {8000, 48000, 192000};
    unsigned cases = 0;
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model)
        for (unsigned index = 0; index < sizeof(cartridges) / sizeof(*cartridges); ++index)
            for (unsigned kind = 0; kind < 2; ++kind)
                for (unsigned firmware = 0; firmware < 2; ++firmware) {
                    current_model = model;
                    current_type = cartridges[index].type;
                    current_kind = kind;
                    current_firmware = firmware;
                    Host host = {.now = 1800000000};
                    CupidGb *gb = create_machine((CupidGbModel)model, cartridges[index], &host);
                    if (firmware)
                        install_firmware(gb);
                    install_history(gb, rates[index % 3]);
                    CupidGb *before = malloc(sizeof(*before));
                    CHECK(before);
                    *before = *gb;
                    const unsigned calls = host.clock_calls;
                    CHECK(cupid_gb_reset(gb, (CupidGbResetKind)kind));
                    check_boundary(gb, before, (CupidGbResetKind)kind, firmware != 0, &host, calls);
                    if (firmware)
                        execute_firmware(gb);
                    if (gb->header.mbc_type == CUPID_GB_TPP1) {
                        CHECK(cupid_gb_read_u8(gb, 0xa000) == 0 &&
                              cupid_gb_selected_rom_bank(gb, false) == 1);
                        cupid_gb_write_u8(gb, 0x2003, 0);
                        CHECK(cupid_gb_selected_rom_bank(gb, false) == 0);
                    }
                    ++cases;
                    free(before);
                    cupid_gb_cleanup(gb);
                    free(gb);
                }
    test_rejection_and_printer();
    test_extended_sram_reset();
    test_tpp1_mapping_states();
    test_tpp1_legacy_states();
    printf("Reset checks: %u model/controller/reset/firmware cases; rejection and printer checks "
           "passed\n",
           cases);
    return 0;
}

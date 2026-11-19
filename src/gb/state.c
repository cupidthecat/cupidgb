/**
 * @file    state.c
 * @brief   Portable native snapshots and transactional hardware restoration.
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

#include "cupid/gb/state.h"

#include "audio_output.h"
#include "audio_state.h"
#include "cartridge_internal.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/camera.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/huc3.h"
#include "cupid/gb/infrared.h"
#include "cupid/gb/input.h"
#include "cupid/gb/mbc7.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/tpp1.h"
#include "display_clock.h"
#include "gbs_internal.h"
#include "mbc6_internal.h"
#include "oam_bus.h"
#include "printer_internal.h"
#include "state_internal.h"
#include "state_io.h"
#include "state_sgb_protocol.h"
#include "state_startup.h"
#include "tama5_internal.h"
#include "vram_bus.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    STATE_HEADER_SIZE = 128,
    STATE_VERSION_V30 = CUPID_GB_STATE_VERSION,
    STATE_VERSION_V29 = 29,
    STATE_VERSION_V28 = 28,
    STATE_VERSION_V27 = 27,
    STATE_VERSION_V26 = 26,
    STATE_VERSION_V25 = 25,
    STATE_VERSION_V24 = 24,
    STATE_VERSION_V23 = 23,
    STATE_VERSION_V22 = 22,
    STATE_VERSION_V21 = 21,
    STATE_VERSION_V20 = 20,
    STATE_VERSION_V19 = 19,
    STATE_VERSION_V18 = 18,
    STATE_VERSION_V17 = 17,
    STATE_VERSION_V16 = 16,
    STATE_VERSION_V15 = 15,
    STATE_VERSION_V14 = 14,
    STATE_VERSION_V13 = 13,
    STATE_VERSION_V12 = 12,
    STATE_INFRARED_SIZE = 6,
    STATE_CLOCK_HISTORY_SIZE = 6,
    STATE_INPUT_SAMPLE_SIZE = 1,
    STATE_SERIAL_CLOCK_SIZE = 1,
    STATE_WAVE_HISTORY_SIZE = 1,
    STATE_PCM_READ_SIZE = 2,
    STATE_RAM_LENGTH_SIZE = 4,
    STATE_ACCESSORY_DESCRIPTOR_SIZE = 8,
    /* V11 activates monochrome pulse timing and appends the held DAC codes. */
    STATE_VERSION_V11 = 11,
    STATE_VERSION_V10 = 10,
    STATE_VERSION_V9 = 9,
    STATE_VERSION_V8 = 8,
    STATE_VERSION_V7 = 7,
    STATE_VERSION_V6 = 6,
    STATE_VERSION_V5 = 5,
    STATE_VERSION_V4 = 4,
    STATE_VERSION_V3 = 3,
    STATE_VERSION_V2 = 2,
    STATE_WIRE_SIZE_V2 = 300518,
    STATE_WIRE_SIZE_V3 = STATE_WIRE_SIZE_V2 + 50,
    STATE_WIRE_SIZE_V4 = STATE_WIRE_SIZE_V3 + 4,
    STATE_LCD_PHASE_SIZE = 2,
    STATE_DMA_BUS_SIZE = 2,
    STATE_V6_TAIL_SIZE = STATE_LCD_PHASE_SIZE + STATE_DMA_BUS_SIZE,
    STATE_V7_TAIL_SIZE = STATE_V6_TAIL_SIZE + 1,
    STATE_V8_TAIL_SIZE = STATE_V7_TAIL_SIZE + 11,
    STATE_V9_TAIL_SIZE = STATE_V8_TAIL_SIZE + 6,
    STATE_V10_TAIL_SIZE = STATE_V9_TAIL_SIZE + 4,
    STATE_V11_TAIL_SIZE = STATE_V10_TAIL_SIZE + 2,
    STATE_MMM01_EXTENSION_SIZE = 4,
    STATE_MBC7_EXTENSION_SIZE = 8,
    STATE_CAMERA_EXTENSION_SIZE = 64,
    STATE_TPP1_EXTENSION_SIZE = 21,
    STATE_HUC3_EXTENSION_SIZE = 25,
    STATE_SGB_EXTENSION_SIZE =
        7 + 8192 +
        2 * (CUPID_SGB_TILE_WIDTH * CUPID_SGB_TILE_HEIGHT + CUPID_SGB_BORDER_PALETTE_COLORS)
};

enum {
    STATE_SECTION_SGB = 1u << 0,
    STATE_SECTION_MMM01 = 1u << 1,
    STATE_SECTION_MBC7 = 1u << 2,
    STATE_SECTION_CAMERA = 1u << 3,
    STATE_SECTION_TPP1 = 1u << 4,
    STATE_SECTION_HUC3 = 1u << 5,
    STATE_SECTIONS_ALL = STATE_SECTION_SGB | STATE_SECTION_MMM01 | STATE_SECTION_MBC7 |
                         STATE_SECTION_CAMERA | STATE_SECTION_TPP1 | STATE_SECTION_HUC3
};

static const uint8_t state_magic[8] = {'C', 'U', 'P', 'I', 'D', 'G', 'B', 0};

static void state_noise_envelope(StateIo *io, CupidGb *gb, unsigned version) {
    CupidGbApu *apu = &gb->apu;
    CupidGbEnvelopeGate *gate = &apu->noise_timing.envelope;
    if (version < STATE_VERSION_V29 || !cupid_gb_model_is_cgb(gb->model)) {
        byte(io, &apu->ch4_env_period);
        if (io->input)
            *gate = (CupidGbEnvelopeGate){0};
        return;
    }

    /* The period uses three bits. Color states from version 29 also put
     * the noise envelope latches in the unused bits of that byte. */
    uint8_t raw = (uint8_t)(apu->ch4_env_period | (gate->clock ? 0x08u : 0u) |
                            (gate->locked ? 0x10u : 0u) | (gate->lock_pending ? 0x20u : 0u));
    byte(io, &raw);
    if (io->input) {
        if (raw & 0xc0u)
            io->invalid = true;
        apu->ch4_env_period = raw & 7u;
        *gate = (CupidGbEnvelopeGate){(raw & 0x08u) != 0, (raw & 0x10u) != 0, (raw & 0x20u) != 0};
    }
}

/* The order below is the v2 wire schema. Every scalar has an explicit width and every
 * array has an explicit element encoding. Never replace this with a structure dump:
 * host padding, pointer widths, and bool representations are not part of the format.
 * New hardware state requires a format revision and continuation tests.
 */
static void state_fields(StateIo *io, CupidGb *gb, unsigned version) {
#define U8(field) byte(io, &gb->field)
#define BOOL(field) boolean(io, &gb->field)
#define U16(field) word(io, &gb->field)
#define U32(field) double_word(io, &gb->field)
#define U64(field) quad_word(io, &gb->field)
#define SIZE(field) size_value(io, &gb->field)
#define BYTES(field) bytes(io, gb->field, sizeof(gb->field))
#define WORDS(field) words(io, gb->field, sizeof(gb->field) / sizeof(gb->field[0]))
    U8(cpu.a);
    U8(cpu.f);
    U8(cpu.b);
    U8(cpu.c);
    U8(cpu.d);
    U8(cpu.e);
    U8(cpu.h);
    U8(cpu.l);
    U16(cpu.sp);
    U16(cpu.pc);
    U8(cpu.ime_delay);
    BOOL(cpu.ime);
    BOOL(cpu.halt_bug);
    BOOL(cpu.halted);
    BOOL(cpu.stopped);

    BYTES(video_ram);
    /* This fixed span is part of every version's prefix. Larger storage keeps
     * its first 128 KiB here and appends only the remaining bytes in SRAM-aware versions. */
    bytes(io, cupid_gb_cartridge_ram(gb), CUPID_GB_INLINE_RAM_SIZE);
    BYTES(work_ram);
    BYTES(object_attribute_memory);
    BYTES(io_registers);
    BYTES(high_ram);
    BYTES(frame_buffer);
    WORDS(frame_buffer_color);
    U8(interrupt_enable);
    U8(interrupt_flags);
    SIZE(current_rom_bank);
    SIZE(current_ram_bank);
    U8(mbc1_bank_low5);
    U8(mbc1_bank_high2);
    BOOL(mbc1_multicart);
    BOOL(ram_enabled);
    BOOL(mbc1_ram_banking_mode);
    U8(rtc.s);
    U8(rtc.m);
    U8(rtc.h);
    U8(rtc.dl);
    U8(rtc.dh);
    U8(rtc.ls);
    U8(rtc.lm);
    U8(rtc.lh);
    U8(rtc.ldl);
    U8(rtc.ldh);
    U8(rtc.latch_prev);
    signed_quad_word(io, &gb->rtc.base_time);
    U8(rtc_register);
    U16(mbc5_rom_bank);
    U16(mbc7_sensor_x);
    U16(mbc7_sensor_y);
    BOOL(mbc7_latch);
    BOOL(mmm01_locked);
    U8(mmm01_bank_base);
    BOOL(huc1_ir_mode);
    U8(huc3_mode);
    U8(huc3_value);
    BOOL(serial_master_clock);
    U64(elapsed_cycles);
    U8(serial_bits_remaining);
    U8(serial_tx_latch);
    U16(div_counter);
    U16(timer_counter);
    U8(tima_overflow_delay);
    BOOL(tima_reload_just_happened);
    U16(dma_source_base);
    U16(dma_restart_source_base);
    U8(dma_index);
    U8(dma_start_delay);
    U8(dma_restart_delay);
    BOOL(dma_active);
    BOOL(dma_restart_pending);
    U16(ppu_counter);
    BOOL(cgb_mode);
    U8(cgb_vram_bank);
    U8(cgb_wram_bank);
    BYTES(cgb_bg_palette_ram);
    BYTES(cgb_obj_palette_ram);
    U16(hdma_source);
    U16(hdma_destination);
    U8(hdma_blocks_remaining);
    BOOL(hdma_active);
    BOOL(double_speed);
    BOOL(speed_switch_armed);
    BOOL(speed_phase);
    U8(joypad);
    U8(window_line_counter);
    U8(ppu_lcd_warmup_lines);
    U8(stat_irq_delay);
    BOOL(ppu_lcd_startup);
    BOOL(ppu_line_boundary_hold);
    BOOL(stat_irq_line);
    BOOL(frame_ready);

    BOOL(sgb.enabled);
    U8(sgb.player_count);
    U8(sgb.current_player);
    U8(sgb.mask_mode);
    BYTES(sgb.command);
    U16(sgb.command_write_index);
    BOOL(sgb.ready_for_pulse);
    BOOL(sgb.ready_for_write);
    BOOL(sgb.ready_for_stop);
    BYTES(sgb.attribute_map);
    BYTES(sgb.attribute_files);
    WORDS(sgb.screen_palettes);
    WORDS(sgb.ram_palettes);
    BYTES(sgb.border_tiles);
    WORDS(sgb.border_map);
    WORDS(sgb.border_palettes);
    BYTES(sgb.freeze_buffer);

    U8(apu.fs_step);
    U8(apu.fs_skip_div_event);
    BOOL(apu.apu_on);
    BOOL(apu.ch1_dac);
    BOOL(apu.ch1_on);
    U8(apu.ch1_duty);
    U8(apu.ch1_duty_pos);
    U8(apu.ch1_len);
    BOOL(apu.ch1_len_en);
    U8(apu.ch1_vol);
    BOOL(apu.ch1_env_add);
    U8(apu.ch1_env_period);
    U8(apu.ch1_env_timer);
    U16(apu.ch1_freq);
    U16(apu.ch1_timer);
    BOOL(apu.ch1_sweep_en);
    U8(apu.ch1_sweep_timer);
    U8(apu.ch1_sweep_period);
    BOOL(apu.ch1_sweep_neg);
    U8(apu.ch1_sweep_shift);
    U16(apu.ch1_sweep_shadow);
    BOOL(apu.ch1_sweep_subtracted);
    BOOL(apu.ch2_dac);
    BOOL(apu.ch2_on);
    U8(apu.ch2_duty);
    U8(apu.ch2_duty_pos);
    U8(apu.ch2_len);
    BOOL(apu.ch2_len_en);
    U8(apu.ch2_vol);
    BOOL(apu.ch2_env_add);
    U8(apu.ch2_env_period);
    U8(apu.ch2_env_timer);
    U16(apu.ch2_freq);
    U16(apu.ch2_timer);
    BOOL(apu.ch3_dac);
    BOOL(apu.ch3_on);
    U16(apu.ch3_len);
    BOOL(apu.ch3_len_en);
    U8(apu.ch3_out_level);
    U16(apu.ch3_freq);
    U16(apu.ch3_active_freq);
    U16(apu.ch3_timer);
    U8(apu.ch3_wave_pos);
    U8(apu.ch3_current_byte);
    BOOL(apu.ch3_started);
    U8(apu.ch3_wave_access_ticks);
    BOOL(apu.ch3_freq_pending);
    U8(apu.ch3_sample);
    BOOL(apu.ch4_dac);
    BOOL(apu.ch4_on);
    U8(apu.ch4_len);
    BOOL(apu.ch4_len_en);
    U8(apu.ch4_vol);
    BOOL(apu.ch4_env_add);
    state_noise_envelope(io, gb, version);
    U8(apu.ch4_env_timer);
    U8(apu.ch4_div_code);
    U8(apu.ch4_shift);
    BOOL(apu.ch4_width7);
    U32(apu.ch4_timer);
    U16(apu.ch4_lfsr);
    U32(apu.output.sample_acc);
    /* Signed PCM is stored as explicit two's-complement 16-bit little-endian samples. */
    for (size_t i = 0; i < sizeof(gb->apu.output.buf) / sizeof(gb->apu.output.buf[0]); ++i) {
        uint16_t raw = (uint16_t)gb->apu.output.buf[i];
        word(io, &raw);
        if (io->input) {
            gb->apu.output.buf[i] =
                raw <= INT16_MAX ? (int16_t)raw : (int16_t)((int32_t)raw - 65536);
        }
    }
    U32(apu.output.buf_write);
    BOOL(boot_rom_enabled);
    U32(apu.output.sample_rate);
    /* These V2 slots describe the retired integer mixer. Keep their wire
     * positions and validate old files, while new snapshots write zeroes. */
    int64_t legacy_history[4] = {0};
    for (unsigned i = 0; i < 4; ++i)
        signed_quad_word(io, &legacy_history[i]);
    uint32_t legacy_retention = 0;
    double_word(io, &legacy_retention);
    BOOL(apu.output.policy.filter_disabled);
    const int64_t area_limit = INT64_C(16320) * gb->apu.output.sample_acc;
    if (legacy_history[0] < -area_limit || legacy_history[0] > area_limit ||
        legacy_history[1] < -area_limit || legacy_history[1] > area_limit ||
        legacy_history[2] < -INT64_C(1069547520) || legacy_history[2] > INT64_C(1069547520) ||
        legacy_history[3] < -INT64_C(1069547520) || legacy_history[3] > INT64_C(1069547520) ||
        legacy_retention > UINT32_C(1073741824) ||
        (version >= STATE_VERSION_V14 &&
         (legacy_history[0] || legacy_history[1] || legacy_history[2] || legacy_history[3] ||
          legacy_retention)))
        io->invalid = true;
    U16(pixels.dot);
    U8(pixels.line);
    U8(pixels.x);
    BYTES(pixels.colors);
    BYTES(pixels.palettes);
    BYTES(pixels.priorities);
    U8(pixels.read);
    U8(pixels.count);
    U8(pixels.phase);
    U8(pixels.tile);
    U8(pixels.attributes);
    U8(pixels.low);
    U8(pixels.high);
    U8(pixels.column);
    U8(pixels.discard);
    U8(pixels.delay);
    U16(pixels.address);
    BYTES(pixels.objects);
    BYTES(pixels.object_x);
    BYTES(pixels.object_y);
    U8(pixels.object_count);
    U16(pixels.fetched_objects);
    U8(pixels.object_delay);
    U8(pixels.object_index);
    U8(pixels.object_low);
    U8(pixels.object_high);
    U8(pixels.object_attributes);
    U16(pixels.object_address);
    BYTES(pixels.sprite_colors);
    BYTES(pixels.sprite_palettes);
    BYTES(pixels.sprite_flags);
    WORDS(pixels.sprite_priorities);
    BOOL(pixels.initialized);
    BOOL(pixels.active);
    BOOL(pixels.window_armed);
    BOOL(pixels.window_active);
    BOOL(pixels.window_used);
    BOOL(pixels.lyc_irq);
    BOOL(cgb_object_priority_x);
    BOOL(cpu.just_halted);
#undef U8
#undef BOOL
#undef U16
#undef U32
#undef U64
#undef SIZE
#undef BYTES
#undef WORDS
}

/* The complete v2 payload remains a prefix. New physical latches have their own
 * explicit extension, independent of C structure padding and host ABI changes. */
static void state_extension_v3(StateIo *io, CupidGb *gb) {
    boolean(io, &gb->hdma_pending);
    byte(io, &gb->hdma_hblank_delay);
    boolean(io, &gb->hdma_allow_on_wake);
    byte(io, &gb->apu.clock_phase);
    byte(io, &gb->apu.envelope_delay);
    for (unsigned channel = 0; channel < 2; ++channel) {
        CupidGbPulseTiming *timing = &gb->apu.pulse_timing[channel];
        byte(io, &timing->start_delay);
        byte(io, &timing->edge_age);
        boolean(io, &timing->clock_started);
        boolean(io, &timing->output_high);
        boolean(io, &timing->output_blocked);
        boolean(io, &timing->envelope.clock);
        boolean(io, &timing->envelope.locked);
        boolean(io, &timing->envelope.lock_pending);
    }
    CupidGbSweepTiming *sweep = &gb->apu.sweep_timing;
    byte(io, &sweep->startup_ticks);
    byte(io, &sweep->shift_ticks);
    byte(io, &sweep->restart_hold);
    word(io, &sweep->delta);
    word(io, &sweep->completed_delta);
    boolean(io, &sweep->zero_shift_pending);
    boolean(io, &sweep->zero_shift_armed);
    CupidGbNoiseTiming *noise = &gb->apu.noise_timing;
    word(io, &noise->divider_counter);
    byte(io, &noise->clock_phase);
    byte(io, &noise->remaining);
    boolean(io, &noise->foreground);
    boolean(io, &noise->background);
    boolean(io, &noise->started_dac_off);
    boolean(io, &noise->reload_edge);
    boolean(io, &noise->counter_stepped);
    boolean(io, &noise->output_high);
    boolean(io, &noise->last_shift_narrow);
    boolean(io, &noise->pre_shift_bit7);
    double_word(io, &gb->speed_switch_halt_cycles);
    byte(io, &gb->speed_switch_toggle_cycles);
    byte(io, &gb->speed_switch_freeze_cycles);
    byte(io, &gb->stop_divider_hold_cycles);
    byte(io, &gb->speed_switch_alignment);
}

static void state_extension_v4(StateIo *io, CupidGb *gb) {
    bytes(io, gb->joypad_extra, sizeof(gb->joypad_extra));
    boolean(io, &gb->mbc5_rumble_on);
}

static bool sgb_model(CupidGbModel model) {
    return cupid_gb_model_is_sgb(model);
}

/* Version 5 sections follow the immutable hardware and cartridge identity. */
static unsigned state_sections(const CupidGb *gb) {
    unsigned sections = sgb_model(gb->model) ? STATE_SECTION_SGB : 0;
    if (gb->header.mbc_type == CUPID_GB_MMM01) {
        sections |= STATE_SECTION_MMM01;
    } else if (gb->header.mbc_type == CUPID_GB_MBC7) {
        sections |= STATE_SECTION_MBC7;
    } else if (gb->header.mbc_type == CUPID_GB_CAMERA) {
        sections |= STATE_SECTION_CAMERA;
    } else if (gb->header.mbc_type == CUPID_GB_TPP1) {
        sections |= STATE_SECTION_TPP1;
    } else if (gb->header.mbc_type == CUPID_GB_HUC3) {
        sections |= STATE_SECTION_HUC3;
    }
    return sections;
}

static size_t extended_wire_size(unsigned sections) {
    size_t size = STATE_WIRE_SIZE_V4;
    if (sections & STATE_SECTION_SGB) {
        size += STATE_SGB_EXTENSION_SIZE;
    }
    if (sections & STATE_SECTION_MMM01) {
        size += STATE_MMM01_EXTENSION_SIZE;
    }
    if (sections & STATE_SECTION_MBC7) {
        size += STATE_MBC7_EXTENSION_SIZE;
    }
    if (sections & STATE_SECTION_CAMERA) {
        size += STATE_CAMERA_EXTENSION_SIZE;
    }
    if (sections & STATE_SECTION_TPP1) {
        size += STATE_TPP1_EXTENSION_SIZE;
    }
    if (sections & STATE_SECTION_HUC3) {
        size += STATE_HUC3_EXTENSION_SIZE;
    }
    return size;
}

static void state_extension_sgb(StateIo *io, CupidGb *gb) {
    CupidGbSgbTransfer *transfer = &gb->sgb.transfer;
    double_word(io, &transfer->frame_dots);
    byte(io, &transfer->destination);
    byte(io, &transfer->frames_remaining);
    byte(io, &transfer->border_transition);
    bytes(io, transfer->tiles, sizeof(transfer->tiles));
    words(io, transfer->map, CUPID_SGB_TILE_WIDTH * CUPID_SGB_TILE_HEIGHT);
    words(io, transfer->palettes, CUPID_SGB_BORDER_PALETTE_COLORS);
}

static bool valid_sgb_extension(const CupidGbSgbTransfer *transfer) {
    return transfer->frame_dots <= CUPID_SGB_FRAME_DOTS &&
           transfer->destination <= CUPID_SGB_TRANSFER_BORDER &&
           transfer->frames_remaining <= CUPID_SGB_TRANSFER_FRAMES &&
           ((transfer->destination == CUPID_SGB_TRANSFER_NONE) ==
            (transfer->frames_remaining == 0)) &&
           transfer->border_transition < CUPID_SGB_BORDER_TRANSITION_FRAMES;
}

static void import_legacy_sgb(CupidGb *gb) {
    CupidGbSgb *sgb = &gb->sgb;
    memset(&sgb->transfer, 0, sizeof(sgb->transfer));
    if (sgb_model(gb->model)) {
        /* Older states applied transfers immediately. Retain the visible
         * border as the starting point for subsequent staged uploads. */
        memcpy(sgb->transfer.tiles, sgb->border_tiles, sizeof(sgb->transfer.tiles));
        memcpy(sgb->transfer.map, sgb->border_map, sizeof(sgb->transfer.map));
        memcpy(sgb->transfer.palettes, sgb->border_palettes, sizeof(sgb->transfer.palettes));
        if (sgb->mask_mode != CUPID_SGB_MASK_FREEZE) {
            memcpy(sgb->freeze_buffer, gb->frame_buffer, sizeof(sgb->freeze_buffer));
        }
    }
}

static void state_extension_mmm01(StateIo *io, CupidGb *gb) {
    byte(io, &gb->mmm01_rom_select);
    byte(io, &gb->mmm01_bank_select);
    byte(io, &gb->mmm01_mode);
    byte(io, &gb->mmm01_ram_mask);
}

static bool valid_mmm01_extension(const CupidGb *gb) {
    return gb->mmm01_rom_select <= 0x7fu && gb->mmm01_bank_select <= 0x7fu &&
           (gb->mmm01_mode & 0x82u) == 0 && gb->mmm01_ram_mask <= 3u;
}

static void import_legacy_mmm01(CupidGb *gb) {
    /* Legacy writes never left the menu. Their old lock flag cannot
     * supply the address masks or other latches used after locking. */
    gb->mmm01_locked = false;
    gb->mmm01_rom_select = 0;
    gb->mmm01_bank_select = 0;
    gb->mmm01_mode = 0;
    gb->mmm01_ram_mask = 3;
}

static void state_extension_mbc7(StateIo *io, CupidGbMbc7 *device) {
    word(io, &device->command);
    word(io, &device->output);
    byte(io, &device->data_bits);
    byte(io, &device->pins);
    boolean(io, &device->registers_enabled);
    boolean(io, &device->programming_enabled);
}

static bool valid_mbc7_extension(const CupidGbMbc7 *device) {
    if ((device->pins & ~0xc3u) || device->data_bits > 16) {
        return false;
    }
    if (!device->data_bits) {
        return device->command < 0x400u;
    }
    /* The serial interface accepts a payload only after WRITE or WRAL. */
    return (device->command >= 0x500u && device->command <= 0x5ffu) ||
           (device->command >= 0x440u && device->command <= 0x47fu);
}

static void state_extension_camera(StateIo *io, CupidGbCamera *camera) {
    bytes(io, camera->registers, sizeof(camera->registers));
    byte(io, &camera->clock_phase);
    boolean(io, &camera->mapped);
    double_word(io, &camera->busy_tcycles);
    double_word(io, &camera->capture_number);
}

static bool valid_camera_extension(const CupidGb *gb, const CupidGbCamera *camera) {
    return gb->current_rom_bank <= 0xffu && gb->current_ram_bank <= 0x0fu &&
           camera->registers[0] <= 7u && camera->clock_phase <= 7u &&
           camera->busy_tcycles <= CUPID_GB_CAMERA_MAX_BUSY_T_CYCLES &&
           ((camera->registers[0] & 1u) != 0) == (camera->busy_tcycles != 0);
}

static bool camera_for_snapshot(const CupidGb *gb, CupidGbCamera *camera) {
    *camera = gb->camera;
    const CupidGbCameraHost *host = &gb->camera_host;
    if (!host->request_callback)
        return !host->pending_request;
    /* A device request has no guest PHI countdown. Export only this recognized
     * live shape as idle; all other camera fields still pass the wire validator.
     * Native files never acquire permission to encode busy with zero countdown. */
    if (camera->busy_tcycles ||
        ((camera->registers[0] & 1u) != 0) != (host->pending_request != 0) ||
        (host->pending_request && host->pending_request != host->request_sequence))
        return false;
    camera->registers[0] &= 0xfeu;
    return true;
}

static void state_extension_tpp1(StateIo *io, CupidGbTpp1 *device) {
    word(io, &device->rom_bank);
    byte(io, &device->ram_bank);
    byte(io, &device->mode);
    byte(io, &device->status);
    bytes(io, device->live, sizeof(device->live));
    bytes(io, device->latched, sizeof(device->latched));
    signed_quad_word(io, &device->rtc_base_time);
}

static bool valid_tpp1_extension(const CupidGbTpp1 *device) {
    return cupid_gb_tpp1_valid_mode(device->mode) && device->status <= 0x0fu &&
           device->rtc_base_time >= 0;
}

static void state_extension_huc3(StateIo *io, CupidGbHuc3 *device) {
    signed_quad_word(io, &device->rtc_base_time);
    word(io, &device->minutes);
    word(io, &device->days);
    word(io, &device->alarm_minutes);
    word(io, &device->alarm_days);
    word(io, &device->ir_warmup);
    word(io, &device->ir_decay);
    byte(io, &device->index);
    byte(io, &device->flags);
    boolean(io, &device->alarm_enabled);
    boolean(io, &device->ir_output);
    boolean(io, &device->ir_detected);
}

static bool valid_huc3_extension(const CupidGb *gb) {
    const CupidGbHuc3 *device = &gb->huc3;
    return gb->huc3_mode <= 0x0fu && gb->huc3_value <= 0x0fu && device->flags <= 0x0fu &&
           device->rtc_base_time >= 0 && device->ir_warmup <= CUPID_GB_HUC3_IR_WARMUP_LIMIT &&
           device->ir_decay <= CUPID_GB_HUC3_IR_DECAY_LIMIT;
}

static size_t infrared_wire_size(unsigned sections) {
    return sections & STATE_SECTION_HUC3 ? 0 : STATE_INFRARED_SIZE;
}

static void state_infrared(StateIo *io, CupidGb *gb) {
    word(io, &gb->huc3.ir_warmup);
    word(io, &gb->huc3.ir_decay);
    boolean(io, &gb->huc3.ir_output);
    boolean(io, &gb->huc3.ir_detected);
}

static bool valid_infrared(const CupidGb *gb) {
    return gb->huc3.ir_warmup <= CUPID_GB_INFRARED_WARMUP_LIMIT &&
           gb->huc3.ir_decay <= CUPID_GB_INFRARED_DECAY_LIMIT;
}

static void state_clock_history(StateIo *io, CupidGb *gb) {
    double_word(io, &gb->rtc_cycles);
    const bool tpp1 = gb->header.mbc_type == CUPID_GB_TPP1;
    uint8_t motor = tpp1 ? gb->tpp1.motor_strength : 0;
    byte(io, &motor);
    byte(io, &gb->divider_subcycle);
    if (motor > 3 || (!tpp1 && motor) || gb->divider_subcycle > 3)
        io->invalid = true;
    if (io->input)
        gb->tpp1.motor_strength = motor;
}

static void state_extension_v5(StateIo *io, CupidGb *gb, unsigned sections, CupidGbCamera *camera) {
    if (sections & STATE_SECTION_SGB) {
        state_extension_sgb(io, gb);
    }
    if (sections & STATE_SECTION_MMM01) {
        state_extension_mmm01(io, gb);
    }
    if (sections & STATE_SECTION_MBC7) {
        state_extension_mbc7(io, &gb->mbc7);
    }
    if (sections & STATE_SECTION_CAMERA) {
        state_extension_camera(io, camera);
    }
    if (sections & STATE_SECTION_TPP1) {
        state_extension_tpp1(io, &gb->tpp1);
    }
    if (sections & STATE_SECTION_HUC3) {
        state_extension_huc3(io, &gb->huc3);
    }
}

static void state_lcd_phase(StateIo *io, CupidGb *gb) {
    boolean(io, &gb->lcd_frame_odd);
    boolean(io, &gb->lcd_first_frame);
}

static void state_dma_bus(StateIo *io, CupidGb *gb, uint32_t version) {
    const uint64_t now = gb->elapsed_cycles * 2u;
    const uint64_t remaining = gb->cartridge_bus_expiry > now ? gb->cartridge_bus_expiry - now : 0;
    if (!io->input && remaining > CUPID_GB_CARTRIDGE_BUS_HALF_DOTS) {
        io->invalid = true;
        return;
    }

    uint8_t half_dots = (uint8_t)remaining;
    uint8_t data = cupid_gb_sample_cartridge_bus(gb);
    bytes(io, &half_dots, 1);
    bytes(io, &data, 1);
    if (io->input) {
        const bool valid_stable = data == 0xffu || (version >= STATE_VERSION_V24 && data == 0);
        if (half_dots > CUPID_GB_CARTRIDGE_BUS_HALF_DOTS || (!half_dots && !valid_stable)) {
            io->invalid = true;
            return;
        }
        gb->cartridge_bus_expiry = half_dots ? now + half_dots : 0;
        gb->cartridge_bus_data = data;
    }
}

static void state_input_and_display(StateIo *io, CupidGb *gb) {
    byte(io, &gb->joyp_select_delay);
    byte(io, &gb->joyp_select_value);
    double_word(io, &gb->display_cycles_since_frame);
    double_word(io, &gb->lcd_repeat_countdown);
    byte(io, &gb->display_frame_type);
    const bool monochrome = gb->model == CUPID_GB_MODEL_DMG_ABC ||
                            gb->model == CUPID_GB_MODEL_DMG0 || gb->model == CUPID_GB_MODEL_MGB;
    const unsigned maximum_delay = gb->model == CUPID_GB_MODEL_MGB ? 20u : 24u;
    if ((gb->joyp_select_value & ~0x30u) || (!gb->joyp_select_delay && gb->joyp_select_value) ||
        (gb->joyp_select_delay && (!monochrome || gb->joyp_select_delay > maximum_delay)) ||
        gb->display_cycles_since_frame >= 140448u || gb->lcd_repeat_countdown > 67484u ||
        (!cupid_gb_model_is_cgb(gb->model) && gb->lcd_repeat_countdown) ||
        gb->display_frame_type > CUPID_GB_FRAME_REPEAT)
        io->invalid = true;
}

static bool oam_vram_row_used(const CupidGb *gb) {
    return cupid_gb_model_is_cgb(gb->model) && gb->dma_active && gb->dma_index &&
           gb->dma_index <= 160 && (gb->dma_source_base & 0xe000u) == 0x8000u;
}

static void state_video_bus(StateIo *io, CupidGb *gb, unsigned version) {
    /* Restart, warmup and completed transfers clear or bypass the retained row
     * before another shared read can use it. Saving must not alter the live latch. */
    uint16_t row = !io->input && !oam_vram_row_used(gb) ? UINT16_MAX : gb->oam_vram_row;
    word(io, &row);
    word(io, &gb->oam_search_word);
    boolean(io, &gb->stop_ppu_vram_blocked);
    const unsigned maximum_stage =
        version >= STATE_VERSION_V15 ? CUPID_GB_OBJECT_ABORTED : CUPID_GB_OBJECT_LEGACY;
    const bool invalid_stage = !io->input && (unsigned)gb->object_fetch_stage > maximum_stage;
    uint8_t stage = io->input ? 0 : (uint8_t)gb->object_fetch_stage;
    byte(io, &stage);
    const bool native_busy = stage == CUPID_GB_OBJECT_WAIT_BACKGROUND ||
                             stage == CUPID_GB_OBJECT_READ_WORD ||
                             stage == CUPID_GB_OBJECT_ABORTED ||
                             (stage == CUPID_GB_OBJECT_IDLE && gb->pixels.object_delay);
    const CupidGbPixels *p = &gb->pixels;
    /* Check the selected slot before shifting. Malformed files may contain an
     * index beyond either the selected objects or the fixed ten-slot array. */
    const bool invalid_native =
        native_busy &&
        (p->object_index >= p->object_count || p->object_index >= 10 ||
         !(p->fetched_objects & (1u << p->object_index)) || !p->active || p->line >= 144 ||
         p->x >= CUPID_GB_SCREEN_WIDTH || !p->count || p->delay > 7 ||
         ((stage == CUPID_GB_OBJECT_READ_WORD || stage == CUPID_GB_OBJECT_IDLE) && p->phase != 6));
    if ((row != UINT16_MAX && row > 0x1fffu) || (!oam_vram_row_used(gb) && row != UINT16_MAX) ||
        invalid_stage || invalid_native || stage > maximum_stage ||
        (stage == CUPID_GB_OBJECT_ABORTED && cupid_gb_model_is_cgb(gb->model)) ||
        ((stage == CUPID_GB_OBJECT_WAIT_BACKGROUND || stage == CUPID_GB_OBJECT_READ_WORD ||
          stage == CUPID_GB_OBJECT_ABORTED) &&
         gb->pixels.object_delay) ||
        (stage == CUPID_GB_OBJECT_IDLE && gb->pixels.object_delay > 5) ||
        (stage == CUPID_GB_OBJECT_LEGACY &&
         (!gb->pixels.object_delay || gb->pixels.object_delay > 11))) {
        io->invalid = true;
    }
    if (io->input) {
        gb->oam_vram_row = row;
        gb->object_fetch_stage = (CupidGbObjectFetchStage)stage;
    }
}

static void state_stop_ports(StateIo *io, CupidGb *gb) {
    boolean(io, &gb->stop_ppu_oam_blocked);
    boolean(io, &gb->stop_ppu_palette_blocked);
    word(io, &gb->ppu_transfer_end_dot);
    /* A last-pixel edge can only belong to an inactive visible line. The
     * exact edge is history and cannot be reconstructed from mode bits. */
    if (gb->ppu_transfer_end_dot &&
        (gb->ppu_transfer_end_dot < 240u || gb->ppu_transfer_end_dot > 455u || gb->pixels.active ||
         gb->pixels.line >= 144u || gb->pixels.x != CUPID_GB_SCREEN_WIDTH ||
         gb->pixels.dot < gb->ppu_transfer_end_dot))
        io->invalid = true;
}

static bool valid_v5_sections(const CupidGb *gb, unsigned sections, const CupidGbCamera *camera) {
    return (!(sections & STATE_SECTION_SGB) || valid_sgb_extension(&gb->sgb.transfer)) &&
           (!(sections & STATE_SECTION_MMM01) || valid_mmm01_extension(gb)) &&
           (!(sections & STATE_SECTION_MBC7) || valid_mbc7_extension(&gb->mbc7)) &&
           (!(sections & STATE_SECTION_CAMERA) || valid_camera_extension(gb, camera)) &&
           (!(sections & STATE_SECTION_TPP1) || valid_tpp1_extension(&gb->tpp1)) &&
           (!(sections & STATE_SECTION_HUC3) || valid_huc3_extension(gb));
}

static bool ready(const CupidGb *gb) {
    return gb && gb->loaded && gb->rom && gb->rom_size >= 0x150 &&
           gb->rom_size <= CUPID_GB_MAX_ROM_SIZE && gb->rom_size <= gb->rom_capacity &&
           cupid_gb_cartridge_storage_valid(gb) && cupid_gb_program_valid(gb);
}

static bool valid_hardware_prefix(const CupidGb *gb, unsigned version) {
    const CupidGbApu *a = &gb->apu;
    const CupidGbPixels *p = &gb->pixels;
    const CupidGbAudioOutput *output = &a->output;
    if ((output->sample_rate && (output->sample_rate < 8000 || output->sample_rate > 192000)) ||
        p->dot >= 456 || p->line >= 154 || p->x > 160 || p->read >= 8 || p->count > 8 ||
        p->phase > 6 || p->column > 31 || p->discard > 7 || p->delay > 12 || p->address >= 0x4000 ||
        p->object_count > 10 || p->fetched_objects > 0x3ff || p->object_delay > 11 ||
        p->object_index >= 10 || p->object_address >= 0x3fff)
        return false;
    /* Map fetches read matching attributes from the second VRAM bank. A tile-data
     * address is valid in other fetch phases, but would cross VRAM's end here. */
    if (p->phase == 1 && (p->address < 0x1800 || p->address >= 0x2000))
        return false;
    for (unsigned i = 0; i < 8; ++i) {
        if (p->colors[i] > 3 || p->palettes[i] > 7 || p->priorities[i] > 1 ||
            p->sprite_colors[i] > 3 || p->sprite_palettes[i] > 7 || p->sprite_flags[i] > 1)
            return false;
    }
    for (unsigned i = 0; i < 10; ++i) {
        if (p->objects[i] >= 40)
            return false;
    }
    if (!cupid_gb_model_is_valid(gb->model) || (gb->cpu.f & 15u) || gb->cpu.ime_delay > 1 ||
        gb->div_counter >= 64 || gb->serial_bits_remaining > 8 ||
        (gb->serial_fast_clock && !cupid_gb_model_is_cgb(gb->model)) ||
        gb->tima_overflow_delay > 1 || gb->dma_index > 160 || gb->dma_start_delay > 2 ||
        gb->dma_restart_delay > 2 || gb->ppu_counter >= 114 || gb->ppu_lcd_warmup_lines > 2 ||
        gb->stat_irq_delay > 1 || gb->cgb_vram_bank > 1 || gb->cgb_wram_bank > 7 ||
        (gb->cgb_mode && !cupid_gb_model_is_cgb(gb->model)) ||
        (gb->double_speed && !cupid_gb_model_is_cgb(gb->model) &&
         !(gb->program_kind == CUPID_GB_PROGRAM_GBS && (gb->gbs_header[15] & 0x80u))) ||
        gb->mbc1_bank_low5 > 31 || gb->mbc1_bank_high2 > 3 || gb->mbc5_rom_bank > 511 ||
        gb->current_rom_bank > (version >= STATE_VERSION_V16 && gb->header.mbc_type == CUPID_GB_TPP1
                                    ? 65535u
                                    : 511u) ||
        gb->current_ram_bank > 255 ||
        (gb->boot_rom_enabled &&
         (!gb->boot_rom_size || gb->boot_rom_size > CUPID_GB_MAX_BOOT_ROM_SIZE))) {
        return false;
    }
    /* Bounds below protect every array index derived from restored APU/SGB state. RTC
     * register bytes and raw I/O remain unrestricted: the current bus accepts all byte values. */
    if (a->fs_step > 7 || a->fs_skip_div_event > 2 || a->ch1_duty > 3 || a->ch2_duty > 3 ||
        a->ch1_duty_pos > 7 || a->ch2_duty_pos > 7 || a->ch1_len > 64 || a->ch2_len > 64 ||
        a->ch3_len > 256 || a->ch4_len > 64 || a->ch1_vol > 15 || a->ch2_vol > 15 ||
        a->ch4_vol > 15 || a->ch1_env_period > 7 || a->ch2_env_period > 7 ||
        a->ch4_env_period > 7 || a->ch1_env_timer > 8 || a->ch2_env_timer > 8 ||
        a->ch4_env_timer > 8 || a->ch1_freq > 2047 || a->ch2_freq > 2047 || a->ch3_freq > 2047 ||
        a->ch3_active_freq > 2047 || a->ch1_sweep_shadow > 2047 || a->ch1_sweep_timer > 8 ||
        a->ch1_sweep_period > 7 || a->ch1_sweep_shift > 7 || a->ch3_out_level > 3 ||
        a->ch3_wave_pos > 31 || a->ch3_current_byte > 15 || a->ch3_wave_access_ticks > 1 ||
        a->ch3_sample > 15 || a->ch4_div_code > 7 || a->ch4_shift > 15 || a->ch4_lfsr > 0x7fff ||
        (version < STATE_VERSION_V14 && output->sample_acc >= cupid_gb_base_clock_hz(gb)) ||
        output->buf_write > CUPID_GB_APU_BUF_FRAMES || !gb->sgb.player_count ||
        gb->sgb.player_count > 4 ||
        (gb->sgb.player_count == 3 &&
         (version < STATE_VERSION_V4 || (gb->sgb.current_player & 1u))) ||
        gb->sgb.current_player >= gb->sgb.player_count || gb->sgb.mask_mode > 3 ||
        gb->sgb.command_write_index > sizeof(gb->sgb.command) * 8) {
        return false;
    }
    for (size_t i = 0; i < sizeof(gb->frame_buffer); ++i) {
        if (gb->frame_buffer[i] > 3 || gb->sgb.freeze_buffer[i] > 3) {
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(gb->sgb.attribute_map); ++i) {
        if (gb->sgb.attribute_map[i] > 3) {
            return false;
        }
    }
    return true;
}

static bool valid_hardware_extension(const CupidGb *gb) {
    const bool color = cupid_gb_model_is_cgb(gb->model);
    const bool early_cgb = cupid_gb_model_is_early_cgb(gb->model);
    const unsigned pulse_delay_limit = early_cgb ? 14u : 12u;
    const unsigned noise_delay_limit = early_cgb || !cupid_gb_model_is_cgb(gb->model) ? 38u : 34u;
    const bool rumble_cartridge = gb->header.mbc_type == CUPID_GB_MBC5 && gb->header.has_rumble;
    if ((rumble_cartridge && gb->current_ram_bank > 7u) ||
        (gb->mbc5_rumble_on && !rumble_cartridge)) {
        return false;
    }
    if (gb->hdma_blocks_remaining > 128 || gb->hdma_hblank_delay > 3 || gb->apu.clock_phase > 3 ||
        gb->apu.envelope_delay > (color ? 4u : 2u) ||
        (gb->apu.pcm_read_inhibit && (!early_cgb || (gb->apu.pcm_read_inhibit & 0x0f00u))) ||
        gb->apu.sweep_timing.startup_ticks > 3 || gb->apu.sweep_timing.shift_ticks > 7 ||
        gb->apu.sweep_timing.restart_hold > 8 || gb->apu.sweep_timing.delta > 2047 ||
        gb->apu.sweep_timing.completed_delta > 2047 || gb->apu.noise_timing.clock_phase > 7 ||
        gb->apu.noise_timing.remaining > noise_delay_limit ||
        gb->speed_switch_halt_cycles > 131080 || gb->speed_switch_toggle_cycles > 6 ||
        gb->speed_switch_freeze_cycles > 5 || gb->stop_divider_hold_cycles > 4 ||
        gb->speed_switch_alignment > 7 || (gb->hdma_hblank_delay && !gb->hdma_active) ||
        ((gb->hdma_pending || gb->hdma_active) && !gb->hdma_blocks_remaining)) {
        return false;
    }
    for (unsigned channel = 0; channel < 2; ++channel) {
        const CupidGbPulseTiming *timing = &gb->apu.pulse_timing[channel];
        if (timing->start_delay > pulse_delay_limit || timing->edge_age > 4 ||
            gb->apu.pulse_dac_hold[channel] > 15) {
            return false;
        }
    }
    return true;
}

static void import_noise_envelope(CupidGb *gb) {
    CupidGbApu *apu = &gb->apu;
    CupidGbEnvelopeGate *gate = &apu->noise_timing.envelope;
    *gate = (CupidGbEnvelopeGate){0};
    if (!cupid_gb_model_is_cgb(gb->model) || !apu->ch4_on)
        return;

    /* Former writers counted at phase 7. Project that outstanding count
     * through phase 6, and through the rising tap if it already passed. */
    apu->ch4_env_timer &= 7u;
    if (apu->fs_step != 7u)
        return;
    apu->ch4_env_timer = (uint8_t)((apu->ch4_env_timer - 1u) & 7u);
    const unsigned div_tap = 4u + gb->double_speed;
    if (apu->ch4_env_timer || !(gb->io_registers[4] & (1u << div_tap)))
        return;
    const uint8_t control = gb->io_registers[0x21];
    apu->ch4_env_timer = control & 7u;
    gate->clock = apu->ch4_env_timer != 0;
    gate->lock_pending = gate->clock && apu->ch4_vol == ((control & 8u) ? 15u : 0u);
}

static bool valid_hardware(const CupidGb *gb, unsigned sections, const CupidGbCamera *camera) {
    return valid_hardware_prefix(gb, STATE_VERSION_V24) && valid_hardware_extension(gb) &&
           cupid_gb_audio_state_valid(gb) && cupid_gb_sgb_startup_state_valid(gb) &&
           cupid_gb_sgb_protocol_state_valid(gb) && valid_v5_sections(gb, sections, camera) &&
           valid_infrared(gb) && cupid_gb_display_clock_state_valid(gb) &&
           cupid_gb_pixels_timing_valid(gb) && cupid_gb_vram_bus_state_valid(gb) &&
           gb->divider_subcycle <= 3 &&
           (gb->header.mbc_type != CUPID_GB_TPP1 || gb->tpp1.motor_strength <= 3);
}

static size_t ram_extension_size(const CupidGb *gb) {
    return STATE_RAM_LENGTH_SIZE + (gb->cartridge_ram_size > CUPID_GB_INLINE_RAM_SIZE
                                        ? gb->cartridge_ram_size - CUPID_GB_INLINE_RAM_SIZE
                                        : 0);
}

static void state_ram_extension(StateIo *io, CupidGb *gb) {
    uint32_t length = (uint32_t)gb->cartridge_ram_size;
    double_word(io, &length);
    if (length != gb->cartridge_ram_size) {
        io->invalid = true;
        return;
    }
    if (length > CUPID_GB_INLINE_RAM_SIZE)
        bytes(io, cupid_gb_cartridge_ram(gb) + CUPID_GB_INLINE_RAM_SIZE,
              length - CUPID_GB_INLINE_RAM_SIZE);
}

static size_t current_hardware_size(const CupidGb *gb, size_t *startup_length) {
    size_t startup = 0;
    if (!cupid_gb_sgb_startup_state_size(gb, &startup) || !cupid_gb_sgb_protocol_state_valid(gb))
        return 0;
    if (startup_length)
        *startup_length = startup;
    const unsigned sections = state_sections(gb);
    return extended_wire_size(sections) + STATE_V11_TAIL_SIZE + infrared_wire_size(sections) +
           STATE_CLOCK_HISTORY_SIZE + CUPID_GB_AUDIO_STATE_SIZE +
           CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
           CUPID_GB_VRAM_BUS_STATE_SIZE + STATE_INPUT_SAMPLE_SIZE + STATE_SERIAL_CLOCK_SIZE +
           STATE_WAVE_HISTORY_SIZE + ram_extension_size(gb) + startup +
           CUPID_GB_SGB_PROTOCOL_STATE_SIZE + (gb->mbc6_flash ? CUPID_GB_MBC6_STATE_SIZE : 0u) +
           (gb->header.mbc_type == CUPID_GB_TAMA5 ? CUPID_GB_TAMA5_STATE_SIZE : 0u) +
           cupid_gb_extra_oam_size(gb->model) +
           (cupid_gb_model_is_early_cgb(gb->model) ? STATE_PCM_READ_SIZE : 0u);
}

size_t cupid_gb_state_size(const CupidGb *gb) {
    if (!ready(gb))
        return 0;
    const size_t hardware = current_hardware_size(gb, NULL);
    return hardware ? hardware + STATE_ACCESSORY_DESCRIPTOR_SIZE +
                          (cupid_gb_printer_attached(gb) ? PRINTER_STATE_WIRE_SIZE : 0)
                    : 0;
}

static void put_u32(uint8_t *data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        data[i] = (uint8_t)(value >> (i * 8));
    }
}

static uint32_t get_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

/* The caller supplies a geometry-derived offset. A descriptor cannot move itself
 * by declaring another payload length or leave unparsed bytes after the device. */
static size_t accessory_wire_size(const uint8_t *data, size_t length, size_t offset,
                                  const uint8_t **printer_payload) {
    if (offset > length || length - offset < STATE_ACCESSORY_DESCRIPTOR_SIZE)
        return 0;
    const uint8_t *accessory = data + offset;
    const uint32_t payload_size = get_u32(accessory + 4);
    if (accessory[2] || accessory[3])
        return 0;
    if (accessory[0] == CUPID_GB_SERIAL_DEVICE_NONE) {
        if (accessory[1] || payload_size)
            return 0;
    } else if (accessory[0] == CUPID_GB_SERIAL_DEVICE_PRINTER) {
        if (accessory[1] != PRINTER_STATE_CODEC_VERSION || payload_size != PRINTER_STATE_WIRE_SIZE)
            return 0;
    } else {
        return 0;
    }
    const size_t size = STATE_ACCESSORY_DESCRIPTOR_SIZE + (size_t)payload_size;
    if (length - offset != size)
        return 0;
    if (printer_payload)
        *printer_payload = payload_size ? accessory + STATE_ACCESSORY_DESCRIPTOR_SIZE : NULL;
    return size;
}

static void integrity(const uint8_t *data, size_t length, uint8_t digest[32]) {
    cupid_sha256_parts(data, 96, data + STATE_HEADER_SIZE, length - STATE_HEADER_SIZE, digest);
}

static bool overlaps(const void *a, size_t a_size, const void *b, size_t b_size) {
    if (!a_size || !b_size)
        return false;
    uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;
    return first <= second ? second - first < a_size : first - second < b_size;
}

static bool overlaps_live_storage(const CupidGb *gb, const void *data, size_t size) {
    if (!gb || !data)
        return false;
    const CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
    return overlaps(data, size, gb, sizeof(*gb)) ||
           (gb->rom && overlaps(data, size, gb->rom, gb->rom_capacity)) ||
           (gb->cartridge_ram_heap &&
            overlaps(data, size, gb->cartridge_ram_heap, gb->cartridge_ram_capacity)) ||
           (gb->mbc6_flash && overlaps(data, size, gb->mbc6_flash, CUPID_GB_MBC6_STORAGE_SIZE)) ||
           (printer && overlaps(data, size, printer, sizeof(*printer)));
}

static bool valid_written_pointer(const CupidGb *gb, const uint8_t *data, size_t capacity,
                                  const size_t *written, const CupidGbStateContext *context) {
    return !written ||
           (!overlaps_live_storage(gb, written, sizeof(*written)) &&
            (!data || !overlaps(written, sizeof(*written), data, capacity)) &&
            (!context || !overlaps(written, sizeof(*written), context, sizeof(*context))));
}

static CupidGbStateResult save_state(const CupidGb *gb, uint8_t *data, size_t capacity,
                                     size_t *written, const uint8_t *rom_digest) {
    if (!valid_written_pointer(gb, data, capacity, written, NULL))
        return CUPID_GB_STATE_INVALID;
    if (written) {
        *written = 0;
    }
    if (!ready(gb)) {
        return CUPID_GB_STATE_NO_ROM;
    }
    const unsigned sections = state_sections(gb);
    size_t startup_size = 0;
    const size_t hardware_size = current_hardware_size(gb, &startup_size);
    if (!hardware_size)
        return CUPID_GB_STATE_INVALID;
    const CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
    const size_t size =
        hardware_size + STATE_ACCESSORY_DESCRIPTOR_SIZE + (printer ? PRINTER_STATE_WIRE_SIZE : 0);
    if (!data || capacity < size) {
        return CUPID_GB_STATE_BUFFER_TOO_SMALL;
    }
    CupidGbCamera camera = gb->camera;
    if (overlaps_live_storage(gb, data, size) ||
        ((sections & STATE_SECTION_CAMERA) && !camera_for_snapshot(gb, &camera)) ||
        !valid_hardware(gb, sections, &camera) ||
        (gb->mbc6_flash && !cupid_gb_mbc6_state_valid(gb)) ||
        (gb->header.mbc_type == CUPID_GB_TAMA5 && !cupid_gb_tama5_state_valid(gb)) ||
        (printer && !cupid_gb_printer_state_valid(&printer->state, gb))) {
        return CUPID_GB_STATE_INVALID;
    }
    memset(data, 0, STATE_HEADER_SIZE);
    memcpy(data, state_magic, sizeof(state_magic));
    put_u32(data + 8, STATE_VERSION_V30);
    put_u32(data + 12, (uint32_t)size);
    put_u32(data + 16, (uint32_t)gb->model);
    put_u32(data + 20, (uint32_t)gb->rom_size);
    put_u32(data + 24, gb->boot_rom_enabled ? (uint32_t)gb->boot_rom_size : 0);
    put_u32(data + 28, (gb->boot_rom_enabled ? 1u : 0u) |
                           (gb->program_kind == CUPID_GB_PROGRAM_GBS ? 2u : 0u));
    if (rom_digest) {
        memcpy(data + 32, rom_digest, 32);
    } else {
        cupid_gb_program_digest(gb, data + 32);
    }
    if (gb->boot_rom_enabled) {
        cupid_sha256(gb->boot_rom, gb->boot_rom_size, data + 64);
    }
    StateIo io = {NULL, data, STATE_HEADER_SIZE, hardware_size, false};
    state_fields(&io, (CupidGb *)gb, STATE_VERSION_V30);
    state_extension_v3(&io, (CupidGb *)gb);
    state_extension_v4(&io, (CupidGb *)gb);
    state_extension_v5(&io, (CupidGb *)gb, sections, &camera);
    state_lcd_phase(&io, (CupidGb *)gb);
    state_dma_bus(&io, (CupidGb *)gb, STATE_VERSION_V24);
    uint8_t dma_phase = gb->dma_active || gb->dma_start_delay ? gb->dma_phase : 0;
    bytes(&io, &dma_phase, 1);
    if (gb->dma_phase > 3u)
        io.invalid = true;
    state_input_and_display(&io, (CupidGb *)gb);
    state_video_bus(&io, (CupidGb *)gb, STATE_VERSION_V24);
    state_stop_ports(&io, (CupidGb *)gb);
    bytes(&io, ((CupidGb *)gb)->apu.pulse_dac_hold, 2);
    if (!(sections & STATE_SECTION_HUC3))
        state_infrared(&io, (CupidGb *)gb);
    state_clock_history(&io, (CupidGb *)gb);
    if (!reserve(&io, CUPID_GB_AUDIO_STATE_SIZE) ||
        !cupid_gb_audio_state_encode(gb, data + io.offset, CUPID_GB_AUDIO_STATE_SIZE))
        io.invalid = true;
    io.offset += CUPID_GB_AUDIO_STATE_SIZE;
    if (!reserve(&io, CUPID_GB_DISPLAY_CLOCK_STATE_SIZE) ||
        !cupid_gb_display_clock_state_encode(gb, data + io.offset,
                                             CUPID_GB_DISPLAY_CLOCK_STATE_SIZE))
        io.invalid = true;
    io.offset += CUPID_GB_DISPLAY_CLOCK_STATE_SIZE;
    if (!reserve(&io, CUPID_GB_PIXEL_TIMING_STATE_SIZE) ||
        !cupid_gb_pixels_timing_encode(gb, data + io.offset, CUPID_GB_PIXEL_TIMING_STATE_SIZE))
        io.invalid = true;
    io.offset += CUPID_GB_PIXEL_TIMING_STATE_SIZE;
    if (!reserve(&io, CUPID_GB_VRAM_BUS_STATE_SIZE) ||
        !cupid_gb_vram_bus_state_encode(gb, data + io.offset, CUPID_GB_VRAM_BUS_STATE_SIZE))
        io.invalid = true;
    io.offset += CUPID_GB_VRAM_BUS_STATE_SIZE;
    boolean(&io, &((CupidGb *)gb)->joyp_sample_pending);
    boolean(&io, &((CupidGb *)gb)->serial_fast_clock);
    bytes(&io, &((CupidGb *)gb)->apu.ch3_sample_byte, STATE_WAVE_HISTORY_SIZE);
    state_ram_extension(&io, (CupidGb *)gb);
    if (!reserve(&io, startup_size) ||
        !cupid_gb_sgb_startup_state_encode(gb, data + io.offset, startup_size))
        io.invalid = true;
    io.offset += startup_size;
    if (!reserve(&io, CUPID_GB_SGB_PROTOCOL_STATE_SIZE) ||
        !cupid_gb_sgb_protocol_state_encode(gb, data + io.offset, CUPID_GB_SGB_PROTOCOL_STATE_SIZE))
        io.invalid = true;
    io.offset += CUPID_GB_SGB_PROTOCOL_STATE_SIZE;
    if (gb->mbc6_flash)
        cupid_gb_mbc6_state(&io, (CupidGb *)gb);
    if (gb->header.mbc_type == CUPID_GB_TAMA5)
        cupid_gb_tama5_state(&io, (CupidGb *)gb);
    bytes(&io, ((CupidGb *)gb)->extra_oam, cupid_gb_extra_oam_size(gb->model));
    if (cupid_gb_model_is_early_cgb(gb->model))
        word(&io, &((CupidGb *)gb)->apu.pcm_read_inhibit);
    if (io.invalid || io.offset != hardware_size) {
        return CUPID_GB_STATE_INVALID;
    }
    uint8_t *accessory = data + hardware_size;
    memset(accessory, 0, STATE_ACCESSORY_DESCRIPTOR_SIZE);
    if (printer) {
        accessory[0] = CUPID_GB_SERIAL_DEVICE_PRINTER;
        accessory[1] = PRINTER_STATE_CODEC_VERSION;
        put_u32(accessory + 4, PRINTER_STATE_WIRE_SIZE);
        /* Capture is pure: the device may owe elapsed cycles to a later ordinary
         * update. Advancing it here could emit completion while saving. */
        if (!cupid_gb_printer_state_encode(&printer->state,
                                           accessory + STATE_ACCESSORY_DESCRIPTOR_SIZE,
                                           PRINTER_STATE_WIRE_SIZE))
            return CUPID_GB_STATE_INVALID;
    }
    integrity(data, size, data + 96);
    if (written) {
        *written = size;
    }
    return CUPID_GB_STATE_OK;
}

static void import_legacy_sgb_startup(CupidGb *gb) {
    const CupidGbCpu *cpu = &gb->cpu;
    const CupidGbPixels *pixels = &gb->pixels;
    /* Older writers could save the untouched SGB HLE profile with LY=FF and
     * an uninitialized pixel pipeline. Its first tick then copied FF into the
     * internal scanline. Recognize that pre-execution profile, not arbitrary
     * raw LY bytes or an already-running display, and restore the valid HLE
     * VBlank phase. The DIV subphase depends on the cartridge header packet
     * stream, so retain that validated value. No memory or host configuration
     * belongs to this migration. */
    if (!cupid_gb_model_is_sgb(gb->model) || gb->boot_rom_enabled || gb->elapsed_cycles ||
        cpu->a != (gb->model == CUPID_GB_MODEL_SGB2 ? 0xffu : 0x01u) || cpu->f || cpu->b ||
        cpu->c != 0x14u || cpu->d || cpu->e || cpu->h != 0xc0u || cpu->l != 0x60u ||
        cpu->sp != 0xfffeu || cpu->pc != CUPID_GB_ENTRY_POINT || cpu->ime || cpu->ime_delay ||
        cpu->halt_bug || cpu->halted || cpu->just_halted || cpu->stopped || gb->interrupt_enable ||
        gb->interrupt_flags != 1u || gb->io_registers[0x04] != 0xd8u || gb->timer_counter ||
        gb->ppu_counter || gb->io_registers[CUPID_GB_IO_LCDC] != 0x91u ||
        gb->io_registers[CUPID_GB_IO_STAT] != 0 || gb->io_registers[CUPID_GB_IO_LY] != 0xffu ||
        gb->ppu_lcd_startup || gb->ppu_lcd_warmup_lines || gb->ppu_line_boundary_hold ||
        gb->stat_irq_delay || gb->stat_irq_line || gb->window_line_counter || gb->frame_ready ||
        pixels->initialized || pixels->active || pixels->line || pixels->dot || pixels->x ||
        pixels->read || pixels->count || pixels->phase || pixels->object_count ||
        pixels->object_delay || pixels->window_armed || pixels->window_active ||
        pixels->window_used || pixels->lyc_irq)
        return;
    gb->io_registers[CUPID_GB_IO_LY] = 144;
    gb->io_registers[CUPID_GB_IO_STAT] = 0x81;
    gb->ppu_counter = 50;
}

static void discard_candidate(CupidGb *candidate) {
    /* The candidate owns writable storage but borrows ROM and host attachments. */
    free(candidate->cartridge_ram_heap);
    free(candidate->mbc6_flash);
    free(candidate);
}

static CupidGbStateResult load_state(CupidGb *gb, const uint8_t *data, size_t length,
                                     const uint8_t *rom_digest, bool live_audio,
                                     void *(*allocate)(size_t)) {
    if (!allocate)
        return CUPID_GB_STATE_INVALID;
    if (!ready(gb)) {
        return CUPID_GB_STATE_NO_ROM;
    }
    if (live_audio && (!cupid_gb_audio_state_valid(gb) || !cupid_gb_sgb_startup_output_valid(gb)))
        return CUPID_GB_STATE_INVALID;
    CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
    if (!data || !cupid_gb_cartridge_source_fits(gb, data, length) ||
        (printer && overlaps(data, length, printer, sizeof(*printer))) ||
        length < STATE_HEADER_SIZE || memcmp(data, state_magic, sizeof(state_magic))) {
        return CUPID_GB_STATE_INVALID;
    }
    const uint32_t version = get_u32(data + 8);
    if (version < STATE_VERSION_V2 || version > STATE_VERSION_V30 ||
        (gb->header.mbc_type == CUPID_GB_TAMA5 && version < STATE_VERSION_V26) ||
        (version < STATE_VERSION_V19 && gb->cartridge_ram_size > CUPID_GB_INLINE_RAM_SIZE)) {
        return CUPID_GB_STATE_UNSUPPORTED_VERSION;
    }
    const size_t infrared_size =
        version >= STATE_VERSION_V12 ? infrared_wire_size(state_sections(gb)) : 0;
    const size_t clock_size = version >= STATE_VERSION_V13 ? STATE_CLOCK_HISTORY_SIZE : 0;
    const size_t audio_size = version >= STATE_VERSION_V14 ? CUPID_GB_AUDIO_STATE_SIZE : 0;
    const size_t display_clock_size =
        version >= STATE_VERSION_V15 ? CUPID_GB_DISPLAY_CLOCK_STATE_SIZE : 0;
    const size_t pixel_timing_size =
        version >= STATE_VERSION_V15 ? CUPID_GB_PIXEL_TIMING_STATE_SIZE : 0;
    const size_t vram_bus_size = version >= STATE_VERSION_V15 ? CUPID_GB_VRAM_BUS_STATE_SIZE : 0;
    const size_t input_sample_size = version >= STATE_VERSION_V16 ? STATE_INPUT_SAMPLE_SIZE : 0;
    const size_t serial_clock_size = version >= STATE_VERSION_V18 ? STATE_SERIAL_CLOCK_SIZE : 0;
    const size_t ram_capacity = ram_extension_size(gb);
    size_t ram_size = version >= STATE_VERSION_V20 ? ram_capacity : 0;
    size_t wave_history_size = version >= STATE_VERSION_V20 ? STATE_WAVE_HISTORY_SIZE : 0;
    /* Coarse bounds precede model identity. A non-adapter file must still reach
     * WRONG_MODEL on an adapter receiver, with strict sizing after that check. */
    const size_t startup_maximum =
        version >= STATE_VERSION_V21 ? CUPID_GB_STARTUP_STATE_MAX_SIZE : 0;
    size_t startup_size = 0;
    const size_t protocol_size =
        version >= STATE_VERSION_V22 ? CUPID_GB_SGB_PROTOCOL_STATE_SIZE : 0;
    const size_t mbc6_size =
        version >= STATE_VERSION_V25 && gb->mbc6_flash ? CUPID_GB_MBC6_STATE_SIZE : 0;
    const size_t tama5_size = gb->header.mbc_type == CUPID_GB_TAMA5 ? CUPID_GB_TAMA5_STATE_SIZE : 0;
    const size_t extra_oam_size = cupid_gb_extra_oam_size(gb->model);
    const size_t pcm_read_size =
        version >= STATE_VERSION_V28 && cupid_gb_model_is_early_cgb(gb->model) ? STATE_PCM_READ_SIZE
                                                                               : 0;
    /* Version 19 had two complete layouts. Bound the input before hashing, then
     * choose one by exact geometry and descriptor validation below. */
    const size_t minimum_history =
        version == STATE_VERSION_V19
            ? (gb->cartridge_ram_size > CUPID_GB_INLINE_RAM_SIZE ? ram_capacity
                                                                 : STATE_WAVE_HISTORY_SIZE)
            : ram_size + wave_history_size;
    const size_t maximum_history =
        version == STATE_VERSION_V19 ? ram_capacity : ram_size + wave_history_size;
    const size_t prefix_size = version == STATE_VERSION_V2   ? STATE_WIRE_SIZE_V2
                               : version == STATE_VERSION_V3 ? STATE_WIRE_SIZE_V3
                                                             : STATE_WIRE_SIZE_V4;
    const size_t tail_size = version >= STATE_VERSION_V11   ? STATE_V11_TAIL_SIZE
                             : version == STATE_VERSION_V10 ? STATE_V10_TAIL_SIZE
                             : version == STATE_VERSION_V9  ? STATE_V9_TAIL_SIZE
                             : version == STATE_VERSION_V8  ? STATE_V8_TAIL_SIZE
                             : version == STATE_VERSION_V7  ? STATE_V7_TAIL_SIZE
                             : version == STATE_VERSION_V6  ? STATE_V6_TAIL_SIZE
                                                            : 0;
    size_t device_size = length - tail_size;
    if (length != get_u32(data + 12))
        return CUPID_GB_STATE_INVALID;
    if (version >= STATE_VERSION_V12) {
        if (length < prefix_size + tail_size + infrared_size + clock_size + audio_size +
                         display_clock_size + pixel_timing_size + vram_bus_size +
                         input_sample_size + serial_clock_size + minimum_history + protocol_size +
                         STATE_ACCESSORY_DESCRIPTOR_SIZE ||
            length > extended_wire_size(STATE_SECTIONS_ALL) + tail_size + STATE_INFRARED_SIZE +
                         clock_size + audio_size + display_clock_size + pixel_timing_size +
                         vram_bus_size + input_sample_size + serial_clock_size + maximum_history +
                         startup_maximum + protocol_size + STATE_ACCESSORY_DESCRIPTOR_SIZE +
                         PRINTER_STATE_WIRE_SIZE +
                         (version >= STATE_VERSION_V25 ? CUPID_GB_MBC6_STATE_SIZE : 0) +
                         (version >= STATE_VERSION_V27 ? sizeof(gb->extra_oam) : 0) +
                         (version >= STATE_VERSION_V28 ? STATE_PCM_READ_SIZE : 0))
            return CUPID_GB_STATE_INVALID;
    } else if (version >= STATE_VERSION_V5
                   ? device_size < prefix_size ||
                         device_size > extended_wire_size(STATE_SECTIONS_ALL)
                   : device_size != prefix_size) {
        return CUPID_GB_STATE_INVALID;
    }
    uint8_t digest[32];
    integrity(data, length, digest);
    if (memcmp(digest, data + 96, sizeof(digest))) {
        return CUPID_GB_STATE_INVALID;
    }
    if (get_u32(data + 16) != (uint32_t)gb->model) {
        return CUPID_GB_STATE_WRONG_MODEL;
    }
    if (version < STATE_VERSION_V27 && extra_oam_size)
        return CUPID_GB_STATE_UNSUPPORTED_VERSION;
    const uint32_t flags = get_u32(data + 28);
    if (flags & ~(version >= STATE_VERSION_V23 ? 3u : 1u))
        return CUPID_GB_STATE_INVALID;
    /* The loaded program owns this permission. An incoming flag or an ordinary
     * cartridge's payload can never enable music-specific hardware semantics. */
    if (((flags & 2u) != 0) != (gb->program_kind == CUPID_GB_PROGRAM_GBS) ||
        (gb->program_kind == CUPID_GB_PROGRAM_GBS && version < STATE_VERSION_V23))
        return CUPID_GB_STATE_WRONG_ROM;
    if (get_u32(data + 20) != gb->rom_size) {
        return CUPID_GB_STATE_WRONG_ROM;
    }
    if (rom_digest) {
        memcpy(digest, rom_digest, sizeof(digest));
    } else {
        cupid_gb_program_digest(gb, digest);
    }
    if (memcmp(digest, data + 32, sizeof(digest))) {
        return CUPID_GB_STATE_WRONG_ROM;
    }
    unsigned sections = version >= STATE_VERSION_V5 ? state_sections(gb) : 0;
    size_t accessory_size = 0;
    const uint8_t *printer_payload = NULL;
    if (version >= STATE_VERSION_V12) {
        device_size = extended_wire_size(sections);
        const size_t history_offset = device_size + tail_size + infrared_size + clock_size +
                                      audio_size + display_clock_size + pixel_timing_size +
                                      vram_bus_size + input_sample_size + serial_clock_size;
        if (version == STATE_VERSION_V19) {
            const bool wave_layout =
                gb->cartridge_ram_size <= CUPID_GB_INLINE_RAM_SIZE &&
                accessory_wire_size(data, length, history_offset + STATE_WAVE_HISTORY_SIZE, NULL);
            const bool ram_layout =
                accessory_wire_size(data, length, history_offset + ram_capacity, NULL) &&
                get_u32(data + history_offset) == gb->cartridge_ram_size;
            /* The earlier wave writer could not load larger cartridges.
             * Never infer a missing SRAM tail from the receiving timeline. */
            if (wave_layout == ram_layout)
                return CUPID_GB_STATE_INVALID;
            wave_history_size = wave_layout ? STATE_WAVE_HISTORY_SIZE : 0;
            ram_size = ram_layout ? ram_capacity : 0;
        }
        const size_t startup_offset = history_offset + wave_history_size + ram_size;
        if (startup_offset > length ||
            (version >= STATE_VERSION_V21 &&
             !cupid_gb_sgb_startup_state_measure(gb, data + startup_offset, length - startup_offset,
                                                 &startup_size)))
            return CUPID_GB_STATE_INVALID;
        const size_t protocol_offset = startup_offset + startup_size;
        if (protocol_offset > length || protocol_size > length - protocol_offset ||
            (protocol_size &&
             !cupid_gb_sgb_protocol_wire_valid(gb, data + protocol_offset, protocol_size)))
            return CUPID_GB_STATE_INVALID;
        const size_t accessory_offset = protocol_offset + protocol_size + mbc6_size + tama5_size +
                                        extra_oam_size + pcm_read_size;
        accessory_size = accessory_wire_size(data, length, accessory_offset, &printer_payload);
        if (!accessory_size)
            return CUPID_GB_STATE_INVALID;
        /* Geometry comes from the receiving cartridge. Check it before any
         * allocation or decode; a forged count cannot move the accessory. */
        if (ram_size &&
            get_u32(data + history_offset + wave_history_size) != gb->cartridge_ram_size)
            return CUPID_GB_STATE_INVALID;
    }
    /* Older SGB states already used this version for the border hardware. */
    if (version == STATE_VERSION_V5 && sections == (STATE_SECTION_SGB | STATE_SECTION_HUC3) &&
        device_size == extended_wire_size(STATE_SECTION_SGB)) {
        sections = STATE_SECTION_SGB;
    }
    if ((version == STATE_VERSION_V5 && !sections) ||
        (version >= STATE_VERSION_V5 && device_size != extended_wire_size(sections))) {
        return CUPID_GB_STATE_INVALID;
    }
    const uint32_t boot_length = get_u32(data + 24);
    if (!(flags & 1u) && boot_length) {
        return CUPID_GB_STATE_INVALID;
    }
    if (flags & 1u) {
        if (!boot_length || boot_length > CUPID_GB_MAX_BOOT_ROM_SIZE ||
            boot_length != gb->boot_rom_size) {
            return CUPID_GB_STATE_WRONG_BOOT_ROM;
        }
        cupid_sha256(gb->boot_rom, gb->boot_rom_size, digest);
        if (memcmp(digest, data + 64, sizeof(digest))) {
            return CUPID_GB_STATE_WRONG_BOOT_ROM;
        }
    } else {
        const uint8_t empty[32] = {0};
        if (memcmp(data + 64, empty, sizeof(empty))) {
            return CUPID_GB_STATE_INVALID;
        }
    }
    CupidGb *candidate = allocate(sizeof(*candidate));
    if (!candidate) {
        return CUPID_GB_STATE_OUT_OF_MEMORY;
    }
    /* Host resources, cartridge metadata, and immutable inputs come only from the live
     * instance. Decode into separate storage and never run bus writes while loading. */
    *candidate = *gb;
    candidate->cartridge_ram_heap = NULL;
    candidate->mbc6_flash = NULL;
    if (gb->cartridge_ram_heap) {
        candidate->cartridge_ram_heap = allocate(gb->cartridge_ram_capacity);
        if (!candidate->cartridge_ram_heap) {
            discard_candidate(candidate);
            return CUPID_GB_STATE_OUT_OF_MEMORY;
        }
    }
    if (gb->mbc6_flash) {
        candidate->mbc6_flash = allocate(CUPID_GB_MBC6_STORAGE_SIZE);
        if (!candidate->mbc6_flash) {
            discard_candidate(candidate);
            return CUPID_GB_STATE_OUT_OF_MEMORY;
        }
        memset(&candidate->mbc6, 0, sizeof(candidate->mbc6));
    }
    /* Absent legacy output fields must never borrow an unrelated live timeline. */
    memset(&candidate->apu.output, 0, sizeof(candidate->apu.output));
    memset(candidate->apu.dac_sample, 0, sizeof(candidate->apu.dac_sample));
    candidate->apu.pending_cycles = 0;
    candidate->serial_fast_clock = false;
    candidate->apu.ch3_sample_byte = 0;
    cupid_gb_sgb_startup_state_import_legacy(candidate);
    const size_t hardware_length = length - accessory_size;
    StateIo io = {data, NULL, STATE_HEADER_SIZE, hardware_length, false};
    state_fields(&io, candidate, version);
    if (version >= STATE_VERSION_V3) {
        state_extension_v3(&io, candidate);
    }
    if (version >= STATE_VERSION_V4) {
        state_extension_v4(&io, candidate);
    } else {
        /* Legacy writers discarded the motor bit instead of storing its history.
         * Missing controllers start released; missing motor history starts off. */
        memset(candidate->joypad_extra, 0xff, sizeof(candidate->joypad_extra));
        candidate->mbc5_rumble_on = false;
    }
    state_extension_v5(&io, candidate, sections, &candidate->camera);
    if (version >= STATE_VERSION_V6) {
        state_lcd_phase(&io, candidate);
        state_dma_bus(&io, candidate, version);
        if (version < STATE_VERSION_V8 && !cupid_gb_model_is_cgb(candidate->model) &&
            candidate->cartridge_bus_expiry)
            io.invalid = true;
    } else {
        candidate->cartridge_bus_expiry = 0;
        candidate->cartridge_bus_data = 0xffu;
        candidate->lcd_frame_odd = false;
        candidate->lcd_first_frame =
            cupid_gb_model_is_cgb(candidate->model) && candidate->ppu_lcd_startup;
    }
    if (version >= STATE_VERSION_V7) {
        bytes(&io, &candidate->dma_phase, 1);
        if (candidate->dma_phase > 3u ||
            (!candidate->dma_active && !candidate->dma_start_delay && candidate->dma_phase))
            io.invalid = true;
    } else {
        candidate->dma_phase = 2;
    }
    if (version >= STATE_VERSION_V8) {
        state_input_and_display(&io, candidate);
    } else {
        /* Older payloads have no row-release or presentation-event history. */
        candidate->joyp_select_delay = 0;
        candidate->joyp_select_value = 0;
        candidate->display_cycles_since_frame = 0;
        candidate->lcd_repeat_countdown = 0;
        candidate->display_frame_type = CUPID_GB_FRAME_NORMAL;
    }
    if (version >= STATE_VERSION_V9) {
        state_video_bus(&io, candidate, version);
    } else {
        /* Absent bus history belongs to neither the file nor the live session. */
        candidate->oam_vram_row = UINT16_MAX;
        candidate->oam_search_word = 0;
        candidate->stop_ppu_vram_blocked = false;
        /* Finish an older in-flight object using its captured address/countdown. */
        candidate->object_fetch_stage =
            candidate->pixels.object_delay ? CUPID_GB_OBJECT_LEGACY : CUPID_GB_OBJECT_IDLE;
    }
    if (version >= STATE_VERSION_V10) {
        state_stop_ports(&io, candidate);
    } else {
        /* Older files lack STOP OAM/palette ownership and the last-pixel edge.
         * Clear absent history rather than borrowing it from the live session. */
        candidate->stop_ppu_oam_blocked = false;
        candidate->stop_ppu_palette_blocked = false;
        candidate->ppu_transfer_end_dot = 0;
    }
    if (version >= STATE_VERSION_V11) {
        bytes(&io, candidate->apu.pulse_dac_hold, 2);
    } else {
        /* Older writers did not retain disconnected DAC history. */
        memset(candidate->apu.pulse_dac_hold, 0, sizeof(candidate->apu.pulse_dac_hold));
    }
    if (!(sections & STATE_SECTION_HUC3)) {
        if (version >= STATE_VERSION_V12) {
            state_infrared(&io, candidate);
        } else {
            /* Older non-HuC3 files contain no receiver history. Preserve the
             * live transport input, but do not borrow charge from its old timeline. */
            candidate->huc3.ir_warmup = 0;
            candidate->huc3.ir_decay = 0;
            candidate->huc3.ir_output = false;
            candidate->huc3.ir_detected = false;
        }
    }
    if (version >= STATE_VERSION_V13) {
        state_clock_history(&io, candidate);
    } else {
        /* The old TPP1 section combined literal MR4 bits with motor history.
         * Missing sections cannot inherit either phase or motor from the receiver. */
        candidate->rtc_cycles = 0;
        candidate->divider_subcycle = 0;
        candidate->tpp1.motor_strength =
            sections & STATE_SECTION_TPP1 ? (uint8_t)(candidate->tpp1.status & 3u) : 0;
    }
    if (audio_size) {
        if (!reserve(&io, audio_size) ||
            !cupid_gb_audio_state_decode(candidate, data + io.offset, audio_size,
                                         version >= STATE_VERSION_V30))
            io.invalid = true;
        io.offset += audio_size;
    }
    if (display_clock_size) {
        if (!reserve(&io, display_clock_size) ||
            !cupid_gb_display_clock_state_decode(candidate, data + io.offset, display_clock_size))
            io.invalid = true;
        io.offset += display_clock_size;
    }
    if (pixel_timing_size) {
        if (!reserve(&io, pixel_timing_size) ||
            (version < STATE_VERSION_V17 && data[io.offset + 3]) ||
            !cupid_gb_pixels_timing_decode(candidate, data + io.offset, pixel_timing_size))
            io.invalid = true;
        io.offset += pixel_timing_size;
    }
    if (vram_bus_size) {
        if (!reserve(&io, vram_bus_size) ||
            !cupid_gb_vram_bus_state_decode(candidate, data + io.offset, vram_bus_size))
            io.invalid = true;
        io.offset += vram_bus_size;
    }
    if (input_sample_size)
        boolean(&io, &candidate->joyp_sample_pending);
    if (serial_clock_size) {
        boolean(&io, &candidate->serial_fast_clock);
    } else {
        /* Old files recorded phase and control, but selected the tap from the
         * current mode. Reconstruct that selection without borrowing live history
         * or changing the literal SC byte to its formerly computed readback. */
        candidate->serial_fast_clock = candidate->cgb_mode && (candidate->io_registers[2] & 2u);
    }
    if (wave_history_size)
        bytes(&io, &candidate->apu.ch3_sample_byte, wave_history_size);
    if (ram_size)
        state_ram_extension(&io, candidate);
    if (version >= STATE_VERSION_V21) {
        if (!reserve(&io, startup_size) ||
            !cupid_gb_sgb_startup_state_decode(candidate, data + io.offset, startup_size))
            io.invalid = true;
        io.offset += startup_size;
    }
    if (protocol_size) {
        if (!reserve(&io, protocol_size) ||
            !cupid_gb_sgb_protocol_state_decode(candidate, data + io.offset, protocol_size))
            io.invalid = true;
        io.offset += protocol_size;
    } else {
        cupid_gb_sgb_protocol_state_import_legacy(candidate);
    }
    if (mbc6_size) {
        cupid_gb_mbc6_state(&io, candidate);
    } else if (candidate->mbc6_flash) {
        /* The old controller mapped single 16 KiB ROM and 8 KiB RAM windows.
         * Reconstruct their halves. Those states cannot supply a flash image. */
        const uint8_t rom = (uint8_t)(candidate->current_rom_bank * 2u);
        const uint8_t ram = (uint8_t)((candidate->current_ram_bank & 3u) * 2u);
        candidate->mbc6.rom_bank[0] = rom;
        candidate->mbc6.rom_bank[1] = rom + 1u;
        candidate->mbc6.ram_bank[0] = ram;
        candidate->mbc6.ram_bank[1] = ram + 1u;
        memset(candidate->mbc6_flash, 0xff, CUPID_GB_MBC6_STORAGE_SIZE);
    }
    if (tama5_size)
        cupid_gb_tama5_state(&io, candidate);
    bytes(&io, candidate->extra_oam, extra_oam_size);
    if (pcm_read_size)
        word(&io, &candidate->apu.pcm_read_inhibit);
    else
        candidate->apu.pcm_read_inhibit = 0;
    if (io.invalid || io.offset != hardware_length ||
        candidate->boot_rom_enabled != ((flags & 1u) != 0) ||
        !valid_hardware_prefix(candidate, version) ||
        !valid_v5_sections(candidate, sections, &candidate->camera) || !valid_infrared(candidate)) {
        discard_candidate(candidate);
        return CUPID_GB_STATE_INVALID;
    }
    if (!wave_history_size) {
        /* Files without fetch history retain one raw nibble. Duplicate that
         * validated sample without consulting RAM or the receiving core. */
        candidate->apu.ch3_sample_byte = (uint8_t)(candidate->apu.ch3_sample * 0x11u);
    }
    /* Older payloads retained whole-M-cycle counters. Restore a deterministic
     * source-slot phase and project pending restarts into the new warmup state. */
    if (version < STATE_VERSION_V7) {
        if (candidate->dma_restart_pending) {
            candidate->dma_source_base = candidate->dma_restart_source_base;
            candidate->dma_index = 0;
            candidate->dma_start_delay = 1;
            candidate->dma_active = false;
            candidate->dma_restart_delay = 0;
        } else if (candidate->dma_start_delay) {
            candidate->dma_start_delay = 1;
            candidate->dma_active = false;
        }
    }
    if (!(sections & STATE_SECTION_SGB)) {
        import_legacy_sgb(candidate);
    }
    if (candidate->header.mbc_type == CUPID_GB_MMM01 && !(sections & STATE_SECTION_MMM01)) {
        import_legacy_mmm01(candidate);
    }
    if (candidate->header.mbc_type == CUPID_GB_MBC7 && !(sections & STATE_SECTION_MBC7)) {
        cupid_gb_mbc7_reset_serial(candidate);
    }
    if (candidate->header.mbc_type == CUPID_GB_CAMERA && !(sections & STATE_SECTION_CAMERA)) {
        /* Older builds opened Camera cartridges with ROM-only mapping. */
        memset(&candidate->camera, 0, sizeof(candidate->camera));
        candidate->current_rom_bank = 1;
        candidate->current_ram_bank = 0;
        candidate->ram_enabled = false;
    }
    if (candidate->header.mbc_type == CUPID_GB_TPP1 && !(sections & STATE_SECTION_TPP1)) {
        candidate->tpp1 = (CupidGbTpp1){.rom_bank = 1};
    }
    if (candidate->header.mbc_type == CUPID_GB_TPP1 && version < STATE_VERSION_V16) {
        /* Earlier writers never used the generic cache for TPP1 mapping. Its
         * value may be stale; the saved controller bank was authoritative. */
        candidate->current_rom_bank = candidate->tpp1.rom_bank;
    }
    if (candidate->header.mbc_type == CUPID_GB_HUC3 && !(sections & STATE_SECTION_HUC3)) {
        candidate->huc3 = (CupidGbHuc3){0};
    }
    if (version == STATE_VERSION_V2) {
        /* Earlier states have no pending-DMA or power-relative audio history.
         * Initialize only the new latches after validating every legacy index;
         * never inherit their values from the target's unrelated live timeline. */
        candidate->hdma_pending = false;
        candidate->hdma_hblank_delay = 0;
        candidate->hdma_allow_on_wake = false;
        candidate->speed_switch_halt_cycles = 0;
        candidate->speed_switch_toggle_cycles = 0;
        candidate->speed_switch_freeze_cycles = 0;
        candidate->stop_divider_hold_cycles = 0;
        candidate->speed_switch_alignment = 0;
        cupid_gb_apu_import_legacy(candidate);
    }
    if ((version < STATE_VERSION_V29 && candidate->apu.envelope_delay > 2u) ||
        !valid_hardware_extension(candidate)) {
        discard_candidate(candidate);
        return CUPID_GB_STATE_INVALID;
    }
    if (version < STATE_VERSION_V29)
        import_noise_envelope(candidate);
    import_legacy_sgb_startup(candidate);
    if (version < STATE_VERSION_V11) {
        /* Those writers maintained the pulse latches only for color hardware.
         * Validate first, then derive missing monochrome history from the file. */
        cupid_gb_apu_import_monochrome_pulses(candidate);
    }
    if (version < STATE_VERSION_V14) {
        cupid_gb_apu_import_monochrome_noise(candidate);
        cupid_gb_audio_import_legacy(candidate);
    }
    if (version < STATE_VERSION_V15) {
        /* Earlier files lack quiet-batch, pixel and VRAM bus history. Project only
         * the fully migrated guest state; never inherit the receiver's timeline. */
        cupid_gb_pixels_import_legacy(candidate);
        cupid_gb_display_clock_import_legacy(candidate);
        cupid_gb_vram_bus_import_legacy(candidate);
    }
    if (version < STATE_VERSION_V16) {
        /* Older reads derived contacts from the saved masks. Their stored low
         * nibble is stale; reconstruct its old visible value after SGB migration.
         * Import must not turn that projection into a new input interrupt. */
        cupid_gb_input_import_legacy(candidate);
    }
    if (version >= STATE_VERSION_V15 && version < STATE_VERSION_V17) {
        /* V15-V16 retained other pixel timing but not the tile-map row.
         * Older formats reconstruct that missing row in the legacy importer. */
        candidate->pixel_timing.fetch_y =
            candidate->pixels.window_active
                ? candidate->window_line_counter
                : (uint8_t)(candidate->pixels.line + candidate->io_registers[CUPID_GB_IO_SCY]);
    }
    if (!cupid_gb_audio_state_valid(candidate) || !cupid_gb_display_clock_state_valid(candidate) ||
        !cupid_gb_pixels_timing_valid(candidate) || !cupid_gb_vram_bus_state_valid(candidate) ||
        !cupid_gb_sgb_startup_state_valid(candidate) ||
        !cupid_gb_sgb_protocol_state_valid(candidate)) {
        discard_candidate(candidate);
        return CUPID_GB_STATE_INVALID;
    }
    if (live_audio) {
        /* Finish the saved timeline's deferred digital clocks against its own
         * output credit. A different listening rate cannot safely fund that work.
         * Only the candidate changes; its queued audio is discarded below. */
        cupid_gb_apu_run(candidate, true);
        /* Live listening retains its stream, DAC and filter history. Rebase only
         * the reconstruction fraction at the newly selected guest timeline. */
        candidate->apu.output = gb->apu.output;
        if (!cupid_gb_audio_startup_filter_supported(candidate))
            candidate->apu.output.filter.startup_pending = false;
        candidate->sgb_startup.output = gb->sgb_startup.output;
        candidate->apu.output.sample_fraction = 0;
        candidate->apu.output.fraction_half = false;
        /* Validate the combined history before touching either live owner. */
        if (!cupid_gb_audio_state_valid(candidate) ||
            !cupid_gb_sgb_startup_state_valid(candidate)) {
            discard_candidate(candidate);
            return CUPID_GB_STATE_INVALID;
        }
    }
    CupidGbPrinterState *printer_candidate = NULL;
    if (printer_payload || printer) {
        printer_candidate = allocate(sizeof(*printer_candidate));
        if (!printer_candidate) {
            discard_candidate(candidate);
            return CUPID_GB_STATE_OUT_OF_MEMORY;
        }
        if (printer_payload) {
            /* Validate saved device bytes even when the live host has a different
             * attachment. The candidate's serial context still borrows live storage. */
            if (!cupid_gb_printer_state_decode(printer_candidate, printer_payload,
                                               PRINTER_STATE_WIRE_SIZE) ||
                !cupid_gb_printer_state_valid(printer_candidate, candidate)) {
                free(printer_candidate);
                discard_candidate(candidate);
                return CUPID_GB_STATE_INVALID;
            }
        } else {
            /* V2-V11 and a saved empty attachment cannot recover printer history.
             * Begin an empty device at this timeline, preserving its live callbacks. */
            cupid_gb_printer_state_clear(printer_candidate, candidate->elapsed_cycles);
        }
    }
    /* All parsing and allocation has succeeded. Neither copy can fail or emit
     * callbacks, and a saved device never changes the host's chosen attachment. */
    cupid_gb_camera_cancel_request(candidate);
    free(gb->cartridge_ram_heap);
    free(gb->mbc6_flash);
    *gb = *candidate;
    if (printer)
        printer->state = *printer_candidate;
    free(printer_candidate);
    candidate->cartridge_ram_heap = NULL;
    candidate->mbc6_flash = NULL;
    discard_candidate(candidate);
    return CUPID_GB_STATE_OK;
}

CupidGbStateResult cupid_gb_save_state(const CupidGb *gb, uint8_t *data, size_t capacity,
                                       size_t *written) {
    return save_state(gb, data, capacity, written, NULL);
}

CupidGbStateResult cupid_gb_load_state(CupidGb *gb, const uint8_t *data, size_t length) {
    return load_state(gb, data, length, NULL, false, malloc);
}

CupidGbStateResult cupid_gb_load_state_with_allocator(CupidGb *gb, const uint8_t *data,
                                                      size_t length, bool live_audio,
                                                      void *(*allocate)(size_t)) {
    return load_state(gb, data, length, NULL, live_audio, allocate);
}

CupidGbStateResult cupid_gb_state_context_init(CupidGbStateContext *context, const CupidGb *gb) {
    if (!context || overlaps_live_storage(gb, context, sizeof(*context))) {
        return CUPID_GB_STATE_INVALID;
    }
    memset(context, 0, sizeof(*context));
    if (!ready(gb)) {
        return CUPID_GB_STATE_NO_ROM;
    }
    if (!cupid_gb_model_is_valid(gb->model)) {
        return CUPID_GB_STATE_INVALID;
    }
    context->owner = gb;
    context->rom = gb->rom;
    context->rom_size = gb->rom_size;
    context->rom_generation = gb->rom_generation;
    context->model = gb->model;
    cupid_gb_program_digest(gb, context->rom_digest);
    return CUPID_GB_STATE_OK;
}

static CupidGbStateResult check_context(const CupidGb *gb, const CupidGbStateContext *context) {
    if (!ready(gb)) {
        return CUPID_GB_STATE_NO_ROM;
    }
    if (!context || context->owner != gb || context->rom != gb->rom ||
        context->rom_size != gb->rom_size || context->rom_generation != gb->rom_generation) {
        return CUPID_GB_STATE_WRONG_ROM;
    }
    return context->model == gb->model ? CUPID_GB_STATE_OK : CUPID_GB_STATE_WRONG_MODEL;
}

CupidGbStateResult cupid_gb_save_state_with_context(const CupidGb *gb, uint8_t *data,
                                                    size_t capacity, size_t *written,
                                                    const CupidGbStateContext *context) {
    if (!valid_written_pointer(gb, data, capacity, written, context))
        return CUPID_GB_STATE_INVALID;
    CupidGbStateResult result = check_context(gb, context);
    if (result != CUPID_GB_STATE_OK) {
        if (written) {
            *written = 0;
        }
        return result;
    }
    if (data && overlaps(data, cupid_gb_state_size(gb), context, sizeof(*context))) {
        if (written) {
            *written = 0;
        }
        return CUPID_GB_STATE_INVALID;
    }
    return save_state(gb, data, capacity, written, context->rom_digest);
}

CupidGbStateResult cupid_gb_load_state_with_context(CupidGb *gb, const uint8_t *data, size_t length,
                                                    const CupidGbStateContext *context) {
    CupidGbStateResult result = check_context(gb, context);
    return result == CUPID_GB_STATE_OK
               ? load_state(gb, data, length, context->rom_digest, false, malloc)
               : result;
}

CupidGbStateResult cupid_gb_load_state_live(CupidGb *gb, const uint8_t *data, size_t length) {
    return load_state(gb, data, length, NULL, true, malloc);
}

CupidGbStateResult cupid_gb_load_state_live_with_context(CupidGb *gb, const uint8_t *data,
                                                         size_t length,
                                                         const CupidGbStateContext *context) {
    const CupidGbStateResult result = check_context(gb, context);
    return result == CUPID_GB_STATE_OK
               ? load_state(gb, data, length, context->rom_digest, true, malloc)
               : result;
}

const char *cupid_gb_state_result_name(CupidGbStateResult result) {
    switch (result) {
        case CUPID_GB_STATE_OK:
            return "State operation succeeded";
        case CUPID_GB_STATE_INVALID:
            return "Invalid or damaged state";
        case CUPID_GB_STATE_UNSUPPORTED_VERSION:
            return "Unsupported state version";
        case CUPID_GB_STATE_WRONG_ROM:
            return "State belongs to a different program";
        case CUPID_GB_STATE_WRONG_MODEL:
            return "State uses a different hardware model";
        case CUPID_GB_STATE_WRONG_BOOT_ROM:
            return "State requires the matching boot ROM";
        case CUPID_GB_STATE_BUFFER_TOO_SMALL:
            return "State buffer is too small";
        case CUPID_GB_STATE_OUT_OF_MEMORY:
            return "Cannot allocate state storage";
        case CUPID_GB_STATE_NO_ROM:
            return "No program is loaded";
        default:
            return "Unknown state error";
    }
}

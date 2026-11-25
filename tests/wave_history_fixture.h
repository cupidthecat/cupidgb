/**
 * @file    wave_history_fixture.h
 * @brief   Original guest programs and reachable CH3 sample-history fixtures.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#ifndef CUPID_TEST_WAVE_HISTORY_FIXTURE_H
#define CUPID_TEST_WAVE_HISTORY_FIXTURE_H

#include "cupid/gb/apu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/startup.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum {
    WAVE_HISTORY_ROM_BYTES = 32768,
    WAVE_HISTORY_BYTES = 16,
    WAVE_HISTORY_HOLD_FREQUENCY = 0x7c0,
    WAVE_HISTORY_HELD_BYTE = 0x69,
    WAVE_HISTORY_REPLACEMENT_BYTE = 0xe1
};

static const uint8_t wave_history_input[WAVE_HISTORY_BYTES] = {
    0x96, 0x35, 0xca, 0x6b, 0x69, 0xa6, 0x5c, 0xf0, 0x3c, 0xc3, 0x87, 0x78, 0x55, 0xaa, 0xe1, 0x1e};

static inline void wave_history_arm(CupidGb *gb, uint16_t pc, uint8_t accumulator) {
    gb->cpu.pc = pc;
    gb->cpu.sp = 0xdff0;
    gb->cpu.a = accumulator;
    gb->cpu.f = 0;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
    gb->cpu.halt_bug = false;
    gb->cpu.halted = false;
    gb->cpu.just_halted = false;
    gb->cpu.stopped = false;
}

static inline CupidGb *wave_history_new_cartridge(CupidGbModel model, bool color, bool fast,
                                                  uint16_t frequency, uint8_t cartridge_type) {
    assert(frequency <= 0x7ff && (!fast || model == CUPID_GB_MODEL_CGB));
    assert(cartridge_type == 0 || cartridge_type == 0x22);
    uint8_t rom[WAVE_HISTORY_ROM_BYTES] = {0};
    /* The first program triggers CH3, reads PCM34 and stores the real result. */
    const uint8_t guest[] = {0xea, 0x1e, 0xff, 0xf0, 0x77, 0xe0, 0x80};
    const uint8_t wave_write[] = {0xe0, 0x34};
    const uint8_t sample[] = {0xf0, 0x77, 0xe0, 0x81};
    memcpy(rom + 0x100, guest, sizeof(guest));
    memcpy(rom + 0x110, wave_write, sizeof(wave_write));
    memcpy(rom + 0x120, sample, sizeof(sample));
    memcpy(rom + 0x134, "WAVE HISTORY", 12);
    rom[0x143] = color ? 0x80 : 0;
    rom[0x147] = cartridge_type;
    if (model == CUPID_GB_MODEL_SGB || model == CUPID_GB_MODEL_SGB2) {
        rom[0x146] = 3;
        rom[0x14b] = 0x33;
    }
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x574156452d484953));
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)) && gb->header.header_checksum_valid);
    assert(gb->cgb_mode == color);
    gb->double_speed = fast;
    gb->speed_phase = false;
    gb->io_registers[0x4d] = fast ? 0x80 : 0;
    assert(cupid_gb_apu_set_sample_rate(gb, 48000));
    const uint16_t addresses[] = {0xff40, 0xffff, 0xff0f, 0xff07, 0xff04, 0xff26};
    for (unsigned index = 0; index < sizeof(addresses) / sizeof(*addresses); ++index)
        cupid_gb_write_u8(gb, addresses[index], 0);
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff24, 0x77);
    cupid_gb_write_u8(gb, 0xff25, 0x44);
    for (unsigned index = 0; index < WAVE_HISTORY_BYTES; ++index)
        cupid_gb_write_u8(gb, (uint16_t)(0xff30 + index), wave_history_input[index]);
    cupid_gb_write_u8(gb, 0xff1a, 0x80);
    cupid_gb_write_u8(gb, 0xff1b, 0xff);
    cupid_gb_write_u8(gb, 0xff1c, 0x20);
    cupid_gb_write_u8(gb, 0xff1d, (uint8_t)frequency);
    cupid_gb_write_u8(gb, 0xff1e, (uint8_t)(0x80 | (frequency >> 8)));
    wave_history_arm(gb, 0x200, 0);
    return gb;
}

static inline CupidGb *wave_history_new(CupidGbModel model, bool color, bool fast,
                                        uint16_t frequency) {
    return wave_history_new_cartridge(model, color, fast, frequency, 0);
}

static inline void wave_history_free(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static inline void wave_history_advance(CupidGb *gb, unsigned base_ticks) {
    /* Normal NOPs supply complete CPU intervals. A short direct APU prefix
     * reaches the other sample-clock alignment without editing hidden latches. */
    const unsigned step = gb->double_speed ? 2u : 4u;
    if (base_ticks % step)
        cupid_gb_tick_apu(gb, (uint16_t)(base_ticks % step));
    wave_history_arm(gb, 0x200, 0);
    for (unsigned index = 0; index < base_ticks / step; ++index)
        assert(cupid_gb_step(gb));
}

static inline CupidGb *wave_history_hold_cartridge(bool color, bool fast, uint8_t cartridge_type) {
    CupidGb *gb = wave_history_new_cartridge(CUPID_GB_MODEL_CGB, color, fast,
                                             WAVE_HISTORY_HOLD_FREQUENCY, cartridge_type);
    /* Reach the low nibble of byte four, leaving time for the guest write and
     * retrigger before another fetch. The whole byte was fetched normally. */
    wave_history_advance(gb, 134u + 8u * 128u + 32u);
    assert(gb->apu.ch3_wave_pos == 9 && gb->apu.ch3_timer == 96);
    assert(gb->apu.ch3_sample_byte == WAVE_HISTORY_HELD_BYTE && gb->apu.ch3_sample == 9);
    wave_history_arm(gb, 0x110, WAVE_HISTORY_REPLACEMENT_BYTE);
    assert(cupid_gb_step(gb) && gb->cpu.pc == 0x112);
    assert(gb->io_registers[0x34] == WAVE_HISTORY_REPLACEMENT_BYTE);
    assert(gb->apu.ch3_sample_byte == WAVE_HISTORY_HELD_BYTE && gb->apu.ch3_sample == 9);
    wave_history_arm(gb, 0x100, 0x87);
    return gb;
}

static inline CupidGb *wave_history_hold(bool color, bool fast) {
    return wave_history_hold_cartridge(color, fast, 0);
}

static inline uint8_t wave_history_guest_result(CupidGb *gb) {
    assert(gb->cpu.pc == 0x100);
    for (unsigned step = 0; step < 3; ++step)
        assert(cupid_gb_step(gb));
    assert(gb->cpu.pc == 0x107 && gb->cpu.a == gb->high_ram[0]);
    return gb->high_ram[0];
}

#endif

/**
 * @file    test_bess.c
 * @brief   Portable state round trips and failure atomicity.
 * @author  cupidthecat
 * @date    2026-11-19
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

#include "cupid/gb/bess.h"

#include "../src/gb/oam_bus.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CupidGb *machine(CupidGbModel model, uint8_t type, bool compatibility) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    uint8_t rom[65536] = {0};
    assert(gb);
    const uint8_t guest[] = {0x7e, 0xea, 0x80, 0xff, 0x18, 0xfe}; /* Read HL to HRAM. */
    memcpy(rom + 0x100, guest, sizeof(guest));
    memcpy(rom + 0x134, "PORTABLE STATE", 14);
    rom[0x143] = cupid_gb_model_is_cgb(model) && !compatibility ? 0x80 : 0;
    rom[0x147] = type;
    rom[0x148] = type ? 1 : 0;
    rom[0x149] = type == 6 || !type ? 0 : 3;
    rom[0x14e] = 0x9a;
    rom[0x14f] = 0xd1;
    for (unsigned offset = 0x134; offset < 0x14d; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
    for (unsigned bank = 0; bank < 4; ++bank)
        rom[bank * 0x4000u + 0x200u] = (uint8_t)(0xb0u + bank);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 7);
    assert(cupid_gb_load_rom(gb, rom, type ? sizeof(rom) : sizeof(rom) / 2u));
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void put_long(uint8_t *data, unsigned value) {
    for (unsigned i = 0; i < 4; ++i)
        data[i] = (uint8_t)(value >> (i * 8u));
}

static unsigned read_long(const uint8_t *data) {
    return (unsigned)data[0] | (unsigned)data[1] << 8 | (unsigned)data[2] << 16 |
           (unsigned)data[3] << 24;
}

static uint8_t *find_block(uint8_t *data, size_t length, const char name[4]) {
    size_t cursor = read_long(data + length - 8);
    while (cursor + 8 <= length - 8) {
        const unsigned count = read_long(data + cursor + 4);
        if (!memcmp(data + cursor, name, 4))
            return data + cursor + 8;
        cursor += 8 + count;
    }
    assert(!"Missing fixture block");
    return NULL;
}

static uint8_t *capture(CupidGb *gb, size_t *length) {
    const size_t size = cupid_gb_bess_size(gb);
    assert(size);
    uint8_t *data = malloc(size);
    assert(data);
    assert(cupid_gb_bess_save(gb, data, size, length) == CUPID_GB_STATE_OK);
    assert(*length == size && cupid_gb_bess_has_footer(data, size));
    assert(!cupid_gb_state_has_magic(data, size));
    return data;
}

static void check_native_snapshot(CupidGb *gb) {
    const size_t size = cupid_gb_state_size(gb);
    uint8_t *bytes = malloc(size);
    assert(bytes);
    size_t used = 0;
    assert(cupid_gb_save_state(gb, bytes, size, &used) == CUPID_GB_STATE_OK && size == used);
    assert(cupid_gb_load_state(gb, bytes, size) == CUPID_GB_STATE_OK);
    free(bytes);
}

static void exercise(CupidGbModel model, uint8_t type, bool compatibility) {
    CupidGb *source = machine(model, type, compatibility);
    CupidGb *target = machine(model, type, compatibility);
    assert(cupid_gb_set_rtc_mode(source, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_set_rtc_mode(target, CUPID_GB_RTC_ACCURATE));
    const CupidGbCpu prior = {.a = 0x7c,
                              .f = 0xb0,
                              .b = 0x12,
                              .c = 0x34,
                              .d = 0x56,
                              .e = 0x78,
                              .h = 0xc1,
                              .l = 0,
                              .sp = 0xdffe,
                              .pc = 0x100};
    source->cpu = prior;
    source->io_registers[4] = 0x86;
    source->div_counter = 31;
    source->work_ram[0x100] = 0x7b;
    source->video_ram[0x15] = 0xc9;
    source->object_attribute_memory[32] = 0x55;
    source->high_ram[17] = 0x91;
    for (unsigned i = 0; i < cupid_gb_extra_oam_size(model); ++i)
        source->extra_oam[i] = (uint8_t)(0x80u + i);
    cupid_gb_write_u8(source, 0, 0x0a);
    cupid_gb_write_u8(source, 0x2100, 2);
    cupid_gb_write_u8(source, 0x4000, 1);
    cupid_gb_write_u8(source, 0xa123, 0x5d);
    if (type == 0x10) {
        source->rtc = (CupidGbRtc){.s = 42,
                                   .m = 23,
                                   .h = 17,
                                   .dl = 45,
                                   .dh = 0x81,
                                   .ls = 11,
                                   .lm = 39,
                                   .lh = 6,
                                   .ldl = 55,
                                   .ldh = 0x41,
                                   .latch_prev = 1,
                                   .base_time = INT64_C(123456789)};
        cupid_gb_write_u8(source, 0x4000, 8);
    }
    if (source->cgb_mode) {
        source->double_speed = true;
        cupid_gb_write_u8(source, 0xff4f, 1);
        cupid_gb_write_u8(source, 0xff70, 3);
        source->video_ram[0x2015] = 0x8a;
        source->cgb_bg_palette_ram[3] = 0x55;
    }
    assert(cupid_gb_apu_set_sample_rate(target, 48000));
    assert(cupid_gb_apu_set_filter_mode(target, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM));
    size_t size;
    uint8_t *data = capture(source, &size);
    assert(cupid_gb_bess_load(target, data, size) == CUPID_GB_STATE_OK);
    assert(target->cpu.pc == prior.pc && target->cpu.sp == prior.sp && target->cpu.f == prior.f);
    assert(target->cpu.a == prior.a && target->cpu.b == prior.b && target->cpu.c == prior.c &&
           target->cpu.d == prior.d && target->cpu.e == prior.e && target->cpu.h == prior.h &&
           target->cpu.l == prior.l);
    assert(target->io_registers[4] == 0x86 && !target->div_counter && !target->divider_subcycle);
    assert(target->cgb_mode == source->cgb_mode && target->double_speed == source->double_speed);
    assert(target->cgb_vram_bank == source->cgb_vram_bank &&
           target->cgb_wram_bank == source->cgb_wram_bank);
    assert(!memcmp(target->object_attribute_memory, source->object_attribute_memory,
                   sizeof(source->object_attribute_memory)));
    assert(!memcmp(target->extra_oam, source->extra_oam, cupid_gb_extra_oam_size(model)));
    assert(target->work_ram[0x100] == 0x7b && target->high_ram[17] == 0x91 &&
           target->video_ram[0x15] == 0xc9 &&
           target->video_ram[0x2015] == source->video_ram[0x2015]);
    assert(cupid_gb_read_u8(target, 0x4200) == (type ? 0xb2 : 0xb1));
    if (type == 6) {
        const uint8_t *expected = cupid_gb_cartridge_ram_const(source);
        const uint8_t *actual = cupid_gb_cartridge_ram_const(target);
        for (size_t i = 0; i < source->cartridge_ram_size; ++i)
            assert(actual[i] == (expected[i] & 0x0f));
    } else if (type) {
        assert(!memcmp(cupid_gb_cartridge_ram_const(target), cupid_gb_cartridge_ram_const(source),
                       source->cartridge_ram_size));
    }
    if (type == 0x10) {
        const CupidGbRtc *rtc = &target->rtc;
        assert(rtc->s == 42 && rtc->m == 23 && rtc->h == 17 && rtc->dl == 45 && rtc->dh == 0x81);
        assert(rtc->ls == 11 && rtc->lm == 39 && rtc->lh == 6 && rtc->ldl == 55 &&
               rtc->ldh == 0x41);
        assert(rtc->latch_prev == 1 && rtc->base_time == INT64_C(123456789));
    }
    assert(cupid_gb_apu_get_sample_rate(target) == 48000);
    assert(cupid_gb_apu_get_filter_mode(target) == CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM);
    assert(!target->apu.output.filter.startup_pending);
    assert(!target->dma_active && !target->hdma_active && !target->serial_bits_remaining);

    check_native_snapshot(target);
    assert(cupid_gb_step(target) && cupid_gb_step(target));
    assert(cupid_gb_read_u8(target, 0xff80) == 0x7b);
    if (type == 0x10) {
        memcpy(find_block(data, size, "RTC ") - 8, "TIME", 4);
        target->rtc.s = 57;
        target->rtc.base_time = 98765;
        assert(cupid_gb_bess_load(target, data, size) == CUPID_GB_STATE_OK);
        assert(target->rtc.s == 57 && target->rtc.base_time == 98765);
    }
    free(data);
    destroy(target);
    destroy(source);
}

static void check_invalid(void) {
    CupidGb *source = machine(CUPID_GB_MODEL_CGB_E, 3, false);
    CupidGb *target = machine(CUPID_GB_MODEL_CGB_E, 3, false);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, target, sizeof(*before));
    size_t size;
    uint8_t *valid = capture(source, &size);
    uint8_t *mutated = malloc(size + 11);
    assert(mutated);
    for (unsigned attempt = 0; attempt < 9; ++attempt) {
        memcpy(mutated, valid, size);
        uint8_t *core = find_block(mutated, size, "CORE");
        uint8_t *mapper = find_block(mutated, size, "MBC ");
        uint8_t *info = find_block(mutated, size, "INFO");
        switch (attempt) {
            case 0:
                put_long(mutated + size - 8, 0xffffffffu);
                break;
            case 1:
                put_long(core + 0x9c, read_long(mutated + size - 8));
                break;
            case 2:
                put_long(core - 4, 0xffffffffu);
                break;
            case 3:
                memcpy(info - 8, "CORE", 4);
                break;
            case 4:
                mapper[1] = 0x80;
                break;
            case 5:
                info[0] ^= 1;
                break;
            case 6:
                core[4] = 'G';
                break;
            case 7:
                core[0] = 2;
                break;
            case 8:
                core[0x18 + 0x50] = 0;
                break;
        }
        assert(cupid_gb_bess_load(target, mutated, size) != CUPID_GB_STATE_OK);
        assert(!memcmp(target, before, sizeof(*target)));
    }
    /* An unknown extension can precede the terminator without changing fields. */
    const size_t end = size - 16;
    memcpy(mutated, valid, end);
    memcpy(mutated + end, "TEST", 4);
    put_long(mutated + end + 4, 3);
    memcpy(mutated + end + 8, "new", 3);
    memcpy(mutated + end + 11, valid + end, 16);
    assert(cupid_gb_bess_load(target, mutated, size + 11) == CUPID_GB_STATE_OK);
    free(mutated);
    free(valid);
    free(before);
    destroy(target);
    destroy(source);
}

int main(void) {
    const CupidGbModel models[] = {
        CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0,  CUPID_GB_MODEL_MGB,
        CUPID_GB_MODEL_CGB_0,   CUPID_GB_MODEL_CGB_A, CUPID_GB_MODEL_CGB_B,
        CUPID_GB_MODEL_CGB_C,   CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB_E};
    const uint8_t mappers[] = {0, 3, 6, 0x10, 0x1b};
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); ++i)
        for (unsigned j = 0; j < sizeof(mappers); ++j) {
            exercise(models[i], mappers[j], false);
            if (cupid_gb_model_is_cgb(models[i]))
                exercise(models[i], mappers[j], true);
        }
    check_invalid();
    puts("Portable states preserve mapped cartridge and memory state.");
}

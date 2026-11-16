/**
 * @file    test_infrared.c
 * @brief   Regression tests for infrared registers, callbacks, and receiver timing.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "cupid/common/sha256.h"
#include "cupid/gb/huc3.h"
#include "cupid/gb/infrared.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct OutputEvents {
    unsigned count;
    bool level[16], combined[16];
    uint64_t cycles[16];
} OutputEvents;

static void observe_output(const CupidGb *gb, bool active, uint64_t cycles, void *user_data) {
    OutputEvents *events = user_data;
    assert(events->count < 16);
    const unsigned index = events->count++;
    events->level[index] = active;
    events->combined[index] = cupid_gb_infrared_output(gb);
    events->cycles[index] = cycles;
    assert(cycles == gb->elapsed_cycles);
}

static CupidGb *new_cartridge(CupidGbModel model, uint8_t type, bool color) {
    uint8_t rom[32768] = {0};
    memcpy(rom + 0x134, "IR TEST", 7);
    rom[0x143] = color ? 0x80 : 0;
    rom[0x147] = type;
    rom[0x149] = 3;
    if (type == 0xbc) {
        rom[0x149] = 0xc1;
        rom[0x14a] = 0x65;
        rom[0x152] = 1;
    }
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[index] - 1u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    cupid_gb_write_u8(gb, 0xff40, 0);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static bool infrared_cartridge(const CupidGb *gb) {
    return gb->header.mbc_type == CUPID_GB_HUC1 || gb->header.mbc_type == CUPID_GB_HUC3;
}

static void enable_receiver(CupidGb *gb) {
    if (infrared_cartridge(gb))
        cupid_gb_write_u8(gb, 0, 0x0e);
    cupid_gb_write_u8(gb, 0xff56, 0xc0);
}

static void check_ports(const CupidGb *gb, bool detected) {
    if (gb->header.mbc_type == CUPID_GB_HUC1)
        assert(cupid_gb_read_u8(gb, 0xa000) == (detected ? 0xc1 : 0xc0));
    if (gb->header.mbc_type == CUPID_GB_HUC3)
        assert(cupid_gb_read_u8(gb, 0xa000) == (detected ? 1 : 0));
    assert(cupid_gb_read_u8(gb, 0xff56) == (gb->cgb_mode ? (detected ? 0xec : 0xee) : 0xff));
}

static void test_receiver_by_model_and_mapper(void) {
    const uint8_t types[] = {0x00, 0x08, 0x09, 0x01, 0x03, 0x05, 0x06, 0x0b, 0x0d, 0x0f, 0x10, 0x11,
                             0x13, 0x19, 0x1b, 0x1c, 0x1e, 0x20, 0x22, 0xfc, 0xfe, 0xff, 0xbc};
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_DMG_ABC,
                                   CUPID_GB_MODEL_MGB,  CUPID_GB_MODEL_CGB,
                                   CUPID_GB_MODEL_SGB,  CUPID_GB_MODEL_SGB2};
    for (size_t model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (size_t type = 0; type < sizeof(types) / sizeof(types[0]); ++type) {
            CupidGb *gb = new_cartridge(models[model], types[type], true);
            enable_receiver(gb);
            const uint64_t generation = gb->battery_write_generation;
            cupid_gb_set_infrared_input(gb, true);
            cupid_gb_tick(gb, 3000);
            check_ports(gb, false);
            cupid_gb_tick(gb, 1400);
            const bool available = gb->cgb_mode || infrared_cartridge(gb);
            check_ports(gb, available);
            assert(cupid_gb_infrared_detected(gb) == available);
            assert(gb->battery_write_generation == generation);
            if (!available)
                assert(!gb->huc3.ir_warmup && !gb->huc3.ir_decay);
            cupid_gb_set_infrared_input(gb, false);
            cupid_gb_tick(gb, 1);
            check_ports(gb, false);
            destroy(gb);
        }
    }
}

static void test_led_callbacks_and_storage(void) {
    const uint8_t types[] = {0xfe, 0xff};
    for (size_t type = 0; type < sizeof(types); ++type) {
        CupidGb *gb = new_cartridge(CUPID_GB_MODEL_CGB, types[type], true);
        OutputEvents events = {0};
        cupid_gb_set_infrared_callback(gb, observe_output, &events);
        assert(!events.count && !cupid_gb_infrared_output(gb));
        cupid_gb_write_u8(gb, 0, 0x0a);
        cupid_gb_write_u8(gb, 0xa000, 0x52);
        enable_receiver(gb);
        const uint64_t generation = gb->battery_write_generation;
        cupid_gb_tick(gb, 37);
        cupid_gb_write_u8(gb, 0xa000, 1);
        cupid_gb_write_u8(gb, 0xafff, 0xff);
        assert(events.count == 1 && events.level[0] && events.combined[0]);
        assert(events.cycles[0] == 148);
        cupid_gb_write_u8(gb, 0, 0x0a);
        assert(cupid_gb_infrared_output(gb));
        assert(cupid_gb_read_u8(gb, 0xa000) == 0x52);
        assert(gb->battery_write_generation == generation);
        cupid_gb_write_u8(gb, 0xff56, 0xc1);
        cupid_gb_write_u8(gb, 0xff56, 0xff);
        assert(events.count == 2 && events.level[1] && events.combined[1]);
        cupid_gb_tick(gb, 3);
        cupid_gb_write_u8(gb, 0xff56, 0xc0);
        assert(events.count == 3 && !events.level[2] && events.combined[2]);
        assert(events.cycles[2] == 160);
        cupid_gb_write_u8(gb, 0, 0x0e);
        cupid_gb_write_u8(gb, 0xbfff, 0xfe);
        assert(events.count == 4 && !events.level[3] && !events.combined[3]);
        assert(!cupid_gb_infrared_output(gb));
        cupid_gb_set_infrared_callback(gb, NULL, NULL);
        cupid_gb_write_u8(gb, 0xa000, 1);
        assert(events.count == 4 && cupid_gb_infrared_output(gb));
        destroy(gb);
    }
}

static void test_color_port_callback_order(void) {
    CupidGb *gb = new_cartridge(CUPID_GB_MODEL_CGB, 0, true);
    OutputEvents events = {0};
    cupid_gb_set_infrared_callback(gb, observe_output, &events);
    cupid_gb_write_u8(gb, 0xff56, 0xc1);
    assert(events.count == 1 && events.level[0] && !events.combined[0]);
    assert(cupid_gb_infrared_output(gb) && cupid_gb_read_u8(gb, 0xff56) == 0xef);
    cupid_gb_tick(gb, 5);
    cupid_gb_write_u8(gb, 0xff56, 0xc0);
    assert(events.count == 2 && !events.level[1] && events.combined[1]);
    assert(events.cycles[1] == 20 && !cupid_gb_infrared_output(gb));
    cupid_gb_set_infrared_input(gb, true);
    cupid_gb_init(gb);
    assert(gb->infrared_callback == observe_output && gb->infrared_user_data == &events);
    assert(gb->huc3_ir_input && !gb->huc3.ir_output && !gb->huc3.ir_warmup);
    assert(events.count == 2);
    cupid_gb_set_infrared_callback(gb, NULL, &events);
    assert(!gb->infrared_callback && !gb->infrared_user_data);
    cupid_gb_set_infrared_callback(gb, observe_output, &events);
    cupid_gb_cleanup(gb);
    assert(!gb->infrared_callback && !gb->infrared_user_data);
    free(gb);
}

static void test_compatibility_and_absent_ports(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    const uint8_t types[] = {0x00, 0xfe, 0xff};
    for (size_t model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (size_t type = 0; type < sizeof(types); ++type) {
            CupidGb *gb = new_cartridge(models[model], types[type], false);
            OutputEvents events = {0};
            cupid_gb_set_infrared_callback(gb, observe_output, &events);
            cupid_gb_write_u8(gb, 0xff56, 0xc1);
            const bool physical_color = models[model] == CUPID_GB_MODEL_CGB;
            assert(cupid_gb_read_u8(gb, 0xff56) == 0xff);
            assert(gb->io_registers[0x56] == (physical_color ? 0xc1 : 0));
            assert(events.count == (physical_color ? 1u : 0u));
            assert(cupid_gb_infrared_output(gb) == physical_color);
            if (infrared_cartridge(gb))
                cupid_gb_write_u8(gb, 0, 0x0e);
            cupid_gb_set_infrared_input(gb, true);
            cupid_gb_tick(gb, 4400);
            assert(cupid_gb_infrared_detected(gb) == infrared_cartridge(gb));
            if (types[type] == 0xff)
                assert(cupid_gb_read_u8(gb, 0xa000) == 0xc1);
            else if (types[type] == 0xfe)
                assert(cupid_gb_read_u8(gb, 0xa000) == 1);
            else
                assert(!gb->huc3.ir_warmup && !gb->huc3.ir_decay);
            destroy(gb);
        }
    }
    cupid_gb_set_infrared_input(NULL, true);
    cupid_gb_set_infrared_callback(NULL, observe_output, NULL);
    cupid_gb_infrared_write_cartridge(NULL, 1);
    cupid_gb_infrared_write_port(NULL, 1);
    cupid_gb_infrared_tick(NULL, 1);
    assert(!cupid_gb_infrared_output(NULL) && !cupid_gb_infrared_detected(NULL));
}

static void test_warmup_decay_and_loopback(void) {
    const uint8_t types[] = {0x00, 0xfe, 0xff};
    for (size_t type = 0; type < sizeof(types); ++type) {
        CupidGb *gb = new_cartridge(CUPID_GB_MODEL_CGB, types[type], true);
        enable_receiver(gb);
        cupid_gb_tick(gb, 4400);
        assert(!cupid_gb_infrared_detected(gb));
        cupid_gb_write_u8(gb, 0xff56, 0xc1);
        cupid_gb_tick(gb, 1);
        assert(cupid_gb_infrared_detected(gb));
        assert(cupid_gb_read_u8(gb, 0xff56) == 0xed);
        cupid_gb_tick(gb, 7000);
        assert(!cupid_gb_infrared_detected(gb));
        cupid_gb_write_u8(gb, 0xff56, 0xc0);
        cupid_gb_tick(gb, 7000);
        cupid_gb_set_infrared_input(gb, true);
        cupid_gb_tick(gb, 1);
        assert(cupid_gb_infrared_detected(gb));
        cupid_gb_write_u8(gb, 0xff56, 0);
        if (infrared_cartridge(gb))
            cupid_gb_write_u8(gb, 0, 0);
        cupid_gb_tick(gb, 5000);
        assert(!gb->huc3.ir_warmup && !gb->huc3.ir_decay && !cupid_gb_infrared_detected(gb));
        enable_receiver(gb);
        cupid_gb_tick(gb, 3000);
        assert(!cupid_gb_infrared_detected(gb));
        cupid_gb_tick(gb, 1400);
        assert(cupid_gb_infrared_detected(gb));
        destroy(gb);
    }
}

static void test_sensor_clock_domains(void) {
    CupidGb *normal = new_cartridge(CUPID_GB_MODEL_CGB, 0, true);
    CupidGb *fast = new_cartridge(CUPID_GB_MODEL_CGB, 0, true);
    enable_receiver(normal);
    enable_receiver(fast);
    fast->double_speed = true;
    cupid_gb_tick(normal, 1000);
    cupid_gb_tick(fast, 2000);
    assert(normal->elapsed_cycles == fast->elapsed_cycles);
    assert(normal->huc3.ir_warmup == 8000 && fast->huc3.ir_warmup == 8000);
    const uint64_t elapsed = fast->elapsed_cycles;
    fast->speed_switch_freeze_cycles = 8;
    cupid_gb_tick(fast, 2);
    assert(fast->elapsed_cycles == elapsed && fast->huc3.ir_warmup == 8000);
    cupid_gb_tick(fast, 1);
    assert(fast->elapsed_cycles == elapsed + 2 && fast->huc3.ir_warmup == 8004);
    destroy(fast);
    destroy(normal);
}

static void test_state_continuation(void) {
    const uint8_t types[] = {0x00, 0xfe, 0xff};
    for (size_t type = 0; type < sizeof(types); ++type) {
        CupidGb *gb = new_cartridge(CUPID_GB_MODEL_CGB, types[type], true);
        CupidGb *target = new_cartridge(CUPID_GB_MODEL_CGB, types[type], true);
        enable_receiver(gb);
        cupid_gb_set_infrared_input(gb, true);
        cupid_gb_tick(gb, 3000);
        if (infrared_cartridge(gb))
            cupid_gb_write_u8(gb, 0xa000, 1);
        OutputEvents events = {0};
        cupid_gb_set_infrared_callback(target, observe_output, &events);
        cupid_gb_set_infrared_input(target, true);
        const size_t size = cupid_gb_state_size(gb);
        uint8_t *state = malloc(size);
        assert(state && cupid_gb_save_state(gb, state, size, NULL) == CUPID_GB_STATE_OK);
        assert(cupid_gb_load_state(target, state, size) == CUPID_GB_STATE_OK);
        assert(!events.count && target->huc3_ir_input);
        assert(target->infrared_callback == observe_output &&
               target->infrared_user_data == &events);
        assert(target->huc3.ir_warmup == gb->huc3.ir_warmup);
        assert(target->huc3.ir_output == gb->huc3.ir_output);
        for (unsigned step = 0; step < 10000; ++step) {
            cupid_gb_tick(gb, 1);
            cupid_gb_tick(target, 1);
            assert(cupid_gb_read_u8(gb, 0xff56) == cupid_gb_read_u8(target, 0xff56));
            assert(target->huc3.ir_warmup == gb->huc3.ir_warmup);
            assert(target->huc3.ir_decay == gb->huc3.ir_decay);
            assert(target->huc3.ir_detected == gb->huc3.ir_detected);
        }
        gb->huc3.ir_warmup = CUPID_GB_INFRARED_WARMUP_LIMIT + 1;
        assert(cupid_gb_save_state(gb, state, size, NULL) == CUPID_GB_STATE_INVALID);
        free(state);
        destroy(target);
        destroy(gb);
    }
}

static void test_legacy_sensor_defaults(void) {
    const uint8_t types[] = {0x00, 0xff};
    for (size_t type = 0; type < sizeof(types); ++type) {
        CupidGb *gb = new_cartridge(CUPID_GB_MODEL_CGB, types[type], true);
        enable_receiver(gb);
        cupid_gb_set_infrared_input(gb, true);
        cupid_gb_tick(gb, 4400);
        const size_t size = cupid_gb_state_size(gb);
        uint8_t *state = malloc(size);
        assert(state && cupid_gb_save_state(gb, state, size, NULL) == CUPID_GB_STATE_OK);
        /* The historical V11 CGB/HuC1 payload ends before shared sensor history. */
        const size_t legacy_size = 300600;
        assert(size > legacy_size);
        state[8] = 11;
        for (unsigned index = 0; index < 4; ++index)
            state[12 + index] = (uint8_t)(legacy_size >> (index * 8));
        cupid_sha256_parts(state, 96, state + 128, legacy_size - 128, state + 96);
        gb->huc3.ir_output = true;
        assert(cupid_gb_load_state(gb, state, legacy_size) == CUPID_GB_STATE_OK);
        assert(!gb->huc3.ir_warmup && !gb->huc3.ir_decay && !gb->huc3.ir_output &&
               !gb->huc3.ir_detected);
        assert(gb->huc3_ir_input);
        cupid_gb_tick(gb, 3000);
        assert(!cupid_gb_infrared_detected(gb));
        free(state);
        destroy(gb);
    }
}

int main(void) {
    test_receiver_by_model_and_mapper();
    test_led_callbacks_and_storage();
    test_color_port_callback_order();
    test_compatibility_and_absent_ports();
    test_warmup_decay_and_loopback();
    test_sensor_clock_domains();
    test_state_continuation();
    test_legacy_sensor_defaults();
    puts("Infrared regressions passed.");
    return 0;
}

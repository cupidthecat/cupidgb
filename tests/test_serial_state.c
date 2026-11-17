/**
 * @file    test_serial_state.c
 * @brief   Serial selector snapshot continuation, legacy projection and rejection checks.
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
#include "cupid/common/sha256.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"
#include "test_serial_helpers.h"

#include <stdio.h>

enum {
    HEADER_BYTES = 128,
    ACCESSORY_BYTES = 8,
    PROTOCOL_BYTES = 85,
    SELECTOR_BYTES = 1,
    WAVE_HISTORY_BYTES = 1,
    SRAM_LENGTH_BYTES = 4
};

typedef struct Blob {
    uint8_t *bytes;
    size_t size;
} Blob;

static unsigned native_cases, legacy_cases, published_cases, rejected_cases;

static CupidGb *new_machine(CupidGbModel model) {
    CupidGb *gb = serial_test_new(model, model == CUPID_GB_MODEL_CGB, true, 0x22);
    if (model == CUPID_GB_MODEL_CGB) {
        /* The first 48 instructions are NOPs. Guest reads then retain SB, SC and
         * IF in HRAM; host observations never advance the emulated hardware. */
        const uint8_t sample[] = {0xf0, 1, 0xe0, 0x80, 0xf0, 2, 0xe0, 0x81, 0xf0, 15, 0xe0, 0x82};
        memcpy(gb->boot_rom + 0x230, sample, sizeof(sample));
        gb->cpu.pc = 0x200;
    }
    return gb;
}

static Blob capture(const CupidGb *gb) {
    Blob blob = {NULL, cupid_gb_state_size(gb)};
    blob.bytes = malloc(blob.size);
    assert(blob.bytes);
    size_t written = 0;
    assert(cupid_gb_save_state(gb, blob.bytes, blob.size, &written) == CUPID_GB_STATE_OK);
    assert(written == blob.size && blob.bytes[8] == CUPID_GB_STATE_VERSION);
    return blob;
}

/* This fixture has inline SRAM and no attached accessory payload. */
static size_t selector_offset(const Blob *blob) {
    assert(blob->bytes[8] >= 18 && blob->bytes[8] <= CUPID_GB_STATE_VERSION);
    return blob->size - ACCESSORY_BYTES - SELECTOR_BYTES -
           (blob->bytes[8] >= 22 ? PROTOCOL_BYTES : 0) -
           (blob->bytes[8] >= 19 ? SRAM_LENGTH_BYTES : 0) -
           (blob->bytes[8] >= 20 ? WAVE_HISTORY_BYTES : 0);
}

static void put_u32(uint8_t *data, uint32_t value) {
    for (unsigned index = 0; index < 4; ++index)
        data[index] = (uint8_t)(value >> (index * 8u));
}

static void resign(Blob *blob) {
    put_u32(blob->bytes + 12, (uint32_t)blob->size);
    cupid_sha256_parts(blob->bytes, 96, blob->bytes + HEADER_BYTES, blob->size - HEADER_BYTES,
                       blob->bytes + 96);
}

static CupidGbStateResult restore(CupidGb *gb, const Blob *blob, unsigned api) {
    CupidGbStateContext context;
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    switch (api) {
        case 0:
            return cupid_gb_load_state(gb, blob->bytes, blob->size);
        case 1:
            return cupid_gb_load_state_with_context(gb, blob->bytes, blob->size, &context);
        case 2:
            return cupid_gb_load_state_live(gb, blob->bytes, blob->size);
        default:
            assert(api == 3);
            return cupid_gb_load_state_live_with_context(gb, blob->bytes, blob->size, &context);
    }
}

static void assert_encoded_equal(const CupidGb *left, const CupidGb *right) {
    Blob a = capture(left), b = capture(right);
    assert(a.size == b.size && !memcmp(a.bytes, b.bytes, a.size));
    free(a.bytes);
    free(b.bytes);
}

static void assert_quiet(const SerialWitness *witness) {
    assert(!witness->launches && !witness->samples && !witness->completions);
}

static void test_reset_selection_is_independent(void) {
    CupidGb *gb = new_machine(CUPID_GB_MODEL_CGB);
    assert(gb->io_registers[2] == 0x7e && !gb->serial_fast_clock);
    Blob slow = capture(gb);
    cupid_gb_write_u8(gb, 0xff02, 2);
    Blob fast = capture(gb);
    const size_t selector = selector_offset(&slow);
    assert(slow.size == 305103 + PROTOCOL_BYTES && fast.size == slow.size);
    assert(!slow.bytes[selector] && fast.bytes[selector] == 1);
    /* Identical SC, phase and hardware prefix do not identify the selected tap. */
    assert(!memcmp(slow.bytes + HEADER_BYTES, fast.bytes + HEADER_BYTES, selector - HEADER_BYTES));
    assert(!memcmp(slow.bytes + selector + SELECTOR_BYTES, fast.bytes + selector + SELECTOR_BYTES,
                   slow.size - selector - SELECTOR_BYTES));
    free(slow.bytes);
    free(fast.bytes);
    serial_test_free(gb);
}

static void test_native_continuation(void) {
    for (unsigned active = 0; active < 2; ++active) {
        for (unsigned compatibility = 0; compatibility < 2; ++compatibility) {
            CupidGb *source = new_machine(CUPID_GB_MODEL_CGB);
            cupid_gb_write_u8(source, 0xff02, 0);
            cupid_gb_write_u8(source, 0xff04, 0);
            cupid_gb_write_u8(source, 0xff01, 0);
            cupid_gb_write_u8(source, 0xff0f, 0);
            cupid_gb_write_u8(source, 0xff02, active ? 0x83 : 2);
            if (compatibility)
                cupid_gb_write_u8(source, 0xff4c, 4);
            cupid_gb_tick(source, 3);
            assert(source->serial_fast_clock && source->serial_master_clock);
            Blob blob = capture(source);
            for (unsigned api = 0; api < 4; ++api) {
                CupidGb *targets[2] = {new_machine(CUPID_GB_MODEL_CGB),
                                       new_machine(CUPID_GB_MODEL_CGB)};
                SerialWitness witnesses[2] = {{0}, {0}};
                for (unsigned poison = 0; poison < 2; ++poison) {
                    CupidGb *target = targets[poison];
                    target->serial_fast_clock = poison != 0;
                    serial_test_observe(target, &witnesses[poison]);
                    assert(restore(target, &blob, api) == CUPID_GB_STATE_OK);
                    assert(target->serial_fast_clock && target->serial_master_clock);
                    assert(target->cgb_mode == !compatibility);
                    assert(target->io_registers[2] == source->io_registers[2]);
                    assert(target->serial_bits_remaining == source->serial_bits_remaining);
                    assert(target->serial_tx_latch == source->serial_tx_latch);
                    assert(target->serial_link.user_data == &witnesses[poison]);
                    assert(target->serial_output_user_data == &witnesses[poison]);
                    assert_quiet(&witnesses[poison]);
                    if (api < 2)
                        assert_encoded_equal(source, target);
                }
                assert_encoded_equal(targets[0], targets[1]);
                for (unsigned step = 0; step < 54; ++step) {
                    assert(cupid_gb_step(targets[0]));
                    assert(cupid_gb_step(targets[1]));
                }
                assert_encoded_equal(targets[0], targets[1]);
                for (unsigned index = 0; index < 2; ++index) {
                    assert(targets[index]->cpu.pc == 0x23c);
                    const uint8_t expected[] = {active ? 0xff : 0, active ? 0x7f : 0x7e,
                                                active ? 0xe8 : 0xe0};
                    assert(!memcmp(targets[index]->high_ram, expected, sizeof(expected)));
                    assert(witnesses[index].samples == (active ? 8u : 0u));
                    assert(witnesses[index].completions == active);
                    if (active)
                        assert(witnesses[index].transmitted == 0);
                    serial_test_free(targets[index]);
                }
                ++native_cases;
            }
            free(blob.bytes);
            serial_test_free(source);
        }
    }
}

/* These are the historical lengths for this MBC7 fixture, including the native
 * header and, from version 12, its empty accessory descriptor. They deliberately
 * stay independent of the current writer's size calculation. */
static Blob legacy_blob(const Blob *current, unsigned version) {
    static const size_t lengths[] = {300518, 300568, 300572, 300580, 300584, 300585, 300596,
                                     300602, 300606, 300608, 300622, 300628, 305062, 305096,
                                     305097, 305097, 305098, 305102, 305103, 305103};
    assert(version >= 2 && version <= 21 && current->size == 305103 + PROTOCOL_BYTES);
    Blob legacy = {NULL, lengths[version - 2]};
    legacy.bytes = malloc(legacy.size);
    assert(legacy.bytes);
    const size_t hardware = legacy.size - (version >= 12 ? ACCESSORY_BYTES : 0);
    memcpy(legacy.bytes, current->bytes, hardware);
    if (version == 19) {
        /* This fixture selects the SRAM-only draft, whose size is fixed above. */
        const size_t wave = current->size - ACCESSORY_BYTES - PROTOCOL_BYTES - SRAM_LENGTH_BYTES -
                            WAVE_HISTORY_BYTES;
        memcpy(legacy.bytes + wave, current->bytes + wave + WAVE_HISTORY_BYTES, SRAM_LENGTH_BYTES);
    }
    if (version >= 12)
        memcpy(legacy.bytes + hardware, current->bytes + current->size - ACCESSORY_BYTES,
               ACCESSORY_BYTES);
    if (version == 15 || version == 16) {
        /* The fourth pixel-history byte was reserved in these two versions. */
        const size_t input = version >= 16 ? 1u : 0u;
        legacy.bytes[hardware - input - 10u - 1u] = 0;
    }
    put_u32(legacy.bytes + 8, version);
    resign(&legacy);
    return legacy;
}

static void test_legacy_projection(void) {
    const struct {
        bool mode, original_fast;
        uint8_t raw_control;
    } cases[] = {{true, false, 0x7e},  {true, false, 0xfc},  {false, true, 0xff},
                 {false, false, 0x00}, {false, false, 0x7c}, {true, true, 0x02}};

    for (unsigned scenario = 0; scenario < sizeof(cases) / sizeof(cases[0]); ++scenario) {
        CupidGb *source = new_machine(CUPID_GB_MODEL_CGB);
        source->cgb_mode = cases[scenario].mode;
        source->serial_fast_clock = cases[scenario].original_fast;
        source->io_registers[2] = cases[scenario].raw_control;
        source->io_registers[1] = 0x3c;
        source->serial_master_clock = true;
        source->serial_bits_remaining = source->io_registers[2] & 0x80 ? 3 : 0;
        source->serial_tx_latch = 0xa5;
        const bool projected_fast = source->cgb_mode && (source->io_registers[2] & 2u);
        Blob current = capture(source);
        for (unsigned version = 2; version <= 18; ++version) {
            Blob legacy = legacy_blob(&current, version);
            const bool expected_fast =
                version == 18 ? cases[scenario].original_fast : projected_fast;
            for (unsigned api = 0; api < 4; ++api) {
                CupidGb *targets[2] = {new_machine(CUPID_GB_MODEL_CGB),
                                       new_machine(CUPID_GB_MODEL_CGB)};
                SerialWitness witnesses[2] = {{0}, {0}};
                for (unsigned poison = 0; poison < 2; ++poison) {
                    CupidGb *target = targets[poison];
                    target->serial_fast_clock = poison != 0;
                    serial_test_observe(target, &witnesses[poison]);
                    assert(restore(target, &legacy, api) == CUPID_GB_STATE_OK);
                    assert(target->serial_fast_clock == expected_fast);
                    assert(target->cgb_mode == source->cgb_mode);
                    assert(target->io_registers[2] == cases[scenario].raw_control);
                    assert(cupid_gb_read_u8(target, 0xff02) ==
                           (uint8_t)(cases[scenario].raw_control | 0x7c));
                    assert(target->io_registers[2] == cases[scenario].raw_control);
                    assert(target->serial_master_clock && target->serial_tx_latch == 0xa5);
                    assert(target->serial_bits_remaining == source->serial_bits_remaining);
                    assert(target->serial_link.user_data == &witnesses[poison]);
                    assert(target->serial_output_user_data == &witnesses[poison]);
                    assert_quiet(&witnesses[poison]);
                    /* Published V18 preserves the saved selector even when legacy
                     * projection would choose a different clock. */
                    if (version == 18 && api < 2)
                        assert_encoded_equal(source, target);
                }
                assert_encoded_equal(targets[0], targets[1]);
                for (unsigned step = 0; step < 54; ++step) {
                    assert(cupid_gb_step(targets[0]));
                    assert(cupid_gb_step(targets[1]));
                }
                assert_encoded_equal(targets[0], targets[1]);
                assert(!memcmp(targets[0]->high_ram, targets[1]->high_ram, 3));
                for (unsigned index = 0; index < 2; ++index)
                    serial_test_free(targets[index]);
                if (version == 18)
                    ++published_cases;
                else
                    ++legacy_cases;
            }
            free(legacy.bytes);
        }
        free(current.bytes);
        serial_test_free(source);
    }
}

static void reject_unchanged(CupidGb *target, const Blob *blob) {
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    SerialWitness witness = {0};
    serial_test_observe(target, &witness);
    for (unsigned api = 0; api < 4; ++api) {
        memcpy(before, target, sizeof(*before));
        assert(restore(target, blob, api) == CUPID_GB_STATE_INVALID);
        assert(!memcmp(before, target, sizeof(*before)));
        assert_quiet(&witness);
        ++rejected_cases;
    }
    free(before);
}

static void test_invalid_selector(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_DMG_ABC};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        CupidGb *source = new_machine(models[model]);
        CupidGb *target = new_machine(models[model]);
        for (unsigned version = 18; version <= CUPID_GB_STATE_VERSION; ++version) {
            Blob current = capture(source);
            Blob blob = version <= 21 ? legacy_blob(&current, version) : current;
            const size_t selector = selector_offset(&blob);
            const uint8_t invalid[] = {2, 0x80, 0xff};
            for (unsigned index = 0; index < sizeof(invalid); ++index) {
                blob.bytes[selector] = invalid[index];
                resign(&blob);
                reject_unchanged(target, &blob);
            }
            if (models[model] != CUPID_GB_MODEL_CGB) {
                blob.bytes[selector] = 1;
                resign(&blob);
                reject_unchanged(target, &blob);
            }
            blob.bytes[selector] = 0;
            memmove(blob.bytes + selector, blob.bytes + selector + SELECTOR_BYTES,
                    blob.size - selector - SELECTOR_BYTES);
            blob.size -= SELECTOR_BYTES;
            resign(&blob);
            reject_unchanged(target, &blob);
            if (version <= 21)
                free(blob.bytes);
            free(current.bytes);
        }
        serial_test_free(source);
        serial_test_free(target);
    }
}

int main(void) {
    test_reset_selection_is_independent();
    test_native_continuation();
    test_legacy_projection();
    test_invalid_selector();
    printf("Serial state checks passed: %u native, %u legacy projections, %u published V18, "
           "%u rejected loads.\n",
           native_cases, legacy_cases, published_cases, rejected_cases);
    return 0;
}

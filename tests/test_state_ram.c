/**
 * @file    test_state_ram.c
 * @brief   Extended SRAM snapshot ownership, compatibility and transactional failure checks.
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

#include "../src/gb/printer_internal.h"
#include "../src/gb/state_internal.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/rtc.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HEADER_BYTES = 128,
    DESCRIPTOR_BYTES = 8,
    PROTOCOL_BYTES = 85,
    SELECTOR_BYTES = 1,
    WAVE_HISTORY_BYTES = 1,
    RAM_LENGTH_BYTES = 4
};

typedef struct Blob {
    uint8_t *data;
    size_t size;
} Blob;

typedef struct Guard {
    CupidGb *core;
    uint8_t *ram;
    uint8_t *rom;
    CupidGbPrinterState *printer;
} Guard;

static int64_t fixed_time(void *context) {
    (void)context;
    return INT64_C(1700000000);
}

static CupidGb *create(CupidGbModel model, unsigned code) {
    uint8_t rom[0x8000] = {0};
    rom[0x100] = 0xc3;
    rom[0x101] = 0x60;
    rom[0x102] = 1;
    rom[0x160] = 0x18;
    rom[0x161] = 0xfe;
    memcpy(rom + 0x134, "SRAM STATE", 10);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x147] = 0xbc;
    rom[0x149] = 0xc1;
    rom[0x14a] = 0x65;
    rom[0x152] = (uint8_t)code;
    rom[0x153] = 8;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x5352414d53544154));
    cupid_gb_set_rtc_time_source(gb, fixed_time, NULL);
    assert(cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE));
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(gb->cartridge_ram_size == (code ? (size_t)0x2000 << (code - 1) : 0));
    return gb;
}

static void destroy(CupidGb *gb) {
    CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
    cupid_gb_set_serial_link(gb, (CupidGbSerialLink){0});
    if (printer)
        cupid_gb_printer_destroy(printer);
    cupid_gb_cleanup(gb);
    free(gb);
}

static size_t storage_capacity(const CupidGb *gb) {
    return gb->cartridge_ram_heap ? gb->cartridge_ram_capacity : CUPID_GB_INLINE_RAM_SIZE;
}

static void fill_storage(CupidGb *gb, unsigned salt) {
    uint8_t *data = cupid_gb_cartridge_ram(gb);
    for (size_t i = 0; i < storage_capacity(gb); ++i)
        data[i] = (uint8_t)(i * 17u + (i >> 8) * 31u + (i >> 13) * 47u + salt);
}

static void write_u32(uint8_t *data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data[i] = (uint8_t)(value >> (8 * i));
}

static void seal(Blob blob) {
    write_u32(blob.data + 12, (uint32_t)blob.size);
    cupid_sha256_parts(blob.data, 96, blob.data + HEADER_BYTES, blob.size - HEADER_BYTES,
                       blob.data + 96);
}

static Blob save(const CupidGb *gb) {
    Blob blob = {NULL, cupid_gb_state_size(gb)};
    assert(blob.size);
    blob.data = malloc(blob.size + 16);
    assert(blob.data);
    memset(blob.data, 0xa5, blob.size + 16);
    size_t written = 0;
    assert(cupid_gb_save_state(gb, blob.data, blob.size, &written) == CUPID_GB_STATE_OK);
    assert(written == blob.size && blob.data[8] == CUPID_GB_STATE_VERSION);
    for (size_t i = blob.size; i < blob.size + 16; ++i)
        assert(blob.data[i] == 0xa5);
    return blob;
}

static void same_snapshot(const CupidGb *gb, Blob expected) {
    Blob actual = save(gb);
    assert(actual.size == expected.size);
    assert(!memcmp(actual.data, expected.data, actual.size));
    free(actual.data);
}

static Guard guard(const CupidGb *gb) {
    Guard saved = {0};
    saved.core = malloc(sizeof(*gb));
    saved.ram = malloc(storage_capacity(gb));
    saved.rom = malloc(gb->rom_capacity);
    assert(saved.core && saved.ram && saved.rom);
    memcpy(saved.core, gb, sizeof(*gb));
    memcpy(saved.ram, cupid_gb_cartridge_ram_const(gb), storage_capacity(gb));
    memcpy(saved.rom, gb->rom, gb->rom_capacity);
    const CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
    if (printer) {
        saved.printer = malloc(sizeof(printer->state));
        assert(saved.printer);
        *saved.printer = printer->state;
    }
    return saved;
}

static void unchanged(const CupidGb *gb, const Guard *saved) {
    assert(!memcmp(gb, saved->core, sizeof(*gb)));
    assert(!memcmp(cupid_gb_cartridge_ram_const(gb), saved->ram, storage_capacity(gb)));
    assert(!memcmp(gb->rom, saved->rom, gb->rom_capacity));
    if (saved->printer) {
        const CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
        assert(printer && !memcmp(&printer->state, saved->printer, sizeof(printer->state)));
    }
}

static void release_guard(Guard saved) {
    /* The copied structure borrows its recorded pointers; only these copies own memory. */
    free(saved.core);
    free(saved.ram);
    free(saved.rom);
    free(saved.printer);
}

static CupidGbStateResult load_api(CupidGb *gb, Blob blob, unsigned api,
                                   const CupidGbStateContext *context) {
    if (api == 0)
        return cupid_gb_load_state(gb, blob.data, blob.size);
    if (api == 1)
        return cupid_gb_load_state_with_context(gb, blob.data, blob.size, context);
    if (api == 2)
        return cupid_gb_load_state_live(gb, blob.data, blob.size);
    return cupid_gb_load_state_live_with_context(gb, blob.data, blob.size, context);
}

static void reject_all(CupidGb *gb, Blob blob, CupidGbStateResult expected) {
    CupidGbStateContext context;
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    Guard saved = guard(gb);
    for (unsigned api = 0; api < 4; ++api) {
        assert(load_api(gb, blob, api, &context) == expected);
        unchanged(gb, &saved);
    }
    release_guard(saved);
}

static size_t ram_extension(const CupidGb *gb) {
    return RAM_LENGTH_BYTES + (gb->cartridge_ram_size > CUPID_GB_INLINE_RAM_SIZE
                                   ? gb->cartridge_ram_size - CUPID_GB_INLINE_RAM_SIZE
                                   : 0);
}

static Blob legacy_blob(const CupidGb *gb, Blob current, unsigned version) {
    assert(version == 17 || version == 18);
    const size_t removed = PROTOCOL_BYTES + ram_extension(gb) + WAVE_HISTORY_BYTES +
                           (version < 18 ? SELECTOR_BYTES : 0);
    const size_t prefix = current.size - DESCRIPTOR_BYTES - removed;
    Blob old = {malloc(current.size - removed), current.size - removed};
    assert(old.data && !cupid_gb_printer_attached(gb));
    memcpy(old.data, current.data, prefix);
    memcpy(old.data + prefix, current.data + current.size - DESCRIPTOR_BYTES, DESCRIPTOR_BYTES);
    write_u32(old.data + 8, version);
    seal(old);
    return old;
}

/* A public V18 header describes the serial selector without an SRAM extension.
 * The older unpublished SRAM experiment omitted that selector and fetch history. */
static Blob invalid_v18_ram_extension(const CupidGb *gb, Blob current) {
    const size_t selector = current.size - DESCRIPTOR_BYTES - PROTOCOL_BYTES - ram_extension(gb) -
                            WAVE_HISTORY_BYTES - SELECTOR_BYTES;
    const size_t removed = SELECTOR_BYTES + WAVE_HISTORY_BYTES + PROTOCOL_BYTES;
    Blob invalid = {malloc(current.size - removed), current.size - removed};
    assert(invalid.data && !cupid_gb_printer_attached(gb));
    memcpy(invalid.data, current.data, selector);
    memcpy(invalid.data + selector, current.data + selector + SELECTOR_BYTES + WAVE_HISTORY_BYTES,
           ram_extension(gb));
    memcpy(invalid.data + selector + ram_extension(gb),
           current.data + current.size - DESCRIPTOR_BYTES, DESCRIPTOR_BYTES);
    write_u32(invalid.data + 8, 18);
    seal(invalid);
    return invalid;
}

static void report_identity(CupidGbModel model, unsigned code, Blob blob) {
    uint8_t digest[32];
    cupid_sha256(blob.data, blob.size, digest);
    printf("SRAM state model=%u code=%u bytes=%zu sha256=", (unsigned)model, code, blob.size);
    for (size_t i = 0; i < sizeof(digest); ++i)
        printf("%02x", digest[i]);
    putchar('\n');
}

static void roundtrip(CupidGbModel model, unsigned code) {
    CupidGb *source = create(model, code);
    CupidGb *target = create(model, code);
    fill_storage(source, 7);
    for (unsigned step = 0; step < 128; ++step)
        assert(cupid_gb_step(source));
    source->joypad = 0x7f;
    source->joyp_sample_pending = true;
    Blob snapshot = save(source);
    const CupidGbCpu cpu = source->cpu;
    const bool saved_selector = source->serial_fast_clock;
    const uint8_t projected_wave_byte = (uint8_t)(source->apu.ch3_sample * 0x11u);
    const bool projected_selector = source->cgb_mode && (source->io_registers[2] & 2u);
    report_identity(model, code, snapshot);
    CupidGbStateContext context;
    assert(cupid_gb_state_context_init(&context, target) == CUPID_GB_STATE_OK);
    uint8_t *const rom = target->rom;
    const size_t rom_capacity = target->rom_capacity;
    const uint64_t generation = target->rom_generation;
    for (unsigned api = 0; api < 4; ++api) {
        fill_storage(target, 63 + api);
        for (unsigned step = 0; step < 32; ++step)
            assert(cupid_gb_step(target));
        CupidGbAudioOutput listening = target->apu.output;
        listening.sample_fraction = 0;
        listening.fraction_half = false;
        assert(load_api(target, snapshot, api, &context) == CUPID_GB_STATE_OK);
        assert(target->rom == rom && target->rom_capacity == rom_capacity &&
               target->rom_generation == generation);
        assert(!memcmp(cupid_gb_cartridge_ram_const(source), cupid_gb_cartridge_ram_const(target),
                       storage_capacity(source)));
        assert(!memcmp(&target->cpu, &cpu, sizeof(cpu)) && target->joyp_sample_pending);
        if (api < 2)
            same_snapshot(target, snapshot);
        else
            assert(!memcmp(&target->apu.output, &listening, sizeof(listening)));
    }
    assert(cupid_gb_load_state(target, snapshot.data, snapshot.size) == CUPID_GB_STATE_OK);
    for (unsigned step = 0; step < 128; ++step)
        assert(cupid_gb_step(source) && cupid_gb_step(target));
    Blob continuation = save(source);
    same_snapshot(target, continuation);
    free(continuation.data);

    for (unsigned version = 17; version <= 18; ++version) {
        Blob old = legacy_blob(source, snapshot, version);
        if (code <= 5) {
            Blob expected = {malloc(snapshot.size), snapshot.size};
            assert(expected.data);
            memcpy(expected.data, snapshot.data, snapshot.size);
            const size_t selector = snapshot.size - DESCRIPTOR_BYTES - PROTOCOL_BYTES -
                                    ram_extension(source) - WAVE_HISTORY_BYTES - SELECTOR_BYTES;
            const bool expected_selector = version == 18 ? saved_selector : projected_selector;
            expected.data[selector] = expected_selector;
            expected.data[selector + SELECTOR_BYTES] = projected_wave_byte;
            seal(expected);
            for (unsigned api = 0; api < 4; ++api) {
                fill_storage(target, 91 + api);
                target->serial_fast_clock =
                    target->model == CUPID_GB_MODEL_CGB && !expected_selector;
                CupidGbAudioOutput listening = target->apu.output;
                listening.sample_fraction = 0;
                listening.fraction_half = false;
                assert(load_api(target, old, api, &context) == CUPID_GB_STATE_OK);
                assert(target->serial_fast_clock == expected_selector);
                assert(!memcmp(&target->cpu, &cpu, sizeof(cpu)) && target->joyp_sample_pending);
                assert(!memcmp(cupid_gb_cartridge_ram_const(source),
                               cupid_gb_cartridge_ram_const(target), storage_capacity(source)));
                if (api < 2)
                    same_snapshot(target, expected);
                else
                    assert(!memcmp(&target->apu.output, &listening, sizeof(listening)));
            }
            free(expected.data);
        } else {
            reject_all(target, old, CUPID_GB_STATE_UNSUPPORTED_VERSION);
        }
        free(old.data);
    }
    Blob invalid = invalid_v18_ram_extension(source, snapshot);
    reject_all(target, invalid,
               code <= 5 ? CUPID_GB_STATE_INVALID : CUPID_GB_STATE_UNSUPPORTED_VERSION);
    free(invalid.data);
    free(snapshot.data);
    destroy(source);
    destroy(target);
}

static unsigned allocation_calls;
static unsigned failed_allocation;

static void *allocate_at(size_t size) {
    ++allocation_calls;
    return allocation_calls == failed_allocation ? NULL : malloc(size);
}

static void transactions(void) {
    CupidGb *source = create(CUPID_GB_MODEL_CGB, 9);
    CupidGb *target = create(CUPID_GB_MODEL_CGB, 9);
    fill_storage(source, 3);
    fill_storage(target, 129);
    CupidGbPrinter *source_printer = cupid_gb_printer_create(NULL, NULL, NULL);
    CupidGbPrinter *target_printer = cupid_gb_printer_create(NULL, NULL, NULL);
    assert(source_printer && target_printer);
    cupid_gb_set_serial_link(source, cupid_gb_printer_link(source_printer));
    cupid_gb_set_serial_link(target, cupid_gb_printer_link(target_printer));
    source_printer->state.pixels[0] = 2;
    target_printer->state.pixels[0] = 1;
    Blob valid = save(source);
    Guard saved = guard(target);
    for (unsigned live = 0; live < 2; ++live) {
        for (unsigned failure = 1; failure <= 3; ++failure) {
            allocation_calls = 0;
            failed_allocation = failure;
            assert(cupid_gb_load_state_with_allocator(target, valid.data, valid.size, live != 0,
                                                      allocate_at) == CUPID_GB_STATE_OUT_OF_MEMORY);
            assert(allocation_calls == failure);
            unchanged(target, &saved);
        }
    }
    assert(cupid_gb_load_state_with_allocator(target, valid.data, valid.size, false, NULL) ==
           CUPID_GB_STATE_INVALID);
    unchanged(target, &saved);
    release_guard(saved);

    Blob bad = {malloc(valid.size + 1), valid.size};
    assert(bad.data);
    const size_t descriptor = valid.size - PRINTER_STATE_WIRE_SIZE - DESCRIPTOR_BYTES;
    const size_t length_offset = descriptor - PROTOCOL_BYTES - ram_extension(source);
    const uint32_t bad_lengths[] = {0, CUPID_GB_INLINE_RAM_SIZE, CUPID_GB_MAX_RAM_SIZE - 1,
                                    CUPID_GB_MAX_RAM_SIZE + 1, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(bad_lengths) / sizeof(bad_lengths[0]); ++i) {
        memcpy(bad.data, valid.data, valid.size);
        write_u32(bad.data + length_offset, bad_lengths[i]);
        seal(bad);
        allocation_calls = 0;
        failed_allocation = 1;
        assert(cupid_gb_load_state_with_allocator(target, bad.data, bad.size, false, allocate_at) ==
               CUPID_GB_STATE_INVALID);
        assert(allocation_calls == 0);
        reject_all(target, bad, CUPID_GB_STATE_INVALID);
    }
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        memcpy(bad.data, valid.data, valid.size);
        bad.size = valid.size;
        if (scenario == 0)
            bad.data[HEADER_BYTES + 1] |= 1; /* Invalid F bits after SRAM has been decoded. */
        else if (scenario == 1)
            bad.data[descriptor + DESCRIPTOR_BYTES] = 7; /* Invalid late printer phase. */
        else if (scenario == 2)
            --bad.size;
        else if (scenario == 3)
            bad.data[bad.size++] = 0;
        else if (scenario == 4)
            bad.data[length_offset + RAM_LENGTH_BYTES + 0x1234] ^= 1;
        else if (scenario == 5)
            bad.data[length_offset - WAVE_HISTORY_BYTES - SELECTOR_BYTES] = 2;
        else {
            /* Removing the serial selector must not shift SRAM or printer data
             * into a different valid interpretation of the current format. */
            const size_t selector = length_offset - WAVE_HISTORY_BYTES - SELECTOR_BYTES;
            memmove(bad.data + selector, bad.data + selector + SELECTOR_BYTES,
                    bad.size - selector - SELECTOR_BYTES);
            bad.size -= SELECTOR_BYTES;
        }
        if (scenario != 4)
            seal(bad);
        reject_all(target, bad, CUPID_GB_STATE_INVALID);
    }
    failed_allocation = 0;
    allocation_calls = 0;
    assert(cupid_gb_load_state_with_allocator(target, valid.data, valid.size, false, allocate_at) ==
           CUPID_GB_STATE_OK);
    assert(allocation_calls == 3);
    same_snapshot(target, valid);
    free(bad.data);
    free(valid.data);
    destroy(source);
    destroy(target);
}

typedef enum WireLayout {
    WIRE_CURRENT,
    WIRE_V21,
    WIRE_V20,
    WIRE_V19_WAVE,
    WIRE_V19_SRAM
} WireLayout;

/* Frozen TPP1 placement through the public v18 serial selector, including its
 * 128-byte header. Keep this independent of the current size calculation. */
enum { TPP1_V18_HARDWARE_BYTES = 305103 };

static size_t descriptor_offset(const CupidGb *gb, Blob blob) {
    return blob.size - DESCRIPTOR_BYTES -
           (cupid_gb_printer_attached(gb) ? PRINTER_STATE_WIRE_SIZE : 0);
}

static Blob format_blob(const CupidGb *gb, Blob current, WireLayout layout) {
    const size_t history = TPP1_V18_HARDWARE_BYTES;
    const size_t descriptor = descriptor_offset(gb, current);
    assert(current.data[8] == CUPID_GB_STATE_VERSION);
    assert(descriptor == history + WAVE_HISTORY_BYTES + ram_extension(gb) + PROTOCOL_BYTES);
    assert(current.data[history] == gb->apu.ch3_sample_byte);
    if (layout <= WIRE_V20) {
        const size_t removed = layout == WIRE_CURRENT ? 0 : PROTOCOL_BYTES;
        Blob copy = {malloc(current.size - removed), current.size - removed};
        assert(copy.data);
        memcpy(copy.data, current.data, descriptor - removed);
        memcpy(copy.data + descriptor - removed, current.data + descriptor,
               current.size - descriptor);
        if (layout != WIRE_CURRENT) {
            write_u32(copy.data + 8, layout == WIRE_V21 ? 21 : 20);
            seal(copy);
        }
        return copy;
    }
    if (layout == WIRE_V19_WAVE) {
        assert(gb->cartridge_ram_size <= CUPID_GB_INLINE_RAM_SIZE);
        const size_t removed = ram_extension(gb) + PROTOCOL_BYTES;
        Blob old = {malloc(current.size - removed), current.size - removed};
        assert(old.data);
        memcpy(old.data, current.data, history + WAVE_HISTORY_BYTES);
        memcpy(old.data + history + WAVE_HISTORY_BYTES, current.data + descriptor,
               current.size - descriptor);
        write_u32(old.data + 8, 19);
        seal(old);
        return old;
    }
    assert(layout == WIRE_V19_SRAM);
    const size_t removed = WAVE_HISTORY_BYTES + PROTOCOL_BYTES;
    Blob old = {malloc(current.size - removed), current.size - removed};
    assert(old.data);
    memcpy(old.data, current.data, history);
    memcpy(old.data + history, current.data + history + WAVE_HISTORY_BYTES, ram_extension(gb));
    memcpy(old.data + history + ram_extension(gb), current.data + descriptor,
           current.size - descriptor);
    write_u32(old.data + 8, 19);
    seal(old);
    return old;
}

static void attach_printer(CupidGb *gb, uint8_t marker) {
    CupidGbPrinter *printer = cupid_gb_printer_create(NULL, NULL, NULL);
    assert(printer);
    cupid_gb_set_serial_link(gb, cupid_gb_printer_link(printer));
    printer->state.pixels[0] = marker;
}

static void draft_roundtrip(CupidGbModel model, unsigned code, unsigned attachments) {
    CupidGb *source = create(model, code);
    CupidGb *target = create(model, code);
    fill_storage(source, 17);
    source->apu.ch3_sample = 9;
    source->apu.ch3_sample_byte = 0x69;
    source->joypad = 0x7f;
    source->joyp_sample_pending = true;
    if (attachments & 1u)
        attach_printer(source, 2);
    if (attachments & 2u)
        attach_printer(target, 3);
    Blob current = save(source);
    const size_t extra = code > 5 ? source->cartridge_ram_size - CUPID_GB_INLINE_RAM_SIZE : 0;
    assert(current.size ==
           305116u + PROTOCOL_BYTES + extra + ((attachments & 1u) ? PRINTER_STATE_WIRE_SIZE : 0));
    CupidGbStateContext context;
    assert(cupid_gb_state_context_init(&context, target) == CUPID_GB_STATE_OK);
    for (unsigned layout = WIRE_CURRENT; layout <= WIRE_V19_SRAM; ++layout) {
        if (layout == WIRE_V19_WAVE && code > 5)
            continue;
        Blob snapshot = format_blob(source, current, (WireLayout)layout);
        const uint8_t expected_byte = layout == WIRE_V19_SRAM ? 0x99 : 0x69;
        for (unsigned api = 0; api < 4; ++api) {
            for (unsigned poison = 0; poison < 2; ++poison) {
                fill_storage(target, 53 + api + poison);
                target->apu.ch3_sample = poison ? 2 : 7;
                target->apu.ch3_sample_byte = poison ? 0xe1 : 0x3c;
                target->io_registers[0x30] = poison ? 0x87 : 0x12;
                target->camera_host.image[0] = poison ? 0xc3 : 0x3c;
                assert(cupid_gb_apu_set_sample_rate(target, poison ? 48000 : 32000));
                CupidGbAudioOutput listening = target->apu.output;
                listening.sample_fraction = 0;
                listening.fraction_half = false;
                CupidGbPrinter *target_printer = cupid_gb_printer_attached(target);
                if (target_printer)
                    target_printer->state.pixels[0] = 3;
                assert(load_api(target, snapshot, api, &context) == CUPID_GB_STATE_OK);
                assert(target->apu.ch3_sample_byte == expected_byte && target->apu.ch3_sample == 9);
                assert(target->io_registers[0x30] == source->io_registers[0x30]);
                assert(target->camera_host.image[0] == (poison ? 0xc3 : 0x3c));
                assert(!memcmp(cupid_gb_cartridge_ram_const(source),
                               cupid_gb_cartridge_ram_const(target), storage_capacity(source)));
                assert(!memcmp(&source->cpu, &target->cpu, sizeof(source->cpu)));
                assert(target->joyp_sample_pending && target->joypad == source->joypad);
                assert(target->serial_fast_clock == source->serial_fast_clock);
                if (api >= 2)
                    assert(!memcmp(&target->apu.output, &listening, sizeof(listening)));
                else
                    assert(!memcmp(&target->apu.output, &source->apu.output,
                                   sizeof(source->apu.output)));
                if (target_printer) {
                    const CupidGbPrinter *source_printer = cupid_gb_printer_attached(source);
                    if (source_printer) {
                        assert(!memcmp(&target_printer->state, &source_printer->state,
                                       sizeof(target_printer->state)));
                    } else {
                        CupidGbPrinterState *empty = malloc(sizeof(*empty));
                        assert(empty);
                        cupid_gb_printer_state_clear(empty, source->elapsed_cycles);
                        assert(!memcmp(&target_printer->state, empty, sizeof(*empty)));
                        free(empty);
                    }
                }
                /* Import always upgrades to the complete current layout. */
                Blob upgraded = save(target);
                assert(upgraded.data[TPP1_V18_HARDWARE_BYTES] == expected_byte);
                assert(descriptor_offset(target, upgraded) ==
                       TPP1_V18_HARDWARE_BYTES + WAVE_HISTORY_BYTES + ram_extension(target) +
                           PROTOCOL_BYTES);
                free(upgraded.data);
            }
        }
        free(snapshot.data);
    }
    free(current.data);
    destroy(target);
    destroy(source);
}

static void draft_rejections(unsigned code) {
    CupidGb *source = create(CUPID_GB_MODEL_CGB, code);
    CupidGb *target = create(CUPID_GB_MODEL_CGB, code);
    fill_storage(source, 11);
    fill_storage(target, 97);
    source->apu.ch3_sample = 9;
    source->apu.ch3_sample_byte = 0x69;
    target->apu.ch3_sample_byte = 0xe1;
    attach_printer(source, 2);
    attach_printer(target, 3);
    Blob current = save(source);
    for (unsigned layout = WIRE_CURRENT; layout <= WIRE_V19_SRAM; ++layout) {
        if (layout == WIRE_V19_WAVE && code > 5)
            continue;
        Blob valid = format_blob(source, current, (WireLayout)layout);
        const size_t descriptor = descriptor_offset(source, valid);
        Blob bad = {malloc(valid.size + 1), valid.size};
        assert(bad.data);
        for (unsigned scenario = 0; scenario < 14; ++scenario) {
            memcpy(bad.data, valid.data, valid.size);
            bad.size = valid.size;
            CupidGbStateResult expected = CUPID_GB_STATE_INVALID;
            if (scenario == 0)
                write_u32(bad.data + 12, (uint32_t)valid.size + 1);
            else if (scenario == 1)
                --bad.size;
            else if (scenario == 2)
                bad.data[bad.size++] = 0;
            else if (scenario == 3) {
                write_u32(bad.data + 8, CUPID_GB_STATE_VERSION + 1u);
                expected = CUPID_GB_STATE_UNSUPPORTED_VERSION;
            } else if (scenario == 4) {
                write_u32(bad.data + 16, CUPID_GB_MODEL_DMG_ABC);
                expected = CUPID_GB_STATE_WRONG_MODEL;
            } else if (scenario == 5) {
                write_u32(bad.data + 20, (uint32_t)source->rom_size + 1);
                expected = CUPID_GB_STATE_WRONG_ROM;
            } else if (scenario == 6) {
                bad.data[32] ^= 1;
                expected = CUPID_GB_STATE_WRONG_ROM;
            } else if (scenario == 7)
                write_u32(bad.data + 28, 2);
            else if (scenario == 8)
                bad.data[descriptor + 2] = 1;
            else if (scenario == 9)
                bad.data[descriptor] = 7;
            else if (scenario == 10)
                bad.data[descriptor + 1] = 2;
            else if (scenario == 11)
                write_u32(bad.data + descriptor + 4, PRINTER_STATE_WIRE_SIZE - 1);
            else if (scenario == 12)
                bad.data[descriptor + DESCRIPTOR_BYTES] = 7; /* Late printer decode. */
            else
                bad.data[HEADER_BYTES + 1] |= 1; /* Invalid F after candidate SRAM decode. */
            if (scenario != 0)
                seal(bad);
            reject_all(target, bad, expected);
        }
        if (layout != WIRE_V19_WAVE) {
            const size_t count_offset =
                TPP1_V18_HARDWARE_BYTES + (layout <= WIRE_V20 ? WAVE_HISTORY_BYTES : 0);
            const uint32_t counts[] = {0, 1, CUPID_GB_MAX_RAM_SIZE - 1, UINT32_MAX};
            for (unsigned i = 0; i < sizeof(counts) / sizeof(*counts); ++i) {
                memcpy(bad.data, valid.data, valid.size);
                bad.size = valid.size;
                write_u32(bad.data + count_offset, counts[i]);
                seal(bad);
                allocation_calls = 0;
                failed_allocation = 1;
                assert(cupid_gb_load_state_with_allocator(target, bad.data, bad.size, false,
                                                          allocate_at) == CUPID_GB_STATE_INVALID);
                assert(!allocation_calls);
                reject_all(target, bad, CUPID_GB_STATE_INVALID);
            }
        }
        /* Removing a tail byte cannot silently select another draft layout. */
        memcpy(bad.data, valid.data, valid.size);
        bad.size = valid.size - 1;
        memmove(bad.data + TPP1_V18_HARDWARE_BYTES, bad.data + TPP1_V18_HARDWARE_BYTES + 1,
                valid.size - TPP1_V18_HARDWARE_BYTES - 1);
        seal(bad);
        reject_all(target, bad, CUPID_GB_STATE_INVALID);
        if (code > 5) {
            Guard before = guard(target);
            for (unsigned live = 0; live < 2; ++live)
                for (unsigned failure = 1; failure <= 3; ++failure) {
                    allocation_calls = 0;
                    failed_allocation = failure;
                    assert(cupid_gb_load_state_with_allocator(target, valid.data, valid.size,
                                                              live != 0, allocate_at) ==
                           CUPID_GB_STATE_OUT_OF_MEMORY);
                    assert(allocation_calls == failure);
                    unchanged(target, &before);
                }
            release_guard(before);
        } else {
            /* Matching the receiving ROM identity cannot invent absent large SRAM. */
            CupidGb *large = create(CUPID_GB_MODEL_CGB, 9);
            memcpy(bad.data, valid.data, valid.size);
            bad.size = valid.size;
            cupid_sha256(large->rom, large->rom_size, bad.data + 32);
            seal(bad);
            reject_all(large, bad, CUPID_GB_STATE_INVALID);
            destroy(large);
        }
        failed_allocation = 0;
        free(bad.data);
        free(valid.data);
    }
    free(current.data);
    destroy(target);
    destroy(source);
}

static void input_aliases(void) {
    CupidGb *gb = create(CUPID_GB_MODEL_CGB, 9);
    fill_storage(gb, 27);
    Blob valid = save(gb);
    assert(valid.size > gb->cartridge_ram_capacity);
    /* A genuine header and prefix must not make the decoder read beyond the
     * known allocation while hashing the caller's claimed snapshot length. */
    memcpy(gb->cartridge_ram_heap, valid.data, gb->cartridge_ram_capacity);
    Blob invalid = {gb->cartridge_ram_heap, valid.size};
    reject_all(gb, invalid, CUPID_GB_STATE_INVALID);
    allocation_calls = 0;
    failed_allocation = 1;
    assert(cupid_gb_load_state_with_allocator(gb, invalid.data, invalid.size, false, allocate_at) ==
           CUPID_GB_STATE_INVALID);
    assert(allocation_calls == 0);

    invalid.data = gb->cartridge_ram_heap + 1;
    invalid.size = gb->cartridge_ram_capacity;
    reject_all(gb, invalid, CUPID_GB_STATE_INVALID);
    invalid.data = gb->rom + gb->rom_capacity - 64;
    invalid.size = HEADER_BYTES;
    reject_all(gb, invalid, CUPID_GB_STATE_INVALID);
    invalid.data = (uint8_t *)gb + sizeof(*gb) - 1;
    reject_all(gb, invalid, CUPID_GB_STATE_INVALID);
    free(valid.data);
    destroy(gb);
}

static void output_aliases(void) {
    CupidGb *gb = create(CUPID_GB_MODEL_DMG_ABC, 9);
    fill_storage(gb, 93);
    Blob output = save(gb);
    CupidGbStateContext context;
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    Guard saved = guard(gb);
    const size_t offsets[] = {0, CUPID_GB_INLINE_RAM_SIZE,
                              CUPID_GB_MAX_RAM_SIZE - sizeof(CupidGbStateContext)};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        uint8_t *alias = cupid_gb_cartridge_ram(gb) + offsets[i];
        size_t written = 17;
        assert(cupid_gb_save_state(gb, alias, output.size, &written) == CUPID_GB_STATE_INVALID);
        assert(!written);
        assert(cupid_gb_save_state_with_context(gb, alias, output.size, &written, &context) ==
               CUPID_GB_STATE_INVALID);
        assert(cupid_gb_save_state(gb, output.data, output.size, (size_t *)(void *)alias) ==
               CUPID_GB_STATE_INVALID);
        assert(cupid_gb_save_state_with_context(gb, output.data, output.size,
                                                (size_t *)(void *)alias,
                                                &context) == CUPID_GB_STATE_INVALID);
        assert(cupid_gb_state_context_init((CupidGbStateContext *)(void *)alias, gb) ==
               CUPID_GB_STATE_INVALID);
        unchanged(gb, &saved);
    }
    size_t written = 0;
    assert(cupid_gb_save_state(gb, gb->cartridge_ram_heap + gb->cartridge_ram_capacity - 1,
                               output.size, &written) == CUPID_GB_STATE_INVALID);
    assert(cupid_gb_save_state(gb, output.data, output.size,
                               (size_t *)(void *)(gb->cartridge_ram_heap +
                                                  gb->cartridge_ram_capacity - sizeof(size_t))) ==
           CUPID_GB_STATE_INVALID);
    unchanged(gb, &saved);
    release_guard(saved);
    free(output.data);
    destroy(gb);
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned code = 0; code <= 9; ++code)
            roundtrip(models[model], code);
    }
    transactions();
    const unsigned draft_codes[] = {0, 5, 9};
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned code = 0; code < sizeof(draft_codes) / sizeof(*draft_codes); ++code)
            for (unsigned attachments = 0; attachments < 4; ++attachments)
                draft_roundtrip(models[model], draft_codes[code], attachments);
    draft_rejections(5);
    draft_rejections(9);
    input_aliases();
    output_aliases();
    puts("Extended SRAM native-state checks passed.");
    return 0;
}

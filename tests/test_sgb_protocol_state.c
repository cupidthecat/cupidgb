/**
 * @file    test_sgb_protocol_state.c
 * @brief   Adapter header snapshots, pulse continuation, legacy defaults, and rollback checks.
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

#include "../src/gb/printer_internal.h"
#include "../src/gb/state_internal.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/input.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    NATIVE_HEADER_BYTES = 128,
    PROTOCOL_BYTES = 85,
    RECEIVED_BYTES = 84,
    ACCESSORY_BYTES = 8,
    PACKET_WRITES = 261,
    SGB_LEGACY_BYTES = 10119,
    EMPTY_STARTUP_BYTES = 65
};

typedef struct Blob {
    uint8_t *data;
    size_t size;
} Blob;

typedef struct Guard {
    CupidGb *core;
    uint8_t *heap;
    CupidGbPrinterState *printer;
} Guard;

static unsigned continuation_cases, legacy_cases, rejection_cases, allocation_calls;

static bool physical(CupidGbModel model) {
    return model == CUPID_GB_MODEL_SGB || model == CUPID_GB_MODEL_SGB2;
}

static CupidGb *create(CupidGbModel model, bool enhanced, bool boot, bool legacy_mapper) {
    uint8_t rom[32768] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    memcpy(rom + 0x134, "HEADER STATE", 12);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x146] = enhanced ? 3 : 0;
    rom[0x147] = legacy_mapper ? 0x22 : 0;
    rom[0x14b] = 0x33;
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, UINT64_C(0x4845414445525354));
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    if (boot) {
        gb->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100;
        memset(gb->boot_rom, 0, sizeof(gb->boot_rom));
        cupid_gb_start_boot_rom(gb);
        assert(gb->boot_rom_enabled);
    } else {
        cupid_gb_write_u8(gb, 0xff40, 0);
        cupid_gb_write_u8(gb, 0xff26, 0);
        cupid_gb_apu_run(gb, true);
    }
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

static void attach_printer(CupidGb *gb, uint8_t marker) {
    CupidGbPrinter *printer = cupid_gb_printer_create(NULL, NULL, NULL);
    assert(printer);
    cupid_gb_set_serial_link(gb, cupid_gb_printer_link(printer));
    printer->state.pixels[0] = marker;
}

static void put_u32(uint8_t *data, uint32_t value) {
    for (unsigned index = 0; index < 4; ++index)
        data[index] = (uint8_t)(value >> (8u * index));
}

static void seal(Blob blob) {
    assert(blob.size >= NATIVE_HEADER_BYTES);
    put_u32(blob.data + 12, (uint32_t)blob.size);
    cupid_sha256_parts(blob.data, 96, blob.data + NATIVE_HEADER_BYTES,
                       blob.size - NATIVE_HEADER_BYTES, blob.data + 96);
}

static Blob capture(const CupidGb *gb) {
    Blob blob = {NULL, cupid_gb_state_size(gb)};
    blob.data = malloc(blob.size + 1);
    uint8_t *trusted = malloc(blob.size);
    CupidGb *before = malloc(sizeof(*before));
    assert(blob.size && blob.data && trusted && before);
    memcpy(before, gb, sizeof(*before));
    blob.data[blob.size] = 0xa7;
    CupidGbStateContext context;
    size_t first = 0, second = 0;
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    assert(cupid_gb_save_state(gb, blob.data, blob.size, &first) == CUPID_GB_STATE_OK);
    assert(cupid_gb_save_state_with_context(gb, trusted, blob.size, &second, &context) ==
           CUPID_GB_STATE_OK);
    assert(first == blob.size && second == blob.size && blob.data[blob.size] == 0xa7);
    assert(blob.data[8] == 22 && !memcmp(blob.data, trusted, blob.size));
    assert(!memcmp(before, gb, sizeof(*before)));
    free(before);
    free(trusted);
    return blob;
}

static CupidGbStateResult load(CupidGb *gb, Blob blob, unsigned api) {
    CupidGbStateContext context;
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    switch (api) {
        case 0:
            return cupid_gb_load_state(gb, blob.data, blob.size);
        case 1:
            return cupid_gb_load_state_with_context(gb, blob.data, blob.size, &context);
        case 2:
            return cupid_gb_load_state_live(gb, blob.data, blob.size);
        default:
            assert(api == 3);
            return cupid_gb_load_state_live_with_context(gb, blob.data, blob.size, &context);
    }
}

static size_t protocol_offset(const CupidGb *gb, Blob blob) {
    return blob.size - ACCESSORY_BYTES - PROTOCOL_BYTES -
           (cupid_gb_printer_attached(gb) ? PRINTER_STATE_WIRE_SIZE : 0);
}

static void wire_matches(const CupidGb *gb, Blob blob) {
    const size_t offset = protocol_offset(gb, blob);
    assert(blob.data[offset] == (gb->sgb.commands_disabled ? 1 : 0));
    assert(!memcmp(blob.data + offset + 1, gb->sgb.received_header, RECEIVED_BYTES));
    if (!physical(gb->model))
        for (unsigned index = 0; index < PROTOCOL_BYTES; ++index)
            assert(!blob.data[offset + index]);
}

static void expected_header(uint8_t bytes[RECEIVED_BYTES]) {
    for (unsigned index = 0; index < RECEIVED_BYTES; ++index)
        bytes[index] = (uint8_t)(13u + 7u * index);
    bytes[0x42] = 3;
    bytes[0x47] = 0x33;
}

static void header_packet(uint8_t packet[16], unsigned section,
                          const uint8_t bytes[RECEIVED_BYTES]) {
    assert(section < 6);
    memset(packet, 0, 16);
    packet[0] = (uint8_t)(0xf1u + 2u * section);
    memcpy(packet + 2, bytes + 14u * section, 14);
    for (unsigned index = 2; index < 16; ++index)
        packet[1] = (uint8_t)(packet[1] + packet[index]);
}

static void packet_writes(uint8_t writes[PACKET_WRITES], const uint8_t packet[16]) {
    writes[0] = 0x30;
    writes[1] = 0;
    writes[2] = 0x30;
    for (unsigned bit = 0; bit < 128; ++bit) {
        writes[3 + 2 * bit] = packet[bit / 8] & (1u << (bit % 8)) ? 0x10 : 0x20;
        writes[4 + 2 * bit] = 0x30;
    }
    writes[259] = 0x20;
    writes[260] = 0x30;
}

static void issue(CupidGb *gb, const uint8_t *writes, unsigned first, unsigned end) {
    assert(first <= end && end <= PACKET_WRITES);
    for (unsigned index = first; index < end; ++index)
        cupid_gb_write_u8(gb, 0xff00, writes[index]);
}

static void send(CupidGb *gb, const uint8_t packet[16]) {
    uint8_t writes[PACKET_WRITES];
    packet_writes(writes, packet);
    issue(gb, writes, 0, PACKET_WRITES);
}

static unsigned checkpoint(CupidGb *gb, unsigned stage, uint8_t writes[PACKET_WRITES]) {
    uint8_t bytes[RECEIVED_BYTES], packet[16];
    expected_header(bytes);
    for (unsigned section = 0; section < 5; ++section) {
        header_packet(packet, section, bytes);
        send(gb, packet);
    }
    assert(!memcmp(gb->sgb.received_header, bytes, 70));
    header_packet(packet, 5, bytes);
    packet_writes(writes, packet);
    /* Stop with a data pulse low, before stop, with the stop pulse low, or after
     * completion. Every subsequent assertion observes the same public writes. */
    static const unsigned end[] = {150, 259, 260, PACKET_WRITES, PACKET_WRITES};
    assert(stage < sizeof(end) / sizeof(end[0]));
    issue(gb, writes, 0, end[stage]);
    if (stage < 3) {
        assert(gb->sgb.command_write_index == (stage ? 128 : 73));
        assert(gb->sgb.ready_for_stop == (stage != 0));
    }
    if (stage == 4) {
        const uint8_t players[16] = {0x89, 3};
        send(gb, players);
        header_packet(packet, 2, bytes);
        packet[1] ^= 1;
        send(gb, packet);
        assert(gb->sgb.commands_disabled && gb->sgb.player_count == 4);
    } else {
        assert(!gb->sgb.commands_disabled);
    }
    return end[stage];
}

static void finish(CupidGb *gb, const uint8_t writes[PACKET_WRITES], unsigned cursor,
                   bool disabled) {
    issue(gb, writes, cursor, PACKET_WRITES);
    uint8_t expected[RECEIVED_BYTES];
    expected_header(expected);
    assert(!memcmp(gb->sgb.received_header, expected, sizeof(expected)));
    const uint8_t players[16] = {0x89, disabled ? 0 : 3};
    send(gb, players);
    assert(gb->sgb.commands_disabled == disabled && gb->sgb.player_count == 4);
    assert(!gb->sgb.command_write_index && !gb->sgb.ready_for_stop);
}

static void same_protocol(const CupidGb *first, const CupidGb *second) {
    assert(first->sgb.enabled == second->sgb.enabled);
    assert(first->sgb.commands_disabled == second->sgb.commands_disabled);
    assert(!memcmp(first->sgb.received_header, second->sgb.received_header, RECEIVED_BYTES));
    assert(!memcmp(first->sgb.command, second->sgb.command, sizeof(first->sgb.command)));
    assert(first->sgb.command_write_index == second->sgb.command_write_index);
    assert(first->sgb.ready_for_pulse == second->sgb.ready_for_pulse);
    assert(first->sgb.ready_for_write == second->sgb.ready_for_write);
    assert(first->sgb.ready_for_stop == second->sgb.ready_for_stop);
    assert(first->sgb.player_count == second->sgb.player_count);
    assert(first->sgb.current_player == second->sgb.current_player);
    assert(cupid_gb_input_read(first) == cupid_gb_input_read(second));
}

static void poison(CupidGb *gb, bool disabled) {
    gb->sgb.commands_disabled = disabled;
    memset(gb->sgb.received_header, 0xc3, sizeof(gb->sgb.received_header));
    memset(gb->sgb.command, 0x5a, sizeof(gb->sgb.command));
    gb->sgb.command_write_index = 11;
    gb->sgb.ready_for_write = true;
    gb->sgb.ready_for_stop = false;
    assert(cupid_gb_apu_set_sample_rate(gb, 48000));
    gb->apu.output.buf_write = 2;
    for (unsigned index = 0; index < 4; ++index)
        gb->apu.output.buf[index] = (int16_t)(100 + index * 13);
}

static void pulse_continuations(void) {
    for (unsigned model = CUPID_GB_MODEL_SGB; model <= CUPID_GB_MODEL_SGB2; ++model)
        for (unsigned enhanced = 0; enhanced < 2; ++enhanced)
            for (unsigned stage = 0; stage < 5; ++stage) {
                CupidGb *source = create((CupidGbModel)model, enhanced != 0, true, false);
                uint8_t writes[PACKET_WRITES];
                const unsigned cursor = checkpoint(source, stage, writes);
                Blob saved = capture(source);
                wire_matches(source, saved);
                CupidGb *boundary = malloc(sizeof(*boundary));
                assert(boundary);
                memcpy(boundary, source, sizeof(*boundary));
                /* The expected continuation executes without loading the snapshot. */
                finish(source, writes, cursor, stage == 4);
                for (unsigned api = 0; api < 4; ++api) {
                    CupidGb *target = create((CupidGbModel)model, enhanced != 0, true, false);
                    poison(target, stage != 4);
                    CupidGbAudioOutput listening = target->apu.output;
                    const CupidGbSgbStartupAudio intro = target->sgb_startup.output;
                    listening.sample_fraction = 0;
                    listening.fraction_half = false;
                    assert(load(target, saved, api) == CUPID_GB_STATE_OK);
                    same_protocol(target, boundary);
                    if (api < 2) {
                        Blob restored = capture(target);
                        assert(restored.size == saved.size &&
                               !memcmp(restored.data, saved.data, saved.size));
                        free(restored.data);
                    } else {
                        assert(!memcmp(&target->apu.output, &listening, sizeof(listening)));
                        assert(!memcmp(&target->sgb_startup.output, &intro, sizeof(intro)));
                    }
                    finish(target, writes, cursor, stage == 4);
                    same_protocol(source, target);
                    ++continuation_cases;
                    destroy(target);
                }
                free(boundary);
                free(saved.data);
                destroy(source);
            }
}

static Guard guard(const CupidGb *gb) {
    Guard result = {malloc(sizeof(*gb)), NULL, NULL};
    assert(result.core);
    memcpy(result.core, gb, sizeof(*gb));
    if (gb->cartridge_ram_heap) {
        result.heap = malloc(gb->cartridge_ram_capacity);
        assert(result.heap);
        memcpy(result.heap, gb->cartridge_ram_heap, gb->cartridge_ram_capacity);
    }
    const CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
    if (printer) {
        result.printer = malloc(sizeof(printer->state));
        assert(result.printer);
        memcpy(result.printer, &printer->state, sizeof(printer->state));
    }
    return result;
}

static void unchanged(const CupidGb *gb, const Guard *before) {
    assert(!memcmp(gb, before->core, sizeof(*gb)));
    if (before->heap)
        assert(!memcmp(gb->cartridge_ram_heap, before->heap, gb->cartridge_ram_capacity));
    if (before->printer) {
        const CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
        assert(printer && !memcmp(&printer->state, before->printer, sizeof(printer->state)));
    }
}

static void *deny_allocation(size_t size) {
    (void)size;
    ++allocation_calls;
    return NULL;
}

static void reject(CupidGb *gb, Blob invalid, CupidGbStateResult expected) {
    Guard before = guard(gb);
    seal(invalid);
    for (unsigned api = 0; api < 4; ++api) {
        assert(load(gb, invalid, api) == expected);
        unchanged(gb, &before);
        ++rejection_cases;
    }
    for (unsigned live = 0; live < 2; ++live) {
        allocation_calls = 0;
        assert(cupid_gb_load_state_with_allocator(gb, invalid.data, invalid.size, live != 0,
                                                  deny_allocation) == expected);
        assert(!allocation_calls);
        unchanged(gb, &before);
    }
    free(before.printer);
    free(before.heap);
    free(before.core);
}

static void structural_checks(void) {
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb *source = create((CupidGbModel)model, false, false, false);
        CupidGb *target = create((CupidGbModel)model, false, false, false);
        if (physical((CupidGbModel)model)) {
            /* Cached payload is opaque: no checksum or packet-order rule belongs
             * in the codec, including when the receiver has already disabled. */
            memset(source->sgb.received_header, 0xff, RECEIVED_BYTES);
            source->sgb.commands_disabled = true;
            attach_printer(source, 2);
            attach_printer(target, 3);
        }
        Blob saved = capture(source);
        wire_matches(source, saved);
        for (unsigned api = 0; api < 4; ++api) {
            poison(target, false);
            assert(load(target, saved, api) == CUPID_GB_STATE_OK);
            same_protocol(source, target);
        }
        const size_t offset = protocol_offset(source, saved);
        Blob invalid = {malloc(saved.size + 1), saved.size};
        assert(invalid.data);
        const uint8_t invalid_flags[] = {2, 0xff};
        for (unsigned index = 0; index < sizeof(invalid_flags); ++index) {
            memcpy(invalid.data, saved.data, saved.size);
            invalid.data[offset] = invalid_flags[index];
            reject(target, invalid, CUPID_GB_STATE_INVALID);
        }
        if (!physical((CupidGbModel)model)) {
            const unsigned positions[] = {0, 1, 43, 84};
            for (unsigned index = 0; index < sizeof(positions) / sizeof(positions[0]); ++index) {
                memcpy(invalid.data, saved.data, saved.size);
                invalid.data[offset + positions[index]] = 1;
                reject(target, invalid, CUPID_GB_STATE_INVALID);
            }
            source->sgb.received_header[83] = 1;
            assert(!cupid_gb_state_size(source));
            size_t written = 7;
            assert(cupid_gb_save_state(source, invalid.data, invalid.size, &written) ==
                   CUPID_GB_STATE_INVALID);
            assert(!written);
            source->sgb.received_header[83] = 0;
            source->sgb.commands_disabled = true;
            assert(!cupid_gb_state_size(source));
            source->sgb.commands_disabled = false;
        }
        const unsigned missing[] = {1, 42, PROTOCOL_BYTES};
        for (unsigned index = 0; index < sizeof(missing) / sizeof(missing[0]); ++index) {
            memcpy(invalid.data, saved.data, saved.size);
            memmove(invalid.data + offset, invalid.data + offset + missing[index],
                    saved.size - offset - missing[index]);
            invalid.size = saved.size - missing[index];
            reject(target, invalid, CUPID_GB_STATE_INVALID);
        }
        memcpy(invalid.data, saved.data, saved.size);
        memmove(invalid.data + offset + 1, invalid.data + offset, saved.size - offset);
        invalid.data[offset] = 0;
        invalid.size = saved.size + 1;
        reject(target, invalid, CUPID_GB_STATE_INVALID);
        memcpy(invalid.data, saved.data, saved.size);
        invalid.size = saved.size;
        put_u32(invalid.data + 16,
                model == CUPID_GB_MODEL_SGB ? CUPID_GB_MODEL_DMG_ABC : CUPID_GB_MODEL_SGB);
        reject(target, invalid, CUPID_GB_STATE_WRONG_MODEL);
        free(invalid.data);
        free(saved.data);
        destroy(source);
        destroy(target);
    }
}

/* These literal MBC7 lengths include the header and, from v12, an empty accessory.
 * The adapter transfer section adds 10,119 bytes from v5; only v21 has startup
 * history. The alternate v19 SRAM layout has three more bytes than its wave peer. */
static Blob legacy_blob(const CupidGb *gb, Blob current, unsigned version, bool ram_draft) {
    static const size_t lengths[] = {300518, 300568, 300572, 300580, 300584, 300585, 300596,
                                     300602, 300606, 300608, 300622, 300628, 305062, 305096,
                                     305097, 305097, 305098, 305099, 305103, 305103};
    assert(version >= 2 && version <= 21 && (!ram_draft || version == 19));
    const size_t border = physical(gb->model) && version >= 5 ? SGB_LEGACY_BYTES : 0;
    const size_t startup = physical(gb->model) && version == 21 ? EMPTY_STARTUP_BYTES : 0;
    Blob old = {NULL, lengths[version - 2] + border + startup + (ram_draft ? 3u : 0u)};
    old.data = malloc(old.size);
    assert(old.data);
    const size_t hardware = old.size - (version >= 12 ? ACCESSORY_BYTES : 0);
    memcpy(old.data, current.data, hardware);
    if (ram_draft) {
        const size_t wave = 305090u + border;
        memcpy(old.data + wave, current.data + wave + 1, 4);
    }
    if (version >= 12)
        memcpy(old.data + hardware, current.data + current.size - ACCESSORY_BYTES, ACCESSORY_BYTES);
    if (version == 15 || version == 16)
        old.data[hardware - (version == 16 ? 1u : 0u) - 11u] = 0;
    put_u32(old.data + 8, version);
    seal(old);
    return old;
}

static void legacy_projection(void) {
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model)
        for (unsigned declared = 0; declared <= physical((CupidGbModel)model); ++declared) {
            CupidGb *source = create((CupidGbModel)model, declared != 0, false, true);
            CupidGb *target = create((CupidGbModel)model, declared != 0, false, true);
            const bool disabled = physical((CupidGbModel)model) && !source->sgb.enabled;
            if (physical((CupidGbModel)model)) {
                memset(source->sgb.received_header, 0x69, RECEIVED_BYTES);
                source->sgb.commands_disabled = !disabled;
            }
            Blob current = capture(source);
            assert(
                current.size ==
                305103u + PROTOCOL_BYTES +
                    (physical((CupidGbModel)model) ? SGB_LEGACY_BYTES + EMPTY_STARTUP_BYTES : 0));
            for (unsigned version = 2; version <= 21; ++version)
                for (unsigned draft = 0; draft <= (version == 19); ++draft) {
                    Blob old = legacy_blob(source, current, version, draft != 0);
                    for (unsigned api = 0; api < 4; ++api) {
                        poison(target, !disabled);
                        target->sgb.enabled = !source->sgb.enabled;
                        assert(load(target, old, api) == CUPID_GB_STATE_OK);
                        assert(target->sgb.enabled == source->sgb.enabled);
                        assert(target->sgb.commands_disabled == disabled);
                        for (unsigned byte = 0; byte < RECEIVED_BYTES; ++byte)
                            assert(!target->sgb.received_header[byte]);
                        Blob upgraded = capture(target);
                        wire_matches(target, upgraded);
                        free(upgraded.data);
                        ++legacy_cases;
                    }
                    free(old.data);
                }
            free(current.data);
            destroy(source);
            destroy(target);
        }
}

static void legacy_partial_header(void) {
    CupidGb *source = create(CUPID_GB_MODEL_SGB, true, true, false);
    uint8_t writes[PACKET_WRITES];
    const unsigned cursor = checkpoint(source, 0, writes);
    Blob current = capture(source);
    const size_t offset = protocol_offset(source, current);
    Blob old = {malloc(current.size - PROTOCOL_BYTES), current.size - PROTOCOL_BYTES};
    assert(old.data);
    memcpy(old.data, current.data, offset);
    memcpy(old.data + offset, current.data + offset + PROTOCOL_BYTES, ACCESSORY_BYTES);
    put_u32(old.data + 8, 21);
    seal(old);
    finish(source, writes, cursor, false);
    for (unsigned api = 0; api < 4; ++api) {
        CupidGb *target = create(CUPID_GB_MODEL_SGB, true, true, false);
        poison(target, true);
        assert(load(target, old, api) == CUPID_GB_STATE_OK);
        assert(!target->sgb.commands_disabled && target->sgb.command_write_index == 73);
        issue(target, writes, cursor, PACKET_WRITES);
        /* The pending final packet survives, but the absent earlier cache cannot
         * satisfy its flag check. This legacy projection does not invent history. */
        assert(target->sgb.commands_disabled && !target->sgb.received_header[0x42]);
        assert(target->sgb.received_header[0x47] == 0x33);
        destroy(target);
    }
    free(old.data);
    free(current.data);
    destroy(source);
}

int main(void) {
    assert(CUPID_GB_STATE_VERSION == 22 && CUPID_SGB_HEADER_BYTES == RECEIVED_BYTES);
    pulse_continuations();
    structural_checks();
    legacy_projection();
    legacy_partial_header();
    assert(continuation_cases == 80 && legacy_cases == 672 && rejection_cases == 232);
    printf("Adapter protocol state checks passed: %u pulse continuations, %u legacy imports, "
           "%u public API rejections.\n",
           continuation_cases, legacy_cases, rejection_cases);
    return 0;
}

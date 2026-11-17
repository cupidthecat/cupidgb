/**
 * @file    test_wave_history_state.c
 * @brief   Native CH3 byte history, legacy projection and transactional restore checks.
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
#include "wave_history_fixture.h"

#include <stdio.h>

enum {
    HEADER_BYTES = 128,
    ACCESSORY_BYTES = 8,
    CURRENT_BYTES = 305095,
    SRAM_LENGTH_BYTES = 4,
    MBC7_EXTENSION_BYTES = 8,
    MBC7_CARTRIDGE_TYPE = 0x22
};

typedef struct Blob {
    uint8_t *bytes;
    size_t size;
} Blob;

static unsigned continuation_cases, draft_cases, pending_cases, byte_cases, legacy_cases,
    rejection_cases;

static Blob capture(const CupidGb *gb) {
    Blob blob = {malloc(cupid_gb_state_size(gb)), cupid_gb_state_size(gb)};
    assert(blob.bytes);
    size_t written = 0;
    assert(cupid_gb_save_state(gb, blob.bytes, blob.size, &written) == CUPID_GB_STATE_OK);
    const size_t extension = gb->header.mbc_type == CUPID_GB_MBC7 ? MBC7_EXTENSION_BYTES : 0;
    assert(written == blob.size && blob.size == CURRENT_BYTES + extension);
    assert(blob.bytes[8] == CUPID_GB_STATE_VERSION &&
           blob.bytes[blob.size - ACCESSORY_BYTES - SRAM_LENGTH_BYTES - 1] ==
               gb->apu.ch3_sample_byte);
    return blob;
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

/* Both read-only v19 drafts retain the public v18 prefix. These fixtures have
 * inline SRAM and no accessory payload, so only the one- or four-byte tail differs. */
static Blob draft_blob(const Blob *current, bool wave_history) {
    assert(current->bytes[8] == CUPID_GB_STATE_VERSION);
    const size_t wave = current->size - ACCESSORY_BYTES - SRAM_LENGTH_BYTES - 1;
    const size_t removed = wave_history ? SRAM_LENGTH_BYTES : 1;
    Blob draft = {malloc(current->size - removed), current->size - removed};
    assert(draft.bytes);
    if (wave_history) {
        memcpy(draft.bytes, current->bytes, wave + 1);
        memcpy(draft.bytes + wave + 1, current->bytes + current->size - ACCESSORY_BYTES,
               ACCESSORY_BYTES);
    } else {
        memcpy(draft.bytes, current->bytes, wave);
        memcpy(draft.bytes + wave, current->bytes + wave + 1, SRAM_LENGTH_BYTES + ACCESSORY_BYTES);
    }
    put_u32(draft.bytes + 8, 19);
    resign(&draft);
    return draft;
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

static void equal_state(const CupidGb *source, const CupidGb *target, bool live) {
    CupidGb *expected = malloc(sizeof(*expected));
    assert(expected);
    memcpy(expected, source, sizeof(*expected));
    if (live) {
        cupid_gb_apu_run(expected, true);
        expected->apu.output = target->apu.output;
    }
    Blob left = capture(expected), right = capture(target);
    assert(left.size == right.size && !memcmp(left.bytes, right.bytes, left.size));
    free(left.bytes);
    free(right.bytes);
    /* This comparison copy borrows the source's immutable session resources. */
    free(expected);
}

static CupidGb *different_receiver_cartridge(bool color, bool fast, unsigned poison,
                                             uint8_t cartridge_type) {
    CupidGb *gb = wave_history_new_cartridge(CUPID_GB_MODEL_CGB, color, fast,
                                             WAVE_HISTORY_HOLD_FREQUENCY, cartridge_type);
    assert(cupid_gb_apu_set_sample_rate(gb, poison ? 44100 : 32000));
    wave_history_advance(gb, 134u + (poison ? 7u : 3u) * 128u + 24u);
    assert(gb->apu.ch3_sample_byte != WAVE_HISTORY_HELD_BYTE || gb->apu.ch3_sample != 9);
    gb->apu.ch3_sample_byte = poison ? 0xff : 0;
    cupid_gb_write_u8(gb, 0xff34, poison ? 0x3c : 0xc3);
    return gb;
}

static CupidGb *different_receiver(bool color, bool fast, unsigned poison) {
    return different_receiver_cartridge(color, fast, poison, 0);
}

static void finish_program(CupidGb *gb, bool after_trigger, uint8_t expected) {
    if (!after_trigger)
        assert(cupid_gb_step(gb) && gb->cpu.pc == 0x103);
    assert(cupid_gb_step(gb) && gb->cpu.pc == 0x105 && gb->cpu.a == expected);
    assert(cupid_gb_step(gb) && gb->cpu.pc == 0x107 && gb->high_ram[0] == expected);
}

static void test_draft_continuation(void) {
    for (unsigned color = 0; color < 2; ++color)
        for (unsigned fast = 0; fast < 2; ++fast)
            for (unsigned after = 0; after < 2; ++after)
                for (unsigned pending = 0; pending < 2; ++pending) {
                    CupidGb *source = wave_history_hold(color != 0, fast != 0);
                    if (after)
                        assert(cupid_gb_step(source) && source->cpu.pc == 0x103);
                    if (pending)
                        cupid_gb_tick_apu_divider(source, 4);
                    Blob current = capture(source);
                    for (unsigned wave = 0; wave < 2; ++wave) {
                        Blob saved = draft_blob(&current, wave != 0);
                        const uint8_t expected = wave ? 0x69 : (after ? 0x66 : 0x99);
                        source->apu.ch3_sample_byte = expected;
                        for (unsigned api = 0; api < 4; ++api)
                            for (unsigned poison = 0; poison < 2; ++poison) {
                                CupidGb *target = different_receiver(color != 0, fast != 0, poison);
                                assert(restore(target, &saved, api) == CUPID_GB_STATE_OK);
                                assert(target->apu.ch3_sample_byte == expected);
                                assert(target->apu.ch3_sample == (after ? 6 : 9));
                                assert(target->io_registers[0x34] == 0xe1);
                                assert(target->apu.pending_cycles == (api < 2 && pending ? 4 : 0));
                                equal_state(source, target, api >= 2);
                                finish_program(target, after != 0, (uint8_t)(expected >> 4));
                                wave_history_free(target);
                                ++draft_cases;
                            }
                        free(saved.bytes);
                    }
                    free(current.bytes);
                    wave_history_free(source);
                }
}

static void test_native_continuation(unsigned version) {
    assert(version == 20 || version == CUPID_GB_STATE_VERSION);
    for (unsigned color = 0; color < 2; ++color)
        for (unsigned fast = 0; fast < 2; ++fast)
            for (unsigned after = 0; after < 2; ++after)
                for (unsigned pending = 0; pending < 2; ++pending) {
                    CupidGb *source = wave_history_hold(color != 0, fast != 0);
                    if (after)
                        assert(cupid_gb_step(source) && source->cpu.pc == 0x103);
                    if (pending)
                        cupid_gb_tick_apu_divider(source, 4);
                    assert(source->apu.ch3_sample_byte == 0x69);
                    assert(source->apu.ch3_sample == (after ? 6 : 9));
                    Blob saved = capture(source);
                    put_u32(saved.bytes + 8, version);
                    resign(&saved);
                    for (unsigned api = 0; api < 4; ++api)
                        for (unsigned poison = 0; poison < 2; ++poison) {
                            CupidGb *target = different_receiver(color != 0, fast != 0, poison);
                            CupidGbAudioOutput *receiving = malloc(sizeof(*receiving));
                            assert(receiving);
                            memcpy(receiving, &target->apu.output, sizeof(*receiving));
                            receiving->sample_fraction = 0;
                            receiving->fraction_half = false;
                            assert(restore(target, &saved, api) == CUPID_GB_STATE_OK);
                            assert(target->apu.ch3_sample_byte == 0x69);
                            assert(target->apu.ch3_sample == (after ? 6 : 9));
                            assert(target->io_registers[0x34] == 0xe1);
                            assert(target->apu.pending_cycles == (api < 2 && pending ? 4 : 0));
                            if (api >= 2)
                                assert(!memcmp(receiving, &target->apu.output, sizeof(*receiving)));
                            equal_state(source, target, api >= 2);
                            finish_program(target, after != 0, 6);
                            free(receiving);
                            wave_history_free(target);
                            ++continuation_cases;
                        }
                    free(saved.bytes);
                    wave_history_free(source);
                }
}

static CupidGb *pending_fetch_source(void) {
    CupidGb *source =
        wave_history_new(CUPID_GB_MODEL_CGB, true, false, WAVE_HISTORY_HOLD_FREQUENCY);
    wave_history_advance(source, 134u + 7u * 128u + 32u);
    assert(source->apu.ch3_wave_pos == 8 && source->apu.ch3_sample_byte == 0x69);
    wave_history_arm(source, 0x110, 0xe1);
    assert(cupid_gb_step(source) && source->io_registers[0x34] == 0xe1);
    const unsigned deferred = source->apu.ch3_timer + 2u;
    cupid_gb_tick_apu_divider(source, (uint16_t)deferred);
    wave_history_arm(source, 0x100, 0x87);
    assert(source->apu.ch3_sample_byte == 0x69 && source->apu.ch3_sample == 6);
    return source;
}

static void test_pending_fetch_before_live_output(void) {
    CupidGb *source = pending_fetch_source();
    const unsigned deferred = source->apu.pending_cycles;
    Blob saved = capture(source);
    for (unsigned api = 0; api < 4; ++api) {
        CupidGb *target = different_receiver(true, false, api & 1u);
        const uint32_t receiving_rate = target->apu.output.sample_rate;
        assert(restore(target, &saved, api) == CUPID_GB_STATE_OK);
        if (api < 2) {
            assert(target->apu.pending_cycles == deferred && target->apu.ch3_sample_byte == 0x69);
        } else {
            assert(!target->apu.pending_cycles && target->apu.ch3_sample_byte == 0xe1);
            assert(target->apu.ch3_sample == 1 && target->apu.ch3_wave_pos == 9);
            assert(target->apu.output.sample_rate == receiving_rate);
        }
        equal_state(source, target, api >= 2);
        finish_program(target, false, 14);
        wave_history_free(target);
        ++pending_cases;
    }
    free(saved.bytes);
    wave_history_free(source);
}

static void test_full_byte_roundtrip(void) {
    CupidGb *source = wave_history_hold(true, false);
    CupidGb *target = different_receiver(true, false, 1);
    for (unsigned value = 0; value <= UINT8_MAX; ++value) {
        source->apu.ch3_sample_byte = (uint8_t)value;
        Blob saved = capture(source);
        for (unsigned api = 0; api < 4; ++api) {
            assert(restore(target, &saved, api) == CUPID_GB_STATE_OK);
            assert(target->apu.ch3_sample_byte == value && target->apu.ch3_sample == 9);
            assert(target->io_registers[0x34] == 0xe1);
            finish_program(target, false, (uint8_t)(value >> 4));
            ++byte_cases;
        }
        free(saved.bytes);
    }
    wave_history_free(target);
    wave_history_free(source);
}

/* Literal base lengths retain the historical wire formats. MBC7 adds eight
 * bytes from version 5 onward; that version requires a model or mapper section.
 * The accessory descriptor is absent before version 12 and moves when newer
 * hardware tail bytes are removed. */
static Blob legacy_blob(const Blob *current, unsigned version) {
    static const size_t lengths[] = {300518, 300568, 300572, 300572, 300576, 300577,
                                     300588, 300594, 300598, 300600, 300614, 300620,
                                     305054, 305088, 305089, 305089, 305090};
    assert(version >= 2 && version <= 18);
    const bool mbc7 = current->size == CURRENT_BYTES + MBC7_EXTENSION_BYTES;
    assert(current->size == CURRENT_BYTES || mbc7);
    assert(version != 5 || mbc7);
    Blob old = {NULL, lengths[version - 2] + (mbc7 && version >= 5 ? MBC7_EXTENSION_BYTES : 0)};
    old.bytes = malloc(old.size);
    assert(old.bytes);
    const size_t hardware = old.size - (version >= 12 ? ACCESSORY_BYTES : 0);
    memcpy(old.bytes, current->bytes, hardware);
    if (version >= 12)
        memcpy(old.bytes + hardware, current->bytes + current->size - ACCESSORY_BYTES,
               ACCESSORY_BYTES);
    if (version == 15 || version == 16)
        old.bytes[hardware - (version == 16 ? 1u : 0u) - 11u] = 0;
    put_u32(old.bytes + 8, version);
    resign(&old);
    return old;
}

static void test_legacy_pending_fetch(void) {
    CupidGb *source = pending_fetch_source();
    Blob current = capture(source);
    for (unsigned version = 14; version <= 19; ++version) {
        Blob old = version == 19 ? draft_blob(&current, false) : legacy_blob(&current, version);
        for (unsigned api = 0; api < 4; ++api) {
            CupidGb *target = different_receiver(true, false, api & 1u);
            assert(restore(target, &old, api) == CUPID_GB_STATE_OK);
            if (api < 2) {
                assert(target->apu.ch3_sample_byte == 0x66 && target->apu.ch3_sample == 6);
                assert(target->apu.pending_cycles == source->apu.pending_cycles);
            } else {
                /* The absent byte is projected before pending hardware executes.
                 * A real fetch then replaces it; projecting after execution would
                 * incorrectly turn the new byte E1 into the repeated nibble 11. */
                assert(target->apu.ch3_sample_byte == 0xe1 && target->apu.ch3_sample == 1);
                assert(!target->apu.pending_cycles);
            }
            finish_program(target, false, 14);
            wave_history_free(target);
            ++pending_cases;
        }
        free(old.bytes);
    }
    free(current.bytes);
    wave_history_free(source);
}

static void test_legacy_projection(void) {
    CupidGb *source = wave_history_hold_cartridge(true, false, MBC7_CARTRIDGE_TYPE);
    assert(source->header.mbc_type == CUPID_GB_MBC7);
    assert(!source->serial_fast_clock && source->io_registers[2] == 0x7e);
    for (unsigned sample = 0; sample < 16; ++sample) {
        /* The wire contract retains the decoded nibble even if it differs from
         * current RAM or reconstructed output. Missing fetch history is unknown. */
        source->apu.ch3_sample = (uint8_t)sample;
        Blob current = capture(source);
        for (unsigned version = 2; version <= 19; ++version) {
            Blob old = version == 19 ? draft_blob(&current, false) : legacy_blob(&current, version);
            for (unsigned api = 0; api < 4; ++api) {
                CupidGb *targets[2] = {
                    different_receiver_cartridge(true, false, 0, MBC7_CARTRIDGE_TYPE),
                    different_receiver_cartridge(true, false, 1, MBC7_CARTRIDGE_TYPE)};
                for (unsigned poison = 0; poison < 2; ++poison) {
                    CupidGb *target = targets[poison];
                    const CupidGbStateResult result = restore(target, &old, api);
                    if (result != CUPID_GB_STATE_OK)
                        fprintf(stderr,
                                "Legacy wave restore: version=%u api=%u sample=%u "
                                "receiver=%u bytes=%zu result=%s\n",
                                version, api, sample, poison, old.size,
                                cupid_gb_state_result_name(result));
                    assert(result == CUPID_GB_STATE_OK);
                    assert(target->apu.ch3_sample_byte == sample * 0x11u);
                    assert(target->apu.ch3_sample == sample && target->io_registers[0x34] == 0xe1);
                    assert(target->serial_fast_clock == (version < 18));
                    assert(target->io_registers[2] == 0x7e);
                    finish_program(target, false, (uint8_t)sample);
                    wave_history_free(target);
                }
                ++legacy_cases;
            }
            free(old.bytes);
        }
        free(current.bytes);
    }
    wave_history_free(source);
}

static void reject_unchanged(CupidGb *target, const Blob *bad, CupidGbStateResult expected) {
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    for (unsigned api = 0; api < 4; ++api) {
        memcpy(before, target, sizeof(*before));
        assert(restore(target, bad, api) == expected);
        assert(!memcmp(before, target, sizeof(*before)));
        ++rejection_cases;
    }
    free(before);
}

static Blob copy_blob(const Blob *source, size_t extra) {
    Blob copy = {calloc(source->size + extra, 1), source->size};
    assert(copy.bytes);
    memcpy(copy.bytes, source->bytes, source->size);
    return copy;
}

static void test_transactional_rejection(void) {
    CupidGb *source = wave_history_hold(true, false);
    CupidGb *target = different_receiver(true, false, 1);
    cupid_gb_tick_apu_divider(target, 4);
    Blob good = capture(source);
    const size_t latch = good.size - ACCESSORY_BYTES - SRAM_LENGTH_BYTES - 1;
    Blob bad = copy_blob(&good, 0);
    bad.bytes[latch] ^= 0xff;
    reject_unchanged(target, &bad, CUPID_GB_STATE_INVALID);
    free(bad.bytes);

    bad = copy_blob(&good, 0);
    memmove(bad.bytes + latch, bad.bytes + latch + 1, good.size - latch - 1);
    --bad.size;
    resign(&bad);
    reject_unchanged(target, &bad, CUPID_GB_STATE_INVALID);
    free(bad.bytes);

    bad = copy_blob(&good, 1);
    memmove(bad.bytes + latch + 1, bad.bytes + latch, good.size - latch);
    ++bad.size;
    resign(&bad);
    reject_unchanged(target, &bad, CUPID_GB_STATE_INVALID);
    free(bad.bytes);

    bad = copy_blob(&good, 0);
    put_u32(bad.bytes + 8, 18);
    resign(&bad);
    reject_unchanged(target, &bad, CUPID_GB_STATE_INVALID);
    put_u32(bad.bytes + 8, CUPID_GB_STATE_VERSION + 1u);
    resign(&bad);
    reject_unchanged(target, &bad, CUPID_GB_STATE_UNSUPPORTED_VERSION);
    free(bad.bytes);

    const size_t truncated[] = {HEADER_BYTES - 1, latch, latch + 1, good.size - 1};
    for (unsigned index = 0; index < sizeof(truncated) / sizeof(*truncated); ++index) {
        bad = copy_blob(&good, 0);
        bad.size = truncated[index];
        reject_unchanged(target, &bad, CUPID_GB_STATE_INVALID);
        free(bad.bytes);
    }

    source->apu.ch3_sample = 8;
    Blob alternate = capture(source);
    size_t sample_offset = 0;
    unsigned differences = 0;
    for (size_t offset = HEADER_BYTES; offset < good.size; ++offset)
        if (good.bytes[offset] != alternate.bytes[offset]) {
            sample_offset = offset;
            ++differences;
        }
    assert(differences == 1 && sample_offset < 300518);
    bad = copy_blob(&good, 0);
    bad.bytes[sample_offset] = 0x10;
    resign(&bad);
    reject_unchanged(target, &bad, CUPID_GB_STATE_INVALID);
    free(bad.bytes);
    free(alternate.bytes);
    free(good.bytes);
    wave_history_free(source);
    wave_history_free(target);
}

int main(void) {
    test_native_continuation(20);
    test_native_continuation(CUPID_GB_STATE_VERSION);
    test_draft_continuation();
    test_pending_fetch_before_live_output();
    test_full_byte_roundtrip();
    test_legacy_pending_fetch();
    test_legacy_projection();
    test_transactional_rejection();
    printf("Wave history state checks passed: %u continuations, %u v19 continuations, "
           "%u pending-fetch cases, %u byte roundtrips, %u legacy cases and %u rejected loads.\n",
           continuation_cases, draft_cases, pending_cases, byte_cases, legacy_cases,
           rejection_cases);
    return 0;
}

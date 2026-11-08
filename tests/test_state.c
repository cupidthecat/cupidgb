#include "cupid/common/sha256.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void digest_hex(const void *bytes, size_t length, char hex[65]) {
    uint8_t digest[32];
    cupid_sha256(bytes, length, digest);
    for (unsigned i = 0; i < sizeof(digest); ++i) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
}

static void test_sha256(void) {
    const char *messages[] = {"", "abc",
                              "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *digests[] = {"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    char actual[65];
    for (unsigned i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        digest_hex(messages[i], strlen(messages[i]), actual);
        assert(strcmp(actual, digests[i]) == 0);
    }
    char *million = malloc(1000000);
    assert(million);
    memset(million, 'a', 1000000);
    digest_hex(million, 1000000, actual);
    assert(strcmp(actual, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    CupidSha256 stream;
    uint8_t streamed[32], whole[32];
    cupid_sha256_init(&stream);
    for (size_t offset = 0; offset < 1000000; offset += 67) {
        size_t count = 1000000 - offset;
        cupid_sha256_update(&stream, million + offset, count < 67 ? count : 67);
    }
    cupid_sha256_final(&stream, streamed);
    cupid_sha256(million, 1000000, whole);
    assert(memcmp(streamed, whole, sizeof(whole)) == 0);
    free(million);
}

static void prepare(CupidGb *gb, CupidGbModel model, bool boot) {
    uint8_t rom[32768] = {0};
    const uint8_t program[] = {0x21, 0x00, 0xc0, 0x34, 0x7e, 0xe0, 0x42, 0x18, 0xfa};
    rom[0x100] = 0xc3;
    rom[0x101] = 0x50;
    rom[0x102] = 1;
    memcpy(rom + 0x150, program, sizeof(program));
    memcpy(rom + 0x134, "STATE TEST", 10);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x146] = 3;
    rom[0x147] = 3;
    rom[0x149] = 3;
    for (unsigned i = 0x134; i <= 0x14c; ++i) {
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    }
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    if (boot) {
        memset(gb->boot_rom, 0, sizeof(gb->boot_rom));
        gb->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100;
        gb->boot_rom[0] = 0x18;
        gb->boot_rom[1] = 0xfe; /* Generated boot program remains in the overlay. */
        cupid_gb_enter_boot_rom(gb);
    }
    for (unsigned i = 0; i < sizeof(gb->cartridge_ram); ++i) {
        gb->cartridge_ram[i] = (uint8_t)(i * 17 + (i >> 8));
    }
    for (unsigned i = 0; i < sizeof(gb->video_ram); ++i) {
        gb->video_ram[i] = (uint8_t)(i * 31 + (i >> 5));
    }
    for (unsigned i = 0; i < sizeof(gb->work_ram); ++i) {
        gb->work_ram[i] = (uint8_t)(i * 7);
    }
    for (unsigned i = 0; i < sizeof(gb->sgb.border_tiles); ++i) {
        gb->sgb.border_tiles[i] = (uint8_t)(i ^ (i >> 4));
    }
    gb->sgb.command_write_index = 5;
    gb->sgb.command[0] = 0x21;
    gb->sgb.command[1] = 0x56;
    gb->sgb.ready_for_write = true;
    gb->cpu.ime = false;
    gb->interrupt_enable = 0;
    gb->joypad = 0xe7;
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff17, 0xf1);
    cupid_gb_write_u8(gb, 0xff16, 0x80);
    cupid_gb_write_u8(gb, 0xff18, 0xc0);
    cupid_gb_write_u8(gb, 0xff19, 0x87);
    cupid_gb_write_u8(gb, 0xff24, 0x77);
    cupid_gb_write_u8(gb, 0xff25, 0x22);
}

static uint8_t *encode(const CupidGb *gb, size_t *length) {
    *length = cupid_gb_state_size(gb);
    assert(*length > 128);
    uint8_t *bytes = malloc(*length);
    size_t written = 0;
    assert(bytes);
    assert(cupid_gb_save_state(gb, bytes, *length, &written) == CUPID_GB_STATE_OK);
    assert(written == *length);
    return bytes;
}

static void assert_encoded_equal(const CupidGb *left, const CupidGb *right) {
    size_t a_length, b_length;
    uint8_t *a = encode(left, &a_length);
    uint8_t *b = encode(right, &b_length);
    assert(a_length == b_length && memcmp(a, b, a_length) == 0);
    free(a);
    free(b);
}

static void assert_hardware_equal(const CupidGb *a, const CupidGb *b) {
    assert(a->cpu.a == b->cpu.a && a->cpu.f == b->cpu.f);
    assert(a->cpu.b == b->cpu.b && a->cpu.c == b->cpu.c);
    assert(a->cpu.d == b->cpu.d && a->cpu.e == b->cpu.e);
    assert(a->cpu.h == b->cpu.h && a->cpu.l == b->cpu.l);
    assert(a->cpu.pc == b->cpu.pc && a->cpu.sp == b->cpu.sp);
    assert(a->cpu.ime == b->cpu.ime && a->cpu.ime_delay == b->cpu.ime_delay);
    assert(a->cpu.halted == b->cpu.halted && a->cpu.stopped == b->cpu.stopped);
    assert(a->cpu.halt_bug == b->cpu.halt_bug);
    assert(a->tima_overflow_delay == b->tima_overflow_delay);
    assert(a->tima_reload_just_happened == b->tima_reload_just_happened);
    assert(a->dma_index == b->dma_index && a->dma_active == b->dma_active);
    assert(a->dma_start_delay == b->dma_start_delay);
    assert(a->dma_restart_pending == b->dma_restart_pending);
    assert(a->dma_restart_delay == b->dma_restart_delay);
    assert(a->serial_bits_remaining == b->serial_bits_remaining);
    assert(a->serial_master_clock == b->serial_master_clock);
    assert(a->elapsed_cycles == b->elapsed_cycles && a->ppu_counter == b->ppu_counter);
    assert(a->double_speed == b->double_speed && a->speed_phase == b->speed_phase);
    assert(a->joypad == b->joypad && a->boot_rom_enabled == b->boot_rom_enabled);
    assert(a->sgb.command_write_index == b->sgb.command_write_index);
    assert(a->sgb.ready_for_write == b->sgb.ready_for_write);
#define SAME_ARRAY(field) assert(memcmp(a->field, b->field, sizeof(a->field)) == 0)
    SAME_ARRAY(video_ram);
    SAME_ARRAY(cartridge_ram);
    SAME_ARRAY(work_ram);
    SAME_ARRAY(object_attribute_memory);
    SAME_ARRAY(io_registers);
    SAME_ARRAY(high_ram);
    SAME_ARRAY(frame_buffer);
    SAME_ARRAY(frame_buffer_color);
    SAME_ARRAY(cgb_bg_palette_ram);
    SAME_ARRAY(cgb_obj_palette_ram);
    SAME_ARRAY(sgb.command);
    SAME_ARRAY(sgb.attribute_map);
    SAME_ARRAY(sgb.attribute_files);
    SAME_ARRAY(sgb.screen_palettes);
    SAME_ARRAY(sgb.ram_palettes);
    SAME_ARRAY(sgb.border_tiles);
    SAME_ARRAY(sgb.border_map);
    SAME_ARRAY(sgb.border_palettes);
    SAME_ARRAY(sgb.freeze_buffer);
    SAME_ARRAY(apu.buf);
#undef SAME_ARRAY
}

static void prepare_phase(CupidGb *gb, unsigned phase) {
    for (unsigned i = 0; i < 83; ++i) {
        assert(cupid_gb_step(gb));
    }
    gb->cpu.a = 0x29;
    gb->cpu.f = 0xb0;
    gb->cpu.b = 0x31;
    gb->cpu.c = 0x47;
    gb->cpu.d = 0x59;
    gb->cpu.e = 0x6b;
    cupid_gb_tick(gb, 321);
    if (phase == 0) {
        /* TIMA has overflowed but TMA reload and IRQ are still pending. */
        cupid_gb_write_u8(gb, 0xff07, 0);
        cupid_gb_write_u8(gb, 0xff04, 0);
        cupid_gb_write_u8(gb, 0xff05, 0xff);
        cupid_gb_write_u8(gb, 0xff06, 0x73);
        cupid_gb_write_u8(gb, 0xff07, 5);
        cupid_gb_tick(gb, 4);
        assert(gb->tima_overflow_delay == 1);
    } else if (phase == 1) {
        cupid_gb_write_u8(gb, 0xff46, 0xc0);
        cupid_gb_tick(gb, 1);
        assert(gb->dma_start_delay == 1);
    } else if (phase == 2) {
        cupid_gb_write_u8(gb, 0xff46, 0xc0);
        cupid_gb_tick(gb, 23);
        cupid_gb_write_u8(gb, 0xff46, 0xd0);
        assert(gb->dma_active && gb->dma_restart_pending);
    } else {
        cupid_gb_write_u8(gb, 0xff01, 0x69);
        cupid_gb_write_u8(gb, 0xff02, gb->cgb_mode ? 0x83 : 0x81);
        cupid_gb_tick(gb, gb->cgb_mode ? 13 : 400);
        assert(gb->serial_bits_remaining > 0 && gb->serial_bits_remaining < 8);
    }
}

static void test_resume(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    assert(a && b);
    for (unsigned model = 0; model <= CUPID_GB_MODEL_SGB2; ++model) {
        for (unsigned options = 0; options < (model == CUPID_GB_MODEL_CGB ? 8u : 4u); ++options) {
            for (unsigned phase = 0; phase < 4; ++phase) {
                bool boot = (options & 1) != 0;
                prepare(a, (CupidGbModel)model, boot);
                prepare(b, (CupidGbModel)model, boot);
                a->double_speed = (options & 4) != 0;
                a->speed_phase = a->double_speed;
                a->io_registers[0x4d] = a->double_speed ? 0x80 : 0;
                if (options & 2) {
                    cupid_gb_write_u8(a, 0xff40, 0);
                }
                prepare_phase(a, phase);
                size_t length;
                uint8_t *state = encode(a, &length);
                /* Every version 2 model and power phase has the same portable wire shape. */
                assert(length == 300518);
                uint8_t *rom_pointer = b->rom;
                strcpy(b->save_path, "host-session-save.sav");
                assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
                assert(b->rom == rom_pointer && strcmp(b->save_path, "host-session-save.sav") == 0);
                assert_hardware_equal(a, b);
                assert_encoded_equal(a, b);
                for (unsigned step = 0; step < 5000; ++step) {
                    assert(cupid_gb_step(a) && cupid_gb_step(b));
                    assert(a->cpu.pc == b->cpu.pc && a->cpu.sp == b->cpu.sp);
                    assert(a->cpu.a == b->cpu.a && a->cpu.f == b->cpu.f);
                    assert(a->elapsed_cycles == b->elapsed_cycles);
                    if (step % 137 == 0) {
                        int16_t left[CUPID_GB_APU_BUF_FRAMES * 2];
                        int16_t right[CUPID_GB_APU_BUF_FRAMES * 2];
                        uint32_t left_frames = cupid_gb_apu_drain(a, left, CUPID_GB_APU_BUF_FRAMES);
                        uint32_t right_frames =
                            cupid_gb_apu_drain(b, right, CUPID_GB_APU_BUF_FRAMES);
                        assert(left_frames == right_frames);
                        assert(memcmp(left, right, left_frames * 2 * sizeof(left[0])) == 0);
                    }
                }
                assert(memcmp(a->frame_buffer, b->frame_buffer, sizeof(a->frame_buffer)) == 0);
                assert(memcmp(a->frame_buffer_color, b->frame_buffer_color,
                              sizeof(a->frame_buffer_color)) == 0);
                assert_encoded_equal(a, b);
                assert_hardware_equal(a, b);
                free(state);
            }
        }
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
}

static void rejected_unchanged(CupidGb *gb, const uint8_t *bytes, size_t length,
                               CupidGbStateResult expected) {
    size_t before_length, after_length;
    uint8_t *before = encode(gb, &before_length);
    assert(cupid_gb_load_state(gb, bytes, length) == expected);
    uint8_t *after = encode(gb, &after_length);
    assert(before_length == after_length && memcmp(before, after, before_length) == 0);
    free(before);
    free(after);
}

static void update_integrity(uint8_t *bytes, size_t length) {
    CupidSha256 hash;
    cupid_sha256_init(&hash);
    cupid_sha256_update(&hash, bytes, 96);
    cupid_sha256_update(&hash, bytes + 128, length - 128);
    cupid_sha256_final(&hash, bytes + 96);
}

static void test_rejection(void) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    prepare(gb, CUPID_GB_MODEL_CGB, true);
    prepare_phase(gb, 2);
    size_t length;
    uint8_t *valid = encode(gb, &length);
    uint8_t *damaged = malloc(length + 1);
    CupidGb *snapshot = malloc(sizeof(*snapshot));
    assert(damaged && snapshot);
    memcpy(snapshot, gb, sizeof(*snapshot));
    /* Compare a stable live-object snapshot solely to check transactionality.
     * No struct layout, padding, pointers, or host paths enter the file fixture. */
    for (size_t cut = 0; cut < length; ++cut) {
        assert(cupid_gb_load_state(gb, valid, cut) != CUPID_GB_STATE_OK);
        assert(memcmp(snapshot, gb, sizeof(*gb)) == 0);
    }
    size_t after_length;
    uint8_t *after = encode(gb, &after_length);
    assert(after_length == length && memcmp(valid, after, length) == 0);
    free(after);
    for (size_t index = 0; index < length; index += index < 128 ? 1 : 4093) {
        memcpy(damaged, valid, length);
        damaged[index] ^= 0x80;
        assert(cupid_gb_load_state(gb, damaged, length) != CUPID_GB_STATE_OK);
        assert(memcmp(snapshot, gb, sizeof(*gb)) == 0);
    }
    memcpy(damaged, valid, length);
    damaged[length - 1] ^= 1;
    rejected_unchanged(gb, damaged, length, CUPID_GB_STATE_INVALID);
    const size_t invalid_payload_offsets[] = {
        129,    141,    180784, 300352, 300388, 300389, 300391, 300392, 300393, 300394, 300402,
        300410, 300418, 300419, 300420, 300425, 300426, 300427, 300429, 300430, 300460, 300462,
        300463, 300464, 300469, 300470, 300478, 300486, 300510, 300516, 300517};
    const uint8_t invalid_payload_values[] = {1,  2,  4,    1, 0x80, 2, 2,  154,  161, 4,  8,
                                              2,  8,  9,    7, 32,   8, 13, 0x40, 40,  11, 4,
                                              12, 10, 0x40, 4, 8,    2, 2,  2,    2};
    /* Stable prefix offsets cover CPU flags, an IME bool, and a monochrome pixel.
     * Version 2 appends audio integration and fetch queues at byte 300349. These
     * rehashed payloads must fail domain checks rather than checksum checks. */
    for (unsigned i = 0; i < sizeof(invalid_payload_offsets) / sizeof(invalid_payload_offsets[0]);
         ++i) {
        memcpy(damaged, valid, length);
        damaged[invalid_payload_offsets[i]] = invalid_payload_values[i];
        update_integrity(damaged, length);
        rejected_unchanged(gb, damaged, length, CUPID_GB_STATE_INVALID);
    }
    const uint16_t invalid_map_addresses[] = {0, 0x17ff, 0x2000, 0x3000, 0x3fff};
    for (unsigned i = 0; i < sizeof(invalid_map_addresses) / sizeof(invalid_map_addresses[0]);
         ++i) {
        memcpy(damaged, valid, length);
        damaged[300420] = 1; /* Pending tile/attribute map read. */
        damaged[300428] = (uint8_t)invalid_map_addresses[i];
        damaged[300429] = (uint8_t)(invalid_map_addresses[i] >> 8);
        update_integrity(damaged, length);
        rejected_unchanged(gb, damaged, length, CUPID_GB_STATE_INVALID);
    }
    memcpy(damaged, valid, length);
    damaged[length] = 0;
    rejected_unchanged(gb, damaged, length + 1, CUPID_GB_STATE_INVALID);
    memcpy(damaged, valid, length);
    damaged[8] = 3;
    update_integrity(damaged, length);
    rejected_unchanged(gb, damaged, length, CUPID_GB_STATE_UNSUPPORTED_VERSION);
    memcpy(damaged, valid, length);
    damaged[28] |= 2; /* Reserved header flags remain invalid with a correct digest. */
    update_integrity(damaged, length);
    rejected_unchanged(gb, damaged, length, CUPID_GB_STATE_INVALID);
    rejected_unchanged(gb, NULL, length, CUPID_GB_STATE_INVALID);
    assert(cupid_gb_load_state(NULL, valid, length) != CUPID_GB_STATE_OK);

    gb->rom[0x200] ^= 1;
    rejected_unchanged(gb, valid, length, CUPID_GB_STATE_WRONG_ROM);
    gb->rom[0x200] ^= 1;
    CupidGb *wrong_target = calloc(1, sizeof(*wrong_target));
    assert(wrong_target);
    cupid_gb_set_model(wrong_target, CUPID_GB_MODEL_MGB);
    assert(cupid_gb_load_rom(wrong_target, gb->rom, gb->rom_size));
    rejected_unchanged(wrong_target, valid, length, CUPID_GB_STATE_WRONG_MODEL);
    cupid_gb_cleanup(wrong_target);
    prepare(wrong_target, CUPID_GB_MODEL_CGB, false);
    rejected_unchanged(wrong_target, valid, length, CUPID_GB_STATE_WRONG_BOOT_ROM);
    cupid_gb_cleanup(wrong_target);
    free(wrong_target);
    gb->boot_rom[9] ^= 1;
    rejected_unchanged(gb, valid, length, CUPID_GB_STATE_WRONG_BOOT_ROM);
    gb->boot_rom[9] ^= 1;
    gb->boot_rom_enabled = false;
    assert(cupid_gb_load_state(gb, valid, length) == CUPID_GB_STATE_OK);
    assert(gb->boot_rom_enabled);

    size_t written = 123;
    memset(damaged, 0x5a, length);
    assert(cupid_gb_save_state(gb, damaged, length - 1, &written) ==
           CUPID_GB_STATE_BUFFER_TOO_SMALL);
    for (size_t i = 0; i < length; ++i) {
        assert(damaged[i] == 0x5a);
    }
    CupidGb empty = {0};
    assert(cupid_gb_load_state(&empty, valid, length) == CUPID_GB_STATE_NO_ROM);
    assert(cupid_gb_state_size(&empty) == 0);
    free(snapshot);
    free(damaged);
    free(valid);
    cupid_gb_cleanup(gb);
    free(gb);
}

static void print_fixture(const char *path) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    prepare(gb, CUPID_GB_MODEL_CGB, true);
    gb->double_speed = true;
    cupid_gb_write_u8(gb, 0xff40, 0);
    prepare_phase(gb, 2);
    size_t length;
    uint8_t *bytes = encode(gb, &length);
    char sha256[65];
    digest_hex(bytes, length, sha256);
    printf("CUPIDGB_STATE_FIXTURE size=%zu sha256=%s\n", length, sha256);
    fflush(stdout);
    /* Native format v2 compatibility fixture: changing its schema or generated
     * machine state requires an intentional update to this portable golden. */
    assert(length == 300518);
    assert(strcmp(sha256, "6106c038f80d9ef4ca4417f31305dc05bb714d9252e6caff3eec0205ea4ec0eb") == 0);
    if (path) {
        FILE *file = fopen(path, "wb");
        assert(file && fwrite(bytes, 1, length, file) == length && fclose(file) == 0);
    }
    free(bytes);
    cupid_gb_cleanup(gb);
    free(gb);
}

static void reject_stale_context(CupidGb *gb, const uint8_t *state, size_t length,
                                 const CupidGbStateContext *context, CupidGbStateResult expected) {
    size_t before_length, after_length;
    uint8_t *before = encode(gb, &before_length);
    assert(cupid_gb_load_state_with_context(gb, state, length, context) == expected);
    uint8_t *after = encode(gb, &after_length);
    assert(before_length == after_length && memcmp(before, after, before_length) == 0);
    size_t written = 123;
    assert(cupid_gb_save_state_with_context(gb, after, after_length, &written, context) ==
           expected);
    assert(written == 0 && memcmp(before, after, before_length) == 0);
    free(before);
    free(after);
}

static void test_trusted_context(void) {
    CupidGb *gb = calloc(1, sizeof(*gb)), *other = calloc(1, sizeof(*other));
    assert(gb && other);
    prepare(gb, CUPID_GB_MODEL_DMG_ABC, false);
    prepare(other, CUPID_GB_MODEL_DMG_ABC, false);
    prepare_phase(gb, 0);
    CupidGbStateContext context = {0};
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    size_t length, written = 0;
    uint8_t *normal = encode(gb, &length), *cached = malloc(length);
    assert(cached);
    assert(cupid_gb_save_state_with_context(gb, cached, length, &written, &context) ==
           CUPID_GB_STATE_OK);
    assert(written == length && memcmp(normal, cached, length) == 0);
    for (unsigned i = 0; i < 500; ++i) {
        assert(cupid_gb_step(gb));
    }
    assert(cupid_gb_load_state_with_context(gb, cached, length, &context) == CUPID_GB_STATE_OK);
    size_t restored_length;
    uint8_t *restored = encode(gb, &restored_length);
    assert(restored_length == length && memcmp(restored, normal, length) == 0);
    free(restored);

    reject_stale_context(other, normal, length, &context, CUPID_GB_STATE_WRONG_ROM);
    reject_stale_context(gb, normal, length, NULL, CUPID_GB_STATE_WRONG_ROM);
    CupidGbStateContext empty = {0};
    reject_stale_context(gb, normal, length, &empty, CUPID_GB_STATE_WRONG_ROM);
    uint8_t *original = gb->rom, *replacement = malloc(gb->rom_size);
    assert(replacement);
    memcpy(replacement, original, gb->rom_size);
    gb->rom = replacement;
    reject_stale_context(gb, normal, length, &context, CUPID_GB_STATE_WRONG_ROM);
    gb->rom = original;
    free(replacement);
    --gb->rom_size;
    reject_stale_context(gb, normal, length, &context, CUPID_GB_STATE_WRONG_ROM);
    ++gb->rom_size;
    gb->model = CUPID_GB_MODEL_MGB;
    reject_stale_context(gb, normal, length, &context, CUPID_GB_STATE_WRONG_MODEL);
    gb->model = CUPID_GB_MODEL_DMG_ABC;

    /* In-place patches require discarding and rebuilding the trusted context.
     * Disk-state loading independently hashes the current ROM and rejects the old identity. */
    gb->rom[0x200] ^= 1;
    rejected_unchanged(gb, normal, length, CUPID_GB_STATE_WRONG_ROM);
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    assert(cupid_gb_load_state_with_context(gb, normal, length, &context) ==
           CUPID_GB_STATE_WRONG_ROM);
    assert(cupid_gb_save_state_with_context(gb, cached, length, &written, &context) ==
           CUPID_GB_STATE_OK);
    assert(written == length && memcmp(normal, cached, length) != 0);
    gb->rom[0x200] ^= 1;
    assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
    free(cached);
    free(normal);
    cupid_gb_cleanup(gb);
    cupid_gb_cleanup(other);
    free(gb);
    free(other);
}

static void prepare_mapper(CupidGb *gb, uint8_t cartridge_type) {
    prepare(gb, CUPID_GB_MODEL_DMG_ABC, false);
    const size_t length = cartridge_type == 0 ? 32768 : 65536;
    uint8_t *rom = calloc(1, length);
    assert(rom);
    memcpy(rom, gb->rom, 32768);
    for (size_t bank = 1; bank < length / 16384; ++bank) {
        memset(rom + bank * 16384, (int)(0x40 + bank), 16384);
    }
    rom[0x147] = cartridge_type;
    rom[0x148] = cartridge_type == 0 ? 0 : 1;
    rom[0x149] = cartridge_type == 0 || cartridge_type == 6 || cartridge_type == 0x22 ? 0 : 3;
    rom[0x14d] = 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i) {
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    }
    assert(cupid_gb_load_rom(gb, rom, length));
    free(rom);
}

static void test_mapper_snapshots(void) {
    const uint8_t cartridge_types[] = {0, 3, 6, 0x10, 0x1b, 0x20, 0x22, 0x0d, 0xff, 0xfe};
    const CupidGbMbcType controllers[] = {
        CUPID_GB_MBC_NONE, CUPID_GB_MBC1, CUPID_GB_MBC2,  CUPID_GB_MBC3, CUPID_GB_MBC5,
        CUPID_GB_MBC6,     CUPID_GB_MBC7, CUPID_GB_MMM01, CUPID_GB_HUC1, CUPID_GB_HUC3};
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    assert(a && b);
    for (unsigned i = 0; i < sizeof(cartridge_types); ++i) {
        prepare_mapper(a, cartridge_types[i]);
        prepare_mapper(b, cartridge_types[i]);
        assert(a->header.mbc_type == controllers[i]);
        cupid_gb_write_u8(a, 0x0000, 0x0a);
        cupid_gb_write_u8(a, controllers[i] == CUPID_GB_MBC2 ? 0x2100 : 0x2000, 3);
        if (controllers[i] == CUPID_GB_MBC5) {
            cupid_gb_write_u8(a, 0x3000, 1);
        }
        cupid_gb_write_u8(a, 0x4000, 2);
        cupid_gb_write_u8(a, 0xa045, 0x0b);
        if (controllers[i] == CUPID_GB_MBC1 || controllers[i] == CUPID_GB_HUC1) {
            cupid_gb_write_u8(a, 0x6000, 1);
        }
        if (controllers[i] == CUPID_GB_MBC3) {
            cupid_gb_write_u8(a, 0x4000, 0x08);
            cupid_gb_write_u8(a, 0xa000, 37);
            cupid_gb_write_u8(a, 0x4000, 0x0c);
            cupid_gb_write_u8(a, 0xa000, 0x40);
            cupid_gb_write_u8(a, 0x6000, 0);
            cupid_gb_write_u8(a, 0x6000, 1);
        } else if (controllers[i] == CUPID_GB_MBC7) {
            cupid_gb_write_u8(a, 0xa000, 0x55);
            assert(a->mbc7_latch);
        } else if (controllers[i] == CUPID_GB_HUC1) {
            cupid_gb_write_u8(a, 0x0000, 0x0e);
            assert(a->huc1_ir_mode);
        } else if (controllers[i] == CUPID_GB_HUC3) {
            cupid_gb_write_u8(a, 0x0000, 0x0b);
            assert(a->huc3_mode == 0x0b);
        }
        size_t length;
        uint8_t *state = encode(a, &length);
        assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
        assert(a->current_rom_bank == b->current_rom_bank);
        assert(a->current_ram_bank == b->current_ram_bank && a->ram_enabled == b->ram_enabled);
        assert(a->mbc1_bank_low5 == b->mbc1_bank_low5);
        assert(a->mbc1_bank_high2 == b->mbc1_bank_high2);
        assert(a->mbc1_ram_banking_mode == b->mbc1_ram_banking_mode);
        assert(a->mbc5_rom_bank == b->mbc5_rom_bank && a->mbc7_latch == b->mbc7_latch);
        assert(a->huc1_ir_mode == b->huc1_ir_mode && a->huc3_mode == b->huc3_mode);
        assert(a->rtc_register == b->rtc_register && a->rtc.base_time == b->rtc.base_time);
        assert(cupid_gb_read_u8(a, 0x4001) == cupid_gb_read_u8(b, 0x4001));
        assert(cupid_gb_read_u8(a, 0xa045) == cupid_gb_read_u8(b, 0xa045));
        assert_encoded_equal(a, b);
        for (unsigned step = 0; step < 3000; ++step) {
            assert(cupid_gb_step(a) && cupid_gb_step(b));
        }
        assert_encoded_equal(a, b);
        assert_hardware_equal(a, b);
        free(state);
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
}

static void test_restored_rtc_extreme_timestamps(void) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    const int64_t now = (int64_t)time(NULL);
    const int64_t timestamps[] = {INT64_MIN, INT64_MIN + 1, INT64_MAX,
                                  now - INT64_C(86400) * 514 - 3671, 0};
    const uint64_t seconds_per_cycle = UINT64_C(512) * 86400;
    for (unsigned i = 0; i < sizeof(timestamps) / sizeof(timestamps[0]); ++i) {
        for (unsigned halted = 0; halted < 2; ++halted) {
            prepare_mapper(gb, 0x10);
            gb->rtc.s = 58;
            gb->rtc.m = 59;
            gb->rtc.h = 23;
            gb->rtc.dl = 255;
            gb->rtc.dh = (uint8_t)(1 | (halted ? 0x40 : 0));
            gb->rtc.base_time = timestamps[i];
            size_t length;
            uint8_t *state = encode(gb, &length);
            gb->rtc.base_time = 1;
            gb->rtc.s = 0;
            assert(cupid_gb_load_state(gb, state, length) == CUPID_GB_STATE_OK);
            assert(gb->rtc.base_time == timestamps[i] && gb->rtc.s == 58);
            free(state);

            cupid_gb_write_u8(gb, 0x6000, 0);
            cupid_gb_write_u8(gb, 0x6000, 1);
            /* Derive elapsed time from the exact host timestamp used by the latch,
             * rather than assuming execution cannot cross a wall-clock second. */
            const int64_t latched_at = gb->rtc.base_time;
            assert(latched_at >= now);
            uint64_t expected = seconds_per_cycle - 2;
            uint8_t expected_carry = 0;
            if (!halted && timestamps[i] != 0 && latched_at > timestamps[i]) {
                const uint64_t elapsed = (uint64_t)latched_at - (uint64_t)timestamps[i];
                if (elapsed >= 2) {
                    expected_carry = 0x80;
                }
                expected = (expected + elapsed % seconds_per_cycle) % seconds_per_cycle;
            }
            assert(gb->rtc.s == expected % 60);
            assert(gb->rtc.m == expected / 60 % 60);
            assert(gb->rtc.h == expected / 3600 % 24);
            const uint64_t expected_days = expected / 86400;
            assert(gb->rtc.dl == (expected_days & 255));
            assert(gb->rtc.dh ==
                   (uint8_t)((expected_days >> 8) | expected_carry | (halted ? 0x40 : 0)));
            assert(gb->rtc.ls == gb->rtc.s && gb->rtc.lm == gb->rtc.m);
            assert(gb->rtc.lh == gb->rtc.h && gb->rtc.ldl == gb->rtc.dl);
            assert(gb->rtc.ldh == gb->rtc.dh);
        }
    }
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_pixel_pipeline_resume(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    assert(a && b);
    unsigned queued = 0, fetching_object = 0, window = 0;
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned phase = 0; phase < 32; ++phase) {
            prepare(a, models[model], false);
            prepare(b, models[model], false);
            cupid_gb_write_u8(a, 0xff40, 0);
            cupid_gb_write_u8(a, 0xff43, 5);
            cupid_gb_write_u8(a, 0xff4a, 0);
            cupid_gb_write_u8(a, 0xff4b, 39);
            memset(a->object_attribute_memory, 0, sizeof(a->object_attribute_memory));
            for (unsigned object = 0; object < 10; ++object) {
                a->object_attribute_memory[object * 4] = 16;
                a->object_attribute_memory[object * 4 + 1] = (uint8_t)(16 + object * 13);
                a->object_attribute_memory[object * 4 + 2] = (uint8_t)(object + 1);
            }
            a->cgb_object_priority_x = (phase & 1) != 0;
            cupid_gb_write_u8(a, 0xff40, 0xf3);
            cupid_gb_tick(a, (uint16_t)(114 * 3 + 18 + phase * 2));
            queued += a->pixels.count != 0;
            fetching_object += a->pixels.object_delay != 0;
            window += a->pixels.window_active;
            size_t length;
            uint8_t *state = encode(a, &length);
            assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
            assert(a->cgb_object_priority_x == b->cgb_object_priority_x);
            assert_encoded_equal(a, b);
            for (unsigned cycle = 0; cycle < 240; ++cycle) {
                if (cycle == 7) {
                    /* Writes after restore must see the same pending fetches and emitted pixels. */
                    cupid_gb_write_u8(a, 0xff43, 11);
                    cupid_gb_write_u8(b, 0xff43, 11);
                    cupid_gb_write_u8(a, 0xff47, 0x1b);
                    cupid_gb_write_u8(b, 0xff47, 0x1b);
                }
                cupid_gb_tick(a, 1);
                cupid_gb_tick(b, 1);
            }
            assert_encoded_equal(a, b);
            assert_hardware_equal(a, b);
            free(state);
        }
    }
    assert(queued && fetching_object && window);
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
}

static void test_first_halt_poll_resume(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    assert(a && b);
    prepare(a, CUPID_GB_MODEL_DMG_ABC, false);
    prepare(b, CUPID_GB_MODEL_DMG_ABC, false);
    a->rom[0x100] = b->rom[0x100] = 0x76;
    a->cpu.pc = 0x100;
    a->cpu.ime = true;
    a->interrupt_enable = a->interrupt_flags = 0;
    assert(cupid_gb_step(a));
    assert(a->cpu.halted && a->cpu.just_halted);
    /* The first HALT poll precedes this OAM edge. Later monochrome polls occur
     * midway through a cycle. Restoring the queue without the CPU latch wakes early. */
    a->pixels.line = 4;
    a->pixels.dot = 455;
    a->pixels.x = 160;
    a->pixels.active = false;
    a->pixels.lyc_irq = false;
    a->ppu_counter = 113;
    a->ppu_lcd_startup = false;
    a->io_registers[0x41] = 0xa0;
    a->io_registers[0x44] = 4;
    a->io_registers[0x45] = 20;
    a->stat_irq_line = false;
    a->interrupt_enable = 2;
    a->interrupt_flags = 0;
    size_t length;
    uint8_t *state = encode(a, &length);
    assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
    assert(b->cpu.just_halted);
    assert(cupid_gb_step(a) && cupid_gb_step(b));
    assert(a->cpu.halted && b->cpu.halted && !b->cpu.just_halted);
    assert((a->interrupt_flags & 2) && (b->interrupt_flags & 2));
    assert_encoded_equal(a, b);
    assert(cupid_gb_step(a) && cupid_gb_step(b));
    assert(!a->cpu.halted && a->cpu.pc == 0x48);
    assert_encoded_equal(a, b);
    free(state);
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
}

int main(int argc, char **argv) {
    assert(argc == 1 || (argc == 3 && strcmp(argv[1], "--write-fixture") == 0));
    test_sha256();
    test_resume();
    test_rejection();
    test_trusted_context();
    test_restored_rtc_extreme_timestamps();
    test_mapper_snapshots();
    test_pixel_pipeline_resume();
    test_first_halt_poll_resume();
    print_fixture(argc == 3 ? argv[2] : NULL);
    puts("Portable state checks passed.");
    return 0;
}

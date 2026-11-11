#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"
#include "cupid/gbc/cgb.h"

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
    assert(a->sgb.transfer.frame_dots == b->sgb.transfer.frame_dots);
    assert(a->sgb.transfer.destination == b->sgb.transfer.destination);
    assert(a->sgb.transfer.frames_remaining == b->sgb.transfer.frames_remaining);
    assert(a->sgb.transfer.border_transition == b->sgb.transfer.border_transition);
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
    SAME_ARRAY(sgb.transfer.tiles);
    SAME_ARRAY(sgb.transfer.map);
    SAME_ARRAY(sgb.transfer.palettes);
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
        assert(gb->dma_start_delay == 0 && gb->dma_active && gb->dma_index == 0);
    } else if (phase == 2) {
        cupid_gb_write_u8(gb, 0xff46, 0xc0);
        cupid_gb_tick(gb, 23);
        cupid_gb_write_u8(gb, 0xff46, 0xd0);
        assert(!gb->dma_active && gb->dma_start_delay == 1 && gb->dma_restart_pending);
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
                assert(length == cupid_gb_state_size(b) && length > 300518);
                assert(state[8] == 7);
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

static void put_u32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8));
    }
}

static void state_rumble_callback(const CupidGb *gb, bool enabled, uint64_t cycles, void *data) {
    unsigned *count = data;
    assert(enabled == cupid_gb_rumble_enabled(gb) && cycles == gb->elapsed_cycles);
    ++*count;
}

static void test_controller_state_versions(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    uint8_t rom[32768] = {0};
    rom[0x146] = 3;
    rom[0x147] = 0x1e;
    rom[0x149] = 3;
    cupid_gb_set_model(source, CUPID_GB_MODEL_SGB);
    cupid_gb_set_model(target, CUPID_GB_MODEL_SGB);
    assert(cupid_gb_load_rom(source, rom, sizeof(rom)));
    assert(cupid_gb_load_rom(target, rom, sizeof(rom)));
    source->sgb.player_count = 4;
    source->sgb.current_player = 2;
    source->io_registers[0] = 0x10;
    for (unsigned player = 0; player < 4; ++player) {
        assert(cupid_gb_set_buttons_for_player(source, player, (uint8_t)~(0x11u << player)));
    }
    cupid_gb_write_u8(source, 0x4000, 0x0d);
    size_t length;
    uint8_t *bytes = encode(source, &length);
    assert(length == 310696 && bytes[300568] == source->joypad_extra[0] &&
           bytes[300569] == source->joypad_extra[1] && bytes[300570] == source->joypad_extra[2] &&
           bytes[300571] == 1);
    unsigned callbacks = 0;
    cupid_gb_set_rumble_callback(target, state_rumble_callback, &callbacks);
    assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
    assert_encoded_equal(source, target);
    assert(cupid_gb_input_read(source) == cupid_gb_input_read(target));
    assert(cupid_gb_rumble_enabled(target) && callbacks == 0 &&
           target->rumble_callback == state_rumble_callback &&
           target->rumble_user_data == &callbacks);
    /* A stored motor bool has one canonical byte encoding. Failure must retain
     * both controller state and host registration without generating events. */
    bytes[300571] = 2;
    update_integrity(bytes, length);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    *before = *target;
    assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_INVALID);
    assert(memcmp(target, before, sizeof(*target)) == 0 && callbacks == 0);
    bytes[300571] = 1;
    for (unsigned version = 2; version <= 3; ++version) {
        const size_t old_length = version == 2 ? 300518 : 300568;
        put_u32(bytes + 8, version);
        put_u32(bytes + 12, (uint32_t)old_length);
        update_integrity(bytes, old_length);
        assert(cupid_gb_load_state(target, bytes, old_length) == CUPID_GB_STATE_OK);
        assert(target->joypad == source->joypad && target->sgb.current_player == 2 &&
               target->sgb.player_count == 4 && target->current_ram_bank == 5);
        for (unsigned pad = 0; pad < 3; ++pad) {
            assert(target->joypad_extra[pad] == 0xff);
        }
        assert(!cupid_gb_rumble_enabled(target) && callbacks == 0);
        /* The bank field is an explicit little-endian u64 in the frozen prefix.
         * A legacy writer could never encode bit 3 on this cartridge. */
        const size_t bank_offset = 128 + 17 + 0x4000 + CUPID_GB_MAX_RAM_SIZE + 0x8000 + 0xa0 +
                                   0x80 + 0x7f + 160 * 144 * 3 + 2 + 8;
        assert(bytes[bank_offset] == 5);
        bytes[bank_offset] = 13;
        update_integrity(bytes, old_length);
        *before = *target;
        assert(cupid_gb_load_state(target, bytes, old_length) == CUPID_GB_STATE_INVALID);
        assert(memcmp(target, before, sizeof(*target)) == 0 && callbacks == 0);
        bytes[bank_offset] = 5;
    }
    free(bytes);
    source->sgb.player_count = 3;
    bytes = encode(source, &length);
    assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
    assert_encoded_equal(source, target);
    assert(target->sgb.player_count == 3 && target->sgb.current_player == 2 && callbacks == 0);
    *before = *target;
    for (unsigned version = 2; version <= 3; ++version) {
        const size_t old_length = version == 2 ? 300518 : 300568;
        put_u32(bytes + 8, version);
        put_u32(bytes + 12, (uint32_t)old_length);
        update_integrity(bytes, old_length);
        assert(cupid_gb_load_state(target, bytes, old_length) == CUPID_GB_STATE_INVALID);
        assert(memcmp(target, before, sizeof(*target)) == 0 && callbacks == 0);
    }
    free(before);
    free(bytes);
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void test_v2_import(void) {
    const size_t legacy_length = 300518;
    CupidGb *source = calloc(1, sizeof(*source));
    CupidGb *target = calloc(1, sizeof(*target));
    CupidGb *expected = malloc(sizeof(*expected));
    assert(source && target && expected);
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned boot = 0; boot < 2; ++boot) {
            prepare(source, models[model], boot != 0);
            prepare(target, models[model], boot != 0);
            cupid_gb_write_u8(source, 0xff21, 0xf2);
            cupid_gb_write_u8(source, 0xff22, 0x09);
            cupid_gb_write_u8(source, 0xff23, 0x80);
            prepare_phase(source, 2);
            size_t length;
            uint8_t *bytes = encode(source, &length);
            assert(length > legacy_length);
            /* The v2 payload is frozen. Remove only the new extension and rebuild
             * its original header and digest, without running the current encoder again. */
            put_u32(bytes + 8, 2);
            put_u32(bytes + 12, (uint32_t)legacy_length);
            update_integrity(bytes, legacy_length);
            *expected = *source;
            expected->hdma_pending = false;
            expected->hdma_hblank_delay = 0;
            expected->hdma_allow_on_wake = false;
            expected->speed_switch_halt_cycles = 0;
            expected->speed_switch_toggle_cycles = 0;
            expected->speed_switch_freeze_cycles = 0;
            expected->stop_divider_hold_cycles = 0;
            expected->speed_switch_alignment = 0;
            cupid_gb_apu_import_legacy(expected);
            if (expected->model == CUPID_GB_MODEL_SGB) {
                expected->sgb.transfer.frame_dots = 0;
                memcpy(expected->sgb.transfer.tiles, expected->sgb.border_tiles,
                       sizeof(expected->sgb.transfer.tiles));
                memcpy(expected->sgb.freeze_buffer, expected->frame_buffer,
                       sizeof(expected->sgb.freeze_buffer));
            }
            for (unsigned trusted = 0; trusted < 2; ++trusted) {
                /* A live target's unrelated oscillator and DMA history must never
                 * supply fields absent from the old file. */
                target->hdma_pending = true;
                target->hdma_blocks_remaining = 2;
                target->hdma_hblank_delay = 3;
                target->hdma_allow_on_wake = true;
                target->apu.clock_phase = 3;
                target->apu.envelope_delay = 2;
                target->apu.pulse_timing[0].start_delay = 12;
                target->apu.pulse_timing[1].output_blocked = true;
                target->apu.sweep_timing.delta = 2047;
                target->apu.noise_timing.divider_counter = 0x3fff;
                target->apu.noise_timing.started_dac_off = true;
                target->speed_switch_halt_cycles = 131080;
                target->speed_switch_toggle_cycles = 6;
                target->speed_switch_freeze_cycles = 5;
                target->stop_divider_hold_cycles = 4;
                target->speed_switch_alignment = 7;
                CupidGbStateContext context = {0};
                assert(cupid_gb_state_context_init(&context, target) == CUPID_GB_STATE_OK);
                CupidGbStateResult result =
                    trusted
                        ? cupid_gb_load_state_with_context(target, bytes, legacy_length, &context)
                        : cupid_gb_load_state(target, bytes, legacy_length);
                assert(result == CUPID_GB_STATE_OK);
                assert(!target->hdma_pending && !target->hdma_hblank_delay &&
                       !target->hdma_allow_on_wake);
                assert(!target->speed_switch_halt_cycles && !target->speed_switch_toggle_cycles &&
                       !target->speed_switch_freeze_cycles && !target->stop_divider_hold_cycles &&
                       !target->speed_switch_alignment);
                assert(target->apu.clock_phase == (source->elapsed_cycles & 3u));
                assert(target->apu.envelope_delay == 0 && target->apu.sweep_timing.delta == 0);
                assert(target->apu.noise_timing.foreground ==
                       (source->apu.ch4_on && source->apu.ch4_dac));
                assert(!target->apu.noise_timing.started_dac_off &&
                       target->apu.noise_timing.divider_counter == 0);
                assert_hardware_equal(expected, target);
                assert_encoded_equal(expected, target);
                /* Queued PCM, integration and capacitor history belong to v2,
                 * and importing missing latches must preserve all three. */
                assert(target->apu.sample_acc == source->apu.sample_acc);
                assert(target->apu.mix_left == source->apu.mix_left);
                assert(target->apu.highpass_left == source->apu.highpass_left);
            }
            free(bytes);
        }
    }
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
    free(expected);
}

static void test_cgb_dma_audio_resume(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned phase = 0; phase < 5; ++phase) {
            prepare(source, CUPID_GB_MODEL_CGB, false);
            prepare(target, CUPID_GB_MODEL_CGB, false);
            source->double_speed = fast != 0;
            source->io_registers[0x4d] = fast ? 0x80 : 0;
            cupid_gb_write_u8(source, 0xff40, 0);
            cupid_gb_write_u8(source, 0xff51, 0xc0);
            cupid_gb_write_u8(source, 0xff52, 0);
            cupid_gb_write_u8(source, 0xff53, 8);
            cupid_gb_write_u8(source, 0xff54, 0);
            if (phase) {
                cupid_gb_write_u8(source, 0xff40, 0x91);
                for (unsigned dot = 0; !(source->io_registers[0x41] & 3u); ++dot) {
                    assert(dot < 500);
                    cupid_gb_tick_ppu_dots(source, 1);
                }
                cupid_gb_write_u8(source, 0xff55, 0x81);
                if (phase >= 3) {
                    cupid_cgb_hdma_sleep(source);
                    source->cpu.halted = phase == 3;
                    source->cpu.stopped = phase == 4;
                    assert(source->hdma_allow_on_wake);
                }
                for (unsigned dot = 0; !source->hdma_hblank_delay; ++dot) {
                    assert(dot < 1000);
                    cupid_gb_tick_ppu_dots(source, 1);
                }
                if (phase >= 2) {
                    cupid_gb_tick_ppu_dots(source, 3);
                    assert(source->hdma_pending == (phase == 2));
                }
            } else {
                cupid_gb_write_u8(source, 0xff55, 1);
                assert(source->hdma_pending);
            }
            /* Trigger immediately before capture: restart delay and held output
             * must survive together with the independently scheduled DMA edge. */
            cupid_gb_write_u8(source, 0xff19, 0x87);
            size_t length;
            uint8_t *bytes = encode(source, &length);
            assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
            free(bytes);
            assert_encoded_equal(source, target);
            if (phase >= 3) {
                source->cpu.halted = target->cpu.halted = false;
                source->cpu.stopped = target->cpu.stopped = false;
                cupid_cgb_hdma_wake(source);
                cupid_cgb_hdma_wake(target);
                assert(source->hdma_pending && target->hdma_pending);
            }
            for (unsigned step = 0; step < 160; ++step) {
                assert(cupid_gb_step(source) && cupid_gb_step(target));
                if (step < 4) {
                    assert_encoded_equal(source, target);
                } else if (step % 19 == 0) {
                    assert_hardware_equal(source, target);
                }
            }
            assert_encoded_equal(source, target);
            assert_hardware_equal(source, target);
        }
    }
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void test_audio_pipeline_resume(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    unsigned delayed_pulse = 0, calculating_sweep = 0, advancing_noise = 0;
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned phase = 0; phase < 64; ++phase) {
            prepare(source, CUPID_GB_MODEL_CGB, false);
            prepare(target, CUPID_GB_MODEL_CGB, false);
            source->double_speed = fast != 0;
            source->io_registers[0x4d] = fast ? 0x80 : 0;
            cupid_gb_write_u8(source, 0xff40, 0);
            cupid_gb_write_u8(source, 0xff26, 0);
            cupid_gb_write_u8(source, 0xff26, 0x80);
            cupid_gb_write_u8(source, 0xff10, 0x11);
            cupid_gb_write_u8(source, 0xff11, 0x80);
            cupid_gb_write_u8(source, 0xff12, 0xf1);
            cupid_gb_write_u8(source, 0xff13, 0xf0);
            cupid_gb_write_u8(source, 0xff14, 0x83);
            cupid_gb_write_u8(source, 0xff21, 0xf2);
            cupid_gb_write_u8(source, 0xff22, 0x09);
            cupid_gb_write_u8(source, 0xff23, 0x80);
            /* Component clocks place snapshots on both sides of base-clock
             * edges, including phases not aligned to a CPU machine cycle. */
            cupid_gb_tick_apu(source, (uint16_t)phase);
            delayed_pulse += source->apu.pulse_timing[0].start_delay != 0;
            calculating_sweep += source->apu.sweep_timing.startup_ticks != 0 ||
                                 source->apu.sweep_timing.shift_ticks != 0;
            advancing_noise += source->apu.noise_timing.divider_counter != 0;
            size_t length;
            uint8_t *bytes = encode(source, &length);
            assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
            free(bytes);
            assert_encoded_equal(source, target);
            for (unsigned step = 0; step < 160; ++step) {
                if (step == 3) {
                    /* Frequency and width writes act on restored in-flight
                     * calculations and held output, rather than fresh oscillators. */
                    cupid_gb_write_u8(source, 0xff10, 0x19);
                    cupid_gb_write_u8(target, 0xff10, 0x19);
                    cupid_gb_write_u8(source, 0xff22, 0x01);
                    cupid_gb_write_u8(target, 0xff22, 0x01);
                }
                assert(cupid_gb_step(source) && cupid_gb_step(target));
                if (step < 4) {
                    assert_encoded_equal(source, target);
                } else if (step % 19 == 0) {
                    assert_hardware_equal(source, target);
                }
            }
            assert_encoded_equal(source, target);
            assert_hardware_equal(source, target);
        }
    }
    assert(delayed_pulse && calculating_sweep && advancing_noise);
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void test_noise_counter_wire_domain(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    prepare(source, CUPID_GB_MODEL_CGB, false);
    prepare(target, CUPID_GB_MODEL_CGB, false);
    const uint16_t counters[] = {0x3fff, 0x4000, 0x4001, 0x7fff, 0xbfff, 0xffff};
    for (unsigned i = 0; i < sizeof(counters) / sizeof(counters[0]); ++i) {
        /* Ordinary clocks mask to fourteen bits, but DAC-off carries can reach
         * every uint16_t value. The codec must not truncate or reject that state. */
        source->apu.noise_timing.divider_counter = counters[i];
        size_t length;
        uint8_t *bytes = encode(source, &length);
        assert(bytes[300548] == (uint8_t)counters[i]);
        assert(bytes[300549] == (uint8_t)(counters[i] >> 8));
        assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
        assert(target->apu.noise_timing.divider_counter == counters[i]);
        assert_encoded_equal(source, target);
        free(bytes);
    }
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void prepare_switch(CupidGb *gb, bool fast, bool lcd, unsigned ime) {
    prepare(gb, CUPID_GB_MODEL_CGB, false);
    gb->rom[0x100] = 0x10;
    gb->rom[0x101] = 0;
    gb->rom[0x102] = 4; /* INC B makes resumed opcode execution observable. */
    gb->cpu.pc = 0x100;
    gb->cpu.b = 0;
    gb->cpu.ime = ime == 1;
    gb->cpu.ime_delay = ime == 2;
    gb->joypad = 0xff;
    gb->double_speed = fast;
    gb->io_registers[0x4d] = fast ? 0x80 : 0;
    cupid_gb_write_u8(gb, 0xff00, 0x30);
    if (!lcd) {
        cupid_gb_write_u8(gb, 0xff40, 0);
    }
    cupid_gb_write_u8(gb, 0xff4d, 1);
}

static void restore_peer(const CupidGb *source, CupidGb *target) {
    size_t length;
    uint8_t *bytes = encode(source, &length);
    assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
    free(bytes);
    assert_encoded_equal(source, target);
}

static void test_speed_switch_resume(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned lcd = 0; lcd < 2; ++lcd) {
            for (unsigned ime = 0; ime < 3; ++ime) {
                prepare_switch(source, fast != 0, lcd != 0, ime);
                prepare_switch(target, fast != 0, lcd != 0, ime);
                assert(cupid_gb_step(source));
                assert(source->cpu.halted && source->speed_switch_halt_cycles == 131080);
                unsigned polls = 0;
                while (source->cpu.halted) {
                    assert(polls < 32780);
                    if (polls <= 2 || source->speed_switch_halt_cycles <= 8) {
                        /* Capture before/after the freeze, a pending toggle,
                         * and expiry. Each loaded peer resumes the complete wait. */
                        restore_peer(source, target);
                    }
                    assert(cupid_gb_step(source) && cupid_gb_step(target));
                    ++polls;
                    if (polls <= 3 || !source->cpu.halted || polls % 4096 == 0) {
                        assert_encoded_equal(source, target);
                    }
                }
                assert(source->double_speed == !fast && source->cpu.b == 1);
                assert(source->cpu.pc == 0x103 && source->speed_switch_halt_cycles == 0);
                assert_hardware_equal(source, target);
            }
            prepare_switch(source, fast != 0, lcd != 0, 0);
            prepare_switch(target, fast != 0, lcd != 0, 0);
            assert(cupid_gb_step(source));
            restore_peer(source, target);
            source->interrupt_enable = target->interrupt_enable = 4;
            source->interrupt_flags = target->interrupt_flags = 4;
            assert(cupid_gb_step(source) && cupid_gb_step(target));
            assert(!source->cpu.halted && !source->speed_switch_halt_cycles);
            assert(source->cpu.pc == 0x102 && source->cpu.b == 0);
            assert_encoded_equal(source, target);
            restore_peer(source, target);
            assert(cupid_gb_step(source) && cupid_gb_step(target));
            assert(source->double_speed == !fast && source->cpu.b == 1);
            assert_encoded_equal(source, target);
        }
    }
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void test_word_array_wire_encoding(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *restored = calloc(1, sizeof(*restored));
    assert(source && restored);
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB2};
    const uint16_t boundary_values[] = {0, 1, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x1234};
    for (size_t model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        prepare(source, models[model], false);
        prepare(restored, models[model], false);
        size_t length;
        uint8_t *expected = encode(source, &length);

        struct WordArray {
            uint16_t *source;
            uint16_t *restored;
            size_t count;
            size_t wire_offset;
        };

#define ARRAY(field, offset)                                                                       \
    {                                                                                              \
        source->field, restored->field, sizeof(source->field) / sizeof(source->field[0]), offset   \
    }
        const struct WordArray arrays[] = {
            ARRAY(frame_buffer_color, 203824),  ARRAY(sgb.screen_palettes, 254666),
            ARRAY(sgb.ram_palettes, 254698),    ARRAY(sgb.border_map, 266986),
            ARRAY(sgb.border_palettes, 268778), ARRAY(pixels.sprite_priorities, 300494)};
#undef ARRAY
        /* Fixed v2 offsets check both ends of every word array and the neighboring
         * fields. Constructing expected bytes also detects symmetric codec errors. */
        for (size_t array = 0; array < sizeof(arrays) / sizeof(arrays[0]); ++array) {
            const struct WordArray *words = &arrays[array];
            assert(words->wire_offset + words->count * 2 <= length);
            for (size_t i = 0; i < words->count; ++i) {
                uint16_t value = i < sizeof(boundary_values) / sizeof(boundary_values[0])
                                     ? boundary_values[i]
                                     : (uint16_t)(i * 257u + (i >> 4) + array);
                if (i + 1 == words->count) {
                    value = 0xfe80;
                }
                words->source[i] = value;
                expected[words->wire_offset + i * 2] = (uint8_t)value;
                expected[words->wire_offset + i * 2 + 1] = (uint8_t)(value >> 8);
            }
        }
        update_integrity(expected, length);
        uint8_t *guarded = malloc(length + 2);
        assert(guarded);
        memset(guarded, 0x5a, length + 2);
        size_t written = 0;
        assert(cupid_gb_save_state(source, guarded + 1, length, &written) == CUPID_GB_STATE_OK);
        assert(written == length && guarded[0] == 0x5a && guarded[length + 1] == 0x5a);
        assert(memcmp(expected, guarded + 1, length) == 0);
        assert(cupid_gb_load_state(restored, guarded + 1, length) == CUPID_GB_STATE_OK);
        for (size_t array = 0; array < sizeof(arrays) / sizeof(arrays[0]); ++array) {
            assert(memcmp(arrays[array].source, arrays[array].restored,
                          arrays[array].count * sizeof(uint16_t)) == 0);
        }
        assert_encoded_equal(source, restored);
        free(guarded);
        free(expected);
        cupid_gb_cleanup(source);
        cupid_gb_cleanup(restored);
    }
    free(source);
    free(restored);
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
    memcpy(damaged, valid, length);
    damaged[length - 1] = 4;
    update_integrity(damaged, length);
    rejected_unchanged(gb, damaged, length, CUPID_GB_STATE_INVALID);
    memcpy(damaged, valid, length);
    damaged[300571] = 1;
    update_integrity(damaged, length);
    /* A cartridge without this motor cannot restore an asserted motor latch. */
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
    const uint8_t invalid_extension[][2] = {
        {0, 2},  {1, 4},  {1, 1},  {2, 2},   {3, 4},  {4, 3},   {5, 13}, {6, 5},  {7, 2},
        {8, 2},  {9, 2},  {10, 2}, {11, 2},  {12, 2}, {13, 13}, {14, 5}, {15, 2}, {16, 2},
        {17, 2}, {18, 2}, {19, 2}, {20, 2},  {21, 4}, {22, 8},  {23, 9}, {25, 8}, {27, 8},
        {28, 2}, {29, 2}, {32, 8}, {33, 35}, {34, 2}, {35, 2},  {36, 2}, {37, 2}, {38, 2},
        {39, 2}, {40, 2}, {41, 2}, {44, 3},  {46, 7}, {47, 6},  {48, 5}, {49, 8}};
    for (unsigned i = 0; i < sizeof(invalid_extension) / sizeof(invalid_extension[0]); ++i) {
        memcpy(damaged, valid, length);
        damaged[300518 + invalid_extension[i][0]] = invalid_extension[i][1];
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
    damaged[8] = 8;
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

static void test_sgb_transfer_resume(void) {
    const size_t v4_length = 300572;
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    prepare(source, CUPID_GB_MODEL_SGB2, false);
    prepare(target, CUPID_GB_MODEL_SGB2, false);
    cupid_gb_write_u8(source, 0xff40, 0);
    source->sgb.transfer.destination = CUPID_SGB_TRANSFER_PALETTES;
    source->sgb.transfer.frames_remaining = 1;
    source->sgb.transfer.frame_dots = CUPID_SGB_FRAME_DOTS - 1;
    source->sgb.transfer.border_transition = 33;
    source->sgb.transfer.tiles[4096] = 0xa5;
    source->sgb.transfer.map[0] = 0x0c80;
    source->sgb.transfer.palettes[63] = 0x1234;
    source->sgb.border_map[0] = 0x007f;
    source->sgb.border_palettes[63] = 0x7fff;
    source->frame_buffer[0] = 1;
    source->frame_buffer[1] = 2;
    source->sgb.freeze_buffer[0] = 3;

    size_t length;
    uint8_t *bytes = encode(source, &length);
    assert(length == 310696 && bytes[8] == 7);
    assert(bytes[v4_length + 7 + 4096] == 0xa5);
    assert(bytes[v4_length + 7 + 8192] == 0x80 && bytes[v4_length + 7 + 8193] == 0x0c);
    assert(bytes[length - 7] == 0x34 && bytes[length - 6] == 0x12);
    assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
    assert_hardware_equal(source, target);
    cupid_gb_tick_ppu_dots(source, 1);
    cupid_gb_tick_ppu_dots(target, 1);
    assert_encoded_equal(source, target);
    assert(target->sgb.ram_palettes[0] == 0x4080 && target->sgb.border_map[0] == 0x0c80 &&
           target->sgb.border_tiles[4096] == 0xa5 && target->sgb.border_palettes[63] == 0x1234);
    assert(target->sgb.transfer.border_transition == 32 && target->sgb.freeze_buffer[0] == 3);

    const uint8_t invalid[][2] = {{0, 0xff}, {4, 6}, {4, 0}, {5, 4}, {6, 105}};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        size_t offset = v4_length + invalid[i][0];
        uint8_t original = bytes[offset];
        bytes[offset] = invalid[i][1];
        update_integrity(bytes, length);
        rejected_unchanged(target, bytes, length, CUPID_GB_STATE_INVALID);
        bytes[offset] = original;
    }

    /* A legacy state had visible border data but no unfinished transfer. */
    put_u32(bytes + 8, 4);
    put_u32(bytes + 12, (uint32_t)v4_length);
    update_integrity(bytes, v4_length);
    assert(cupid_gb_load_state(target, bytes, v4_length) == CUPID_GB_STATE_OK);
    assert(!target->sgb.transfer.destination && !target->sgb.transfer.frames_remaining &&
           !target->sgb.transfer.frame_dots && !target->sgb.transfer.border_transition);
    assert(target->sgb.transfer.map[0] == 0x007f && target->sgb.transfer.tiles[4096] != 0xa5 &&
           target->sgb.transfer.palettes[63] == 0x7fff && target->sgb.freeze_buffer[0] == 1);

    free(bytes);
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void tick_to_vblank(CupidGb *gb) {
    gb->frame_ready = false;
    for (unsigned line = 0; line <= 154 && !gb->frame_ready; ++line)
        cupid_gb_tick_ppu_dots(gb, 456);
    assert(gb->frame_ready);
}

static void test_lcd_frame_phase_resume(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); ++i) {
        const bool cgb = models[i] == CUPID_GB_MODEL_CGB;
        const bool handheld = models[i] != CUPID_GB_MODEL_SGB;
        prepare(source, models[i], false);
        prepare(target, models[i], false);
        assert(!source->lcd_frame_odd && !source->lcd_first_frame);
        tick_to_vblank(source);
        assert(source->lcd_frame_odd == handheld);
        tick_to_vblank(source);
        assert(!source->lcd_frame_odd);
        const uint8_t lcdc = source->io_registers[CUPID_GB_IO_LCDC];
        cupid_gb_write_u8(source, 0xff40, lcdc & 0x7f);
        assert(source->lcd_frame_odd == handheld);
        cupid_gb_write_u8(source, 0xff40, lcdc & 0x7f);
        assert(source->lcd_frame_odd == handheld);
        cupid_gb_write_u8(source, 0xff40, lcdc);
        assert(source->lcd_first_frame == cgb);
        size_t length;
        uint8_t *state = encode(source, &length);
        const size_t phase_offset = length - 5;
        assert(state[phase_offset] == handheld && state[phase_offset + 1] == cgb);
        assert(cupid_gb_load_state(target, state, length) == CUPID_GB_STATE_OK);
        assert(target->lcd_frame_odd == handheld && target->lcd_first_frame == cgb);
        tick_to_vblank(source);
        tick_to_vblank(target);
        assert(source->lcd_frame_odd == cgb && !source->lcd_first_frame);
        assert(target->lcd_frame_odd == cgb && !target->lcd_first_frame);
        tick_to_vblank(source);
        tick_to_vblank(target);
        assert(source->lcd_frame_odd == (handheld && !cgb));
        assert_encoded_equal(source, target);
        for (size_t offset = phase_offset; offset < phase_offset + 2; ++offset) {
            const uint8_t value = state[offset];
            state[offset] = 2;
            update_integrity(state, length);
            rejected_unchanged(target, state, length, CUPID_GB_STATE_INVALID);
            state[offset] = value;
        }
        if (cgb) {
            const size_t old_length = phase_offset;
            put_u32(state + 8, 4);
            put_u32(state + 12, (uint32_t)old_length);
            update_integrity(state, old_length);
            assert(cupid_gb_load_state(target, state, old_length) == CUPID_GB_STATE_OK);
            assert(!target->lcd_frame_odd && target->lcd_first_frame);
        }
        free(state);
        cupid_gb_cleanup(source);
        cupid_gb_cleanup(target);
    }
    free(source);
    free(target);
}

static void test_v5_sgb_mapper_sections(void) {
    const size_t mapper_offset = 310691;
    const uint8_t types[] = {0x0b, 0x22, 0xfc, 0xbc};
    uint8_t rom[32768] = {0};
    rom[0x146] = 3;
    for (unsigned mapper = 0; mapper < sizeof(types); ++mapper) {
        CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
        assert(source && target);
        rom[0x147] = types[mapper];
        if (types[mapper] == 0xbc) {
            rom[0x149] = 0xc1;
            rom[0x14a] = 0x65;
            rom[0x152] = 1;
        }
        cupid_gb_set_model(source, CUPID_GB_MODEL_SGB2);
        cupid_gb_set_model(target, CUPID_GB_MODEL_SGB2);
        assert(cupid_gb_load_rom(source, rom, sizeof(rom)));
        assert(cupid_gb_load_rom(target, rom, sizeof(rom)));
        source->sgb.transfer.palettes[63] = 0x1234;
        uint8_t wire[64] = {0};
        size_t mapper_length;
        if (types[mapper] == 0x0b) {
            source->mmm01_rom_select = 0x37;
            source->mmm01_bank_select = 0x19;
            source->mmm01_mode = 0x45;
            source->mmm01_ram_mask = 2;
            const uint8_t expected[] = {0x37, 0x19, 0x45, 0x02};
            mapper_length = sizeof(expected);
            memcpy(wire, expected, mapper_length);
        } else if (types[mapper] == 0x22) {
            source->mbc7.command = 0x513;
            source->mbc7.output = 0x1234;
            source->mbc7.data_bits = 7;
            source->mbc7.pins = 0x82;
            source->mbc7.registers_enabled = true;
            source->mbc7.programming_enabled = true;
            const uint8_t expected[] = {0x13, 0x05, 0x34, 0x12, 7, 0x82, 1, 1};
            mapper_length = sizeof(expected);
            memcpy(wire, expected, mapper_length);
        } else if (types[mapper] == 0xfc) {
            source->camera.registers[0] = 5;
            source->camera.registers[53] = 0x95;
            source->camera.clock_phase = 7;
            source->camera.mapped = true;
            source->camera.busy_tcycles = 1024;
            source->camera.capture_number = UINT32_C(0x12345678);
            wire[0] = 5;
            wire[53] = 0x95;
            wire[54] = 7;
            wire[55] = 1;
            wire[57] = 4;
            wire[60] = 0x78;
            wire[61] = 0x56;
            wire[62] = 0x34;
            wire[63] = 0x12;
            mapper_length = sizeof(wire);
        } else {
            source->tpp1.rom_bank = 0x4321;
            source->tpp1.ram_bank = 3;
            source->tpp1.mode = 5;
            source->tpp1.status = 0x0b;
            const uint8_t live[] = {10, 20, 30, 40};
            const uint8_t latched[] = {11, 21, 31, 41};
            memcpy(source->tpp1.live, live, sizeof(live));
            memcpy(source->tpp1.latched, latched, sizeof(latched));
            source->tpp1.rtc_base_time = INT64_C(0x0123456789abcdef);
            const uint8_t expected[] = {0x21, 0x43, 3,    5,    0x0b, 10,   20,
                                        30,   40,   11,   21,   31,   41,   0xef,
                                        0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};
            mapper_length = sizeof(expected);
            memcpy(wire, expected, mapper_length);
        }
        size_t length;
        uint8_t *bytes = encode(source, &length);
        assert(length == mapper_offset + mapper_length + 5 && bytes[8] == 7 &&
               bytes[mapper_offset - 2] == 0x34 && bytes[mapper_offset - 1] == 0x12);
        assert(memcmp(bytes + mapper_offset, wire, mapper_length) == 0);
        assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
        assert_encoded_equal(source, target);
        const size_t v5_length = length - 5;
        put_u32(bytes + 8, 5);
        put_u32(bytes + 12, (uint32_t)v5_length);
        update_integrity(bytes, v5_length);
        assert(cupid_gb_load_state(target, bytes, v5_length) == CUPID_GB_STATE_OK);
        assert_encoded_equal(source, target);
        free(bytes);
        cupid_gb_cleanup(source);
        cupid_gb_cleanup(target);
        free(source);
        free(target);
    }
}

static void test_sgb_sample_phase_and_legacy_filter(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    prepare(source, CUPID_GB_MODEL_SGB, false);
    prepare(target, CUPID_GB_MODEL_SGB, false);
    const uint32_t clock_hz = cupid_gb_base_clock_hz(source);
    source->apu.sample_acc = clock_hz - 1;
    size_t length;
    uint8_t *state = encode(source, &length);
    assert(cupid_gb_load_state(target, state, length) == CUPID_GB_STATE_OK);
    assert(target->apu.sample_acc == clock_hz - 1);
    assert_encoded_equal(source, target);

    /* A correctly hashed phase at the model's clock boundary must fail validation. */
    size_t phase_offset = 0, matches = 0;
    for (size_t offset = 128; offset + 4 <= length; ++offset) {
        uint32_t value = (uint32_t)state[offset] | ((uint32_t)state[offset + 1] << 8) |
                         ((uint32_t)state[offset + 2] << 16) | ((uint32_t)state[offset + 3] << 24);
        if (value == clock_hz - 1) {
            phase_offset = offset;
            ++matches;
        }
    }
    assert(matches == 1);
    for (unsigned byte = 0; byte < 4; ++byte) {
        state[phase_offset + byte] = (uint8_t)(clock_hz >> (byte * 8));
    }
    update_integrity(state, length);
    rejected_unchanged(target, state, length, CUPID_GB_STATE_INVALID);
    free(state);

    CupidGb handheld = {0}, corrected = {0};
    corrected.model = CUPID_GB_MODEL_SGB;
    assert(cupid_gb_apu_set_sample_rate(&handheld, CUPID_GB_APU_SAMPLE_RATE));
    assert(cupid_gb_apu_set_sample_rate(&corrected, CUPID_GB_APU_SAMPLE_RATE));
    source->apu.sample_rate = 0; /* Legacy default-rate sentinel must survive migration. */
    source->apu.sample_acc = 4194303;
    source->apu.mix_left = 123456;
    source->apu.mix_right = -456789;
    source->apu.highpass_rate = handheld.apu.highpass_rate;
    source->apu.highpass_left = 123;
    source->apu.highpass_right = -456;
    source->apu.buf_write = 2;
    source->apu.buf[0] = 1234;
    source->apu.buf[1] = -2345;
    state = encode(source, &length);
    assert(cupid_gb_load_state(target, state, length) == CUPID_GB_STATE_OK);
    assert(target->apu.highpass_rate == corrected.apu.highpass_rate);
    assert(target->apu.highpass_rate != source->apu.highpass_rate);
    source->apu.highpass_rate = corrected.apu.highpass_rate;
    assert_encoded_equal(source, target);
    assert_hardware_equal(source, target);
    free(state);
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void print_fixture(const char *path) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    prepare(gb, CUPID_GB_MODEL_CGB, true);
    /* Preserve the original divider phase of this compatibility fixture. */
    gb->div_counter = 0;
    gb->double_speed = true;
    cupid_gb_write_u8(gb, 0xff40, 0);
    prepare_phase(gb, 2);
    /* Keep the historical in-flight OAM prefix of this compatibility fixture.
     * The current scheduler completes its last source read one M-cycle earlier. */
    gb->object_attribute_memory[21] = 0;
    gb->dma_source_base = 0xc000;
    gb->dma_index = 21;
    gb->dma_start_delay = 0;
    gb->dma_restart_delay = 2;
    gb->dma_active = true;
    size_t length;
    uint8_t *bytes = encode(gb, &length);
    assert(length == 300577 && bytes[8] == 7 && !bytes[length - 5] && !bytes[length - 4]);
    length -= 5;
    put_u32(bytes + 8, 4);
    put_u32(bytes + 12, (uint32_t)length);
    update_integrity(bytes, length);
    char sha256[65];
    digest_hex(bytes, length, sha256);
    printf("CUPIDGB_STATE_FIXTURE size=%zu sha256=%s\n", length, sha256);
    fflush(stdout);
    assert(length == 300572);
    assert(strcmp(sha256, "652af4f40504c86dd59eff09f0997f5b60b654816a0908d7802704f981b43be5") == 0);
    /* The older wire payloads remain byte-for-byte compatible when wrapped
     * in their original headers. Added fields cannot shift either prefix. */
    put_u32(bytes + 8, 3);
    put_u32(bytes + 12, 300568);
    update_integrity(bytes, 300568);
    char legacy_sha256[65];
    digest_hex(bytes, 300568, legacy_sha256);
    assert(strcmp(legacy_sha256,
                  "e4fdd3d7ebfdd04aec94ce7b85f0f2d7d83de1416242fa89f26ccf5dfaf71eb2") == 0);
    put_u32(bytes + 8, 4);
    put_u32(bytes + 12, (uint32_t)length);
    update_integrity(bytes, length);
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
            cupid_gb_write_u8(a, 0x4000, 0x40);
            cupid_gb_write_u8(a, 0xa010, 0xaa);
            assert(!a->mbc7_latch && cupid_gb_read_u8(a, 0xa020) == 0xd0);
            cupid_gb_write_u8(a, 0xa000, 0x55);
            assert(a->mbc7_latch && cupid_gb_read_u8(a, 0xa020) == 0);
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

static void test_lcd_interrupt_edge_resume(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    assert(a && b);
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_CGB,
                                   CUPID_GB_MODEL_CGB};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned before = 0; before < 2; ++before) {
            prepare(a, models[model], false);
            prepare(b, models[model], false);
            a->double_speed = model == 6;
            cupid_gb_write_u8(a, 0xff41, 0);
            cupid_gb_write_u8(a, 0xff45, 1);
            cupid_gb_write_u8(a, 0xff40, 0);
            cupid_gb_write_u8(a, 0xff45, 0);
            cupid_gb_write_u8(a, 0xff41, 0x40);
            a->interrupt_flags = 0;
            cupid_gb_write_u8(a, 0xff40, 0x91);
            cupid_gb_tick_ppu_dots(a, (uint16_t)before);
            size_t length;
            uint8_t *state = encode(a, &length);
            assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
            assert_encoded_equal(a, b);
            free(state);
            if (a->double_speed && before == 0) {
                cupid_gb_sync_lcd_startup(a);
                cupid_gb_sync_lcd_startup(b);
                assert(a->pixels.dot == 0 && a->elapsed_cycles == 0);
                assert(a->interrupt_flags & 2);
            }
            for (unsigned dot = 1; dot <= 4; ++dot) {
                cupid_gb_tick_ppu_dots(a, 1);
                cupid_gb_tick_ppu_dots(b, 1);
                const unsigned edge = models[model] == CUPID_GB_MODEL_CGB ? 1 : 2;
                assert((a->interrupt_flags & 2) == (before + dot >= edge ? 2 : 0));
                assert_encoded_equal(a, b);
            }
        }
    }
    for (unsigned double_speed = 0; double_speed < 2; ++double_speed) {
        prepare(a, CUPID_GB_MODEL_CGB, false);
        prepare(b, CUPID_GB_MODEL_CGB, false);
        a->double_speed = double_speed != 0;
        cupid_gb_write_u8(a, 0xff41, 0);
        cupid_gb_write_u8(a, 0xff40, 0);
        cupid_gb_write_u8(a, 0xff45, 0);
        cupid_gb_write_u8(a, 0xff40, 0x91);
        while (a->pixels.active || a->pixels.x != CUPID_GB_SCREEN_WIDTH) {
            assert(a->pixels.dot < 400);
            cupid_gb_tick_ppu_dots(a, 1);
        }
        cupid_gb_write_u8(a, 0xff41, 0x48);
        assert(a->stat_irq_delay == 1 && (a->interrupt_flags & 2));
        size_t length;
        uint8_t *state = encode(a, &length);
        assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
        free(state);
        a->interrupt_flags = b->interrupt_flags = 0;
        cupid_gb_tick_ppu_dots(a, 1);
        cupid_gb_tick_ppu_dots(b, 1);
        assert(a->stat_irq_delay == 0 && (a->io_registers[CUPID_GB_IO_STAT] & 3) == 0);
        assert(!(a->interrupt_flags & 2));
        assert_encoded_equal(a, b);
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(a);
    free(b);
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
    test_word_array_wire_encoding();
    test_v2_import();
    test_controller_state_versions();
    test_cgb_dma_audio_resume();
    test_audio_pipeline_resume();
    test_noise_counter_wire_domain();
    test_speed_switch_resume();
    test_rejection();
    test_trusted_context();
    test_restored_rtc_extreme_timestamps();
    test_mapper_snapshots();
    test_lcd_interrupt_edge_resume();
    test_pixel_pipeline_resume();
    test_first_halt_poll_resume();
    test_sgb_transfer_resume();
    test_lcd_frame_phase_resume();
    test_v5_sgb_mapper_sections();
    test_sgb_sample_phase_and_legacy_filter();
    print_fixture(argc == 3 ? argv[2] : NULL);
    puts("Portable state checks passed.");
    return 0;
}

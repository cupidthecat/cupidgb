/**
 * @file    test_printer_state.c
 * @brief   Printer snapshot continuation and transactional accessory-state validation.
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

#include "../src/gb/audio_state.h"
#include "../src/gb/display_clock.h"
#include "../src/gb/vram_bus.h"
#include "cupid/common/sha256.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/printer.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sampled JOYP, serial selection and the CH3 fetch byte precede the SRAM length. */
enum { IO_HISTORY_BYTES = 3, SRAM_LENGTH_BYTES = 4 };

enum {
    NATIVE_HEADER_BYTES = 128,
    DEVICE_DESCRIPTOR_BYTES = 8,
    INFRARED_HISTORY_BYTES = 6,
    RTC_HISTORY_BYTES = 6,
    MOTOR_HISTORY_OFFSET = 4,
    BARE_STATE_BYTES = SRAM_LENGTH_BYTES + IO_HISTORY_BYTES + 300600 + INFRARED_HISTORY_BYTES +
                       RTC_HISTORY_BYTES + CUPID_GB_AUDIO_STATE_SIZE +
                       CUPID_GB_DISPLAY_CLOCK_STATE_SIZE + CUPID_GB_PIXEL_TIMING_STATE_SIZE +
                       CUPID_GB_VRAM_BUS_STATE_SIZE + DEVICE_DESCRIPTOR_BYTES,
    PRINTER_WIRE_BYTES = 32687,
    WIRE_PHASE = 0,
    WIRE_HEADER = 1,
    WIRE_PAYLOAD = 5,
    WIRE_INDEX = 645,
    WIRE_PAYLOAD_SIZE = 647,
    WIRE_DECODED_SIZE = 649,
    WIRE_CHECKSUM = 651,
    WIRE_RECEIVED_CHECKSUM = 653,
    WIRE_RLE_REMAINING = 655,
    WIRE_RLE_REPEAT = 656,
    WIRE_STATUS = 657,
    WIRE_ROWS = 658,
    WIRE_PIXELS = 659,
    WIRE_INCOMING = 32659,
    WIRE_INCOMING_BITS = 32660,
    WIRE_REPLY = 32661,
    WIRE_REPLY_BIT = 32662,
    WIRE_OUTPUT = 32663,
    WIRE_CLOCK_SEEN = 32664,
    WIRE_LAST_CYCLE = 32665,
    WIRE_SUB_TICK = 32673,
    WIRE_IDLE_TICKS = 32675,
    WIRE_REMAINING_TICKS = 32683,
};

typedef struct Event {
    uint64_t cycle;
    uint8_t digest[32];
    unsigned kind;
    unsigned height;
    unsigned top;
    unsigned bottom;
    unsigned exposure;
} Event;

typedef struct Capture {
    const CupidGb *gb;
    Event events[16];
    uint8_t pixels[CUPID_GB_PRINTER_WIDTH * 200];
    unsigned count;
    unsigned images;
    unsigned completed;
} Capture;

typedef struct External {
    unsigned launches;
    unsigned samples;
    unsigned notifications;
} External;

typedef struct Rig {
    CupidGb *gb;
    CupidGbPrinter *printer;
    CupidGbStateContext context;
    Capture capture;
    External external;
    uint8_t control;
} Rig;

typedef struct Blob {
    uint8_t *bytes;
    size_t length;
} Blob;

typedef struct Stream {
    uint8_t bytes[2048];
    uint8_t replies[2048];
    size_t length;
} Stream;

typedef enum SerialMode { SERIAL_DEFAULT, SERIAL_DOUBLE_SPEED, SERIAL_COLOR_NORMAL } SerialMode;

static const uint8_t stripe_run[] = {0xff, 0x55, 0xff, 0x55, 0xff, 0x55, 0xff, 0x55, 0xfa, 0x55};
static const uint8_t print_parameters[] = {1, 0x21, 0xe4, 0xa0};
static unsigned continuations;
static unsigned rejected_states;

static uint64_t read_le(const uint8_t *bytes, unsigned width) {
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= (uint64_t)bytes[i] << (8 * i);
    return value;
}

static void write_le(uint8_t *bytes, unsigned width, uint64_t value) {
    for (unsigned i = 0; i < width; ++i)
        bytes[i] = (uint8_t)(value >> (8 * i));
}

static void resign(Blob *blob) {
    assert(blob->length >= NATIVE_HEADER_BYTES);
    write_le(blob->bytes + 12, 4, blob->length);
    CupidSha256 digest;
    cupid_sha256_init(&digest);
    cupid_sha256_update(&digest, blob->bytes, 96);
    cupid_sha256_update(&digest, blob->bytes + NATIVE_HEADER_BYTES,
                        blob->length - NATIVE_HEADER_BYTES);
    cupid_sha256_final(&digest, blob->bytes + 96);
}

static void on_image(const CupidGbPrinterImage *image, void *user_data) {
    Capture *capture = user_data;
    assert(capture->count < sizeof(capture->events) / sizeof(capture->events[0]));
    Event *event = &capture->events[capture->count++];
    event->cycle = capture->gb->elapsed_cycles;
    event->kind = 1;
    event->height = image->height;
    event->top = image->top_margin;
    event->bottom = image->bottom_margin;
    event->exposure = image->exposure;
    const size_t size = (size_t)image->height * CUPID_GB_PRINTER_WIDTH;
    cupid_sha256(image->pixels, size, event->digest);
    memcpy(capture->pixels, image->pixels, size);
    ++capture->images;
}

static void on_done(void *user_data) {
    Capture *capture = user_data;
    assert(capture->count < sizeof(capture->events) / sizeof(capture->events[0]));
    Event *event = &capture->events[capture->count++];
    event->cycle = capture->gb->elapsed_cycles;
    event->kind = 2;
    ++capture->completed;
}

static void external_launch(const CupidGb *gb, bool outgoing, void *user_data) {
    (void)gb;
    (void)outgoing;
    ++((External *)user_data)->launches;
}

static bool external_sample(const CupidGb *gb, void *user_data) {
    (void)gb;
    ++((External *)user_data)->samples;
    return true;
}

static void serial_observer(const CupidGb *gb, uint8_t byte, void *user_data) {
    (void)gb;
    (void)byte;
    ++((External *)user_data)->notifications;
}

static void clear_capture(Rig *rig) {
    memset(&rig->capture, 0, sizeof(rig->capture));
    rig->capture.gb = rig->gb;
}

static Rig *create_rig(CupidGbModel model, bool double_speed) {
    Rig *rig = calloc(1, sizeof(*rig));
    assert(rig);
    rig->gb = calloc(1, sizeof(*rig->gb));
    assert(rig->gb);
    uint8_t rom[32768] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    memcpy(rom + 0x134, "PRINTER STATE", 13);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x146] = 3;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    cupid_gb_set_model(rig->gb, model);
    assert(cupid_gb_load_rom(rig->gb, rom, sizeof(rom)));
    cupid_gb_write_u8(rig->gb, 0xff40, 0);
    cupid_gb_write_u8(rig->gb, 0xff26, 0);
    rig->gb->cpu.ime = false;
    rig->gb->interrupt_enable = 0;
    rig->gb->double_speed = double_speed;
    rig->gb->elapsed_cycles = 37;
    rig->control = model == CUPID_GB_MODEL_CGB ? 0x83 : 0x81;
    clear_capture(rig);
    rig->printer = cupid_gb_printer_create(on_image, on_done, &rig->capture);
    assert(rig->printer);
    cupid_gb_set_serial_link(rig->gb, cupid_gb_printer_link(rig->printer));
    rig->gb->serial_output_callback = serial_observer;
    rig->gb->serial_output_user_data = &rig->external;
    assert(cupid_gb_state_context_init(&rig->context, rig->gb) == CUPID_GB_STATE_OK);
    return rig;
}

static void destroy_rig(Rig *rig) {
    cupid_gb_set_serial_link(rig->gb, (CupidGbSerialLink){0});
    cupid_gb_printer_destroy(rig->printer);
    cupid_gb_cleanup(rig->gb);
    free(rig->gb);
    free(rig);
}

static Blob save_rig(const Rig *rig, bool trusted) {
    Blob blob = {NULL, cupid_gb_state_size(rig->gb)};
    assert(blob.length > NATIVE_HEADER_BYTES);
    blob.bytes = malloc(blob.length);
    assert(blob.bytes);
    size_t written = 0;
    CupidGbStateResult result =
        trusted ? cupid_gb_save_state_with_context(rig->gb, blob.bytes, blob.length, &written,
                                                   &rig->context)
                : cupid_gb_save_state(rig->gb, blob.bytes, blob.length, &written);
    assert(result == CUPID_GB_STATE_OK && written == blob.length);
    assert(read_le(blob.bytes + 8, 4) == CUPID_GB_STATE_VERSION);
    return blob;
}

static void free_blob(Blob blob) {
    free(blob.bytes);
}

static size_t printer_descriptor(const Blob *blob) {
    assert(blob->length >= NATIVE_HEADER_BYTES + DEVICE_DESCRIPTOR_BYTES + PRINTER_WIRE_BYTES);
    const size_t offset = blob->length - PRINTER_WIRE_BYTES - DEVICE_DESCRIPTOR_BYTES;
    assert(blob->bytes[offset] == 1 && blob->bytes[offset + 1] == 1);
    assert(read_le(blob->bytes + offset + 2, 2) == 0);
    assert(read_le(blob->bytes + offset + 4, 4) == PRINTER_WIRE_BYTES);
    return offset;
}

static const uint8_t *printer_bytes(const Blob *blob) {
    return blob->bytes + printer_descriptor(blob) + DEVICE_DESCRIPTOR_BYTES;
}

static void assert_blobs_equal(const Blob *a, const Blob *b) {
    assert(a->length == b->length);
    assert(memcmp(a->bytes, b->bytes, a->length) == 0);
}

static void assert_same_state(const Rig *a, const Rig *b) {
    Blob left = save_rig(a, false), right = save_rig(b, true);
    assert_blobs_equal(&left, &right);
    free_blob(left);
    free_blob(right);
}

static CupidGbStateResult load_rig(Rig *rig, const Blob *blob, bool trusted) {
    const CupidGbSerialLink link = rig->gb->serial_link;
    const unsigned events = rig->capture.count;
    const External external = rig->external;
    CupidGbStateResult result =
        trusted
            ? cupid_gb_load_state_with_context(rig->gb, blob->bytes, blob->length, &rig->context)
            : cupid_gb_load_state(rig->gb, blob->bytes, blob->length);
    assert(rig->capture.count == events);
    assert(memcmp(&external, &rig->external, sizeof(external)) == 0);
    assert(rig->gb->serial_link.launch == link.launch);
    assert(rig->gb->serial_link.sample == link.sample);
    assert(rig->gb->serial_link.user_data == link.user_data);
    assert(rig->gb->serial_link.device == link.device);
    assert(rig->gb->serial_output_callback == serial_observer);
    assert(rig->gb->serial_output_user_data == &rig->external);
    return result;
}

static void start_byte(Rig *rig, uint8_t byte) {
    assert(rig->gb->serial_bits_remaining == 0);
    cupid_gb_write_u8(rig->gb, 0xff01, byte);
    cupid_gb_write_u8(rig->gb, 0xff02, rig->control);
    assert(rig->gb->serial_bits_remaining == 8);
}

static void advance_bits(Rig *rig, unsigned samples) {
    assert(samples <= 8);
    const unsigned remaining = 8 - samples;
    unsigned guard = 0;
    while (rig->gb->serial_bits_remaining > remaining && guard++ < 2048)
        cupid_gb_tick(rig->gb, 1);
    assert(rig->gb->serial_bits_remaining == remaining);
}

static uint8_t finish_byte(Rig *rig) {
    advance_bits(rig, 8);
    cupid_gb_printer_update(rig->printer, rig->gb);
    assert((cupid_gb_read_u8(rig->gb, 0xff02) & 0x80u) == 0);
    return cupid_gb_read_u8(rig->gb, 0xff01);
}

static uint8_t send_byte(Rig *rig, uint8_t byte) {
    start_byte(rig, byte);
    return finish_byte(rig);
}

static void add_packet(Stream *stream, uint8_t command, bool compressed, const uint8_t *payload,
                       unsigned length, bool corrupt, uint8_t status) {
    assert(length <= 1023 && stream->length + length + 10 <= sizeof(stream->bytes));
    const uint8_t header[] = {
        0x88, 0x33, command, compressed, (uint8_t)length, (uint8_t)(length >> 8)};
    unsigned checksum = 0;
    for (unsigned i = 0; i < sizeof(header); ++i) {
        stream->bytes[stream->length++] = header[i];
        if (i >= 2)
            checksum += header[i];
    }
    for (unsigned i = 0; i < length; ++i) {
        stream->bytes[stream->length++] = payload[i];
        checksum += payload[i];
    }
    checksum ^= corrupt;
    stream->bytes[stream->length++] = (uint8_t)checksum;
    stream->bytes[stream->length++] = (uint8_t)(checksum >> 8);
    stream->replies[stream->length++] = 0x81;
    stream->replies[stream->length++] = status;
}

static void send_stream(Rig *rig, const Stream *stream) {
    for (size_t i = 0; i < stream->length; ++i)
        assert(send_byte(rig, stream->bytes[i]) == stream->replies[i]);
}

static void send_packet(Rig *rig, uint8_t command, bool compressed, const uint8_t *payload,
                        unsigned length, bool corrupt, uint8_t status) {
    Stream stream = {0};
    add_packet(&stream, command, compressed, payload, length, corrupt, status);
    send_stream(rig, &stream);
}

static void advance_clock(Rig *rig, uint64_t cycles, bool synchronize) {
    rig->gb->elapsed_cycles += cycles;
    if (synchronize)
        cupid_gb_printer_update(rig->printer, rig->gb);
}

static void assert_stripes(const Capture *capture, unsigned height) {
    assert(capture->images == 1 && capture->events[0].kind == 1);
    const Event *image = &capture->events[0];
    assert(image->height == height && image->top == 2 && image->bottom == 1);
    assert(image->exposure == 0x20);
    for (unsigned i = 0; i < height * CUPID_GB_PRINTER_WIDTH; ++i)
        assert(capture->pixels[i] == (i & 1u ? 0 : 255));
}

static void assert_same_events(const Rig *a, const Rig *b) {
    assert(a->capture.count == b->capture.count);
    assert(a->capture.images == b->capture.images);
    assert(a->capture.completed == b->capture.completed);
    assert(memcmp(a->capture.events, b->capture.events, sizeof(a->capture.events)) == 0);
    assert(memcmp(a->capture.pixels, b->capture.pixels, sizeof(a->capture.pixels)) == 0);
}

static void continue_packet(CupidGbModel model, SerialMode mode, const Stream *stream, size_t cut,
                            unsigned sampled_bits, bool trusted, unsigned rows) {
    assert(cut < stream->length && sampled_bits < 8);
    Rig *source = create_rig(model, mode == SERIAL_DOUBLE_SPEED);
    Rig *target = create_rig(model, mode == SERIAL_DOUBLE_SPEED);
    if (mode == SERIAL_COLOR_NORMAL)
        source->control = target->control = 0x81;
    for (size_t i = 0; i < cut; ++i)
        assert(send_byte(source, stream->bytes[i]) == stream->replies[i]);
    start_byte(source, stream->bytes[cut]);
    advance_bits(source, sampled_bits);
    Blob saved = save_rig(source, trusted);
    const unsigned prefix_images = source->capture.images;
    if (prefix_images)
        assert_stripes(&source->capture, rows);
    assert(load_rig(target, &saved, trusted) == CUPID_GB_STATE_OK);
    Blob roundtrip = save_rig(target, !trusted);
    assert_blobs_equal(&saved, &roundtrip);
    free_blob(roundtrip);
    free_blob(saved);
    clear_capture(source);
    clear_capture(target);
    assert(finish_byte(source) == stream->replies[cut]);
    assert(finish_byte(target) == stream->replies[cut]);
    for (size_t i = cut + 1; i < stream->length; ++i) {
        assert(send_byte(source, stream->bytes[i]) == stream->replies[i]);
        assert(send_byte(target, stream->bytes[i]) == stream->replies[i]);
    }
    assert(source->capture.images + prefix_images == 1);
    if (!prefix_images)
        assert_stripes(&source->capture, rows);
    const uint64_t finish = (uint64_t)cupid_gb_base_clock_hz(source->gb) * 30;
    advance_clock(source, finish, true);
    advance_clock(target, finish, true);
    assert(source->capture.completed == 1);
    assert_same_events(source, target);
    assert_same_state(source, target);
    ++continuations;
    destroy_rig(source);
    destroy_rig(target);
}

static void test_packet_bits(void) {
    Stream stream = {0};
    add_packet(&stream, 4, true, stripe_run, sizeof(stripe_run), false, 0);
    add_packet(&stream, 2, false, print_parameters, sizeof(print_parameters), false, 8);
    add_packet(&stream, 15, false, NULL, 0, false, 6);
    /* These byte positions cover sync, all header fields, both compressed run
     * edges, both checksum bytes, acknowledgement, status and the print edge. */
    const size_t cuts[] = {0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 33};
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m)
        for (unsigned api = 0; api < 2; ++api)
            for (unsigned c = 0; c < sizeof(cuts) / sizeof(cuts[0]); ++c)
                for (unsigned bits = 0; bits < 8; ++bits)
                    continue_packet(models[m], SERIAL_DEFAULT, &stream, cuts[c], bits, api != 0,
                                    16);
    for (unsigned bits = 0; bits < 8; ++bits) {
        continue_packet(CUPID_GB_MODEL_CGB, SERIAL_DOUBLE_SPEED, &stream, 18, bits, true, 16);
        continue_packet(CUPID_GB_MODEL_CGB, SERIAL_COLOR_NORMAL, &stream, 18, bits, false, 16);
    }
}

static void add_print_and_poll(Stream *stream) {
    add_packet(stream, 2, false, print_parameters, sizeof(print_parameters), false, 8);
    add_packet(stream, 15, false, NULL, 0, false, 6);
}

static void test_decoder_continuation(void) {
    for (unsigned api = 0; api < 2; ++api) {
        Stream repeated = {0};
        const uint8_t repeat_start[] = {0xff};
        const uint8_t repeat_tail[] = {0x55, 0xff, 0x55, 0xff, 0x55, 0xff, 0x55, 0xfa, 0x55};
        add_packet(&repeated, 4, true, repeat_start, sizeof(repeat_start), false, 0);
        add_packet(&repeated, 4, true, repeat_tail, sizeof(repeat_tail), false, 0);
        add_print_and_poll(&repeated);
        continue_packet(CUPID_GB_MODEL_CGB, SERIAL_DEFAULT, &repeated, 11, 4, api != 0, 16);

        Stream literal = {0};
        const uint8_t literal_start[] = {0x7f};
        uint8_t literal_tail[136];
        memset(literal_tail, 0x55, 128);
        const uint8_t runs[] = {0xff, 0x55, 0xff, 0x55, 0xff, 0x55, 0xfb, 0x55};
        memcpy(literal_tail + 128, runs, sizeof(runs));
        add_packet(&literal, 4, true, literal_start, sizeof(literal_start), false, 0);
        add_packet(&literal, 4, true, literal_tail, sizeof(literal_tail), false, 0);
        add_print_and_poll(&literal);
        continue_packet(CUPID_GB_MODEL_CGB, SERIAL_DEFAULT, &literal, 11, 0, api != 0, 16);
        continue_packet(CUPID_GB_MODEL_DMG_ABC, SERIAL_DEFAULT, &literal, 70, 7, api != 0, 16);

        Stream saturated = {0};
        uint8_t oversized[1023];
        memset(oversized, 0x55, sizeof(oversized));
        add_packet(&saturated, 4, false, oversized, sizeof(oversized), false, 0);
        add_print_and_poll(&saturated);
        /* The wire packet keeps advancing after the 640-byte decoder is full. */
        continue_packet(CUPID_GB_MODEL_CGB, SERIAL_DEFAULT, &saturated, 646, 3, api != 0, 16);

        Stream checksum = {0};
        add_packet(&checksum, 4, true, stripe_run, sizeof(stripe_run), true, 1);
        add_packet(&checksum, 4, true, stripe_run, sizeof(stripe_run), false, 0);
        add_print_and_poll(&checksum);
        continue_packet(CUPID_GB_MODEL_DMG_ABC, SERIAL_DEFAULT, &checksum, 18, 4, api != 0, 16);

        Stream full_page = {0};
        for (unsigned row = 0; row < 13; ++row)
            add_packet(&full_page, 4, true, stripe_run, sizeof(stripe_run), false, row ? 8 : 0);
        add_print_and_poll(&full_page);
        continue_packet(CUPID_GB_MODEL_CGB, SERIAL_DEFAULT, &full_page, 260, 0, api != 0, 200);

        Stream empty_page = {0};
        add_packet(&empty_page, 2, false, print_parameters, sizeof(print_parameters), false, 0);
        add_packet(&empty_page, 15, false, NULL, 0, false, 4);
        continue_packet(CUPID_GB_MODEL_CGB, SERIAL_DEFAULT, &empty_page, 13, 0, api != 0, 0);
        continue_packet(CUPID_GB_MODEL_CGB, SERIAL_DEFAULT, &empty_page, 13, 7, api != 0, 0);
    }
}

static void begin_job(Rig *rig) {
    send_packet(rig, 4, true, stripe_run, sizeof(stripe_run), false, 0);
    send_packet(rig, 2, false, print_parameters, sizeof(print_parameters), false, 8);
    assert_stripes(&rig->capture, 16);
}

static uint64_t cycles_until_done(const Rig *rig, const Blob *blob) {
    const uint8_t *device = printer_bytes(blob);
    const uint64_t ticks = read_le(device + WIRE_REMAINING_TICKS, 4);
    const uint64_t fraction = read_le(device + WIRE_SUB_TICK, 2);
    const uint64_t debt = rig->gb->elapsed_cycles - read_le(device + WIRE_LAST_CYCLE, 8);
    assert(ticks && ticks * 256 > fraction + debt);
    return ticks * 256 - fraction - debt;
}

static void assert_fresh_printer(const Rig *rig, const Blob *blob) {
    uint8_t expected[PRINTER_WIRE_BYTES] = {0};
    expected[WIRE_CLOCK_SEEN] = 1;
    write_le(expected + WIRE_LAST_CYCLE, 8, rig->gb->elapsed_cycles);
    assert(memcmp(printer_bytes(blob), expected, sizeof(expected)) == 0);
}

static void test_job_clocks(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m) {
        for (unsigned api = 0; api < 2; ++api) {
            for (unsigned scenario = 0; scenario < 5; ++scenario) {
                Rig *source = create_rig(models[m], false), *target = create_rig(models[m], false);
                begin_job(source);
                advance_clock(source, 173, true);
                if (scenario == 1)
                    send_packet(source, 1, false, NULL, 0, false, 0);
                else if (scenario == 2)
                    send_packet(source, 4, true, stripe_run, sizeof(stripe_run), false, 6);
                Blob timed = save_rig(source, api != 0);
                const uint64_t due = cycles_until_done(source, &timed);
                const uint8_t status = printer_bytes(&timed)[WIRE_STATUS];
                assert(status == (scenario == 1 ? 0 : scenario == 2 ? 8 : 6));
                assert(read_le(printer_bytes(&timed) + WIRE_SUB_TICK, 2) != 0);
                if (scenario == 3)
                    advance_clock(source, due, true);
                else if (scenario == 4)
                    advance_clock(source, due + 39, false);
                free_blob(timed);

                CupidGb *before = malloc(sizeof(*before));
                assert(before);
                memcpy(before, source->gb, sizeof(*before));
                const unsigned events = source->capture.count;
                Blob saved = save_rig(source, api != 0);
                Blob repeated = save_rig(source, api == 0);
                assert_blobs_equal(&saved, &repeated);
                assert(memcmp(before, source->gb, sizeof(*before)) == 0);
                assert(source->capture.count == events);
                free(before);
                free_blob(repeated);
                if (scenario == 3) {
                    assert(source->capture.completed == 1);
                    assert(printer_bytes(&saved)[WIRE_STATUS] == 6);
                    assert(read_le(printer_bytes(&saved) + WIRE_REMAINING_TICKS, 4) == 0);
                } else {
                    assert(source->capture.completed == 0);
                    assert(read_le(printer_bytes(&saved) + WIRE_REMAINING_TICKS, 4) > 0);
                }
                assert(load_rig(target, &saved, api != 0) == CUPID_GB_STATE_OK);
                Blob restored = save_rig(target, api == 0);
                assert_blobs_equal(&saved, &restored);
                free_blob(restored);
                clear_capture(source);
                clear_capture(target);

                if (scenario < 3) {
                    const uint64_t remaining = cycles_until_done(source, &saved);
                    advance_clock(source, remaining - 1, true);
                    advance_clock(target, remaining - 1, true);
                    assert(source->capture.completed == 0 && target->capture.completed == 0);
                    advance_clock(source, 1, true);
                    advance_clock(target, 1, true);
                    assert(source->capture.completed == 1 && target->capture.completed == 1);
                } else {
                    cupid_gb_printer_update(source->printer, source->gb);
                    cupid_gb_printer_update(target->printer, target->gb);
                    assert(source->capture.completed == (scenario == 4));
                    assert(target->capture.completed == (scenario == 4));
                }
                advance_clock(source, 1025, true);
                advance_clock(target, 1025, true);
                assert(source->capture.completed == (scenario != 3));
                assert_same_events(source, target);
                assert_same_state(source, target);
                free_blob(saved);
                ++continuations;
                destroy_rig(source);
                destroy_rig(target);
            }
        }
    }
}

static void test_timeout_stale_header(void) {
    for (unsigned api = 0; api < 2; ++api) {
        Rig *source = create_rig(CUPID_GB_MODEL_CGB, false);
        Rig *target = create_rig(CUPID_GB_MODEL_CGB, false);
        send_packet(source, 4, true, stripe_run, sizeof(stripe_run), false, 0);
        const uint8_t partial_header[] = {0x88, 0x33, 4, 1, 0x80};
        for (unsigned i = 0; i < sizeof(partial_header); ++i)
            assert(send_byte(source, partial_header[i]) == 0);
        advance_clock(source, ((uint64_t)cupid_gb_base_clock_hz(source->gb) + 1) * 256, true);
        start_byte(source, 0);
        Blob saved = save_rig(source, api != 0);
        const uint8_t *device = printer_bytes(&saved);
        assert(device[WIRE_PHASE] == 0 && read_le(device + WIRE_INDEX, 2) == 3);
        assert(device[WIRE_HEADER + 2] == 0x80);
        assert(read_le(device + WIRE_PAYLOAD_SIZE, 2) == sizeof(stripe_run));
        assert(device[WIRE_INCOMING_BITS] == 1);
        assert(load_rig(target, &saved, api != 0) == CUPID_GB_STATE_OK);
        assert(finish_byte(source) == 0 && finish_byte(target) == 0);
        send_packet(source, 2, false, print_parameters, sizeof(print_parameters), false, 8);
        send_packet(target, 2, false, print_parameters, sizeof(print_parameters), false, 8);
        assert_stripes(&source->capture, 16);
        assert_same_events(source, target);
        assert_same_state(source, target);
        free_blob(saved);
        ++continuations;
        destroy_rig(source);
        destroy_rig(target);
    }
}

static void attach_kind(Rig *rig, unsigned kind) {
    if (kind == 0)
        cupid_gb_set_serial_link(rig->gb, (CupidGbSerialLink){0});
    else if (kind == 1)
        cupid_gb_set_serial_link(rig->gb, cupid_gb_printer_link(rig->printer));
    else
        cupid_gb_set_serial_link(rig->gb,
                                 (CupidGbSerialLink){.launch = external_launch,
                                                     .sample = external_sample,
                                                     .user_data = &rig->external,
                                                     .device = CUPID_GB_SERIAL_DEVICE_NONE});
}

static void test_attachment_matrix(void) {
    for (unsigned api = 0; api < 2; ++api) {
        for (unsigned saved_kind = 0; saved_kind < 3; ++saved_kind) {
            for (unsigned live_kind = 0; live_kind < 3; ++live_kind) {
                Rig *source = create_rig(CUPID_GB_MODEL_CGB, false);
                Rig *target = create_rig(CUPID_GB_MODEL_CGB, false);
                send_packet(source, 4, true, stripe_run, sizeof(stripe_run), false, 0);
                begin_job(target);
                Blob live = save_rig(target, false);
                attach_kind(source, saved_kind);
                attach_kind(target, live_kind);
                advance_clock(source, 20000000, false);
                Blob saved = save_rig(source, api != 0);
                assert(saved.length ==
                       BARE_STATE_BYTES + (saved_kind == 1 ? PRINTER_WIRE_BYTES : 0));
                assert(load_rig(target, &saved, api != 0) == CUPID_GB_STATE_OK);
                if (live_kind == 1) {
                    Blob loaded = save_rig(target, false);
                    if (saved_kind == 1)
                        assert(memcmp(printer_bytes(&loaded), printer_bytes(&saved),
                                      PRINTER_WIRE_BYTES) == 0);
                    else
                        assert_fresh_printer(target, &loaded);
                    free_blob(loaded);
                } else {
                    Blob detached = save_rig(target, false);
                    assert(detached.length == BARE_STATE_BYTES);
                    const uint8_t none[DEVICE_DESCRIPTOR_BYTES] = {0};
                    assert(memcmp(detached.bytes + detached.length - sizeof(none), none,
                                  sizeof(none)) == 0);
                    free_blob(detached);
                    /* A detached allocation is still caller owned. Attaching it
                     * later must reveal its original, unrelated pending job. */
                    attach_kind(target, 1);
                    Blob reattached = save_rig(target, false);
                    assert(memcmp(printer_bytes(&reattached), printer_bytes(&live),
                                  PRINTER_WIRE_BYTES) == 0);
                    free_blob(reattached);
                }
                free_blob(live);
                free_blob(saved);
                destroy_rig(source);
                destroy_rig(target);
            }
        }
    }
}

static Blob copy_blob(const Blob *source, size_t length) {
    Blob copy = {calloc(length, 1), length};
    assert(copy.bytes);
    memcpy(copy.bytes, source->bytes, source->length < length ? source->length : length);
    return copy;
}

static void reject_unchanged(Rig *target, const Blob *invalid, bool trusted) {
    CupidGb *core = malloc(sizeof(*core));
    Capture *capture = malloc(sizeof(*capture));
    assert(core && capture);
    memcpy(core, target->gb, sizeof(*core));
    memcpy(capture, &target->capture, sizeof(*capture));
    Blob before = save_rig(target, false);
    const CupidGbStateContext context = target->context;
    const CupidGbStateResult result = load_rig(target, invalid, trusted);
    if (result != CUPID_GB_STATE_INVALID)
        fprintf(stderr, "Malformed printer case %u returned %s\n", rejected_states,
                cupid_gb_state_result_name(result));
    assert(result == CUPID_GB_STATE_INVALID);
    assert(memcmp(core, target->gb, sizeof(*core)) == 0);
    assert(memcmp(capture, &target->capture, sizeof(*capture)) == 0);
    assert(memcmp(&context, &target->context, sizeof(context)) == 0);
    Blob after = save_rig(target, true);
    assert_blobs_equal(&before, &after);
    free_blob(before);
    free_blob(after);
    free(core);
    free(capture);
    ++rejected_states;
}

static void test_malformed_devices(void) {
    Rig *source = create_rig(CUPID_GB_MODEL_CGB, false);
    Rig *target = create_rig(CUPID_GB_MODEL_CGB, false);
    send_packet(source, 4, true, stripe_run, sizeof(stripe_run), false, 0);
    begin_job(target);
    Blob valid = save_rig(source, false);
    const size_t descriptor = printer_descriptor(&valid);
    const size_t payload = descriptor + DEVICE_DESCRIPTOR_BYTES;

    const struct {
        unsigned offset;
        unsigned width;
        uint64_t value;
    } invalid_fields[] = {
        {WIRE_PHASE, 1, 7},
        {WIRE_INDEX, 2, 1024},
        {WIRE_PAYLOAD_SIZE, 2, 1024},
        {WIRE_DECODED_SIZE, 2, 641},
        {WIRE_RLE_REMAINING, 1, 130},
        {WIRE_RLE_REPEAT, 1, 2},
        {WIRE_STATUS, 1, 2},
        {WIRE_STATUS, 1, 0x80},
        {WIRE_ROWS, 1, 1},
        {WIRE_ROWS, 1, 201},
        {WIRE_PIXELS, 1, 4},
        {WIRE_INCOMING, 1, 1},
        {WIRE_INCOMING_BITS, 1, 8},
        {WIRE_REPLY_BIT, 1, 9},
        {WIRE_OUTPUT, 1, 2},
        {WIRE_CLOCK_SEEN, 1, 2},
        {WIRE_CLOCK_SEEN, 1, 0},
        {WIRE_LAST_CYCLE, 8, UINT64_MAX},
        {WIRE_SUB_TICK, 2, 256},
        {WIRE_REMAINING_TICKS, 4, 409601},
    };

    for (unsigned api = 0; api < 2; ++api) {
        for (unsigned i = 0; i < sizeof(invalid_fields) / sizeof(invalid_fields[0]); ++i) {
            Blob bad = copy_blob(&valid, valid.length);
            write_le(bad.bytes + payload + invalid_fields[i].offset, invalid_fields[i].width,
                     invalid_fields[i].value);
            resign(&bad);
            reject_unchanged(target, &bad, api != 0);
            free_blob(bad);
        }
        for (unsigned scenario = 0; scenario < 8; ++scenario) {
            Blob bad = copy_blob(&valid, valid.length);
            uint8_t *device = bad.bytes + payload;
            switch (scenario) {
                case 0:
                    device[WIRE_PHASE] = 2;
                    write_le(device + WIRE_INDEX, 2, 4);
                    break;
                case 1:
                    device[WIRE_PHASE] = 3;
                    write_le(device + WIRE_INDEX, 2, sizeof(stripe_run));
                    break;
                case 2:
                    device[WIRE_PHASE] = 3;
                    device[WIRE_HEADER + 2] = 0;
                    write_le(device + WIRE_PAYLOAD_SIZE, 2, 0);
                    break;
                case 3:
                    device[WIRE_PHASE] = 4;
                    write_le(device + WIRE_INDEX, 2, 2);
                    break;
                case 4:
                    device[WIRE_PHASE] = 4;
                    write_le(device + WIRE_INDEX, 2, 1);
                    write_le(device + WIRE_RECEIVED_CHECKSUM, 2, 256);
                    break;
                case 5:
                    device[WIRE_PHASE] = 5;
                    write_le(device + WIRE_INDEX, 2, 1);
                    break;
                case 6:
                    device[WIRE_PHASE] = 6;
                    device[WIRE_HEADER + 2] ^= 1;
                    break;
                case 7:
                    device[WIRE_RLE_REPEAT] = 0;
                    device[WIRE_RLE_REMAINING] = 129;
                    break;
            }
            resign(&bad);
            reject_unchanged(target, &bad, api != 0);
            free_blob(bad);
        }
        for (unsigned scenario = 0; scenario < 10; ++scenario) {
            const size_t length = valid.length + (scenario == 6) - (scenario == 7);
            Blob bad = copy_blob(&valid, length);
            switch (scenario) {
                case 0:
                    bad.bytes[descriptor] = 2;
                    break;
                case 1:
                    bad.bytes[descriptor + 1] = 2;
                    break;
                case 2:
                    bad.bytes[descriptor + 2] = 1;
                    break;
                case 3:
                    write_le(bad.bytes + descriptor + 4, 4, PRINTER_WIRE_BYTES - 1);
                    break;
                case 4:
                    write_le(bad.bytes + descriptor + 4, 4, PRINTER_WIRE_BYTES + 1);
                    break;
                case 5:
                    bad.bytes[descriptor] = 0;
                    break;
                case 8:
                    /* Valid accessory data must not commit before a later
                     * hardware validation failure has been resolved. */
                    bad.bytes[descriptor - SRAM_LENGTH_BYTES - IO_HISTORY_BYTES -
                              CUPID_GB_DISPLAY_CLOCK_STATE_SIZE - CUPID_GB_PIXEL_TIMING_STATE_SIZE -
                              CUPID_GB_VRAM_BUS_STATE_SIZE - CUPID_GB_AUDIO_STATE_SIZE -
                              RTC_HISTORY_BYTES - 1] = 16;
                    break;
                case 9:
                    /* A non-TPP1 core must reject even an otherwise valid motor value. */
                    bad.bytes[descriptor - SRAM_LENGTH_BYTES - IO_HISTORY_BYTES -
                              CUPID_GB_DISPLAY_CLOCK_STATE_SIZE - CUPID_GB_PIXEL_TIMING_STATE_SIZE -
                              CUPID_GB_VRAM_BUS_STATE_SIZE - CUPID_GB_AUDIO_STATE_SIZE -
                              RTC_HISTORY_BYTES + MOTOR_HISTORY_OFFSET] = 1;
                    break;
                default:
                    break;
            }
            resign(&bad);
            reject_unchanged(target, &bad, api != 0);
            free_blob(bad);
        }
        /* Malformed saved accessories are rejected even when the selected live
         * connection would discard their decoded state after a valid load. */
        for (unsigned live_kind = 0; live_kind < 3; ++live_kind) {
            attach_kind(target, live_kind);
            Blob bad = copy_blob(&valid, valid.length);
            bad.bytes[payload + WIRE_PIXELS] = 255;
            resign(&bad);
            reject_unchanged(target, &bad, api != 0);
            free_blob(bad);
        }
        attach_kind(target, 1);
    }
    free_blob(valid);
    destroy_rig(source);
    destroy_rig(target);
}

static void test_storage_aliases(void) {
    Rig *rig = create_rig(CUPID_GB_MODEL_CGB, false);
    begin_job(rig);
    Blob before = save_rig(rig, false);
    CupidGb *core = malloc(sizeof(*core));
    assert(core);
    memcpy(core, rig->gb, sizeof(*core));
    const CupidGbStateContext context = rig->context;
    for (unsigned api = 0; api < 2; ++api) {
        for (unsigned interior = 0; interior < 2; ++interior) {
            uint8_t *destination = (uint8_t *)rig->printer + interior * sizeof(void *);
            size_t written = 17;
            CupidGbStateResult result =
                api ? cupid_gb_save_state_with_context(rig->gb, destination, before.length,
                                                       &written, &rig->context)
                    : cupid_gb_save_state(rig->gb, destination, before.length, &written);
            assert(result == CUPID_GB_STATE_INVALID && written == 0);
        }
        void *aliases[] = {rig->printer, rig->gb, rig->gb->rom, &rig->context, before.bytes};
        for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
            /* The ordinary entry point has no trusted context to protect. */
            if (!api && aliases[i] == &rig->context)
                continue;
            const CupidGbStateResult result =
                api ? cupid_gb_save_state_with_context(rig->gb, before.bytes, before.length,
                                                       aliases[i], &rig->context)
                    : cupid_gb_save_state(rig->gb, before.bytes, before.length, aliases[i]);
            assert(result == CUPID_GB_STATE_INVALID);
        }
    }
    assert(cupid_gb_state_context_init((CupidGbStateContext *)rig->printer, rig->gb) ==
           CUPID_GB_STATE_INVALID);
    size_t written = 31;
    assert(cupid_gb_save_state_with_context(rig->gb, (uint8_t *)&rig->context, before.length,
                                            &written, &rig->context) == CUPID_GB_STATE_INVALID);
    assert(written == 0);
    assert(cupid_gb_load_state(rig->gb, (const uint8_t *)rig->printer, before.length) ==
           CUPID_GB_STATE_INVALID);
    assert(memcmp(core, rig->gb, sizeof(*core)) == 0);
    assert(memcmp(&context, &rig->context, sizeof(context)) == 0);
    Blob after = save_rig(rig, true);
    assert_blobs_equal(&before, &after);
    assert(rig->capture.images == 1 && rig->capture.completed == 0);
    free(core);
    free_blob(before);
    free_blob(after);
    destroy_rig(rig);
}

static void test_callback_registration(void) {
    for (unsigned mask = 0; mask < 4; ++mask) {
        Rig *source = create_rig(CUPID_GB_MODEL_CGB, false);
        Rig *target = create_rig(CUPID_GB_MODEL_CGB, false);
        begin_job(source);
        attach_kind(target, 0);
        cupid_gb_printer_destroy(target->printer);
        target->printer = cupid_gb_printer_create(mask & 1 ? on_image : NULL,
                                                  mask & 2 ? on_done : NULL, &target->capture);
        assert(target->printer);
        attach_kind(target, 1);
        Blob saved = save_rig(source, false);
        assert(load_rig(target, &saved, mask & 1) == CUPID_GB_STATE_OK);
        assert(target->capture.count == 0);
        advance_clock(target, cycles_until_done(target, &saved), true);
        assert(target->capture.completed == ((mask & 2) != 0));
        assert(target->capture.images == 0);
        send_packet(target, 1, false, NULL, 0, false, 0);
        send_packet(target, 4, true, stripe_run, sizeof(stripe_run), false, 0);
        send_packet(target, 2, false, print_parameters, sizeof(print_parameters), false, 8);
        assert(target->capture.images == ((mask & 1) != 0));
        advance_clock(target, (uint64_t)cupid_gb_base_clock_hz(target->gb) * 3, true);
        assert(target->capture.completed == ((mask & 2) ? 2u : 0u));
        Blob current = save_rig(target, true);
        assert(printer_bytes(&current)[WIRE_ROWS] == 0);
        free_blob(current);
        free_blob(saved);
        destroy_rig(source);
        destroy_rig(target);
    }
}

static void test_origins_and_lifecycle(void) {
    Rig *rig = create_rig(CUPID_GB_MODEL_CGB, false);
    Blob fresh = save_rig(rig, false);
    assert_fresh_printer(rig, &fresh);
    free_blob(fresh);
    advance_clock(rig, 255, true);
    Blob fractional = save_rig(rig, false);
    assert(read_le(printer_bytes(&fractional) + WIRE_SUB_TICK, 2) == 255);
    attach_kind(rig, 1);
    Blob same_attachment = save_rig(rig, true);
    assert_blobs_equal(&fractional, &same_attachment);
    free_blob(same_attachment);
    attach_kind(rig, 0);
    advance_clock(rig, 259, false);
    attach_kind(rig, 1);
    Blob reattached = save_rig(rig, false);
    assert(memcmp(printer_bytes(&fractional), printer_bytes(&reattached), PRINTER_WIRE_BYTES) == 0);
    free_blob(fractional);
    free_blob(reattached);
    cupid_gb_printer_update(rig->printer, rig->gb);
    Blob caught_up = save_rig(rig, false);
    assert(read_le(printer_bytes(&caught_up) + WIRE_SUB_TICK, 2) == 2);
    free_blob(caught_up);
    cupid_gb_printer_reset(rig->printer);
    Blob reset = save_rig(rig, false);
    const uint8_t empty[PRINTER_WIRE_BYTES] = {0};
    assert(memcmp(printer_bytes(&reset), empty, sizeof(empty)) == 0);
    free_blob(reset);
    attach_kind(rig, 1);
    fresh = save_rig(rig, false);
    assert_fresh_printer(rig, &fresh);
    free_blob(fresh);
    destroy_rig(rig);

    for (unsigned boot = 0; boot < 2; ++boot) {
        rig = create_rig(CUPID_GB_MODEL_CGB, false);
        begin_job(rig);
        const CupidGbSerialLink link = rig->gb->serial_link;
        const unsigned images = rig->capture.images;
        if (boot) {
            memset(rig->gb->boot_rom, 0, sizeof(rig->gb->boot_rom));
            rig->gb->boot_rom[0] = 0x18;
            rig->gb->boot_rom[1] = 0xfe;
            rig->gb->boot_rom_size = 0x900;
            cupid_gb_enter_boot_rom(rig->gb);
            fresh = save_rig(rig, false);
            assert_fresh_printer(rig, &fresh);
            free_blob(fresh);
        } else {
            cupid_gb_init(rig->gb);
        }
        assert(rig->gb->serial_link.launch == link.launch);
        assert(rig->gb->serial_link.sample == link.sample);
        assert(rig->gb->serial_link.user_data == link.user_data);
        assert(rig->gb->serial_link.device == link.device);
        assert(rig->capture.images == images && rig->capture.completed == 0);
        advance_clock(rig, (uint64_t)cupid_gb_base_clock_hz(rig->gb) * 30, true);
        assert(rig->capture.completed == 0);
        send_packet(rig, 15, false, NULL, 0, false, 0);
        clear_capture(rig);
        begin_job(rig);
        advance_clock(rig, (uint64_t)cupid_gb_base_clock_hz(rig->gb) * 3, true);
        assert(rig->capture.completed == 1);
        destroy_rig(rig);
    }

    rig = create_rig(CUPID_GB_MODEL_CGB, false);
    attach_kind(rig, 2);
    CupidGbSerialLink foreign = rig->gb->serial_link;
    foreign.device = CUPID_GB_SERIAL_DEVICE_PRINTER;
    cupid_gb_set_serial_link(rig->gb, foreign);
    const External before = rig->external;
    Blob unrecognized = save_rig(rig, false);
    assert(unrecognized.length == BARE_STATE_BYTES);
    assert(memcmp(&before, &rig->external, sizeof(before)) == 0);
    const uint8_t none[DEVICE_DESCRIPTOR_BYTES] = {0};
    assert(memcmp(unrecognized.bytes + unrecognized.length - sizeof(none), none, sizeof(none)) ==
           0);
    assert(send_byte(rig, 0x5a) == 0xff);
    assert(rig->external.launches == 8 && rig->external.samples == 8);
    free_blob(unrecognized);
    destroy_rig(rig);
}

static void test_earlier_timeline_reattachment(void) {
    for (unsigned api = 0; api < 2; ++api) {
        Rig *rig = create_rig(CUPID_GB_MODEL_CGB, false);
        attach_kind(rig, 0);
        Blob earlier = save_rig(rig, api != 0);
        attach_kind(rig, 1);
        begin_job(rig);
        advance_clock(rig, 255, true);
        Blob pending = save_rig(rig, api != 0);
        const unsigned events = rig->capture.count;
        attach_kind(rig, 0);
        assert(load_rig(rig, &earlier, api != 0) == CUPID_GB_STATE_OK);
        attach_kind(rig, 1);
        Blob reattached = save_rig(rig, api != 0);
        uint8_t expected[PRINTER_WIRE_BYTES];
        memcpy(expected, printer_bytes(&pending), sizeof(expected));
        write_le(expected + WIRE_LAST_CYCLE, 8, rig->gb->elapsed_cycles);
        write_le(expected + WIRE_SUB_TICK, 2, 0);
        assert(memcmp(expected, printer_bytes(&reattached), sizeof(expected)) == 0);
        assert(rig->capture.count == events);
        const uint64_t remaining = cycles_until_done(rig, &reattached);
        assert(remaining > 1);
        advance_clock(rig, remaining - 1, true);
        assert(rig->capture.completed == 0);
        advance_clock(rig, 1, true);
        assert(rig->capture.completed == 1 && rig->capture.images == 1);
        free_blob(reattached);
        free_blob(pending);
        free_blob(earlier);
        destroy_rig(rig);
    }
}

static void test_legacy_fresh_device(void) {
    Rig *source = create_rig(CUPID_GB_MODEL_SGB, false);
    attach_kind(source, 0);
    source->gb->elapsed_cycles = 0x1234567;
    memset(source->gb->apu.pulse_timing, 0, sizeof(source->gb->apu.pulse_timing));
    Blob current = save_rig(source, false);
    const size_t hardware_size = current.length - DEVICE_DESCRIPTOR_BYTES - SRAM_LENGTH_BYTES -
                                 IO_HISTORY_BYTES - CUPID_GB_DISPLAY_CLOCK_STATE_SIZE -
                                 CUPID_GB_PIXEL_TIMING_STATE_SIZE - CUPID_GB_VRAM_BUS_STATE_SIZE -
                                 CUPID_GB_AUDIO_STATE_SIZE - RTC_HISTORY_BYTES -
                                 INFRARED_HISTORY_BYTES;
    const size_t lengths[] = {300518,
                              300568,
                              300572,
                              hardware_size - 28,
                              hardware_size - 24,
                              hardware_size - 23,
                              hardware_size - 12,
                              hardware_size - 6,
                              hardware_size - 2,
                              hardware_size};
    for (unsigned api = 0; api < 2; ++api) {
        for (unsigned version = 2; version <= 11; ++version) {
            Rig *target = create_rig(CUPID_GB_MODEL_SGB, false);
            begin_job(target);
            Blob legacy = copy_blob(&current, lengths[version - 2]);
            write_le(legacy.bytes + 8, 4, version);
            resign(&legacy);
            const CupidGbStateResult result = load_rig(target, &legacy, api != 0);
            if (result != CUPID_GB_STATE_OK)
                fprintf(stderr, "Printer import from native version %u: %s\n", version,
                        cupid_gb_state_result_name(result));
            assert(result == CUPID_GB_STATE_OK);
            Blob loaded = save_rig(target, false);
            assert_fresh_printer(target, &loaded);
            assert(target->gb->elapsed_cycles == source->gb->elapsed_cycles);
            assert(target->capture.images == 1 && target->capture.completed == 0);
            advance_clock(target, 255, true);
            Blob fraction = save_rig(target, false);
            assert(read_le(printer_bytes(&fraction) + WIRE_SUB_TICK, 2) == 255);
            advance_clock(target, 1, true);
            Blob tick = save_rig(target, false);
            assert(read_le(printer_bytes(&tick) + WIRE_SUB_TICK, 2) == 0);
            assert(target->capture.completed == 0);
            free_blob(tick);
            free_blob(fraction);
            free_blob(loaded);
            free_blob(legacy);
            destroy_rig(target);
        }
    }
    free_blob(current);
    destroy_rig(source);
}

int main(void) {
    test_packet_bits();
    test_decoder_continuation();
    test_job_clocks();
    test_timeout_stale_header();
    test_attachment_matrix();
    test_malformed_devices();
    test_storage_aliases();
    test_callback_registration();
    test_origins_and_lifecycle();
    test_earlier_timeline_reattachment();
    test_legacy_fresh_device();
    printf("Printer state: %u uninterrupted continuations and %u transactional rejections\n",
           continuations, rejected_states);
    return 0;
}

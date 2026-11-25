/**
 * @file    printer_state.c
 * @brief   Portable serialization and validation of printer protocol and image history.
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

#include "printer_internal.h"

#include <string.h>

typedef struct PrinterStateIo {
    const uint8_t *input;
    uint8_t *output;
    size_t offset;
    bool invalid;
} PrinterStateIo;

static void bytes(PrinterStateIo *io, uint8_t *value, size_t count) {
    if (io->invalid || io->offset > PRINTER_STATE_WIRE_SIZE ||
        count > PRINTER_STATE_WIRE_SIZE - io->offset) {
        io->invalid = true;
        return;
    }
    if (io->input)
        memcpy(value, io->input + io->offset, count);
    else
        memcpy(io->output + io->offset, value, count);
    io->offset += count;
}

static void boolean(PrinterStateIo *io, bool *value) {
    uint8_t raw = io->input ? 0 : (uint8_t)*value;
    bytes(io, &raw, 1);
    if (io->input) {
        if (raw > 1)
            io->invalid = true;
        else
            *value = raw != 0;
    }
}

static void word(PrinterStateIo *io, uint16_t *value) {
    uint8_t raw[2] = {(uint8_t)*value, (uint8_t)(*value >> 8)};
    bytes(io, raw, sizeof(raw));
    if (io->input)
        *value = (uint16_t)((uint16_t)raw[0] | (uint16_t)((uint16_t)raw[1] << 8));
}

static void double_word(PrinterStateIo *io, uint32_t *value) {
    uint8_t raw[4];
    for (unsigned i = 0; i < sizeof(raw); ++i)
        raw[i] = (uint8_t)(*value >> (8 * i));
    bytes(io, raw, sizeof(raw));
    if (io->input) {
        *value = 0;
        for (unsigned i = 0; i < sizeof(raw); ++i)
            *value |= (uint32_t)raw[i] << (8 * i);
    }
}

static void quad_word(PrinterStateIo *io, uint64_t *value) {
    uint8_t raw[8];
    for (unsigned i = 0; i < sizeof(raw); ++i)
        raw[i] = (uint8_t)(*value >> (8 * i));
    bytes(io, raw, sizeof(raw));
    if (io->input) {
        *value = 0;
        for (unsigned i = 0; i < sizeof(raw); ++i)
            *value |= (uint64_t)raw[i] << (8 * i);
    }
}

static void fields(PrinterStateIo *io, CupidGbPrinterState *state) {
#define U8(field) bytes(io, &state->field, 1)
#define U16(field) word(io, &state->field)
#define U32(field) double_word(io, &state->field)
#define U64(field) quad_word(io, &state->field)
#define BOOL(field) boolean(io, &state->field)
#define BYTES(field) bytes(io, state->field, sizeof(state->field))
    U8(phase);
    BYTES(header);
    BYTES(payload);
    U16(index);
    U16(payload_size);
    U16(decoded_size);
    U16(checksum);
    U16(received_checksum);
    U8(rle_remaining);
    BOOL(rle_repeat);
    U8(status);
    U8(rows);
    BYTES(pixels);
    U8(incoming);
    U8(incoming_bits);
    U8(reply);
    U8(reply_bit);
    BOOL(output);
    BOOL(clock_seen);
    U64(last_cycle);
    U16(sub_tick);
    U64(idle_ticks);
    U32(remaining_ticks);
#undef U8
#undef U16
#undef U32
#undef U64
#undef BOOL
#undef BYTES
}

void cupid_gb_printer_state_clear(CupidGbPrinterState *state, uint64_t cycle) {
    memset(state, 0, sizeof(*state));
    /* Absent device history starts a new fractional clock at the load/reset
     * boundary. A same-device restore instead retains its original clock debt. */
    state->clock_seen = true;
    state->last_cycle = cycle;
}

bool cupid_gb_printer_state_valid(const CupidGbPrinterState *state, const CupidGb *gb) {
    if (!state || !gb || state->phase > PRINTER_STATUS || state->index > 1023 ||
        state->payload_size > 1023 || state->decoded_size > PRINTER_DATA_BYTES ||
        state->rle_remaining > (state->rle_repeat ? 129u : 128u) ||
        state->rows > PRINTER_MAX_ROWS || (state->rows % 16 && state->rows != PRINTER_MAX_ROWS) ||
        state->incoming_bits > 7 || state->incoming >= (1u << state->incoming_bits) ||
        state->reply_bit > 8 || state->sub_tick >= PRINTER_BASE_CYCLES_PER_TICK)
        return false;
    const unsigned status = state->status & (unsigned)~PRINTER_CHECKSUM_ERROR;
    if (status != 0 && status != PRINTER_DONE && status != PRINTER_PRINTING &&
        status != PRINTER_BUFFERED)
        return false;
    if (state->phase == PRINTER_HEADER && state->index >= sizeof(state->header))
        return false;
    if (state->phase >= PRINTER_PAYLOAD) {
        const unsigned payload_size = state->header[2] | ((state->header[3] & 3u) << 8);
        if (state->payload_size != payload_size)
            return false;
    }
    if (state->phase == PRINTER_PAYLOAD &&
        (!state->payload_size || state->index >= state->payload_size))
        return false;
    if (state->phase == PRINTER_CHECKSUM &&
        (state->index >= 2 || (state->index == 1 && state->received_checksum > 255)))
        return false;
    if (state->phase >= PRINTER_ALIVE && state->index != 2)
        return false;
    /* A timeout can retain an unfinished header and inactive indices. A printed
     * image can also retain grayscale bytes outside its now-empty active area. */
    for (unsigned i = 0; i < (unsigned)state->rows * CUPID_GB_PRINTER_WIDTH; ++i) {
        if (state->pixels[i] > 3)
            return false;
    }
    if (state->clock_seen) {
        if (state->last_cycle > gb->elapsed_cycles)
            return false;
    } else if (state->last_cycle || state->sub_tick || state->idle_ticks ||
               state->remaining_ticks) {
        return false;
    }
    const uint32_t maximum_job =
        PRINTER_MAX_ROWS * cupid_gb_base_clock_hz(gb) / (PRINTER_BASE_CYCLES_PER_TICK * 8u);
    /* INIT and DATA change status without cancelling the completion timer.
     * Conversely, an expired job keeps its old status until the next poll. */
    return state->remaining_ticks <= maximum_job;
}

bool cupid_gb_printer_state_encode(const CupidGbPrinterState *state, uint8_t *data, size_t length) {
    if (!state || !data || length != PRINTER_STATE_WIRE_SIZE)
        return false;
    PrinterStateIo io = {NULL, data, 0, false};
    /* The output path reads fields only, matching the console's explicit codec. */
    fields(&io, (CupidGbPrinterState *)state);
    return !io.invalid && io.offset == length;
}

bool cupid_gb_printer_state_decode(CupidGbPrinterState *state, const uint8_t *data, size_t length) {
    if (!state || !data || length != PRINTER_STATE_WIRE_SIZE)
        return false;
    memset(state, 0, sizeof(*state));
    PrinterStateIo io = {data, NULL, 0, false};
    fields(&io, state);
    return !io.invalid && io.offset == length;
}

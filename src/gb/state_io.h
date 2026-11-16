/**
 * @file    state_io.h
 * @brief   Bounds-checked little-endian primitives for native state codecs.
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

#ifndef CUPID_GB_STATE_IO_H
#define CUPID_GB_STATE_IO_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct StateIo {
    const uint8_t *input;
    uint8_t *output;
    size_t offset;
    size_t capacity;
    bool invalid;
} StateIo;

static inline bool reserve(StateIo *io, size_t count) {
    if (io->invalid || io->offset > io->capacity || count > io->capacity - io->offset) {
        io->invalid = true;
        return false;
    }
    return true;
}

static inline void bytes(StateIo *io, uint8_t *value, size_t count) {
    if (!reserve(io, count)) {
        return;
    }
    if (io->input) {
        memcpy(value, io->input + io->offset, count);
    } else if (io->output) {
        memcpy(io->output + io->offset, value, count);
    }
    io->offset += count;
}

static inline void byte(StateIo *io, uint8_t *value) {
    bytes(io, value, 1);
}

static inline void boolean(StateIo *io, bool *value) {
    uint8_t raw = io->input ? 0 : (uint8_t)*value;
    byte(io, &raw);
    if (io->input) {
        if (raw > 1) {
            io->invalid = true;
        } else {
            *value = raw != 0;
        }
    }
}

static inline void word(StateIo *io, uint16_t *value) {
    uint8_t raw[2] = {(uint8_t)*value, (uint8_t)(*value >> 8)};
    bytes(io, raw, sizeof(raw));
    if (io->input) {
        *value = (uint16_t)((uint16_t)raw[0] | (uint16_t)((uint16_t)raw[1] << 8));
    }
}

static inline void double_word(StateIo *io, uint32_t *value) {
    uint8_t raw[4];
    for (unsigned i = 0; i < 4; ++i) {
        raw[i] = (uint8_t)(*value >> (i * 8));
    }
    bytes(io, raw, sizeof(raw));
    if (io->input) {
        *value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            *value |= (uint32_t)raw[i] << (i * 8);
        }
    }
}

static inline void quad_word(StateIo *io, uint64_t *value) {
    uint8_t raw[8];
    for (unsigned i = 0; i < 8; ++i) {
        raw[i] = (uint8_t)(*value >> (i * 8));
    }
    bytes(io, raw, sizeof(raw));
    if (io->input) {
        *value = 0;
        for (unsigned i = 0; i < 8; ++i) {
            *value |= (uint64_t)raw[i] << (i * 8);
        }
    }
}

static inline void signed_quad_word(StateIo *io, int64_t *value) {
    uint64_t raw = (uint64_t)*value;
    quad_word(io, &raw);
    if (io->input) {
        *value = raw <= INT64_MAX ? (int64_t)raw : -1 - (int64_t)(UINT64_MAX - raw);
    }
}

static inline void size_value(StateIo *io, size_t *value) {
    uint64_t raw = (uint64_t)*value;
    quad_word(io, &raw);
    if (io->input) {
        if (raw > SIZE_MAX) {
            io->invalid = true;
        } else {
            *value = (size_t)raw;
        }
    }
}

static inline void words(StateIo *io, uint16_t *values, size_t count) {
    /* Check the entire wire range before touching an array. Each element still has
     * an explicit byte order, including when the state buffer is unaligned. */
    if (count > SIZE_MAX / 2) {
        io->invalid = true;
        return;
    }
    const size_t byte_count = count * 2;
    if (!reserve(io, byte_count)) {
        return;
    }
    if (io->input) {
        const uint8_t *source = io->input + io->offset;
        for (size_t i = 0; i < count; ++i) {
            values[i] =
                (uint16_t)((uint16_t)source[i * 2] | (uint16_t)((uint16_t)source[i * 2 + 1] << 8));
        }
    } else if (io->output) {
        uint8_t *destination = io->output + io->offset;
        for (size_t i = 0; i < count; ++i) {
            destination[i * 2] = (uint8_t)values[i];
            destination[i * 2 + 1] = (uint8_t)(values[i] >> 8);
        }
    }
    io->offset += byte_count;
}

#endif

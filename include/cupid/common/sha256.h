/**
 * @file    sha256.h
 * @brief   Streaming and one-shot SHA-256 hashing interfaces.
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

#ifndef CUPID_COMMON_SHA256_H
#define CUPID_COMMON_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CupidSha256 {
    uint32_t words[8];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} CupidSha256;

void cupid_sha256_init(CupidSha256 *context);
void cupid_sha256_update(CupidSha256 *context, const void *data, size_t length);
void cupid_sha256_final(CupidSha256 *context, uint8_t digest[32]);
/* Hash the concatenation without copying either span. A zero-length span may be NULL.
 * The digest may overlap either input; all input is consumed before output is written. */
void cupid_sha256_parts(const void *first, size_t first_length, const void *second,
                        size_t second_length, uint8_t digest[32]);
void cupid_sha256(const void *data, size_t length, uint8_t digest[32]);

#ifdef __cplusplus
}
#endif
#endif

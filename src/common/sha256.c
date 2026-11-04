#include "cupid/common/sha256.h"

#include <string.h>

/* SHA-256 compression, padding, and initial values follow FIPS 180-4 sections 4-6. */
static uint32_t rotate_right(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32u - bits));
}

static uint32_t read_big_endian(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
           data[3];
}

static void compress_block(CupidSha256 *context, const uint8_t block[64]) {
    static const uint32_t constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t schedule[64];
    for (unsigned i = 0; i < 16; ++i) {
        schedule[i] = read_big_endian(block + i * 4);
    }
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t a = schedule[i - 15], b = schedule[i - 2];
        uint32_t small0 = rotate_right(a, 7) ^ rotate_right(a, 18) ^ (a >> 3);
        uint32_t small1 = rotate_right(b, 17) ^ rotate_right(b, 19) ^ (b >> 10);
        schedule[i] = schedule[i - 16] + small0 + schedule[i - 7] + small1;
    }
    uint32_t a = context->words[0], b = context->words[1], c = context->words[2];
    uint32_t d = context->words[3], e = context->words[4], f = context->words[5];
    uint32_t g = context->words[6], h = context->words[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t large1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = h + large1 + choose + constants[i] + schedule[i];
        uint32_t large0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + large0 + majority;
    }
    context->words[0] += a;
    context->words[1] += b;
    context->words[2] += c;
    context->words[3] += d;
    context->words[4] += e;
    context->words[5] += f;
    context->words[6] += g;
    context->words[7] += h;
}

void cupid_sha256_init(CupidSha256 *context) {
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(context->words, initial, sizeof(initial));
    context->length = 0;
    context->used = 0;
}

void cupid_sha256_update(CupidSha256 *context, const void *data, size_t length) {
    const uint8_t *bytes = data;
    context->length += (uint64_t)length;
    while (length) {
        if (!context->used && length >= 64) {
            compress_block(context, bytes);
            bytes += 64;
            length -= 64;
        } else {
            size_t amount = 64 - context->used;
            if (amount > length) {
                amount = length;
            }
            memcpy(context->block + context->used, bytes, amount);
            context->used += amount;
            bytes += amount;
            length -= amount;
            if (context->used == 64) {
                compress_block(context, context->block);
                context->used = 0;
            }
        }
    }
}

void cupid_sha256_final(CupidSha256 *context, uint8_t digest[32]) {
    uint64_t bits = context->length * UINT64_C(8);
    context->block[context->used++] = 0x80;
    if (context->used > 56) {
        memset(context->block + context->used, 0, 64 - context->used);
        compress_block(context, context->block);
        context->used = 0;
    }
    memset(context->block + context->used, 0, 56 - context->used);
    for (unsigned i = 0; i < 8; ++i) {
        context->block[63 - i] = (uint8_t)(bits >> (i * 8));
    }
    compress_block(context, context->block);
    for (unsigned i = 0; i < 8; ++i) {
        for (unsigned j = 0; j < 4; ++j) {
            digest[i * 4 + j] = (uint8_t)(context->words[i] >> (24 - j * 8));
        }
    }
    memset(context, 0, sizeof(*context));
}

void cupid_sha256(const void *data, size_t length, uint8_t digest[32]) {
    CupidSha256 context;
    cupid_sha256_init(&context);
    cupid_sha256_update(&context, data, length);
    cupid_sha256_final(&context, digest);
}

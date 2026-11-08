#include "cupid/common/sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_hex(const uint8_t digest[32], const char *expected) {
    char hex[65];
    for (unsigned i = 0; i < 32; ++i) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    assert(strcmp(hex, expected) == 0);
}

static void known_vectors(void) {
    const char *messages[] = {"", "abc",
                              "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *expected[] = {"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    uint8_t digest[32];
    for (unsigned i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        size_t length = strlen(messages[i]);
        for (size_t split = 0; split <= length; ++split) {
            cupid_sha256_parts(messages[i], split, messages[i] + split, length - split, digest);
            expect_hex(digest, expected[i]);
        }
    }
    cupid_sha256_parts(NULL, 0, NULL, 0, digest);
    expect_hex(digest, expected[0]);
    char *million = malloc(1000000);
    assert(million);
    memset(million, 'a', 1000000);
    cupid_sha256_parts(million, 123, million + 123, 1000000 - 123, digest);
    expect_hex(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    free(million);
}

static void portable_digest(const uint8_t *data, size_t length, uint8_t digest[32]) {
    CupidSha256 context;
    cupid_sha256_init(&context);
    for (size_t offset = 0; offset < length;) {
        size_t amount = length - offset;
        if (amount > 67) {
            amount = 67;
        }
        cupid_sha256_update(&context, data + offset, amount);
        offset += amount;
    }
    cupid_sha256_final(&context, digest);
}

static void boundaries_and_aliases(void) {
    const size_t lengths[] = {0,   1,   31,  32,   55,    56,    63,    64,     65,
                              127, 128, 129, 4097, 65535, 65536, 65537, 300486, 1048576};
    uint8_t *allocation = malloc(1048576 + 64);
    assert(allocation);
    uint8_t *data = allocation + 1; /* Both APIs must accept unaligned input. */
    for (size_t i = 0; i < 1048576 + 32; ++i) {
        data[i] = (uint8_t)(i * 37u + 17u);
    }
    for (size_t item = 0; item < sizeof(lengths) / sizeof(lengths[0]); ++item) {
        size_t length = lengths[item];
        uint8_t expected[32], actual[32];
        portable_digest(data, length, expected);
        cupid_sha256(data, length, actual);
        assert(memcmp(expected, actual, sizeof(actual)) == 0);
        cupid_sha256_parts(NULL, 0, data, length, actual);
        assert(memcmp(expected, actual, sizeof(actual)) == 0);
        cupid_sha256_parts(data, length, NULL, 0, actual);
        assert(memcmp(expected, actual, sizeof(actual)) == 0);
        size_t splits[] = {0, length / 2, length ? length - 1 : 0, length};
        for (unsigned split_index = 0; split_index < 4; ++split_index) {
            size_t split = splits[split_index];
            cupid_sha256_parts(data, split, data + split, length - split, actual);
            assert(memcmp(expected, actual, sizeof(actual)) == 0);
        }
        if (length >= 64) {
            uint8_t saved[32];
            memcpy(saved, data, sizeof(saved));
            cupid_sha256_parts(data, length / 2, data + length / 2, length - length / 2, data);
            assert(memcmp(expected, data, sizeof(expected)) == 0);
            memcpy(data, saved, sizeof(saved));
            size_t second = length / 2;
            memcpy(saved, data + second, sizeof(saved));
            cupid_sha256_parts(data, second, data + second, length - second, data + second);
            assert(memcmp(expected, data + second, sizeof(expected)) == 0);
            memcpy(data + second, saved, sizeof(saved));
        }
    }
    free(allocation);
}

static void separate_spans(void) {
    uint8_t first[19], second[81], joined[100], expected[32], actual[32];
    memset(first, 0x27, sizeof(first));
    memset(second, 0xad, sizeof(second));
    memcpy(joined, first, sizeof(first));
    memcpy(joined + sizeof(first), second, sizeof(second));
    portable_digest(joined, sizeof(joined), expected);
    cupid_sha256_parts(first, sizeof(first), second, sizeof(second), actual);
    assert(memcmp(expected, actual, sizeof(actual)) == 0);
}

int main(void) {
    known_vectors();
    boundaries_and_aliases();
    separate_spans();
    puts("SHA-256 vector, span, alignment, and alias checks passed.");
    return 0;
}

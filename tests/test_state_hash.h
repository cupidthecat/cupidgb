/**
 * @file    test_state_hash.h
 * @brief   Snapshot digest regressions and hexadecimal fixture checks.
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

#ifndef CUPID_TEST_STATE_HASH_H
#define CUPID_TEST_STATE_HASH_H

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

#endif

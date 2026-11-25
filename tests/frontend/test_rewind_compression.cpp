/**
 * @file    test_rewind_compression.cpp
 * @brief   Verify rewind byte preservation, ordinary captures and large SRAM sampling.
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

#include "rewind_compression.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {
constexpr qsizetype LargeStateBytes = 2271194;
constexpr qsizetype InlineRamOffset = 16529;
constexpr qsizetype InlineRamBytes = 128 * 1024;
constexpr qsizetype TailRamBytes = 2 * 1024 * 1024 - InlineRamBytes;

char nextRandom(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return char(state);
}

QByteArray randomBytes(qsizetype size) {
    QByteArray bytes(size, '\0');
    uint32_t state = 0x619ad325;
    for (char &byte : bytes)
        byte = nextRandom(state);
    return bytes;
}

void checkRoundTrip(const QByteArray &state, int expectedLevel) {
    CupidRewind::CompressionChoice choice;
    const QByteArray encoded = CupidRewind::compressSnapshot(state, &choice);
    assert(choice.level == expectedLevel);
    assert(!choice.sampleFailed);
    assert(choice.inputBytes <= CupidRewind::MaximumSampleBytes);
    assert(choice.samples <= CupidRewind::SampleCount);
    if (state.isEmpty()) {
        assert(encoded.isEmpty());
        return;
    }
    assert(!encoded.isEmpty());
    assert(qUncompress(encoded) == state);
    if (state.size() < CupidRewind::SamplingThreshold) {
        assert(choice.samples == 0 && choice.inputBytes == 0);
        assert(encoded == qCompress(state, 1));
    } else {
        assert(choice.samples != 0 && choice.inputBytes != 0);
    }
}

QByteArray repeatedBytes(qsizetype period) {
    const QByteArray block = randomBytes(period);
    QByteArray bytes;
    bytes.reserve(LargeStateBytes);
    while (bytes.size() < LargeStateBytes)
        bytes.append(block.constData(), qMin(period, LargeStateBytes - bytes.size()));
    return bytes;
}

QByteArray splitSramLayout() {
    // This synthetic buffer models split SRAM storage; it is not a native state.
    // The first 128 KiB SRAM span precedes hardware/history,
    // with the remaining SRAM immediately before the final accessory descriptor.
    QByteArray bytes(LargeStateBytes, '\0');
    uint32_t state = 0x619ad325;
    for (qsizetype i = 0; i < InlineRamBytes; ++i)
        bytes[InlineRamOffset + i] = nextRandom(state);
    const qsizetype tailOffset = bytes.size() - 8 - TailRamBytes;
    for (qsizetype i = 0; i < TailRamBytes; ++i)
        bytes[tailOffset + i] = nextRandom(state);
    return bytes;
}

void checkIndependentCaptures() {
    QByteArray state = splitSramLayout();
    checkRoundTrip(state, 0);
    const QByteArray first = CupidRewind::compressSnapshot(state);
    state[InlineRamOffset] ^= 0x5a;
    state[state.size() - 9] ^= 0x33;
    const QByteArray changed = CupidRewind::compressSnapshot(state);
    assert(qUncompress(changed) == state);
    state[InlineRamOffset] ^= 0x5a;
    state[state.size() - 9] ^= 0x33;
    assert(qUncompress(first) == state);
    assert(first != changed);
}
} // namespace

int main() {
    checkRoundTrip(QByteArray{}, 1);
    checkRoundTrip(QByteArray(1, '\0'), 1);
    checkRoundTrip(randomBytes(305114), 1);
    checkRoundTrip(randomBytes(CupidRewind::SamplingThreshold - 1), 1);
    checkRoundTrip(randomBytes(CupidRewind::SamplingThreshold), 0);
    checkRoundTrip(randomBytes(CupidRewind::SamplingThreshold + 1), 0);
    checkRoundTrip(randomBytes(LargeStateBytes), 0);
    checkRoundTrip(QByteArray(LargeStateBytes, '\0'), 1);
    checkRoundTrip(repeatedBytes(8 * 1024), 1);
    checkRoundTrip(repeatedBytes(16 * 1024), 1);

    QByteArray mixed(LargeStateBytes / 2, '\0');
    mixed.append(randomBytes(LargeStateBytes - mixed.size()));
    checkRoundTrip(mixed, 1);
    checkIndependentCaptures();
    std::puts("Rewind compression checks passed.");
    return 0;
}

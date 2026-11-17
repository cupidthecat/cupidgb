/**
 * @file    rewind_compression.cpp
 * @brief   Sample large rewind states without changing snapshot bytes or framing.
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

#include <algorithm>

namespace CupidRewind {
CompressionChoice compressionChoice(const QByteArray &state) {
    CompressionChoice choice;
    if (state.size() < SamplingThreshold)
        return choice;
    // Independent 32 KiB windows detect repeated 8/16 KiB banks without inventing
    // matches between distant regions. Distributed windows cover the hardware prefix
    // and large SRAM tail while limiting preliminary compression to 256 KiB.
    const qsizetype bucketSize = state.size() / SampleCount;
    const qsizetype remainder = state.size() % SampleCount;
    for (unsigned sample = 0; sample < SampleCount; ++sample) {
        const qsizetype index = qsizetype(sample);
        const qsizetype begin = index * bucketSize + std::min(index, remainder);
        const qsizetype width = bucketSize + (index < remainder ? 1 : 0);
        const qsizetype offset = begin + (width - SampleBytes) / 2;
        const QByteArray encoded =
            qCompress(reinterpret_cast<const uchar *>(state.constData() + offset), SampleBytes, 1);
        ++choice.samples;
        choice.inputBytes += SampleBytes;
        if (encoded.isEmpty()) {
            choice.sampleFailed = true;
            choice.level = 0;
            return choice;
        }
        choice.compressedBytes += encoded.size();
    }
    // A stored stream keeps the same qUncompress envelope when predicted savings
    // are small. Mixed states can still benefit from full compression.
    if (choice.compressedBytes * 100 >= choice.inputBytes * 90)
        choice.level = 0;
    return choice;
}

QByteArray compressSnapshot(const QByteArray &state, CompressionChoice *details) {
    const CompressionChoice choice = compressionChoice(state);
    if (details)
        *details = choice;
    return state.isEmpty() ? QByteArray{} : qCompress(state, choice.level);
}
} // namespace CupidRewind

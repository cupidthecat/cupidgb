/**
 * @file    rewind_compression.h
 * @brief   Choose a lossless Qt snapshot compression level with bounded sampling.
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

#pragma once

#include <QByteArray>

namespace CupidRewind {
constexpr qsizetype SamplingThreshold = 512 * 1024;
constexpr unsigned SampleCount = 8;
constexpr qsizetype SampleBytes = 32 * 1024;
constexpr qsizetype MaximumSampleBytes = SampleCount * SampleBytes;

struct CompressionChoice {
    int level = 1;
    unsigned samples = 0;
    qsizetype inputBytes = 0;
    qsizetype compressedBytes = 0;
    bool sampleFailed = false;
};

CompressionChoice compressionChoice(const QByteArray &state);
QByteArray compressSnapshot(const QByteArray &state, CompressionChoice *choice = nullptr);
} // namespace CupidRewind

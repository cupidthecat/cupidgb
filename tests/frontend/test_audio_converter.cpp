/**
 * @file    test_audio_converter.cpp
 * @brief   Regression tests for streaming host audio conversion and rate changes.
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

#include "audio_clock_drift.h"
#include "audio_clock_trace.h"
#include "audio_converter.h"

#include <QScopeGuard>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {
bool check(bool condition, const char *message) {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

QAudioFormat format(int rate, int channels, QAudioFormat::SampleFormat sampleFormat) {
    QAudioFormat result;
    result.setSampleRate(rate);
    result.setChannelCount(channels);
    result.setSampleFormat(sampleFormat);
    return result;
}

bool checkClockTrace() {
    const bool hadVariable = qEnvironmentVariableIsSet("CUPIDGB_AUDIO_TRACE");
    const QByteArray original = qgetenv("CUPIDGB_AUDIO_TRACE");
    const auto restore = qScopeGuard([hadVariable, original] {
        if (hadVariable)
            qputenv("CUPIDGB_AUDIO_TRACE", original);
        else
            qunsetenv("CUPIDGB_AUDIO_TRACE");
    });
    qunsetenv("CUPIDGB_AUDIO_TRACE");
    AudioClockTrace disabled;
    disabled.pump({});
    disabled.stateChanged(1, 0);
    if (!check(!disabled.enabled() && disabled.report()["samples"].toArray().isEmpty(),
               "Disabled audio tracing retained samples"))
        return false;
    qputenv("CUPIDGB_AUDIO_TRACE", "1");
    AudioClockTrace trace;
    trace.reset(100);
    AudioClockTrace::Pump sample{100, 4, 8, 12, 16, 0, false, 0.97, 0.98, 2, -20};
    trace.pump(sample);
    sample.rawNanoseconds = 1000100;
    sample.acceptedBytes = 20;
    trace.pump(sample);
    trace.stateChanged(2000100, 1);
    sample.rawNanoseconds = 10000100;
    trace.pump(sample);
    const auto report = trace.report();
    const auto rows = report["samples"].toArray();
    if (!check(rows.size() == 3 && rows[1].toArray()[2].toInt() == 1 &&
                   rows[1].toArray()[3].toInt() == 20 && rows[1].toArray()[7].toInt() == 1 &&
                   rows[2].toArray()[2].toInt() == 0 &&
                   rows[2].toArray()[0].toDouble() == 10000000 &&
                   report["maximum_pump_gap_nanoseconds"].toDouble() == 9000000,
               "Audio trace lost decimated queue, state-change or clock observations"))
        return false;
    trace.reset(0);
    for (size_t index = 0; index < AudioClockTrace::maximumSamples + 3; ++index) {
        sample.rawNanoseconds = qint64(index) * AudioClockTrace::sampleIntervalNanoseconds;
        trace.pump(sample);
    }
    const auto full = trace.report();
    return check(full["samples"].toArray().size() == int(AudioClockTrace::maximumSamples) &&
                     full["truncated_samples"].toInt() == 3,
                 "Audio trace exceeded its explicit sample capacity");
}
} // namespace

int main() {
    if (!checkClockTrace())
        return 1;
    // Native writes contain both new audio and buffer filling. Only the sum
    // of accepted frames and writable capacity isolates actual consumption.
    AudioClockDrift drift;
    for (int tick = 0; tick <= 5000; ++tick) {
        const auto now = int64_t(tick) * 10000000;
        const auto consumed = uint64_t(double(now) * 48000 * 0.972 / 1e9);
        const int64_t queued = 100 + tick % 17 * 11;
        drift.observe(now, consumed + uint64_t(queued), 10000 - queued, 48000);
    }
    if (!check(drift.updates() == 50 && std::abs(drift.ratio() - 0.972) < 0.00005,
               "Host clock estimate followed buffer filling or missed drift"))
        return 1;
    const double honestRate = drift.ratio();
    if (!check(drift.playbackRatio(2160, 48000) == honestRate &&
                   drift.playbackRatio(0, 48000) > honestRate &&
                   drift.playbackRatio(4800, 48000) < honestRate,
               "Queue feedback did not stabilize playback lead"))
        return 1;
    drift.interrupt(INT64_C(50001000000));
    if (!check(drift.ratio() == honestRate && drift.updates() == 50,
               "A transient underrun discarded the clock estimate"))
        return 1;
    drift.interrupt(INT64_C(56000000000));
    if (!check(drift.ratio() == 1 && !drift.updates(),
               "A long underrun retained a stale clock estimate"))
        return 1;
    drift.reset();
    drift.observe(INT64_C(50000000000), 2300000, 10000, 48000);
    drift.observe(INT64_C(50010000000), 0, 10000, 48000);
    if (!check(drift.ratio() == 1 && !drift.updates(), "Reset sink retained old drift"))
        return 1;
    drift.observe(INT64_C(56020000000), 300000, 10000, 48000);
    if (!check(drift.ratio() == 1 && !drift.updates(), "Host stall became a drift sample"))
        return 1;
    drift.observe(INT64_C(57020000000), 900000, 10000, 48000);
    if (!check(drift.ratio() == 1 && !drift.updates(), "Invalid sink rate changed playback"))
        return 1;
    drift.reset();
    for (int tick = 0; tick <= 200; ++tick)
        drift.observe(int64_t(tick) * 10000000, uint64_t(tick) * 480, 2000, 48000);
    if (!check(drift.ratio() == 1 && drift.updates() == 2,
               "Nominal host audio changed the converter ratio"))
        return 1;
    drift.reset();
    uint64_t consumedFrames = 0;
    for (int second = 0; second <= 40; ++second) {
        drift.observe(int64_t(second) * 1000000000, consumedFrames + 1000, 1000, 48000);
        consumedFrames += second < 20 ? 46656u : 48960u;
        if (second == 19 &&
            !check(std::abs(drift.ratio() - 0.972) < 0.00001, "Initial host rate did not settle"))
            return 1;
    }
    if (!check(std::abs(drift.ratio() - 1.02) < 0.00001,
               "Host rate recovery retained an old clock estimate"))
        return 1;
    std::vector<int16_t> sine(44100 * 2);
    for (size_t i = 0; i < 44100; ++i) {
        sine[i * 2] = int16_t(std::sin(double(i) * 6.283185307179586 * 1000 / 44100) * 16000);
        sine[i * 2 + 1] = int16_t(-sine[i * 2]);
    }
    const auto output = format(48000, 2, QAudioFormat::Float);
    AudioConverter whole(output);
    const QByteArray expected = whole.convert(sine.data(), 44100);
    AudioConverter blocks(output);
    QByteArray actual;
    for (size_t i = 0; i < 44100; i += 137) {
        const size_t count = std::min(size_t(137), size_t(44100) - i);
        actual += blocks.convert(sine.data() + i * 2, count);
    }
    if (!check(actual == expected, "Block boundaries changed converted samples"))
        return 1;
    const auto frames = actual.size() / 8;
    if (!check(frames >= 47998 && frames <= 48000, "Incorrect 44.1 kHz to 48 kHz sample count"))
        return 1;
    int crossings = 0;
    float previous = 0;
    for (qsizetype i = 0; i < frames; ++i) {
        float samples[2];
        std::memcpy(samples, actual.constData() + i * 8, 8);
        if (!check(std::isfinite(samples[0]) && std::abs(samples[0]) <= 0.5f &&
                       samples[0] == -samples[1],
                   "Invalid amplitude or incorrect stereo order"))
            return 1;
        if (previous <= 0 && samples[0] > 0)
            ++crossings;
        previous = samples[0];
    }
    if (!check(crossings >= 999 && crossings <= 1001, "Resampling changed sine frequency"))
        return 1;
    AudioConverter adjusted(output);
    adjusted.setHostClockRatio(0.972);
    const auto adjustedBytes = adjusted.convert(sine.data(), 44100);
    if (!check(std::abs(double(adjustedBytes.size() / 8) - 48000 * 0.972) <= 2,
               "Host drift correction produced an incorrect playback duration"))
        return 1;
    crossings = 0;
    previous = 0;
    for (qsizetype frame = 0; frame < adjustedBytes.size() / 8; ++frame) {
        float sample;
        std::memcpy(&sample, adjustedBytes.constData() + frame * 8, sizeof(sample));
        if (previous <= 0 && sample > 0)
            ++crossings;
        previous = sample;
    }
    if (!check(crossings >= 999 && crossings <= 1001,
               "Host drift correction lost source waveform cycles"))
        return 1;
    AudioConverter adjustedBlocks(output);
    adjustedBlocks.setHostClockRatio(0.972);
    QByteArray adjustedParts;
    for (size_t start = 0; start < 44100; start += 137)
        adjustedParts += adjustedBlocks.convert(sine.data() + start * 2,
                                                std::min(size_t(137), size_t(44100) - start));
    if (!check(adjustedParts == adjustedBytes, "Adjusted converter changed across block splits"))
        return 1;
    adjusted.reset();
    if (!check(adjusted.convert(sine.data(), 44100) == expected,
               "Converter reset retained a previous host clock"))
        return 1;
    adjusted.reset();
    adjusted.setHostClockRatio(std::numeric_limits<double>::quiet_NaN());
    if (!check(adjusted.convert(sine.data(), 44100) == expected,
               "Invalid host clock changed nominal conversion"))
        return 1;
    AudioConverter slow(output);
    slow.setInputRate(22050);
    const QByteArray stretched = slow.convert(sine.data(), 44100);
    if (!check(stretched.size() / 8 >= 95996 && stretched.size() / 8 <= 96000,
               "Half speed did not double host audio duration"))
        return 1;
    crossings = 0;
    previous = 0;
    for (qsizetype i = 0; i < stretched.size() / 8; ++i) {
        float sample;
        std::memcpy(&sample, stretched.constData() + i * 8, sizeof(sample));
        if (previous <= 0 && sample > 0)
            ++crossings;
        previous = sample;
    }
    if (!check(crossings >= 999 && crossings <= 1001, "Half-speed output lost waveform cycles"))
        return 1;
    AudioConverter mono(format(32000, 1, QAudioFormat::Float));
    const auto monoBytes = mono.convert(sine.data(), 44100);
    if (!check(monoBytes.size() / 4 >= 31998 && monoBytes.size() / 4 <= 32000,
               "Incorrect downsampled frame count"))
        return 1;
    for (const auto byte : monoBytes)
        if (!check(byte == 0, "Opposite stereo channels did not cancel in mono"))
            return 1;
    AudioConverter surround(format(48000, 6, QAudioFormat::Float));
    const auto surroundBytes = surround.convert(sine.data(), 32);
    for (qsizetype i = 0; i < surroundBytes.size() / 24; ++i) {
        float samples[6];
        std::memcpy(samples, surroundBytes.constData() + i * 24, sizeof(samples));
        if (!check(samples[0] == -samples[1] && samples[2] == 0 && samples[3] == 0 &&
                       samples[4] == 0 && samples[5] == 0,
                   "Surround host channel layout changed stereo content"))
            return 1;
    }
    const int16_t extremes[] = {-32768, 32767, -32768, 32767};
    for (const auto sampleFormat :
         {QAudioFormat::UInt8, QAudioFormat::Int16, QAudioFormat::Int32, QAudioFormat::Float}) {
        const auto sampleFormatDescription = format(44100, 2, sampleFormat);
        AudioConverter converter(sampleFormatDescription);
        const QByteArray bytes = converter.convert(extremes, 2);
        if (!check(bytes.size() == sampleFormatDescription.bytesPerFrame(), "Wrong sample width"))
            return 1;
        if (sampleFormat == QAudioFormat::UInt8 &&
            !check(uint8_t(bytes[0]) == 0 && uint8_t(bytes[1]) == 255, "UInt8 extrema conversion"))
            return 1;
        if (sampleFormat == QAudioFormat::Int16 &&
            !check(std::memcmp(bytes.constData(), extremes, 4) == 0, "Int16 extrema conversion"))
            return 1;
        if (sampleFormat == QAudioFormat::Int32) {
            int32_t values[2];
            std::memcpy(values, bytes.constData(), sizeof(values));
            if (!check(values[0] == INT32_MIN && values[1] == 2147418112,
                       "Int32 extrema conversion"))
                return 1;
        }
        converter.reset();
        if (!check(converter.convert(extremes, 2) == bytes, "Reset changed conversion output"))
            return 1;
    }
    std::cout << "CupidGB audio conversion regression passed\n";
    return 0;
}

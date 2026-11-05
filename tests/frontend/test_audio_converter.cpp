#include "audio_converter.h"

#include <cmath>
#include <cstring>
#include <iostream>
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
} // namespace

int main() {
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

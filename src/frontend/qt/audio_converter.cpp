#include "audio_converter.h"

#include <algorithm>
#include <cmath>
#include <limits>

AudioConverter::AudioConverter(QAudioFormat output) : output_(output) {
}

void AudioConverter::reset() {
    inputFrames_ = 0;
    nextPosition_ = 0;
    previous_[0] = previous_[1] = 0;
}

void AudioConverter::setInputRate(double rate) {
    if (std::isfinite(rate) && rate >= 4000 && rate <= 384000)
        inputRate_ = rate;
}

void AudioConverter::append(QByteArray &bytes, float sample) const {
    sample = std::clamp(sample, -1.0f, 1.0f);
    switch (output_.sampleFormat()) {
        case QAudioFormat::UInt8: {
            const uint8_t value = uint8_t(std::clamp(std::lround(sample * 128 + 128), 0L, 255L));
            bytes.append(reinterpret_cast<const char *>(&value), sizeof(value));
            break;
        }
        case QAudioFormat::Int16: {
            const int16_t value = int16_t(std::clamp(std::lround(sample * 32768), -32768L, 32767L));
            bytes.append(reinterpret_cast<const char *>(&value), sizeof(value));
            break;
        }
        case QAudioFormat::Int32: {
            const int32_t value = int32_t(std::clamp(std::llround(double(sample) * 2147483648.0),
                                                     -2147483648LL, 2147483647LL));
            bytes.append(reinterpret_cast<const char *>(&value), sizeof(value));
            break;
        }
        case QAudioFormat::Float:
            bytes.append(reinterpret_cast<const char *>(&sample), sizeof(sample));
            break;
        default:
            break;
    }
}

QByteArray AudioConverter::convert(const int16_t *samples, size_t frames) {
    QByteArray bytes;
    if (!samples || !frames || !output_.isValid())
        return bytes;
    const uint64_t end = inputFrames_ + frames;
    const double step = inputRate_ / output_.sampleRate();
    // Delay the final fractional sample until its right neighbor is available.
    while (nextPosition_ < double(end - 1)) {
        const auto first = uint64_t(nextPosition_);
        const float fraction = float(nextPosition_ - double(first));
        float stereo[2];
        for (size_t channel = 0; channel < 2; ++channel) {
            const float left = first < inputFrames_
                                   ? previous_[channel]
                                   : samples[(first - inputFrames_) * 2 + channel] / 32768.0f;
            const float right = samples[(first + 1 - inputFrames_) * 2 + channel] / 32768.0f;
            stereo[channel] = left + (right - left) * fraction;
        }
        for (int channel = 0; channel < output_.channelCount(); ++channel) {
            const float value = output_.channelCount() == 1 ? (stereo[0] + stereo[1]) * 0.5f
                                : channel < 2               ? stereo[channel]
                                                            : 0.0f;
            append(bytes, value);
        }
        nextPosition_ += step;
    }
    previous_[0] = samples[(frames - 1) * 2] / 32768.0f;
    previous_[1] = samples[(frames - 1) * 2 + 1] / 32768.0f;
    inputFrames_ = end;
    return bytes;
}

#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <cstddef>
#include <cstdint>

// Converts interleaved stereo samples at the configured playback rate for the host sink.
// Fractional source positions and boundary samples persist across input blocks.
class AudioConverter {
  public:
    explicit AudioConverter(QAudioFormat output);
    QByteArray convert(const int16_t *samples, size_t frames);
    void setInputRate(double rate);
    void reset();

  private:
    void append(QByteArray &bytes, float sample) const;
    QAudioFormat output_;
    double inputRate_ = 44100;
    uint64_t inputFrames_ = 0;
    double nextPosition_ = 0;
    float previous_[2] = {};
};

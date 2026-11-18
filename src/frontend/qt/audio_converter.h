/**
 * @file    audio_converter.h
 * @brief   Streaming audio format and rate conversion state and interface.
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
    void setHostClockRatio(double ratio);

    double hostClockRatio() const {
        return hostClockRatio_;
    }

    void reset();

  private:
    void append(QByteArray &bytes, float sample) const;
    QAudioFormat output_;
    double inputRate_ = 44100;
    double hostClockRatio_ = 1;
    uint64_t inputFrames_ = 0;
    double nextPosition_ = 0;
    float previous_[2] = {};
};

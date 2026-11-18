/**
 * @file    wave_recorder.h
 * @brief   Stereo PCM WAVE recorder state and lifecycle interface.
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

#include <QSaveFile>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <memory>

// Streams signed 16-bit stereo PCM into an atomically committed RIFF/WAVE file.
// Host mute, volume, resampling, and playback speed do not alter these samples.
class WaveRecorder final {
  public:
    static constexpr uint64_t maximumFrames = (UINT32_MAX - 36ULL) / 4;

    bool start(const QString &path, uint32_t sampleRate);
    bool append(const int16_t *samples, size_t frames);
    bool finish();
    bool active() const;
    uint64_t framesWritten() const;
    uint32_t sampleRate() const;
    QString errorString() const;

  private:
    friend struct WaveRecorderTestAccess;
    QByteArray header() const;
    bool write(const QByteArray &bytes);

    std::unique_ptr<QSaveFile> file_;
    uint64_t frames_ = 0;
    uint32_t sampleRate_ = 0;
    QString error_;
    bool failed_ = false;
};

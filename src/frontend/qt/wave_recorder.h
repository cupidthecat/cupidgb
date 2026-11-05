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
    QByteArray header() const;
    bool write(const QByteArray &bytes);

    std::unique_ptr<QSaveFile> file_;
    uint64_t frames_ = 0;
    uint32_t sampleRate_ = 0;
    QString error_;
    bool failed_ = false;
};

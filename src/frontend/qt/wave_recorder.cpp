#include "wave_recorder.h"

#include <QDataStream>
#include <QtEndian>
#include <algorithm>

QByteArray WaveRecorder::header() const {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + frames_ * 4);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(2) << quint32(sampleRate_);
    stream << quint32(sampleRate_ * 4) << quint16(4) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(frames_ * 4);
    return bytes;
}

bool WaveRecorder::write(const QByteArray &bytes) {
    if (file_->write(bytes) == bytes.size())
        return true;
    error_ = file_->errorString();
    failed_ = true;
    return false;
}

bool WaveRecorder::start(const QString &path, uint32_t sampleRate) {
    if (active()) {
        error_ = "An audio recording is already open.";
        return false;
    }
    error_.clear();
    if (!sampleRate || sampleRate > UINT32_MAX / 4) {
        error_ = "The audio sample rate is invalid.";
        return false;
    }
    frames_ = 0;
    sampleRate_ = sampleRate;
    failed_ = false;
    file_ = std::make_unique<QSaveFile>(path);
    if (!file_->open(QIODevice::WriteOnly)) {
        error_ = file_->errorString();
        file_.reset();
        return false;
    }
    if (!write(header())) {
        file_->cancelWriting();
        file_.reset();
        return false;
    }
    return true;
}

bool WaveRecorder::append(const int16_t *samples, size_t frames) {
    if (!active() || failed_)
        return false;
    if (frames > maximumFrames - frames_) {
        // Keep the valid prefix available for finish(), without wrapping RIFF's 32-bit size.
        error_ = "The WAVE recording reached the RIFF size limit. Start a new recording.";
        return false;
    }
    if (!samples && frames) {
        error_ = "The audio sample buffer is invalid.";
        return false;
    }
    // Bound temporary memory independently of caller block size and encode little endian.
    for (size_t offset = 0; offset < frames;) {
        const size_t count = std::min(size_t(4096), frames - offset);
        QByteArray bytes(qsizetype(count * 4), Qt::Uninitialized);
        for (size_t i = 0; i < count * 2; ++i)
            qToLittleEndian<quint16>(quint16(samples[offset * 2 + i]), bytes.data() + i * 2);
        if (!write(bytes))
            return false;
        frames_ += count;
        offset += count;
    }
    return true;
}

bool WaveRecorder::finish() {
    if (!active())
        return true;
    bool success = !failed_;
    if (success && !file_->seek(0)) {
        error_ = file_->errorString();
        success = false;
    }
    if (success)
        success = write(header());
    if (success && !file_->commit()) {
        error_ = file_->errorString();
        success = false;
    }
    if (!success)
        file_->cancelWriting();
    file_.reset();
    return success;
}

bool WaveRecorder::active() const {
    return bool(file_);
}

uint64_t WaveRecorder::framesWritten() const {
    return frames_;
}

uint32_t WaveRecorder::sampleRate() const {
    return sampleRate_;
}

QString WaveRecorder::errorString() const {
    return error_;
}

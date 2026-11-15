#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cstdint>

// Keep generated audio until the host accepts it. Callers retain this object
// across writes because a backend may dispatch session-changing Qt events.
class AudioBuffer final {
  public:
    explicit AudioBuffer(int bytesPerFrame = 4) : bytesPerFrame_(std::max(1, bytesPerFrame)) {
    }

    void append(const char *data, qsizetype size) {
        QMutexLocker lock(&mutex_);
        queue_.append(data, size);
        maximumQueued_ = std::max(maximumQueued_, queue_.size());
    }

    void clear() {
        QMutexLocker lock(&mutex_);
        queue_.clear();
        ++queueGeneration_;
    }

    qint64 bytesAvailable() const {
        QMutexLocker lock(&mutex_);
        return queue_.size();
    }

    void resetTelemetry() {
        QMutexLocker lock(&mutex_);
        bytesRead_ = 0;
        maximumQueued_ = queue_.size();
        ++telemetryGeneration_;
    }

    QJsonObject telemetry() const {
        QMutexLocker lock(&mutex_);
        return {{"host_frames_written_to_sink", double(bytesRead_) / bytesPerFrame_},
                {"maximum_queued_bytes", double(maximumQueued_)},
                {"remaining_queued_bytes", double(queue_.size())}};
    }

    qint64 writeTo(QIODevice *device, qint64 maximum) {
        QByteArray pending;
        std::uint64_t generation;
        std::uint64_t telemetryGeneration;
        {
            QMutexLocker lock(&mutex_);
            // Nested event dispatch must not send the same prefix twice.
            if (writing_ || !device)
                return 0;
            const qint64 available = std::min(maximum, qint64(queue_.size()));
            const qint64 aligned = available - available % bytesPerFrame_;
            if (aligned <= 0)
                return 0;
            pending = queue_.left(qsizetype(aligned));
            generation = queueGeneration_;
            telemetryGeneration = telemetryGeneration_;
            writing_ = true;
        }

        // A backend write can dispatch Qt events. Never hold the queue lock
        // across it: reporting and timeline changes also inspect this queue.
        const qint64 written = device->write(pending.constData(), pending.size());
        // Release any shared prefix before removing it from the original queue.
        pending.clear();
        {
            QMutexLocker lock(&mutex_);
            writing_ = false;
            if (written > 0) {
                // A reentrant timeline clear may already have replaced the queue.
                // Its new samples do not belong to this in-flight transaction.
                if (generation == queueGeneration_)
                    queue_.remove(0, qsizetype(written));
                // A reentrant report reset starts a new accounting window.
                if (telemetryGeneration == telemetryGeneration_)
                    bytesRead_ += std::uint64_t(written);
            }
        }
        return written;
    }

  private:
    mutable QMutex mutex_;
    QByteArray queue_;
    std::uint64_t bytesRead_ = 0;
    std::uint64_t queueGeneration_ = 0;
    std::uint64_t telemetryGeneration_ = 0;
    int bytesPerFrame_ = 4;
    qsizetype maximumQueued_ = 0;
    bool writing_ = false;
};

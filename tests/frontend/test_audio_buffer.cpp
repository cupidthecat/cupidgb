/**
 * @file    test_audio_buffer.cpp
 * @brief   Regression tests for queued audio, partial writes, and reentrant delivery.
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

#include "audio_buffer.h"
#include "audio_sink_lifetime.h"

#include <cassert>
#include <functional>
#include <iostream>
#include <memory>

class HostSink final : public QIODevice {
  public:
    HostSink() {
        open(QIODevice::WriteOnly | QIODevice::Unbuffered);
    }

    QByteArray received;
    qint64 allowance = 100000;
    bool fail = false;
    bool writing = false;
    std::function<void()> duringWrite;

  protected:
    qint64 readData(char *, qint64) override {
        return -1;
    }

    qint64 writeData(const char *data, qint64 size) override {
        writing = true;
        if (duringWrite)
            duringWrite();
        if (fail) {
            writing = false;
            return -1;
        }
        const qint64 accepted = std::min(size, allowance);
        received.append(data, qsizetype(accepted));
        writing = false;
        return accepted;
    }
};

static void partial_and_failed_writes() {
    AudioBuffer buffer;
    HostSink sink;
    const QByteArray pcm("abcdefghijklmnop");
    buffer.append(pcm.constData(), pcm.size());
    assert(buffer.writeTo(&sink, 3) == 0);
    assert(buffer.writeTo(nullptr, 100) == 0);
    sink.allowance = 4;
    assert(buffer.writeTo(&sink, 7) == 4);
    assert(buffer.bytesAvailable() == 12);
    sink.fail = true;
    assert(buffer.writeTo(&sink, 12) == -1);
    assert(buffer.bytesAvailable() == 12);
    sink.fail = false;
    sink.allowance = 0;
    assert(buffer.writeTo(&sink, 12) == 0);
    assert(buffer.bytesAvailable() == 12);
    sink.allowance = 100;
    assert(buffer.writeTo(&sink, 12) == 12);
    assert(buffer.bytesAvailable() == 0);
    assert(sink.received == pcm);
    const auto counters = buffer.telemetry();
    assert(counters.value("host_frames_written_to_sink").toDouble() == 4);
    assert(counters.value("maximum_queued_bytes").toInt() == 16);
    assert(counters.value("remaining_queued_bytes").toInt() == 0);
}

static void reentrant_report_and_append() {
    AudioBuffer buffer;
    HostSink sink;
    buffer.append("abcdefgh", 8);
    sink.duringWrite = [&] {
        // Native backend event dispatch can run reporting or append callbacks.
        // None may wait for a lock held across the external write operation.
        assert(buffer.bytesAvailable() == 8);
        assert(buffer.telemetry().value("remaining_queued_bytes").toInt() == 8);
        buffer.append("ijkl", 4);
        assert(buffer.writeTo(&sink, 12) == 0);
    };
    assert(buffer.writeTo(&sink, 8) == 8);
    assert(buffer.bytesAvailable() == 4);
    sink.duringWrite = {};
    assert(buffer.writeTo(&sink, 4) == 4);
    assert(sink.received == "abcdefghijkl");
}

static void reentrant_timeline_replacement() {
    AudioBuffer buffer;
    HostSink sink;
    buffer.append("old!", 4);
    sink.duringWrite = [&] {
        buffer.clear();
        buffer.append("new!", 4);
        buffer.resetTelemetry();
    };
    assert(buffer.writeTo(&sink, 4) == 4);
    assert(buffer.bytesAvailable() == 4);
    assert(buffer.telemetry().value("host_frames_written_to_sink").toDouble() == 0);
    sink.duringWrite = {};
    assert(buffer.writeTo(&sink, 4) == 4);
    assert(sink.received == "old!new!");
    assert(buffer.bytesAvailable() == 0);
    assert(buffer.telemetry().value("host_frames_written_to_sink").toDouble() == 1);
}

static void retained_queue_lifetime() {
    auto current = std::make_shared<AudioBuffer>();
    const std::weak_ptr<AudioBuffer> old = current;
    current->append("old!", 4);
    HostSink sink;
    sink.duringWrite = [&] {
        current = std::make_shared<AudioBuffer>();
        current->append("new!", 4);
        assert(!old.expired());
    };
    {
        const auto inFlight = current;
        assert(inFlight->writeTo(&sink, 4) == 4);
        assert(inFlight->bytesAvailable() == 0);
        assert(current->bytesAvailable() == 4);
    }
    assert(old.expired());
    sink.duringWrite = {};
    assert(current->writeTo(&sink, 4) == 4);
    assert(sink.received == "old!new!");
}

struct BackendEvents {
    unsigned resets = 0;
    unsigned destroyed = 0;
    bool resetDuringWrite = false;
};

class HostBackend final : public QObject {
  public:
    explicit HostBackend(BackendEvents &events)
        : events_(events), device(std::make_unique<HostSink>()) {
    }

    ~HostBackend() override {
        ++events_.destroyed;
    }

    void reset() {
        ++events_.resets;
        events_.resetDuringWrite = device && device->writing;
        device.reset();
    }

    BackendEvents &events_;
    std::unique_ptr<HostSink> device;
};

static void backend_retirement_waits_for_active_call() {
    BackendEvents events;
    QObject receiver;
    auto current = createAudioSink<HostBackend>(events);
    AudioBuffer buffer;
    buffer.append("old!", 4);
    {
        const auto inFlight = current;
        HostSink *device = inFlight->device.get();
        device->duringWrite = [&] {
            // A synchronous pause/close detaches the current backend. Its device
            // must survive the remaining QIODevice::write call and queue commit.
            retireAudioSink(current, &receiver);
            assert(!current && !events.resets && !events.destroyed);
        };
        assert(buffer.writeTo(device, 4) == 4);
        assert(device->received == "old!");
        assert(buffer.bytesAvailable() == 0);
        assert(!events.resets && !events.destroyed);
    }
    assert(events.resets == 1 && events.destroyed == 1 && !events.resetDuringWrite);

    BackendEvents idleEvents;
    current = createAudioSink<HostBackend>(idleEvents);
    retireAudioSink(current, &receiver);
    assert(!current && idleEvents.resets == 1 && idleEvents.destroyed == 1);
}

int main() {
    partial_and_failed_writes();
    reentrant_report_and_append();
    reentrant_timeline_replacement();
    retained_queue_lifetime();
    backend_retirement_waits_for_active_call();
    std::cout << "Audio queue partial writes and reentrant host callbacks passed.\n";
}

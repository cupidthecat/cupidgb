/**
 * @file    audio_clock_trace.h
 * @brief   Bounded opt-in diagnostics for native audio clocks and playback queues.
 * @author  cupidthecat
 * @date    2026-11-18
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cstdint>
#include <vector>

class AudioClockTrace {
  public:
    static constexpr size_t maximumSamples = 16384;
    static constexpr qint64 sampleIntervalNanoseconds = 10000000;

    struct Pump {
        qint64 rawNanoseconds = 0;
        std::uint64_t acceptedBytes = 0;
        qint64 freeBytes = -1;
        qint64 nativeBufferBytes = -1;
        qint64 softwareBytes = 0;
        int state = -1;
        bool priming = false;
        double estimatedRatio = 1;
        double appliedRatio = 1;
        std::uint64_t clockUpdates = 0;
        qint64 pacingLeadNanoseconds = 0;
    };

    AudioClockTrace() : enabled_(qEnvironmentVariableIntValue("CUPIDGB_AUDIO_TRACE") == 1) {
        if (enabled_) {
            samples_.reserve(maximumSamples);
            adjusted_.start();
        }
    }

    bool enabled() const {
        return enabled_;
    }

    void reset(qint64 rawNanoseconds) {
        if (!enabled_)
            return;
        samples_.clear();
        rawOrigin_ = rawNanoseconds;
        adjusted_.start();
        previousPump_ = -1;
        previousSample_ = -1;
        maximumPumpGap_ = 0;
        pendingPumpGap_ = 0;
        truncated_ = 0;
        last_ = {};
    }

    void pump(const Pump &sample) {
        if (!enabled_)
            return;
        if (previousPump_ >= 0) {
            const qint64 gap = std::max(qint64(0), sample.rawNanoseconds - previousPump_);
            pendingPumpGap_ = std::max(pendingPumpGap_, gap);
            maximumPumpGap_ = std::max(maximumPumpGap_, gap);
        }
        previousPump_ = sample.rawNanoseconds;
        last_ = sample;
        if (previousSample_ >= 0 &&
            sample.rawNanoseconds - previousSample_ < sampleIntervalNanoseconds)
            return;
        append(sample, false);
        previousSample_ = sample.rawNanoseconds;
        pendingPumpGap_ = 0;
    }

    void stateChanged(qint64 rawNanoseconds, int state) {
        if (!enabled_)
            return;
        Pump sample = last_;
        sample.rawNanoseconds = rawNanoseconds;
        sample.state = state;
        append(sample, true);
    }

    QJsonObject report() const {
        QJsonArray rows;
        for (const auto &sample : samples_) {
            const auto &pump = sample.pump;
            rows.append(QJsonArray{
                double(pump.rawNanoseconds - rawOrigin_), double(sample.adjustedNanoseconds),
                sample.stateChange ? 1 : 0, double(pump.acceptedBytes), double(pump.freeBytes),
                double(pump.nativeBufferBytes), double(pump.softwareBytes), pump.state,
                pump.priming ? 1 : 0, pump.estimatedRatio, pump.appliedRatio,
                double(pump.clockUpdates), double(pump.pacingLeadNanoseconds),
                double(sample.maximumPumpGapNanoseconds)});
        }
        return {{"sample_interval_nanoseconds", double(sampleIntervalNanoseconds)},
                {"capacity", double(maximumSamples)},
                {"truncated_samples", double(truncated_)},
                {"maximum_pump_gap_nanoseconds", double(maximumPumpGap_)},
                {"state_change_values", "Queue and ratio fields retain the latest pump sample"},
                {"columns",
                 QJsonArray{"raw_nanoseconds", "adjusted_nanoseconds", "state_change",
                            "accepted_bytes", "free_bytes", "native_buffer_bytes", "software_bytes",
                            "state", "priming", "estimated_ratio", "applied_ratio", "clock_updates",
                            "pacing_lead_nanoseconds", "maximum_pump_gap_nanoseconds"}},
                {"samples", rows}};
    }

  private:
    struct Sample {
        Pump pump;
        qint64 adjustedNanoseconds;
        qint64 maximumPumpGapNanoseconds;
        bool stateChange;
    };

    void append(const Pump &pump, bool stateChange) {
        if (samples_.size() == maximumSamples) {
            ++truncated_;
            return;
        }
        samples_.push_back({pump, adjusted_.nsecsElapsed(), pendingPumpGap_, stateChange});
    }

    bool enabled_;
    std::vector<Sample> samples_;
    QElapsedTimer adjusted_;
    Pump last_;
    qint64 rawOrigin_ = 0;
    qint64 previousPump_ = -1;
    qint64 previousSample_ = -1;
    qint64 maximumPumpGap_ = 0;
    qint64 pendingPumpGap_ = 0;
    std::uint64_t truncated_ = 0;
};

/**
 * @file    audio_clock_drift.h
 * @brief   Estimate native audio consumption against the independent host clock.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Written frames plus free backend frames advance by exactly the number of
// consumed frames. Buffer filling therefore cannot masquerade as a faster DAC.
class AudioClockDrift {
  public:
    void reset() {
        anchored_ = false;
        updated_ = 0;
        ratio_ = 1;
        updates_ = 0;
    }

    void interrupt(std::int64_t now) {
        // An underrun breaks the relation between writable capacity and time.
        // A short gap does not change the oscillator in the same open sink.
        anchored_ = false;
        if (now < updated_ || now - updated_ > INT64_C(5000000000))
            reset();
    }

    void observe(std::int64_t now, std::uint64_t written, std::int64_t freeFrames,
                 unsigned sampleRate) {
        if (now < 0 || freeFrames < 0 || !sampleRate) {
            reset();
            return;
        }
        if (!anchored_) {
            anchor(now, written, freeFrames);
            return;
        }
        const std::int64_t elapsed = now - started_;
        if (elapsed < 0 || elapsed > INT64_C(5000000000) || written < written_) {
            reset();
            anchor(now, written, freeFrames);
            return;
        }
        if (elapsed < INT64_C(1000000000))
            return;
        const double consumed = double(written - written_) + double(freeFrames - free_);
        const double measured = consumed * 1e9 / (double(elapsed) * sampleRate);
        if (!std::isfinite(measured) || measured < 0.9 || measured > 1.1) {
            interrupt(now);
            anchor(now, written, freeFrames);
            return;
        }
        // One second rejects individual period boundaries. A two-second smoothing
        // time constant tracks drift without carrying backend jitter into pitch.
        const double weight = updates_ ? 1 - std::exp(-double(elapsed) / 2e9) : 1;
        ratio_ += (measured - ratio_) * weight;
        ++updates_;
        updated_ = now;
        anchor(now, written, freeFrames);
    }

    double playbackRatio(std::int64_t queuedFrames, unsigned sampleRate) const {
        if (!updates_ || queuedFrames < 0 || !sampleRate)
            return ratio_;
        // Correct queue depth on a shorter timescale than the oscillator
        // estimate. An empty queue needs enough gain to recover when that
        // estimate has briefly fallen below the rate the device consumes.
        const double errorSeconds = 0.045 - double(queuedFrames) / sampleRate;
        const double adjustment = std::clamp(errorSeconds / 0.30, -0.10, 0.10);
        return std::clamp(ratio_ + adjustment, 0.9, 1.1);
    }

    double ratio() const {
        return ratio_;
    }

    std::uint64_t updates() const {
        return updates_;
    }

  private:
    void anchor(std::int64_t now, std::uint64_t written, std::int64_t freeFrames) {
        started_ = now;
        written_ = written;
        free_ = freeFrames;
        anchored_ = true;
    }

    std::int64_t started_ = 0;
    std::int64_t updated_ = 0;
    std::uint64_t written_ = 0;
    std::int64_t free_ = 0;
    bool anchored_ = false;
    double ratio_ = 1;
    std::uint64_t updates_ = 0;
};

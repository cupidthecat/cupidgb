/**
 * @file    turbo_audio_window.cpp
 * @brief   Selection of complete host audio frames during Turbo playback.
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

#include "turbo_audio_window.h"

#include <algorithm>
#include <cstring>

bool TurboAudioWindow::setSampleRate(std::uint32_t sampleRate) {
    if (sampleRate < 8)
        return false;
    if (sampleRate == sampleRate_)
        return true;
    const std::uint32_t nextPeriod = sampleRate / 8;
    phase_ = period_ ? std::uint32_t(std::uint64_t(phase_) * nextPeriod / period_) : 0;
    sampleRate_ = sampleRate;
    period_ = nextPeriod;
    return true;
}

TurboAudioWindow::Result TurboAudioWindow::filter(char *data, std::size_t bytes,
                                                  std::size_t bytesPerFrame, bool turbo,
                                                  unsigned capQuarters,
                                                  std::size_t outputCapacityFrames) {
    if (!period_ || !bytesPerFrame || bytes % bytesPerFrame || (!data && bytes) ||
        (turbo && capQuarters && (capQuarters < 6 || capQuarters > 16)))
        return {};

    Result result;
    result.status = Status::Ok;
    result.inputFrames = bytes / bytesPerFrame;
    if (!turbo) {
        result.outputFrames = std::min(result.inputFrames, outputCapacityFrames);
        result.capacityDroppedFrames = result.inputFrames - result.outputFrames;
        return result;
    }

    // Phase zero and the upper boundary are both retained. Advance before
    // selection, so a newly constructed window begins with phase one.
    const std::uint32_t lastRetained =
        capQuarters ? std::uint32_t(std::uint64_t(period_) * 4 / capQuarters) : sampleRate_ / 16;
    if (lastRetained == period_ - 1) {
        result.outputFrames = std::min(result.inputFrames, outputCapacityFrames);
        result.capacityDroppedFrames = result.inputFrames - result.outputFrames;
        phase_ = std::uint32_t((phase_ + result.inputFrames % period_) % period_);
        return result;
    }

    std::size_t consumed = 0;
    while (consumed < result.inputFrames) {
        const std::uint32_t first = phase_ == period_ - 1 ? 0 : phase_ + 1;
        const bool selected = first <= lastRetained;
        const std::size_t span = selected ? std::size_t(lastRetained - first) + 1 : period_ - first;
        const std::size_t count = std::min(span, result.inputFrames - consumed);
        if (selected) {
            const std::size_t written = std::min(count, outputCapacityFrames - result.outputFrames);
            if (written && consumed != result.outputFrames)
                std::memmove(data + result.outputFrames * bytesPerFrame,
                             data + consumed * bytesPerFrame, written * bytesPerFrame);
            result.outputFrames += written;
            result.capacityDroppedFrames += count - written;
        } else {
            result.windowDroppedFrames += count;
        }
        phase_ = first + std::uint32_t(count - 1);
        consumed += count;
    }
    return result;
}

std::uint32_t TurboAudioWindow::sampleRate() const {
    return sampleRate_;
}

std::uint32_t TurboAudioWindow::phase() const {
    return phase_;
}

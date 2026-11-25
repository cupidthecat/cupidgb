/**
 * @file    core_audio_collector.h
 * @brief   Bounded, ordered collection of handheld and adapter playback audio.
 * @author  cupidthecat
 * @date    2026-11-17
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

#include "cupid/gb/sgb_startup_audio.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CoreAudioCollector final {
  public:
    static constexpr uint32_t maximumFrames = 16384;
    static constexpr size_t maximumSpans = 32;

    struct Span {
        CupidGbAudioOrigin origin;
        uint32_t firstFrame;
        uint32_t frames;
    };

    // One interval includes entry output, at most one final hardware step and
    // its two possible startup publications. The conservative bound is 15,000.
    bool collect(CupidGb &gb);
    static bool needsDrain(const CupidGb &gb);
    static bool discard(CupidGb &gb);
    const int16_t *samples() const;
    uint32_t frames() const;
    uint32_t frames(CupidGbAudioOrigin origin) const;
    const Span *spans() const;
    size_t spanCount() const;

  private:
    std::array<int16_t, maximumFrames * 2> samples_;
    std::array<Span, maximumSpans> spans_;
    uint32_t frames_ = 0;
    uint32_t handheldFrames_ = 0;
    uint32_t startupFrames_ = 0;
    size_t spanCount_ = 0;
};

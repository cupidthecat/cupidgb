/**
 * @file    turbo_audio_window.h
 * @brief   Turbo audio-window state, filtering results, and rate controls.
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

#include <cstddef>
#include <cstdint>
#include <limits>

/* Select complete host playback frames after format/rate conversion. The caller
 * retains the original guest and recording samples separately. Keep this object
 * alive across sink clears, normal playback, and Turbo/cap changes. */
class TurboAudioWindow {
  public:
    enum class Status { Ok, InvalidInput };

    struct Result {
        Status status = Status::InvalidInput;
        std::size_t inputFrames = 0;
        std::size_t outputFrames = 0;
        std::size_t windowDroppedFrames = 0;
        std::size_t capacityDroppedFrames = 0;
    };

    static constexpr std::size_t unlimitedFrames = (std::numeric_limits<std::size_t>::max)();

    /* The window lasts floor(sampleRate / 8) host frames. Rates below eight are
     * invalid. A device rate change maps the phase proportionally into the new
     * period, rounding down; this is a host-device transition policy. Reopening
     * the same rate preserves phase exactly. Invalid rates leave state intact. */
    bool setSampleRate(std::uint32_t sampleRate);

    /* Compact retained frames into the supplied buffer's prefix, without an
     * allocation. Never touch bytes outside that buffer. Turbo accepts cap zero
     * (uncapped) or 6..16 quarter-speed units. Normal mode bypasses the window and
     * does not advance its phase; the explicit capacity still bounds output.
     * Supply unlimitedFrames for normal playback to preserve its queue policy.
     * Excess selected frames are dropped, not deferred or charged to a future
     * call. Rejected input leaves both the bytes and phase unchanged. */
    Result filter(char *data, std::size_t bytes, std::size_t bytesPerFrame, bool turbo,
                  unsigned capQuarters, std::size_t outputCapacityFrames = unlimitedFrames);

    std::uint32_t sampleRate() const;
    std::uint32_t phase() const;

  private:
    std::uint32_t sampleRate_ = 0;
    std::uint32_t period_ = 0;
    std::uint32_t phase_ = 0;
};

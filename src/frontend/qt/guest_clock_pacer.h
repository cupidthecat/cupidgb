/**
 * @file    guest_clock_pacer.h
 * @brief   Guest-clock pacing state and playback-rate interface.
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

#include <cstdint>
#include <optional>

/* Convert consumed base-speed guest T-cycles into host time. LCD completions
 * can cover partial frames, so presentation counts are not clock units. */
class GuestClockPacer {
  public:
    static constexpr std::uint64_t baseClockHz = 4194304;
    static constexpr unsigned normalSpeedSteps = 16;
    static constexpr unsigned normalTurboQuarters = 4;

    void reset();

    /* Rates 8..16 represent half through normal speed. Turbo quarters are 4
     * without turbo, or 6..16 for caps from 150% through 400%. Multiply both
     * rates before division so combined slow motion and turbo stay exact.
     * Stable-rate calls carry
     * the fractional nanosecond forward exactly. A rate change discards only
     * that fraction, as does a clock change; reset also clears it after a host
     * timeline discontinuity. Zero clocks, invalid rates, denominator overflow
     * or an unrepresentable signed duration leave state intact.
     * The caller separately bounds addition to its absolute host deadline. */
    std::optional<std::int64_t> advance(std::uint64_t baseTCycles, unsigned speedSteps,
                                        std::uint64_t clockHz = baseClockHz,
                                        unsigned turboQuarters = normalTurboQuarters);

  private:
    std::uint64_t remainder_ = 0;
    unsigned speedSteps_ = 0;
    unsigned turboQuarters_ = 0;
    std::uint64_t clockHz_ = 0;
};

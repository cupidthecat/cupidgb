/**
 * @file    guest_clock_pacer.cpp
 * @brief   Guest-clock conversion to host deadlines with fractional timing carry.
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

#include "guest_clock_pacer.h"

#include <limits>

namespace {

// Both summands are below the denominator. Compare before adding so even a
// denominator near the unsigned limit keeps the residue representable.
bool addResidue(std::uint64_t &residue, std::uint64_t addend, std::uint64_t denominator) {
    if (residue >= denominator - addend) {
        residue -= denominator - addend;
        return true;
    }
    residue += addend;
    return false;
}

std::uint64_t scaledFraction(std::uint64_t residue, std::uint64_t multiplier, std::uint64_t carry,
                             std::uint64_t denominator, std::uint64_t &nextCarry) {
    if (residue <= (std::numeric_limits<std::uint64_t>::max() - carry) / multiplier) {
        const auto fraction = residue * multiplier + carry;
        nextCarry = fraction % denominator;
        return fraction / denominator;
    }

    // Unusually large clocks may overflow the intermediate product despite a
    // small final duration. Long multiplication keeps only quotient and residue.
    std::uint64_t quotient = 0;
    nextCarry = 0;
    for (unsigned bit = 64; bit-- > 0;) {
        quotient = quotient * 2 + addResidue(nextCarry, nextCarry, denominator);
        if ((multiplier >> bit) & 1)
            quotient += addResidue(nextCarry, residue, denominator);
    }
    quotient += addResidue(nextCarry, carry, denominator);
    return quotient;
}

} // namespace

void GuestClockPacer::reset() {
    remainder_ = 0;
    speedSteps_ = 0;
    turboQuarters_ = 0;
    clockHz_ = 0;
}

std::optional<std::int64_t> GuestClockPacer::advance(std::uint64_t baseTCycles, unsigned speedSteps,
                                                     std::uint64_t clockHz,
                                                     unsigned turboQuarters) {
    if (speedSteps < 8 || speedSteps > normalSpeedSteps || clockHz == 0 ||
        (turboQuarters != normalTurboQuarters && (turboQuarters < 6 || turboQuarters > 16)))
        return std::nullopt;

    // Cancel the quarter denominator first. Normal-speed calls retain their
    // previous wide-clock range, while fractional turbo caps retain all bits.
    const unsigned divisor = turboQuarters % 4 == 0 ? 4 : turboQuarters % 2 == 0 ? 2 : 1;
    const unsigned rate = speedSteps * (turboQuarters / divisor);
    if (clockHz > std::numeric_limits<std::uint64_t>::max() / rate)
        return std::nullopt;
    const std::uint64_t numerator =
        UINT64_C(1000000000) * normalSpeedSteps * (normalTurboQuarters / divisor);
    constexpr std::uint64_t maximum = std::uint64_t(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t denominator = clockHz * rate;
    const std::uint64_t carry =
        speedSteps == speedSteps_ && clockHz == clockHz_ && turboQuarters == turboQuarters_
            ? remainder_
            : 0;

    // Decompose before multiplication so large caller deltas cannot wrap.
    const std::uint64_t whole = baseTCycles / denominator;
    if (whole > maximum / numerator)
        return std::nullopt;
    const std::uint64_t coarseDuration = whole * numerator;
    std::uint64_t nextCarry;
    const std::uint64_t fineDuration =
        scaledFraction(baseTCycles % denominator, numerator, carry, denominator, nextCarry);
    if (fineDuration > maximum - coarseDuration)
        return std::nullopt;

    remainder_ = nextCarry;
    speedSteps_ = speedSteps;
    turboQuarters_ = turboQuarters;
    clockHz_ = clockHz;
    return std::int64_t(coarseDuration + fineDuration);
}

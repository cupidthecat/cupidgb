#include "guest_clock_pacer.h"

#include <limits>

void GuestClockPacer::reset() {
    remainder_ = 0;
    speedSteps_ = 0;
}

std::optional<std::int64_t> GuestClockPacer::advance(std::uint64_t baseTCycles,
                                                     unsigned speedSteps) {
    if (speedSteps < 8 || speedSteps > normalSpeedSteps)
        return std::nullopt;

    constexpr std::uint64_t numerator = UINT64_C(1000000000) * normalSpeedSteps;
    constexpr std::uint64_t maximum = std::uint64_t(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t denominator = baseClockHz * speedSteps;
    const std::uint64_t carry = speedSteps == speedSteps_ ? remainder_ : 0;

    // Decompose before multiplication so large caller deltas cannot wrap.
    // The residue is below 2^26 cycles, keeping its product below 2^60.
    const std::uint64_t whole = baseTCycles / denominator;
    if (whole > maximum / numerator)
        return std::nullopt;
    const std::uint64_t fraction = (baseTCycles % denominator) * numerator + carry;
    const std::uint64_t coarseDuration = whole * numerator;
    const std::uint64_t fineDuration = fraction / denominator;
    if (fineDuration > maximum - coarseDuration)
        return std::nullopt;

    remainder_ = fraction % denominator;
    speedSteps_ = speedSteps;
    return std::int64_t(coarseDuration + fineDuration);
}

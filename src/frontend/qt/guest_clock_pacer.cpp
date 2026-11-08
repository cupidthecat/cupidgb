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
    clockHz_ = 0;
}

std::optional<std::int64_t> GuestClockPacer::advance(std::uint64_t baseTCycles, unsigned speedSteps,
                                                     std::uint64_t clockHz) {
    if (speedSteps < 8 || speedSteps > normalSpeedSteps || clockHz == 0 ||
        clockHz > std::numeric_limits<std::uint64_t>::max() / speedSteps)
        return std::nullopt;

    constexpr std::uint64_t numerator = UINT64_C(1000000000) * normalSpeedSteps;
    constexpr std::uint64_t maximum = std::uint64_t(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t denominator = clockHz * speedSteps;
    const std::uint64_t carry = speedSteps == speedSteps_ && clockHz == clockHz_ ? remainder_ : 0;

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
    clockHz_ = clockHz;
    return std::int64_t(coarseDuration + fineDuration);
}

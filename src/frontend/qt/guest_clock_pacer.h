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

#pragma once

#include <cstdint>
#include <optional>

/* Convert consumed base-speed guest T-cycles into host time. LCD completions
 * can cover partial frames, so presentation counts are not clock units. */
class GuestClockPacer {
  public:
    static constexpr std::uint64_t baseClockHz = 4194304;
    static constexpr unsigned normalSpeedSteps = 16;

    void reset();

    /* Rates 8..16 represent half through normal speed. Stable-rate calls carry
     * the fractional nanosecond forward exactly. A rate change discards only
     * that fraction; reset also clears it after a host timeline discontinuity.
     * Invalid rates or an unrepresentable signed duration leave state intact.
     * The caller separately bounds addition to its absolute host deadline. */
    std::optional<std::int64_t> advance(std::uint64_t baseTCycles, unsigned speedSteps);

  private:
    std::uint64_t remainder_ = 0;
    unsigned speedSteps_ = 0;
};

#include "guest_clock_pacer.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

static std::int64_t advance(GuestClockPacer &pacer, std::uint64_t cycles, unsigned steps = 16,
                            std::uint64_t clockHz = GuestClockPacer::baseClockHz) {
    const auto duration = pacer.advance(cycles, steps, clockHz);
    assert(duration);
    return *duration;
}

static void frame_and_partial_intervals() {
    GuestClockPacer frames;
    std::int64_t elapsed = 0;
    for (unsigned frame = 0; frame < 512; ++frame)
        elapsed += advance(frames, 70224);
    assert(elapsed == INT64_C(8572265625));

    GuestClockPacer complete, split;
    for (unsigned frame = 0; frame < 512; ++frame) {
        // A first VBlank or LCD power transition need not consume a full frame.
        const auto full = advance(complete, 70224);
        const auto partial = advance(split, 65664) + advance(split, 4560);
        assert(full == partial);
    }
    GuestClockPacer mixed;
    const std::uint64_t intervals[] = {70224, 4560, 65664, 2, 2, 70224, 100, 4, 70000};
    std::uint64_t cycles = 0;
    elapsed = 0;
    for (const auto interval : intervals) {
        cycles += interval;
        elapsed += advance(mixed, interval);
    }
    assert(elapsed == std::int64_t(cycles * UINT64_C(1000000000) / GuestClockPacer::baseClockHz));
}

static void exact_clock_and_double_speed_granularity() {
    for (const unsigned chunk : {1u, 2u, 4u}) {
        GuestClockPacer pacer;
        std::int64_t elapsed = 0;
        for (std::uint64_t cycles = 0; cycles < GuestClockPacer::baseClockHz; cycles += chunk)
            elapsed += advance(pacer, chunk);
        assert(elapsed == INT64_C(1000000000));
    }
    // Every supported slow-motion rate reaches an exact common clock horizon.
    for (unsigned steps = 8; steps <= 16; ++steps) {
        GuestClockPacer pacer;
        std::int64_t elapsed = 0;
        for (unsigned second = 0; second < steps; ++second)
            elapsed += advance(pacer, GuestClockPacer::baseClockHz, steps);
        assert(elapsed == INT64_C(16000000000));
    }
}

static void long_running_variable_chunks() {
    GuestClockPacer pacer;
    std::uint32_t sequence = 12345;
    std::uint64_t cycles = 0;
    std::int64_t elapsed = 0;
    for (unsigned interval = 0; interval < 100000; ++interval) {
        sequence = sequence * 1664525u + 1013904223u;
        const std::uint64_t delta = sequence % 10000u;
        cycles += delta;
        elapsed += advance(pacer, delta);
        assert(elapsed ==
               std::int64_t(cycles * UINT64_C(1000000000) / GuestClockPacer::baseClockHz));
    }
}

static void reset_and_rate_changes() {
    GuestClockPacer pacer;
    assert(advance(pacer, 1) == 238);
    assert(advance(pacer, 1) == 238);
    assert(advance(pacer, 1) == 239);
    pacer.reset();
    assert(advance(pacer, 1) == 238);
    assert(advance(pacer, 1, 8) == 476);
    assert(advance(pacer, 1, 8) == 477);
    assert(advance(pacer, 0, 16) == 0);
    assert(advance(pacer, 1, 16) == 238);
    assert(advance(pacer, 0, 16) == 0);
    assert(advance(pacer, 1, 16) == 238);
    assert(advance(pacer, 1, 16) == 239);
}

static void invalid_inputs_preserve_carry() {
    GuestClockPacer pacer;
    assert(advance(pacer, 1) == 238);
    assert(advance(pacer, 1) == 238);
    assert(!pacer.advance(1, 0));
    assert(!pacer.advance(1, 7));
    assert(!pacer.advance(1, 17));
    assert(!pacer.advance(1, 16, 0));
    assert(!pacer.advance(1, 16, std::numeric_limits<std::uint64_t>::max()));
    assert(!pacer.advance(std::numeric_limits<std::uint64_t>::max(), 16));
    assert(advance(pacer, 1) == 239);

    for (const unsigned steps : {8u, 16u}) {
        pacer.reset();
        const std::uint64_t secondDuration = UINT64_C(16000000000) / steps;
        const std::uint64_t seconds =
            std::uint64_t(std::numeric_limits<std::int64_t>::max()) / secondDuration;
        assert(advance(pacer, GuestClockPacer::baseClockHz * seconds, steps) ==
               std::int64_t(seconds * secondDuration));
        assert(!pacer.advance(GuestClockPacer::baseClockHz * (seconds + 1), steps));
    }
}

static void model_clocks_and_switches() {
    constexpr std::uint64_t adapterClockHz = 4295454;
    for (unsigned steps = 8; steps <= 16; ++steps) {
        GuestClockPacer pacer;
        std::int64_t elapsed = 0;
        for (unsigned second = 0; second < steps; ++second)
            elapsed += advance(pacer, adapterClockHz, steps, adapterClockHz);
        assert(elapsed == INT64_C(16000000000));
    }

    GuestClockPacer pacer;
    std::uint64_t cycles = 0;
    std::int64_t elapsed = 0;
    for (unsigned frame = 0; frame < 10000; ++frame) {
        cycles += 70224;
        elapsed += advance(pacer, 65664, 16, adapterClockHz);
        elapsed += advance(pacer, 4560, 16, adapterClockHz);
        assert(elapsed == std::int64_t(cycles * UINT64_C(1000000000) / adapterClockHz));
    }

    pacer.reset();
    assert(advance(pacer, 1, 16, adapterClockHz) == 232);
    assert(advance(pacer, 1, 16, adapterClockHz) == 233);
    assert(advance(pacer, 1) == 238);
    assert(advance(pacer, 1, 16, adapterClockHz) == 232);
    assert(advance(pacer, 0) == 0);
    assert(advance(pacer, 1) == 238);
    assert(advance(pacer, 1) == 238);
    // A rejected call with another clock must not clear the pending fraction.
    assert(!pacer.advance(std::numeric_limits<std::uint64_t>::max(), 16, adapterClockHz));
    assert(advance(pacer, 1) == 239);
}

static void wide_clock_arithmetic() {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    constexpr auto clockHz = maximum / 16;
    constexpr auto denominator = clockHz * 16;
    GuestClockPacer pacer;
    // Exercise the large-residue path without overflowing the exact quotient.
    assert(advance(pacer, denominator - 1, 16, clockHz) == INT64_C(15999999999));
    assert(advance(pacer, 1, 16, clockHz) == 1);
    assert(advance(pacer, denominator, 16, clockHz) == INT64_C(16000000000));
    pacer.reset();
    assert(advance(pacer, maximum, 16, clockHz) == INT64_C(16000000000));
    assert(!pacer.advance(1, 16, clockHz + 1));

    pacer.reset();
    assert(advance(pacer, 1, 16, 1) == INT64_C(1000000000));
    assert(!pacer.advance(maximum, 16, 1));
    assert(advance(pacer, 1, 8, 1) == INT64_C(2000000000));

    constexpr auto signedMaximum = std::numeric_limits<std::int64_t>::max();
    pacer.reset();
    assert(advance(pacer, std::uint64_t(signedMaximum), 16, UINT64_C(1000000000)) == signedMaximum);
    assert(!pacer.advance(std::uint64_t(signedMaximum) + 1, 16, UINT64_C(1000000000)));
}

int main() {
    frame_and_partial_intervals();
    exact_clock_and_double_speed_granularity();
    long_running_variable_chunks();
    reset_and_rate_changes();
    invalid_inputs_preserve_carry();
    model_clocks_and_switches();
    wide_clock_arithmetic();
    std::cout << "Guest-clock pacing, fractional carry and rate changes passed.\n";
}

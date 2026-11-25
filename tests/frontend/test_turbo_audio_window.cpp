/**
 * @file    test_turbo_audio_window.cpp
 * @brief   Regression tests for Turbo audio selection and capacity accounting.
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
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using Window = TurboAudioWindow;

constexpr std::array<unsigned, 12> caps = {0, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
std::uint64_t checkedFrames = 0;
std::uint64_t checkedBlocks = 0;

struct Expected {
    std::vector<char> bytes;
    std::size_t windowDropped = 0;
    std::size_t capacityDropped = 0;
};

/* The oracle visits every frame and uses integer inequalities. The production
 * helper instead copies spans between window edges. */
struct Oracle {
    std::uint32_t rate = 0;
    std::uint32_t position = 0;

    void rateChanged(std::uint32_t next) {
        position = rate ? std::uint32_t(std::uint64_t(position) * (next / 8) / (rate / 8)) : 0;
        rate = next;
    }

    Expected filter(const std::vector<char> &input, std::size_t frameBytes, bool turbo,
                    unsigned cap, std::size_t capacity) {
        Expected expected;
        const std::size_t frames = input.size() / frameBytes;
        std::size_t retained = 0;
        for (std::size_t i = 0; i < frames; ++i) {
            bool selected = true;
            if (turbo) {
                position = (position + 1) % (rate / 8);
                selected = cap ? std::uint64_t(position) * cap <= std::uint64_t(rate / 8) * 4
                               : std::uint64_t(position) * 16 <= rate;
            }
            if (!selected) {
                ++expected.windowDropped;
            } else if (retained == capacity) {
                ++expected.capacityDropped;
            } else {
                expected.bytes.insert(expected.bytes.end(), input.data() + i * frameBytes,
                                      input.data() + (i + 1) * frameBytes);
                ++retained;
            }
        }
        return expected;
    }
};

std::vector<char> frames(std::size_t count, std::size_t frameBytes, std::uint64_t first = 0) {
    std::vector<char> result(count * frameBytes);
    for (std::size_t frame = 0; frame < count; ++frame) {
        std::uint64_t value = (first + frame + 1) * UINT64_C(0x9e3779b97f4a7c15);
        for (std::size_t byte = 0; byte < frameBytes; ++byte) {
            value ^= value >> 12;
            value ^= value << 25;
            value ^= value >> 27;
            result[frame * frameBytes + byte] = char((value >> 29) & 0xff);
        }
    }
    return result;
}

std::vector<char> verify(Window &window, Oracle &oracle, const std::vector<char> &input,
                         std::size_t frameBytes, bool turbo, unsigned cap,
                         std::size_t capacity = Window::unlimitedFrames) {
    constexpr std::size_t guard = 37;
    std::vector<char> storage(input.size() + guard * 2, 'Z');
    std::copy(input.begin(), input.end(), storage.begin() + guard);
    const auto expected = oracle.filter(input, frameBytes, turbo, cap, capacity);
    const auto result =
        window.filter(storage.data() + guard, input.size(), frameBytes, turbo, cap, capacity);
    assert(result.status == Window::Status::Ok);
    assert(result.inputFrames == input.size() / frameBytes);
    assert(result.outputFrames == expected.bytes.size() / frameBytes);
    assert(result.windowDroppedFrames == expected.windowDropped);
    assert(result.capacityDroppedFrames == expected.capacityDropped);
    assert(result.inputFrames ==
           result.outputFrames + result.windowDroppedFrames + result.capacityDroppedFrames);
    assert(result.outputFrames <= capacity);
    assert(window.phase() == oracle.position);
    assert(window.sampleRate() == oracle.rate);
    assert(std::equal(expected.bytes.begin(), expected.bytes.end(), storage.begin() + guard));
    assert(std::all_of(storage.begin(), storage.begin() + guard,
                       [](char value) { return value == 'Z'; }));
    assert(
        std::all_of(storage.end() - guard, storage.end(), [](char value) { return value == 'Z'; }));
    checkedFrames += result.inputFrames;
    ++checkedBlocks;
    return expected.bytes;
}

void formats_rates_caps_and_partitions() {
    // UInt8 mono/stereo, Int16 stereo, Float/Int32 stereo, and surround frames.
    for (const std::size_t frameBytes : {1u, 2u, 4u, 8u, 24u, 32u}) {
        for (const std::uint32_t rate : {44100u, 48000u, 96000u}) {
            for (const unsigned cap : caps) {
                Window whole, split;
                Oracle wholeOracle, splitOracle;
                assert(whole.setSampleRate(rate) && split.setSampleRate(rate));
                wholeOracle.rateChanged(rate);
                splitOracle.rateChanged(rate);
                const auto input = frames(std::size_t(rate / 8) * 3 + 511, frameBytes);
                const auto expected = verify(whole, wholeOracle, input, frameBytes, true, cap);
                std::vector<char> combined;
                std::size_t offset = 0;
                std::uint32_t sequence = 17;
                while (offset < input.size() / frameBytes) {
                    sequence = sequence * 1664525u + 1013904223u;
                    const std::size_t count = std::min(std::size_t(sequence % 997 + 1),
                                                       input.size() / frameBytes - offset);
                    const std::vector<char> block(input.data() + offset * frameBytes,
                                                  input.data() + (offset + count) * frameBytes);
                    const auto output = verify(split, splitOracle, block, frameBytes, true, cap);
                    combined.insert(combined.end(), output.begin(), output.end());
                    offset += count;
                }
                assert(combined == expected);
                assert(split.phase() == whole.phase());
            }
        }
    }
}

void inclusive_edges_and_wrap() {
    for (const unsigned cap : caps) {
        Window window;
        Oracle oracle;
        assert(window.setSampleRate(48000));
        oracle.rateChanged(48000);
        const std::size_t upper = cap ? 24000 / cap : 3000;
        assert(verify(window, oracle, frames(upper - 1, 4), 4, true, cap).size() ==
               (upper - 1) * 4);
        assert(verify(window, oracle, frames(1, 4), 4, true, cap).size() == 4);
        assert(window.phase() == upper);
        assert(verify(window, oracle, frames(1, 4), 4, true, cap).empty());
        assert(verify(window, oracle, frames(5999 - upper - 1, 4), 4, true, cap).empty());
        assert(window.phase() == 5999);
        assert(verify(window, oracle, frames(1, 4), 4, true, cap).size() == 4);
        assert(window.phase() == 0);
        assert(verify(window, oracle, frames(6000, 4), 4, true, cap).size() == (upper + 1) * 4);
    }
}

void capacity_drops_still_advance_phase() {
    for (const std::uint32_t rate : {44100u, 48000u, 96000u}) {
        for (const unsigned cap : caps) {
            const auto input = frames(rate / 8 * 2 + 53, 8);
            for (const std::size_t capacity : {std::size_t(0), std::size_t(1), std::size_t(31),
                                               std::size_t(rate / 16), input.size() / 8}) {
                Window window;
                Oracle oracle;
                assert(window.setSampleRate(rate));
                oracle.rateChanged(rate);
                verify(window, oracle, input, 8, true, cap, capacity);
                verify(window, oracle, frames(997, 8, 0x10000), 8, true, cap, 113);
            }
        }
    }
}

void toggles_caps_and_normal_bypass() {
    Window window;
    Oracle oracle;
    assert(window.setSampleRate(44100));
    oracle.rateChanged(44100);
    std::uint64_t first = 0;
    for (unsigned round = 0; round < 48; ++round) {
        const unsigned cap = caps[round % caps.size()];
        const auto count = std::size_t(157 + round * 31);
        verify(window, oracle, frames(count, 4, first), 4, true, cap, round % 2 ? 71 : count);
        first += count;
        const auto phase = window.phase();
        const auto normal = frames(173, 8, first);
        const auto output = verify(window, oracle, normal, 8, false, 99);
        assert(output == normal);
        assert(window.phase() == phase);
        assert(window.setSampleRate(44100));
        assert(window.phase() == phase);
        first += 173;
    }
    const auto phase = window.phase();
    verify(window, oracle, frames(29, 2), 2, false, 4, 7);
    assert(window.phase() == phase);
}

void device_rate_changes() {
    Window window;
    Oracle oracle;
    assert(window.setSampleRate(44100));
    oracle.rateChanged(44100);
    verify(window, oracle, frames(4321, 4), 4, true, 16, 0);
    assert(window.phase() == 4321);
    for (const std::uint32_t rate :
         {48000u, 96000u, 22050u, 8000u, 8u, UINT32_MAX, 44100u, 44101u}) {
        const auto oldPhase = window.phase();
        const auto oldPeriod = window.sampleRate() / 8;
        const auto expected = std::uint32_t(std::uint64_t(oldPhase) * (rate / 8) / oldPeriod);
        assert(window.setSampleRate(rate));
        oracle.rateChanged(rate);
        assert(window.phase() == expected);
        assert(window.phase() < rate / 8);
        verify(window, oracle, frames(37, 8), 8, true, 7, 11);
        const auto phase = window.phase();
        assert(window.setSampleRate(rate));
        assert(window.phase() == phase);
        assert(!window.setSampleRate(0) && !window.setSampleRate(7));
        assert(window.sampleRate() == rate && window.phase() == phase);
    }
}

void small_periods() {
    for (std::uint32_t rate = 8; rate <= 40; ++rate) {
        for (const unsigned cap : caps) {
            Window window;
            Oracle oracle;
            assert(window.setSampleRate(rate));
            oracle.rateChanged(rate);
            verify(window, oracle, frames(197, 3), 3, true, cap, 23);
            verify(window, oracle, frames(19, 3), 3, true, cap);
        }
    }
}

void invalid_inputs_and_empty_blocks() {
    Window window;
    auto input = frames(19, 4);
    const auto before = input;
    auto invalid = [&](char *data, std::size_t bytes, std::size_t frameBytes, bool turbo,
                       unsigned cap) {
        const auto phase = window.phase();
        const auto result = window.filter(data, bytes, frameBytes, turbo, cap);
        assert(result.status == Window::Status::InvalidInput);
        assert(!result.inputFrames && !result.outputFrames && !result.windowDroppedFrames &&
               !result.capacityDroppedFrames);
        assert(input == before && window.phase() == phase);
    };
    invalid(input.data(), input.size(), 4, true, 8);
    assert(!window.setSampleRate(0));
    assert(window.setSampleRate(48000));
    assert(window.filter(input.data(), 4, 4, true, 8).status == Window::Status::Ok);
    assert(window.phase() == 1);
    invalid(input.data(), input.size(), 0, true, 8);
    invalid(input.data(), input.size() - 1, 4, true, 8);
    invalid(nullptr, input.size(), 4, true, 8);
    for (const unsigned cap : {1u, 4u, 5u, 17u, UINT32_MAX})
        invalid(input.data(), input.size(), 4, true, cap);
    const auto result = window.filter(nullptr, 0, 8, true, 8, 0);
    assert(result.status == Window::Status::Ok);
    assert(!result.inputFrames && !result.outputFrames && !result.windowDroppedFrames &&
           !result.capacityDroppedFrames && window.phase() == 1);
}
} // namespace

int main() {
    formats_rates_caps_and_partitions();
    inclusive_edges_and_wrap();
    capacity_drops_still_advance_phase();
    toggles_caps_and_normal_bypass();
    device_rate_changes();
    small_periods();
    invalid_inputs_and_empty_blocks();
    std::cout << "CupidGB turbo audio window regression passed: " << checkedFrames << " frames in "
              << checkedBlocks << " blocks\n";
    return 0;
}

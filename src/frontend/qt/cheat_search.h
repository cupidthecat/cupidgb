/**
 * @file    cheat_search.h
 * @brief   Physical-RAM search types, candidates, and comparison interface.
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
#include <vector>

extern "C" {
#include "cupid/gb/gb.h"
}

enum class CheatSearchType { Byte, WordLittleEndian, WordBigEndian };
enum class CheatSearchCondition {
    Any,
    Equal,
    NotEqual,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual,
    Changed,
    Unchanged,
    Increased,
    Decreased
};

struct CheatSearchResult {
    uint16_t address;
    uint16_t bank;
    uint16_t value;
    size_t offset;
};

class CheatSearch final {
  public:
    void reset();
    void filter(const CupidGb &gb, CheatSearchType type, CheatSearchCondition condition,
                uint16_t operand);
    size_t count() const;
    CheatSearchResult result(size_t index) const;
    CheatSearchResult byteResult(size_t index, unsigned byte) const;
    uint16_t currentValue(const CupidGb &gb, size_t index) const;
    bool writeValue(CupidGb &gb, size_t index, uint16_t value) const;
    CheatSearchType type() const;

  private:
    const uint8_t *address(const CupidGb &gb, size_t offset) const;
    uint16_t value(const uint8_t *data) const;
    CheatSearchResult location(size_t offset) const;
    bool matches(uint16_t previous, uint16_t current, CheatSearchCondition condition,
                 uint16_t operand) const;

    CheatSearchType type_ = CheatSearchType::Byte;
    size_t workRamSize_ = 0;
    size_t cartridgeRamSize_ = 0;
    std::vector<uint8_t> previous_;
    std::vector<size_t> candidates_;
};

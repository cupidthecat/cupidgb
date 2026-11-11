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

#include "cheat_search.h"

#include <cstring>
#include <stdexcept>

void CheatSearch::reset() {
    previous_.clear();
    candidates_.clear();
}

void CheatSearch::filter(const CupidGb &gb, CheatSearchType type, CheatSearchCondition condition,
                         uint16_t operand) {
    const size_t workSize = gb.model == CUPID_GB_MODEL_CGB ? 0x8000 : 0x2000;
    if (candidates_.empty() || workSize != workRamSize_ ||
        gb.cartridge_ram_size != cartridgeRamSize_) {
        reset();
        workRamSize_ = workSize;
        cartridgeRamSize_ = gb.cartridge_ram_size;
        previous_.resize(workSize + cartridgeRamSize_ + sizeof(gb.high_ram));
        for (size_t offset = 0; offset < previous_.size(); ++offset)
            candidates_.push_back(offset);
    }
    type_ = type;
    size_t retained = 0;
    for (size_t offset : candidates_) {
        // Words may cross a physical bank boundary, but never the end of a RAM region.
        if (type != CheatSearchType::Byte &&
            (offset + 1 == workSize || offset + 1 == workSize + cartridgeRamSize_ ||
             offset + 1 == previous_.size()))
            continue;
        if (matches(value(previous_.data() + offset), value(address(gb, offset)), condition,
                    operand))
            candidates_[retained++] = offset;
    }
    candidates_.resize(retained);
    std::memcpy(previous_.data(), gb.work_ram, workRamSize_);
    if (cartridgeRamSize_)
        std::memcpy(previous_.data() + workRamSize_, gb.cartridge_ram, cartridgeRamSize_);
    std::memcpy(previous_.data() + workRamSize_ + cartridgeRamSize_, gb.high_ram,
                sizeof(gb.high_ram));
}

size_t CheatSearch::count() const {
    return candidates_.size();
}

CheatSearchResult CheatSearch::result(size_t index) const {
    const size_t offset = candidates_.at(index);
    auto result = location(offset);
    result.value = value(previous_.data() + offset);
    return result;
}

CheatSearchResult CheatSearch::byteResult(size_t index, unsigned byte) const {
    if (byte > unsigned(type_ != CheatSearchType::Byte))
        throw std::out_of_range("Cheat search byte index");
    return location(candidates_.at(index) + byte);
}

CheatSearchResult CheatSearch::location(size_t offset) const {
    uint16_t bank = 0, mapped = 0;
    if (offset < workRamSize_) {
        bank = uint16_t(offset / 0x1000);
        mapped = uint16_t((bank ? 0xd000 : 0xc000) + (offset & 0xfff));
    } else if (offset < workRamSize_ + cartridgeRamSize_) {
        bank = uint16_t((offset - workRamSize_) / 0x2000);
        mapped = uint16_t(0xa000 + ((offset - workRamSize_) & 0x1fff));
    } else {
        mapped = uint16_t(0xff80 + offset - workRamSize_ - cartridgeRamSize_);
    }
    return {mapped, bank, previous_.at(offset), offset};
}

const uint8_t *CheatSearch::address(const CupidGb &gb, size_t offset) const {
    if (offset < workRamSize_)
        return gb.work_ram + offset;
    offset -= workRamSize_;
    if (offset < cartridgeRamSize_)
        return gb.cartridge_ram + offset;
    return gb.high_ram + offset - cartridgeRamSize_;
}

uint16_t CheatSearch::value(const uint8_t *data) const {
    if (type_ == CheatSearchType::Byte)
        return data[0];
    if (type_ == CheatSearchType::WordBigEndian)
        return uint16_t(uint16_t(data[0]) << 8 | data[1]);
    return uint16_t(data[0] | uint16_t(data[1]) << 8);
}

uint16_t CheatSearch::currentValue(const CupidGb &gb, size_t index) const {
    return value(address(gb, candidates_.at(index)));
}

bool CheatSearch::writeValue(CupidGb &gb, size_t index, uint16_t number) const {
    if (index >= candidates_.size() ||
        workRamSize_ != (gb.model == CUPID_GB_MODEL_CGB ? 0x8000u : 0x2000u) ||
        cartridgeRamSize_ != gb.cartridge_ram_size)
        return false;
    auto *destination = const_cast<uint8_t *>(address(gb, candidates_[index]));
    const uint16_t encoded =
        type_ == CheatSearchType::WordBigEndian ? uint16_t(number >> 8 | number << 8) : number;
    destination[0] = uint8_t(encoded);
    if (type_ != CheatSearchType::Byte)
        destination[1] = uint8_t(encoded >> 8);
    if (candidates_[index] >= workRamSize_ && candidates_[index] < workRamSize_ + cartridgeRamSize_)
        ++gb.battery_write_generation;
    return true;
}

CheatSearchType CheatSearch::type() const {
    return type_;
}

bool CheatSearch::matches(uint16_t previous, uint16_t current, CheatSearchCondition condition,
                          uint16_t operand) const {
    switch (condition) {
        case CheatSearchCondition::Any:
            return true;
        case CheatSearchCondition::Equal:
            return current == operand;
        case CheatSearchCondition::NotEqual:
            return current != operand;
        case CheatSearchCondition::Greater:
            return current > operand;
        case CheatSearchCondition::GreaterOrEqual:
            return current >= operand;
        case CheatSearchCondition::Less:
            return current < operand;
        case CheatSearchCondition::LessOrEqual:
            return current <= operand;
        case CheatSearchCondition::Changed:
            return current != previous;
        case CheatSearchCondition::Unchanged:
            return current == previous;
        case CheatSearchCondition::Increased:
            return current > previous;
        case CheatSearchCondition::Decreased:
            return current < previous;
    }
    return false;
}

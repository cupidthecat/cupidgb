#include "cheat_search.h"

#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
std::unique_ptr<CupidGb> machine(bool color, size_t cartridgeSize = 0) {
    auto gb = std::make_unique<CupidGb>();
    gb->model = color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
    gb->cartridge_ram_size = cartridgeSize;
    gb->header.has_battery = cartridgeSize != 0;
    gb->current_ram_bank = 1;
    gb->cgb_wram_bank = 1;
    return gb;
}

bool word(CheatSearchType type) {
    return type != CheatSearchType::Byte;
}

void store(uint8_t *bytes, CheatSearchType type, uint16_t number) {
    if (type == CheatSearchType::WordBigEndian) {
        bytes[0] = uint8_t(number >> 8);
        bytes[1] = uint8_t(number);
    } else {
        bytes[0] = uint8_t(number);
        if (word(type))
            bytes[1] = uint8_t(number >> 8);
    }
}

CheatSearchResult atOffset(const CheatSearch &search, size_t offset) {
    for (size_t index = 0; index < search.count(); ++index) {
        if (search.result(index).offset == offset)
            return search.result(index);
    }
    assert(false);
    return {};
}

size_t indexOf(const CheatSearch &search, size_t offset) {
    for (size_t index = 0; index < search.count(); ++index) {
        if (search.result(index).offset == offset)
            return index;
    }
    assert(false);
    return 0;
}

void testConditions() {
    struct Case {
        CheatSearchCondition condition;
        std::vector<unsigned> retained;
    };

    const std::array<Case, 11> cases = {{
        {CheatSearchCondition::Any, {0, 1, 2, 3, 4, 5, 6, 7}},
        {CheatSearchCondition::Equal, {3}},
        {CheatSearchCondition::NotEqual, {0, 1, 2, 4, 5, 6, 7}},
        {CheatSearchCondition::Greater, {4, 5, 6, 7}},
        {CheatSearchCondition::GreaterOrEqual, {3, 4, 5, 6, 7}},
        {CheatSearchCondition::Less, {0, 1, 2}},
        {CheatSearchCondition::LessOrEqual, {0, 1, 2, 3}},
        {CheatSearchCondition::Changed, {0, 1, 2, 4, 5, 6, 7}},
        {CheatSearchCondition::Unchanged, {3}},
        {CheatSearchCondition::Increased, {4, 5, 6, 7}},
        {CheatSearchCondition::Decreased, {0, 1, 2}},
    }};
    for (auto type : {CheatSearchType::Byte, CheatSearchType::WordLittleEndian,
                      CheatSearchType::WordBigEndian}) {
        const uint16_t marker = word(type) ? 0x5555 : 0x55;
        const std::array<uint16_t, 8> values = {0,
                                                1,
                                                uint16_t(marker - 1),
                                                marker,
                                                uint16_t(marker + 1),
                                                uint16_t(word(type) ? 0x8000 : 0x80),
                                                uint16_t(word(type) ? 0xfffe : 0xfe),
                                                uint16_t(word(type) ? 0xffff : 0xff)};
        for (const auto &test : cases) {
            auto gb = machine(true);
            CheatSearch search;
            for (unsigned i = 0; i < values.size(); ++i)
                store(gb->work_ram + 8 + 8 * i, type, marker);
            search.filter(*gb, type, CheatSearchCondition::Equal, marker);
            assert(search.count() == values.size());
            for (unsigned i = 0; i < values.size(); ++i)
                store(gb->work_ram + 8 + 8 * i, type, values[i]);
            search.filter(*gb, type, test.condition, marker);
            assert(search.count() == test.retained.size());
            for (size_t i = 0; i < test.retained.size(); ++i) {
                const auto result = search.result(i);
                assert(result.offset == 8 + 8 * test.retained[i]);
                assert(result.address == 0xc008 + 8 * test.retained[i] && result.bank == 0);
                assert(result.value == values[test.retained[i]]);
            }
        }
    }
}

void testPhysicalEnumeration() {
    for (bool color : {false, true}) {
        for (size_t cartridgeSize : {size_t(0), size_t(512), size_t(0x8000)}) {
            auto gb = machine(color, cartridgeSize);
            const size_t workSize = color ? 0x8000 : 0x2000;
            const size_t total = workSize + cartridgeSize + 127;
            for (auto type : {CheatSearchType::Byte, CheatSearchType::WordLittleEndian,
                              CheatSearchType::WordBigEndian}) {
                CheatSearch search;
                search.filter(*gb, type, CheatSearchCondition::Any, 0);
                assert(search.count() == total - (word(type) ? (cartridgeSize ? 3 : 2) : 0));
                for (size_t index = 0; index < search.count(); ++index) {
                    const auto result = search.result(index);
                    assert(result.value == 0);
                    if (word(type)) {
                        assert(result.offset + 1 != workSize);
                        assert(!cartridgeSize || result.offset + 1 != workSize + cartridgeSize);
                        assert(result.offset + 1 != total);
                    }
                }
                const auto first = search.result(0);
                const auto secondBank = atOffset(search, 0x1000);
                assert(first.address == 0xc000 && first.bank == 0);
                assert(secondBank.address == 0xd000 && secondBank.bank == 1);
                if (color) {
                    const auto thirdBank = atOffset(search, 0x2000);
                    assert(thirdBank.address == 0xd000 && thirdBank.bank == 2);
                    const auto finalBank = atOffset(search, 0x7000);
                    assert(finalBank.address == 0xd000 && finalBank.bank == 7);
                }
                if (cartridgeSize) {
                    const auto ram = atOffset(search, workSize);
                    assert(ram.address == 0xa000 && ram.bank == 0);
                    if (cartridgeSize > 0x2000) {
                        const auto bank = atOffset(search, workSize + 0x2000);
                        assert(bank.address == 0xa000 && bank.bank == 1);
                    }
                }
                const auto hram = atOffset(search, workSize + cartridgeSize);
                const auto last = search.result(search.count() - 1);
                assert(hram.address == 0xff80 && hram.bank == 0);
                assert(last.address == (word(type) ? 0xfffd : 0xfffe) && last.bank == 0);
            }
        }
    }
}

void testOverlappingWordsAndRegionEnds() {
    auto gb = machine(true, 0x4000);
    gb->work_ram[0x1fff] = 0x34;
    gb->work_ram[0x2000] = 0x12;
    gb->work_ram[0x7fff] = 0xff;
    gb->cartridge_ram[0] = 0xff;
    gb->cartridge_ram[0x3fff] = 0xff;
    gb->high_ram[0] = 0xff;
    CheatSearch search;
    search.filter(*gb, CheatSearchType::WordLittleEndian, CheatSearchCondition::Equal, 0x1234);
    assert(search.count() == 1);
    assert(search.result(0).address == 0xdfff && search.result(0).bank == 1);
    const auto nextWorkBank = search.byteResult(0, 1);
    assert(nextWorkBank.address == 0xd000 && nextWorkBank.bank == 2 && nextWorkBank.value == 0x12 &&
           nextWorkBank.offset == 0x2000);
    search.reset();
    search.filter(*gb, CheatSearchType::WordLittleEndian, CheatSearchCondition::Equal, 0xffff);
    assert(search.count() == 0);

    gb = machine(true);
    gb->work_ram[8] = 1;
    gb->work_ram[9] = 2;
    gb->work_ram[10] = 3;
    search.reset();
    search.filter(*gb, CheatSearchType::WordLittleEndian, CheatSearchCondition::Any, 0);
    gb->work_ram[9] = 4;
    search.filter(*gb, CheatSearchType::WordLittleEndian, CheatSearchCondition::Changed, 0);
    assert(search.count() == 2);
    assert(search.result(0).offset == 8 && search.result(0).value == 0x0401);
    assert(search.result(1).offset == 9 && search.result(1).value == 0x0304);
    gb->work_ram[9] = 5;
    assert(search.result(0).value == 0x0401 && search.currentValue(*gb, 0) == 0x0501);
    search.reset();
    search.filter(*gb, CheatSearchType::WordLittleEndian, CheatSearchCondition::Any, 0);
    const auto nextFixedBank = search.byteResult(indexOf(search, 0xfff), 1);
    assert(nextFixedBank.address == 0xd000 && nextFixedBank.bank == 1);
}

void testInitialZeroAndEmptyRestart() {
    auto gb = machine(false);
    gb->work_ram[8] = 0x55;
    CheatSearch search;
    search.filter(*gb, CheatSearchType::Byte, CheatSearchCondition::Changed, 0);
    assert(search.count() == 1 && search.result(0).offset == 8);
    bool invalidByteRejected = false;
    try {
        search.byteResult(0, 1);
    } catch (const std::out_of_range &) {
        invalidByteRejected = true;
    }
    assert(invalidByteRejected);
    search.filter(*gb, CheatSearchType::Byte, CheatSearchCondition::Unchanged, 0);
    assert(search.count() == 1 && search.result(0).offset == 8);
    search.filter(*gb, CheatSearchType::Byte, CheatSearchCondition::Equal, 0x66);
    assert(search.count() == 0);
    search.filter(*gb, CheatSearchType::Byte, CheatSearchCondition::Unchanged, 0);
    assert(search.count() == 0x2000 + 127 - 1);
    search.reset();
    assert(search.count() == 0);
    search.filter(*gb, CheatSearchType::Byte, CheatSearchCondition::Decreased, 0);
    assert(search.count() == 0);
}

void testTypeChangeKeepsCandidates() {
    auto gb = machine(false);
    gb->work_ram[8] = 0x12;
    CheatSearch search;
    search.filter(*gb, CheatSearchType::Byte, CheatSearchCondition::Equal, 0x12);
    gb->work_ram[8] = 0x13;
    gb->work_ram[9] = 0x12;
    search.filter(*gb, CheatSearchType::WordLittleEndian, CheatSearchCondition::Increased, 0);
    assert(search.count() == 1 && search.result(0).offset == 8);
    assert(search.result(0).value == 0x1213);
    gb->work_ram[8] = 0x14;
    search.filter(*gb, CheatSearchType::WordBigEndian, CheatSearchCondition::Increased, 0);
    assert(search.count() == 1 && search.result(0).offset == 8);
    assert(search.result(0).value == 0x1412);

    search.reset();
    std::memset(gb->work_ram, 0, sizeof(gb->work_ram));
    gb->work_ram[0x1fff] = 0x55;
    search.filter(*gb, CheatSearchType::Byte, CheatSearchCondition::Equal, 0x55);
    assert(search.count() == 1);
    search.filter(*gb, CheatSearchType::WordLittleEndian, CheatSearchCondition::Any, 0);
    assert(search.count() == 0);
}

void testPhysicalWrites() {
    for (auto type : {CheatSearchType::Byte, CheatSearchType::WordLittleEndian,
                      CheatSearchType::WordBigEndian}) {
        auto gb = machine(true, 0x6000);
        const uint16_t original = word(type) ? 0x5a5a : 0x5a;
        store(gb->work_ram + 0x2010, type, original);
        store(gb->cartridge_ram + 0x4005, type, original);
        store(gb->high_ram + 7, type, original);
        CheatSearch search;
        search.filter(*gb, type, CheatSearchCondition::Equal, original);
        assert(search.count() == 3);
        assert(search.result(0).address == 0xd010 && search.result(0).bank == 2);
        assert(search.result(1).address == 0xa005 && search.result(1).bank == 2);
        assert(search.result(2).address == 0xff87 && search.result(2).bank == 0);
        const uint16_t replacement = word(type) ? 0xabcd : 0xcd;
        assert(search.writeValue(*gb, 0, replacement));
        assert(gb->battery_write_generation == 0);
        assert(search.writeValue(*gb, 1, replacement));
        assert(gb->battery_write_generation == 1);
        assert(search.writeValue(*gb, 2, replacement));
        assert(gb->battery_write_generation == 1);
        for (size_t index = 0; index < search.count(); ++index) {
            assert(search.currentValue(*gb, index) == replacement);
            assert(search.result(index).value == original);
        }
        assert(gb->current_ram_bank == 1 && gb->cgb_wram_bank == 1);
        assert(gb->work_ram[0x1010] == 0 && gb->cartridge_ram[0x2005] == 0);
        assert(!search.writeValue(*gb, search.count(), 0));
        --gb->cartridge_ram_size;
        assert(!search.writeValue(*gb, 0, 0));
    }
    auto gb = machine(true, 0x4000);
    gb->cartridge_ram[0x1fff] = 0x34;
    gb->cartridge_ram[0x2000] = 0x12;
    CheatSearch search;
    search.filter(*gb, CheatSearchType::WordLittleEndian, CheatSearchCondition::Equal, 0x1234);
    assert(search.count() == 1 && search.result(0).address == 0xbfff);
    const auto nextCartridgeBank = search.byteResult(0, 1);
    assert(nextCartridgeBank.address == 0xa000 && nextCartridgeBank.bank == 1 &&
           nextCartridgeBank.value == 0x12);
    assert(search.writeValue(*gb, 0, 0xabcd));
    assert(gb->cartridge_ram[0x1fff] == 0xcd && gb->cartridge_ram[0x2000] == 0xab);
    assert(gb->battery_write_generation == 1);
}
} // namespace

int main() {
    testConditions();
    testPhysicalEnumeration();
    testOverlappingWordsAndRegionEnds();
    testInitialZeroAndEmptyRestart();
    testTypeChangeKeepsCandidates();
    testPhysicalWrites();
}

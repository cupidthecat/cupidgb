#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

extern "C" {
#include "cupid/common/sha256.h"
#include "cupid/compat/native_v1.h"
#include "cupid/gb/battery.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/state.h"
}

namespace {
struct HardwareDeleter {
    void operator()(CupidGb *gb) const {
        cupid_gb_cleanup(gb);
        delete gb;
    }
};

using Hardware = std::unique_ptr<CupidGb, HardwareDeleter>;

std::vector<uint8_t> cartridge() {
    std::vector<uint8_t> rom(32768);
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    rom[0x147] = 9;
    rom[0x149] = 2;
    uint8_t sum = 0;
    for (size_t i = 0x134; i < 0x14d; ++i)
        sum = uint8_t(sum - rom[i] - 1);
    rom[0x14d] = sum;
    return rom;
}

void put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = uint8_t(value >> (i * 8));
}

void legacy_recovery(const std::vector<uint8_t> &rom) {
    std::vector<uint8_t> legacy(300349);
    std::memcpy(legacy.data(), "CUPIDGB", 8);
    put32(legacy, 8, 1);
    put32(legacy, 12, uint32_t(legacy.size()));
    put32(legacy, 20, uint32_t(rom.size()));
    cupid_sha256(rom.data(), rom.size(), legacy.data() + 32);
    constexpr size_t ramOffset = 128 + 17 + 16384;
    for (size_t i = 0; i < 8192; ++i)
        legacy[ramOffset + i] = uint8_t(i * 37 + 19);
    CupidSha256 integrity;
    cupid_sha256_init(&integrity);
    cupid_sha256_update(&integrity, legacy.data(), 96);
    cupid_sha256_update(&integrity, legacy.data() + 128, legacy.size() - 128);
    cupid_sha256_final(&integrity, legacy.data() + 96);
    // A C aggregate copy must remain visible to the owning C++ object and the
    // following C API calls, including under Release interprocedural optimization.
    auto recovered = std::make_unique<CupidNativeV1Battery>();
    assert(cupid_native_v1_recover_battery(legacy.data(), legacy.size(), rom.data(), rom.size(),
                                           8192, recovered.get()) == CUPID_NATIVE_V1_OK);
    assert(recovered->ram_size == 8192 && !recovered->has_rtc);
    assert(cupid_native_v1_battery_size(recovered.get()) == 8192);
    std::vector<uint8_t> exported(8192);
    size_t written = 0;
    assert(cupid_native_v1_export_battery(recovered.get(), exported.data(), exported.size(),
                                          &written) == CUPID_NATIVE_V1_OK);
    assert(written == exported.size());
    assert(std::memcmp(exported.data(), legacy.data() + ramOffset, exported.size()) == 0);
}

struct Observation {
    unsigned count = 0;
    uint8_t last = 0;
};

void serial_output(const CupidGb *gb, uint8_t byte, void *user_data) {
    auto &observation = *static_cast<Observation *>(user_data);
    assert(!(gb->io_registers[2] & 0x80) && (gb->interrupt_flags & 8));
    ++observation.count;
    observation.last = byte;
}

void live_state(const std::vector<uint8_t> &rom, CupidGbModel model) {
    Hardware gb(new CupidGb{});
    cupid_gb_set_model(gb.get(), model);
    assert(cupid_gb_load_rom(gb.get(), rom.data(), rom.size()));
    assert(gb->loaded && gb->header.has_battery && gb->cartridge_ram_size == 8192);
    assert(cupid_gb_battery_size(gb.get()) == 8192);
    gb->cpu.a = 0x17;
    gb->work_ram[3] = 0x42;
    std::vector<uint8_t> state(cupid_gb_state_size(gb.get()));
    size_t written = 0;
    assert(cupid_gb_save_state(gb.get(), state.data(), state.size(), &written) ==
           CUPID_GB_STATE_OK);
    assert(written == state.size());
    gb->cpu.a = 0x88;
    gb->work_ram[3] = 0xee;
    Observation observation;
    cupid_gb_set_serial_output_callback(gb.get(), serial_output, &observation);
    assert(cupid_gb_load_state(gb.get(), state.data(), state.size()) == CUPID_GB_STATE_OK);
    assert(gb->cpu.a == 0x17 && gb->work_ram[3] == 0x42);
    assert(gb->serial_output_callback == serial_output &&
           gb->serial_output_user_data == &observation);
    cupid_gb_write_u8(gb.get(), 0xff01, 0x41);
    cupid_gb_write_u8(gb.get(), 0xff02, 0x81);
    for (unsigned i = 0; gb->serial_bits_remaining && i < 10000; ++i)
        assert(cupid_gb_step(gb.get()));
    assert(!gb->serial_bits_remaining && observation.count == 1 && observation.last == 0x41);
}
} // namespace

int main() {
    const auto rom = cartridge();
    legacy_recovery(rom);
    live_state(rom, CUPID_GB_MODEL_DMG_ABC);
    live_state(rom, CUPID_GB_MODEL_CGB);
}

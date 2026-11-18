/**
 * @file    test_core_interop.cpp
 * @brief   Regression tests for C/C++ core interoperability under optimization.
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

#include "cupid/gb/gbs.h"
#include "cupid/gb/startup.h"

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
    cupid_gb_set_power_on_seed(gb.get(), UINT64_C(0x0123456789abcdef));
    assert(cupid_gb_load_rom(gb.get(), rom.data(), rom.size()));
    assert(cupid_gb_get_power_on_seed(gb.get()) == UINT64_C(0x0123456789abcdef));
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

std::vector<uint8_t> music_program() {
    std::vector<uint8_t> program(CUPID_GB_GBS_HEADER_SIZE + 64u);
    std::memcpy(program.data(), "GBS\x01", 4);
    program[4] = 2;
    program[5] = 2;
    program[7] = 2;     // Load at 0200.
    program[9] = 2;     // Init at 0200.
    program[10] = 0x10; // Play at 0210.
    program[11] = 2;
    program[12] = 0xf0;
    program[13] = 0xdf;
    program[14] = 0xfc;
    program[15] = 0x85; // Timer playback and GBS double speed, including monochrome hardware.
    std::memcpy(program.data() + 16, "Interop music", 13);
    const std::array<uint8_t, 9> init = {0xea, 0x01, 0xc0, 0x3e, 0x5a, 0xea, 0x00, 0xc0, 0xc9};
    const std::array<uint8_t, 8> play = {0xfa, 0x02, 0xc0, 0x3c, 0xea, 0x02, 0xc0, 0xc9};
    std::copy(init.begin(), init.end(), program.begin() + CUPID_GB_GBS_HEADER_SIZE);
    std::copy(play.begin(), play.end(), program.begin() + CUPID_GB_GBS_HEADER_SIZE + 16u);
    return program;
}

void music_lifecycle(const std::vector<uint8_t> &rom, CupidGbModel model) {
    Hardware gb(new CupidGb{});
    cupid_gb_set_model(gb.get(), model);
    cupid_gb_set_power_on_seed(gb.get(), UINT64_C(0x26154));
    cupid_gb_init(gb.get());
    Observation observation;
    cupid_gb_set_serial_output_callback(gb.get(), serial_output, &observation);
    auto program = music_program();
    CupidGbGbsInfo info{};
    assert(cupid_gb_load_gbs(gb.get(), program.data(), program.size(), &info) == CUPID_GB_GBS_OK);
    assert(gb->loaded && gb->model == model && gb->cpu.a == 1);
    assert(info.first_track == 1 && info.track_count == 2);
    assert(std::strcmp(info.title, "Interop music") == 0);
    assert(cupid_gb_battery_size(gb.get()) == 0);
    const uint64_t generation = gb->rom_generation;
    // The host's mutable source is not the live program or its metadata.
    program[16] = 'X';
    program[CUPID_GB_GBS_HEADER_SIZE] = 0x76;
    for (unsigned i = 0; i < 32; ++i)
        assert(cupid_gb_step(gb.get()));
    assert(gb->work_ram[0] == 0x5a && gb->work_ram[1] == 1);
    std::vector<uint8_t> state(cupid_gb_state_size(gb.get()));
    assert(!state.empty());
    size_t written = 0;
    assert(cupid_gb_save_state(gb.get(), state.data(), state.size(), &written) ==
           CUPID_GB_STATE_OK);
    assert(written == state.size());
    assert(cupid_gb_gbs_start_track(gb.get(), 255) == CUPID_GB_GBS_OK);
    assert(gb->rom_generation == generation && gb->cpu.a == 255);
    for (unsigned i = 0; i < 32; ++i)
        assert(cupid_gb_step(gb.get()));
    assert(gb->work_ram[0] == 0x5a && gb->work_ram[1] == 255);
    assert(cupid_gb_load_state(gb.get(), state.data(), state.size()) == CUPID_GB_STATE_OK);
    assert(gb->work_ram[0] == 0x5a && gb->work_ram[1] == 1);
    assert(cupid_gb_gbs_info(gb.get(), &info) && info.first_track == 1);
    assert(std::strcmp(info.title, "Interop music") == 0);
    assert(gb->serial_output_callback == serial_output &&
           gb->serial_output_user_data == &observation);
    program[0] = 0;
    assert(cupid_gb_load_gbs(gb.get(), program.data(), program.size(), nullptr) ==
           CUPID_GB_GBS_INVALID_HEADER);
    assert(cupid_gb_gbs_info(gb.get(), nullptr) && gb->work_ram[1] == 1);
    assert(cupid_gb_load_rom(gb.get(), rom.data(), rom.size()));
    assert(!cupid_gb_gbs_info(gb.get(), nullptr));
    assert(gb->rom_generation != generation && gb->header.has_battery);
    assert(cupid_gb_battery_size(gb.get()) == 8192);
}
} // namespace

int main() {
    // This header declares C linkage even when included outside the caller's wrapper.
    cupid_gb_set_power_on_seed(nullptr, 0);
    cupid_gb_clear_power_on_seed(nullptr);
    cupid_gb_start_boot_rom(nullptr);
    assert(cupid_gb_get_power_on_seed(nullptr) == 0);
    const auto rom = cartridge();
    legacy_recovery(rom);
    live_state(rom, CUPID_GB_MODEL_DMG_ABC);
    live_state(rom, CUPID_GB_MODEL_CGB);
    music_lifecycle(rom, CUPID_GB_MODEL_DMG_ABC);
    music_lifecycle(rom, CUPID_GB_MODEL_CGB);
    music_lifecycle(rom, CUPID_GB_MODEL_SGB2);
}

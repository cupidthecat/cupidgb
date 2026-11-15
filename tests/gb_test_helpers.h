#ifndef CUPID_GB_TEST_HELPERS_H
#define CUPID_GB_TEST_HELPERS_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cupid/core/emulator.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"
#include "cupid/gbc/cgb.h"

static inline uint8_t compute_header_checksum(const uint8_t *rom) {
    size_t index;
    uint8_t checksum = 0u;

    for (index = 0x0134u; index <= 0x014cu; ++index) {
        checksum = (uint8_t)(checksum - rom[index] - 1u);
    }

    return checksum;
}

static inline void build_test_rom(uint8_t *rom, size_t rom_size) {
    static const uint8_t program[] = {0x3eu, 0x12u, 0xeau, 0x00u, 0xc0u,
                                      0xafu, 0xfau, 0x00u, 0xc0u, 0x76u};
    const char title[] = "CPU TEST";

    memset(rom, 0, rom_size);
    memcpy(&rom[0x0100], program, sizeof(program));
    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x00u;
    rom[0x0148] = 0x00u;
    rom[0x0149] = 0x00u;
    rom[0x014d] = compute_header_checksum(rom);
}

static inline void build_idle_rom(uint8_t *rom, size_t rom_size) {
    build_test_rom(rom, rom_size);
    memset(&rom[0x0100], 0x00, 0x50u);
    rom[0x014d] = 0x00u;
}

static inline void prepare_gb(CupidGb *gb, uint8_t *rom, uint8_t opcode0, uint8_t opcode1,
                              uint8_t opcode2) {
    build_test_rom(rom, 32u * 1024u);
    rom[0x0100] = opcode0;
    rom[0x0101] = opcode1;
    rom[0x0102] = opcode2;
    rom[0x0103] = 0x00u;
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(gb, rom, 32u * 1024u));

    gb->cpu.a = 0x5au;
    gb->cpu.b = 0xc0u;
    gb->cpu.c = 0x10u;
    gb->cpu.d = 0xc0u;
    gb->cpu.e = 0x20u;
    gb->cpu.h = 0xc0u;
    gb->cpu.l = 0x30u;
    gb->cpu.sp = 0xff82u;
    gb->cpu.f = 0u;
    gb->cpu.halted = false;
    gb->cpu.stopped = false;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0u;

    cupid_gb_write_u8(gb, 0xc010u, 0x11u);
    cupid_gb_write_u8(gb, 0xc020u, 0x22u);
    cupid_gb_write_u8(gb, 0xc030u, 0x33u);
    cupid_gb_write_u8(gb, 0xff80u, 0x34u);
    cupid_gb_write_u8(gb, 0xff81u, 0x12u);
}

static inline void step_gb(CupidGb *gb, unsigned int steps) {
    unsigned int index;

    for (index = 0u; index < steps; ++index) {
        assert(cupid_gb_step(gb));
    }
}

void test_gb_hardware(void);

#endif

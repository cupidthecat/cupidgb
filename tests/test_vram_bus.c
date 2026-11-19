/**
 * @file    test_vram_bus.c
 * @brief   VRAM bank selection, held read data, HBlank access, and DMA ownership regressions.
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

#include "../src/gb/vram_bus.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/startup.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fixture {
    CupidGb *gb;
    unsigned phase;
    unsigned cycles;
} Fixture;

static uint8_t pattern(unsigned bank, unsigned offset) {
    return (uint8_t)((offset * 37u + offset / 256u * 13u + bank * 91u) ^ 0xa6u);
}

static void write_byte(Fixture *fixture, uint16_t address, uint8_t value) {
    cupid_gb_write_u8(fixture->gb, address, value);
}

static Fixture create_fixture(unsigned profile, bool fast, unsigned cpu_bank, unsigned ppu_bank,
                              unsigned scroll) {
    Fixture fixture = {0};
    fixture.gb = calloc(1, sizeof(*fixture.gb));
    assert(fixture.gb);
    uint8_t rom[32768] = {0};
    rom[0x143] = profile == 1 ? 0x80 : 0;
    rom[0x147] = profile ? 0 : 0xff;
    rom[0x149] = 3;
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[index] - 1u);
    cupid_gb_set_power_on_seed(fixture.gb, 0);
    cupid_gb_set_model(fixture.gb, profile ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
    assert(cupid_gb_load_rom(fixture.gb, rom, sizeof(rom)));
    write_byte(&fixture, 0xff40, 0);
    for (unsigned bank = 0; bank <= (profile != 0); ++bank)
        for (unsigned offset = 0; offset < 0x2000; ++offset)
            fixture.gb->video_ram[bank * 0x2000u + offset] = pattern(bank, offset);
    memset(fixture.gb->video_ram + 0x1800, 3, 0x800);
    if (profile)
        memset(fixture.gb->video_ram + 0x3800, ppu_bank ? 8 : 0, 0x800);
    for (unsigned index = 0; index < 160; ++index)
        write_byte(&fixture, (uint16_t)(0xfe00u + index), 0);
    write_byte(&fixture, 0xff04, 0);
    write_byte(&fixture, 0xff41, 0);
    write_byte(&fixture, 0xff42, 0);
    write_byte(&fixture, 0xff43, (uint8_t)scroll);
    write_byte(&fixture, 0xff45, 0);
    write_byte(&fixture, 0xff4a, 0);
    write_byte(&fixture, 0xff4b, 200);
    /* Compatibility mode may inherit this bank from color firmware. The fixture
     * establishes that retained latch directly, without changing cartridge mode. */
    fixture.gb->cgb_vram_bank = (uint8_t)cpu_bank;
    fixture.gb->double_speed = fast;
    return fixture;
}

static void destroy_fixture(Fixture *fixture) {
    cupid_gb_cleanup(fixture->gb);
    free(fixture->gb);
}

static void step(Fixture *fixture) {
    fixture->phase = cupid_gb_tick_color_dma_slot(fixture->gb, 1, fixture->phase);
    ++fixture->cycles;
}

static void check_read(Fixture *fixture, uint16_t address, uint8_t returned, uint8_t driven) {
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, fixture->gb, sizeof(*before));
    assert(cupid_gb_read_u8(fixture->gb, address) == returned);
    assert(memcmp(before, fixture->gb, sizeof(*before)) == 0);
    free(before);
    assert(cupid_gb_read_bus_u8(fixture->gb, address) == returned);
    assert(fixture->gb->vram_bus.read_value == driven);
}

static void check_hblank(unsigned profile, bool fast, unsigned cpu_bank, unsigned ppu_bank,
                         bool high_data, unsigned scroll) {
    static const uint16_t addresses[] = {0x8012, 0x8ffe, 0x9000, 0x8013, 0x9fff, 0x8ffd};
    static const uint8_t monochrome[2][6] = {
        {0x3c, 0xdf, 0xa6, 0x19, 0x38, 0xf2},
        {0x3c, 0xdf, 0x76, 0x19, 0x03, 0xf2},
    };
    /* Fixed byte vectors for this generated pattern, ordered CPU bank, map
     * attribute bank, unsigned/signed tile selection, then repeated CPU reads. */
    static const uint8_t color[2][2][2][6] = {
        {{{0xb3, 0xb3, 0x03, 0xb3, 0x03, 0xb3}, {0xba, 0x3c, 0x03, 0x03, 0x03, 0x03}},
         {{0xb3, 0xb3, 0x03, 0xb3, 0x03, 0xb3}, {0xba, 0x3c, 0x03, 0x03, 0x03, 0x03}}},
        {{{0xd6, 0xd6, 0x00, 0xd6, 0x00, 0xd6}, {0xd1, 0x53, 0x00, 0x00, 0x00, 0x00}},
         {{0xd6, 0xd6, 0x08, 0xd6, 0x08, 0xd6}, {0xd1, 0x53, 0x08, 0x08, 0x08, 0x08}}},
    };
    static const uint8_t low_address_data[2][4] = {
        {0x3c, 0xdf, 0x19, 0xf2},
        {0x53, 0x72, 0xbc, 0x09},
    };
    const uint8_t map_byte = cpu_bank ? (ppu_bank ? 8u : 0u) : 3u;
    const uint8_t prime = cpu_bank ? 0x91u : 0x7au;
    const uint8_t dma_byte = cpu_bank ? 0xd1u : 0xbau;
    Fixture fixture = create_fixture(profile, fast, cpu_bank, ppu_bank, scroll);
    check_read(&fixture, 0x81e3, prime, prime);
    write_byte(&fixture, 0x8012, 0x6d);
    assert(fixture.gb->vram_bus.read_value == prime);
    write_byte(&fixture, 0x8012, pattern(cpu_bank, 0x12));
    write_byte(&fixture, 0xff40, high_data ? 0x81 : 0x91);

    /* Explicit CPU-T deadlines keep both speed modes on an external schedule.
     * A fast half-dot still owns the CPU port when the first HBlank begins. */
    const unsigned edge = profile ? (fast ? 501u + 2u * scroll : 251u + scroll) : 252u + scroll;
    while (fixture.cycles < edge) {
        step(&fixture);
        const uint8_t stat = cupid_gb_read_bus_u8(fixture.gb, 0xff41);
        if (fixture.cycles == (fast ? 200u : 100u)) {
            assert((stat & 3u) == 3u);
            check_read(&fixture, 0x8123, 0xffu, prime);
            assert(cupid_gb_vram_bus_read(fixture.gb, fixture.gb, 0x8123, true) == dma_byte);
            assert(fixture.gb->vram_bus.read_value == dma_byte);
        }
    }
    assert(fixture.gb->vram_bus.map_address == 0x1814u + (scroll != 0));
    assert(fixture.gb->vram_bus.data_address ==
           0x31u + (high_data ? 0x1000u : 0u) + (ppu_bank ? 0x2000u : 0u));
    assert(fixture.gb->vram_bus.read_value == dma_byte);
    unsigned low_index = 0;
    for (unsigned index = 0; index < sizeof(addresses) / sizeof(*addresses); ++index) {
        uint8_t returned =
            profile ? color[cpu_bank][ppu_bank][high_data][index] : monochrome[high_data][index];
        uint8_t driven = returned;
        if (fast) {
            returned = 0xffu;
            driven = dma_byte;
        } else if (profile && high_data && !(addresses[index] & 0x1000u)) {
            driven = low_address_data[cpu_bank][low_index++];
        }
        check_read(&fixture, addresses[index], returned, driven);
    }
    const uint8_t edge_dma = profile || high_data ? map_byte : 0xf2u;
    assert(cupid_gb_vram_bus_read(fixture.gb, fixture.gb, 0x9ffd, true) == edge_dma);
    assert(fixture.gb->vram_bus.read_value == edge_dma);
    step(&fixture);
    step(&fixture);
    (void)cupid_gb_read_bus_u8(fixture.gb, 0xff41);
    check_read(&fixture, 0x8012, cpu_bank ? 0x53u : 0x3cu, cpu_bank ? 0x53u : 0x3cu);
    check_read(&fixture, 0x9000, cpu_bank ? 0x8du : 0x76u, cpu_bank ? 0x8du : 0x76u);

    CupidGbVramBus history;
    memcpy(&history, &fixture.gb->vram_bus, sizeof(history));
    write_byte(&fixture, 0xff40, 0);
    assert(memcmp(&history, &fixture.gb->vram_bus, sizeof(history)) == 0);
    write_byte(&fixture, 0x8012, 0x53);
    assert(memcmp(&history, &fixture.gb->vram_bus, sizeof(history)) == 0);
    check_read(&fixture, 0x8012, 0x53, 0x53);
    destroy_fixture(&fixture);
}

static void check_oam_dma(unsigned profile, unsigned bank) {
    Fixture fixture = create_fixture(profile, false, bank, 0, 0);
    const uint16_t map = fixture.gb->vram_bus.map_address;
    const uint16_t data = fixture.gb->vram_bus.data_address;
    write_byte(&fixture, 0xff46, 0x80);
    while ((fixture.gb->dma_active || fixture.gb->dma_start_delay) && fixture.cycles < 700)
        step(&fixture);
    assert(fixture.cycles < 700);
    for (unsigned index = 0; index < 160; ++index)
        assert(fixture.gb->object_attribute_memory[index] == pattern(bank, index));
    assert(fixture.gb->vram_bus.read_value == pattern(bank, 159));
    assert(fixture.gb->vram_bus.map_address == map && fixture.gb->vram_bus.data_address == data);
    destroy_fixture(&fixture);
}

static void check_write_edges(unsigned profile, bool fast, unsigned bank) {
    static const unsigned normal_cycles[] = {78,  79,  80,  83,  84,  250, 251, 252,
                                             253, 530, 532, 533, 534, 704, 705, 706};
    static const unsigned fast_cycles[] = {154, 156,  157,  165,  166,  500,  501,  502,
                                           503, 1060, 1064, 1065, 1066, 1410, 1411, 1412};
    static const bool accepted[3][16] = {
        {true, true, false, false, false, false, false, true, true, true, true, true, false, false,
         false, true},
        {true, true, true, true, false, false, true, true, true, true, true, false, false, false,
         true, true},
        {true, true, false, false, false, false, false, false, true, true, true, false, false,
         false, true, true},
    };
    const unsigned *cycles = fast ? fast_cycles : normal_cycles;
    const unsigned vector = fast ? 2u : profile ? 1u : 0u;
    Fixture fixture = create_fixture(profile, fast, bank, 0, 0);
    for (unsigned index = 0; index < 16; ++index) {
        fixture.gb->video_ram[0x800u + cycles[index]] = 0x19;
        fixture.gb->video_ram[0x2800u + cycles[index]] = 0x19;
    }
    write_byte(&fixture, 0xff40, 0x91);
    for (unsigned index = 0; index < 16; ++index) {
        while (fixture.cycles < cycles[index])
            step(&fixture);
        const uint16_t address = (uint16_t)(0x8800u + cycles[index]);
        write_byte(&fixture, address, 0xa6);
        const size_t offset = (address & 0x1fffu) + bank * 0x2000u;
        assert(fixture.gb->video_ram[offset] == (accepted[vector][index] ? 0xa6 : 0x19));
        if (profile)
            assert(fixture.gb->video_ram[offset ^ 0x2000u] == 0x19);
        /* Monochrome OAM can reject reads while still accepting this write. */
        if (!profile && cycles[index] >= 530u && cycles[index] <= 533u)
            assert(cupid_gb_read_bus_u8(fixture.gb, address) == 0xff);
    }
    destroy_fixture(&fixture);
}

static void check_stop_history(unsigned profile, bool fast, bool forced, unsigned edge) {
    static const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB,
                                          CUPID_GB_MODEL_CGB,     CUPID_GB_MODEL_MGB,
                                          CUPID_GB_MODEL_SGB,     CUPID_GB_MODEL_SGB2};
    static const unsigned normal_cycles[] = {76, 79, 80, 83, 84, 529, 530, 531, 532, 533, 534};
    static const unsigned fast_cycles[] = {154,  156,  157,  160,  166, 167,
                                           1060, 1064, 1065, 1066, 1068};
    static const bool sampled_open[3][11] = {
        {true, true, false, false, false, true, true, true, true, false, false},
        {true, true, true, true, false, true, true, true, true, false, false},
        {true, true, false, false, false, false, true, true, false, false, false},
    };
    static const bool readable_after_stop[3][11] = {
        {true, true, false, false, false, true, true, true, true, false, false},
        {true, true, false, false, false, false, false, false, false, false, false},
        {false, false, false, false, false, false, true, false, false, false, false},
    };
    const bool color = models[profile] == CUPID_GB_MODEL_CGB;
    const unsigned vector = fast ? 2u : color ? 1u : 0u;
    const unsigned target = fast ? fast_cycles[edge] : normal_cycles[edge];
    const bool forced_claim = !color && forced && edge >= 6u && edge <= 8u;
    uint8_t rom[32768] = {0};
    rom[0x143] = profile == 1 ? 0x80 : 0;
    for (unsigned index = 0x134; index <= 0x14c; ++index)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[index] - 1u);
    Fixture fixture = {0};
    fixture.gb = calloc(1, sizeof(*fixture.gb));
    assert(fixture.gb);
    CupidGb *gb = fixture.gb;
    cupid_gb_set_power_on_seed(gb, 0);
    cupid_gb_set_model(gb, models[profile]);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    write_byte(&fixture, 0xff40, 0);
    memset(gb->video_ram, 0, sizeof(gb->video_ram));
    memset(gb->object_attribute_memory, 0, sizeof(gb->object_attribute_memory));
    gb->video_ram[0x30] = 0x5a;
    write_byte(&fixture, 0xff04, 0);
    write_byte(&fixture, 0xff41, 0);
    write_byte(&fixture, 0xff42, 0);
    write_byte(&fixture, 0xff43, 0);
    write_byte(&fixture, 0xff45, 0);
    write_byte(&fixture, 0xff4a, 0);
    write_byte(&fixture, 0xff4b, 200);
    write_byte(&fixture, 0xff00, 0x30);
    write_byte(&fixture, 0xff0f, 0);
    write_byte(&fixture, 0xffff, 0);
    write_byte(&fixture, 0xff80, 0x10);
    write_byte(&fixture, 0xff81, 0);
    write_byte(&fixture, 0xff82, 0);
    gb->cpu.pc = 0xff80;
    gb->cpu.sp = 0xfffe;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
    gb->double_speed = fast;
    write_byte(&fixture, 0xff40, 0x91);
    while (fixture.cycles < target - 4u) {
        step(&fixture);
        if (forced)
            (void)cupid_gb_read_bus_u8(gb, 0xff41);
    }
    const uint64_t before = gb->elapsed_cycles;
    assert(cupid_gb_step(gb));
    assert(gb->cpu.stopped && gb->cpu.pc == 0xff82);
    assert(gb->elapsed_cycles - before == (fast ? 4u : 8u));
    /* STOP samples before its padding cycle. Color hardware keeps advancing;
     * monochrome hardware retains quiet or forced ownership at that instant. */
    assert(gb->stop_ppu_vram_blocked == (sampled_open[vector][edge] && !forced_claim));
    assert(cupid_gb_read_bus_u8(gb, 0x8030) ==
           (readable_after_stop[vector][edge] && !forced_claim ? 0x5a : 0xff));

    /* A selected button wakes the CPU and releases every complementary PPU gate.
     * The resumed transfer claims VRAM, then HBlank returns the stored byte. */
    write_byte(&fixture, 0xff00, 0x20);
    cupid_gb_set_buttons(gb, 0xfe);
    assert(cupid_gb_step(gb));
    assert(!gb->cpu.stopped && gb->cpu.pc == 0xff82);
    assert(!gb->stop_ppu_vram_blocked && !gb->stop_ppu_oam_blocked &&
           !gb->stop_ppu_palette_blocked);
    cupid_gb_tick(gb, 16);
    assert(cupid_gb_read_bus_u8(gb, 0x8030) == 0xff);
    cupid_gb_tick(gb, fast ? 80u : 40u);
    assert(cupid_gb_read_bus_u8(gb, 0x8030) == 0x5a);
    if (!color && !forced && edge == 2u) {
        cupid_gb_set_buttons(gb, 0xff);
        write_byte(&fixture, 0xff00, 0x30);
        write_byte(&fixture, 0xff40, 0);
        write_byte(&fixture, 0xff0f, 0);
        gb->cpu.pc = 0xff80;
        write_byte(&fixture, 0xff40, 0x91);
        cupid_gb_tick(gb, 24);
        assert(cupid_gb_step(gb) && gb->cpu.stopped);
        assert(!gb->stop_ppu_vram_blocked);
        assert(cupid_gb_read_bus_u8(gb, 0x8030) == 0xff);
        /* LCD power releases the CPU port while the sleeping PPU keeps its
         * complementary STOP latch. Enabling alone cannot advance that latch. */
        write_byte(&fixture, 0xff40, 0);
        assert(cupid_gb_read_bus_u8(gb, 0x8030) == 0x5a);
        write_byte(&fixture, 0xff40, 0x91);
        assert(cupid_gb_read_bus_u8(gb, 0x8030) == 0x5a);
        cupid_gb_tick(gb, 4);
        assert(gb->cpu.stopped && !gb->stop_ppu_vram_blocked);
        assert(cupid_gb_read_bus_u8(gb, 0x8030) == 0x5a);
    }
    destroy_fixture(&fixture);
}

int main(void) {
    for (unsigned profile = 0; profile < 3; ++profile) {
        for (unsigned fast = 0; fast <= (profile != 0); ++fast)
            for (unsigned cpu_bank = 0; cpu_bank <= (profile != 0); ++cpu_bank)
                for (unsigned ppu_bank = 0; ppu_bank <= (profile != 0); ++ppu_bank)
                    for (unsigned high_data = 0; high_data < 2; ++high_data)
                        for (unsigned scroll = 0; scroll < 8; scroll += 7)
                            check_hblank(profile, fast != 0, cpu_bank, ppu_bank, high_data != 0,
                                         scroll);
        for (unsigned bank = 0; bank <= (profile != 0); ++bank)
            check_oam_dma(profile, bank);
        for (unsigned fast = 0; fast <= (profile != 0); ++fast)
            for (unsigned bank = 0; bank <= (profile != 0); ++bank)
                check_write_edges(profile, fast != 0, bank);
    }
    for (unsigned profile = 0; profile < 6; ++profile)
        for (unsigned fast = 0; fast <= (profile == 1 || profile == 2); ++fast)
            for (unsigned forced = 0; forced < 2; ++forced)
                for (unsigned edge = 0; edge < 11; ++edge)
                    check_stop_history(profile, fast != 0, forced != 0, edge);
    return 0;
}

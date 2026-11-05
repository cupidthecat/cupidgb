#include "cupid/gb/cartridge.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *make_rom(size_t size, uint8_t type, uint8_t rom_code, uint8_t ram_code) {
    uint8_t *rom = malloc(size);
    assert(rom && size >= 0x150);
    for (size_t offset = 0; offset < size; ++offset) {
        rom[offset] = (uint8_t)(offset / 0x4000);
    }
    memcpy(rom + 0x134, "CARTRIDGE TEST", 14);
    rom[0x143] = 0;
    rom[0x147] = type;
    rom[0x148] = rom_code;
    rom[0x149] = ram_code;
    uint8_t checksum = 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        checksum = (uint8_t)(checksum - rom[i] - 1);
    rom[0x14d] = checksum;
    return rom;
}

static void test_physical_rom_address_lines(void) {
    CupidGb gb = {0};
    /* File population determines connected address lines; the size byte is
     * metadata. Empty tails of a partially populated chip read as erased ROM. */
    const size_t populated = 0xc001;
    uint8_t *rom = make_rom(populated, 0x19, 0, 0);
    assert(cupid_gb_load_rom(&gb, rom, populated));
    assert(gb.rom_bank_count == 4 && gb.rom_size == populated);
    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x2000, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0x4000) == (bank & 3));
        assert(cupid_gb_read_u8(&gb, 0x4001) == ((bank & 3) == 3 ? 0xff : bank & 3));
    }
    free(rom);
    rom = make_rom(0x8000, 0x19, 8, 0);
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    assert(gb.rom_bank_count == 2 && gb.header.rom_bank_count == 512);
    cupid_gb_write_u8(&gb, 0x2000, 31);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
    /* Unknown size metadata must not hide valid, populated ROM bytes. */
    rom[0x148] = 0xff;
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    assert(gb.rom_bank_count == 2 && gb.header.rom_bank_count == 0);
    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_multicart_wiring(void) {
    CupidGb gb = {0};
    const size_t size = 0x80000;
    uint8_t *rom = make_rom(size, 3, 4, 3);
    /* A second identical logo region identifies 16-bank subcartridges. Two
     * populated headers are enough; checksums of other entries are unrelated. */
    memcpy(rom + 0x40104, rom + 0x104, 48);
    assert(cupid_gb_load_rom(&gb, rom, size));
    assert(gb.mbc1_multicart);
    cupid_gb_write_u8(&gb, 0, 0x0a);
    cupid_gb_write_u8(&gb, 0xa000, 0x5a);
    for (unsigned mode = 0; mode < 2; ++mode) {
        cupid_gb_write_u8(&gb, 0x6000, (uint8_t)mode);
        for (unsigned high = 0; high < 4; ++high) {
            cupid_gb_write_u8(&gb, 0x4000, (uint8_t)high);
            assert(cupid_gb_read_u8(&gb, 0xa000) == 0x5a);
            for (unsigned low = 0; low < 32; ++low) {
                cupid_gb_write_u8(&gb, 0x2000, (uint8_t)low);
                const unsigned upper = ((high << 4) | (low ? low & 15 : 1)) & 31;
                assert(cupid_gb_read_u8(&gb, 0x4000) == upper);
                assert(cupid_gb_read_u8(&gb, 0) == (mode ? (high << 4) & 31 : 0));
            }
        }
    }
    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_small_ram_mirrors(void) {
    const uint8_t types[] = {0x09, 0x03, 0x13, 0x1b};
    for (unsigned controller = 0; controller < sizeof(types); ++controller) {
        CupidGb gb = {0};
        uint8_t *rom = make_rom(0x8000, types[controller], 0, 1);
        assert(cupid_gb_load_rom(&gb, rom, 0x8000));
        assert(gb.cartridge_ram_size == 0x800);
        cupid_gb_write_u8(&gb, 0, 0x0a);
        const uint64_t generation = gb.battery_write_generation;
        cupid_gb_write_u8(&gb, 0xa123, 0x6b);
        for (unsigned mirror = 0; mirror < 4; ++mirror) {
            assert(cupid_gb_read_u8(&gb, (uint16_t)(0xa123 + mirror * 0x800)) == 0x6b);
            cupid_gb_write_u8(&gb, (uint16_t)(0xa123 + mirror * 0x800), 0x6b);
        }
        assert(gb.battery_write_generation == generation + 1);
        cupid_gb_write_u8(&gb, 0xb923, 0x71);
        assert(cupid_gb_read_u8(&gb, 0xa123) == 0x71);
        assert(gb.battery_write_generation == generation + 2);
        free(rom);
        cupid_gb_cleanup(&gb);
    }
}

static void test_extended_mbc3(void) {
    /* Either a larger ROM or additional RAM selects the wider controller. */
    const size_t sizes[] = {0x400000, 0x8000, 0x400000, 0x8000};
    const uint8_t rom_codes[] = {7, 0, 7, 0};
    const uint8_t ram_codes[] = {3, 5, 3, 5};
    const uint8_t types[] = {0x10, 0x10, 0x13, 0x13};
    for (unsigned variant = 0; variant < sizeof(types); ++variant) {
        CupidGb gb = {0};
        uint8_t *rom =
            make_rom(sizes[variant], types[variant], rom_codes[variant], ram_codes[variant]);
        assert(cupid_gb_load_rom(&gb, rom, sizes[variant]));
        cupid_gb_write_u8(&gb, 0, 0x0a);
        for (unsigned bank = 0; bank < 256; ++bank) {
            cupid_gb_write_u8(&gb, 0x2000, (uint8_t)bank);
            assert(gb.current_rom_bank == (bank ? bank : 1));
            assert(cupid_gb_read_u8(&gb, 0x4000) == ((bank ? bank : 1) % gb.rom_bank_count));
        }
        for (unsigned bank = 0; bank < 8; ++bank) {
            cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
            cupid_gb_write_u8(&gb, 0xa000, (uint8_t)(0x60 + bank));
        }
        for (unsigned bank = 0; bank < 8; ++bank) {
            cupid_gb_write_u8(&gb, 0x4000, (uint8_t)(0x10 + bank));
            const unsigned physical = bank % gb.ram_bank_count;
            const unsigned expected = physical + (gb.ram_bank_count < 8 ? 4 : 0);
            assert(cupid_gb_read_u8(&gb, 0xa000) == 0x60 + expected);
        }
        gb.rtc.ls = 0x25;
        cupid_gb_write_u8(&gb, 0x4000, 0x18);
        assert(cupid_gb_read_u8(&gb, 0xa000) == (gb.header.has_timer     ? 0x25u
                                                 : gb.ram_bank_count < 8 ? 0x64u
                                                                         : 0x60u));
        /* Disk state restores the wider banks without extra host state. */
        cupid_gb_write_u8(&gb, 0x4000, 7);
        cupid_gb_write_u8(&gb, 0x2000, 0x81);
        const size_t length = cupid_gb_state_size(&gb);
        uint8_t *state = malloc(length);
        assert(state && cupid_gb_save_state(&gb, state, length, NULL) == CUPID_GB_STATE_OK);
        cupid_gb_write_u8(&gb, 0x4000, 0);
        cupid_gb_write_u8(&gb, 0x2000, 1);
        assert(cupid_gb_load_state(&gb, state, length) == CUPID_GB_STATE_OK);
        assert(gb.current_rom_bank == 0x81 && gb.current_ram_bank == 7);
        free(state);
        free(rom);
        cupid_gb_cleanup(&gb);
    }
}

static void test_ordinary_mbc3_ram_aliases(void) {
    CupidGb gb = {0};
    uint8_t *rom = make_rom(0x8000, 0x13, 0, 3);
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    cupid_gb_write_u8(&gb, 0, 10);
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        cupid_gb_write_u8(&gb, 0xa000, (uint8_t)(0x40 + bank));
    }
    for (unsigned selection = 0; selection < 256; ++selection) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)selection);
        assert(gb.rtc_register == 0);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0x44u + (selection & 3u));
    }
    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_mbc5_ram_enable(void) {
    CupidGb gb = {0};
    uint8_t *rom = make_rom(0x8000, 0x1b, 0, 2);
    assert(cupid_gb_load_rom(&gb, rom, 0x8000));
    cupid_gb_write_u8(&gb, 0, 0x0a);
    cupid_gb_write_u8(&gb, 0xa000, 0x5a);
    for (unsigned value = 0; value < 256; ++value) {
        cupid_gb_write_u8(&gb, 0x1fff, (uint8_t)value);
        assert(gb.ram_enabled == (value == 0x0a));
        assert(cupid_gb_read_u8(&gb, 0xa000) == (value == 0x0a ? 0x5a : 0xff));
    }
    free(rom);
    cupid_gb_cleanup(&gb);
}

static void test_huc1_banks_and_ir(void) {
    CupidGb gb = {0};
    uint8_t *rom = make_rom(0x100000, 0xff, 5, 5);
    for (unsigned region = 0; region < 8; ++region) {
        assert(cupid_gb_load_rom(&gb, rom, 0x100000));
        assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
        const size_t length = cupid_gb_state_size(&gb);
        uint8_t *state = malloc(length);
        assert(state && cupid_gb_save_state(&gb, state, length, NULL) == CUPID_GB_STATE_OK);
        const uint16_t address = (uint16_t)(region * 0x1000);
        cupid_gb_write_u8(&gb, address, 0);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 0);
        /* Restore the initial mapping and its still-unapplied raw register. */
        assert(cupid_gb_load_state(&gb, state, length) == CUPID_GB_STATE_OK);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
        cupid_gb_write_u8(&gb, address, 0);
        assert(cupid_gb_read_u8(&gb, 0x4000) == 0);
        free(state);
    }
    assert(cupid_gb_load_rom(&gb, rom, 0x100000));
    /* HuC1 RAM does not require MBC1's enable sequence. */
    cupid_gb_write_u8(&gb, 0xa000, 0x42);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 0x42);
    for (unsigned bank = 0; bank < 64; ++bank) {
        cupid_gb_write_u8(&gb, 0x2000, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0x4000) == bank);
        assert(cupid_gb_read_u8(&gb, 0) == 0);
    }
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        cupid_gb_write_u8(&gb, 0xa000, (uint8_t)(0x50 + bank));
    }
    for (unsigned bank = 0; bank < 8; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        cupid_gb_write_u8(&gb, 0x6000, 1);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0x50 + bank);
        cupid_gb_write_u8(&gb, 0, 0x1e);
        assert(gb.huc1_ir_mode && cupid_gb_read_u8(&gb, 0xa000) == 0xc0);
        cupid_gb_write_u8(&gb, 0xa000, 0x13);
        cupid_gb_write_u8(&gb, 0, 0);
        assert(cupid_gb_read_u8(&gb, 0xa000) == 0x50 + bank);
    }
    free(rom);
    cupid_gb_cleanup(&gb);
}

int main(void) {
    test_physical_rom_address_lines();
    test_multicart_wiring();
    test_small_ram_mirrors();
    test_extended_mbc3();
    test_ordinary_mbc3_ram_aliases();
    test_mbc5_ram_enable();
    test_huc1_banks_and_ir();
    puts("Cartridge regressions passed.");
    return 0;
}

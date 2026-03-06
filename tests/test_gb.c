#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "cupid/core/emulator.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"

static uint8_t compute_header_checksum(const uint8_t *rom)
{
    size_t index;
    uint8_t checksum = 0u;

    for (index = 0x0134u; index <= 0x014cu; ++index) {
        checksum = (uint8_t)(checksum - rom[index] - 1u);
    }

    return checksum;
}

static void build_test_rom(uint8_t *rom, size_t rom_size)
{
    static const uint8_t program[] = {
        0x3eu, 0x12u,
        0xeau, 0x00u, 0xc0u,
        0xafu,
        0xfau, 0x00u, 0xc0u,
        0x76u
    };
    const char title[] = "CPU TEST";

    memset(rom, 0, rom_size);
    memcpy(&rom[0x0100], program, sizeof(program));
    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x00u;
    rom[0x0148] = 0x00u;
    rom[0x0149] = 0x00u;
    rom[0x014d] = compute_header_checksum(rom);
}

static void build_idle_rom(uint8_t *rom, size_t rom_size)
{
    build_test_rom(rom, rom_size);
    memset(&rom[0x0100], 0x00, 0x50u);
    rom[0x014d] = 0x00u;
}

static void build_mbc1_test_rom(uint8_t *rom, size_t rom_size)
{
    size_t bank;
    const char title[] = "MBC1 TEST";

    memset(rom, 0, rom_size);
    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x01u;
    rom[0x0148] = 0x02u;
    rom[0x0149] = 0x03u;
    rom[0x014a] = 0x01u;

    for (bank = 0u; bank < 8u; ++bank) {
        size_t bank_offset = bank * 0x4000u;
        if (bank_offset < rom_size) {
            memset(&rom[bank_offset], (int)bank, 0x4000u);
        }
    }

    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x01u;
    rom[0x0148] = 0x02u;
    rom[0x0149] = 0x03u;
    rom[0x014a] = 0x01u;
    rom[0x014d] = compute_header_checksum(rom);
}

static bool is_illegal_unprefixed(uint8_t opcode)
{
    switch (opcode) {
    case 0xd3u:
    case 0xdbu:
    case 0xddu:
    case 0xe3u:
    case 0xe4u:
    case 0xebu:
    case 0xecu:
    case 0xedu:
    case 0xf4u:
    case 0xfcu:
    case 0xfdu:
        return true;
    default:
        return false;
    }
}

static void prepare_gb(CupidGb *gb, uint8_t *rom, uint8_t opcode0, uint8_t opcode1, uint8_t opcode2)
{
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
    gb->cpu.sp = 0xff80u;
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

static void step_gb(CupidGb *gb, unsigned int steps)
{
    unsigned int index;

    for (index = 0u; index < steps; ++index) {
        assert(cupid_gb_step(gb));
    }
}

static void test_parse_header(void)
{
    uint8_t rom[32u * 1024u];
    CupidGbCartridgeHeader header = {0};

    build_test_rom(rom, sizeof(rom));

    assert(cupid_gb_parse_header(rom, sizeof(rom), &header));
    assert(strcmp(header.title, "CPU TEST") == 0);
    assert(header.cartridge_type == 0x00u);
    assert(header.rom_size_code == 0x00u);
    assert(header.destination_code == 0x00u);
    assert(header.rom_bank_count == 2u);
    assert(header.ram_bank_count == 0u);
    assert(header.mbc_type == CUPID_GB_MBC_NONE);
    assert(header.header_checksum_valid);
}

static void test_parse_mbc1_header(void)
{
    uint8_t rom[128u * 1024u];
    CupidGbCartridgeHeader header = {0};

    build_mbc1_test_rom(rom, sizeof(rom));

    assert(cupid_gb_parse_header(rom, sizeof(rom), &header));
    assert(strcmp(header.title, "MBC1 TEST") == 0);
    assert(header.cartridge_type == 0x01u);
    assert(header.destination_code == 0x01u);
    assert(header.rom_bank_count == 8u);
    assert(header.ram_bank_count == 4u);
    assert(header.mbc_type == CUPID_GB_MBC1);
    assert(header.header_checksum_valid);
}

static void test_load_rom_file(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    char path[] = "/tmp/cupidgb-rom-XXXXXX";
    int fd;
    FILE *file;

    build_test_rom(rom, sizeof(rom));
    fd = mkstemp(path);
    assert(fd >= 0);

    file = fdopen(fd, "wb");
    assert(file != NULL);
    assert(fwrite(rom, 1u, sizeof(rom), file) == sizeof(rom));
    assert(fclose(file) == 0);

    assert(cupid_gb_load_rom_file(&gb, path));
    assert(strcmp(gb.header.title, "CPU TEST") == 0);
    assert(gb.rom_bank_count == 2u);
    assert(gb.ram_bank_count == 0u);

    assert(unlink(path) == 0);
}

static void test_mbc1_bank_switching(void)
{
    uint8_t rom[128u * 1024u];
    CupidGb gb = {0};

    build_mbc1_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    assert(cupid_gb_read_u8(&gb, 0x0150u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x01u);

    cupid_gb_write_u8(&gb, 0x2000u, 0x02u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x02u);

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);
    cupid_gb_write_u8(&gb, 0xa000u, 0x5au);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x5au);
}

static void test_emulator_executes_program(void)
{
    uint8_t rom[32u * 1024u];
    CupidEmulator emulator = {0};
    unsigned int steps = 0u;

    build_test_rom(rom, sizeof(rom));
    cupid_emulator_init(&emulator, CUPID_SYSTEM_GB);

    assert(cupid_emulator_load_rom(&emulator, rom, sizeof(rom)));
    assert(emulator.gb.cpu.pc == 0x0100u);

    while (!emulator.gb.cpu.halted && steps < 16u) {
        assert(cupid_emulator_step(&emulator));
        ++steps;
    }

    assert(emulator.gb.cpu.halted);
    assert(steps == 5u);
    assert(emulator.gb.cpu.a == 0x12u);
    assert(cupid_gb_read_u8(&emulator.gb, 0xc000u) == 0x12u);

    cupid_emulator_shutdown(&emulator);
}

static void test_unprefixed_opcode_coverage(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int opcode;

    for (opcode = 0u; opcode <= 0xffu; ++opcode) {
        prepare_gb(&gb, rom, (uint8_t)opcode, 0x00u, 0x00u);

        if (is_illegal_unprefixed((uint8_t)opcode)) {
            assert(!cupid_gb_step(&gb));
        } else {
            assert(cupid_gb_step(&gb));
        }
    }
}

static void test_cb_opcode_coverage(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int opcode;

    for (opcode = 0u; opcode <= 0xffu; ++opcode) {
        prepare_gb(&gb, rom, 0xcbu, (uint8_t)opcode, 0x00u);
        assert(cupid_gb_step(&gb));
    }
}

static void test_call_and_ret(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    prepare_gb(&gb, rom, 0xcdu, 0x00u, 0x02u);
    assert(cupid_gb_step(&gb));
    assert(gb.cpu.pc == 0x0200u);
    assert(gb.cpu.sp == 0xff7eu);
    assert(cupid_gb_read_u8(&gb, 0xff7eu) == 0x03u);
    assert(cupid_gb_read_u8(&gb, 0xff7fu) == 0x01u);

    gb.rom[0x0200] = 0xc9u;
    assert(cupid_gb_step(&gb));
    assert(gb.cpu.pc == 0x0103u);
    assert(gb.cpu.sp == 0xff80u);
}

static void test_cb_bit_res_set(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    prepare_gb(&gb, rom, 0xcbu, 0x46u, 0x00u);
    cupid_gb_write_u8(&gb, 0xc030u, 0x01u);
    assert(cupid_gb_step(&gb));
    assert((gb.cpu.f & 0x80u) == 0u);
    assert((gb.cpu.f & 0x20u) != 0u);

    prepare_gb(&gb, rom, 0xcbu, 0x86u, 0x00u);
    cupid_gb_write_u8(&gb, 0xc030u, 0xffu);
    assert(cupid_gb_step(&gb));
    assert(cupid_gb_read_u8(&gb, 0xc030u) == 0xfeu);

    prepare_gb(&gb, rom, 0xcbu, 0xfeu, 0x00u);
    cupid_gb_write_u8(&gb, 0xc030u, 0x00u);
    assert(cupid_gb_step(&gb));
    assert(cupid_gb_read_u8(&gb, 0xc030u) == 0x80u);
}

static void test_jr_negative(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int steps = 0u;
    static const uint8_t program[] = {
        0x3eu, 0x00u,
        0x3cu,
        0xfeu, 0x02u,
        0x28u, 0x02u,
        0x18u, 0xf9u,
        0x76u
    };

    build_test_rom(rom, sizeof(rom));
    memcpy(&rom[0x0100], program, sizeof(program));
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    while (!gb.cpu.halted && steps < 64u) {
        assert(cupid_gb_step(&gb));
        ++steps;
    }

    assert(gb.cpu.halted);
    assert(gb.cpu.a == 0x02u);
}

static void test_jr_positive(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int steps = 0u;
    static const uint8_t program[] = {
        0x3eu, 0x00u,
        0x18u, 0x01u,
        0x3cu,
        0x3cu,
        0x3cu,
        0x76u
    };

    build_test_rom(rom, sizeof(rom));
    memcpy(&rom[0x0100], program, sizeof(program));
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    while (!gb.cpu.halted && steps < 16u) {
        assert(cupid_gb_step(&gb));
        ++steps;
    }

    assert(gb.cpu.halted);
    assert(gb.cpu.a == 0x02u);
}

static void test_jp_hl(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int steps = 0u;
    static const uint8_t program[] = {
        0x21u, 0x06u, 0x01u,
        0x3eu, 0x00u,
        0xe9u,
        0x3cu,
        0x3cu,
        0x76u
    };

    build_test_rom(rom, sizeof(rom));
    memcpy(&rom[0x0100], program, sizeof(program));
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    while (!gb.cpu.halted && steps < 16u) {
        assert(cupid_gb_step(&gb));
        ++steps;
    }

    assert(gb.cpu.halted);
    assert(gb.cpu.a == 0x02u);
}

static void test_pop_af_masks_low_nibble(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int value;

    for (value = 0u; value <= 0xffu; ++value) {
        prepare_gb(&gb, rom, 0xc5u, 0xf1u, 0xf5u);
        gb.cpu.b = 0x12u;
        gb.cpu.c = (uint8_t)value;
        gb.rom[0x0103] = 0xd1u;
        gb.rom[0x0104] = 0x76u;

        assert(cupid_gb_step(&gb));
        assert(cupid_gb_step(&gb));
        assert(cupid_gb_step(&gb));
        assert(cupid_gb_step(&gb));

        assert(gb.cpu.d == 0x12u);
        assert(gb.cpu.e == ((uint8_t)value & 0xf0u));
    }
}

static uint8_t reference_daa(uint8_t a, uint8_t *flags)
{
    bool subtract = ((*flags) & 0x40u) != 0u;
    bool half_carry = ((*flags) & 0x20u) != 0u;
    bool carry = ((*flags) & 0x10u) != 0u;
    uint8_t correction = 0u;

    if (!subtract) {
        if (half_carry || (a & 0x0fu) > 0x09u) {
            correction |= 0x06u;
        }
        if (carry || a > 0x99u) {
            correction |= 0x60u;
            carry = true;
        }
        a = (uint8_t)(a + correction);
    } else {
        if (half_carry) {
            correction |= 0x06u;
        }
        if (carry) {
            correction |= 0x60u;
        }
        a = (uint8_t)(a - correction);
    }

    *flags = (uint8_t)((*flags) & 0x50u);
    if (a == 0u) {
        *flags |= 0x80u;
    }
    if (carry) {
        *flags |= 0x10u;
    }

    return a;
}

static void test_daa_exhaustive(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int a;
    unsigned int flags;

    for (a = 0u; a <= 0xffu; ++a) {
        for (flags = 0u; flags <= 0xf0u; flags += 0x10u) {
            prepare_gb(&gb, rom, 0x27u, 0x76u, 0x00u);
            gb.cpu.a = (uint8_t)a;
            gb.cpu.f = (uint8_t)flags;

            assert(cupid_gb_step(&gb));

            {
                uint8_t expected_flags = (uint8_t)flags;
                uint8_t expected_a = reference_daa((uint8_t)a, &expected_flags);
                assert(gb.cpu.a == expected_a);
                assert(gb.cpu.f == expected_flags);
            }
        }
    }
}

static void test_ppu_scanline_progression(void)
{
    enum { CUPID_GB_STEPS_PER_SCANLINE = 114 };
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0xff40u, 0x91u);
    /* LCD-on warmup adds CUPID_GB_PPU_LCD_ON_DELAY M-cycles of mode 0
       before the first OAM scan begins. */
    step_gb(&gb, CUPID_GB_STEPS_PER_SCANLINE + CUPID_GB_PPU_LCD_ON_DELAY);

    assert(cupid_gb_read_u8(&gb, 0xff44u) == 0x01u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == 0x02u);
}

static void test_ppu_vblank_interrupt(void)
{
    enum {
        CUPID_GB_STEPS_PER_SCANLINE = 114,
        CUPID_GB_VISIBLE_SCANLINES = 144
    };
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0xff40u, 0x91u);
    step_gb(&gb, CUPID_GB_STEPS_PER_SCANLINE * CUPID_GB_VISIBLE_SCANLINES
                 + CUPID_GB_PPU_LCD_ON_DELAY);

    assert(cupid_gb_read_u8(&gb, 0xff44u) == 144u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == 0x01u);
    assert((cupid_gb_read_u8(&gb, 0xff0fu) & 0x01u) != 0u);
    assert(gb.frame_ready);
}

static void test_ppu_dma_transfer(void)
{
    size_t index;
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    for (index = 0u; index < 0xa0u; ++index) {
        cupid_gb_write_u8(&gb, (uint16_t)(0xc000u + index), (uint8_t)(index ^ 0x5au));
    }

    cupid_gb_write_u8(&gb, 0xff46u, 0xc0u);

    for (index = 0u; index < 0xa0u; ++index) {
        assert(cupid_gb_read_u8(&gb, (uint16_t)(0xfe00u + index)) == (uint8_t)(index ^ 0x5au));
    }
}

static void test_ppu_background_render(void)
{
    enum { CUPID_GB_STEPS_UNTIL_HBLANK = 64 };
    static const uint8_t expected[8] = {0u, 1u, 2u, 3u, 0u, 1u, 2u, 3u};
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    size_t index;

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0xff47u, 0xe4u);
    cupid_gb_write_u8(&gb, 0x8000u, 0x55u);
    cupid_gb_write_u8(&gb, 0x8001u, 0x33u);
    cupid_gb_write_u8(&gb, 0x9800u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff40u, 0x91u);

    step_gb(&gb, CUPID_GB_STEPS_UNTIL_HBLANK + CUPID_GB_PPU_LCD_ON_DELAY);

    for (index = 0u; index < 8u; ++index) {
        assert(gb.frame_buffer[index] == expected[index]);
    }
}

int main(void)
{
    test_parse_header();
    test_parse_mbc1_header();
    test_load_rom_file();
    test_mbc1_bank_switching();
    test_emulator_executes_program();
    test_unprefixed_opcode_coverage();
    test_cb_opcode_coverage();
    test_call_and_ret();
    test_cb_bit_res_set();
    test_jr_negative();
    test_jr_positive();
    test_jp_hl();
    test_pop_af_masks_low_nibble();
    test_daa_exhaustive();
    test_ppu_scanline_progression();
    test_ppu_vblank_interrupt();
    test_ppu_dma_transfer();
    test_ppu_background_render();
    return 0;
}
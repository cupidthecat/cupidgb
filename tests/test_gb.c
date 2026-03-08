#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "cupid/core/emulator.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/timer.h"
#include "cupid/gbc/cgb.h"

static uint8_t compute_header_checksum(const uint8_t *rom)
{
    size_t index;
    uint8_t checksum = 0u;

    for (index = 0x0134u; index <= 0x014cu; ++index) {
        checksum = (uint8_t)(checksum - rom[index] - 1u);
    }

    return checksum;
}

static const uint8_t nintendo_logo[] = {
    0xceu, 0xedu, 0x66u, 0x66u, 0xccu, 0x0du, 0x00u, 0x0bu,
    0x03u, 0x73u, 0x00u, 0x83u, 0x00u, 0x0cu, 0x00u, 0x0du,
    0x00u, 0x08u, 0x11u, 0x1fu, 0x88u, 0x89u, 0x00u, 0x0eu,
    0xdcu, 0xccu, 0x6eu, 0xe6u, 0xddu, 0xddu, 0xd9u, 0x99u,
    0xbbu, 0xbbu, 0x67u, 0x63u, 0x6eu, 0x0eu, 0xecu, 0xccu,
    0xddu, 0xdcu, 0x99u, 0x9fu, 0xbbu, 0xb9u, 0x33u, 0x3eu
};

static uint8_t compute_header_checksum_at(const uint8_t *rom, size_t base_offset)
{
    size_t index;
    uint8_t checksum = 0u;

    for (index = base_offset + 0x0134u; index <= base_offset + 0x014cu; ++index) {
        checksum = (uint8_t)(checksum - rom[index] - 1u);
    }

    return checksum;
}

static uint8_t popcount8(uint8_t value)
{
    uint8_t count = 0u;

    while (value != 0u) {
        count = (uint8_t)(count + (value & 0x01u));
        value >>= 1u;
    }

    return count;
}

static unsigned sgb_header_stream_popcount(const uint8_t *rom, size_t rom_size)
{
    size_t offset = 0x0104u;
    unsigned total = 0u;

    while (offset < 0x0158u) {
        uint8_t checksum = 0u;
        size_t block_index;

        for (block_index = 0u; block_index < 14u; ++block_index) {
            uint8_t value = 0u;

            if (offset <= 0x014fu && offset < rom_size) {
                value = rom[offset];
            }

            checksum = (uint8_t)(checksum + value);
            total += popcount8(value);
            offset += 1u;
        }

        total += popcount8(checksum);
    }

    return total;
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

static void build_cgb_speed_rom(uint8_t *rom, size_t rom_size)
{
    static const uint8_t program[] = {
        0x3eu, 0x01u,       /* LD A,1 */
        0xe0u, 0x4du,       /* LDH (KEY1),A */
        0x10u, 0x00u,       /* STOP */
        0x00u,              /* NOP */
        0x76u               /* HALT */
    };

    build_test_rom(rom, rom_size);
    memcpy(&rom[0x0100], program, sizeof(program));
    rom[0x0143] = 0xc0u; /* CGB required */
    rom[0x014d] = compute_header_checksum(rom);
}

static void build_ei_sequence_rom(uint8_t *rom, size_t rom_size)
{
    static const uint8_t program[] = {
        0xf3u,                         /* DI */
        0x3eu, 0x08u,                  /* LD A,$08 */
        0xe0u, 0x0fu,                  /* LDH (IF),A */
        0xeau, 0xffu, 0xffu,           /* LD ($FFFF),A */
        0xafu,                         /* XOR A */
        0xeau, 0x00u, 0xc0u,           /* LD ($C000),A */
        0xeau, 0x01u, 0xc0u,           /* LD ($C001),A */
        0xfbu,                         /* EI */
        0xfbu,                         /* EI */
        0x3eu, 0x01u,                  /* LD A,$01 ; must not execute */
        0xeau, 0x00u, 0xc0u,           /* LD ($C000),A */
        0x76u                          /* HALT */
    };
    static const uint8_t isr[] = {
        0x3eu, 0x01u,                  /* LD A,$01 */
        0xeau, 0x01u, 0xc0u,           /* LD ($C001),A */
        0x76u                          /* HALT */
    };

    build_test_rom(rom, rom_size);
    memcpy(&rom[0x0100], program, sizeof(program));
    memcpy(&rom[0x0058], isr, sizeof(isr));
    rom[0x014d] = compute_header_checksum(rom);
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

static void build_mbc1_ram_test_rom(uint8_t *rom, size_t rom_size)
{
    const char title[] = "MBC1 RAM";

    memset(rom, 0, rom_size);
    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x03u;
    rom[0x0148] = 0x01u;
    rom[0x0149] = 0x03u;
    rom[0x014a] = 0x01u;
    rom[0x014d] = compute_header_checksum(rom);
}

static void build_mbc2_test_rom(uint8_t *rom, size_t rom_size)
{
    size_t bank;
    const char title[] = "MBC2 TEST";

    memset(rom, 0, rom_size);
    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x06u;
    rom[0x0148] = 0x01u;
    rom[0x0149] = 0x00u;
    rom[0x014a] = 0x01u;

    for (bank = 0u; bank < 4u; ++bank) {
        size_t bank_offset = bank * 0x4000u;
        if (bank_offset < rom_size) {
            memset(&rom[bank_offset], (int)bank, 0x4000u);
        }
    }

    memcpy(&rom[0x0134], title, sizeof(title) - 1u);
    rom[0x0147] = 0x06u;
    rom[0x0148] = 0x01u;
    rom[0x0149] = 0x00u;
    rom[0x014a] = 0x01u;
    rom[0x014d] = compute_header_checksum(rom);
}

static void build_mbc1_multicart_test_rom(uint8_t *rom, size_t rom_size)
{
    size_t bank;
    size_t image;
    static const char *const titles[4] = {
        "MULTI-A",
        "MULTI-B",
        "MULTI-C",
        "MULTI-D"
    };

    memset(rom, 0, rom_size);

    for (bank = 0u; bank < 64u; ++bank) {
        memset(&rom[bank * 0x4000u], (int)bank, 0x4000u);
    }

    for (image = 0u; image < 4u; ++image) {
        size_t base_offset = image * 0x40000u;

        memcpy(&rom[base_offset + 0x0104u], nintendo_logo, sizeof(nintendo_logo));
        memset(&rom[base_offset + 0x0134u], 0, 16u);
        memcpy(&rom[base_offset + 0x0134u], titles[image], strlen(titles[image]));
        rom[base_offset + 0x0143u] = 0x00u;
        rom[base_offset + 0x0147u] = 0x01u;
        rom[base_offset + 0x0148u] = 0x03u;
        rom[base_offset + 0x0149u] = 0x00u;
        rom[base_offset + 0x014au] = 0x01u;
        rom[base_offset + 0x014du] = compute_header_checksum_at(rom, base_offset);
    }

    rom[0x0148u] = 0x05u;
    rom[0x014du] = compute_header_checksum(rom);
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

static void test_boot_model_profiles(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG0);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_DMG0);
    assert(gb.cpu.a == 0x01u);
    assert(gb.cpu.f == 0x00u);
    assert(gb.cpu.b == 0xffu);
    assert(gb.cpu.c == 0x13u);
    assert(gb.cpu.e == 0xc1u);
    assert(gb.cpu.h == 0x84u);
    assert(gb.cpu.l == 0x03u);
    assert(gb.io_registers[0x04u] == 0x18u);
    assert(gb.io_registers[CUPID_GB_IO_STAT] == 0x83u);
    assert(gb.io_registers[CUPID_GB_IO_DMA] == 0x01u);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x91u);
    assert(gb.div_counter == 12u);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(gb.model == CUPID_GB_MODEL_DMG0);
    assert(gb.cpu.b == 0xffu);
    assert(gb.io_registers[0x04u] == 0x18u);
    assert(gb.io_registers[CUPID_GB_IO_STAT] == 0x83u);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x91u);
    assert(gb.div_counter == 12u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG_ABC);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_DMG_ABC);
    assert(gb.cpu.a == 0x01u);
    assert(gb.cpu.f == 0xb0u);
    assert(gb.cpu.b == 0x00u);
    assert(gb.cpu.c == 0x13u);
    assert(gb.cpu.e == 0xd8u);
    assert(gb.cpu.h == 0x01u);
    assert(gb.cpu.l == 0x4du);
    assert(gb.io_registers[0x04u] == 0xabu);
    assert(gb.io_registers[CUPID_GB_IO_STAT] == 0x80u);
    assert(gb.io_registers[CUPID_GB_IO_DMA] == 0x0au);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x00u);
    assert(gb.div_counter == 51u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_CGB);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_CGB);
    assert(gb.cpu.a == 0x11u);
    assert(gb.cpu.f == 0x80u);
    assert(gb.cpu.d == 0xffu);
    assert(gb.cpu.e == 0x56u);
    assert(gb.io_registers[0x04u] == 0x1eu);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x00u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_MGB);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_MGB);
    assert(gb.cpu.a == 0xffu);
    assert(gb.cpu.f == 0xb0u);
    assert(gb.cpu.b == 0x00u);
    assert(gb.cpu.c == 0x13u);
    assert(gb.cpu.e == 0xd8u);
    assert(gb.cpu.h == 0x01u);
    assert(gb.cpu.l == 0x4du);
    assert(gb.io_registers[0x04u] == 0xabu);
    assert(gb.io_registers[CUPID_GB_IO_STAT] == 0x80u);
    assert(gb.io_registers[CUPID_GB_IO_DMA] == 0x0au);
    assert(gb.io_registers[CUPID_GB_IO_LY] == 0x00u);
    assert(gb.div_counter == 51u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_SGB);
    assert(gb.cpu.a == 0x01u);
    assert(gb.cpu.f == 0x00u);
    assert(gb.cpu.b == 0x00u);
    assert(gb.cpu.c == 0x14u);
    assert(gb.cpu.d == 0x00u);
    assert(gb.cpu.e == 0x00u);
    assert(gb.cpu.h == 0xc0u);
    assert(gb.cpu.l == 0x60u);
    assert(gb.io_registers[0x04u] == 0xd8u);
    assert(gb.div_counter == 24u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB2);
    cupid_gb_init(&gb);
    assert(gb.model == CUPID_GB_MODEL_SGB2);
    assert(gb.cpu.a == 0xffu);
    assert(gb.cpu.f == 0x00u);
    assert(gb.cpu.b == 0x00u);
    assert(gb.cpu.c == 0x14u);
    assert(gb.cpu.d == 0x00u);
    assert(gb.cpu.e == 0x00u);
    assert(gb.cpu.h == 0xc0u);
    assert(gb.cpu.l == 0x60u);
    assert(gb.io_registers[0x04u] == 0xd8u);
    assert(gb.div_counter == 24u);
}

static void test_boot_io_defaults(void)
{
    CupidGb gb = {0};

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG0);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff02u) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff03u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff07u) == 0xf8u);
    assert(cupid_gb_read_u8(&gb, 0xff08u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff41u) == 0x83u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 0x91u);
    assert(cupid_gb_read_u8(&gb, 0xff46u) == 0x01u);
    assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xffu);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_DMG_ABC);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff02u) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff07u) == 0xf8u);
    assert(cupid_gb_read_u8(&gb, 0xff41u) == 0x80u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xff46u) == 0x0au);
    assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xffu);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_CGB);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff4du) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff4fu) == 0xfeu);
    assert(cupid_gb_read_u8(&gb, 0xff68u) == 0x40u);
    assert(cupid_gb_read_u8(&gb, 0xff70u) == 0xf9u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_MGB);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff02u) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff07u) == 0xf8u);
    assert(cupid_gb_read_u8(&gb, 0xff41u) == 0x80u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xff46u) == 0x0au);
    assert(cupid_gb_read_u8(&gb, 0xff50u) == 0xffu);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff00u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff02u) == 0x7eu);
    assert(cupid_gb_read_u8(&gb, 0xff07u) == 0xf8u);
    assert(cupid_gb_read_u8(&gb, 0xff26u) == 0xf0u);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB2);
    cupid_gb_init(&gb);
    assert(cupid_gb_read_u8(&gb, 0xff00u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff26u) == 0xf0u);
}

static void test_sgb_boot_div_phase_depends_on_header_stream(void)
{
    uint8_t rom1[32u * 1024u];
    uint8_t rom2[32u * 1024u];
    CupidGb gb = {0};
    uint16_t counter1;
    unsigned popcount1;
    unsigned popcount2;

    build_idle_rom(rom1, sizeof(rom1));
    memcpy(rom2, rom1, sizeof(rom2));

    rom1[0x0146u] = 0x03u;
    rom2[0x0146u] = 0x03u;
    rom1[0x014eu] = 0x34u;
    rom1[0x014fu] = 0x12u;
    rom2[0x014eu] = 0x96u;
    rom2[0x014fu] = 0xa7u;
    popcount1 = sgb_header_stream_popcount(rom1, sizeof(rom1));
    popcount2 = sgb_header_stream_popcount(rom2, sizeof(rom2));

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    assert(cupid_gb_load_rom(&gb, rom1, sizeof(rom1)));
    assert(gb.io_registers[0x04u] == 0xd8u);
    counter1 = gb.div_counter;

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    assert(cupid_gb_load_rom(&gb, rom2, sizeof(rom2)));
    assert(gb.io_registers[0x04u] == 0xd8u);
    assert(gb.div_counter == (uint16_t)((counter1 + 64u - ((popcount2 - popcount1) & 0x3fu)) & 0x3fu));
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

static void test_battery_save_loads_for_valid_rom(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    char rom_path[] = "/tmp/cupidgb-save-valid-XXXXXX.gb";
    uint8_t expected[8] = {0x12u, 0x34u, 0x56u, 0x78u, 0x9au, 0xbcu, 0xdeu, 0xf0u};
    int fd;
    FILE *file;

    build_test_rom(rom, sizeof(rom));
    rom[0x0147] = 0x03u; /* MBC1+RAM+BATTERY */
    rom[0x0149] = 0x02u; /* 8 KiB RAM */
    rom[0x014d] = compute_header_checksum(rom);

    fd = mkstemps(rom_path, 3);
    assert(fd >= 0);
    file = fdopen(fd, "wb");
    assert(file != NULL);
    assert(fwrite(rom, 1u, sizeof(rom), file) == sizeof(rom));
    assert(fclose(file) == 0);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    cupid_gb_set_save_path(&gb, rom_path);

    file = fopen(gb.save_path, "wb");
    assert(file != NULL);
    assert(fwrite(expected, 1u, sizeof(expected), file) == sizeof(expected));
    assert(fclose(file) == 0);

    cupid_gb_load_save(&gb);

    assert(memcmp(gb.cartridge_ram, expected, sizeof(expected)) == 0);

    assert(unlink(gb.save_path) == 0);
    assert(unlink(rom_path) == 0);
}

static void test_battery_save_ignored_for_invalid_untitled_rom(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    char rom_path[] = "/tmp/cupidgb-save-skip-XXXXXX.gb";
    uint8_t persisted[8] = {0xffu, 0xeeu, 0xddu, 0xccu, 0xbbu, 0xaau, 0x99u, 0x88u};
    uint8_t zeroes[8] = {0};
    int fd;
    FILE *file;

    build_test_rom(rom, sizeof(rom));
    memset(&rom[0x0134], 0, 16u);
    rom[0x0147] = 0x03u; /* MBC1+RAM+BATTERY */
    rom[0x0149] = 0x02u; /* 8 KiB RAM */
    rom[0x014d] = 0x00u; /* intentionally invalid */

    fd = mkstemps(rom_path, 3);
    assert(fd >= 0);
    file = fdopen(fd, "wb");
    assert(file != NULL);
    assert(fwrite(rom, 1u, sizeof(rom), file) == sizeof(rom));
    assert(fclose(file) == 0);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    cupid_gb_set_save_path(&gb, rom_path);

    file = fopen(gb.save_path, "wb");
    assert(file != NULL);
    assert(fwrite(persisted, 1u, sizeof(persisted), file) == sizeof(persisted));
    assert(fclose(file) == 0);

    cupid_gb_load_save(&gb);

    assert(memcmp(gb.cartridge_ram, zeroes, sizeof(zeroes)) == 0);

    assert(unlink(gb.save_path) == 0);
    assert(unlink(rom_path) == 0);
}

static void test_mbc1_bank_switching(void)
{
    uint8_t rom[128u * 1024u];
    CupidGb gb = {0};

    build_mbc1_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(!gb.mbc1_multicart);

    assert(cupid_gb_read_u8(&gb, 0x0150u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x01u);

    cupid_gb_write_u8(&gb, 0x2000u, 0x02u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x02u);

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);
    cupid_gb_write_u8(&gb, 0xa000u, 0x5au);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x5au);
}

static void test_mbc1_multicart_bank_switching(void)
{
    uint8_t rom[1024u * 1024u];
    CupidGb gb = {0};

    build_mbc1_multicart_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(gb.mbc1_multicart);

    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x01u);

    cupid_gb_write_u8(&gb, 0x2000u, 0x10u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x00u);

    cupid_gb_write_u8(&gb, 0x4000u, 0x02u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x20u);

    cupid_gb_write_u8(&gb, 0x6000u, 0x01u);
    assert(cupid_gb_read_u8(&gb, 0x0000u) == 0x20u);
}

static void test_mbc1_ram_banking_mode_switching(void)
{
    uint8_t rom[64u * 1024u];
    CupidGb gb = {0};

    build_mbc1_ram_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);

    cupid_gb_write_u8(&gb, 0x6000u, 0x01u);
    cupid_gb_write_u8(&gb, 0x4000u, 0x03u);
    cupid_gb_write_u8(&gb, 0xa000u, 0x5au);
    assert(gb.current_ram_bank == 3u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x5au);

    cupid_gb_write_u8(&gb, 0x6000u, 0x00u);
    assert(gb.current_ram_bank == 0u);
    cupid_gb_write_u8(&gb, 0xa000u, 0x11u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x11u);

    cupid_gb_write_u8(&gb, 0x6000u, 0x01u);
    cupid_gb_write_u8(&gb, 0x4000u, 0x03u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x5au);

    cupid_gb_write_u8(&gb, 0x4000u, 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0x11u);
}

static void test_mbc2_address_decoding(void)
{
    uint8_t rom[64u * 1024u];
    CupidGb gb = {0};

    build_mbc2_test_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);
    cupid_gb_write_u8(&gb, 0xa000u, 0x05u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0xf5u);

    cupid_gb_write_u8(&gb, 0x3effu, 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0xffu);

    cupid_gb_write_u8(&gb, 0x3fffu, 0x0au);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0xffu);

    cupid_gb_write_u8(&gb, 0x0000u, 0x0au);
    assert(cupid_gb_read_u8(&gb, 0xa000u) == 0xf5u);

    cupid_gb_write_u8(&gb, 0x0100u, 0x02u);
    assert(cupid_gb_read_u8(&gb, 0x4000u) == 0x02u);
}

static void test_load_32mb_mbc5_rom(void)
{
    uint8_t *rom;
    CupidGb gb = {0};

    rom = calloc(1u, 4u * 1024u * 1024u);
    assert(rom != NULL);

    memcpy(&rom[0x0134], "MBC5 32MB", 9u);
    rom[0x0147] = 0x19u;
    rom[0x0148] = 0x07u;
    rom[0x0149] = 0x00u;
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, 4u * 1024u * 1024u));
    assert(gb.rom_bank_count == 256u);
    assert(gb.header.mbc_type == CUPID_GB_MBC5);

    free(rom);
}

static void test_load_64mb_mbc5_rom(void)
{
    uint8_t *rom;
    CupidGb gb = {0};

    rom = calloc(1u, 8u * 1024u * 1024u);
    assert(rom != NULL);

    memcpy(&rom[0x0134], "MBC5 64MB", 9u);
    rom[0x0147] = 0x19u;
    rom[0x0148] = 0x08u;
    rom[0x0149] = 0x00u;
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, 8u * 1024u * 1024u));
    assert(gb.rom_bank_count == 512u);
    assert(gb.header.mbc_type == CUPID_GB_MBC5);

    free(rom);
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
    assert(gb.cpu.sp == 0xff80u);
    assert(cupid_gb_read_u8(&gb, 0xff80u) == 0x03u);
    assert(cupid_gb_read_u8(&gb, 0xff81u) == 0x01u);

    gb.rom[0x0200] = 0xc9u;
    assert(cupid_gb_step(&gb));
    assert(gb.cpu.pc == 0x0103u);
    assert(gb.cpu.sp == 0xff82u);
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

static void test_cgb_speed_switch(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_cgb_speed_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    assert(gb.cgb_mode);
    assert(gb.cpu.a == 0x11u);
    assert(cupid_gb_read_u8(&gb, 0xff4du) == 0x7eu);

    assert(cupid_gb_step(&gb)); /* LD A,1 */
    assert(cupid_gb_step(&gb)); /* LDH (KEY1),A */
    assert((cupid_gb_read_u8(&gb, 0xff4du) & 0x81u) == 0x01u);

    assert(cupid_gb_step(&gb)); /* STOP toggles speed */
    assert(gb.double_speed);
    assert(!gb.cpu.stopped);
    assert((cupid_gb_read_u8(&gb, 0xff4du) & 0x81u) == 0x80u);
}

static void test_dual_mode_rom_stays_dmg(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x0143] = 0x80u; /* CGB-compatible, but not CGB-required */
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(!gb.cgb_mode);
    assert(gb.cpu.a == 0x01u);
    assert(cupid_gb_read_u8(&gb, 0xff4du) == 0xffu);
}

static void test_dual_mode_rom_enters_cgb_when_model_selected(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x0143] = 0x80u;
    rom[0x014d] = compute_header_checksum(rom);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_CGB);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(gb.cgb_mode);
    assert(gb.cpu.a == 0x11u);
    assert(cupid_gb_read_u8(&gb, 0xff4du) == 0x7eu);
}

static void test_cgb_vram_and_wram_banking(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x0143] = 0xc0u;
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(gb.cgb_mode);

    cupid_gb_write_u8(&gb, 0x8000u, 0x12u);
    cupid_gb_write_u8(&gb, 0xff4fu, 0x01u);
    cupid_gb_write_u8(&gb, 0x8000u, 0x34u);
    assert(cupid_gb_read_u8(&gb, 0x8000u) == 0x34u);
    cupid_gb_write_u8(&gb, 0xff4fu, 0x00u);
    assert(cupid_gb_read_u8(&gb, 0x8000u) == 0x12u);

    cupid_gb_write_u8(&gb, 0xd000u, 0x56u);
    cupid_gb_write_u8(&gb, 0xff70u, 0x02u);
    cupid_gb_write_u8(&gb, 0xd000u, 0x78u);
    assert(cupid_gb_read_u8(&gb, 0xd000u) == 0x78u);
    cupid_gb_write_u8(&gb, 0xff70u, 0x01u);
    assert(cupid_gb_read_u8(&gb, 0xd000u) == 0x56u);
}

static void test_cgb_palette_registers_and_gdma(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x0143] = 0xc0u;
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(gb.cgb_mode);

    cupid_gb_write_u8(&gb, 0xff68u, 0x80u);
    cupid_gb_write_u8(&gb, 0xff69u, 0x1fu);
    cupid_gb_write_u8(&gb, 0xff69u, 0x03u);
    assert((cupid_gb_read_u8(&gb, 0xff68u) & 0x3fu) == 0x02u);
    cupid_gb_write_u8(&gb, 0xff68u, 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xff69u) == 0x1fu);
    cupid_gb_write_u8(&gb, 0xff68u, 0x01u);
    assert(cupid_gb_read_u8(&gb, 0xff69u) == 0x03u);

    cupid_gb_write_u8(&gb, 0xc120u, 0xaau);
    cupid_gb_write_u8(&gb, 0xc121u, 0xbbu);
    cupid_gb_write_u8(&gb, 0xff51u, 0xc1u);
    cupid_gb_write_u8(&gb, 0xff52u, 0x20u);
    cupid_gb_write_u8(&gb, 0xff53u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff54u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff55u, 0x00u);
    assert(cupid_gb_read_u8(&gb, 0x8000u) == 0xaau);
    assert(cupid_gb_read_u8(&gb, 0x8001u) == 0xbbu);
    assert(cupid_gb_read_u8(&gb, 0xff55u) == 0xffu);
}

static void test_cgb_dmg_compatibility_palette_for_pokemon_red(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    memset(&rom[0x0134], 0, 16u);
    memcpy(&rom[0x0134], "POKEMON RED", 11u);
    rom[0x0143] = 0x00u;
    rom[0x014bu] = 0x01u;
    rom[0x014d] = compute_header_checksum(rom);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_CGB);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(!gb.cgb_mode);
    assert(cupid_cgb_compat_active(&gb));
    assert(cupid_cgb_compat_bg_color(&gb, 0u) == 0x7fffu);
    assert(cupid_cgb_compat_bg_color(&gb, 1u) == 0x421fu);
    assert(cupid_cgb_compat_bg_color(&gb, 2u) == 0x1cf2u);
    assert(cupid_cgb_compat_obj_color(&gb, 0u, 1u) == 0x1befu);
    assert(cupid_cgb_compat_obj_color(&gb, 1u, 1u) == 0x421fu);
}

static void test_cgb_dmg_compatibility_palette_requires_nintendo_licensee(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    memset(&rom[0x0134], 0, 16u);
    memcpy(&rom[0x0134], "POKEMON RED", 11u);
    rom[0x0143] = 0x00u;
    rom[0x014bu] = 0x08u;
    rom[0x014d] = compute_header_checksum(rom);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_CGB);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(!gb.cgb_mode);
    assert(cupid_cgb_compat_active(&gb));
    assert(cupid_cgb_compat_bg_color(&gb, 0u) == 0x7fffu);
    assert(cupid_cgb_compat_bg_color(&gb, 1u) == 0x56b5u);
}

static void test_sgb_uses_cgb_compatibility_palette_as_initial_colors(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    memset(&rom[0x0134], 0, 16u);
    memcpy(&rom[0x0134], "POKEMON RED", 11u);
    rom[0x0143] = 0x00u;
    rom[0x0146] = 0x03u;
    rom[0x014bu] = 0x01u;
    rom[0x014d] = compute_header_checksum(rom);

    cupid_gb_set_model(&gb, CUPID_GB_MODEL_SGB);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    assert(gb.sgb.enabled);
    assert(gb.sgb.screen_palettes[0] == 0x7fffu);
    assert(gb.sgb.screen_palettes[1] == 0x421fu);
    assert(gb.sgb.screen_palettes[2] == 0x1cf2u);
}

static void test_cgb_double_speed_slows_timer_domain(void)
{
    enum { CUPID_GB_SPEED_TEST_STEPS = 128 };
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int index;
    uint8_t div_before;

    build_cgb_speed_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    assert(cupid_gb_step(&gb));
    assert(cupid_gb_step(&gb));
    assert(cupid_gb_step(&gb));
    assert(gb.double_speed);

    div_before = cupid_gb_read_u8(&gb, 0xff04u);

    for (index = 0u; index < CUPID_GB_SPEED_TEST_STEPS; ++index) {
        assert(cupid_gb_step(&gb));
    }

    assert((uint8_t)(cupid_gb_read_u8(&gb, 0xff04u) - div_before) == 0x02u);
}

static void test_ei_sequence_enables_interrupts_after_next_instruction(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    unsigned int steps;

    build_ei_sequence_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    for (steps = 0u; steps < 32u && !gb.cpu.halted; ++steps) {
        assert(cupid_gb_step(&gb));
    }

    assert(gb.cpu.halted);
    assert(cupid_gb_read_u8(&gb, 0xc000u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xc001u) == 0x01u);
}

static void test_interrupt_ie_push_can_cancel_dispatch(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cpu.pc = 0x0214u;
    gb.cpu.sp = 0x0000u;
    gb.cpu.ime = true;
    gb.interrupt_enable = 0x04u;
    gb.interrupt_flags = 0x04u;

    assert(cupid_gb_service_interrupt(&gb));
    assert(!gb.cpu.ime);
    assert(gb.cpu.sp == 0xfffeu);
    assert(gb.interrupt_enable == 0x02u);
    assert((gb.interrupt_flags & 0x1fu) == 0x04u);
    assert(gb.cpu.pc == 0x0000u);
}

static void test_interrupt_ie_push_can_reroute_dispatch(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cpu.pc = 0x0253u;
    gb.cpu.sp = 0x0000u;
    gb.cpu.ime = true;
    gb.interrupt_enable = 0x03u;
    gb.interrupt_flags = 0x03u;

    assert(cupid_gb_service_interrupt(&gb));
    assert(!gb.cpu.ime);
    assert(gb.cpu.sp == 0xfffeu);
    assert(gb.interrupt_enable == 0x02u);
    assert((gb.interrupt_flags & 0x1fu) == 0x01u);
    assert(gb.cpu.pc == 0x0048u);
}

static void test_interrupt_ie_push_low_byte_is_too_late_to_cancel(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cpu.pc = 0x0235u;
    gb.cpu.sp = 0x0001u;
    gb.cpu.ime = true;
    gb.interrupt_enable = 0x08u;
    gb.interrupt_flags = 0x08u;

    assert(cupid_gb_service_interrupt(&gb));
    assert(!gb.cpu.ime);
    assert(gb.cpu.sp == 0xffffu);
    assert(gb.interrupt_enable == 0x35u);
    assert((gb.interrupt_flags & 0x1fu) == 0x00u);
    assert(gb.cpu.pc == 0x0058u);
}

static void test_dmg_oam_bug_inc_de(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    size_t index;
    uint8_t original_row[8];

    build_test_rom(rom, sizeof(rom));
    rom[0x0100] = 0x13u; /* INC DE */
    rom[0x0101] = 0x76u; /* HALT */
    rom[0x0143] = 0x80u; /* dual-mode cart should still run as DMG */
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    for (index = 0u; index < 0x00a0u; ++index) {
        gb.object_attribute_memory[index] = (uint8_t)index;
    }

    gb.cpu.d = 0xfeu;
    gb.cpu.e = 0x00u;
    gb.io_registers[CUPID_GB_IO_LY] = 0u;
    gb.io_registers[CUPID_GB_IO_STAT] =
        (uint8_t)((gb.io_registers[CUPID_GB_IO_STAT] & (uint8_t)~0x03u) | 0x02u);
    gb.ppu_counter = 3u;

    memcpy(original_row, &gb.object_attribute_memory[32], sizeof(original_row));

    assert(cupid_gb_step(&gb));

    assert(gb.cpu.d == 0xfeu && gb.cpu.e == 0x01u);
    for (index = 2u; index < 8u; ++index) {
        assert(gb.object_attribute_memory[32u + index] == gb.object_attribute_memory[24u + index]);
        assert(gb.object_attribute_memory[32u + index] != original_row[index]);
    }
}

static void test_dmg_oam_bug_pop_bc_fdff(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    size_t index;
    uint8_t original_row24[8];
    uint8_t original_row32[8];

    build_test_rom(rom, sizeof(rom));
    rom[0x0100] = 0xc1u; /* POP BC */
    rom[0x0101] = 0x76u; /* HALT */
    rom[0x0143] = 0x80u; /* dual-mode cart should still run as DMG */
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    for (index = 0u; index < 0x00a0u; ++index) {
        gb.object_attribute_memory[index] = (uint8_t)index;
    }

    gb.cpu.sp = 0xfdffu;
    gb.io_registers[CUPID_GB_IO_LY] = 0u;
    gb.io_registers[CUPID_GB_IO_STAT] =
        (uint8_t)((gb.io_registers[CUPID_GB_IO_STAT] & (uint8_t)~0x03u) | 0x02u);
    gb.ppu_counter = 3u;

    memcpy(original_row24, &gb.object_attribute_memory[24], sizeof(original_row24));
    memcpy(original_row32, &gb.object_attribute_memory[32], sizeof(original_row32));

    assert(cupid_gb_step(&gb));

    assert(gb.cpu.sp == 0xfe01u);
    for (index = 0u; index < 8u; ++index) {
        assert(gb.object_attribute_memory[24u + index] == gb.object_attribute_memory[32u + index]);
    }
    assert(memcmp(&gb.object_attribute_memory[24], original_row24, sizeof(original_row24)) != 0);
}

static void test_dmg_oam_bug_push_bc_fef0(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    size_t index;
    uint8_t original_row24[8];
    uint8_t original_row32[8];
    uint8_t original_row48[8];

    build_test_rom(rom, sizeof(rom));
    rom[0x0100] = 0xc5u; /* PUSH BC */
    rom[0x0101] = 0x76u; /* HALT */
    rom[0x0143] = 0x80u; /* dual-mode cart should still run as DMG */
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    for (index = 0u; index < 0x00a0u; ++index) {
        gb.object_attribute_memory[index] = (uint8_t)index;
    }

    gb.cpu.sp = 0xfef0u;
    gb.io_registers[CUPID_GB_IO_LY] = 0u;
    gb.io_registers[CUPID_GB_IO_STAT] =
        (uint8_t)((gb.io_registers[CUPID_GB_IO_STAT] & (uint8_t)~0x03u) | 0x02u);
    gb.ppu_counter = 3u;

    memcpy(original_row24, &gb.object_attribute_memory[24], sizeof(original_row24));
    memcpy(original_row32, &gb.object_attribute_memory[32], sizeof(original_row32));
    memcpy(original_row48, &gb.object_attribute_memory[48], sizeof(original_row48));

    assert(cupid_gb_step(&gb));

    assert(gb.cpu.sp == 0xfeeeu);
    for (index = 0u; index < 8u; ++index) {
        assert(gb.object_attribute_memory[32u + index] == original_row24[index]);
        assert(gb.object_attribute_memory[40u + index] == original_row24[index]);
        assert(gb.object_attribute_memory[32u + index] != original_row32[index]);
        assert(gb.object_attribute_memory[48u + index] == original_row48[index]);
    }
}

static void test_halt_bug_repeats_next_opcode_fetch(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x0100] = 0xf3u; /* DI */
    rom[0x0101] = 0x3eu; /* LD A,$01 */
    rom[0x0102] = 0x01u;
    rom[0x0103] = 0xe0u; /* LDH ($0F),A */
    rom[0x0104] = 0x0fu;
    rom[0x0105] = 0xeau; /* LD ($FFFF),A */
    rom[0x0106] = 0xffu;
    rom[0x0107] = 0xffu;
    rom[0x0108] = 0x76u; /* HALT */
    rom[0x0109] = 0x3eu; /* LD A,$99 */
    rom[0x010a] = 0x99u;
    rom[0x010b] = 0x76u; /* HALT */
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    assert(cupid_gb_step(&gb));
    assert(cupid_gb_step(&gb));
    assert(cupid_gb_step(&gb));
    assert(cupid_gb_step(&gb));
    assert(gb.interrupt_flags == 0x01u);
    assert(gb.interrupt_enable == 0x01u);

    assert(cupid_gb_step(&gb));
    assert(!gb.cpu.halted);
    assert(gb.cpu.halt_bug);
    assert(gb.cpu.pc == 0x0109u);

    assert(cupid_gb_step(&gb));
    assert(gb.cpu.a == 0x3eu);
    assert(gb.cpu.pc == 0x010au);
    assert(!gb.cpu.halt_bug);
}

static void test_halt_services_interrupt_requested_during_halt_cycle(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    uint16_t initial_sp;
    uint16_t initial_pc;

    build_test_rom(rom, sizeof(rom));
    rom[0x0050] = 0x76u; /* HALT at timer vector */
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cpu.halted = true;
    gb.cpu.ime = true;
    gb.interrupt_enable = CUPID_GB_INTERRUPT_TIMER;
    gb.interrupt_flags = 0u;
    gb.tima_overflow_delay = 1u;

    initial_sp = gb.cpu.sp;
    initial_pc = gb.cpu.pc;

    assert(cupid_gb_step(&gb));

    assert(!gb.cpu.halted);
    assert(!gb.cpu.ime);
    assert(gb.cpu.pc == 0x0050u);
    assert(gb.cpu.sp == (uint16_t)(initial_sp - 2u));
    assert((gb.interrupt_flags & CUPID_GB_INTERRUPT_TIMER) == 0u);
    assert(cupid_gb_read_u8(&gb, gb.cpu.sp) == (uint8_t)(initial_pc & 0xffu));
    assert(cupid_gb_read_u8(&gb, (uint16_t)(gb.cpu.sp + 1u)) == (uint8_t)(initial_pc >> 8u));
}

static void test_halt_ime0_wakes_and_executes_without_extra_step(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x0100] = 0x04u; /* INC B */
    rom[0x0101] = 0x76u; /* HALT */
    rom[0x014d] = compute_header_checksum(rom);

    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cpu.halted = true;
    gb.cpu.ime = false;
    gb.cpu.b = 0x12u;
    gb.interrupt_enable = CUPID_GB_INTERRUPT_TIMER;
    gb.interrupt_flags = 0u;
    gb.tima_overflow_delay = 1u;

    assert(cupid_gb_step(&gb));

    assert(!gb.cpu.halted);
    assert(gb.cpu.b == 0x13u);
    assert(gb.cpu.pc == 0x0101u);
    assert((gb.interrupt_flags & CUPID_GB_INTERRUPT_TIMER) != 0u);
}

static void test_apu_register_read_masks(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0xff10u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff11u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff12u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff15u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff1au, 0x00u);
    cupid_gb_write_u8(&gb, 0xff1cu, 0x00u);
    cupid_gb_write_u8(&gb, 0xff24u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff25u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff27u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff30u, 0x5au);

    assert(cupid_gb_read_u8(&gb, 0xff10u) == 0x80u);
    assert(cupid_gb_read_u8(&gb, 0xff11u) == 0x3fu);
    assert(cupid_gb_read_u8(&gb, 0xff12u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xff15u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff1au) == 0x7fu);
    assert(cupid_gb_read_u8(&gb, 0xff1cu) == 0x9fu);
    assert(cupid_gb_read_u8(&gb, 0xff24u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xff25u) == 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xff27u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff30u) == 0x5au);
}

static void test_apu_writes_ignored_while_off(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0xff26u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff10u, 0xffu);
    cupid_gb_write_u8(&gb, 0xff11u, 0xffu);
    cupid_gb_write_u8(&gb, 0xff20u, 0xffu);
    cupid_gb_write_u8(&gb, 0xff30u, 0x37u);

    assert(cupid_gb_read_u8(&gb, 0xff10u) == 0x80u);
    assert(cupid_gb_read_u8(&gb, 0xff11u) == 0x3fu);
    assert(cupid_gb_read_u8(&gb, 0xff20u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff30u) == 0x37u);
}

static void test_apu_dmg_length_load_writes_work_while_off(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cgb_mode = false;

    cupid_gb_write_u8(&gb, 0xff26u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff11u, (uint8_t)-0x11);
    cupid_gb_write_u8(&gb, 0xff16u, (uint8_t)-0x22);
    cupid_gb_write_u8(&gb, 0xff1bu, (uint8_t)-0x44);
    cupid_gb_write_u8(&gb, 0xff20u, (uint8_t)-0x33);

    assert(gb.apu.ch1_len == 0x11u);
    assert(gb.apu.ch2_len == 0x22u);
    assert(gb.apu.ch3_len == 0x44u);
    assert(gb.apu.ch4_len == 0x33u);

    assert(cupid_gb_read_u8(&gb, 0xff11u) == 0x3fu);
    assert(cupid_gb_read_u8(&gb, 0xff16u) == 0x3fu);
    assert(cupid_gb_read_u8(&gb, 0xff1bu) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff20u) == 0xffu);
}

static void test_apu_length_enable_clocks_in_first_half(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.apu.fs_step = 1u;
    gb.apu.ch2_len_en = false;
    gb.apu.ch2_len = 2u;
    gb.apu.ch2_on = true;
    gb.io_registers[0x17u] = 0x08u;
    gb.apu.ch2_dac = true;

    cupid_gb_write_u8(&gb, 0xff19u, 0x40u);

    assert(gb.apu.ch2_len_en);
    assert(gb.apu.ch2_len == 1u);
    assert(gb.apu.ch2_on);
}

static void test_apu_trigger_zero_length_reloads_and_clocks(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.apu.fs_step = 1u;
    gb.apu.ch2_len_en = true;
    gb.apu.ch2_len = 0u;
    gb.apu.ch2_on = false;
    gb.io_registers[0x17u] = 0x08u;
    gb.apu.ch2_dac = true;

    cupid_gb_write_u8(&gb, 0xff19u, 0xc0u);

    assert(gb.apu.ch2_len == 63u);
    assert(gb.apu.ch2_on);
}

static void test_apu_sweep_trigger_copies_frequency_shadow(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0xff10u, 0x12u);
    cupid_gb_write_u8(&gb, 0xff12u, 0x08u);
    cupid_gb_write_u8(&gb, 0xff13u, 0x04u);
    cupid_gb_write_u8(&gb, 0xff14u, 0x80u);

    assert(gb.apu.ch1_sweep_shadow == 0x0004u);

    cupid_gb_write_u8(&gb, 0xff13u, 0x00u);
    assert(gb.apu.ch1_freq == 0x0000u);
    assert(gb.apu.ch1_sweep_shadow == 0x0004u);

    gb.apu.fs_step = 2u;
    gb.apu.fs_counter = 0u;
    cupid_gb_tick_apu(&gb, 1u);

    assert(gb.apu.ch1_freq == 0x0005u);
    assert(gb.apu.ch1_sweep_shadow == 0x0005u);
    assert((gb.io_registers[0x13u] | ((uint16_t)(gb.io_registers[0x14u] & 0x07u) << 8u)) == 0x0005u);
}

static void test_apu_dmg_power_off_preserves_length_counters(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cgb_mode = false;
    gb.apu.ch1_len = 0x11u;
    gb.apu.ch2_len = 0x22u;
    gb.apu.ch3_len = 0x44u;
    gb.apu.ch4_len = 0x33u;
    gb.apu.ch1_len_en = true;
    gb.apu.ch2_len_en = true;
    gb.apu.ch3_len_en = true;
    gb.apu.ch4_len_en = true;
    gb.apu.ch1_on = true;
    gb.apu.ch2_on = true;
    gb.apu.ch3_on = true;
    gb.apu.ch4_on = true;

    cupid_gb_write_u8(&gb, 0xff26u, 0x00u);
    cupid_gb_tick_apu(&gb, 8192u * 8u);
    cupid_gb_write_u8(&gb, 0xff26u, 0x80u);
    cupid_gb_tick_apu(&gb, 2048u);

    assert(gb.apu.ch1_len == 0x11u);
    assert(gb.apu.ch2_len == 0x22u);
    assert(gb.apu.ch3_len == 0x44u);
    assert(gb.apu.ch4_len == 0x33u);
    assert(!gb.apu.ch1_len_en);
    assert(!gb.apu.ch2_len_en);
    assert(!gb.apu.ch3_len_en);
    assert(!gb.apu.ch4_len_en);

    cupid_gb_write_u8(&gb, 0xff17u, 0x08u);
    cupid_gb_write_u8(&gb, 0xff19u, 0xc0u);
    assert(gb.apu.ch2_len == 0x22u);

    cupid_gb_write_u8(&gb, 0xff12u, 0x08u);
    cupid_gb_write_u8(&gb, 0xff14u, 0xc0u);
    assert(gb.apu.ch1_len == 0x11u);

    cupid_gb_write_u8(&gb, 0xff1au, 0x80u);
    cupid_gb_write_u8(&gb, 0xff1eu, 0xc0u);
    assert(gb.apu.ch3_len == 0x44u);

    cupid_gb_write_u8(&gb, 0xff21u, 0x08u);
    cupid_gb_write_u8(&gb, 0xff23u, 0xc0u);
    assert(gb.apu.ch4_len == 0x33u);
}

static void test_apu_cgb_power_on_resets_length_counters(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cgb_mode = true;
    gb.apu.ch1_len = 0x11u;
    gb.apu.ch2_len = 0x22u;
    gb.apu.ch3_len = 0x44u;
    gb.apu.ch4_len = 0x33u;

    cupid_gb_write_u8(&gb, 0xff26u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff20u, (uint8_t)-0x33);
    assert(gb.apu.ch4_len == 0x33u);

    cupid_gb_write_u8(&gb, 0xff26u, 0x80u);

    assert(gb.apu.ch1_len == 0u);
    assert(gb.apu.ch2_len == 0u);
    assert(gb.apu.ch3_len == 0u);
    assert(gb.apu.ch4_len == 0u);
}

static void test_apu_cgb_wave_ram_access_while_on_uses_current_byte(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    uint16_t address;

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cgb_mode = true;
    gb.apu.apu_on = true;
    gb.apu.ch3_on = true;
    gb.apu.ch3_wave_pos = 9u;
    gb.apu.ch3_current_byte = 4u;
    gb.apu.ch3_started = true;

    for (address = 0xff30u; address <= 0xff3fu; ++address) {
        gb.io_registers[address - 0xff00u] = (uint8_t)(address - 0xff20u);
    }

    for (address = 0xff30u; address <= 0xff3fu; ++address) {
        assert(cupid_gb_read_u8(&gb, address) == 0x14u);
    }

    cupid_gb_write_u8(&gb, 0xff30u, 0xbcu);
    assert(cupid_gb_read_u8(&gb, 0xff34u) == 0xbcu);
}

static void test_apu_dmg_wave_ram_access_while_on_requires_recent_fetch(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cgb_mode = false;
    gb.apu.apu_on = true;
    gb.apu.ch3_on = true;
    gb.apu.ch3_current_byte = 4u;
    gb.io_registers[0x34u] = 0x44u;
    gb.io_registers[0x35u] = 0x55u;

    gb.apu.ch3_started = true;
    gb.apu.ch3_wave_access_ticks = 0u;
    assert(cupid_gb_read_u8(&gb, 0xff30u) == 0xffu);
    assert(cupid_gb_read_u8(&gb, 0xff3cu) == 0xffu);
    cupid_gb_write_u8(&gb, 0xff35u, 0xa5u);
    assert(gb.io_registers[0x34u] == 0x44u);
    assert(gb.io_registers[0x35u] == 0x55u);

    gb.apu.ch3_wave_access_ticks = 1u;
    assert(cupid_gb_read_u8(&gb, 0xff30u) == 0x44u);
    assert(cupid_gb_read_u8(&gb, 0xff3cu) == 0x44u);
    cupid_gb_write_u8(&gb, 0xff35u, 0xa5u);
    assert(gb.io_registers[0x34u] == 0xa5u);
}

static void test_apu_dmg_wave_trigger_while_on_corrupts_wave_ram(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    uint8_t i;

    build_test_rom(rom, sizeof(rom));
    rom[0x014d] = compute_header_checksum(rom);
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cgb_mode = false;
    gb.apu.apu_on = true;
    gb.apu.ch3_on = true;
    gb.apu.ch3_dac = true;
    gb.apu.ch3_started = true;
    gb.apu.ch3_wave_pos = 1u;
    gb.apu.ch3_current_byte = 0u;
    gb.apu.ch3_timer = 2u;
    gb.io_registers[0x1au] = 0x80u;

    for (i = 0u; i < 16u; ++i) {
        gb.io_registers[0x30u + i] = (uint8_t)(i * 0x11u);
    }

    cupid_gb_apu_on_write(&gb, 0x1eu, 0x87u);

    assert(gb.io_registers[0x30u] == 0x11u);
    assert(gb.io_registers[0x31u] == 0x11u);
    assert(gb.apu.ch3_on);
    assert(!gb.apu.ch3_started);
    assert(gb.apu.ch3_wave_pos == 0u);
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

    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff40u, 0x91u);
    /* After LCD enable, ppu_counter starts at 1, so the first scanline
       completes in 113 M-cycles. After 114 steps LY=1, still in OAM. */
    step_gb(&gb, CUPID_GB_STEPS_PER_SCANLINE);

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

    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff40u, 0x91u);
    step_gb(&gb, CUPID_GB_STEPS_PER_SCANLINE * CUPID_GB_VISIBLE_SCANLINES);

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
    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);

    for (index = 0u; index < 0xa0u; ++index) {
        cupid_gb_write_u8(&gb, (uint16_t)(0xc000u + index), (uint8_t)(index ^ 0x5au));
    }

    cupid_gb_write_u8(&gb, 0xff46u, 0xc0u);

    cupid_gb_tick(&gb, 1u);
    assert(!gb.dma_active);
    assert(gb.dma_start_delay == 1u);

    cupid_gb_tick(&gb, 1u);
    assert(gb.dma_active);
    assert(gb.dma_start_delay == 0u);
    assert(gb.dma_index == 0u);
    assert(cupid_gb_read_u8(&gb, 0xfe00u) == 0xffu);

    cupid_gb_tick(&gb, 1u);
    assert(gb.dma_active);
    assert(gb.dma_index == 1u);
    assert(cupid_gb_read_u8(&gb, 0xfe00u) == 0xffu);

    cupid_gb_tick(&gb, 159u);
    assert(!gb.dma_active);

    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);

    for (index = 0u; index < 0xa0u; ++index) {
        assert(cupid_gb_read_u8(&gb, (uint16_t)(0xfe00u + index)) == (uint8_t)(index ^ 0x5au));
    }
}

static void test_ppu_dma_restart_extends_blocking_until_restart_completes(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);

    cupid_gb_write_u8(&gb, 0x8000u, 0x01u);
    cupid_gb_write_u8(&gb, 0xff46u, 0x80u);

    cupid_gb_tick(&gb, 8u);
    cupid_gb_write_u8(&gb, 0xff46u, 0x80u);

    cupid_gb_tick(&gb, 160u);
    assert(gb.dma_active || gb.dma_start_delay > 0u);
    assert(cupid_gb_read_u8(&gb, 0xfe00u) == 0xffu);

    cupid_gb_tick(&gb, 1u);
    assert(gb.dma_active || gb.dma_start_delay > 0u);
    assert(cupid_gb_read_u8(&gb, 0xfe00u) == 0xffu);

    cupid_gb_tick(&gb, 1u);
    assert(!gb.dma_active);
    assert(gb.dma_start_delay == 0u);
    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);
    assert(cupid_gb_read_u8(&gb, 0xfe00u) == 0x01u);
}

static void test_ppu_dma_fe00_source_uses_wram_echo(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    size_t index;

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);

    for (index = 0u; index < 0xa0u; ++index) {
        cupid_gb_write_u8(&gb, (uint16_t)(0xde00u + index), (uint8_t)(index ^ 0x96u));
        cupid_gb_write_u8(&gb, (uint16_t)(0xfe00u + index), 0x00u);
    }

    cupid_gb_write_u8(&gb, 0xff46u, 0xfeu);
    cupid_gb_tick(&gb, 162u);

    for (index = 0u; index < 0xa0u; ++index) {
        assert(cupid_gb_read_u8(&gb, (uint16_t)(0xfe00u + index)) == (uint8_t)(index ^ 0x96u));
    }
}

static void test_ppu_dma_ff00_source_uses_wram_echo(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    size_t index;

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));
    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);

    for (index = 0u; index < 0xa0u; ++index) {
        cupid_gb_write_u8(&gb, (uint16_t)(0xdf00u + index), (uint8_t)(index ^ 0x5au));
        cupid_gb_write_u8(&gb, (uint16_t)(0xfe00u + index), 0x00u);
    }

    cupid_gb_write_u8(&gb, 0xff46u, 0xffu);
    cupid_gb_tick(&gb, 162u);

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

    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff47u, 0xe4u);
    cupid_gb_write_u8(&gb, 0x8000u, 0x55u);
    cupid_gb_write_u8(&gb, 0x8001u, 0x33u);
    cupid_gb_write_u8(&gb, 0x9800u, 0x00u);
    cupid_gb_write_u8(&gb, 0xff40u, 0x91u);

    step_gb(&gb, CUPID_GB_STEPS_UNTIL_HBLANK);

    for (index = 0u; index < 8u; ++index) {
        assert(gb.frame_buffer[index] == expected[index]);
    }

    for (index = 0u; index < 8u; ++index) {
        assert(gb.frame_buffer[index] == expected[index]);
    }
}

static void test_ppu_mode3_length_depends_on_scx(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.io_registers[CUPID_GB_IO_LCDC] = 0x91u;
    gb.io_registers[CUPID_GB_IO_LY] = 1u;

    gb.io_registers[CUPID_GB_IO_SCX] = 0u;
    gb.ppu_counter = CUPID_GB_PPU_OAM_CYCLES;
    cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_TRANSFER);
    cupid_gb_tick_ppu(&gb, CUPID_GB_PPU_TRANSFER_CYCLES);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_TRANSFER);
    cupid_gb_tick_ppu(&gb, 1u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_HBLANK);

    gb.io_registers[CUPID_GB_IO_LY] = 1u;
    gb.io_registers[CUPID_GB_IO_SCX] = 1u;
    gb.ppu_counter = CUPID_GB_PPU_OAM_CYCLES;
    cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_TRANSFER);
    cupid_gb_tick_ppu(&gb, CUPID_GB_PPU_TRANSFER_CYCLES + 1u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_TRANSFER);
    cupid_gb_tick_ppu(&gb, 1u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_HBLANK);

    gb.io_registers[CUPID_GB_IO_LY] = 1u;
    gb.io_registers[CUPID_GB_IO_SCX] = 5u;
    gb.ppu_counter = CUPID_GB_PPU_OAM_CYCLES;
    cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_TRANSFER);
    cupid_gb_tick_ppu(&gb, CUPID_GB_PPU_TRANSFER_CYCLES + 2u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_TRANSFER);
    cupid_gb_tick_ppu(&gb, 1u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_HBLANK);
}

static void test_ppu_hblank_stat_interrupt_is_requested_on_transition(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.io_registers[CUPID_GB_IO_LCDC] = 0x91u;
    gb.io_registers[CUPID_GB_IO_LY] = 0u;
    gb.io_registers[CUPID_GB_IO_SCX] = 0u;
    gb.io_registers[CUPID_GB_IO_STAT] = 0x88u;
    gb.interrupt_flags = 0u;
    gb.stat_irq_line = false;
    gb.stat_irq_delay = 0u;
    gb.ppu_counter = CUPID_GB_PPU_OAM_CYCLES;
    cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_TRANSFER);

    cupid_gb_tick_ppu(&gb, CUPID_GB_PPU_TRANSFER_CYCLES);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_TRANSFER);

    cupid_gb_tick_ppu(&gb, 1u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_HBLANK);
    assert((gb.interrupt_flags & CUPID_GB_INTERRUPT_LCD_STAT) != 0u);
    assert(gb.stat_irq_delay == 0u);
}

static void test_ppu_mode3_length_includes_left_edge_sprite_penalty(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.io_registers[CUPID_GB_IO_LCDC] = 0x93u;
    gb.io_registers[CUPID_GB_IO_LY] = 1u;
    gb.io_registers[CUPID_GB_IO_SCX] = 0u;
    gb.object_attribute_memory[0] = 17u;
    gb.object_attribute_memory[1] = 0u;
    gb.object_attribute_memory[2] = 0u;
    gb.object_attribute_memory[3] = 0u;
    gb.ppu_counter = CUPID_GB_PPU_OAM_CYCLES;
    cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_TRANSFER);

    cupid_gb_tick_ppu(&gb, CUPID_GB_PPU_TRANSFER_CYCLES + 2u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_TRANSFER);
    cupid_gb_tick_ppu(&gb, 1u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_HBLANK);
}

static void test_push_timing_matches_dma_edge_behavior(void)
{
    static const uint8_t code[] = {
        0x31u, 0x10u, 0xfeu,
        0x16u, 0x42u,
        0x1eu, 0x24u,
        0xf0u, 0x44u,
        0xfeu, 0x8fu,
        0x20u, 0xfau,
        0xf0u, 0x44u,
        0xfeu, 0x90u,
        0x20u, 0xfau,
        0x3eu, 0x80u,
        0xe0u, 0x46u,
        0x3eu, 0x27u,
        0x3du, 0x20u, 0xfdu,
        0x00u, 0x00u,
        0xd5u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0xe1u,
        0xf0u, 0x44u,
        0xfeu, 0x8fu,
        0x20u, 0xfau,
        0xf0u, 0x44u,
        0xfeu, 0x90u,
        0x20u, 0xfau,
        0x3eu, 0x80u,
        0xe0u, 0x46u,
        0x3eu, 0x27u,
        0x3du, 0x20u, 0xfdu,
        0x00u,
        0xd5u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0xd1u,
        0x76u
    };
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};
    size_t index;

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    cupid_gb_write_u8(&gb, 0xff40u, 0x00u);
    for (index = 0u; index < 0x20u; ++index) {
        cupid_gb_write_u8(&gb, (uint16_t)(0x8000u + index), 0x81u);
    }
    for (index = 0u; index < sizeof(code); ++index) {
        cupid_gb_write_u8(&gb, (uint16_t)(0xff80u + index), code[index]);
    }

    gb.cpu.pc = 0xff80u;
    gb.cpu.sp = 0xfffeu;
    gb.cpu.ime = false;
    gb.cpu.ime_delay = 0u;
    gb.cpu.halted = false;
    gb.cpu.stopped = false;
    gb.io_registers[CUPID_GB_IO_LCDC] = 0x91u;
    gb.io_registers[CUPID_GB_IO_LY] = 0x8fu;
    gb.ppu_counter = 0u;
    gb.ppu_lcd_startup = false;
    gb.ppu_lcd_warmup_lines = 0u;
    gb.ppu_line_boundary_hold = false;
    cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_OAM);

    for (index = 0u; index < 50000u && !gb.cpu.halted; ++index) {
        assert(cupid_gb_step(&gb));
    }

    assert(gb.cpu.halted);
    assert(gb.cpu.d == 0x81u);
    assert(gb.cpu.e == 0x24u);
    assert(gb.cpu.h == 0x42u);
    assert(gb.cpu.l == 0x24u);
}

static void test_ppu_oam_stat_interrupt_is_requested_on_oam_transition(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.io_registers[CUPID_GB_IO_LCDC] = 0x91u;
    gb.io_registers[CUPID_GB_IO_LY] = 43u;
    gb.io_registers[CUPID_GB_IO_STAT] = 0xa0u;
    gb.interrupt_flags = 0u;
    gb.stat_irq_line = false;
    gb.stat_irq_delay = 0u;
    gb.ppu_lcd_startup = false;
    gb.ppu_lcd_warmup_lines = 0u;
    gb.ppu_line_boundary_hold = false;
    gb.ppu_counter = (uint16_t)(CUPID_GB_PPU_SCANLINE_CYCLES - 1u);
    cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_HBLANK);

    cupid_gb_tick_ppu(&gb, 1u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 44u);
    assert((gb.interrupt_flags & CUPID_GB_INTERRUPT_LCD_STAT) == 0u);
    assert(gb.stat_irq_delay == 0u);

    cupid_gb_tick_ppu(&gb, 1u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_OAM);
    assert((gb.interrupt_flags & CUPID_GB_INTERRUPT_LCD_STAT) != 0u);
    assert(gb.stat_irq_delay == 0u);
}

static void test_ppu_dmg_last_vblank_line_is_shorter(void)
{
    uint8_t rom[32u * 1024u];
    CupidGb gb = {0};

    build_idle_rom(rom, sizeof(rom));
    assert(cupid_gb_load_rom(&gb, rom, sizeof(rom)));

    gb.cgb_mode = false;
    gb.io_registers[CUPID_GB_IO_LCDC] = 0x91u;
    gb.io_registers[CUPID_GB_IO_LY] = 153u;
    gb.ppu_counter = (uint16_t)(CUPID_GB_PPU_SCANLINE_CYCLES - 3u);
    cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_VBLANK);

    cupid_gb_tick_ppu(&gb, 1u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 153u);

    cupid_gb_tick_ppu(&gb, 1u);
    assert(cupid_gb_read_u8(&gb, 0xff44u) == 0u);
    assert((cupid_gb_read_u8(&gb, 0xff41u) & 0x03u) == CUPID_GB_PPU_MODE_OAM);
}

static void build_workspace_path(char *path, size_t path_size, const char *relative_path)
{
    const char *tests_dir = strstr(__FILE__, "/tests/");
    size_t root_len;

    assert(tests_dir != NULL);
    root_len = (size_t)(tests_dir - __FILE__);
    assert(root_len + 1u + strlen(relative_path) + 1u <= path_size);

    memcpy(path, __FILE__, root_len);
    path[root_len] = '/';
    strcpy(path + root_len + 1u, relative_path);
}

static void assert_acceptance_rom_reaches_success(const char *relative_path,
                                                  uint16_t success_pc,
                                                  uint16_t failure_pc,
                                                  size_t max_steps)
{
    char path[1024];
    CupidGb gb = {0};
    size_t step;

    build_workspace_path(path, sizeof(path), relative_path);
    assert(cupid_gb_load_rom_file(&gb, path));

    for (step = 0u;
         step < max_steps && gb.cpu.pc != success_pc && gb.cpu.pc != failure_pc;
         ++step) {
        assert(cupid_gb_step(&gb));
    }

    assert(gb.cpu.pc == success_pc);
}

static void test_acceptance_ret_timing_roms_reach_success(void)
{
    assert_acceptance_rom_reaches_success(
        "mts-20240926-1737-443f6e1/acceptance/ret_timing.gb",
        0x4830u,
        0x483eu,
        150000u);
    assert_acceptance_rom_reaches_success(
        "mts-20240926-1737-443f6e1/acceptance/ret_cc_timing.gb",
        0x4830u,
        0x483eu,
        150000u);
}

    static void test_acceptance_rst_timing_rom_reaches_success(void)
    {
        assert_acceptance_rom_reaches_success(
        "mts-20240926-1737-443f6e1/acceptance/rst_timing.gb",
        0x4a6bu,
        0x4a79u,
        150000u);
    }

int main(void)
{
    test_parse_header();
    test_parse_mbc1_header();
    test_boot_model_profiles();
    test_boot_io_defaults();
    test_sgb_boot_div_phase_depends_on_header_stream();
    test_load_rom_file();
    test_battery_save_loads_for_valid_rom();
    test_battery_save_ignored_for_invalid_untitled_rom();
    test_mbc1_bank_switching();
    test_mbc1_multicart_bank_switching();
    test_mbc1_ram_banking_mode_switching();
    test_mbc2_address_decoding();
    test_load_32mb_mbc5_rom();
    test_load_64mb_mbc5_rom();
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
    test_ppu_dma_restart_extends_blocking_until_restart_completes();
    test_ppu_dma_fe00_source_uses_wram_echo();
    test_ppu_dma_ff00_source_uses_wram_echo();
    test_ppu_background_render();
    test_ppu_mode3_length_depends_on_scx();
    test_ppu_hblank_stat_interrupt_is_requested_on_transition();
    test_ppu_mode3_length_includes_left_edge_sprite_penalty();
    test_push_timing_matches_dma_edge_behavior();
    test_acceptance_ret_timing_roms_reach_success();
    test_acceptance_rst_timing_rom_reaches_success();
    test_ppu_oam_stat_interrupt_is_requested_on_oam_transition();
    test_ppu_dmg_last_vblank_line_is_shorter();
    test_dmg_oam_bug_inc_de();
    test_dmg_oam_bug_pop_bc_fdff();
    test_dmg_oam_bug_push_bc_fef0();
    test_halt_bug_repeats_next_opcode_fetch();
    test_halt_services_interrupt_requested_during_halt_cycle();
    test_halt_ime0_wakes_and_executes_without_extra_step();
    test_apu_register_read_masks();
    test_apu_writes_ignored_while_off();
    test_apu_dmg_length_load_writes_work_while_off();
    test_apu_length_enable_clocks_in_first_half();
    test_apu_trigger_zero_length_reloads_and_clocks();
    test_apu_sweep_trigger_copies_frequency_shadow();
    test_apu_dmg_power_off_preserves_length_counters();
    test_apu_cgb_power_on_resets_length_counters();
    test_apu_cgb_wave_ram_access_while_on_uses_current_byte();
    test_apu_dmg_wave_ram_access_while_on_requires_recent_fetch();
    test_apu_dmg_wave_trigger_while_on_corrupts_wave_ram();
    test_dual_mode_rom_stays_dmg();
    test_dual_mode_rom_enters_cgb_when_model_selected();
    test_cgb_vram_and_wram_banking();
    test_cgb_palette_registers_and_gdma();
    test_cgb_dmg_compatibility_palette_for_pokemon_red();
    test_cgb_dmg_compatibility_palette_requires_nintendo_licensee();
    test_sgb_uses_cgb_compatibility_palette_as_initial_colors();
    test_cgb_speed_switch();
    test_cgb_double_speed_slows_timer_domain();
    test_ei_sequence_enables_interrupts_after_next_instruction();
    test_interrupt_ie_push_can_cancel_dispatch();
    test_interrupt_ie_push_can_reroute_dispatch();
    test_interrupt_ie_push_low_byte_is_too_late_to_cancel();
    return 0;
}
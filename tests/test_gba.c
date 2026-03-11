#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cupid/core/emulator.h"
#include "cupid/gba/gba.h"
#include "cupid/gba/arm7tdmi.h"

static void test_gba_init_sets_power_on_state(void)
{
    CupidGba gba = {0};

    cupid_gba_init(&gba);

    assert(gba.rom != 0);
    assert(gba.key_input == 0x03ffu);
    assert(gba.display_control == 0x0080u);
    assert(gba.cpu.r[15] == 0x08000000u);
    assert(!gba.loaded);

    cupid_gba_cleanup(&gba);
}

static void test_gba_load_rom_maps_gamepak_space(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];
    unsigned int index;

    for (index = 0u; index < sizeof(rom); ++index) {
        rom[index] = (uint8_t)(index ^ 0x5au);
    }

    cupid_gba_init(&gba);
    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));
    assert(gba.loaded);
    assert(gba.rom_size == sizeof(rom));
    assert(cupid_gba_read_u8(&gba, 0x08000000u) == rom[0]);
    assert(cupid_gba_read_u8(&gba, 0x08000080u) == rom[0x80]);
    assert(gba.cpu.r[15] == 0x08000000u);

    cupid_gba_cleanup(&gba);
}

static void test_gba_memory_regions_round_trip(void)
{
    CupidGba gba = {0};

    cupid_gba_init(&gba);

    /* EWRAM, IWRAM, IO */
    cupid_gba_write_u8(&gba, 0x02000010u, 0x12u);
    cupid_gba_write_u8(&gba, 0x03000020u, 0x34u);
    cupid_gba_write_u8(&gba, 0x04000000u, 0x03u);
    cupid_gba_write_u8(&gba, 0x04000001u, 0x01u);

    assert(cupid_gba_read_u8(&gba, 0x02000010u) == 0x12u);
    assert(cupid_gba_read_u8(&gba, 0x03000020u) == 0x34u);
    assert(gba.display_control == 0x0103u);

    /* Palette RAM: 8-bit writes duplicate the byte to aligned halfword */
    cupid_gba_write_u8(&gba, 0x05000002u, 0x56u);
    assert(cupid_gba_read_u8(&gba, 0x05000002u) == 0x56u);
    assert(cupid_gba_read_u8(&gba, 0x05000003u) == 0x56u);

    /* VRAM: 8-bit writes in BG area duplicate the byte to aligned halfword */
    cupid_gba_write_u8(&gba, 0x06000004u, 0x78u);
    assert(cupid_gba_read_u8(&gba, 0x06000004u) == 0x78u);
    assert(cupid_gba_read_u8(&gba, 0x06000005u) == 0x78u);

    /* OAM: 8-bit writes are ignored on real hardware */
    cupid_gba_write_u8(&gba, 0x07000006u, 0x9au);
    assert(cupid_gba_read_u8(&gba, 0x07000006u) == 0x00u);

    /* 16-bit write to OAM does work */
    cupid_gba_write_u16(&gba, 0x07000006u, 0xBEEFu);
    assert(cupid_gba_read_u16(&gba, 0x07000006u) == 0xBEEFu);

    cupid_gba_cleanup(&gba);
}

static void test_gba_16_and_32_bit_bus(void)
{
    CupidGba gba = {0};

    cupid_gba_init(&gba);

    /* 16-bit round trip in EWRAM */
    cupid_gba_write_u16(&gba, 0x02000100u, 0xABCDu);
    assert(cupid_gba_read_u16(&gba, 0x02000100u) == 0xABCDu);
    assert(cupid_gba_read_u8(&gba, 0x02000100u) == 0xCDu);
    assert(cupid_gba_read_u8(&gba, 0x02000101u) == 0xABu);

    /* 32-bit round trip in IWRAM */
    cupid_gba_write_u32(&gba, 0x03000200u, 0xDEADBEEFu);
    assert(cupid_gba_read_u32(&gba, 0x03000200u) == 0xDEADBEEFu);
    assert(cupid_gba_read_u8(&gba, 0x03000200u) == 0xEFu);
    assert(cupid_gba_read_u8(&gba, 0x03000201u) == 0xBEu);
    assert(cupid_gba_read_u8(&gba, 0x03000202u) == 0xADu);
    assert(cupid_gba_read_u8(&gba, 0x03000203u) == 0xDEu);

    cupid_gba_cleanup(&gba);
}

static void test_gba_memory_region_mirrors(void)
{
    CupidGba gba = {0};
    uint8_t rom[512];
    unsigned int index;

    for (index = 0u; index < sizeof(rom); ++index) {
        rom[index] = (uint8_t)(index ^ 0xA5u);
    }

    cupid_gba_init(&gba);
    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    cupid_gba_write_u32(&gba, 0x02000000u, 0x11223344u);
    assert(cupid_gba_read_u32(&gba, 0x02040000u) == 0x11223344u);

    cupid_gba_write_u32(&gba, 0x03000000u, 0x55667788u);
    assert(cupid_gba_read_u32(&gba, 0x03008000u) == 0x55667788u);

    cupid_gba_write_u16(&gba, 0x04000000u, 0x1234u);
    assert(cupid_gba_read_u16(&gba, 0x04000400u) == 0x1234u);

    cupid_gba_write_u16(&gba, 0x05000000u, 0x7C1Fu);
    assert(cupid_gba_read_u16(&gba, 0x05000400u) == 0x7C1Fu);

    cupid_gba_write_u16(&gba, 0x06000000u, 0x2468u);
    assert(cupid_gba_read_u16(&gba, 0x06020000u) == 0x2468u);

    cupid_gba_write_u16(&gba, 0x06010000u, 0x1357u);
    assert(cupid_gba_read_u16(&gba, 0x06018000u) == 0x1357u);

    cupid_gba_write_u16(&gba, 0x07000000u, 0xBEEFu);
    assert(cupid_gba_read_u16(&gba, 0x07000400u) == 0xBEEFu);

    assert(cupid_gba_read_u32(&gba, 0x08000080u) == cupid_gba_read_u32(&gba, 0x0A000080u));
    assert(cupid_gba_read_u32(&gba, 0x08000100u) == cupid_gba_read_u32(&gba, 0x0C000100u));

    cupid_gba_cleanup(&gba);
}

static void test_gba_unmapped_gamepak_reads_follow_address_pattern(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    memset(rom, 0, sizeof(rom));
    rom[0] = 0x12u;
    rom[1] = 0x34u;

    cupid_gba_init(&gba);
    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    assert(cupid_gba_read_u8(&gba, 0x08000100u) == 0x80u);
    assert(cupid_gba_read_u8(&gba, 0x08000101u) == 0x00u);
    assert(cupid_gba_read_u16(&gba, 0x08000100u) == 0x0080u);

    cupid_gba_cleanup(&gba);
}

static void test_gba_io_dispstat_and_vcount(void)
{
    CupidGba gba = {0};

    cupid_gba_init(&gba);

    /* Write V-Counter match target via DISPSTAT high byte */
    cupid_gba_write_u8(&gba, 0x04000005u, 42u);
    assert((cupid_gba_read_u8(&gba, 0x04000005u)) == 42u);

    /* DISPSTAT low byte bits 0-2 are read-only (VBlank/HBlank/VCount flags) */
    gba.display_status = 0x0003u; /* set VBlank + HBlank */
    cupid_gba_write_u8(&gba, 0x04000004u, 0xF8u); /* try to set IRQ enable bits */
    /* Bits 0-2 should be preserved, bits 3-7 should be written */
    assert((cupid_gba_read_u8(&gba, 0x04000004u) & 0x07u) == 0x03u);
    assert((cupid_gba_read_u8(&gba, 0x04000004u) & 0xF8u) == 0xF8u);

    /* VCOUNT is read-only (set by PPU) */
    gba.vertical_count = 120u;
    assert(cupid_gba_read_u8(&gba, 0x04000006u) == 120u);

    cupid_gba_cleanup(&gba);
}

static void test_gba_interrupt_acknowledge(void)
{
    CupidGba gba = {0};

    cupid_gba_init(&gba);

    /* Set some interrupt flags */
    gba.interrupt_flags = 0x0003u; /* VBlank + HBlank pending */
    gba.interrupt_enable = 0x0003u;

    /* Acknowledge VBlank (bit 0) by writing 1 to IF */
    cupid_gba_write_u8(&gba, 0x04000202u, 0x01u);
    assert(gba.interrupt_flags == 0x0002u); /* only HBlank remains */

    /* IME write */
    cupid_gba_write_u8(&gba, 0x04000208u, 0x01u);
    assert(gba.interrupt_master == 0x0001u);

    cupid_gba_cleanup(&gba);
}

static void test_gba_timer_register_reads_reflect_live_counter(void)
{
    CupidGba gba = {0};
    uint8_t rom[32];

    memset(rom, 0, sizeof(rom));
    /* ARM NOPs */
    rom[0] = 0x00u; rom[1] = 0x00u; rom[2] = 0xA0u; rom[3] = 0xE1u;
    rom[4] = 0x00u; rom[5] = 0x00u; rom[6] = 0xA0u; rom[7] = 0xE1u;
    rom[8] = 0x00u; rom[9] = 0x00u; rom[10] = 0xA0u; rom[11] = 0xE1u;

    cupid_gba_init(&gba);
    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F;
    gba.cpu.pipeline_valid = false;

    cupid_gba_write_u16(&gba, 0x04000100u, 0xFFF0u);
    cupid_gba_write_u16(&gba, 0x04000102u, 0x0080u);

    cupid_gba_step(&gba);
    cupid_gba_step(&gba);
    cupid_gba_step(&gba);

    assert(cupid_gba_read_u16(&gba, 0x04000100u) == gba.timers[0].counter);
    assert(cupid_gba_read_u16(&gba, 0x04000102u) == gba.timers[0].control);

    cupid_gba_cleanup(&gba);
}

static void test_gba_dma3_immediate_copy(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    memset(rom, 0, sizeof(rom));
    rom[0x20] = 0x44u;
    rom[0x21] = 0x33u;
    rom[0x22] = 0x22u;
    rom[0x23] = 0x11u;
    rom[0x24] = 0x88u;
    rom[0x25] = 0x77u;
    rom[0x26] = 0x66u;
    rom[0x27] = 0x55u;

    cupid_gba_init(&gba);
    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    cupid_gba_write_u32(&gba, 0x040000D4u, 0x08000020u);
    cupid_gba_write_u32(&gba, 0x040000D8u, 0x02000000u);
    cupid_gba_write_u16(&gba, 0x040000DCu, 2u);
    cupid_gba_write_u16(&gba, 0x040000DEu, 0x8400u);

    assert(cupid_gba_read_u32(&gba, 0x02000000u) == 0x11223344u);
    assert(cupid_gba_read_u32(&gba, 0x02000004u) == 0x55667788u);

    cupid_gba_cleanup(&gba);
}

static void test_gba_flash128_save_commands(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    memset(rom, 0, sizeof(rom));
    memcpy(rom + 32u, "FLASH1M_V", 9u);

    cupid_gba_init(&gba);
    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    assert(cupid_gba_read_u8(&gba, 0x0E000000u) == 0xFFu);

    cupid_gba_write_u8(&gba, 0x0E005555u, 0xAAu);
    cupid_gba_write_u8(&gba, 0x0E002AAAu, 0x55u);
    cupid_gba_write_u8(&gba, 0x0E005555u, 0xA0u);
    cupid_gba_write_u8(&gba, 0x0E000020u, 0x11u);

    assert(cupid_gba_read_u8(&gba, 0x0E000020u) == 0x11u);
    assert(cupid_gba_read_u16(&gba, 0x0E000020u) == 0x1111u);
    assert(cupid_gba_read_u32(&gba, 0x0E000020u) == 0x11111111u);
    assert(cupid_gba_read_u8(&gba, 0x0E010020u) == 0x11u);
    assert(cupid_gba_read_u8(&gba, 0x0F000020u) == 0x11u);

    cupid_gba_write_u8(&gba, 0x0E005555u, 0xAAu);
    cupid_gba_write_u8(&gba, 0x0E002AAAu, 0x55u);
    cupid_gba_write_u8(&gba, 0x0E005555u, 0xA0u);
    cupid_gba_write_u16(&gba, 0x0E000041u, 0xAABBu);
    assert(cupid_gba_read_u8(&gba, 0x0E000041u) == 0xAAu);

    cupid_gba_write_u8(&gba, 0x0E005555u, 0xAAu);
    cupid_gba_write_u8(&gba, 0x0E002AAAu, 0x55u);
    cupid_gba_write_u8(&gba, 0x0E005555u, 0xA0u);
    cupid_gba_write_u32(&gba, 0x0E000043u, 0xAABBCCDDu);
    assert(cupid_gba_read_u8(&gba, 0x0E000043u) == 0xAAu);

    cupid_gba_write_u8(&gba, 0x0E005555u, 0xAAu);
    cupid_gba_write_u8(&gba, 0x0E002AAAu, 0x55u);
    cupid_gba_write_u8(&gba, 0x0E005555u, 0xB0u);
    cupid_gba_write_u8(&gba, 0x0E000000u, 0x01u);

    cupid_gba_write_u8(&gba, 0x0E005555u, 0xAAu);
    cupid_gba_write_u8(&gba, 0x0E002AAAu, 0x55u);
    cupid_gba_write_u8(&gba, 0x0E005555u, 0xA0u);
    cupid_gba_write_u8(&gba, 0x0E000020u, 0x22u);

    assert(cupid_gba_read_u8(&gba, 0x0E000020u) == 0x22u);

    cupid_gba_write_u8(&gba, 0x0E005555u, 0xAAu);
    cupid_gba_write_u8(&gba, 0x0E002AAAu, 0x55u);
    cupid_gba_write_u8(&gba, 0x0E005555u, 0xB0u);
    cupid_gba_write_u8(&gba, 0x0E000000u, 0x00u);

    assert(cupid_gba_read_u8(&gba, 0x0E000020u) == 0x11u);

    cupid_gba_cleanup(&gba);
}

static void test_gba_sram_save_commands(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    memset(rom, 0, sizeof(rom));
    memcpy(rom + 32u, "SRAM_V", 6u);

    cupid_gba_init(&gba);
    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    assert(cupid_gba_read_u8(&gba, 0x0E000000u) == 0xFFu);

    cupid_gba_write_u8(&gba, 0x0E000020u, 0x11u);
    assert(cupid_gba_read_u8(&gba, 0x0E010020u) == 0x11u);
    assert(cupid_gba_read_u8(&gba, 0x0F000020u) == 0x11u);

    cupid_gba_write_u8(&gba, 0x0E000000u, 0x22u);
    assert(cupid_gba_read_u8(&gba, 0x0E008000u) == 0x22u);

    cupid_gba_write_u8(&gba, 0x0E000040u, 0x33u);
    assert(cupid_gba_read_u16(&gba, 0x0E000040u) == 0x3333u);
    assert(cupid_gba_read_u32(&gba, 0x0E000040u) == 0x33333333u);

    cupid_gba_write_u16(&gba, 0x0E000041u, 0xAABBu);
    assert(cupid_gba_read_u8(&gba, 0x0E000041u) == 0xAAu);

    cupid_gba_write_u32(&gba, 0x0E000043u, 0xAABBCCDDu);
    assert(cupid_gba_read_u8(&gba, 0x0E000043u) == 0xAAu);

    cupid_gba_cleanup(&gba);
}

static void test_arm7_condition_checks(void)
{
    CupidArm7 cpu = {0};

    /* Test EQ (Z set) */
    cpu.cpsr = CUPID_ARM_FLAG_Z;
    assert(cupid_arm7_check_condition(&cpu, 0x0u)); /* EQ */
    assert(!cupid_arm7_check_condition(&cpu, 0x1u)); /* NE */

    /* Test CS/HS (C set) */
    cpu.cpsr = CUPID_ARM_FLAG_C;
    assert(cupid_arm7_check_condition(&cpu, 0x2u)); /* CS */
    assert(!cupid_arm7_check_condition(&cpu, 0x3u)); /* CC */

    /* Test MI (N set) */
    cpu.cpsr = CUPID_ARM_FLAG_N;
    assert(cupid_arm7_check_condition(&cpu, 0x4u)); /* MI */
    assert(!cupid_arm7_check_condition(&cpu, 0x5u)); /* PL */

    /* Test VS (V set) */
    cpu.cpsr = CUPID_ARM_FLAG_V;
    assert(cupid_arm7_check_condition(&cpu, 0x6u)); /* VS */
    assert(!cupid_arm7_check_condition(&cpu, 0x7u)); /* VC */

    /* Test AL (always) */
    cpu.cpsr = 0u;
    assert(cupid_arm7_check_condition(&cpu, 0xEu)); /* AL */
}

static void test_arm7_barrel_shift(void)
{
    bool carry = false;
    uint32_t result;

    /* LSL #0 is identity */
    result = cupid_arm7_barrel_shift(0x12345678u, 0u, 0u, &carry, false);
    assert(result == 0x12345678u);

    /* LSL #4 */
    result = cupid_arm7_barrel_shift(0x00000001u, 0u, 4u, &carry, false);
    assert(result == 0x00000010u);

    /* LSR #4 */
    result = cupid_arm7_barrel_shift(0x00000010u, 1u, 4u, &carry, false);
    assert(result == 0x00000001u);

    /* ASR #1 with sign bit set */
    result = cupid_arm7_barrel_shift(0x80000000u, 2u, 1u, &carry, false);
    assert(result == 0xC0000000u);

    /* ROR #4 */
    result = cupid_arm7_barrel_shift(0x0000000Fu, 3u, 4u, &carry, false);
    assert(result == 0xF0000000u);
}

static void test_arm7_register_shift_zero_preserves_carry(void)
{
    bool carry = true;
    uint32_t result;

    result = cupid_arm7_barrel_shift(1u, 0u, 0u, &carry, true);
    assert(result == 1u);
    assert(carry);

    result = cupid_arm7_barrel_shift(1u, 1u, 0u, &carry, true);
    assert(result == 1u);
    assert(carry);

    result = cupid_arm7_barrel_shift(1u, 2u, 0u, &carry, true);
    assert(result == 1u);
    assert(carry);

    result = cupid_arm7_barrel_shift(1u, 3u, 0u, &carry, true);
    assert(result == 1u);
    assert(carry);
}

static void test_arm7_mode_switching(void)
{
    CupidArm7 cpu = {0};

    /* Initialize in SVC mode (as after reset) */
    cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F;
    cpu.r[13] = 0x03007FE0u; /* SVC stack */
    cpu.r[14] = 0xDEADBEEFu;

    /* Switch from SVC to IRQ mode */
    cupid_arm7_switch_mode(&cpu, CUPID_ARM_MODE_IRQ);
    assert((cpu.cpsr & 0x1Fu) == CUPID_ARM_MODE_IRQ);

    /* The SVC SP/LR should have been banked */
    /* Now set IRQ SP/LR */
    cpu.r[13] = 0x03007FA0u; /* IRQ stack */
    cpu.r[14] = 0xCAFEBABEu;

    /* Switch back to SVC mode */
    cupid_arm7_switch_mode(&cpu, CUPID_ARM_MODE_SVC);
    assert((cpu.cpsr & 0x1Fu) == CUPID_ARM_MODE_SVC);
    assert(cpu.r[13] == 0x03007FE0u); /* SVC stack restored */
    assert(cpu.r[14] == 0xDEADBEEFu); /* SVC LR restored */
}

static void test_arm_mov_instruction(void)
{
    CupidGba gba = {0};
    cupid_gba_init(&gba);

    /* Place a MOV R0, #42 instruction at 0x08000000 */
    /* ARM encoding: E3A0002A  (AL, MOV, Rd=0, Imm=0x2A) */
    uint8_t rom[256];
    memset(rom, 0, sizeof(rom));
    rom[0] = 0x2Au; rom[1] = 0x00u; rom[2] = 0xA0u; rom[3] = 0xE3u;
    /* Place a NOP (MOV R0, R0) at 0x08000004 for the pipeline */
    rom[4] = 0x00u; rom[5] = 0x00u; rom[6] = 0xA0u; rom[7] = 0xE1u;
    rom[8] = 0x00u; rom[9] = 0x00u; rom[10] = 0xA0u; rom[11] = 0xE1u;

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    /* Set CPU to ARM mode at entry point */
    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F;
    gba.cpu.pipeline_valid = false;

    /* Step once — executes MOV R0, #42 */
    cupid_arm7_step(&gba);

    assert(gba.cpu.r[0] == 42u);

    cupid_gba_cleanup(&gba);
}

static void test_arm_mrs_spsr_in_system_mode_returns_cpsr(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    /* MRS R0, SPSR */
    rom[0] = 0x00u; rom[1] = 0x00u; rom[2] = 0x4Fu; rom[3] = 0xE1u; /* E14F0000 */
    rom[4] = 0x00u; rom[5] = 0x00u; rom[6] = 0xA0u; rom[7] = 0xE1u;
    rom[8] = 0x00u; rom[9] = 0x00u; rom[10] = 0xA0u; rom[11] = 0xE1u;

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = 0xF000001Fu;
    gba.cpu.pipeline_valid = false;

    cupid_arm7_step(&gba);

    assert(gba.cpu.r[0] == 0xF000001Fu);

    cupid_gba_cleanup(&gba);
}

static void test_arm_invalid_cpsr_mode_write_sets_bit4(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    /* MOV R0, #3 */
    rom[0] = 0x03u; rom[1] = 0x00u; rom[2] = 0xA0u; rom[3] = 0xE3u; /* E3A00003 */
    /* MSR CPSR_c, R0 */
    rom[4] = 0x00u; rom[5] = 0xF0u; rom[6] = 0x21u; rom[7] = 0xE1u; /* E121F000 */
    /* MRS R1, CPSR */
    rom[8] = 0x00u; rom[9] = 0x10u; rom[10] = 0x0Fu; rom[11] = 0xE1u; /* E10F1000 */
    rom[12] = 0x00u; rom[13] = 0x00u; rom[14] = 0xA0u; rom[15] = 0xE1u;
    rom[16] = 0x00u; rom[17] = 0x00u; rom[18] = 0xA0u; rom[19] = 0xE1u;

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = 0xF0000017u;
    gba.cpu.pipeline_valid = false;

    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);

    assert((gba.cpu.r[1] & 0xffu) == 0x13u);

    cupid_gba_cleanup(&gba);
}

static void test_arm_teqp_style_restores_cpsr_from_spsr(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    /* TEQP PC, R9  (encoded as TEQ with Rd=PC) */
    rom[0] = 0x09u; rom[1] = 0xF0u; rom[2] = 0x3Fu; rom[3] = 0xE1u; /* E13FF009 */
    rom[4] = 0x00u; rom[5] = 0x00u; rom[6] = 0xA0u; rom[7] = 0xE1u;
    rom[8] = 0x00u; rom[9] = 0x00u; rom[10] = 0xA0u; rom[11] = 0xE1u;

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = 0xF0000017u;
    gba.cpu.spsr[CUPID_ARM_BANK_ABT] = 0x0000009Fu;
    gba.cpu.r[9] = 8u;
    gba.cpu.pipeline_valid = false;

    cupid_arm7_step(&gba);

    assert((gba.cpu.cpsr & 0xffu) == 0x9Fu);
    assert((gba.cpu.r[15] & ~3u) == 0x08000004u);

    cupid_gba_cleanup(&gba);
}

static void test_arm_add_instruction(void)
{
    CupidGba gba = {0};
    cupid_gba_init(&gba);

    /* MOV R1, #10: E3A0100A */
    /* ADD R2, R1, #5: E2812005 */
    /* NOP: E1A00000 */
    uint8_t rom[256];
    memset(rom, 0, sizeof(rom));
    rom[0] = 0x0Au; rom[1] = 0x10u; rom[2] = 0xA0u; rom[3] = 0xE3u;
    rom[4] = 0x05u; rom[5] = 0x20u; rom[6] = 0x81u; rom[7] = 0xE2u;
    rom[8] = 0x00u; rom[9] = 0x00u; rom[10] = 0xA0u; rom[11] = 0xE1u;
    rom[12] = 0x00u; rom[13] = 0x00u; rom[14] = 0xA0u; rom[15] = 0xE1u;
    rom[16] = 0x00u; rom[17] = 0x00u; rom[18] = 0xA0u; rom[19] = 0xE1u;

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F;
    gba.cpu.pipeline_valid = false;

    /* Execute MOV R1, #10 */
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[1] == 10u);

    /* Execute ADD R2, R1, #5 */
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[2] == 15u);

    cupid_gba_cleanup(&gba);
}

static void test_arm_pc_operand_in_data_processing(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    /* ADD R0, PC, #4  -> with ARM PC semantics this yields 0x0800000C */
    rom[0] = 0x04u; rom[1] = 0x00u; rom[2] = 0x8Fu; rom[3] = 0xE2u; /* E28F0004 */
    /* CMP R0, PC -> should compare against 0x0800000C and set Z */
    rom[4] = 0x0Fu; rom[5] = 0x00u; rom[6] = 0x50u; rom[7] = 0xE1u; /* E150000F */
    /* NOPs */
    rom[8] = 0x00u; rom[9] = 0x00u; rom[10] = 0xA0u; rom[11] = 0xE1u;
    rom[12] = 0x00u; rom[13] = 0x00u; rom[14] = 0xA0u; rom[15] = 0xE1u;

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F;
    gba.cpu.pipeline_valid = false;

    cupid_arm7_step(&gba);
    assert(gba.cpu.r[0] == 0x0800000Cu);

    cupid_arm7_step(&gba);
    assert((gba.cpu.cpsr & CUPID_ARM_FLAG_Z) != 0u);

    cupid_gba_cleanup(&gba);
}

static void test_arm_ldm_user_bank_transfer_in_fiq_mode(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    /* LDMIA R1, {R8,R9}^ */
    rom[0] = 0x00u; rom[1] = 0x03u; rom[2] = 0xD1u; rom[3] = 0xE8u; /* E8D10300 */
    /* NOPs for pipeline fill */
    rom[4] = 0x00u; rom[5] = 0x00u; rom[6] = 0xA0u; rom[7] = 0xE1u;
    rom[8] = 0x00u; rom[9] = 0x00u; rom[10] = 0xA0u; rom[11] = 0xE1u;

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    cupid_gba_write_u32(&gba, 0x02000000u, 0x11223344u);
    cupid_gba_write_u32(&gba, 0x02000004u, 0x55667788u);

    gba.cpu.r[8] = 0xAAAAAAAAu;
    gba.cpu.r[9] = 0xBBBBBBBBu;
    cupid_arm7_switch_mode(&gba.cpu, CUPID_ARM_MODE_FIQ);
    gba.cpu.cpsr |= CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F;

    gba.cpu.r[8] = 0x11111111u;
    gba.cpu.r[9] = 0x22222222u;
    gba.cpu.r[1] = 0x02000000u;
    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.pipeline_valid = false;

    cupid_arm7_step(&gba);

    /* FIQ bank must remain visible in current mode. */
    assert(gba.cpu.r[8] == 0x11111111u);
    assert(gba.cpu.r[9] == 0x22222222u);
    /* User bank receives the loaded values. */
    assert(gba.cpu.r8_usr[0] == 0x11223344u);
    assert(gba.cpu.r8_usr[1] == 0x55667788u);

    cupid_gba_cleanup(&gba);
}

static void test_thumb_mov_instruction(void)
{
    CupidGba gba = {0};
    cupid_gba_init(&gba);

    /* Thumb: MOVS R3, #99  => 0x2363 */
    /* Thumb: NOP (MOV R8, R8) => 0x46C0 */
    uint8_t rom[256];
    memset(rom, 0, sizeof(rom));
    rom[0] = 0x63u; rom[1] = 0x23u;  /* MOVS R3, #99 */
    rom[2] = 0xC0u; rom[3] = 0x46u;  /* NOP */
    rom[4] = 0xC0u; rom[5] = 0x46u;  /* NOP */
    rom[6] = 0xC0u; rom[7] = 0x46u;  /* NOP */

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    /* Switch to Thumb mode */
    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F | CUPID_ARM_FLAG_T;
    gba.cpu.pipeline_valid = false;

    /* Execute MOVS R3, #99 */
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[3] == 99u);

    cupid_gba_cleanup(&gba);
}

static void test_thumb_pc_relative_load(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));
    rom[0] = 0x00u; rom[1] = 0x48u; /* LDR R0, [PC, #0] */
    rom[2] = 0xC0u; rom[3] = 0x46u; /* NOP */
    rom[4] = 0x78u; rom[5] = 0x56u; rom[6] = 0x34u; rom[7] = 0x12u;

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F | CUPID_ARM_FLAG_T;
    gba.cpu.pipeline_valid = false;

    cupid_arm7_step(&gba);

    assert(gba.cpu.r[0] == 0x12345678u);

    cupid_gba_cleanup(&gba);
}

static void test_thumb_add_pc_matches_mov_pc_sequence(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    rom[0] = 0x00u; rom[1] = 0x46u; /* MOV R0, R0 */
    rom[2] = 0x08u; rom[3] = 0xA0u; /* ADD R0, PC, #32 */
    rom[4] = 0x79u; rom[5] = 0x46u; /* MOV R1, PC */
    rom[6] = 0x1Cu; rom[7] = 0x31u; /* ADD R1, #28 */

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F | CUPID_ARM_FLAG_T;
    gba.cpu.pipeline_valid = false;

    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);

    assert(gba.cpu.r[0] == gba.cpu.r[1]);

    cupid_gba_cleanup(&gba);
}

static void test_thumb_misaligned_loads_rotate(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    rom[0] = 0x32u; rom[1] = 0x58u; /* LDR  R2, [R6, R0] */
    rom[2] = 0x32u; rom[3] = 0x5Au; /* LDRH R2, [R6, R0] */
    rom[4] = 0x32u; rom[5] = 0x5Eu; /* LDSH R2, [R6, R0] */

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    cupid_gba_write_u32(&gba, 0x02000000u, 0x000000FFu);
    cupid_gba_write_u16(&gba, 0x02000010u, 0x00FFu);
    cupid_gba_write_u16(&gba, 0x02000020u, 0xFF00u);

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F | CUPID_ARM_FLAG_T;
    gba.cpu.pipeline_valid = false;

    gba.cpu.r[6] = 0x02000000u;
    gba.cpu.r[0] = 1u;
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[2] == 0xFF000000u);

    gba.cpu.r[6] = 0x02000010u;
    gba.cpu.r[0] = 1u;
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[2] == 0xFF000000u);

    gba.cpu.r[6] = 0x02000020u;
    gba.cpu.r[0] = 1u;
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[2] == 0xFFFFFFFFu);

    cupid_gba_cleanup(&gba);
}

static void test_thumb_multiple_empty_rlist_semantics(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];
    uint32_t mem = 0x02000000u;

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    rom[0] = 0x00u; rom[1] = 0xC8u; /* LDMIA R0!, {} */
    rom[2] = 0x00u; rom[3] = 0xC1u; /* STMIA R1!, {} */

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    cupid_gba_write_u32(&gba, mem, 0x08000009u);

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F | CUPID_ARM_FLAG_T;
    gba.cpu.pipeline_valid = false;
    gba.cpu.r[0] = mem;

    cupid_arm7_step(&gba);
    assert(gba.cpu.r[0] == mem + 0x40u);
    assert(gba.cpu.r[15] == 0x08000008u);

    gba.cpu.r[15] = 0x08000002u;
    gba.cpu.pipeline_valid = false;
    gba.cpu.r[1] = mem + 4u;
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[1] == mem + 4u + 0x40u);
    assert(cupid_gba_read_u32(&gba, mem + 4u) == 0x08000008u);

    cupid_gba_cleanup(&gba);
}

static void test_thumb_stmia_base_in_rlist_semantics(void)
{
    CupidGba gba = {0};
    uint8_t rom[256];
    uint32_t mem = 0x02000000u;

    cupid_gba_init(&gba);
    memset(rom, 0, sizeof(rom));

    rom[0] = 0x0Fu; rom[1] = 0xC1u; /* STMIA R1!, {R0-R3} */
    rom[2] = 0x1Eu; rom[3] = 0xC1u; /* STMIA R1!, {R1-R4} */

    assert(cupid_gba_load_rom(&gba, rom, sizeof(rom)));

    gba.cpu.r[15] = 0x08000000u;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F | CUPID_ARM_FLAG_T;
    gba.cpu.pipeline_valid = false;

    gba.cpu.r[0] = 0x10u;
    gba.cpu.r[1] = mem;
    gba.cpu.r[2] = 0x30u;
    gba.cpu.r[3] = 0x40u;
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[1] == mem + 16u);
    assert(cupid_gba_read_u32(&gba, mem + 0u) == 0x10u);
    assert(cupid_gba_read_u32(&gba, mem + 4u) == mem + 16u);
    assert(cupid_gba_read_u32(&gba, mem + 8u) == 0x30u);
    assert(cupid_gba_read_u32(&gba, mem + 12u) == 0x40u);

    gba.cpu.r[1] = mem + 0x20u;
    gba.cpu.r[2] = 0x220u;
    gba.cpu.r[3] = 0x330u;
    gba.cpu.r[4] = 0x440u;
    cupid_arm7_step(&gba);
    assert(gba.cpu.r[1] == mem + 0x30u);
    assert(cupid_gba_read_u32(&gba, mem + 0x20u) == mem + 0x20u);
    assert(cupid_gba_read_u32(&gba, mem + 0x24u) == 0x220u);
    assert(cupid_gba_read_u32(&gba, mem + 0x28u) == 0x330u);
    assert(cupid_gba_read_u32(&gba, mem + 0x2Cu) == 0x440u);

    cupid_gba_cleanup(&gba);
}

static void test_arm_prefetched_instructions_survive_self_modifying_code(void)
{
    CupidGba gba = {0};
    uint32_t base = 0x06014000u;

    cupid_gba_init(&gba);

    /* str r2, [r0] */
    cupid_gba_write_u32(&gba, base + 0u, 0xE5802000u);
    /* str r2, [r1] */
    cupid_gba_write_u32(&gba, base + 4u, 0xE5812000u);
    /* cmp r2, #0 */
    cupid_gba_write_u32(&gba, base + 8u, 0xE3520000u);
    /* beq success */
    cupid_gba_write_u32(&gba, base + 12u, 0x0A000000u);
    /* mov r12, #1 */
    cupid_gba_write_u32(&gba, base + 16u, 0xE3A0C001u);
    /* nop */
    cupid_gba_write_u32(&gba, base + 20u, 0xE1A00000u);
    /* nop */
    cupid_gba_write_u32(&gba, base + 24u, 0xE1A00000u);

    gba.cpu.r[0] = base + 8u;
    gba.cpu.r[1] = base + 12u;
    gba.cpu.r[2] = 0u;
    gba.cpu.r[12] = 0u;
    gba.cpu.r[15] = base;
    gba.cpu.cpsr = CUPID_ARM_MODE_SVC | CUPID_ARM_FLAG_I | CUPID_ARM_FLAG_F;
    gba.cpu.pipeline_valid = false;

    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);
    cupid_arm7_step(&gba);

    assert(gba.cpu.r[12] == 0u);

    cupid_gba_cleanup(&gba);
}


static void test_gba_render_mode3_argb(void)
{
    CupidGba gba = {0};
    uint32_t pixels[CUPID_GBA_SCREEN_WIDTH * CUPID_GBA_SCREEN_HEIGHT];

    cupid_gba_init(&gba);
    gba.display_control = 0x0003u;
    cupid_gba_write_u16(&gba, 0x06000000u, 0x001Fu);
    cupid_gba_write_u16(&gba, 0x06000002u, 0x03E0u);
    cupid_gba_render_argb(&gba, pixels, CUPID_GBA_SCREEN_WIDTH * CUPID_GBA_SCREEN_HEIGHT);

    assert(pixels[0] == 0xFFFF0000u);
    assert(pixels[1] == 0xFF00FF00u);

    cupid_gba_cleanup(&gba);
}

static void test_gba_render_mode0_bg0_tile(void)
{
    CupidGba gba = {0};
    uint32_t pixels[CUPID_GBA_SCREEN_WIDTH * CUPID_GBA_SCREEN_HEIGHT];
    unsigned int row;

    cupid_gba_init(&gba);
    gba.display_control = 0x0100u;
    cupid_gba_write_u16(&gba, 0x05000000u, 0x0000u);
    cupid_gba_write_u16(&gba, 0x05000002u, 0x7C00u);
    cupid_gba_write_u16(&gba, 0x04000008u, 0x0100u);
    cupid_gba_write_u16(&gba, 0x06000000u, 0x1111u);
    for (row = 1u; row < 8u; ++row) {
        cupid_gba_write_u16(&gba, 0x06000000u + row * 4u, 0x1111u);
        cupid_gba_write_u16(&gba, 0x06000002u + row * 4u, 0x1111u);
    }
    cupid_gba_write_u16(&gba, 0x06000000u + 0x800u, 0x0000u);
    cupid_gba_write_u16(&gba, 0x06000000u + 0x802u, 0x0001u);

    cupid_gba_render_argb(&gba, pixels, CUPID_GBA_SCREEN_WIDTH * CUPID_GBA_SCREEN_HEIGHT);

    assert(pixels[0] == 0xFF0000FFu);
    assert(pixels[8] == 0xFF000000u);

    cupid_gba_cleanup(&gba);
}

static bool load_gba_rom_fixture(CupidGba *gba, const char *path)
{
    static const char *prefixes[] = {
        "",
        "../",
    };
    unsigned int i;

    for (i = 0u; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        char candidate[256];
        int written = snprintf(candidate, sizeof(candidate), "%s%s", prefixes[i], path);

        if (written > 0 && (size_t)written < sizeof(candidate) &&
            cupid_gba_load_rom_file(gba, candidate)) {
            return true;
        }
    }

    return false;
}

static void test_arm_rom_passes(void)
{
    CupidGba gba = {0};
    uint32_t last_pc = 0xffffffffu;
    uint32_t same_pc_count = 0u;
    uint32_t steps;

    cupid_gba_init(&gba);
    assert(load_gba_rom_fixture(&gba, "gba-tests/arm/arm.gba"));

    for (steps = 0u; steps < 5000000u; ++steps) {
        uint32_t pc;

        cupid_gba_step(&gba);
        pc = gba.cpu.r[15] & ~1u;

        if (pc == last_pc) {
            same_pc_count++;
        } else {
            last_pc = pc;
            same_pc_count = 0u;
        }

        if (same_pc_count >= 64u && (gba.display_control & 0x7u) == 4u) {
            break;
        }
    }

    if (!(same_pc_count >= 64u && (gba.display_control & 0x7u) == 4u)) {
        fprintf(stderr,
                "arm.gba did not reach its idle loop after %u instructions (pc=%08X r12=%u)\n",
                steps,
                gba.cpu.r[15],
                gba.cpu.r[12]);
        assert(0);
    }

    if (gba.cpu.r[12] != 0u) {
        fprintf(stderr,
                "arm.gba failed test %u (pc=%08X)\n",
                gba.cpu.r[12],
                gba.cpu.r[15]);
        assert(gba.cpu.r[12] == 0u);
    }

    cupid_gba_cleanup(&gba);
}

static void test_emulator_initializes_gba_core(void)
{
    CupidEmulator emulator = {0};
    uint8_t rom[64];

    memset(rom, 0, sizeof(rom));
    /* Place NOPs (MOV R0, R0) so the CPU doesn't execute garbage */
    {
        unsigned int i;
        for (i = 0u; i + 3u < sizeof(rom); i += 4u) {
            rom[i] = 0x00u; rom[i+1] = 0x00u;
            rom[i+2] = 0xA0u; rom[i+3] = 0xE1u;
        }
    }

    cupid_emulator_init(&emulator, CUPID_SYSTEM_GBA);
    assert(emulator.initialized);
    assert(emulator.gba.rom != 0);
    assert(cupid_emulator_load_rom(&emulator, rom, sizeof(rom)));
    assert(emulator.gba.loaded);
    assert(!cupid_emulator_step(&emulator));

    cupid_emulator_shutdown(&emulator);
    assert(!emulator.initialized);
}

int main(void)
{
    test_gba_init_sets_power_on_state();
    test_gba_load_rom_maps_gamepak_space();
    test_gba_memory_regions_round_trip();
    test_gba_16_and_32_bit_bus();
    test_gba_memory_region_mirrors();
    test_gba_unmapped_gamepak_reads_follow_address_pattern();
    test_gba_io_dispstat_and_vcount();
    test_gba_interrupt_acknowledge();
    test_gba_timer_register_reads_reflect_live_counter();
    test_gba_dma3_immediate_copy();
    test_gba_flash128_save_commands();
    test_gba_sram_save_commands();
    test_arm7_condition_checks();
    test_arm7_barrel_shift();
    test_arm7_register_shift_zero_preserves_carry();
    test_arm7_mode_switching();
    test_arm_mov_instruction();
    test_arm_mrs_spsr_in_system_mode_returns_cpsr();
    test_arm_invalid_cpsr_mode_write_sets_bit4();
    test_arm_teqp_style_restores_cpsr_from_spsr();
    test_arm_add_instruction();
    test_arm_pc_operand_in_data_processing();
    test_arm_ldm_user_bank_transfer_in_fiq_mode();
    test_thumb_mov_instruction();
    test_thumb_pc_relative_load();
    test_thumb_add_pc_matches_mov_pc_sequence();
    test_thumb_misaligned_loads_rotate();
    test_thumb_multiple_empty_rlist_semantics();
    test_thumb_stmia_base_in_rlist_semantics();
    test_arm_prefetched_instructions_survive_self_modifying_code();
    test_gba_render_mode3_argb();
    test_gba_render_mode0_bg0_tile();
    test_arm_rom_passes();
    test_emulator_initializes_gba_core();
    return 0;
}

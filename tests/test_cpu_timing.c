#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void prepare(CupidGb *gb, uint8_t opcode, uint8_t flags) {
    uint8_t rom[32768] = {0};
    memcpy(rom + 0x134, "CPU TIMING", 10);
    rom[0x100] = opcode;
    rom[0x102] = 2;
    for (unsigned i = 0x134; i <= 0x14c; ++i) {
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    }
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->cpu.f = flags;
    gb->cpu.sp = 0xc100;
    gb->cpu.ime = false;
    gb->interrupt_enable = 0;
    gb->io_registers[0x40] = 0;
    gb->io_registers[0x04] = 0;
    gb->div_counter = 0;
    gb->work_ram[0x100] = 0;
    gb->work_ram[0x101] = 2;
}

static void step_cycles(CupidGb *gb, unsigned tcycles) {
    uint64_t before = gb->elapsed_cycles;
    assert(cupid_gb_step(gb));
    assert(gb->elapsed_cycles - before == tcycles);
}

static void test_conditional_control_flow(void) {
    CupidGb gb = {0};
    /* NZ, Z, NC, C: exercise both outcomes with the other flags held clear. */
    const uint8_t flags_taken[] = {0, 0x80, 0, 0x10};
    const uint8_t flags_skipped[] = {0x80, 0, 0x10, 0};
    for (unsigned condition = 0; condition < 4; ++condition) {
        uint8_t jump = (uint8_t)(0xc2 + 8 * condition);
        uint8_t call = (uint8_t)(0xc4 + 8 * condition);
        uint8_t ret = (uint8_t)(0xc0 + 8 * condition);
        prepare(&gb, jump, flags_taken[condition]);
        step_cycles(&gb, 16);
        assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc100);
        prepare(&gb, jump, flags_skipped[condition]);
        step_cycles(&gb, 12);
        assert(gb.cpu.pc == 0x103 && gb.cpu.sp == 0xc100);

        prepare(&gb, call, flags_taken[condition]);
        step_cycles(&gb, 24);
        assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc0fe);
        assert(gb.work_ram[0xfe] == 3 && gb.work_ram[0xff] == 1);
        prepare(&gb, call, flags_skipped[condition]);
        gb.work_ram[0xfe] = 0xa5;
        gb.work_ram[0xff] = 0x5a;
        step_cycles(&gb, 12);
        assert(gb.cpu.pc == 0x103 && gb.cpu.sp == 0xc100);
        assert(gb.work_ram[0xfe] == 0xa5 && gb.work_ram[0xff] == 0x5a);

        prepare(&gb, ret, flags_taken[condition]);
        step_cycles(&gb, 20);
        assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc102);
        prepare(&gb, ret, flags_skipped[condition]);
        step_cycles(&gb, 8);
        assert(gb.cpu.pc == 0x101 && gb.cpu.sp == 0xc100);
    }
    cupid_gb_cleanup(&gb);
}

static void test_unconditional_control_flow(void) {
    CupidGb gb = {0};
    prepare(&gb, 0xc3, 0);
    step_cycles(&gb, 16);
    assert(gb.cpu.pc == 0x200);
    prepare(&gb, 0xcd, 0);
    step_cycles(&gb, 24);
    assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc0fe);
    prepare(&gb, 0xc9, 0);
    step_cycles(&gb, 16);
    assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc102);
    prepare(&gb, 0xd9, 0);
    step_cycles(&gb, 16);
    assert(gb.cpu.pc == 0x200 && gb.cpu.sp == 0xc102 && gb.cpu.ime);
    cupid_gb_cleanup(&gb);
}

static void test_stack_bus_phases(void) {
    CupidGb gb = {0};
    /* A low stack byte read from DIV observes the divider after M1 for RET,
     * and after the extra condition cycle for a taken conditional return. */
    prepare(&gb, 0xc9, 0);
    gb.cpu.sp = 0xff04;
    gb.div_counter = 63;
    gb.io_registers[5] = 2;
    step_cycles(&gb, 16);
    assert(gb.cpu.pc == 0x201);
    prepare(&gb, 0xc0, 0);
    gb.cpu.sp = 0xff04;
    gb.div_counter = 62;
    gb.io_registers[5] = 2;
    step_cycles(&gb, 20);
    assert(gb.cpu.pc == 0x201);

    /* CALL writes stack bytes in M5 then M6. Writing DIV resets its phase,
     * exposing the number of cycles remaining after each write. */
    prepare(&gb, 0xcd, 0);
    gb.cpu.sp = 0xff05;
    step_cycles(&gb, 24);
    assert(gb.div_counter == 2);
    prepare(&gb, 0xcd, 0);
    gb.cpu.sp = 0xff06;
    step_cycles(&gb, 24);
    assert(gb.div_counter == 1);
    cupid_gb_cleanup(&gb);
}

static void test_sp_offset_operand_bus_phase(void) {
    CupidGb gb = {0};
    const uint8_t opcodes[] = {0xe8, 0xf8};
    for (unsigned i = 0; i < sizeof(opcodes); ++i) {
        prepare(&gb, opcodes[i], 0);
        /* Execute from the WRAM echo's final byte. Its displacement lives in
         * OAM and becomes readable as the PPU enters HBlank during M1. */
        cupid_gb_write_u8(&gb, 0xfdff, opcodes[i]);
        gb.cpu.pc = 0xfdff;
        gb.cpu.sp = 0x100;
        gb.object_attribute_memory[0] = 1;
        gb.io_registers[CUPID_GB_IO_LCDC] = 0x91;
        gb.io_registers[CUPID_GB_IO_LY] = 1;
        gb.ppu_counter = CUPID_GB_PPU_OAM_CYCLES + CUPID_GB_PPU_TRANSFER_CYCLES;
        cupid_gb_set_ppu_mode(&gb, CUPID_GB_PPU_MODE_TRANSFER);
        assert(cupid_gb_read_u8(&gb, 0xfe00) == 0xff);
        step_cycles(&gb, i == 0 ? 16 : 12);
        if (i == 0) {
            assert(gb.cpu.sp == 0x101);
        } else {
            assert(gb.cpu.sp == 0x100);
            assert(gb.cpu.h == 1 && gb.cpu.l == 1);
        }
        assert(gb.cpu.pc == 0xfe01 && gb.cpu.f == 0);
    }
    cupid_gb_cleanup(&gb);
}

int main(void) {
    test_conditional_control_flow();
    test_unconditional_control_flow();
    test_stack_bus_phases();
    test_sp_offset_operand_bus_phase();
    puts("CPU control-flow timing checks passed.");
    return 0;
}

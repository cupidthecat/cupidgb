#include "cupid/gb/debugger.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cupid/gb/state.h"

static void prepare(CupidGb *gb) {
    uint8_t rom[32768] = {0};
    memcpy(rom + 0x134, "DEBUGGER TEST", 13);
    for (size_t offset = 0x134; offset < 0x14d; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1u);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->cpu.pc = 0x100;
    gb->cpu.sp = 0xdff0;
    gb->cpu.a = gb->cpu.b = gb->cpu.f = 0;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    cupid_gb_write_u8(gb, 0xff40, 0);
}

static void calls(CupidGb *gb) {
    prepare(gb);
    const uint8_t outer[] = {0xcd, 0x10, 0x01, 0x3c, 0x00};
    const uint8_t inner[] = {0x04, 0xcd, 0x20, 0x01, 0xc9};
    const uint8_t nested[] = {0x3c, 0xc9};
    memcpy(gb->rom + 0x100, outer, sizeof(outer));
    memcpy(gb->rom + 0x110, inner, sizeof(inner));
    memcpy(gb->rom + 0x120, nested, sizeof(nested));
}

static void halt_at_vram_boundary(CupidGb *gb) {
    prepare(gb);
    gb->cpu.pc = 0x8000;
    gb->cpu.halted = true;
    gb->cpu.just_halted = false;
    gb->interrupt_enable = gb->interrupt_flags = 1;
    gb->io_registers[0x40] = 0x91;
    gb->io_registers[0x41] = 2;
    gb->io_registers[0x44] = 0;
    gb->pixels.initialized = true;
    gb->pixels.dot = 78;
    gb->pixels.line = 0;
    gb->ppu_counter = 19;
    gb->video_ram[0] = 0;
    gb->rom[0x38] = 0xc9;
}

static void assert_same_hardware(const CupidGb *first, const CupidGb *second) {
    const size_t size = cupid_gb_state_size(first);
    assert(size && size == cupid_gb_state_size(second));
    uint8_t *observed = malloc(size), *expected = malloc(size);
    assert(observed && expected);
    assert(cupid_gb_save_state(first, observed, size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_save_state(second, expected, size, NULL) == CUPID_GB_STATE_OK);
    assert(!memcmp(observed, expected, size));
    free(expected);
    free(observed);
}

static void test_execution(CupidGb *gb) {
    CupidGbDebugger debug;
    cupid_gb_debugger_init(&debug);
    calls(gb);
    const uint64_t before = gb->elapsed_cycles;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 100) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x103 && gb->cpu.sp == 0xdff0);
    assert(gb->cpu.a == 1 && gb->cpu.b == 1 && debug.steps == 6);
    assert(gb->elapsed_cycles - before == 88);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_STEP);
    assert(cupid_gb_debugger_step(&debug, gb) == CUPID_GB_DEBUG_STOP_STEP);
    assert(gb->cpu.pc == 0x104 && gb->cpu.a == 2);

    calls(gb);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_STEP);
    assert(cupid_gb_debugger_step(&debug, gb) == CUPID_GB_DEBUG_STOP_STEP);
    assert(gb->cpu.pc == 0x110 && gb->cpu.sp == 0xdfee);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_FINISH);
    assert(cupid_gb_debugger_run(&debug, gb, 100) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x103 && gb->cpu.sp == 0xdff0 && debug.steps == 5);

    calls(gb);
    gb->rom[0x100] = 0xc4; /* CALL NZ exercises both taken and untaken flow. */
    gb->cpu.f = 0x80;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 100) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(debug.steps == 1 && gb->cpu.pc == 0x103 && gb->cpu.sp == 0xdff0);
    gb->cpu.pc = 0x100;
    gb->cpu.f = 0;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 100) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(debug.steps == 6 && gb->cpu.pc == 0x103 && gb->cpu.a == 1);

    prepare(gb);
    gb->rom[0x100] = 0xcf; /* RST $08 and its return are one Next command. */
    gb->rom[8] = 0xc9;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x101 && gb->cpu.sp == 0xdff0 && debug.steps == 2);

    prepare(gb);
    gb->cpu.pc = 0x8000;
    gb->video_ram[0] = 0;
    gb->io_registers[0x40] = 0x80;
    gb->io_registers[0x41] = 3;
    gb->io_registers[0x44] = 0;
    gb->rom[0x38] = 0xc9;
    assert(cupid_gb_debug_peek(gb, 0x8000) == 0);
    assert(cupid_gb_read_u8(gb, 0x8000) == 0xff);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x8001 && gb->cpu.sp == 0xdff0 && debug.steps == 2);

    halt_at_vram_boundary(gb);
    assert(cupid_gb_read_u8(gb, 0x8000) == 0);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x8001 && gb->cpu.sp == 0xdff0 && debug.steps == 2);

    prepare(gb);
    gb->rom[0x100] = 0x18;
    gb->rom[0x101] = 0xfe; /* A busy loop must return control to the host. */
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_FINISH);
    assert(cupid_gb_debugger_run(&debug, gb, 17) == CUPID_GB_DEBUG_STOP_BUDGET);
    assert(debug.mode == CUPID_GB_DEBUG_FINISH && debug.steps == 17);
    assert(cupid_gb_debugger_run(&debug, gb, 3) == CUPID_GB_DEBUG_STOP_BUDGET);
    assert(debug.steps == 20 && gb->cpu.pc == 0x100);
    cupid_gb_debugger_reset_execution(&debug);
    assert(debug.mode == CUPID_GB_DEBUG_PAUSED);
    cupid_gb_debugger_cleanup(&debug);
}

static void test_interrupts_and_halt(CupidGb *gb) {
    CupidGbDebugger debug;
    cupid_gb_debugger_init(&debug);
    prepare(gb);
    gb->rom[0x50] = 0xd9;
    gb->cpu.ime = true;
    gb->interrupt_enable = gb->interrupt_flags = 4;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 20) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x100 && gb->cpu.sp == 0xdff0 && debug.steps == 2);
    gb->interrupt_flags = 4;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_STEP);
    assert(cupid_gb_debugger_step(&debug, gb) == CUPID_GB_DEBUG_STOP_STEP);
    assert(gb->cpu.pc == 0x50 && gb->cpu.sp == 0xdfee);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_FINISH);
    assert(cupid_gb_debugger_run(&debug, gb, 20) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x100 && gb->cpu.sp == 0xdff0 && debug.steps == 1);

    prepare(gb);
    gb->rom[0x100] = 0x76;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_BUDGET);
    assert(gb->cpu.halted && gb->cpu.pc == 0x101);
    gb->interrupt_enable = gb->interrupt_flags = 1;
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(!gb->cpu.halted && gb->cpu.pc == 0x102);
    prepare(gb);
    gb->rom[0x100] = 0x76;
    assert(cupid_gb_debugger_breakpoint_add(&debug, -1, 0x101));
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_RUN);
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_BREAKPOINT);
    assert(gb->cpu.halted && gb->cpu.pc == 0x101);
    cupid_gb_debugger_breakpoints_clear(&debug);

    /* With IME clear, a pending request wakes HALT and executes the next
     * opcode in the same core step. Calls and returns still affect depth. */
    prepare(gb);
    gb->rom[0x101] = 0xcd;
    gb->rom[0x102] = 0x10;
    gb->rom[0x103] = 0x01;
    gb->rom[0x110] = 0xc9;
    gb->cpu.pc = 0x101;
    gb->cpu.halted = true;
    gb->interrupt_enable = gb->interrupt_flags = 1;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x104 && gb->cpu.sp == 0xdff0 && debug.steps == 2);
    gb->cpu.pc = 0x110;
    gb->cpu.sp = 0xdfee;
    gb->cpu.halted = true;
    assert(cupid_gb_debug_poke(gb, 0xdfee, 0x04));
    assert(cupid_gb_debug_poke(gb, 0xdfef, 0x01));
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_FINISH);
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.pc == 0x104 && gb->cpu.sp == 0xdff0 && debug.steps == 1);

    prepare(gb);
    gb->rom[0x100] = 0x10;
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 8) == CUPID_GB_DEBUG_STOP_TARGET);
    assert(gb->cpu.stopped && gb->cpu.pc == 0x102 && debug.steps == 1);

    prepare(gb);
    gb->rom[0x100] = 0xd3;
    const size_t size = cupid_gb_state_size(gb);
    uint8_t *before_error = malloc(size);
    assert(before_error && cupid_gb_save_state(gb, before_error, size, NULL) == CUPID_GB_STATE_OK);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_STEP);
    assert(cupid_gb_debugger_step(&debug, gb) == CUPID_GB_DEBUG_STOP_ERROR);
    assert(debug.steps == 1 && gb->cpu.pc == 0x101 && gb->cpu.halted);
    assert(cupid_gb_load_state(gb, before_error, size) == CUPID_GB_STATE_OK);
    assert(gb->cpu.pc == 0x100 && !gb->cpu.halted);
    free(before_error);
    cupid_gb_debugger_cleanup(&debug);
}

static void test_breakpoints(CupidGb *gb) {
    CupidGbDebugger debug;
    cupid_gb_debugger_init(&debug);
    calls(gb);
    assert(cupid_gb_debugger_breakpoint_add(&debug, -1, 0x111));
    assert(cupid_gb_debugger_breakpoint_add(&debug, -1, 0x111));
    assert(debug.bp_count == 1);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_NEXT);
    assert(cupid_gb_debugger_run(&debug, gb, 100) == CUPID_GB_DEBUG_STOP_BREAKPOINT);
    assert(gb->cpu.pc == 0x111 && gb->cpu.b == 1 && gb->cpu.a == 0);
    const uint64_t cycles = gb->elapsed_cycles;
    assert(cupid_gb_debugger_check(&debug, gb) == CUPID_GB_DEBUG_STOP_BREAKPOINT);
    assert(gb->elapsed_cycles == cycles);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_RUN);
    assert(cupid_gb_debugger_step(&debug, gb) == CUPID_GB_DEBUG_STOP_NONE);
    assert(gb->cpu.pc == 0x120);
    assert(cupid_gb_debugger_breakpoint_remove(&debug, 0));
    assert(!cupid_gb_debugger_breakpoint_remove(&debug, 0));
    assert(!cupid_gb_debugger_breakpoint_add(&debug, -2, 0x100));
    for (unsigned point = 0; point < CUPID_GB_DEBUG_MAX_BREAKPOINTS; ++point)
        assert(cupid_gb_debugger_breakpoint_add(&debug, 0, (uint16_t)point));
    assert(!cupid_gb_debugger_breakpoint_add(&debug, 0, 0x1234));
    cupid_gb_debugger_breakpoints_clear(&debug);

    uint8_t *banked_rom = calloc(65536, 1);
    assert(banked_rom);
    banked_rom[0x147] = 1;
    banked_rom[0x148] = 1;
    for (size_t offset = 0x134; offset < 0x14d; ++offset)
        banked_rom[0x14d] = (uint8_t)(banked_rom[0x14d] - banked_rom[offset] - 1u);
    assert(cupid_gb_load_rom(gb, banked_rom, 65536));
    free(banked_rom);
    gb->cpu.ime = false;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    gb->cpu.pc = 0x4000;
    assert(cupid_gb_debugger_breakpoint_add(&debug, 2, 0x4000));
    assert(cupid_gb_debugger_bank(gb, 0x4000) == 1);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_RUN);
    assert(cupid_gb_debugger_check(&debug, gb) == CUPID_GB_DEBUG_STOP_NONE);
    assert(cupid_gb_step(gb));
    gb->cpu.pc = 0x4000;
    cupid_gb_write_u8(gb, 0x2000, 2);
    assert(cupid_gb_debugger_bank(gb, 0x4000) == 2);
    const uint64_t bank_cycles = gb->elapsed_cycles;
    assert(cupid_gb_debugger_check(&debug, gb) == CUPID_GB_DEBUG_STOP_BREAKPOINT);
    assert(gb->elapsed_cycles == bank_cycles);
    debug.breakpoints[0].enabled = false;
    assert(!cupid_gb_debugger_match_breakpoint(&debug, gb));
    cupid_gb_debugger_reset_execution(&debug);
    assert(debug.bp_count == 1);
    cupid_gb_debugger_cleanup(&debug);
}

static void test_memory(CupidGb *gb) {
    prepare(gb);
    cupid_gb_set_model(gb, CUPID_GB_MODEL_CGB);
    gb->cgb_mode = true;
    gb->cgb_vram_bank = 1;
    gb->io_registers[0x40] = 0x80;
    gb->io_registers[0x41] = 3;
    gb->io_registers[0x44] = 0;
    gb->video_ram[0x2000] = 0x12;
    gb->object_attribute_memory[0] = 0x34;
    gb->dma_active = true;
    gb->apu.apu_on = gb->apu.ch3_on = true;
    gb->apu.ch3_current_byte = 5;
    gb->io_registers[0x30] = 0x56;
    gb->io_registers[0x35] = 0x78;
    assert(cupid_gb_read_u8(gb, 0x8000) == 0xff);
    assert(cupid_gb_read_u8(gb, 0xfe00) == 0xff);
    assert(cupid_gb_debug_peek(gb, 0x8000) == 0x12);
    assert(cupid_gb_debug_peek(gb, 0xfe00) == 0x34);
    assert(cupid_gb_debug_peek(gb, 0xff30) == 0x56);
    CupidGb *before = malloc(sizeof(*before));
    assert(before);
    memcpy(before, gb, sizeof(*before));
    for (unsigned address = 0; address < 65536; ++address)
        (void)cupid_gb_debug_peek(gb, (uint16_t)address);
    assert(!memcmp(before, gb, sizeof(*before)));
    free(before);
    const uint64_t cycles = gb->elapsed_cycles;
    assert(cupid_gb_debug_poke(gb, 0x8000, 0x9a));
    assert(gb->video_ram[0x2000] == 0x9a);
    assert(cupid_gb_debug_poke(gb, 0xfe00, 0xbc));
    assert(gb->object_attribute_memory[0] == 0xbc);
    assert(cupid_gb_debug_poke(gb, 0xff30, 0xde));
    assert(gb->io_registers[0x30] == 0xde && gb->io_registers[0x35] == 0x78);
    assert(cupid_gb_debug_poke(gb, 0xc123, 0x42));
    assert(cupid_gb_debug_peek(gb, 0xe123) == 0x42);
    assert(cupid_gb_debug_poke(gb, 0xff80, 0x43));
    assert(gb->high_ram[0] == 0x43);
    assert(cupid_gb_debug_poke(gb, 0xffff, 0x1a));
    assert(gb->interrupt_enable == 0x1a);
    assert(!cupid_gb_debug_poke(gb, 0x100, 0x44));
    assert(!cupid_gb_debug_poke(gb, 0xfea0, 0x44));
    assert(!cupid_gb_debug_poke(NULL, 0x8000, 0x44));
    assert(cupid_gb_debug_peek(NULL, 0x100) == 0xff);
    assert(gb->elapsed_cycles == cycles);

    gb->header.mbc_type = CUPID_GB_MBC2;
    gb->header.has_battery = true;
    gb->cartridge_ram_size = 512;
    const uint64_t generation = gb->battery_write_generation;
    assert(cupid_gb_debug_poke(gb, 0xa123, 0xab));
    assert(cupid_gb_debug_peek(gb, 0xa323) == 0xfb);
    assert(gb->battery_write_generation == generation + 1);
    gb->header.mbc_type = CUPID_GB_MBC7;
    assert(!cupid_gb_debug_poke(gb, 0xa000, 0x44));
    gb->header.mbc_type = CUPID_GB_MBC3;
    gb->rtc_register = 8;
    assert(!cupid_gb_debug_poke(gb, 0xa000, 0x44));
    cupid_gb_set_model(gb, CUPID_GB_MODEL_DMG_ABC);
}

static void test_decoder(CupidGb *gb) {
    static const uint8_t lengths_low[64] = {1, 3, 1, 1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 2, 1,
                                            2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
                                            2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
                                            2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1};
    static const uint8_t lengths_high[64] = {1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 2, 3, 3, 2, 1,
                                             1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, 1, 2, 1,
                                             2, 1, 1, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1,
                                             2, 1, 1, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1};
    static const uint8_t illegal[] = {0xd3, 0xdb, 0xdd, 0xe3, 0xe4, 0xeb,
                                      0xec, 0xed, 0xf4, 0xfc, 0xfd};
    prepare(gb);
    CupidGbDebugInstruction instruction;
    for (unsigned opcode = 0; opcode < 256; ++opcode) {
        gb->rom[0x100] = (uint8_t)opcode;
        gb->rom[0x101] = 0x34;
        gb->rom[0x102] = 0x12;
        cupid_gb_debug_disassemble(gb, 0x100, &instruction);
        const unsigned expected = opcode < 64    ? lengths_low[opcode]
                                  : opcode < 192 ? 1
                                                 : lengths_high[opcode - 192];
        assert(instruction.length == expected && instruction.bytes[0] == opcode);
        assert(instruction.text[0]);
        bool valid = true;
        for (size_t index = 0; index < sizeof(illegal); ++index)
            if (opcode == illegal[index])
                valid = false;
        assert(instruction.valid == valid);
    }
    gb->rom[0x100] = 0xcb;
    for (unsigned opcode = 0; opcode < 256; ++opcode) {
        gb->rom[0x101] = (uint8_t)opcode;
        cupid_gb_debug_disassemble(gb, 0x100, &instruction);
        assert(instruction.length == 2 && instruction.valid && instruction.text[0]);
    }

    const struct {
        uint8_t bytes[3];
        const char *text;
    } cases[] = {{{0xcd, 0x34, 0x12}, "CALL $1234"}, {{0x20, 0xfc, 0}, "JR NZ, $00FE"},
                 {{0xcb, 0x7e, 0}, "BIT 7, (HL)"},   {{0xe8, 0xff, 0}, "ADD SP, -1"},
                 {{0xf8, 0x80, 0}, "LD HL, SP-128"}, {{0xea, 0x34, 0x12}, "LD ($1234), A"}};

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        memcpy(gb->rom + 0x100, cases[index].bytes, 3);
        cupid_gb_debug_disassemble(gb, 0x100, &instruction);
        assert(!strcmp(instruction.text, cases[index].text));
    }
    gb->interrupt_enable = 1;
    gb->rom[0] = 0x34;
    gb->rom[1] = 0x12;
    cupid_gb_debug_disassemble(gb, 0xffff, &instruction);
    assert(instruction.length == 3 && !strcmp(instruction.text, "LD BC, $1234"));
    cupid_gb_debug_disassemble(gb, 0x100, NULL);
}

static void test_symbols(void) {
    CupidGbDebugger debug;
    cupid_gb_debugger_init(&debug);
    const char text[] =
        "; comments and CRLF are accepted\r\n00:0100 Start\r\n01:4000 Banked\n01:4003 Banked.local "
        "; label\n02:4010 Duplicate\n03:4020 Duplicate\n";
    size_t error = 99;
    assert(cupid_gb_debugger_load_symbols(&debug, text, sizeof(text) - 1u, &error));
    assert(error == 0 && debug.symbol_count == 5);
    uint16_t bank = 0, address = 0;
    assert(cupid_gb_debugger_lookup_symbol(&debug, "Duplicate", &bank, &address));
    assert(bank == 3 && address == 0x4020);
    assert(!strcmp(cupid_gb_debugger_symbol_for_address(&debug, 1, 0x4003, true), "Banked.local"));
    assert(!strcmp(cupid_gb_debugger_symbol_for_address(&debug, 1, 0x4007, false), "Banked.local"));
    assert(!strcmp(cupid_gb_debugger_symbol_for_address(&debug, 9, 0x100, true), "Start"));
    assert(!cupid_gb_debugger_symbol_for_address(&debug, 1, 0x4002, true));
    const char bad[] = "00:0100 Valid\n01:4000 Missing Name\n";
    CupidGbDebugSymbol *unchanged = debug.symbols;
    assert(!cupid_gb_debugger_load_symbols(&debug, bad, sizeof(bad) - 1u, &error));
    assert(error == 2 && debug.symbols == unchanged && debug.symbol_count == 5);
    const char embedded_zero[] = {'0', '0', ':', '0', '0', '0', '0', ' ', 'x', 0, 'y'};
    assert(!cupid_gb_debugger_load_symbols(&debug, embedded_zero, sizeof(embedded_zero), &error));
    assert(error == 1 && debug.symbols == unchanged);
    const char *bad_lines[] = {"10000:0000 Label", "00:10000 Label", "gg:0000 Label",
                               "00:0000",          "00 0000 Label",  "00:0000 Invalid!"};
    for (size_t index = 0; index < sizeof(bad_lines) / sizeof(bad_lines[0]); ++index)
        assert(!cupid_gb_debugger_load_symbols(&debug, bad_lines[index], strlen(bad_lines[index]),
                                               &error));
    cupid_gb_debugger_reset_execution(&debug);
    assert(debug.symbol_count == 5);
    assert(cupid_gb_debugger_load_symbols(&debug, NULL, 0, &error));
    assert(debug.symbol_count == 0 && !debug.symbols);
    cupid_gb_debugger_cleanup(&debug);
    assert(cupid_gb_debugger_run(&debug, NULL, 0) == CUPID_GB_DEBUG_STOP_NO_ROM);
}

static void test_execution_preserves_hardware(CupidGb *gb) {
    CupidGb *plain = calloc(1, sizeof(*plain));
    assert(plain);
    prepare(gb);
    prepare(plain);
    const uint8_t program[] = {0x04, 0xc3, 0x00, 0x01};
    memcpy(gb->rom + 0x100, program, sizeof(program));
    memcpy(plain->rom + 0x100, program, sizeof(program));
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    cupid_gb_write_u8(plain, 0xff40, 0x91);
    cupid_gb_write_u8(gb, 0xff07, 5);
    cupid_gb_write_u8(plain, 0xff07, 5);
    CupidGbDebugger debug;
    cupid_gb_debugger_init(&debug);
    cupid_gb_debugger_begin(&debug, gb, CUPID_GB_DEBUG_RUN);
    assert(cupid_gb_debugger_run(&debug, gb, 8192) == CUPID_GB_DEBUG_STOP_BUDGET);
    for (unsigned step = 0; step < 8192; ++step)
        assert(cupid_gb_step(plain));
    assert_same_hardware(gb, plain);
    cupid_gb_debugger_cleanup(&debug);
    cupid_gb_cleanup(plain);
    free(plain);
}

static void test_step_observation(CupidGb *gb) {
    CupidGb *observed = calloc(1, sizeof(*observed));
    assert(observed);
    prepare(observed);
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        prepare(gb);
        CupidGbStepEvent event = CUPID_GB_STEP_IDLE;
        uint16_t address = 0x100;
        uint8_t opcode = 0;
        if (scenario == 0) {
            event = CUPID_GB_STEP_INSTRUCTION;
            gb->rom[0x100] = opcode = 0x3c;
        } else if (scenario == 1) {
            event = CUPID_GB_STEP_INTERRUPT;
            gb->cpu.ime = true;
            gb->interrupt_enable = gb->interrupt_flags = 4;
        } else if (scenario == 2) {
            gb->cpu.halted = true;
        } else if (scenario == 3 || scenario == 4) {
            gb->cpu.stopped = true;
            if (scenario == 4)
                gb->joypad = 0;
        } else {
            halt_at_vram_boundary(gb);
            event = CUPID_GB_STEP_INSTRUCTION;
            address = 0x8000;
            opcode = 0xff;
            assert(cupid_gb_read_u8(gb, address) == 0);
        }
        uint8_t *owned_rom = observed->rom;
        *observed = *gb;
        observed->rom = owned_rom;
        memcpy(observed->rom, gb->rom, gb->rom_size);
        CupidGbStepTrace trace;
        assert(cupid_gb_step(gb));
        assert(cupid_gb_step_observed(observed, &trace));
        assert(trace.event == event && trace.address == address && trace.opcode == opcode);
        assert_same_hardware(gb, observed);
    }
    CupidGbStepTrace trace = {CUPID_GB_STEP_INTERRUPT, 1, 1};
    assert(!cupid_gb_step_observed(NULL, &trace));
    assert(trace.event == CUPID_GB_STEP_IDLE && trace.address == 0 && trace.opcode == 0);
    cupid_gb_cleanup(observed);
    free(observed);
}

int main(void) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    test_execution(gb);
    test_interrupts_and_halt(gb);
    test_breakpoints(gb);
    test_memory(gb);
    test_decoder(gb);
    test_symbols();
    test_execution_preserves_hardware(gb);
    test_step_observation(gb);
    cupid_gb_cleanup(gb);
    free(gb);
    puts("Debugger execution, memory, disassembly and symbols passed.");
    return 0;
}

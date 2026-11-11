#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CupidGb *hardware(CupidGbModel model, bool fast, uint8_t opcode) {
    uint8_t rom[32768] = {0};
    rom[0x100] = opcode;
    rom[0x101] = 0;
    rom[0x102] = 0xa0;
    rom[0x200] = 0xa5;
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x147] = 3;
    rom[0x149] = 2;
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_init(gb);
    gb->model = model;
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0, 0);
    gb->double_speed = fast;
    gb->cpu.pc = 0x100;
    gb->cpu.h = gb->cpu.b = gb->cpu.d = 0xa0;
    gb->cpu.l = gb->cpu.c = gb->cpu.e = 0;
    gb->cpu.a = gb->cpu.f = 0;
    gb->cpu.ime = false;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_drivers_and_decay(CupidGbModel model) {
    CupidGb *gb = hardware(model, false, 0);
    const bool color = model == CUPID_GB_MODEL_CGB;
    gb->work_ram[0x123] = 0x36;
    gb->object_attribute_memory[0x80] = 0x73;
    gb->video_ram[0x123] = 0x91;
    gb->high_ram[0] = 0x9a;
    const uint16_t reads[] = {0xc123, 0xe123, 0xfe80, 0xfea0, 0xfeff,
                              0x8123, 0xff80, 0xff40, 0xffff};
    const uint8_t values[] = {0x36, 0x36, 0x73, color ? 0xaa : 0, color ? 0xff : 0, 0x91,
                              0x9a, 0,    0};
    for (unsigned index = 0; index < sizeof(reads) / sizeof(reads[0]); ++index) {
        assert(cupid_gb_read_bus_u8(gb, 0x200) == 0xa5);
        assert(cupid_gb_read_u8(gb, reads[index]) == values[index]);
        assert(cupid_gb_sample_cartridge_bus(gb) == 0xa5);
        assert(cupid_gb_read_bus_u8(gb, reads[index]) == values[index]);
        assert(cupid_gb_sample_cartridge_bus(gb) == (!color && index < 5 ? values[index] : 0xa5));
    }
    const uint8_t unused[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    for (unsigned group = 0; group < sizeof(unused); ++group)
        for (unsigned byte = 0; byte < 16; ++byte)
            assert(cupid_gb_read_bus_u8(gb, (uint16_t)(0xfea0 + group * 16 + byte)) ==
                   (color ? unused[group] : 0));
    const uint16_t writes[] = {0x2000, 0xa123, 0xc123, 0xe123, 0xfe80,
                               0xfea0, 0x8123, 0xff80, 0xff40, 0xffff};
    for (unsigned index = 0; index < sizeof(writes) / sizeof(writes[0]); ++index) {
        assert(cupid_gb_read_bus_u8(gb, 0x200) == 0xa5);
        cupid_gb_write_u8(gb, writes[index], 0x3c);
        const bool drives = index < 2 || (!color && index < 6);
        assert(cupid_gb_sample_cartridge_bus(gb) == (drives ? 0x3c : 0xa5));
    }
    assert(gb->cartridge_ram[0x123] == 0xff);
    // A floating read does not extend the six-base-dot hold period.
    assert(cupid_gb_read_bus_u8(gb, 0x200) == 0xa5);
    const uint64_t origin = gb->elapsed_cycles;
    const uint8_t expected[] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xff, 0xff};
    for (unsigned dot = 0; dot < sizeof(expected); ++dot) {
        gb->elapsed_cycles = origin + dot;
        assert(cupid_gb_read_bus_u8(gb, 0xa000) == expected[dot]);
    }
    release(gb);
}

static void test_cpu_and_resume(CupidGbModel model, bool fast) {
    const uint8_t opcodes[] = {0x7e, 0x0a, 0x1a, 0xfa};
    const uint8_t expected[] = {0x7e, 0x0a, 0x1a, 0xa0};
    for (unsigned index = 0; index < sizeof(opcodes); ++index) {
        CupidGb *gb = hardware(model, fast, opcodes[index]);
        assert(cupid_gb_step(gb));
        assert(gb->cpu.a == expected[index]);
        assert(gb->cpu.pc == (opcodes[index] == 0xfa ? 0x103 : 0x101));
        release(gb);
    }
    CupidGb *a = hardware(model, fast, 0), *b = hardware(model, fast, 0);
    assert(cupid_gb_read_bus_u8(a, 0x200) == 0xa5);
    cupid_gb_tick(a, 1);
    assert(cupid_gb_sample_cartridge_bus(a) == 0xa5);
    const size_t size = cupid_gb_state_size(a);
    uint8_t *state = malloc(size);
    assert(state && cupid_gb_save_state(a, state, size, NULL) == CUPID_GB_STATE_OK);
    assert(cupid_gb_load_state(b, state, size) == CUPID_GB_STATE_OK);
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        assert(cupid_gb_read_bus_u8(a, 0xa000) == cupid_gb_read_bus_u8(b, 0xa000));
        cupid_gb_tick(a, 1);
        cupid_gb_tick(b, 1);
    }
    assert(cupid_gb_sample_cartridge_bus(a) == 0xff);
    free(state);
    release(a);
    release(b);
}

static void test_dma_resolution(CupidGbModel model, bool fast) {
    const bool color = model == CUPID_GB_MODEL_CGB;
    CupidGb *gb = hardware(model, fast, 0);
    gb->rom[0] = 0xab;
    gb->work_ram[0x123] = 0x36;
    gb->work_ram[0x1123] = 0x73;
    cupid_gb_write_u8(gb, 0xff46, 0);
    cupid_gb_tick(gb, 2);
    assert(gb->dma_index == 1 && gb->object_attribute_memory[0] == 0xab);
    if (color)
        assert(cupid_gb_read_bus_u8(gb, 0xd123) == 0x36);
    // Contended stores use the preceding source address, including mapper ports.
    cupid_gb_write_u8(gb, color ? 0x4000 : 0xc123, 0x0a);
    assert(gb->ram_enabled);
    assert(gb->object_attribute_memory[0] == (color ? 0 : 0xab));
    release(gb);

    gb = hardware(model, fast, 0);
    gb->work_ram[0] = 0xab;
    gb->work_ram[0x123] = 0x11;
    cupid_gb_write_u8(gb, 0xff46, 0xc0);
    cupid_gb_tick(gb, 2);
    assert(gb->object_attribute_memory[0] == 0xab);
    cupid_gb_write_u8(gb, 0xc123, 0x3c);
    assert(gb->work_ram[0x123] == 0x11);
    assert(gb->object_attribute_memory[0] == (color ? 0xab : 0x28));
    release(gb);

    gb = hardware(model, fast, 0);
    gb->work_ram[0] = 0xab;
    gb->work_ram[1] = 0x44;
    assert(cupid_gb_read_bus_u8(gb, 0x200) == 0xa5);
    cupid_gb_write_u8(gb, 0xff46, 0xe0);
    cupid_gb_tick(gb, 2);
    assert(gb->dma_index == 1);
    assert(cupid_gb_read_bus_u8(gb, 0xc001) == 0x44);
    assert(cupid_gb_read_bus_u8(gb, 0xe001) == 0x44);
    if (color) {
        const uint64_t expiry = gb->cartridge_bus_expiry;
        assert(cupid_gb_read_bus_u8(gb, 0x200) == 0xff);
        assert(gb->cartridge_bus_expiry == expiry);
    }
    release(gb);
}

static void test_oam_warmup_write(CupidGbModel model) {
    CupidGb *gb = hardware(model, false, 0);
    gb->object_attribute_memory[0] = 0x77;
    cupid_gb_write_u8(gb, 0xff46, 0xc0);
    cupid_gb_tick_dma_tcycles(gb, 2);
    assert(gb->dma_active && !gb->dma_start_delay && !gb->dma_index);
    // The initially readable destination slot still rejects OAM stores.
    assert(cupid_gb_read_bus_u8(gb, 0xfe00) == 0x77);
    cupid_gb_write_u8(gb, 0xfe00, 0x11);
    assert(gb->object_attribute_memory[0] == 0x77);
    cupid_gb_tick_dma_tcycles(gb, 644);
    assert(!gb->dma_active);
    cupid_gb_write_u8(gb, 0xfe00, 0x11);
    assert(gb->object_attribute_memory[0] == 0x11);
    release(gb);
}

int main(void) {
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        test_drivers_and_decay((CupidGbModel)model);
        test_cpu_and_resume((CupidGbModel)model, false);
        test_dma_resolution((CupidGbModel)model, false);
        test_oam_warmup_write((CupidGbModel)model);
    }
    test_cpu_and_resume(CUPID_GB_MODEL_CGB, true);
    test_dma_resolution(CUPID_GB_MODEL_CGB, true);
    puts("Shared main-bus checks passed.");
    return 0;
}

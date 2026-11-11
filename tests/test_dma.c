#include "cupid/gb/debugger.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static CupidGb *hardware(CupidGbModel model, bool color, bool fast, uint8_t opcode) {
    uint8_t rom[32768] = {0};
    rom[0x100] = opcode;
    rom[0x143] = color ? 0x80 : 0;
    for (unsigned i = 0x134; i < 0x14d; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->double_speed = fast;
    gb->cpu.ime = false;
    gb->cpu.b = 0;
    gb->interrupt_enable = gb->interrupt_flags = 0;
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff41, 0);
    cupid_gb_write_u8(gb, 0xff45, 0xff);
    cupid_gb_write_u8(gb, 0xff4b, 0xff);
    for (unsigned i = 0; i < 128; ++i)
        cupid_gb_write_u8(gb, (uint16_t)(0xc000 + i), i ? (uint8_t)(0x20 + i) : 4);
    memset(gb->video_ram, 0, sizeof(gb->video_ram));
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void addresses(CupidGb *gb) {
    cupid_gb_write_u8(gb, 0xff51, 0xc0);
    cupid_gb_write_u8(gb, 0xff52, 0x0f);
    cupid_gb_write_u8(gb, 0xff53, 0x68);
    cupid_gb_write_u8(gb, 0xff54, 0x0f);
}

static void copied(const CupidGb *gb, unsigned count) {
    assert(memcmp(gb->video_ram + 0x800, gb->work_ram, count) == 0);
    for (unsigned i = count; i < 128; ++i)
        assert(gb->video_ram[0x800 + i] == 0);
}

static void lcd_phase(CupidGb *gb, unsigned dots) {
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    cupid_gb_tick_ppu_dots(gb, (uint16_t)dots);
}

static void test_start_boundary_and_latched_opcode(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned lcd = 0; lcd < 2; ++lcd) {
            for (unsigned hblank = 0; hblank < 2; ++hblank) {
                for (unsigned video_fetch = 0; video_fetch < 2; ++video_fetch) {
                    CupidGb *gb = hardware(CUPID_GB_MODEL_CGB, true, fast != 0, 0);
                    if (lcd)
                        lcd_phase(gb, 300);
                    if (video_fetch)
                        gb->cpu.pc = 0x8800;
                    addresses(gb);
                    cupid_gb_write_u8(gb, 0xff55, hblank ? 0x83 : 0);
                    copied(gb, 0);
                    assert(gb->hdma_pending);
                    assert(cupid_gb_read_u8(gb, 0xff55) == (hblank ? 3 : 0));
                    const uint16_t pc = gb->cpu.pc;
                    const uint64_t before = gb->elapsed_cycles;
                    assert(cupid_gb_step(gb));
                    assert(gb->elapsed_cycles - before == (fast ? 36 : 40));
                    assert(gb->cpu.pc == pc + 1 && gb->cpu.b == 0);
                    copied(gb, 16);
                    assert(!gb->hdma_pending);
                    assert(cupid_gb_read_u8(gb, 0xff55) == (hblank ? 2 : 0xff));
                    if (hblank) {
                        cupid_gb_write_u8(gb, 0xff55, 0);
                        assert(cupid_gb_read_u8(gb, 0xff55) == 0x80);
                        assert(!gb->hdma_active && !gb->hdma_pending);
                    }
                    release(gb);
                }
            }
        }
    }
}

static void test_general_transfer_clocks_and_blocks(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        CupidGb *gb = hardware(CUPID_GB_MODEL_CGB, true, fast != 0, 0);
        addresses(gb);
        cupid_gb_write_u8(gb, 0xff55, 3);
        const unsigned divider = gb->io_registers[4] * 64u + gb->div_counter;
        assert(cupid_gb_step(gb));
        copied(gb, 64);
        const unsigned duration = fast ? 132 : 136;
        assert(gb->elapsed_cycles == duration);
        assert(gb->io_registers[4] * 64u + gb->div_counter ==
               ((divider + duration / (fast ? 2u : 4u)) & 0x3fffu));
        assert(gb->apu.sample_acc == duration * UINT32_C(44100) % 4194304u);
        assert(gb->apu.buf_write == duration * UINT32_C(44100) / 4194304u);
        assert(cupid_gb_read_u8(gb, 0xff55) == 0xff);
        release(gb);
    }
}

static void test_open_bus_dma_decay(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        CupidGb *gb = hardware(CUPID_GB_MODEL_CGB, true, fast != 0, 0);
        addresses(gb);
        cupid_gb_write_u8(gb, 0xff51, 0x7f);
        cupid_gb_write_u8(gb, 0xff52, 0xf0);
        cupid_gb_write_u8(gb, 0xff55, 1);
        assert(cupid_gb_step(gb));
        /* The last ROM byte drives the bus. Two subsequent source slots have no driver
         * but still see that byte. The six-dot hold expires before the third. */
        for (unsigned i = 0; i < 32; ++i)
            assert(gb->video_ram[0x800 + i] == (i < 18 ? 0 : 0xff));
        release(gb);
    }
}

static void test_cartridge_bus_read_and_resume(void) {
    uint8_t rom[32768] = {0};
    rom[0x100] = 0xfa; /* LD A,($A800) */
    rom[0x102] = 0xa8;
    rom[0x143] = 0x80;
    rom[0x147] = 0x1a; /* MBC5 + RAM */
    rom[0x149] = 2;
    for (unsigned i = 0x134; i < 0x14d; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);

    for (unsigned fast = 0; fast < 2; ++fast) {
        CupidGb *gb = calloc(1, sizeof(*gb));
        assert(gb);
        cupid_gb_set_model(gb, CUPID_GB_MODEL_CGB);
        assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
        gb->double_speed = fast != 0;
        cupid_gb_write_u8(gb, 0xff40, 0);
        assert(!gb->ram_enabled && cupid_gb_step(gb));
        assert(gb->cpu.a == 0xa8 && gb->cpu.pc == 0x103);
        assert(gb->elapsed_cycles == (fast ? 8u : 16u));

        /* A write to disabled RAM still drives the cartridge pins. Accesses on the
         * separate WRAM bus and passive debugger reads cannot replace the held value. */
        cupid_gb_write_u8(gb, 0xa800, 0x5c);
        cupid_gb_write_u8(gb, 0xc000, 0x99);
        assert(cupid_gb_read_bus_u8(gb, 0xc000) == 0x99);
        assert(cupid_gb_read_u8(gb, 0xa800) == 0xff);
        assert(cupid_gb_debug_peek(gb, 0x100) == 0xfa);
        assert(cupid_gb_read_bus_u8(gb, 0xa800) == 0x5c);
        cupid_gb_tick(gb, 1);
        const size_t length = cupid_gb_state_size(gb);
        uint8_t *snapshot = malloc(length);
        assert(snapshot && cupid_gb_save_state(gb, snapshot, length, NULL) == CUPID_GB_STATE_OK);

        cupid_gb_tick(gb, fast ? 2 : 1);
        assert(cupid_gb_read_bus_u8(gb, 0xa800) == 0xff);
        assert(cupid_gb_load_state(gb, snapshot, length) == CUPID_GB_STATE_OK);
        assert(cupid_gb_read_bus_u8(gb, 0xa800) == 0x5c);
        cupid_gb_tick(gb, 1);
        assert(cupid_gb_read_bus_u8(gb, 0xa800) == (fast ? 0x5c : 0xff));
        if (fast)
            cupid_gb_tick(gb, 1);
        assert(cupid_gb_read_bus_u8(gb, 0xa800) == 0xff);

        cupid_gb_write_u8(gb, 0x0000, 0x0a);
        cupid_gb_write_u8(gb, 0xa800, 0x72);
        assert(gb->ram_enabled && cupid_gb_read_bus_u8(gb, 0xa800) == 0x72);
        free(snapshot);
        release(gb);
    }
}

static void test_mode_3_destination_conflict(void) {
    static const struct {
        uint16_t offset;
        uint8_t value;
    } writes[] = {{0x000, 0x80}, {0x002, 0x8d}, {0x800, 0xd7}, {0x803, 0x2e}, {0x804, 0x0b},
                  {0x807, 0x62}, {0x808, 0x3f}, {0x80b, 0x96}, {0x80c, 0x73}, {0x80f, 0xca},
                  {0x813, 0xfe}, {0x817, 0x32}, {0x81b, 0x66}, {0x81f, 0x9a}};

    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned bank = 0; bank < 2; ++bank) {
            CupidGb *gb = hardware(CUPID_GB_MODEL_CGB, true, fast != 0, 0);
            uint8_t expected[0x4000];
            for (unsigned i = 0; i < sizeof(expected); ++i)
                expected[i] = (uint8_t)(i * 17 + (i >> 13) * 0x51 + 9);
            memcpy(gb->video_ram, expected, sizeof(expected));
            for (unsigned i = 0; i < 32; ++i)
                cupid_gb_write_u8(gb, (uint16_t)(0xc000 + i), (uint8_t)(i * 13 + 7));

            /* At this fetch phase, address contention redirects several bytes into tile data
             * before 8800. The blocked CPU VRAM port lets the writes reach both banks. */
            for (unsigned i = 0; i < sizeof(writes) / sizeof(writes[0]); ++i) {
                expected[writes[i].offset] = writes[i].value;
                expected[0x2000 + writes[i].offset] = writes[i].value;
            }
            cupid_gb_write_u8(gb, 0xff4f, (uint8_t)bank);
            lcd_phase(gb, 600);
            assert((cupid_gb_read_u8(gb, 0xff41) & 3) == CUPID_GB_PPU_MODE_TRANSFER);
            addresses(gb);
            cupid_gb_write_u8(gb, 0xff55, 1);
            assert(cupid_gb_step(gb));
            assert(gb->elapsed_cycles == (fast ? 68u : 72u));
            assert(cupid_gb_read_u8(gb, 0xff55) == 0xff);
            assert(!gb->hdma_running);
            assert(memcmp(gb->video_ram, expected, sizeof(expected)) == 0);
            release(gb);
        }
    }
}

static void test_hblank_delay_and_next_block(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        CupidGb *gb = hardware(CUPID_GB_MODEL_CGB, true, fast != 0, 0);
        lcd_phase(gb, 500);
        addresses(gb);
        cupid_gb_write_u8(gb, 0xff55, 0x83);
        assert(!gb->hdma_pending);
        while (!gb->hdma_hblank_delay) {
            assert(gb->pixels.dot < 300);
            cupid_gb_tick_ppu_dots(gb, 1);
        }
        assert(gb->hdma_hblank_delay == 3);
        assert((cupid_gb_read_u8(gb, 0xff41) & 3) == (fast ? 3 : 0));
        cupid_gb_tick_ppu_dots(gb, 2);
        assert(gb->hdma_hblank_delay == 1 && !gb->hdma_pending);
        cupid_gb_tick_ppu_dots(gb, 1);
        assert(gb->hdma_pending);
        copied(gb, 0);
        assert(cupid_gb_step(gb));
        copied(gb, 16);
        assert(cupid_gb_read_u8(gb, 0xff55) == 2);
        cupid_gb_tick_ppu_dots(gb, 456);
        assert(gb->hdma_pending);
        assert(cupid_gb_step(gb));
        copied(gb, 32);
        assert(cupid_gb_read_u8(gb, 0xff55) == 1);
        release(gb);
    }
}

static void test_lcd_disable_requests_only_nonzero_mode(void) {
    const unsigned phases[] = {500, 600, 705, 706, 800};
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned phase = 0; phase < sizeof(phases) / sizeof(phases[0]); ++phase) {
            CupidGb *gb = hardware(CUPID_GB_MODEL_CGB, true, fast != 0, 0);
            lcd_phase(gb, 500);
            addresses(gb);
            cupid_gb_write_u8(gb, 0xff55, 0x83);
            cupid_gb_tick_ppu_dots(gb, (uint16_t)(phases[phase] - 500));
            const bool pending = gb->hdma_pending;
            const unsigned old_mode = cupid_gb_read_u8(gb, 0xff41) & 3;
            cupid_gb_write_u8(gb, 0xff40, 0);
            assert(gb->hdma_pending == (pending || old_mode != 0));
            assert(gb->hdma_hblank_delay == 0);
            assert(cupid_gb_step(gb));
            copied(gb, pending || old_mode ? 16 : 0);
            release(gb);
        }
    }
}

static void test_halt_and_stop_wake_permission(void) {
    for (unsigned fast = 0; fast < 2; ++fast) {
        for (unsigned stop = 0; stop < 2; ++stop) {
            CupidGb *gb = hardware(CUPID_GB_MODEL_CGB, true, fast != 0, stop ? 0x10 : 0x76);
            lcd_phase(gb, 500);
            addresses(gb);
            cupid_gb_write_u8(gb, 0xff55, 0x83);
            assert(cupid_gb_step(gb));
            assert(gb->cpu.halted || gb->cpu.stopped);
            assert(gb->hdma_allow_on_wake);
            while ((cupid_gb_read_u8(gb, 0xff41) & 3) != 0 || gb->hdma_hblank_delay) {
                assert(gb->pixels.dot < 350);
                assert(cupid_gb_step(gb));
            }
            assert(!gb->hdma_pending);
            copied(gb, 0);
            if (stop) {
                cupid_gb_write_u8(gb, 0xff00, 0x10);
                cupid_gb_set_buttons(gb, 0xef);
                assert(cupid_gb_step(gb));
                assert(!gb->cpu.stopped && gb->hdma_pending);
            } else {
                gb->interrupt_enable = gb->interrupt_flags = 4;
                assert(cupid_gb_step(gb));
                assert(!gb->cpu.halted && gb->hdma_pending);
                copied(gb, 0);
            }
            assert(cupid_gb_step(gb));
            copied(gb, 16);
            release(gb);
        }

        CupidGb *gb = hardware(CUPID_GB_MODEL_CGB, true, fast != 0, 0x76);
        lcd_phase(gb, 300);
        addresses(gb);
        cupid_gb_write_u8(gb, 0xff55, 0x83);
        assert(cupid_gb_step(gb));
        copied(gb, 16);
        assert(gb->cpu.halted && !gb->hdma_allow_on_wake);
        gb->interrupt_enable = gb->interrupt_flags = 4;
        assert(cupid_gb_step(gb));
        copied(gb, 16); /* Waking in the same HBlank must not repeat its block. */
        release(gb);
    }
}

static void test_non_color_transfer_registers_are_inaccessible(void) {
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb *gb = hardware((CupidGbModel)model, false, false, 0);
        addresses(gb);
        cupid_gb_write_u8(gb, 0xff55, 0x83);
        assert(cupid_gb_read_u8(gb, 0xff55) == 0xff);
        assert(!gb->hdma_active && !gb->hdma_pending);
        assert(cupid_gb_step(gb));
        copied(gb, 0);
        release(gb);
    }
}

int main(void) {
    test_start_boundary_and_latched_opcode();
    test_general_transfer_clocks_and_blocks();
    test_mode_3_destination_conflict();
    test_open_bus_dma_decay();
    test_cartridge_bus_read_and_resume();
    test_hblank_delay_and_next_block();
    test_lcd_disable_requests_only_nonzero_mode();
    test_halt_and_stop_wake_permission();
    test_non_color_transfer_registers_are_inaccessible();
}

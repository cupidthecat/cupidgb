#include "cupid/common/sha256.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CupidGb *machine(bool color, bool fast) {
    uint8_t rom[32768] = {0};
    rom[0x143] = color ? 0x80 : 0;
    for (unsigned i = 0x134; i <= 0x14c; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->double_speed = fast;
    gb->cpu.ime = false;
    gb->cpu.pc = 0xff80;
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff41, 0);
    cupid_gb_write_u8(gb, 0xff4b, 0xff);
    memset(gb->object_attribute_memory, 0, 160);
    for (unsigned i = 0; i < 0x2000; ++i)
        cupid_gb_write_u8(gb, (uint16_t)(0xc000 + i), (uint8_t)(i * 13 + 7));
    for (unsigned i = 0; i < (color ? 0x4000u : 0x2000u); ++i)
        gb->video_ram[i] = (uint8_t)(i * 17 + (i >> 13) * 0x51 + 9);
    return gb;
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void expect_digest(const uint8_t *data, size_t length, const char *expected) {
    static const char digits[] = "0123456789abcdef";
    uint8_t digest[32];
    char text[65];
    cupid_sha256(data, length, digest);
    for (unsigned i = 0; i < 32; ++i) {
        text[i * 2] = digits[digest[i] >> 4];
        text[i * 2 + 1] = digits[digest[i] & 15];
    }
    text[64] = '\0';
    if (strcmp(text, expected) != 0)
        fprintf(stderr, "Video bus digest: expected %s, got %s\n", expected, text);
    assert(strcmp(text, expected) == 0);
}

static uint32_t fingerprint(const uint8_t *bytes, size_t length) {
    uint32_t result = UINT32_C(2166136261);
    for (size_t i = 0; i < length; ++i)
        result = (result ^ bytes[i]) * UINT32_C(16777619);
    return result;
}

static void test_shared_video_port(void) {
    static const struct {
        bool fast;
        uint8_t bank, page, blocks;
        uint16_t warm, destination, duration;
        uint8_t status;
        const char *vram, *oam;
    } cases[] = {
        {false, 0, 0x80, 1, 0, 0x8800, 40, 0xff,
         "7826961f3f7f2d9ac59353a97c46d2773e0116cddd52bd0cc8418981ec040b29",
         "c22b112a53cc281d4cf51335f2d70a7ea129962db47bbd8b4af2e65268e5428c"},
        {true, 0, 0x80, 2, 0, 0xfff0, 36, 0x80,
         "2a92e2e80b00488815e8a96cbe3fd9f2f21dbf2f310f29a45e5e8de2a679e0e6",
         "b764ddbd56582899165fe549ecf082d6ecf8d2473513f7e79a2c0b289d5a0630"},
        {false, 1, 0x80, 1, 161, 0x8800, 40, 0xff,
         "b00d909aadb0875394d6d462a2bc364c63c7b63069cb6b7af78ceb4bcff2184a",
         "85dcf683510ff73bf216cb9d16a18e16196ee0e7816d8591b51fe62743eac040"},
        {true, 0, 0x00, 2, 162, 0x8800, 68, 0xff,
         "59d2a8328834d8f6702750a0d26a0dcfc59386e1a3380cf40a7cdeac5b754535",
         "b393978842a0fa3d3e1470196f098f473f9678e72463cb65ec4ab5581856c2e4"},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CupidGb *gb = machine(true, cases[i].fast);
        cupid_gb_write_u8(gb, 0xff4f, cases[i].bank);
        cupid_gb_write_u8(gb, 0xff40, 0x91);
        cupid_gb_tick_ppu_dots(gb, 600);
        cupid_gb_write_u8(gb, 0xff51, 1);
        cupid_gb_write_u8(gb, 0xff52, 15);
        cupid_gb_write_u8(gb, 0xff53, (uint8_t)(cases[i].destination >> 8));
        cupid_gb_write_u8(gb, 0xff54, (uint8_t)(cases[i].destination | 15));
        cupid_gb_write_u8(gb, 0xff80, 0);
        cupid_gb_write_u8(gb, 0x7fff, 0);
        cupid_gb_write_u8(gb, 0xff46, cases[i].page);
        cupid_gb_tick(gb, cases[i].warm);
        cupid_gb_write_u8(gb, 0xff55, (uint8_t)(cases[i].blocks - 1));
        const uint64_t before = gb->elapsed_cycles;
        assert(cupid_gb_step(gb));
        assert(gb->elapsed_cycles - before == cases[i].duration);
        assert(gb->cpu.pc == 0xff81);
        assert(cupid_gb_read_u8(gb, 0xff55) == cases[i].status);
        /* Both engines share the source port. Fetches can replace OAM bytes
         * already copied by DMA; correct destination bytes alone are insufficient. */
        expect_digest(gb->video_ram, 0x4000, cases[i].vram);
        expect_digest(gb->object_attribute_memory, 160, cases[i].oam);
        release(gb);
    }
}

typedef enum Program {
    HALT_PROGRAM,
    INTERRUPT_STACK_IO,
    STOP_PROGRAM,
    SPEED_SWITCH_PENDING,
    STAT_STORE,
    SPEED_SWITCH_WAIT
} Program;

static void test_cpu_video_edges(void) {
    static const struct {
        Program program;
        bool color, fast;
        uint16_t warm;
        uint8_t cursor, phase, duration;
        uint16_t pc, sp, divider;
        uint8_t timer, flags, status, next_cursor, next_phase;
        bool dma_active, halted, stopped, next_fast;
        uint32_t oam, vram;
    } cases[] = {
        {HALT_PROGRAM, false, false, 248,  159,  0,     4,     0xff81,     0xc100,    4, 252, 0,
         0x80,         160,   0,     true, true, false, false, 0xa543de4d, 0x20779dc5},
        {INTERRUPT_STACK_IO,
         true,
         false,
         248,
         160,
         0,
         20,
         0x0040,
         0xff40,
         20,
         253,
         0,
         0x80,
         160,
         0,
         false,
         false,
         false,
         false,
         0xd110f3c5,
         0x441a1dc5},
        {STOP_PROGRAM, true, false, 248,  159,   0,    8,     0xff82,     0xc100,    0, 252, 0,
         0x80,         160,  0,     true, false, true, false, 0xdfdfcbc4, 0x441a1dc5},
        {SPEED_SWITCH_PENDING,
         true,
         false,
         76,
         0,
         1,
         4,
         0xff81,
         0xc100,
         0,
         252,
         4,
         0x83,
         2,
         1,
         true,
         false,
         false,
         false,
         0x77ec474a,
         0x441a1dc5},
        {STAT_STORE, true, true, 248, 159,   0,     6,     0xff82, 0xc100,     12,        252,
         2,          0x88, 160,  0,   false, false, false, true,   0xdfdfcbc4, 0x441a1dc5},
        {SPEED_SWITCH_WAIT,
         true,
         true,
         248,
         159,
         0,
         4,
         0xff82,
         0xc100,
         0,
         252,
         0,
         0x80,
         160,
         0,
         false,
         true,
         false,
         false,
         0xdfdfcbc4,
         0x441a1dc5},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CupidGb *gb = machine(cases[i].color, cases[i].fast);
        cupid_gb_write_u8(gb, 0xff26, 0);
        cupid_gb_write_u8(gb, 0xff07, 0);
        cupid_gb_write_u8(gb, 0xff42, 0);
        cupid_gb_write_u8(gb, 0xff43, 0);
        cupid_gb_write_u8(gb, 0xff4a, 0);
        cupid_gb_write_u8(gb, 0xff45, 0xff);
        memset(gb->object_attribute_memory, 0xcc, 160);
        cupid_gb_write_u8(gb, 0xff40, 0x91);
        cupid_gb_tick(gb, (uint16_t)(cases[i].warm / (cases[i].fast ? 2 : 4)));
        cupid_gb_write_u8(gb, 0xff04, 0);
        cupid_gb_write_u8(gb, 0xff05, 0xfc);
        cupid_gb_write_u8(gb, 0xff06, 0x42);
        cupid_gb_write_u8(gb, 0xff07, 5);
        cupid_gb_write_u8(gb, 0xffff, 0);
        cupid_gb_write_u8(gb, 0xff0f, 0);
        cupid_gb_write_u8(gb, 0xff00, 0x10);
        for (unsigned j = 0; j < 16; ++j)
            cupid_gb_write_u8(gb, (uint16_t)(0xff80 + j), 0);
        gb->cpu.sp = 0xc100;
        gb->cpu.a = 8;
        gb->cpu.f = 0;
        gb->cpu.b = 0;
        gb->cpu.c = 0x91;
        gb->cpu.h = 0x80;
        gb->cpu.l = 0;
        switch (cases[i].program) {
            case HALT_PROGRAM:
                cupid_gb_write_u8(gb, 0xff80, 0x76);
                break;
            case INTERRUPT_STACK_IO:
                gb->cpu.pc = 0x0091;
                gb->cpu.sp = 0xff42;
                gb->cpu.ime = true;
                cupid_gb_write_u8(gb, 0xffff, 1);
                cupid_gb_write_u8(gb, 0xff0f, 1);
                break;
            case STAT_STORE:
                cupid_gb_write_u8(gb, 0xff80, 0xe0);
                cupid_gb_write_u8(gb, 0xff81, 0x41);
                break;
            case SPEED_SWITCH_PENDING:
                gb->cpu.c = 0x41;
                cupid_gb_write_u8(gb, 0xff81, 0xe2);
                cupid_gb_write_u8(gb, 0xffff, 4);
                cupid_gb_write_u8(gb, 0xff0f, 4);
                /* fall through */
            case SPEED_SWITCH_WAIT:
                cupid_gb_write_u8(gb, 0xff4d, 1);
                /* fall through */
            case STOP_PROGRAM:
                cupid_gb_write_u8(gb, 0xff80, 0x10);
                break;
        }
        gb->apu.sample_acc = 0;
        gb->apu.buf_write = 0;
        /* Seed a public instruction boundary inside an existing transfer. The
         * instruction owns subsequent clocks, including wake-only DMA credit. */
        gb->dma_source_base = 0x8000;
        gb->dma_index = cases[i].cursor;
        gb->dma_phase = cases[i].phase;
        gb->dma_active = true;
        gb->dma_start_delay = 0;
        gb->dma_restart_pending = false;
        gb->dma_restart_delay = 0;
        const uint64_t before = gb->elapsed_cycles;
        assert(cupid_gb_step(gb));
        assert(gb->elapsed_cycles - before == cases[i].duration);
        assert(gb->cpu.pc == cases[i].pc && gb->cpu.sp == cases[i].sp);
        assert(gb->cpu.halted == cases[i].halted && gb->cpu.stopped == cases[i].stopped);
        assert(gb->double_speed == cases[i].next_fast);
        assert(gb->io_registers[4] * 256u + gb->div_counter * 4u == cases[i].divider);
        assert(cupid_gb_read_u8(gb, 0xff05) == cases[i].timer);
        assert((cupid_gb_read_u8(gb, 0xff0f) & 31) == cases[i].flags);
        assert(cupid_gb_read_u8(gb, 0xff41) == cases[i].status);
        assert(gb->dma_index == cases[i].next_cursor && gb->dma_active == cases[i].dma_active);
        if (gb->dma_active)
            assert(gb->dma_phase == cases[i].next_phase);
        assert(gb->apu.buf_write == 0);
        assert(gb->apu.sample_acc == (uint64_t)cases[i].duration * 44100u);
        assert(fingerprint(gb->object_attribute_memory, 160) == cases[i].oam);
        assert(fingerprint(gb->video_ram, cases[i].color ? 0x4000 : 0x2000) == cases[i].vram);
        release(gb);
    }
}

static void test_cpu_video_continuation(void) {
    typedef struct Observation {
        uint16_t cycles;
        uint32_t audio;
        uint16_t pc, sp, divider;
        uint8_t timer, flags, status, cursor, phase;
        bool active, fast, stopped;
        uint32_t oam;
    } Observation;

    static const struct {
        Program program;
        bool fast;
        uint16_t warm;
        uint8_t cursor, phase;
        Observation expected[4];
    } cases[] = {
        {STAT_STORE,
         true,
         600,
         0,
         3,
         {{6, 264600, 0xff82, 0xc100, 12, 252, 0, 139, 3, 3, true, true, false, 0xb79cc93d},
          {8, 352800, 0xff83, 0xc100, 16, 253, 0, 139, 4, 3, true, true, false, 0x3132edcd},
          {10, 441000, 0xff84, 0xc100, 20, 253, 0, 139, 5, 3, true, true, false, 0xf12923fc},
          {12, 529200, 0xff85, 0xc100, 24, 253, 0, 139, 6, 3, true, true, false, 0x43b2050a}}},
        {SPEED_SWITCH_PENDING,
         false,
         76,
         0,
         1,
         {{4, 176400, 0xff81, 0xc100, 0, 252, 4, 131, 2, 1, true, false, false, 0x77ec474a},
          {10, 264600, 0xff82, 0xc100, 4, 252, 4, 139, 4, 1, true, true, false, 0x3132edcd},
          {12, 352800, 0xff83, 0xc100, 8, 252, 4, 139, 5, 1, true, true, false, 0xf12923fc},
          {14, 441000, 0xff84, 0xc100, 12, 252, 4, 139, 6, 1, true, true, false, 0x43b2050a}}},
        {SPEED_SWITCH_PENDING,
         false,
         600,
         0,
         1,
         {{4, 176400, 0xff81, 0xc100, 0, 252, 4, 131, 2, 1, true, false, false, 0x77ec474a},
          {10, 264600, 0xff82, 0xc100, 4, 252, 4, 139, 4, 1, true, true, false, 0x3132edcd},
          {12, 352800, 0xff83, 0xc100, 8, 252, 4, 139, 5, 1, true, true, false, 0xf12923fc},
          {14, 441000, 0xff84, 0xc100, 12, 252, 4, 139, 6, 1, true, true, false, 0x43b2050a}}},
        {SPEED_SWITCH_PENDING,
         true,
         600,
         1,
         2,
         {{2, 88200, 0xff81, 0xc100, 0, 252, 4, 131, 3, 2, true, false, false, 0xae74506c},
          {8, 264600, 0xff82, 0xc100, 4, 252, 4, 139, 5, 2, true, false, false, 0x1fb3c42d},
          {12, 441000, 0xff83, 0xc100, 8, 252, 4, 139, 6, 2, true, false, false, 0x8070d92b},
          {16, 617400, 0xff84, 0xc100, 12, 252, 4, 139, 7, 2, true, false, false, 0xce5bb39a}}},
        {INTERRUPT_STACK_IO,
         true,
         76,
         0,
         0,
         {{10, 441000, 0x0040, 0xff40, 20, 253, 0, 131, 6, 0, true, true, false, 0xe3671206},
          {12, 529200, 0x0041, 0xff40, 24, 253, 0, 131, 7, 0, true, true, false, 0x4e412e48},
          {14, 617400, 0x0042, 0xff40, 28, 253, 0, 131, 8, 0, true, true, false, 0x4be16cfc},
          {16, 705600, 0x0043, 0xff40, 32, 254, 0, 131, 9, 0, true, true, false, 0x61b2bbc1}}},
        {STOP_PROGRAM,
         false,
         76,
         0,
         0,
         {{8, 352800, 0xff82, 0xc100, 0, 252, 0, 131, 1, 0, true, false, true, 0x5243f280},
          {12, 529200, 0xff82, 0xc100, 0, 252, 0, 131, 1, 0, true, false, true, 0x5243f280},
          {24, 882000, 0xff82, 0xc100, 4, 252, 16, 131, 4, 0, true, false, false, 0x3132edcd},
          {28, 1058400, 0xff83, 0xc100, 8, 252, 16, 131, 5, 0, true, false, false, 0x0e122dd9}}}};

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CupidGb *gb = machine(true, cases[i].fast);
        cupid_gb_write_u8(gb, 0xff26, 0);
        cupid_gb_write_u8(gb, 0xff07, 0);
        cupid_gb_write_u8(gb, 0xff42, 0);
        cupid_gb_write_u8(gb, 0xff43, 0);
        cupid_gb_write_u8(gb, 0xff4a, 0);
        cupid_gb_write_u8(gb, 0xff45, 0xff);
        memset(gb->object_attribute_memory, 0xcc, 160);
        cupid_gb_write_u8(gb, 0xff40, 0x91);
        cupid_gb_tick(gb, (uint16_t)(cases[i].warm / (cases[i].fast ? 2 : 4)));
        cupid_gb_write_u8(gb, 0xff04, 0);
        cupid_gb_write_u8(gb, 0xff05, 0xfc);
        cupid_gb_write_u8(gb, 0xff06, 0x42);
        cupid_gb_write_u8(gb, 0xff07, 5);
        cupid_gb_write_u8(gb, 0xffff, 0);
        cupid_gb_write_u8(gb, 0xff0f, 0);
        cupid_gb_write_u8(gb, 0xff00, 0x10);
        cupid_gb_set_buttons(gb, 0xff);
        for (unsigned j = 0; j < 0x7f; ++j)
            cupid_gb_write_u8(gb, (uint16_t)(0xff80 + j), 0);
        gb->cpu.pc = 0xff80;
        gb->cpu.sp = 0xc100;
        gb->cpu.a = 8;
        gb->cpu.f = 0;
        gb->cpu.b = 0;
        gb->cpu.c = 0x91;
        gb->cpu.h = 0x80;
        gb->cpu.l = 0;
        if (cases[i].program == STAT_STORE) {
            cupid_gb_write_u8(gb, 0xff80, 0xe0);
            cupid_gb_write_u8(gb, 0xff81, 0x41);
        } else if (cases[i].program == SPEED_SWITCH_PENDING) {
            gb->cpu.c = 0x41;
            cupid_gb_write_u8(gb, 0xff80, 0x10);
            cupid_gb_write_u8(gb, 0xff81, 0xe2);
            cupid_gb_write_u8(gb, 0xff4d, 1);
            cupid_gb_write_u8(gb, 0xffff, 4);
            cupid_gb_write_u8(gb, 0xff0f, 4);
        } else if (cases[i].program == INTERRUPT_STACK_IO) {
            gb->cpu.pc = 0x0091;
            gb->cpu.sp = 0xff42;
            gb->cpu.ime = true;
            cupid_gb_write_u8(gb, 0xffff, 1);
            cupid_gb_write_u8(gb, 0xff0f, 1);
        } else {
            assert(cases[i].program == STOP_PROGRAM);
            cupid_gb_write_u8(gb, 0xff80, 0x10);
        }
        gb->apu.sample_acc = 0;
        gb->apu.buf_write = 0;
        gb->dma_source_base = 0x8000;
        gb->dma_index = cases[i].cursor;
        gb->dma_phase = cases[i].phase;
        gb->dma_active = true;
        gb->dma_start_delay = 0;
        gb->dma_restart_pending = false;
        gb->dma_restart_delay = 0;
        gb->oam_vram_row = UINT16_MAX;
        const uint64_t before = gb->elapsed_cycles;
        /* Keep the borrowed bus edge and switch continuation inside public
         * instruction steps. Aggregate cycle totals alone miss these changes. */
        for (unsigned step = 0; step < 4; ++step) {
            const Observation *expected = &cases[i].expected[step];
            if (cases[i].program == STOP_PROGRAM && step == 2)
                cupid_gb_set_buttons(gb, 0xef);
            assert(cupid_gb_step(gb));
            assert(gb->elapsed_cycles - before == expected->cycles);
            assert(gb->cpu.pc == expected->pc && gb->cpu.sp == expected->sp);
            assert(!gb->cpu.halted && gb->cpu.stopped == expected->stopped);
            assert(gb->double_speed == expected->fast);
            assert(gb->io_registers[4] * 256u + gb->div_counter * 4u == expected->divider);
            assert(cupid_gb_read_u8(gb, 0xff05) == expected->timer);
            assert((cupid_gb_read_u8(gb, 0xff0f) & 31) == expected->flags);
            assert(cupid_gb_read_u8(gb, 0xff41) == expected->status);
            assert(gb->dma_index == expected->cursor && gb->dma_active == expected->active);
            if (gb->dma_active)
                assert(gb->dma_phase == expected->phase);
            assert(gb->apu.buf_write == 0 && gb->apu.sample_acc == expected->audio);
            assert(fingerprint(gb->object_attribute_memory, 160) == expected->oam);
            assert(fingerprint(gb->video_ram, 0x4000) == UINT32_C(0x441a1dc5));
        }
        release(gb);
    }
}

static void test_acknowledge_video_edge(void) {
    for (unsigned reload = 0; reload < 2; ++reload) {
        CupidGb *gb = machine(false, false);
        cupid_gb_write_u8(gb, 0xff26, 0);
        cupid_gb_write_u8(gb, 0xff07, 0);
        cupid_gb_write_u8(gb, 0xff42, 0);
        cupid_gb_write_u8(gb, 0xff43, 0);
        cupid_gb_write_u8(gb, 0xff4a, 0);
        cupid_gb_write_u8(gb, 0xff45, 0xff);
        memset(gb->object_attribute_memory, 0xcc, 160);
        cupid_gb_write_u8(gb, 0xff40, 0x91);
        cupid_gb_tick(gb, 21);
        cupid_gb_write_u8(gb, 0xff04, 0);
        cupid_gb_write_u8(gb, 0xff05, 0xfc);
        cupid_gb_write_u8(gb, 0xff06, 0x42);
        cupid_gb_write_u8(gb, 0xff07, 5);
        cupid_gb_write_u8(gb, 0xffff, 0);
        cupid_gb_write_u8(gb, 0xff0f, 4);
        if (reload) {
            gb->io_registers[5] = 0;
            gb->tima_overflow_delay = 1;
        }
        gb->apu.sample_acc = 0;
        gb->apu.buf_write = 0;
        /* This fixture starts inside the final DMA byte, before any shared
         * fetch has retained a row. It intentionally bypasses the start delay. */
        gb->dma_source_base = 0x8000;
        gb->dma_index = 159;
        gb->dma_phase = 3;
        gb->dma_active = true;
        gb->dma_start_delay = 0;
        gb->dma_restart_pending = false;
        gb->dma_restart_delay = 0;
        gb->oam_vram_row = UINT16_MAX;
        const uint64_t before = gb->elapsed_cycles;
        const uint16_t pc = gb->cpu.pc, sp = gb->cpu.sp;
        cupid_gb_acknowledge_interrupt(gb, 4);
        assert(gb->elapsed_cycles - before == 4);
        assert(gb->cpu.pc == pc && gb->cpu.sp == sp);
        assert(gb->io_registers[4] * 256u + gb->div_counter * 4u == 4);
        /* The old request is cleared before the divider's reload event. A new
         * request from that event must remain pending after acknowledgment. */
        assert((cupid_gb_read_u8(gb, 0xff0f) & 31) == (reload ? 4 : 0));
        assert(cupid_gb_read_u8(gb, 0xff05) == (reload ? 0x42 : 0xfc));
        assert(gb->dma_index == 160 && gb->dma_active && gb->dma_phase == 3);
        assert(gb->apu.buf_write == 0 && gb->apu.sample_acc == 176400);
        assert(fingerprint(gb->object_attribute_memory, 160) == UINT32_C(0x9d10a1e9));
        assert(fingerprint(gb->video_ram, 0x2000) == UINT32_C(0x20779dc5));
        release(gb);
    }
}

static void test_object_word_sample_edge(void) {
    const unsigned mutation_dots[] = {548, 550};
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        CupidGb *gb = machine(true, true);
        cupid_gb_write_u8(gb, 0xff4a, 1);
        for (unsigned i = 0; i < sizeof(gb->video_ram); ++i)
            gb->video_ram[i] = (uint8_t)(i * 17 + 9);
        gb->object_attribute_memory[0] = 17;
        gb->object_attribute_memory[1] = 8;
        gb->object_attribute_memory[2] = 3;
        gb->object_attribute_memory[3] = 9;
        cupid_gb_write_u8(gb, 0xff40, 0x93);
        for (unsigned dot = 0; dot <= 555; ++dot) {
            /* Change the physical OAM port immediately before either alignment
             * edge. Selection has already latched Y/X; tile/flags have not. */
            if (dot == mutation_dots[scenario]) {
                gb->object_attribute_memory[2] = 7;
                gb->object_attribute_memory[3] = 1;
            }
            cupid_gb_tick_ppu_dots(gb, 1);
            if (dot == 550)
                assert(gb->oam_search_word == 0);
            if (dot == 551) {
                assert(gb->oam_search_word == 0x0107);
                assert(gb->pixels.object_attributes == 1);
            }
            if (dot == 553) {
                assert(gb->pixels.object_address == 0x70);
                assert(gb->pixels.object_low == gb->video_ram[0x70]);
            }
            if (dot == 555)
                assert(gb->pixels.object_high == gb->video_ram[0x71]);
        }
        release(gb);
    }
}

int main(void) {
    test_shared_video_port();
    test_cpu_video_edges();
    test_cpu_video_continuation();
    test_acknowledge_video_edge();
    test_object_word_sample_edge();
    return 0;
}

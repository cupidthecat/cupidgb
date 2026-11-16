#include "cupid/gb/apu.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/debugger.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Profile {
    CupidGbModel model;
    bool fast;
} Profile;

static const Profile profiles[] = {
    {CUPID_GB_MODEL_DMG_ABC, false}, {CUPID_GB_MODEL_MGB, false}, {CUPID_GB_MODEL_CGB, false},
    {CUPID_GB_MODEL_CGB, true},      {CUPID_GB_MODEL_SGB, false}, {CUPID_GB_MODEL_SGB2, false},
};

static const uint8_t illegal_opcodes[] = {
    0xd3, 0xdb, 0xdd, 0xe3, 0xe4, 0xeb, 0xec, 0xed, 0xf4, 0xfc, 0xfd,
};

static unsigned machine_cycles(Profile profile) {
    return profile.fast ? 2u : 4u;
}

static void prepare(CupidGb *gb, Profile profile, uint8_t opcode) {
    uint8_t rom[32768] = {0};
    memcpy(rom + 0x134, "CPU LOCKUP", 10);
    rom[0x100] = opcode;
    rom[0x143] = profile.model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    if (profile.model == CUPID_GB_MODEL_SGB || profile.model == CUPID_GB_MODEL_SGB2) {
        rom[0x146] = 3;
        rom[0x14b] = 0x33;
    }
    for (unsigned i = 0x134; i < 0x14d; ++i)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
    cupid_gb_set_serial_output_callback(gb, NULL, NULL);
    cupid_gb_set_model(gb, profile.model);
    /* Plain and observed stepping compare complete snapshots of one cold image. */
    cupid_gb_set_power_on_seed(gb, 0);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    gb->cpu.pc = 0x100;
    gb->cpu.sp = 0xfffe;
    gb->cpu.a = 0x5a;
    gb->cpu.f = 0xa0;
    gb->cpu.b = 0x12;
    gb->cpu.c = 0x34;
    gb->cpu.d = 0x56;
    gb->cpu.e = 0x78;
    gb->cpu.h = 0xc0;
    gb->cpu.l = 0;
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0;
    gb->cpu.halted = gb->cpu.just_halted = gb->cpu.stopped = gb->cpu.halt_bug = false;
    gb->double_speed = profile.fast;
    cupid_gb_write_u8(gb, 0xff40, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    cupid_gb_write_u8(gb, 0xff0f, 0);
    cupid_gb_write_u8(gb, 0xffff, 0xff);
    cupid_gb_write_u8(gb, 0xc000, 0x81);
}

static void assert_registers(const CupidGbCpu *cpu, const CupidGbCpu *before) {
    assert(cpu->a == before->a && cpu->f == before->f);
    assert(cpu->b == before->b && cpu->c == before->c);
    assert(cpu->d == before->d && cpu->e == before->e);
    assert(cpu->h == before->h && cpu->l == before->l && cpu->sp == before->sp);
}

static uint8_t *save(const CupidGb *gb, size_t *size) {
    *size = cupid_gb_state_size(gb);
    assert(*size);
    uint8_t *bytes = malloc(*size);
    size_t written = 0;
    assert(bytes && cupid_gb_save_state(gb, bytes, *size, &written) == CUPID_GB_STATE_OK);
    assert(written == *size);
    /* The corrected hardware state fits the existing native schema. */
    assert(bytes[8] == 12 && bytes[9] == 0 && bytes[10] == 0 && bytes[11] == 0);
    return bytes;
}

static void assert_same_state(const CupidGb *plain, const CupidGb *observed) {
    size_t plain_size, observed_size;
    uint8_t *plain_bytes = save(plain, &plain_size);
    uint8_t *observed_bytes = save(observed, &observed_size);
    assert(plain_size == observed_size && !memcmp(plain_bytes, observed_bytes, plain_size));
    free(observed_bytes);
    free(plain_bytes);
}

static void step_pair(CupidGb *plain, CupidGb *observed, CupidGbStepEvent event, uint16_t address,
                      uint8_t opcode, unsigned cycles) {
    const uint64_t before = plain->elapsed_cycles;
    assert(observed->elapsed_cycles == before);
    CupidGbStepTrace trace = {CUPID_GB_STEP_INTERRUPT, 0xffff, 0xff};
    assert(cupid_gb_step(plain) && cupid_gb_step_observed(observed, &trace));
    assert(trace.event == event && trace.address == address && trace.opcode == opcode);
    assert(plain->elapsed_cycles == before + cycles && observed->elapsed_cycles == before + cycles);
}

static void test_illegal_matrix(CupidGb *plain, CupidGb *observed) {
    for (size_t profile_index = 0; profile_index < sizeof(profiles) / sizeof(profiles[0]);
         ++profile_index) {
        const Profile profile = profiles[profile_index];
        const unsigned cycles = machine_cycles(profile);
        for (size_t index = 0; index < sizeof(illegal_opcodes); ++index) {
            const uint8_t opcode = illegal_opcodes[index];
            for (unsigned ime = 0; ime < 3; ++ime) {
                prepare(plain, profile, opcode);
                prepare(observed, profile, opcode);
                plain->cpu.ime = observed->cpu.ime = ime == 1;
                uint16_t address = 0x100;
                if (ime == 2) {
                    plain->rom[0x100] = observed->rom[0x100] = 0xfb;
                    plain->rom[0x101] = observed->rom[0x101] = opcode;
                    step_pair(plain, observed, CUPID_GB_STEP_INSTRUCTION, address, 0xfb, cycles);
                    assert(!plain->cpu.ime && plain->cpu.ime_delay == 1);
                    ++address;
                }
                const CupidGbCpu before = plain->cpu;
                step_pair(plain, observed, CUPID_GB_STEP_ILLEGAL_OPCODE, address, opcode, cycles);
                assert_registers(&plain->cpu, &before);
                assert(plain->cpu.pc == address + 1u && plain->cpu.halted);
                assert(!plain->cpu.just_halted && !plain->cpu.stopped && !plain->cpu.halt_bug);
                assert(!plain->interrupt_enable && !plain->interrupt_flags);
                assert(plain->cpu.ime == (ime != 0) && !plain->cpu.ime_delay);
                assert_same_state(plain, observed);

                cupid_gb_write_u8(plain, 0xff0f, 0x1f);
                cupid_gb_write_u8(observed, 0xff0f, 0x1f);
                for (unsigned idle = 0; idle < 8; ++idle) {
                    step_pair(plain, observed, CUPID_GB_STEP_IDLE, (uint16_t)(address + 1u), 0,
                              cycles);
                    assert(plain->cpu.halted && plain->cpu.pc == address + 1u);
                    assert(!plain->interrupt_enable && plain->interrupt_flags == 0x1f);
                    assert_registers(&plain->cpu, &before);
                }
                assert_same_state(plain, observed);

                /* A debugger can deliberately release this ordinary HALT state.
                 * Pending requests alone could not enable any interrupt. */
                cupid_gb_write_u8(plain, 0xffff, 1);
                cupid_gb_write_u8(observed, 0xffff, 1);
                step_pair(plain, observed, ime ? CUPID_GB_STEP_INTERRUPT : CUPID_GB_STEP_IDLE,
                          (uint16_t)(address + 1u), 0, cycles * (ime ? 6u : 1u));
                const uint16_t resumed = ime ? 0x40 : (uint16_t)(address + 1u);
                assert(!plain->cpu.halted && plain->cpu.pc == resumed);
                if (ime) {
                    assert(plain->cpu.sp == before.sp - 2u && !plain->cpu.ime);
                    assert(cupid_gb_read_u8(plain, plain->cpu.sp) == (uint8_t)(address + 1u));
                    assert(cupid_gb_read_u8(plain, (uint16_t)(plain->cpu.sp + 1u)) == 1);
                }
                step_pair(plain, observed, CUPID_GB_STEP_INSTRUCTION, resumed, 0, cycles);
                assert(plain->cpu.pc == resumed + 1u);
                assert_same_state(plain, observed);
            }
        }
    }
}

static void test_cb_legality(CupidGb *gb) {
    for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
        for (unsigned opcode = 0; opcode < 256; ++opcode) {
            prepare(gb, profiles[p], 0xcb);
            gb->rom[0x101] = (uint8_t)opcode;
            CupidGbStepTrace trace;
            assert(cupid_gb_step_observed(gb, &trace));
            assert(trace.event == CUPID_GB_STEP_INSTRUCTION && trace.address == 0x100 &&
                   trace.opcode == 0xcb);
            assert(!gb->cpu.halted && gb->cpu.pc == 0x102 && gb->interrupt_enable == 0xff);
        }
    }
}

static void test_fetches(CupidGb *gb) {
    for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
        const unsigned cycles = machine_cycles(profiles[p]);
        prepare(gb, profiles[p], 0x76);
        gb->rom[0x101] = 0xd3;
        gb->interrupt_enable = gb->interrupt_flags = 1;
        CupidGbStepTrace trace;
        assert(cupid_gb_step_observed(gb, &trace));
        assert(trace.event == CUPID_GB_STEP_INSTRUCTION && trace.opcode == 0x76);
        assert(gb->cpu.halt_bug && !gb->cpu.halted && gb->cpu.pc == 0x101);
        uint64_t before = gb->elapsed_cycles;
        assert(cupid_gb_step_observed(gb, &trace));
        assert(trace.event == CUPID_GB_STEP_ILLEGAL_OPCODE && trace.address == 0x101 &&
               trace.opcode == 0xd3);
        assert(gb->cpu.pc == 0x101 && gb->cpu.halted && !gb->cpu.halt_bug);
        assert(!gb->interrupt_enable && gb->elapsed_cycles == before + cycles);

        prepare(gb, profiles[p], 0xd3);
        gb->cpu.ime = true;
        gb->interrupt_enable = gb->interrupt_flags = 1;
        before = gb->elapsed_cycles;
        assert(cupid_gb_step_observed(gb, &trace));
        assert(trace.event == CUPID_GB_STEP_INTERRUPT && trace.address == 0x100 && !trace.opcode);
        assert(!gb->cpu.halted && gb->interrupt_enable == 1 && gb->cpu.pc == 0x40);
        assert(gb->elapsed_cycles == before + cycles * 5u);

        prepare(gb, profiles[p], 0);
        gb->cpu.pc = 0xffff;
        gb->interrupt_enable = 0xdd;
        assert(cupid_gb_step_observed(gb, &trace));
        assert(trace.event == CUPID_GB_STEP_ILLEGAL_OPCODE && trace.address == 0xffff &&
               trace.opcode == 0xdd);
        assert(!gb->cpu.pc && !gb->interrupt_enable && gb->cpu.halted);

        prepare(gb, profiles[p], 0);
        cupid_gb_write_u8(gb, 0x8000, 0xd3);
        gb->cpu.pc = 0x8000;
        assert(cupid_gb_step_observed(gb, &trace));
        assert(trace.event == CUPID_GB_STEP_ILLEGAL_OPCODE && trace.address == 0x8000 &&
               trace.opcode == 0xd3);

        prepare(gb, profiles[p], 0);
        cupid_gb_write_u8(gb, 0x8000, 0xd3);
        cupid_gb_write_u8(gb, 0xff40, 0x91);
        for (unsigned tick = 0; tick < 400 && (cupid_gb_read_u8(gb, 0xff41) & 3u) != 3u; ++tick)
            cupid_gb_tick(gb, 1);
        assert((cupid_gb_read_u8(gb, 0xff41) & 3u) == 3u);
        assert(cupid_gb_debug_peek(gb, 0x8000) == 0xd3 && cupid_gb_read_u8(gb, 0x8000) == 0xff);
        gb->cpu.pc = 0x8000;
        before = gb->elapsed_cycles;
        assert(cupid_gb_step_observed(gb, &trace));
        assert(trace.event == CUPID_GB_STEP_INSTRUCTION && trace.address == 0x8000 &&
               trace.opcode == 0xff);
        assert(!gb->cpu.halted && gb->cpu.pc == 0x38 && gb->interrupt_enable == 0xff);
        assert(gb->elapsed_cycles == before + cycles * 4u);
    }
}

typedef struct SerialCapture {
    unsigned count;
    uint8_t byte;
    uint64_t cycle;
} SerialCapture;

static void serial_output(const CupidGb *gb, uint8_t byte, void *user_data) {
    SerialCapture *capture = user_data;
    ++capture->count;
    capture->byte = byte;
    capture->cycle = gb->elapsed_cycles;
}

static void arm_peripherals(CupidGb *gb, SerialCapture *capture) {
    cupid_gb_set_serial_output_callback(gb, serial_output, capture);
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff24, 0x77);
    cupid_gb_write_u8(gb, 0xff25, 0x11);
    cupid_gb_write_u8(gb, 0xff11, 0x80);
    cupid_gb_write_u8(gb, 0xff12, 0xf0);
    cupid_gb_write_u8(gb, 0xff13, 0x40);
    cupid_gb_write_u8(gb, 0xff14, 0x87);
    cupid_gb_write_u8(gb, 0xff06, 0x7b);
    cupid_gb_write_u8(gb, 0xff05, 0xff);
    cupid_gb_write_u8(gb, 0xff07, 5);
    cupid_gb_write_u8(gb, 0xff01, 0xa5);
    cupid_gb_write_u8(gb, 0xff02, 0x81);
    cupid_gb_write_u8(gb, 0xff40, 0x91);
    for (unsigned i = 0; i < 160; ++i)
        cupid_gb_write_u8(gb, (uint16_t)(0xc000u + i), (uint8_t)(0xa5u ^ i));
}

static void test_peripheral_clocks(CupidGb *gb) {
    for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
        prepare(gb, profiles[p], 0xd3);
        SerialCapture capture = {0};
        arm_peripherals(gb, &capture);
        const uint64_t before = gb->elapsed_cycles;
        const uint64_t sample_acc = gb->apu.sample_acc;
        const uint32_t rate = cupid_gb_apu_get_sample_rate(gb);
        CupidGbStepTrace trace;
        assert(cupid_gb_step_observed(gb, &trace) && trace.event == CUPID_GB_STEP_ILLEGAL_OPCODE);
        /* Start DMA after the fetch; an active OAM transfer can itself change
         * the byte seen by a CPU fetching outside high RAM. */
        uint8_t oam[160];
        memcpy(oam, gb->object_attribute_memory, sizeof(oam));
        cupid_gb_write_u8(gb, 0xff46, 0xc0);
        uint64_t frames = 0;
        bool nonzero = false;
        while (gb->elapsed_cycles - before < 2u * 70224u) {
            assert(cupid_gb_step_observed(gb, &trace));
            assert(trace.event == CUPID_GB_STEP_IDLE && trace.address == 0x101 && !trace.opcode);
            assert(gb->cpu.halted && gb->cpu.pc == 0x101 && !gb->interrupt_enable);
            int16_t samples[128];
            const uint32_t count = cupid_gb_apu_drain(gb, samples, 64);
            frames += count;
            for (unsigned i = 0; i < count * 2u; ++i)
                nonzero |= samples[i] != 0;
        }
        const uint64_t elapsed = gb->elapsed_cycles - before;
        assert(elapsed == 2u * 70224u);
        assert(gb->vblank_events > 0 && gb->frame_ready);
        assert((gb->interrupt_flags & 0x0du) == 0x0du);
        assert(capture.count == 1 && capture.byte == 0xa5 && capture.cycle > before);
        assert(!(cupid_gb_read_u8(gb, 0xff02) & 0x80u));
        /* OAM DMA is gated during HALT even though timer, display and sound
         * clocks continue. Its pending start must survive until CPU wake. */
        assert(!gb->dma_active && gb->dma_start_delay && !gb->dma_index);
        assert(!memcmp(oam, gb->object_attribute_memory, sizeof(oam)));
        assert(frames == (sample_acc + elapsed * rate) / cupid_gb_base_clock_hz(gb));
        assert(frames > 1000 && nonzero);
        const uint64_t cpu_ticks = elapsed * (profiles[p].fast ? 2u : 1u);
        assert(cupid_gb_read_u8(gb, 0xff04) == (uint8_t)(cpu_ticks / 256u));
        cupid_gb_write_u8(gb, 0xff0f, 1);
        cupid_gb_write_u8(gb, 0xffff, 1);
        assert(cupid_gb_step_observed(gb, &trace));
        assert(trace.event == CUPID_GB_STEP_IDLE && !gb->cpu.halted && gb->cpu.pc == 0x101);
        cupid_gb_tick(gb, 200);
        assert(!gb->dma_active && !gb->dma_start_delay);
        for (unsigned i = 0; i < 160; ++i)
            assert(gb->object_attribute_memory[i] == (uint8_t)(0xa5u ^ i));
    }
}

static void test_state_continuation(CupidGb *plain, CupidGb *restored) {
    for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
        prepare(plain, profiles[p], 0xed);
        prepare(restored, profiles[p], 0xed);
        plain->cpu.ime = restored->cpu.ime = (p & 1u) != 0;
        SerialCapture expected_serial = {0}, restored_serial = {0};
        arm_peripherals(plain, &expected_serial);
        arm_peripherals(restored, &restored_serial);
        const unsigned cycles = machine_cycles(profiles[p]);
        step_pair(plain, restored, CUPID_GB_STEP_ILLEGAL_OPCODE, 0x100, 0xed, cycles);
        cupid_gb_write_u8(plain, 0xff46, 0xc0);
        cupid_gb_write_u8(restored, 0xff46, 0xc0);
        for (unsigned idle = 0; idle < 7; ++idle)
            step_pair(plain, restored, CUPID_GB_STEP_IDLE, 0x101, 0, cycles);
        assert(plain->dma_start_delay && !plain->dma_active && plain->serial_bits_remaining);
        size_t size;
        uint8_t *bytes = save(plain, &size);
        restored->cpu.halted = false;
        restored->interrupt_enable = 0xff;
        assert(cupid_gb_load_state(restored, bytes, size) == CUPID_GB_STATE_OK);
        free(bytes);
        assert(restored->cpu.halted && !restored->interrupt_enable);
        assert(!restored_serial.count && !expected_serial.count);
        assert_same_state(plain, restored);
        const uint64_t before = plain->elapsed_cycles;
        uint64_t audio_frames = 0;
        while (plain->elapsed_cycles - before < 70224u) {
            step_pair(plain, restored, CUPID_GB_STEP_IDLE, 0x101, 0, cycles);
            int16_t plain_samples[128], restored_samples[128];
            const uint32_t count = cupid_gb_apu_drain(plain, plain_samples, 64);
            assert(cupid_gb_apu_drain(restored, restored_samples, 64) == count);
            assert(!memcmp(plain_samples, restored_samples, count * 2u * sizeof(int16_t)));
            audio_frames += count;
        }
        assert(audio_frames > 0 && expected_serial.count == 1 && restored_serial.count == 1);
        assert(expected_serial.byte == restored_serial.byte &&
               expected_serial.cycle == restored_serial.cycle);
        assert_same_state(plain, restored);
        cupid_gb_write_u8(plain, 0xff0f, 1);
        cupid_gb_write_u8(restored, 0xff0f, 1);
        cupid_gb_write_u8(plain, 0xffff, 1);
        cupid_gb_write_u8(restored, 0xffff, 1);
        const bool ime = plain->cpu.ime;
        step_pair(plain, restored, ime ? CUPID_GB_STEP_INTERRUPT : CUPID_GB_STEP_IDLE, 0x101, 0,
                  cycles * (ime ? 6u : 1u));
        assert(!plain->cpu.halted && !restored->cpu.halted);
        assert_same_state(plain, restored);
    }
}

static void test_invalid_instances(void) {
    CupidGb *empty = calloc(1, sizeof(*empty));
    assert(empty);
    CupidGbStepTrace trace = {CUPID_GB_STEP_ILLEGAL_OPCODE, 0xffff, 0xff};
    assert(!cupid_gb_step(NULL) && !cupid_gb_step(empty));
    assert(!cupid_gb_step_observed(NULL, &trace));
    assert(trace.event == CUPID_GB_STEP_IDLE && !trace.address && !trace.opcode);
    empty->cpu.pc = 0x1234;
    assert(!cupid_gb_step_observed(empty, &trace));
    assert(trace.event == CUPID_GB_STEP_IDLE && trace.address == 0x1234 && !trace.opcode);
    free(empty);
}

int main(void) {
    CupidGb *plain = calloc(1, sizeof(*plain));
    CupidGb *observed = calloc(1, sizeof(*observed));
    assert(plain && observed);
    test_illegal_matrix(plain, observed);
    test_cb_legality(plain);
    test_fetches(plain);
    test_peripheral_clocks(plain);
    test_state_continuation(plain, observed);
    test_invalid_instances();
    cupid_gb_cleanup(observed);
    cupid_gb_cleanup(plain);
    free(observed);
    free(plain);
    puts("CPU lockup checks passed.");
    return 0;
}

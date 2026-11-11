#define _POSIX_C_SOURCE 200809L

#include "cupid/gb/gb.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define stream_descriptor _fileno
#define duplicate_descriptor _dup
#define replace_descriptor _dup2
#define close_descriptor _close
#else
#include <unistd.h>
#define stream_descriptor fileno
#define duplicate_descriptor dup
#define replace_descriptor dup2
#define close_descriptor close
#endif

typedef struct SerialCapture {
    uint8_t bytes[32];
    uint64_t cycles[32];
    unsigned count;
} SerialCapture;

typedef struct SerialPeer {
    CupidGb *gb;
    bool outgoing;
    unsigned launches;
    unsigned samples;
} SerialPeer;

static void launch_bit(const CupidGb *gb, bool outgoing, void *user_data) {
    (void)gb;
    SerialPeer *peer = user_data;
    peer->outgoing = outgoing;
    ++peer->launches;
}

static bool sample_peer(const CupidGb *gb, void *user_data) {
    (void)gb;
    SerialPeer *peer = user_data;
    ++peer->samples;
    const bool incoming = cupid_gb_serial_external_bit(peer->gb);
    cupid_gb_serial_external_clock(peer->gb, peer->outgoing);
    return incoming;
}

static void capture_byte(const CupidGb *gb, uint8_t transmitted, void *user_data) {
    SerialCapture *capture = user_data;
    assert(capture && capture->count < sizeof(capture->bytes));
    assert(gb->io_registers[1] == 0xff);
    assert(!(gb->io_registers[2] & 0x80));
    assert(gb->interrupt_flags & 8);
    assert(gb->serial_bits_remaining == 0);
    capture->bytes[capture->count] = transmitted;
    capture->cycles[capture->count++] = gb->elapsed_cycles;
}

static void prepare(CupidGb *gb, bool cgb, bool double_speed) {
    *gb = (CupidGb){0};
    gb->cgb_mode = cgb;
    gb->double_speed = double_speed;
    gb->io_registers[0x02] = 0x7e;
}

static void test_control_masks(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_write_u8(&gb, 0xff02, 0x81);
    assert(cupid_gb_read_u8(&gb, 0xff02) == 0xff);
    prepare(&gb, true, false);
    cupid_gb_write_u8(&gb, 0xff02, 0x81);
    assert(cupid_gb_read_u8(&gb, 0xff02) == 0xfd);
    cupid_gb_write_u8(&gb, 0xff02, 0x83);
    assert(cupid_gb_read_u8(&gb, 0xff02) == 0xff);
}

static void test_byte_duration(bool cgb, bool fast, bool double_speed) {
    CupidGb gb;
    SerialCapture capture = {0};
    const unsigned cycles_per_bit = cgb && fast ? 4 : 128;
    prepare(&gb, cgb, double_speed);
    cupid_gb_set_serial_output_callback(&gb, capture_byte, &capture);
    cupid_gb_write_u8(&gb, 0xff01, 0x41);
    cupid_gb_write_u8(&gb, 0xff02, fast ? 0x83 : 0x81);
    cupid_gb_tick(&gb, (uint16_t)(cycles_per_bit * 8 - 1));
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0xff);
    assert((cupid_gb_read_u8(&gb, 0xff02) & 0x80) != 0);
    assert((cupid_gb_read_u8(&gb, 0xff0f) & 8) == 0);
    assert(capture.count == 0);
    cupid_gb_tick(&gb, 1);
    assert((cupid_gb_read_u8(&gb, 0xff02) & 0x80) == 0);
    assert((cupid_gb_read_u8(&gb, 0xff0f) & 8) != 0);
    assert(gb.elapsed_cycles == cycles_per_bit * 8 * (double_speed ? 2 : 4));
    assert(capture.count == 1 && capture.bytes[0] == 0x41);
}

static void test_external_clock(void) {
    CupidGb gb;
    SerialCapture capture = {0};
    prepare(&gb, true, false);
    cupid_gb_set_serial_output_callback(&gb, capture_byte, &capture);
    cupid_gb_write_u8(&gb, 0xff01, 0x55);
    cupid_gb_write_u8(&gb, 0xff02, 0x82);
    cupid_gb_tick(&gb, 2048);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0x55);
    assert((cupid_gb_read_u8(&gb, 0xff02) & 0x80) != 0);
    assert((cupid_gb_read_u8(&gb, 0xff0f) & 8) == 0);
    assert(capture.count == 0);

    const uint8_t incoming = 0xc3;
    for (unsigned i = 0; i < 8; ++i) {
        assert(cupid_gb_serial_external_bit(&gb) == ((0x55u & (0x80u >> i)) != 0u));
        cupid_gb_serial_external_clock(&gb, (incoming & (0x80u >> i)) != 0u);
        assert(((cupid_gb_read_u8(&gb, 0xff02) & 0x80) == 0) == (i == 7));
        assert(((cupid_gb_read_u8(&gb, 0xff0f) & 8) != 0) == (i == 7));
    }
    assert(!cupid_gb_serial_external_bit(&gb));
    cupid_gb_serial_external_clock(&gb, false);
    assert(cupid_gb_read_u8(&gb, 0xff01) == incoming);
    assert(capture.count == 0);

    cupid_gb_write_u8(&gb, 0xff01, 0);
    cupid_gb_write_u8(&gb, 0xff02, 0x81);
    assert(cupid_gb_serial_external_bit(&gb));
    cupid_gb_serial_external_clock(&gb, true);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0);
}

static void test_link_pair(bool cgb, bool fast, bool double_speed) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    assert(a && b);
    prepare(a, cgb, double_speed);
    prepare(b, cgb, double_speed);
    SerialPeer cable_a = {.gb = b}, cable_b = {.gb = a};
    cupid_gb_set_serial_link(a, (CupidGbSerialLink){launch_bit, sample_peer, &cable_a});
    cupid_gb_set_serial_link(b, (CupidGbSerialLink){launch_bit, sample_peer, &cable_b});

    const uint16_t period = cgb && fast ? 4 : 128;
    for (unsigned direction = 0; direction < 2; ++direction) {
        CupidGb *master = direction ? b : a, *slave = direction ? a : b;
        SerialPeer *cable = direction ? &cable_b : &cable_a;
        const uint8_t master_byte = direction ? 0xa5 : 0x69, slave_byte = direction ? 0xc3 : 0x96;
        uint8_t master_sb = master_byte, slave_sb = slave_byte;
        cupid_gb_write_u8(master, 0xff0f, 0);
        cupid_gb_write_u8(slave, 0xff0f, 0);
        cupid_gb_write_u8(slave, 0xff01, slave_byte);
        cupid_gb_write_u8(slave, 0xff02, fast ? 0x82 : 0x80);
        cupid_gb_write_u8(master, 0xff01, master_byte);
        cupid_gb_write_u8(master, 0xff02, fast ? 0x83 : 0x81);

        for (unsigned i = 0; i < 8; ++i) {
            cupid_gb_tick(slave, period);
            cupid_gb_tick(master, (uint16_t)(period - 1));
            assert(cable->samples == i && cable->launches == i + 1);
            assert(cupid_gb_read_u8(master, 0xff01) == master_sb);
            assert(cupid_gb_read_u8(slave, 0xff01) == slave_sb);
            cupid_gb_tick(master, 1);
            master_sb = (uint8_t)(2u * master_sb + ((slave_byte & (0x80u >> i)) != 0u));
            slave_sb = (uint8_t)(2u * slave_sb + ((master_byte & (0x80u >> i)) != 0u));
            assert(cupid_gb_read_u8(master, 0xff01) == master_sb);
            assert(cupid_gb_read_u8(slave, 0xff01) == slave_sb);
            assert(((cupid_gb_read_u8(master, 0xff0f) & 8) != 0) == (i == 7));
            assert(((cupid_gb_read_u8(slave, 0xff0f) & 8) != 0) == (i == 7));
            assert(((cupid_gb_read_u8(master, 0xff02) & 0x80) == 0) == (i == 7));
            assert(((cupid_gb_read_u8(slave, 0xff02) & 0x80) == 0) == (i == 7));
        }
        assert(cable->launches == 8 && cable->samples == 8);
        assert(master_sb == slave_byte && slave_sb == master_byte);
    }
    free(a);
    free(b);
}

static void test_link_control_write(void) {
    CupidGb *master = calloc(1, sizeof(*master)), *slave = calloc(1, sizeof(*slave));
    assert(master && slave);
    prepare(master, true, false);
    prepare(slave, true, false);
    SerialPeer cable = {.gb = slave};
    cupid_gb_set_serial_link(master, (CupidGbSerialLink){launch_bit, sample_peer, &cable});
    cupid_gb_write_u8(slave, 0xff01, 0xa5);
    cupid_gb_write_u8(slave, 0xff02, 0x80);
    cupid_gb_write_u8(master, 0xff01, 0x80);
    cupid_gb_write_u8(master, 0xff02, 0x83);
    assert(cable.launches == 1 && cable.outgoing);

    /* Replacing SB changes its receive shift register. The bit already
     * launched before the falling edge remains on the cable. */
    cupid_gb_write_u8(master, 0xff01, 0);
    cupid_gb_tick(master, 2);
    cupid_gb_write_u8(master, 0xff02, 0x80);
    assert(cable.samples == 1 && cable.launches == 2);
    assert(cupid_gb_read_u8(master, 0xff01) == 1);
    assert(cupid_gb_read_u8(slave, 0xff01) == 0x4b);
    for (unsigned i = 0; i < 7; ++i) {
        assert(cupid_gb_read_u8(master, 0xff02) & 0x80);
        cupid_gb_serial_external_clock(master, false);
    }
    assert(cupid_gb_read_u8(master, 0xff01) == 0x80);
    assert((cupid_gb_read_u8(master, 0xff02) & 0x80) == 0);
    assert(cupid_gb_read_u8(master, 0xff0f) & 8);
    free(master);
    free(slave);
}

static void load_link_rom(CupidGb *gb, CupidGbModel model, uint8_t data, uint8_t control) {
    const uint8_t program[] = {0x3e, data,    0xe0, 0x01, /* Load the outgoing byte. */
                               0x3e, control, 0xe0, 0x02, /* Start serial. */
                               0xf0, 0x02,    0xe6, 0x80, 0x20,
                               0xfa,                            /* Wait for SC bit 7 to clear. */
                               0xf0, 0x01,    0xea, 0x00, 0xc0, /* Store received SB. */
                               0x3e, 0x01,    0xea, 0x01, 0xc0, /* Signal completion. */
                               0x18, 0xfe};
    uint8_t rom[32768] = {0};
    rom[0x100] = 0xc3;
    rom[0x101] = 0x50;
    rom[0x102] = 0x01;
    rom[0x143] = 0x80;
    memcpy(rom + 0x150, program, sizeof(program));
    for (unsigned i = 0x134; i <= 0x14c; ++i) {
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
    }
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    cupid_gb_write_u8(gb, 0xff0f, 0);
}

static void test_link_roms(void) {
    CupidGb *master = calloc(1, sizeof(*master)), *slave = calloc(1, sizeof(*slave));
    assert(master && slave);
    SerialPeer cable = {.gb = slave};
    cupid_gb_set_serial_link(master, (CupidGbSerialLink){launch_bit, sample_peer, &cable});
    load_link_rom(master, CUPID_GB_MODEL_CGB, 0x23, 0x83);
    load_link_rom(slave, CUPID_GB_MODEL_DMG_ABC, 0xd9, 0x80);
    for (unsigned i = 0; i < 8; ++i) {
        cupid_gb_step(slave);
    }
    assert(cupid_gb_read_u8(slave, 0xff02) & 0x80);
    for (unsigned i = 0; i < 128; ++i) {
        cupid_gb_step(master);
        cupid_gb_step(slave);
    }
    assert(cupid_gb_read_u8(master, 0xc000) == 0xd9 && cupid_gb_read_u8(master, 0xc001) == 1);
    assert(cupid_gb_read_u8(slave, 0xc000) == 0x23 && cupid_gb_read_u8(slave, 0xc001) == 1);
    assert(cupid_gb_read_u8(master, 0xff0f) & 8);
    assert(cupid_gb_read_u8(slave, 0xff0f) & 8);
    assert(cable.launches == 8 && cable.samples == 8);
    cupid_gb_cleanup(master);
    cupid_gb_cleanup(slave);
    free(master);
    free(slave);
}

static void test_divider_phase_and_reset(void) {
    CupidGb gb;
    prepare(&gb, true, false);
    /* Fast clock has already reached its high phase before transfer starts.
     * SC writes finish that phase; DIV reset supplies the next rising edge. */
    cupid_gb_write_u8(&gb, 0xff02, 2);
    cupid_gb_tick(&gb, 3);
    cupid_gb_write_u8(&gb, 0xff01, 0);
    cupid_gb_write_u8(&gb, 0xff02, 0x83);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0);
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0);
    cupid_gb_tick(&gb, 1);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0);
    cupid_gb_tick(&gb, 1);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 1);
}

static void test_control_write_completes_high_phase(void) {
    CupidGb gb;
    prepare(&gb, true, false);
    cupid_gb_write_u8(&gb, 0xff01, 0);
    cupid_gb_write_u8(&gb, 0xff02, 0x83);
    cupid_gb_tick(&gb, 2);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0);
    cupid_gb_write_u8(&gb, 0xff02, 0x83);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 1);
    cupid_gb_tick(&gb, 28);
    assert((cupid_gb_read_u8(&gb, 0xff02) & 0x80) == 0);
    assert((cupid_gb_read_u8(&gb, 0xff0f) & 8) != 0);
}

static void test_div_reset_can_shift(void) {
    CupidGb gb;
    prepare(&gb, true, false);
    cupid_gb_write_u8(&gb, 0xff01, 0);
    cupid_gb_write_u8(&gb, 0xff02, 0x83);
    cupid_gb_tick(&gb, 3);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0);
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 1);
}

static void test_control_access_restarts_byte_count(void) {
    static const struct {
        uint8_t old_control, new_control, data, remaining;
    } cases[] = {
        {0x80, 0x83, 0x80, 8}, {0x01, 0x83, 0x80, 8}, {0x81, 0x83, 0x01, 7}, {0x83, 0x00, 0x01, 0}};

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CupidGb gb;
        prepare(&gb, true, false);
        gb.io_registers[1] = 0x80;
        gb.io_registers[2] = cases[i].old_control;
        gb.serial_master_clock = true;
        gb.serial_bits_remaining = 1;
        cupid_gb_write_u8(&gb, 0xff02, cases[i].new_control);
        assert(gb.io_registers[1] == cases[i].data);
        assert(gb.serial_bits_remaining == cases[i].remaining);
        assert(!gb.serial_master_clock && !(gb.interrupt_flags & 8));
    }
}

static void test_base_clock_counts_with_lcd_off(void) {
    CupidGb gb;
    prepare(&gb, true, true);
    cupid_gb_tick(&gb, 1);
    assert(gb.elapsed_cycles == 2);
    cupid_gb_tick(&gb, 1);
    assert(gb.elapsed_cycles == 4);
    gb.double_speed = false;
    cupid_gb_tick(&gb, 1);
    assert(gb.elapsed_cycles == 8);
}

static void send_byte(CupidGb *gb, uint8_t byte) {
    gb->interrupt_flags &= (uint8_t)~8u;
    cupid_gb_write_u8(gb, 0xff01, byte);
    cupid_gb_write_u8(gb, 0xff02, 0x83);
    while (gb->io_registers[2] & 0x80) {
        cupid_gb_tick(gb, 1);
    }
}

static void test_observer_sequence_and_timing(void) {
    const uint8_t sequence[] = {0x00, 0x80, 0x41, 0xff};
    for (unsigned double_speed = 0; double_speed < 2; ++double_speed) {
        CupidGb gb;
        SerialCapture capture = {0};
        prepare(&gb, true, double_speed != 0);
        cupid_gb_set_serial_output_callback(&gb, capture_byte, &capture);
        for (unsigned i = 0; i < sizeof(sequence); ++i) {
            send_byte(&gb, sequence[i]);
            assert(capture.count == i + 1);
            assert(capture.bytes[i] == sequence[i]);
            /* The observer runs on the divider edge, before this machine cycle's
             * LCD/APU dots advance the elapsed base-clock count. */
            assert(capture.cycles[i] + (double_speed ? 2u : 4u) == gb.elapsed_cycles);
        }
        cupid_gb_set_serial_output_callback(&gb, NULL, &capture);
        assert(!gb.serial_output_callback && !gb.serial_output_user_data);
        send_byte(&gb, 0x55);
        assert(capture.count == sizeof(sequence));
    }
    cupid_gb_set_serial_output_callback(NULL, capture_byte, NULL);
}

static void test_default_has_no_stream_output(void) {
    fflush(stdout);
    const int original = duplicate_descriptor(stream_descriptor(stdout));
    FILE *captured = tmpfile();
    assert(original >= 0 && captured);
    assert(replace_descriptor(stream_descriptor(captured), stream_descriptor(stdout)) >= 0);
    CupidGb gb;
    prepare(&gb, true, false);
    for (unsigned i = 0; i < 16; ++i) {
        send_byte(&gb, (uint8_t)i);
    }
    fflush(stdout);
    assert(fseek(captured, 0, SEEK_END) == 0);
    const long written = ftell(captured);
    assert(replace_descriptor(original, stream_descriptor(stdout)) >= 0);
    close_descriptor(original);
    fclose(captured);
    assert(written == 0);
}

static void test_observer_host_lifetime_and_snapshot_identity(void) {
    CupidGb *observed = calloc(1, sizeof(*observed)), *silent = calloc(1, sizeof(*silent));
    assert(observed && silent);
    uint8_t rom[32768] = {0};
    rom[0x143] = 0x80;
    for (unsigned i = 0x134; i <= 0x14c; ++i) {
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[i] - 1u);
    }
    SerialCapture capture = {0}, replacement = {0};
    SerialPeer cable = {0};
    cupid_gb_set_model(observed, CUPID_GB_MODEL_CGB);
    cupid_gb_set_model(silent, CUPID_GB_MODEL_CGB);
    cupid_gb_set_serial_output_callback(observed, capture_byte, &capture);
    cupid_gb_set_serial_link(observed,
                             (CupidGbSerialLink){.launch = launch_bit, .user_data = &cable});
    assert(cupid_gb_load_rom(observed, rom, sizeof(rom)));
    assert(cupid_gb_load_rom(silent, rom, sizeof(rom)));
    assert(observed->serial_output_callback == capture_byte);
    assert(observed->serial_output_user_data == &capture);
    assert(observed->serial_link.user_data == &cable);
    const size_t size = cupid_gb_state_size(observed);
    uint8_t *a = malloc(size), *b = malloc(size);
    assert(a && b);
    for (unsigned i = 0; i < 3; ++i) {
        send_byte(observed, (uint8_t)(0x40u + i));
        send_byte(silent, (uint8_t)(0x40u + i));
        assert(cupid_gb_save_state(observed, a, size, NULL) == CUPID_GB_STATE_OK);
        assert(cupid_gb_save_state(silent, b, size, NULL) == CUPID_GB_STATE_OK);
        assert(memcmp(a, b, size) == 0);
    }
    cupid_gb_set_serial_output_callback(observed, capture_byte, &replacement);
    assert(cupid_gb_load_state(observed, a, size) == CUPID_GB_STATE_OK);
    assert(observed->serial_output_user_data == &replacement);
    assert(observed->serial_link.user_data == &cable && cable.launches == 24);
    send_byte(observed, 0xa5);
    assert(cable.launches == 32);
    assert(replacement.count == 1 && replacement.bytes[0] == 0xa5);
    assert(capture.count == 3);
    a[size - 1] ^= 1;
    assert(cupid_gb_load_state(observed, a, size) != CUPID_GB_STATE_OK);
    assert(observed->serial_output_callback == capture_byte);
    assert(observed->serial_output_user_data == &replacement);
    cupid_gb_init(observed);
    assert(observed->serial_output_callback == capture_byte);
    assert(observed->serial_output_user_data == &replacement);
    observed->boot_rom_size = 0x900;
    cupid_gb_enter_boot_rom(observed);
    assert(observed->serial_output_callback == capture_byte);
    assert(observed->serial_output_user_data == &replacement);
    assert(observed->serial_link.launch == launch_bit && observed->serial_link.user_data == &cable);
    cupid_gb_cleanup(observed);
    assert(!observed->serial_output_callback && !observed->serial_output_user_data);
    assert(!observed->serial_link.launch && !observed->serial_link.sample &&
           !observed->serial_link.user_data);
    cupid_gb_cleanup(silent);
    free(a);
    free(b);
    free(observed);
    free(silent);
}

int main(void) {
    test_control_masks();
    test_byte_duration(false, false, false);
    test_byte_duration(false, true, false);
    test_byte_duration(true, false, false);
    test_byte_duration(true, true, false);
    test_byte_duration(true, true, true);
    test_external_clock();
    test_link_pair(false, false, false);
    test_link_pair(true, true, false);
    test_link_pair(true, true, true);
    test_link_control_write();
    test_link_roms();
    test_divider_phase_and_reset();
    test_control_write_completes_high_phase();
    test_div_reset_can_shift();
    test_control_access_restarts_byte_count();
    test_base_clock_counts_with_lcd_off();
    test_observer_sequence_and_timing();
    test_default_has_no_stream_output();
    test_observer_host_lifetime_and_snapshot_identity();
    puts("Serial timing checks passed.");
    return 0;
}

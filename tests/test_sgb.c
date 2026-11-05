#include "cupid/gb/gb.h"
#include "cupid/gb/sgb.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CupidGb *screen(void) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    gb->sgb.enabled = true;
    gb->sgb.player_count = 1;
    return gb;
}

static void write_port(CupidGb *gb, uint8_t value) {
    cupid_gb_sgb_write_joyp(gb, value);
    gb->io_registers[0] = value;
}

static void send_command(CupidGb *gb, const uint8_t *command) {
    const unsigned packets = command[0] & 7u;
    assert(packets > 0);
    for (unsigned packet = 0; packet < packets; ++packet) {
        write_port(gb, 0x30);
        write_port(gb, 0);
        for (unsigned bit = 0; bit < 128; ++bit) {
            write_port(gb, 0x30);
            write_port(gb, command[packet * 16 + bit / 8] & (1u << (bit % 8)) ? 0x10 : 0x20);
        }
        write_port(gb, 0x30);
        write_port(gb, 0x20);
        write_port(gb, 0x30);
    }
    assert(gb->sgb.command_write_index == 0);
}

static uint8_t cell(const CupidGb *gb, unsigned x, unsigned y) {
    return gb->sgb.attribute_map[y * CUPID_SGB_ATTR_WIDTH + x];
}

static void uniform(const CupidGb *gb, uint8_t value) {
    for (unsigned i = 0; i < sizeof(gb->sgb.attribute_map); ++i) {
        assert(gb->sgb.attribute_map[i] == value);
    }
}

static void test_block_controls(void) {
    const uint8_t edge_palette[] = {0, 1, 2, 2, 3, 0, 2, 2};
    CupidGb *gb = screen();
    for (unsigned control = 0; control < 8; ++control) {
        uint8_t command[16] = {0x21, 1, (uint8_t)(0xf8u | control), 0xf9, 3, 3, 7, 7};
        memset(gb->sgb.attribute_map, 0, sizeof(gb->sgb.attribute_map));
        send_command(gb, command);
        assert(cell(gb, 5, 5) == ((control & 1) ? 1 : 0));
        assert(cell(gb, 1, 1) == ((control & 4) ? 3 : 0));
        assert(cell(gb, 3, 5) == edge_palette[control]);
        assert(cell(gb, 3, 3) == edge_palette[control]);
        assert(cell(gb, 7, 5) == edge_palette[control]);
        assert(cell(gb, 7, 7) == edge_palette[control]);
        assert(cell(gb, 5, 3) == edge_palette[control]);
        assert(cell(gb, 5, 7) == edge_palette[control]);
    }
    free(gb);
}

static void test_block_geometry(void) {
    CupidGb *gb = screen();
    uint8_t command[16] = {0x21, 1, 7, 0x39, 7, 3, 3, 7};
    /* Reversed bounds do not get normalized into a visible rectangle. */
    send_command(gb, command);
    uniform(gb, 3);
    command[4] = 3;
    command[6] = 7;
    command[5] = 7;
    command[7] = 3;
    send_command(gb, command);
    uniform(gb, 3);

    command[4] = 0xe2;
    command[5] = 0xe3;
    command[6] = 0xe6;
    command[7] = 0xe7;
    send_command(gb, command);
    assert(cell(gb, 2, 3) == 2 && cell(gb, 6, 7) == 2);
    assert(cell(gb, 4, 5) == 1 && cell(gb, 1, 3) == 3);

    command[4] = 5;
    command[5] = 5;
    command[6] = 5;
    command[7] = 5;
    send_command(gb, command);
    assert(cell(gb, 5, 5) == 2);
    assert(cell(gb, 5, 4) == 3 && cell(gb, 4, 5) == 3);

    command[4] = 31;
    command[5] = 31;
    command[6] = 31;
    command[7] = 31;
    send_command(gb, command);
    uniform(gb, 3);

    command[4] = 18;
    command[5] = 16;
    send_command(gb, command);
    assert(cell(gb, 18, 16) == 2 && cell(gb, 19, 16) == 2);
    assert(cell(gb, 18, 17) == 2 && cell(gb, 19, 17) == 1);
    free(gb);
}

static void test_block_packet_limits(void) {
    CupidGb *gb = screen();
    uint8_t command[CUPID_SGB_MAX_COMMAND_BYTES] = {0x27, 18};
    for (unsigned record = 0; record < 18; ++record) {
        uint8_t *bytes = command + 2 + record * 6;
        bytes[0] = 1;
        bytes[1] = (uint8_t)(record % 4);
        bytes[2] = bytes[3] = 0;
        bytes[4] = 19;
        bytes[5] = 17;
    }
    send_command(gb, command);
    uniform(gb, 1);
    command[1] = 19;
    memset(gb->sgb.attribute_map, 2, sizeof(gb->sgb.attribute_map));
    send_command(gb, command);
    uniform(gb, 2);
    command[1] = 0;
    send_command(gb, command);
    uniform(gb, 2);
    free(gb);
}

static void test_character_order(void) {
    CupidGb *gb = screen();
    uint8_t command[16] = {0x39, 18, 2, 5, 0, 0, 0x6c, 0x40};
    send_command(gb, command);
    assert(cell(gb, 18, 2) == 1 && cell(gb, 19, 2) == 2);
    assert(cell(gb, 0, 3) == 3 && cell(gb, 1, 3) == 0 && cell(gb, 2, 3) == 1);

    memset(gb->sgb.attribute_map, 3, sizeof(gb->sgb.attribute_map));
    command[1] = 2;
    command[2] = 16;
    command[5] = 0x80;
    send_command(gb, command);
    assert(cell(gb, 2, 16) == 1 && cell(gb, 2, 17) == 2);
    assert(cell(gb, 3, 0) == 3 && cell(gb, 3, 1) == 0 && cell(gb, 3, 2) == 1);
    assert(cell(gb, 3, 3) == 3);

    command[1] = 20;
    send_command(gb, command);
    assert(cell(gb, 2, 16) == 1 && cell(gb, 3, 1) == 0);
    command[1] = 0;
    command[2] = 18;
    send_command(gb, command);
    assert(cell(gb, 2, 16) == 1 && cell(gb, 3, 1) == 0);
    free(gb);
}

static void test_character_end_and_count(void) {
    CupidGb *gb = screen();
    uint8_t command[CUPID_SGB_MAX_COMMAND_BYTES];
    memset(command, 0xe4, sizeof(command));
    command[0] = 0x3f;
    command[1] = command[2] = command[5] = 0;
    command[3] = command[4] = 0xff;
    send_command(gb, command);
    assert(cell(gb, 0, 0) == 3 && cell(gb, 1, 0) == 2);
    assert(cell(gb, 2, 0) == 1 && cell(gb, 3, 0) == 0);
    assert(cell(gb, 19, 17) == 0);

    memset(gb->sgb.attribute_map, 2, sizeof(gb->sgb.attribute_map));
    command[1] = 19;
    command[2] = 17;
    command[5] = 1;
    send_command(gb, command);
    assert(cell(gb, 19, 17) == 3 && cell(gb, 18, 17) == 2);
    command[3] = command[4] = 0;
    command[6] = 0;
    send_command(gb, command);
    assert(cell(gb, 19, 17) == 3);
    free(gb);
}

static void test_packet_edges_and_reset(void) {
    CupidGb *gb = screen();
    /* Pulses without the reset handshake cannot start a command. */
    write_port(gb, 0x20);
    write_port(gb, 0x30);
    assert(gb->sgb.command_write_index == 0);
    write_port(gb, 0);
    write_port(gb, 0x30);
    write_port(gb, 0x10);
    assert(gb->sgb.command_write_index == 0 && gb->sgb.command[0] == 0);
    write_port(gb, 0x30);
    assert(gb->sgb.command_write_index == 1 && gb->sgb.command[0] == 1);
    write_port(gb, 0x30);
    assert(gb->sgb.command_write_index == 1);

    /* Repeated data writes do not clock bits; the last pulse wins. */
    write_port(gb, 0x10);
    write_port(gb, 0x20);
    assert(gb->sgb.command_write_index == 1);
    write_port(gb, 0x30);
    assert(gb->sgb.command_write_index == 2 && gb->sgb.command[0] == 1);
    write_port(gb, 0);
    assert(gb->sgb.command_write_index == 0 && gb->sgb.command[0] == 0);
    assert(!gb->sgb.ready_for_write && !gb->sgb.ready_for_stop);

    /* A complete command takes effect when its stop pulse returns high. */
    const uint8_t command[16] = {0xb9, 2};
    write_port(gb, 0x30);
    for (unsigned bit = 0; bit < 128; ++bit) {
        write_port(gb, command[bit / 8] & (1u << (bit % 8)) ? 0x10 : 0x20);
        write_port(gb, 0x30);
    }
    assert(gb->sgb.command_write_index == 128 && gb->sgb.ready_for_stop);
    assert(gb->sgb.mask_mode == 0);
    write_port(gb, 0x20);
    assert(gb->sgb.mask_mode == 0);
    write_port(gb, 0x30);
    assert(gb->sgb.mask_mode == 2 && gb->sgb.command_write_index == 0);
    assert(!gb->sgb.ready_for_write && !gb->sgb.ready_for_stop);
    free(gb);
}

int main(void) {
    test_block_controls();
    test_block_geometry();
    test_block_packet_limits();
    test_character_order();
    test_character_end_and_count();
    test_packet_edges_and_reset();
    puts("SGB command tests passed");
    return 0;
}

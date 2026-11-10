#include "cupid/gb/gb.h"
#include "cupid/gb/input.h"
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

static void test_irregular_multiplayer_request(void) {
    const uint8_t command[16] = {0x89, 2};
    const uint8_t expected[] = {2, 2, 0, 0};
    CupidGb *gb = screen();
    cupid_gb_set_buttons(gb, 0xef);
    assert(cupid_gb_set_buttons_for_player(gb, 2, 0xdf));
    for (unsigned start = 0; start < sizeof(expected); ++start) {
        gb->sgb.player_count = 4;
        gb->sgb.current_player = (uint8_t)start;
        send_command(gb, command);
        assert(gb->sgb.player_count == 3 && gb->sgb.current_player == expected[start]);
        assert(cupid_gb_input_read(gb) == 0xff - expected[start]);
        cupid_gb_input_select(gb, 0x10);
        assert(cupid_gb_input_read(gb) == (expected[start] ? 0xdd : 0xde));
        cupid_gb_input_select(gb, 0x30);
        assert(cupid_gb_input_read(gb) == 0xff - expected[start]);
    }
    free(gb);
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

static uint32_t pixel_at(const uint32_t *pixels, unsigned x, unsigned y) {
    return pixels[y * CUPID_SGB_SCREEN_WIDTH + x];
}

static void test_border_compositor(void) {
    CupidGb *gb = screen();
    const size_t count = CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT;
    uint32_t *pixels = calloc(count, sizeof(*pixels));
    assert(pixels);

    const unsigned view_x = CUPID_SGB_VIEWPORT_X;
    const unsigned view_y = CUPID_SGB_VIEWPORT_Y;
    const unsigned view_cell = (view_y / 8u) * CUPID_SGB_TILE_WIDTH + view_x / 8u;
    const unsigned index = 0xa7u;
    uint8_t *tile = gb->sgb.border_tiles + index * 32u;
    tile[0] = 0x80;
    tile[1] = 0x40;
    tile[16] = 0x20;
    tile[17] = 0x10;
    gb->sgb.screen_palettes[0] = 0x0003;
    gb->sgb.screen_palettes[9] = 0x03e0;
    gb->sgb.screen_palettes[10] = 0x7c00;
    gb->sgb.attribute_map[0] = gb->sgb.attribute_map[1] = 2;
    memset(gb->frame_buffer, 1, sizeof(gb->frame_buffer));
    memset(gb->sgb.freeze_buffer, 2, sizeof(gb->sgb.freeze_buffer));

    gb->sgb.border_map[0] = (uint16_t)(0x1c00u | index);
    gb->sgb.border_map[1] = (uint16_t)(0x5400u | index);
    gb->sgb.border_map[2] = (uint16_t)(0x9800u | index);
    gb->sgb.border_map[3] = (uint16_t)(0xd000u | index);
    gb->sgb.border_map[4] = (uint16_t)(0x0100u | index);
    gb->sgb.border_map[view_cell] = (uint16_t)(0x1c00u | index);
    gb->sgb.border_map[view_cell + 1] = (uint16_t)(0x0200u | index);
    gb->sgb.border_palettes[0] = 0x7fff;
    gb->sgb.border_palettes[49] = 0x001f;
    gb->sgb.border_palettes[50] = 0x03e0;
    gb->sgb.border_palettes[52] = 0x7c00;
    gb->sgb.border_palettes[56] = 0x7fff;
    gb->sgb.border_palettes[1] = 0x7fe0;
    gb->sgb.border_palettes[17] = 0x03ff;
    gb->sgb.border_palettes[33] = 0x7c1f;
    gb->sgb.border_palettes[97] = 0x4210;

    cupid_gb_sgb_render_argb(gb, pixels, count - 1u);
    assert(pixel_at(pixels, 0, 0) == 0);
    cupid_gb_sgb_render_argb(gb, pixels, count);
    assert(pixel_at(pixels, 0, 0) == 0xffff0000u);
    assert(pixel_at(pixels, 1, 0) == 0xff00ff00u);
    assert(pixel_at(pixels, 2, 0) == 0xff0000ffu);
    assert(pixel_at(pixels, 3, 0) == 0xffffffffu);
    assert(pixel_at(pixels, 4, 0) == 0xff180000u);
    assert(pixel_at(pixels, 15, 0) == 0xffffff00u);
    assert(pixel_at(pixels, 16, 7) == 0xffff00ffu);
    assert(pixel_at(pixels, 31, 7) == 0xff00ffffu);
    assert(pixel_at(pixels, 32, 0) == 0xff000000u);
    assert(pixel_at(pixels, view_x, view_y) == 0xffff0000u);
    assert(pixel_at(pixels, view_x + 4, view_y) == 0xff00ff00u);
    assert(pixel_at(pixels, view_x + 8, view_y) == 0xff00ff00u);

    const uint32_t transparent[] = {0xff00ff00u, 0xff0000ffu, 0xff000000u, 0xff180000u};
    for (unsigned mask = CUPID_SGB_MASK_DISABLED; mask <= CUPID_SGB_MASK_COLOR0; ++mask) {
        gb->sgb.mask_mode = (uint8_t)mask;
        cupid_gb_sgb_render_argb(gb, pixels, count);
        assert(pixel_at(pixels, view_x, view_y) == 0xffff0000u);
        assert(pixel_at(pixels, view_x + 4, view_y) == transparent[mask]);
        assert(pixel_at(pixels, view_x + 8, view_y) == transparent[mask]);
        assert(pixel_at(pixels, CUPID_SGB_SCREEN_WIDTH - 1, CUPID_SGB_SCREEN_HEIGHT - 1) ==
               0xff180000u);
    }

    free(pixels);
    free(gb);
}

int main(void) {
    test_irregular_multiplayer_request();
    test_block_controls();
    test_block_geometry();
    test_block_packet_limits();
    test_character_order();
    test_character_end_and_count();
    test_packet_edges_and_reset();
    test_border_compositor();
    puts("SGB command and border tests passed");
    return 0;
}

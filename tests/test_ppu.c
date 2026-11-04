#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void set_tile(CupidGb *gb, unsigned tile, uint8_t color) {
    for (unsigned row = 0; row < 8; ++row) {
        gb->video_ram[tile * 16 + row * 2] = color & 1 ? 0xff : 0;
        gb->video_ram[tile * 16 + row * 2 + 1] = color & 2 ? 0xff : 0;
    }
}

static void prepare(CupidGb *gb, bool cgb, uint8_t bg) {
    memset(gb, 0, sizeof(*gb));
    gb->cgb_mode = cgb;
    gb->io_registers[CUPID_GB_IO_LCDC] = 0x93;
    gb->io_registers[CUPID_GB_IO_BGP] = 0xe4;
    gb->io_registers[CUPID_GB_IO_OBP0] = 0xe4;
    cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_OAM);
    set_tile(gb, 0, bg);
    set_tile(gb, 1, 1);
    set_tile(gb, 2, 2);
    gb->cgb_bg_palette_ram[2] = 0x1f;
    gb->cgb_obj_palette_ram[2] = 0xe0;
    gb->cgb_obj_palette_ram[3] = 3;
    gb->cgb_obj_palette_ram[5] = 0x7c;
}

static void object(CupidGb *gb, unsigned index, uint8_t x, uint8_t tile, uint8_t attrs) {
    gb->object_attribute_memory[index * 4] = 16;
    gb->object_attribute_memory[index * 4 + 1] = x;
    gb->object_attribute_memory[index * 4 + 2] = tile;
    gb->object_attribute_memory[index * 4 + 3] = attrs;
}

static void render(CupidGb *gb) {
    cupid_gb_tick_ppu(gb, 114);
}

static void test_dmg_object_order(void) {
    CupidGb gb;
    prepare(&gb, false, 0);
    object(&gb, 0, 24, 1, 0);
    object(&gb, 1, 24, 2, 0);
    render(&gb);
    assert(gb.frame_buffer[16] == 1); /* Earlier OAM wins equal X. */

    prepare(&gb, false, 0);
    object(&gb, 0, 24, 1, 0);
    object(&gb, 1, 20, 2, 0);
    render(&gb);
    assert(gb.frame_buffer[16] == 2); /* Smaller X beats earlier OAM. */

    prepare(&gb, false, 0);
    object(&gb, 0, 24, 3, 0);
    object(&gb, 1, 24, 2, 0);
    render(&gb);
    assert(gb.frame_buffer[16] == 2); /* Transparent pixels do not claim priority. */
}

static void test_dmg_priority_uses_raw_bg_color(void) {
    CupidGb gb;
    prepare(&gb, false, 0);
    gb.io_registers[CUPID_GB_IO_BGP] = 0xff;
    object(&gb, 0, 24, 1, 0x80);
    render(&gb);
    assert(gb.frame_buffer[16] == 1); /* BG color zero stays behind OBJ at any shade. */

    prepare(&gb, false, 1);
    gb.io_registers[CUPID_GB_IO_BGP] = 0;
    object(&gb, 0, 24, 1, 0x80);
    render(&gb);
    assert(gb.frame_buffer[16] == 0); /* Nonzero BG colors hide OBJ even when mapped white. */
}

static void test_hidden_object_still_wins_obj_priority(bool cgb) {
    CupidGb gb;
    prepare(&gb, cgb, 1);
    object(&gb, 0, 24, 1, 0x80);
    object(&gb, 1, 24, 2, 0);
    render(&gb);
    if (cgb) {
        assert(gb.frame_buffer_color[16] == 0x1f);
    } else {
        assert(gb.frame_buffer[16] == 1);
    }
}

static void test_offscreen_objects_use_scanline_limit(bool cgb) {
    CupidGb gb;
    prepare(&gb, cgb, 0);
    for (unsigned i = 0; i < 10; ++i) {
        object(&gb, i, 0, 1, 0);
    }
    object(&gb, 10, 24, 2, 0);
    render(&gb);
    assert(gb.frame_buffer[16] == 0);
    assert(gb.frame_buffer_color[16] == 0);
}

static void test_cgb_oam_priority(void) {
    CupidGb gb;
    prepare(&gb, true, 0);
    object(&gb, 0, 24, 1, 0);
    object(&gb, 1, 20, 2, 0);
    render(&gb);
    assert(gb.frame_buffer_color[16] == 0x3e0);
}

int main(void) {
    test_dmg_object_order();
    test_dmg_priority_uses_raw_bg_color();
    test_hidden_object_still_wins_obj_priority(false);
    test_hidden_object_still_wins_obj_priority(true);
    test_offscreen_objects_use_scanline_limit(false);
    test_offscreen_objects_use_scanline_limit(true);
    test_cgb_oam_priority();
    puts("PPU priority checks passed.");
    return 0;
}

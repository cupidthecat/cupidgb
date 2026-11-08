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
    gb->model = cgb ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
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

static void test_cgb_priority_latch(void) {
    CupidGb gb;
    prepare(&gb, true, 0);
    object(&gb, 0, 24, 1, 0);
    object(&gb, 1, 20, 2, 0);
    cupid_gb_write_u8(&gb, 0xff6c, 1);
    assert(cupid_gb_read_u8(&gb, 0xff6c) == 0xff);
    render(&gb);
    assert(gb.frame_buffer_color[16] == 0x3e0); /* Readback cannot change normal color priority. */

    prepare(&gb, true, 0);
    object(&gb, 0, 24, 1, 0);
    object(&gb, 1, 20, 2, 0);
    gb.boot_rom_enabled = true;
    cupid_gb_write_u8(&gb, 0xff6c, 1);
    gb.boot_rom_enabled = false;
    cupid_gb_write_u8(&gb, 0xff6c, 0);
    assert(cupid_gb_read_u8(&gb, 0xff6c) == 0xfe);
    render(&gb);
    assert(gb.frame_buffer_color[16] == 0x7c00); /* Boot-selected X priority remains latched. */

    prepare(&gb, true, 0);
    object(&gb, 0, 24, 1, 0);
    object(&gb, 1, 20, 2, 0);
    gb.boot_rom_enabled = true;
    cupid_gb_write_u8(&gb, 0xff4c, 8);
    gb.boot_rom_enabled = false;
    cupid_gb_write_u8(&gb, 0xff6c, 1);
    render(&gb);
    assert(gb.frame_buffer_color[16] == 0x7c00); /* KEY0 bit 3 keeps the selector writable. */

    prepare(&gb, true, 0);
    cupid_gb_write_u8(&gb, 0xff4c, 8);
    cupid_gb_write_u8(&gb, 0xff6c, 1);
    assert(!gb.cgb_object_priority_x); /* KEY0 itself is locked after boot. */

    prepare(&gb, true, 0);
    gb.cgb_mode = false;
    gb.boot_rom_enabled = true;
    cupid_gb_write_u8(&gb, 0xff6c, 1);
    assert(cupid_gb_read_u8(&gb, 0xff6c) == 0xff);
    gb.boot_rom_enabled = false;
    cupid_gb_write_u8(&gb, 0xff6c, 0);
    assert(cupid_gb_read_u8(&gb, 0xff6c) ==
           0xff); /* Hardware readback survives compatibility mode. */

    gb.model = CUPID_GB_MODEL_DMG_ABC;
    cupid_gb_write_u8(&gb, 0xff6c, 0);
    assert(cupid_gb_read_u8(&gb, 0xff6c) == 0xff);
}

static void test_midline_writes_preserve_emitted_pixels(bool cgb) {
    CupidGb control, changed;
    prepare(&control, cgb, 1);
    control.io_registers[CUPID_GB_IO_LCDC] = 0x91;
    set_tile(&control, 1, 2);
    for (unsigned x = 0; x < 32; ++x)
        control.video_ram[0x1800 + x] = (uint8_t)(x & 1);
    changed = control;
    while (changed.pixels.x < 48) {
        cupid_gb_tick_ppu(&control, 1);
        cupid_gb_tick_ppu(&changed, 1);
    }
    uint8_t prefix[48];
    uint16_t prefix_color[48];
    memcpy(prefix, changed.frame_buffer, sizeof(prefix));
    memcpy(prefix_color, changed.frame_buffer_color, sizeof(prefix_color));
    changed.io_registers[CUPID_GB_IO_SCX] = 8;
    cupid_gb_tick_ppu(&control, 114);
    cupid_gb_tick_ppu(&changed, 114);
    assert(!memcmp(prefix, changed.frame_buffer, sizeof(prefix)));
    assert(!memcmp(prefix_color, changed.frame_buffer_color, sizeof(prefix_color)));
    assert(memcmp(control.frame_buffer + 72, changed.frame_buffer + 72, 80));
}

static void test_palette_is_sampled_at_pixel_output(void) {
    CupidGb gb;
    prepare(&gb, false, 1);
    gb.io_registers[CUPID_GB_IO_LCDC] = 0x91;
    while (gb.pixels.x < 48)
        cupid_gb_tick_ppu(&gb, 1);
    unsigned split = gb.pixels.x;
    gb.io_registers[CUPID_GB_IO_BGP] = 0xfc;
    cupid_gb_tick_ppu(&gb, 70);
    for (unsigned x = 0; x < split; ++x)
        assert(gb.frame_buffer[x] == 1);
    for (unsigned x = split; x < 160; ++x)
        assert(gb.frame_buffer[x] == 3);
}

static void test_lcd_power_preserves_stat_interrupt_latch(void) {
    CupidGb gb;
    prepare(&gb, false, 0);
    gb.io_registers[CUPID_GB_IO_LY] = 144;
    gb.io_registers[CUPID_GB_IO_LYC] = 144;
    gb.io_registers[CUPID_GB_IO_STAT] = 0xc1;
    cupid_gb_update_stat_irq(&gb);
    assert(gb.stat_irq_line);
    cupid_gb_write_u8(&gb, 0xff40, 0);
    gb.interrupt_flags = 0;
    cupid_gb_write_u8(&gb, 0xff45, 0);
    cupid_gb_tick_ppu(&gb, 16);
    assert(cupid_gb_read_u8(&gb, 0xff41) == 0xc4);
    assert(gb.interrupt_flags == 0);
    cupid_gb_write_u8(&gb, 0xff40, 0x80);
    assert(gb.interrupt_flags == 0); /* The matching condition stayed high across power-off. */
    cupid_gb_write_u8(&gb, 0xff45, 1);
    assert(!gb.stat_irq_line);
    cupid_gb_write_u8(&gb, 0xff45, 0);
    assert(gb.interrupt_flags & 2); /* A new low-to-high comparison still requests STAT. */
}

static void test_halt_midpoint_samples_interrupt_phase(void) {
    CupidGb gb, control;
    prepare(&gb, false, 0);
    gb.interrupt_enable = 1;
    gb.pixels.initialized = true;
    gb.pixels.line = 144;
    gb.pixels.dot = 0;
    gb.io_registers[CUPID_GB_IO_LY] = 144;
    assert(cupid_gb_tick_ppu_halt(&gb) == 1); /* VBlank arrives in the first two dots. */
    assert(gb.interrupt_flags & 1);

    prepare(&gb, false, 0);
    gb.interrupt_enable = 1;
    gb.pixels.initialized = true;
    gb.pixels.line = 143;
    gb.pixels.dot = 455;
    gb.io_registers[CUPID_GB_IO_LY] = 143;
    control = gb;
    assert(cupid_gb_tick_ppu_halt(&gb) == 0); /* The request arrives after the midpoint. */
    assert(gb.interrupt_flags & 1);
    cupid_gb_tick_ppu(&control, 1);
    assert(memcmp(&gb, &control, sizeof(gb)) == 0); /* Sampling does not change the elapsed work. */
}

static void test_hblank_interrupt_follows_visible_mode(void) {
    CupidGb gb;
    prepare(&gb, false, 0);
    gb.io_registers[CUPID_GB_IO_STAT] = 0x8a;
    gb.io_registers[CUPID_GB_IO_SCX] = 3;
    gb.interrupt_enable = 2;
    while ((gb.io_registers[CUPID_GB_IO_STAT] & 3) != CUPID_GB_PPU_MODE_HBLANK)
        cupid_gb_tick_ppu(&gb, 1);
    assert((gb.interrupt_flags & 2) == 0);
    assert(gb.stat_irq_delay == 1); /* Mode 0 begins on the final dot of this M-cycle. */
    assert(cupid_gb_tick_ppu_halt(&gb) == 2);
    assert(gb.interrupt_flags & 2);
    assert(gb.stat_irq_delay == 0);
}

static void test_objects_share_background_fetch_wait(void) {
    static const struct {
        uint8_t count, x, spacing, scroll;
        uint16_t dots;
    } cases[] = {
        {1, 8, 0, 0, 183}, {2, 8, 0, 0, 189}, {3, 8, 0, 0, 195}, {3, 12, 0, 0, 191},
        {3, 8, 2, 0, 195}, {3, 8, 4, 0, 200}, {3, 8, 8, 0, 205}, {1, 0, 0, 3, 186},
        {2, 0, 0, 3, 192}, {3, 8, 2, 3, 195}, {3, 8, 4, 3, 197}, {3, 8, 8, 3, 199},
        {2, 0, 8, 3, 194},
    };

    for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
        CupidGb gb;
        prepare(&gb, false, 0);
        gb.io_registers[CUPID_GB_IO_SCX] = cases[test].scroll;
        for (unsigned i = 0; i < cases[test].count; ++i)
            object(&gb, i, (uint8_t)(cases[test].x + i * cases[test].spacing), 1, 0);
        cupid_gb_pixels_begin_line(&gb);
        for (unsigned i = 0; i < 40; ++i)
            cupid_gb_pixels_scan_object(&gb, i);
        cupid_gb_pixels_begin_transfer(&gb);
        unsigned dots = 0;
        while (!cupid_gb_pixels_tick(&gb) && dots < 400)
            ++dots;
        assert(dots == cases[test].dots);
    }
}

static void test_first_line_oam_interrupt_waits_for_visible_mode(void) {
    CupidGb gb;
    prepare(&gb, false, 0);
    gb.pixels.initialized = true;
    gb.pixels.line = 153;
    gb.pixels.dot = 452;
    gb.io_registers[CUPID_GB_IO_LY] = 0;
    gb.io_registers[CUPID_GB_IO_STAT] = 0xa1;
    cupid_gb_tick_ppu(&gb, 1);
    assert(gb.pixels.line == 0 && gb.pixels.dot == 0);
    assert((gb.io_registers[CUPID_GB_IO_STAT] & 3) == CUPID_GB_PPU_MODE_VBLANK);
    assert((gb.interrupt_flags & 2) == 0);
    cupid_gb_tick_ppu(&gb, 1);
    assert((gb.io_registers[CUPID_GB_IO_STAT] & 3) == CUPID_GB_PPU_MODE_OAM);
    assert(gb.interrupt_flags & 2);
}

static void test_lcd_startup_dot_boundaries(void) {
    static const struct {
        uint16_t clock;
        uint8_t line, mode;
    } boundaries[] = {
        {0, 0, 0},   {79, 0, 0},  {80, 0, 3},  {251, 0, 3}, {252, 0, 0},
        {452, 0, 0}, {453, 1, 0}, {454, 1, 2}, {533, 1, 2}, {534, 1, 3},
        {705, 1, 3}, {706, 1, 0}, {908, 1, 0}, {909, 2, 0}, {910, 2, 2},
    };

    CupidGb gb = {0};
    gb.io_registers[CUPID_GB_IO_WX] = 255;
    cupid_gb_write_u8(&gb, 0xff40, 0x91);
    unsigned clock = 0;
    for (unsigned i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
        while (clock < boundaries[i].clock) {
            cupid_gb_tick_ppu_dots(&gb, 1);
            ++clock;
        }
        assert(cupid_gb_read_u8(&gb, 0xff44) == boundaries[i].line);
        assert((cupid_gb_read_u8(&gb, 0xff41) & 3) == boundaries[i].mode);
    }
}

static void test_oam_read_lock_precedes_visible_scan_mode(void) {
    CupidGb gb;
    for (unsigned color = 0; color < 2; ++color) {
        prepare(&gb, color != 0, 0);
        gb.object_attribute_memory[0] = 0x52;
        cupid_gb_write_u8(&gb, 0xff40, 0);
        assert(cupid_gb_read_u8(&gb, 0xfe00) == 0x52);
        cupid_gb_write_u8(&gb, 0xff40, 0x91);
        cupid_gb_tick_ppu_dots(&gb, color ? 451 : 452);
        assert(gb.pixels.line == 0);
        assert(cupid_gb_read_u8(&gb, 0xfe00) == 0x52);
        cupid_gb_tick_ppu_dots(&gb, 1);
        assert(gb.pixels.line == 1 && gb.pixels.dot == 0);
        assert((cupid_gb_read_u8(&gb, 0xff41) & 3) == CUPID_GB_PPU_MODE_HBLANK);
        assert(cupid_gb_read_u8(&gb, 0xfe00) == 0xff);
        cupid_gb_tick_ppu_dots(&gb, 1);
        assert((cupid_gb_read_u8(&gb, 0xff41) & 3) == CUPID_GB_PPU_MODE_OAM);
        assert(cupid_gb_read_u8(&gb, 0xfe00) == 0xff);
        cupid_gb_tick_ppu_dots(&gb, 455);
        assert(gb.pixels.line == 2 && gb.pixels.dot == 0);
        assert(cupid_gb_read_u8(&gb, 0xfe00) == 0xff);
        cupid_gb_write_u8(&gb, 0xff40, 0);
        assert(cupid_gb_read_u8(&gb, 0xfe00) == 0x52);
    }
}

static void test_stat_unused_bit_reads_high_after_power_on(void) {
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb gb = {0};
        gb.model = (CupidGbModel)model;
        gb.boot_rom_size = 256;
        cupid_gb_enter_boot_rom(&gb);
        assert(gb.io_registers[CUPID_GB_IO_STAT] == 0);
        assert(cupid_gb_read_u8(&gb, 0xff41) == 0x80);
        cupid_gb_write_u8(&gb, 0xff40, 0x80);
        assert(cupid_gb_read_u8(&gb, 0xff41) == 0x84);
        cupid_gb_tick_ppu_dots(&gb, 90);
        assert(cupid_gb_read_u8(&gb, 0xff41) == 0x87);
        cupid_gb_write_u8(&gb, 0xff40, 0);
        assert(cupid_gb_read_u8(&gb, 0xff41) == 0x84);
    }
}

static void test_double_speed_hblank_mode_and_irq_edge(void) {
    CupidGb gb;
    prepare(&gb, true, 0);
    gb.double_speed = true;
    cupid_gb_write_u8(&gb, 0xff40, 0);
    cupid_gb_write_u8(&gb, 0xff41, 8);
    cupid_gb_write_u8(&gb, 0xff40, 0x91);
    gb.interrupt_flags = 0;
    while (gb.pixels.active || gb.pixels.x != CUPID_GB_SCREEN_WIDTH) {
        assert(gb.pixels.dot < 300);
        cupid_gb_tick_ppu_dots(&gb, 1);
    }
    assert((cupid_gb_read_u8(&gb, 0xff41) & 3) == CUPID_GB_PPU_MODE_TRANSFER);
    assert(gb.stat_irq_delay == 1 && !(gb.interrupt_flags & 2));
    cupid_gb_tick_ppu_dots(&gb, 1);
    assert((cupid_gb_read_u8(&gb, 0xff41) & 3) == CUPID_GB_PPU_MODE_HBLANK);
    assert(gb.stat_irq_delay == 0 && (gb.interrupt_flags & 2));
}

static void test_dot_chunks_preserve_pipeline_state(void) {
    static const uint16_t chunks[] = {1, 2, 3, 5, 7, 8, 17};
    CupidGb machine, dots;
    prepare(&machine, false, 1);
    machine.io_registers[CUPID_GB_IO_SCX] = 3;
    object(&machine, 0, 12, 2, 0);
    object(&machine, 1, 12, 1, 0x80);
    dots = machine;
    cupid_gb_tick_ppu(&machine, 200);
    unsigned remaining = 800;
    for (unsigned i = 0; remaining; ++i) {
        uint16_t chunk = chunks[i % (sizeof(chunks) / sizeof(chunks[0]))];
        if (chunk > remaining)
            chunk = (uint16_t)remaining;
        cupid_gb_tick_ppu_dots(&dots, chunk);
        remaining -= chunk;
    }
    assert(memcmp(&machine, &dots, sizeof(machine)) == 0);
}

int main(void) {
    test_dmg_object_order();
    test_dmg_priority_uses_raw_bg_color();
    test_hidden_object_still_wins_obj_priority(false);
    test_hidden_object_still_wins_obj_priority(true);
    test_offscreen_objects_use_scanline_limit(false);
    test_offscreen_objects_use_scanline_limit(true);
    test_cgb_oam_priority();
    test_cgb_priority_latch();
    test_midline_writes_preserve_emitted_pixels(false);
    test_midline_writes_preserve_emitted_pixels(true);
    test_palette_is_sampled_at_pixel_output();
    test_lcd_power_preserves_stat_interrupt_latch();
    test_halt_midpoint_samples_interrupt_phase();
    test_hblank_interrupt_follows_visible_mode();
    test_objects_share_background_fetch_wait();
    test_first_line_oam_interrupt_waits_for_visible_mode();
    test_lcd_startup_dot_boundaries();
    test_oam_read_lock_precedes_visible_scan_mode();
    test_stat_unused_bit_reads_high_after_power_on();
    test_double_speed_hblank_mode_and_irq_edge();
    test_dot_chunks_preserve_pipeline_state();
    puts("PPU priority checks passed.");
    return 0;
}

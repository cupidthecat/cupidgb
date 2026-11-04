#include "cupid/gb/apu.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>

static void prepare(CupidGb *gb, bool cgb, bool double_speed) {
    *gb = (CupidGb){0};
    gb->model = cgb ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
    gb->cgb_mode = cgb;
    gb->double_speed = double_speed;
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff17, 0xf0);
    cupid_gb_write_u8(gb, 0xff16, 62); /* Length two. */
    cupid_gb_write_u8(gb, 0xff19, 0xc0);
    assert((cupid_gb_read_u8(gb, 0xff26) & 2) != 0);
}

static void test_divider_clock(bool double_speed) {
    CupidGb gb;
    unsigned period = double_speed ? 4096 : 2048;
    prepare(&gb, double_speed, double_speed);
    cupid_gb_tick(&gb, (uint16_t)(period - 1));
    assert(gb.apu.ch2_len == 2);
    cupid_gb_tick(&gb, 1);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_tick(&gb, (uint16_t)period);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_tick(&gb, (uint16_t)period);
    assert(gb.apu.ch2_len == 0);
    assert((cupid_gb_read_u8(&gb, 0xff26) & 2) == 0);
}

static void test_div_reset_edge(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_tick(&gb, 1024); /* DIV bit 12 high, no falling edge yet. */
    assert(gb.apu.ch2_len == 2);
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1); /* A low-to-low DIV reset cannot clock length. */
}

static void test_power_on_high_div(void) {
    CupidGb gb = {0};
    gb.io_registers[4] = 0x10;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff17, 0xf0);
    cupid_gb_write_u8(&gb, 0xff16, 62);
    cupid_gb_write_u8(&gb, 0xff19, 0xc0);
    assert(gb.apu.ch2_len == 1); /* Enabling length in this phase clocks once. */
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1); /* First edge after power-on in the high half is skipped. */
    cupid_gb_tick(&gb, 2048);
    assert(gb.apu.ch2_len == 0);
}

static void test_power_counter_reset(bool cgb) {
    CupidGb gb;
    prepare(&gb, cgb, false);
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.ch2_len == 0);
    cupid_gb_write_u8(&gb, 0xff16, 60);
    cupid_gb_write_u8(&gb, 0xff20, 60);
    assert(gb.apu.ch2_len == (cgb ? 0 : 4));
    assert(gb.apu.ch4_len == (cgb ? 0 : 4));
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    assert(gb.apu.ch2_len == (cgb ? 0 : 4));
    assert(gb.apu.ch4_len == (cgb ? 0 : 4));
}

static void test_apu_ticks_do_not_clock_length(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_tick_apu(&gb, 32768);
    assert(gb.apu.ch2_len == 2); /* The frequency clock cannot substitute for DIV edges. */
}

static void test_cgb_compatibility_uses_color_hardware(void) {
    CupidGb gb;
    prepare(&gb, true, false);
    gb.cgb_mode = false; /* A monochrome cartridge changes video mode, not APU hardware. */
    gb.apu.ch3_on = true;
    gb.apu.ch3_current_byte = 3;
    gb.apu.ch3_wave_access_ticks = 0;
    gb.io_registers[0x33] = 0x5a;
    assert(cupid_gb_read_u8(&gb, 0xff3f) == 0x5a);
    cupid_gb_write_u8(&gb, 0xff30, 0xa5);
    assert(gb.io_registers[0x33] == 0xa5);
    cupid_gb_write_u8(&gb, 0xff26, 0);
    cupid_gb_write_u8(&gb, 0xff16, 60);
    cupid_gb_write_u8(&gb, 0xff20, 60);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    assert(gb.apu.ch2_len == 0);
    assert(gb.apu.ch4_len == 0);
}

static void test_sweep_zero_period_and_shadow(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_write_u8(&gb, 0xff10, 0x01);
    cupid_gb_write_u8(&gb, 0xff12, 0xf0);
    cupid_gb_write_u8(&gb, 0xff13, 0);
    cupid_gb_write_u8(&gb, 0xff14, 0x82);
    for (unsigned i = 0; i < 64; ++i) {
        cupid_gb_apu_div_event(&gb);
    }
    assert(gb.apu.ch1_on);
    assert(gb.apu.ch1_freq == 0x200);
    cupid_gb_write_u8(&gb, 0xff10, 0x12);
    cupid_gb_write_u8(&gb, 0xff13, 4);
    cupid_gb_write_u8(&gb, 0xff14, 0x80);
    cupid_gb_write_u8(&gb, 0xff13, 0);
    assert(gb.apu.ch1_sweep_shadow == 4);
    for (unsigned i = 0; i < 3; ++i) {
        cupid_gb_apu_div_event(&gb);
    }
    assert(gb.apu.ch1_freq == 5);
    assert(gb.apu.ch1_sweep_shadow == 5);
}

int main(void) {
    test_divider_clock(false);
    test_divider_clock(true);
    test_div_reset_edge();
    test_power_on_high_div();
    test_power_counter_reset(false);
    test_power_counter_reset(true);
    test_apu_ticks_do_not_clock_length();
    test_cgb_compatibility_uses_color_hardware();
    test_sweep_zero_period_and_shadow();
    puts("APU divider and power regression checks passed.");
    return 0;
}

#include "cupid/gb/gb.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>

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
    const unsigned cycles_per_bit = cgb && fast ? 4 : 128;
    prepare(&gb, cgb, double_speed);
    cupid_gb_write_u8(&gb, 0xff01, 0x41);
    cupid_gb_write_u8(&gb, 0xff02, fast ? 0x83 : 0x81);
    cupid_gb_tick(&gb, (uint16_t)(cycles_per_bit * 8 - 1));
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0xff);
    assert((cupid_gb_read_u8(&gb, 0xff02) & 0x80) != 0);
    assert((cupid_gb_read_u8(&gb, 0xff0f) & 8) == 0);
    cupid_gb_tick(&gb, 1);
    assert((cupid_gb_read_u8(&gb, 0xff02) & 0x80) == 0);
    assert((cupid_gb_read_u8(&gb, 0xff0f) & 8) != 0);
    assert(gb.elapsed_cycles == cycles_per_bit * 8 * (double_speed ? 2 : 4));
}

static void test_external_clock(void) {
    CupidGb gb;
    prepare(&gb, true, false);
    cupid_gb_write_u8(&gb, 0xff01, 0x55);
    cupid_gb_write_u8(&gb, 0xff02, 0x82);
    cupid_gb_tick(&gb, 2048);
    assert(cupid_gb_read_u8(&gb, 0xff01) == 0x55);
    assert((cupid_gb_read_u8(&gb, 0xff02) & 0x80) != 0);
    assert((cupid_gb_read_u8(&gb, 0xff0f) & 8) == 0);
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

int main(void) {
    test_control_masks();
    test_byte_duration(false, false, false);
    test_byte_duration(false, true, false);
    test_byte_duration(true, false, false);
    test_byte_duration(true, true, false);
    test_byte_duration(true, true, true);
    test_external_clock();
    test_divider_phase_and_reset();
    test_control_write_completes_high_phase();
    test_div_reset_can_shift();
    test_control_access_restarts_byte_count();
    test_base_clock_counts_with_lcd_off();
    puts("Serial timing checks passed.");
    return 0;
}

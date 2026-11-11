#include "cupid/common/sha256.h"
#include "cupid/gb/battery.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/huc3.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>

static CupidGb *new_cartridge(CupidGbModel model) {
    uint8_t rom[0x8000] = {0};
    rom[0x143] = 0x80;
    rom[0x147] = 0xfe;
    rom[0x149] = 2;
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    gb->model = model;
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(gb->header.mbc_type == CUPID_GB_HUC3 && gb->header.has_timer);
    return gb;
}

static void destroy(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void command(CupidGb *gb, uint8_t value) {
    cupid_gb_write_u8(gb, 0xbabc, value);
}

static void set_index(CupidGb *gb, unsigned index) {
    command(gb, (uint8_t)(0x40u | (index & 0x0fu)));
    command(gb, (uint8_t)(0x50u | (index >> 4)));
}

static void write_digits(CupidGb *gb, unsigned value, unsigned digits) {
    for (unsigned i = 0; i < digits; ++i) {
        command(gb, (uint8_t)(0x30u | (value & 0x0fu)));
        value >>= 4;
    }
}

static uint8_t read_window(CupidGb *gb, uint8_t mode) {
    cupid_gb_write_u8(gb, 0x1fff, mode);
    return cupid_gb_read_u8(gb, 0xa031);
}

static void test_command_bus(void) {
    CupidGb *gb = new_cartridge(CUPID_GB_MODEL_DMG_ABC);
    assert(read_window(gb, CUPID_GB_HUC3_COMMAND) == 1);
    set_index(gb, 0);
    write_digits(gb, 0x5a5, 3);
    write_digits(gb, 0xabcd, 4);
    set_index(gb, 1);
    command(gb, 0x24);
    command(gb, 0x29);
    assert(gb->huc3.index == 1 && gb->huc3.minutes == 0x595);
    assert(gb->huc3.days == 0xabcd);

    command(gb, 0x10);
    assert(read_window(gb, CUPID_GB_HUC3_RESPONSE) == 9);
    command(gb, 0x10);
    assert(read_window(gb, CUPID_GB_HUC3_RESPONSE) == 9);
    assert(read_window(gb, CUPID_GB_HUC3_STATUS) == 1);
    command(gb, 0xfe);
    assert(read_window(gb, 9) == 1);
    read_window(gb, CUPID_GB_HUC3_COMMAND);
    set_index(gb, 0xff);
    command(gb, 0x10);
    assert(gb->huc3.index == 0);
    assert(read_window(gb, CUPID_GB_HUC3_RESPONSE) == 9);
    read_window(gb, CUPID_GB_HUC3_COMMAND);
    command(gb, 0x10);
    command(gb, 0x62);
    assert(read_window(gb, CUPID_GB_HUC3_RESPONSE) == 1);
    read_window(gb, CUPID_GB_HUC3_COMMAND);
    command(gb, 0x63);
    assert(read_window(gb, CUPID_GB_HUC3_RESPONSE) == 5);

    read_window(gb, CUPID_GB_HUC3_COMMAND);
    const uint64_t generation = gb->battery_write_generation;
    set_index(gb, 0x58);
    write_digits(gb, 0x115, 3);
    write_digits(gb, 0x2002, 4);
    command(gb, 0x3b);
    assert(gb->huc3.alarm_minutes == 0x115 && gb->huc3.alarm_days == 0x2002);
    assert(gb->huc3.alarm_enabled && gb->battery_write_generation > generation);
    destroy(gb);
}

static void test_clock_and_battery(void) {
    CupidGbHuc3 clock = {.rtc_base_time = 60030, .minutes = 1439, .days = 65535};
    cupid_gb_huc3_advance(&clock, 60059);
    assert(clock.minutes == 1439 && clock.days == 65535);
    cupid_gb_huc3_advance(&clock, 60060);
    assert(clock.minutes == 0 && clock.days == 0);
    cupid_gb_huc3_advance(&clock, 60059);
    assert(clock.minutes == 0 && clock.days == 0);
    cupid_gb_huc3_advance(&clock, 60060 + 60 * 1442);
    assert(clock.minutes == 2 && clock.days == 1);
    clock = (CupidGbHuc3){.rtc_base_time = 60000, .minutes = 65534, .days = 13};
    cupid_gb_huc3_advance(&clock, 60120);
    assert(clock.minutes == 0 && clock.days == 13);
    cupid_gb_huc3_advance(&clock, 60120 + 60 * 1440);
    assert(clock.minutes == 0 && clock.days == 14);

    CupidGb *gb = new_cartridge(CUPID_GB_MODEL_DMG_ABC);
    CupidGb *target = new_cartridge(CUPID_GB_MODEL_DMG_ABC);
    gb->huc3 = (CupidGbHuc3){.rtc_base_time = 60030,
                             .minutes = 1439,
                             .days = 1,
                             .alarm_minutes = 4,
                             .alarm_days = 0x2002,
                             .alarm_enabled = true};
    assert(cupid_gb_huc3_seconds_to_alarm(gb, 60059) == 241);
    assert(cupid_gb_huc3_seconds_to_alarm(gb, 60060) == 240);
    assert(cupid_gb_huc3_seconds_to_alarm(gb, 60300) == 0);
    gb->cartridge_ram[7] = 0x94;
    const size_t size = cupid_gb_battery_size(gb);
    uint8_t *battery = malloc(size);
    assert(battery && size == gb->cartridge_ram_size + 17);
    assert(cupid_gb_save_battery_at(gb, battery, size, NULL, 60060) == CUPID_GB_BATTERY_OK);
    const uint8_t trailer[] = {0x9c, 0xea, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 4, 0, 2, 0x20, 1};
    for (size_t i = 0; i < sizeof(trailer); ++i)
        assert(battery[gb->cartridge_ram_size + i] == trailer[i]);
    for (int64_t now = 60060; now < 60120; ++now) {
        assert(cupid_gb_load_battery_at(target, battery, size, now) == CUPID_GB_BATTERY_OK);
        assert(target->huc3.minutes == 0 && target->huc3.days == 2 && target->huc3.alarm_enabled &&
               target->huc3.alarm_minutes == 4 && target->huc3.alarm_days == 0x2002 &&
               target->cartridge_ram[7] == 0x94);
    }
    assert(cupid_gb_load_battery_at(target, battery, size, 60240) == CUPID_GB_BATTERY_OK);
    assert(target->huc3.minutes == 3 && target->huc3.days == 2 && target->huc3.alarm_enabled);
    assert(target->huc3.alarm_minutes == 4 && target->huc3.alarm_days == 0x2002);
    assert(target->cartridge_ram[7] == 0x94);
    battery[7] = 0x01;
    battery[size - 1] = 2;
    assert(cupid_gb_load_battery_at(target, battery, size, 60240) == CUPID_GB_BATTERY_INVALID_RTC);
    assert(cupid_gb_load_battery_at(target, battery, size - 1, 60240) ==
           CUPID_GB_BATTERY_INVALID_SIZE);
    assert(target->huc3.minutes == 3 && target->cartridge_ram[7] == 0x94);
    battery[size - 1] = 1;
    assert(cupid_gb_load_battery_at(target, battery, size, 60000) == CUPID_GB_BATTERY_OK);
    assert(target->huc3.minutes == 0x0fff && target->huc3.days == 0xffff);
    assert(!target->huc3.alarm_enabled && target->cartridge_ram[7] == 1);
    assert(cupid_gb_load_battery_at(target, battery, target->cartridge_ram_size, 60240) ==
           CUPID_GB_BATTERY_OK);
    assert(target->huc3.minutes == 0x0fff && target->huc3.days == 0xffff);
    free(battery);
    destroy(target);
    destroy(gb);
}

static void test_infrared_and_state(void) {
    CupidGb *gb = new_cartridge(CUPID_GB_MODEL_CGB);
    CupidGb *target = new_cartridge(CUPID_GB_MODEL_CGB);
    assert(read_window(gb, CUPID_GB_HUC3_INFRARED) == 0);
    cupid_gb_write_u8(gb, 0xff56, 0xc0);
    cupid_gb_huc3_set_infrared_input(gb, true);
    cupid_gb_tick(gb, 3000);
    assert(cupid_gb_read_u8(gb, 0xa000) == 0);
    cupid_gb_tick(gb, 1400);
    assert(cupid_gb_read_u8(gb, 0xa000) == 1);
    assert(cupid_gb_read_u8(gb, 0xff56) == 0xec);
    cupid_gb_huc3_set_infrared_input(gb, false);
    cupid_gb_tick(gb, 1);
    assert(cupid_gb_read_u8(gb, 0xa000) == 0);
    command(gb, 0xff);
    assert(cupid_gb_huc3_infrared_output(gb));
    cupid_gb_tick(gb, 1);
    assert(cupid_gb_read_u8(gb, 0xa000) == 1);

    const size_t size = cupid_gb_state_size(gb);
    uint8_t *state = malloc(size);
    assert(state && cupid_gb_save_state(gb, state, size, NULL) == CUPID_GB_STATE_OK);
    cupid_gb_huc3_set_infrared_input(target, true);
    assert(cupid_gb_load_state(target, state, size) == CUPID_GB_STATE_OK);
    assert(target->huc3_ir_input && cupid_gb_huc3_infrared_output(target));
    assert(cupid_gb_read_u8(target, 0xa000) == 1);
    cupid_gb_tick(gb, 7000);
    cupid_gb_tick(target, 7000);
    assert(cupid_gb_read_u8(gb, 0xa000) == 0);
    assert(cupid_gb_read_u8(target, 0xa000) == 0);
    command(target, 0xfe);
    assert(!cupid_gb_huc3_infrared_output(target));
    free(state);
    destroy(target);
    destroy(gb);
}

static void test_legacy_state_without_clock(void) {
    for (unsigned model_index = 0; model_index < 2; ++model_index) {
        const bool sgb = model_index == 1;
        const CupidGbModel model = sgb ? CUPID_GB_MODEL_SGB : CUPID_GB_MODEL_DMG_ABC;
        CupidGb *gb = new_cartridge(model);
        read_window(gb, CUPID_GB_HUC3_COMMAND);
        set_index(gb, 2);
        command(gb, 0x37);
        command(gb, 0x62);
        gb->cartridge_ram[0] = 0x73;
        const size_t current_size = cupid_gb_state_size(gb);
        uint8_t *state = malloc(current_size);
        assert(state && cupid_gb_save_state(gb, state, current_size, NULL) == CUPID_GB_STATE_OK);

        /* These versions stored the mapper's bank and mode in the common
         * prefix. The version 5 variant also stored the SGB border extension. */
        const size_t version_four_size = 300572;
        const size_t sgb_border_size =
            7 + 8192 +
            2 * (CUPID_SGB_TILE_WIDTH * CUPID_SGB_TILE_HEIGHT + CUPID_SGB_BORDER_PALETTE_COLORS);
        const size_t legacy_size = version_four_size + (sgb ? sgb_border_size : 0);
        state[8] = sgb ? 5 : 4;
        for (unsigned i = 0; i < 4; ++i)
            state[12 + i] = (uint8_t)(legacy_size >> (8u * i));
        cupid_sha256_parts(state, 96, state + 128, legacy_size - 128, state + 96);
        assert(cupid_gb_load_state(gb, state, legacy_size) == CUPID_GB_STATE_OK);
        assert(gb->huc3_mode == CUPID_GB_HUC3_COMMAND && gb->cartridge_ram[0] == 0x73);
        assert(gb->huc3.index == 0 && gb->huc3.flags == 0 && gb->huc3.minutes == 0);
        free(state);
        destroy(gb);
    }
}

static void test_infrared_during_speed_switch(void) {
    CupidGb *gb = new_cartridge(CUPID_GB_MODEL_CGB);
    gb->huc3_mode = CUPID_GB_HUC3_INFRARED;
    gb->speed_switch_toggle_cycles = 8;
    const uint64_t before = gb->elapsed_cycles;
    CupidGbCpuBus bus = {0};
    cupid_gb_cpu_write(gb, 0xff41, 0x20, 1, &bus);
    cupid_gb_cpu_finish(gb, 1, &bus);
    assert(gb->elapsed_cycles > before);
    assert(gb->huc3.ir_warmup == (gb->elapsed_cycles - before) * 2);
    destroy(gb);
}

int main(void) {
    test_command_bus();
    test_clock_and_battery();
    test_infrared_and_state();
    test_infrared_during_speed_switch();
    test_legacy_state_without_clock();
    return 0;
}

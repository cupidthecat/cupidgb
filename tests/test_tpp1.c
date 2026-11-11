#include "cupid/gb/battery.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/state.h"
#include "cupid/gb/tpp1.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROM_BANK_SIZE = 0x4000, ROM_BANKS = 8, RAM_BANK_SIZE = 0x2000 };

static void update_checksum(uint8_t *rom) {
    uint8_t checksum = 0;
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        checksum = (uint8_t)(checksum - rom[offset] - 1);
    rom[0x14d] = checksum;
}

static void make_rom(uint8_t *rom, bool color) {
    memset(rom, 0, ROM_BANK_SIZE * ROM_BANKS);
    for (unsigned bank = 0; bank < ROM_BANKS; ++bank)
        rom[bank * ROM_BANK_SIZE] = (uint8_t)bank;
    memcpy(rom + 0x134, "TPP1 TEST", 9);
    rom[0x143] = color ? 0x80 : 0;
    rom[0x147] = 0xbc;
    rom[0x148] = 2;
    rom[0x149] = 0xc1;
    rom[0x14a] = 0x65;
    rom[0x152] = 3;
    rom[0x153] = 8;
    update_checksum(rom);
}

static void command(CupidGb *gb, uint8_t value) {
    cupid_gb_write_u8(gb, 0x7fff, value);
}

typedef struct RumbleProbe {
    unsigned transitions;
    bool enabled;
} RumbleProbe;

static void observe_motor(const CupidGb *gb, bool enabled, uint64_t cycles, void *user_data) {
    RumbleProbe *probe = user_data;
    assert(cupid_gb_rumble_enabled(gb) == enabled);
    assert(cycles == gb->elapsed_cycles);
    ++probe->transitions;
    probe->enabled = enabled;
}

static void test_bank_modes_and_rumble(CupidGb *gb) {
    assert(gb->header.mbc_type == CUPID_GB_TPP1 && gb->header.has_battery);
    assert(gb->ram_bank_count == 4 && gb->cartridge_ram_size == 4 * RAM_BANK_SIZE);
    assert(cupid_gb_read_u8(gb, 0x4000) == 1 && cupid_gb_read_u8(gb, 0xa004) == 1);
    cupid_gb_write_u8(gb, 0x401c, 0x37);
    cupid_gb_write_u8(gb, 0x51fd, 0x89);
    cupid_gb_write_u8(gb, 0x6ffe, 0xfd);
    assert(cupid_gb_read_u8(gb, 0x4000) == 7 && cupid_gb_read_u8(gb, 0x0000) == 0);
    assert(cupid_gb_read_u8(gb, 0xbffc) == 0x37);
    assert(cupid_gb_read_u8(gb, 0xbffd) == 0x89);
    assert(cupid_gb_read_u8(gb, 0xbffe) == 0xfd);

    command(gb, 3);
    for (unsigned bank = 0; bank < 4; ++bank) {
        cupid_gb_write_u8(gb, 0x0002, (uint8_t)bank);
        assert(cupid_gb_read_u8(gb, 0xa123) == 0xff);
        cupid_gb_write_u8(gb, 0xa123, (uint8_t)(0x30 + bank));
    }
    command(gb, 2);
    command(gb, 4); /* Undefined commands leave the current mode selected. */
    for (unsigned bank = 0; bank < 256; ++bank) {
        cupid_gb_write_u8(gb, 0x0002, (uint8_t)bank);
        cupid_gb_write_u8(gb, 0xa123, 0x70);
        assert(cupid_gb_read_u8(gb, 0xa123) == 0x30 + (bank & 3));
    }
    command(gb, 0);
    RumbleProbe motor = {0};
    cupid_gb_set_rumble_callback(gb, observe_motor, &motor);
    for (unsigned strength = 1; strength <= 3; ++strength) {
        command(gb, (uint8_t)(0x20 + strength));
        assert(cupid_gb_read_u8(gb, 0xa003) == strength);
        assert(motor.enabled && motor.transitions == 1);
    }
    command(gb, 0x20);
    assert(!motor.enabled && motor.transitions == 2);
    cupid_gb_set_rumble_callback(gb, NULL, NULL);
}

static void test_clock_and_battery(CupidGb *gb) {
    static const uint8_t end_of_week[] = {255, 0xd7, 59, 59};
    command(gb, 5);
    for (unsigned port = 0; port < sizeof(end_of_week); ++port)
        cupid_gb_write_u8(gb, (uint16_t)(0xa004 + port), end_of_week[port]);
    command(gb, 0x11);
    cupid_gb_write_u8(gb, 0xa003, 0);
    command(gb, 0x10);
    assert(cupid_gb_read_u8(gb, 0xbfff) == 59);
    command(gb, 0x19);
    cupid_gb_tpp1_advance(&gb->tpp1, gb->tpp1.rtc_base_time + 86401);
    assert(gb->tpp1.status == 12);
    assert(gb->tpp1.live[0] == 0 && gb->tpp1.live[1] == 0x20);
    assert(gb->tpp1.live[2] == 0 && gb->tpp1.live[3] == 0);
    command(gb, 0x18);
    command(gb, 0);
    assert(cupid_gb_read_u8(gb, 0xa003) == 8);
    command(gb, 0x14);
    assert(cupid_gb_read_u8(gb, 0xa003) == 0);

    const size_t length = cupid_gb_battery_size(gb);
    assert(length == 4 * RAM_BANK_SIZE + 20);
    uint8_t *battery = malloc(length);
    assert(battery);
    command(gb, 3);
    cupid_gb_write_u8(gb, 0x0002, 0);
    cupid_gb_write_u8(gb, 0xa123, 0x29);
    assert(cupid_gb_save_battery_at(gb, battery, length, NULL, INT64_C(1800000000)) ==
           CUPID_GB_BATTERY_OK);
    assert(memcmp(battery + length - 20, "TPP1\x01\x00", 6) == 0);
    cupid_gb_write_u8(gb, 0xa123, 0x53);
    battery[length - 20] = 0;
    assert(cupid_gb_load_battery_at(gb, battery, length, INT64_C(1800000000)) ==
           CUPID_GB_BATTERY_INVALID_RTC);
    assert(cupid_gb_read_u8(gb, 0xa123) == 0x53);
    battery[length - 20] = 'T';
    battery[length - 14] = 2;
    RumbleProbe motor = {0};
    cupid_gb_set_rumble_callback(gb, observe_motor, &motor);
    assert(cupid_gb_load_battery_at(gb, battery, length, INT64_C(1800000000)) ==
           CUPID_GB_BATTERY_OK);
    assert(cupid_gb_read_u8(gb, 0xa123) == 0x29);
    assert(cupid_gb_rumble_enabled(gb) && motor.transitions == 0);
    command(gb, 0);
    assert(cupid_gb_read_u8(gb, 0xa003) == 2);
    assert(cupid_gb_save_battery_at(gb, battery, length, NULL, INT64_C(1800000000)) ==
           CUPID_GB_BATTERY_OK);
    assert(battery[length - 14] == 2);
    cupid_gb_set_rumble_callback(gb, NULL, NULL);
    free(battery);
}

static void test_state_restore(CupidGb *gb) {
    command(gb, 0x22);
    command(gb, 5);
    cupid_gb_write_u8(gb, 0xa003, 0x58);
    const size_t size = cupid_gb_state_size(gb);
    assert(size);
    uint8_t *state = malloc(size);
    assert(state && cupid_gb_save_state(gb, state, size, NULL) == CUPID_GB_STATE_OK);
    assert(state[8] == 5);
    command(gb, 0x20);
    command(gb, 0);
    cupid_gb_write_u8(gb, 0, 0);
    cupid_gb_write_u8(gb, 1, 0);
    assert(cupid_gb_load_state(gb, state, size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_rumble_enabled(gb));
    assert(cupid_gb_read_u8(gb, 0xa003) == 0x58);
    assert(cupid_gb_read_u8(gb, 0x4000) == 7);
    free(state);
}

int main(void) {
    const size_t length = ROM_BANK_SIZE * ROM_BANKS;
    uint8_t *rom = malloc(length);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(rom && gb);
    for (unsigned color = 0; color < 2; ++color) {
        make_rom(rom, color != 0);
        cupid_gb_set_model(gb, color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
        assert(cupid_gb_load_rom(gb, rom, length));
        test_bank_modes_and_rumble(gb);
        test_clock_and_battery(gb);
        test_state_restore(gb);
        rom[0x152] = 6; /* 256 KiB exceeds the current core SRAM capacity. */
        assert(!cupid_gb_load_rom(gb, rom, length));
        assert(cupid_gb_read_u8(gb, 0x4000) == 7 && gb->tpp1.mode == 5);
        rom[0x152] = 0;
        rom[0x153] = 0;
        assert(cupid_gb_load_rom(gb, rom, length));
        assert(!gb->cartridge_ram_size && !cupid_gb_battery_size(gb));
    }
    cupid_gb_cleanup(gb);
    free(gb);
    free(rom);
    puts("TPP1 cartridge regressions passed.");
    return 0;
}

#include "cupid/gb/cartridge.h"
#include "cupid/gb/cheats.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static CupidGb *cartridge(uint8_t type) {
    uint8_t rom[0x10000] = {0};
    rom[0x100] = 0xfa;
    rom[0x101] = 0x01;
    rom[0x102] = 0xc0;
    rom[0x103] = 0xea;
    rom[0x104] = 0x02;
    rom[0x105] = 0xc0;
    rom[0x106] = 0x18;
    rom[0x107] = 0xfe;
    rom[0x143] = 0x80;
    rom[0x147] = type;
    rom[0x148] = 1;
    rom[0x149] = 3;
    for (unsigned bank = 0; bank < 4; ++bank)
        rom[bank * 0x4000u + 0x220u] = (uint8_t)bank;
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    gb->model = CUPID_GB_MODEL_CGB;
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    return gb;
}

static void release(CupidGb *gb, CupidGbCheats *cheats) {
    cupid_gb_set_cheats(gb, NULL);
    cupid_gb_cheats_destroy(cheats);
    cupid_gb_cleanup(gb);
    free(gb);
}

static void test_code_formats(void) {
    CupidGbCheat cheat;
    assert(cupid_gb_cheat_decode("01FF00C0", "infinite", true, &cheat));
    assert(cheat.address == 0xc000 && cheat.bank == CUPID_GB_CHEAT_ANY_BANK);
    assert(cheat.value == 0xff && !cheat.use_compare && cheat.enabled);
    assert(!strcmp(cheat.description, "infinite"));
    assert(cupid_gb_cheat_decode("A3a912d3", "", false, &cheat));
    assert(cheat.address == 0xd312 && cheat.bank == 3 && cheat.value == 0xa9);
    assert(!cheat.enabled);
    assert(cupid_gb_cheat_decode("00FF00C0", NULL, true, &cheat) && cheat.bank == 0);
    assert(cupid_gb_cheat_decode("00A-17B", "", true, &cheat));
    assert(cheat.address == 0x4a17 && cheat.value == 0 && !cheat.use_compare);
    assert(cupid_gb_cheat_decode("01A-17B-DEC", "", true, &cheat));
    assert(cheat.address == 0x4a17 && cheat.value == 1 && cheat.use_compare);
    assert(cheat.compare_value == 0x8d);
    const char *invalid[] = {"",          "xyz",       "01GG00C0",
                             "0A-17B",    "00A-170",   "01A-17B-DEC0",
                             " 01FF00C0", "01FF00C0 ", "-00A-17B"};
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(*invalid); ++index)
        assert(!cupid_gb_cheat_decode(invalid[index], "", true, &cheat));
    char description[200];
    memset(description, 'x', sizeof(description) - 1);
    description[sizeof(description) - 1] = 0;
    assert(cupid_gb_cheat_decode("00A-17B", description, true, &cheat));
    assert(strlen(cheat.description) == 127);
}

static void test_read_interception_and_bank_aliases(void) {
    CupidGb *gb = cartridge(0x1b);
    CupidGbCheats *cheats = cupid_gb_cheats_create();
    assert(cheats && !cupid_gb_cheats_enabled(cheats));
    cupid_gb_set_cheats(gb, cheats);
    CupidGbCheat record = {.address = 0x4220, .bank = 9, .value = 0xe1, .enabled = true};
    assert(cupid_gb_cheats_add(cheats, &record) == CUPID_GB_CHEAT_OK);
    cupid_gb_write_u8(gb, 0x2000, 9);
    assert(cupid_gb_selected_rom_bank(gb, false) == 9);
    assert(cupid_gb_effective_rom_bank(gb, false) == 1);
    assert(cupid_gb_read_u8(gb, 0x4220) == 1);
    cupid_gb_cheats_set_enabled(cheats, true);
    assert(cupid_gb_read_bus_u8(gb, 0x4220) == 0xe1);
    assert(cupid_gb_sample_cartridge_bus(gb) == 0xe1);
    assert(gb->rom[0x4220] == 1);
    cupid_gb_write_u8(gb, 0x2000, 1);
    assert(cupid_gb_read_u8(gb, 0x4220) == 1);

    record = (CupidGbCheat){.address = 0xa000, .bank = 1, .value = 0x7d, .enabled = true};
    assert(cupid_gb_cheats_add(cheats, &record) == CUPID_GB_CHEAT_OK);
    cupid_gb_write_u8(gb, 0, 0x0a);
    cupid_gb_write_u8(gb, 0x4000, 5);
    cupid_gb_write_u8(gb, 0xa000, 0x32);
    assert(cupid_gb_read_bus_u8(gb, 0xa000) == 0x7d);
    assert(cupid_gb_sample_cartridge_bus(gb) == 0x7d);
    assert(gb->cartridge_ram[0x2000] == 0x32);
    assert(gb->battery_write_generation == 1);
    cupid_gb_write_u8(gb, 0, 0);
    assert(cupid_gb_read_bus_u8(gb, 0x4220) == 1);
    assert(cupid_gb_read_bus_u8(gb, 0xa000) == 0x7d);
    assert(cupid_gb_sample_cartridge_bus(gb) == 1);

    record = (CupidGbCheat){.address = 0xd030, .bank = 3, .value = 0xc9, .enabled = true};
    assert(cupid_gb_cheats_add(cheats, &record) == CUPID_GB_CHEAT_OK);
    cupid_gb_write_u8(gb, 0xff70, 3);
    assert(cupid_gb_read_u8(gb, 0xd030) == 0xc9);
    cupid_gb_write_u8(gb, 0xff70, 2);
    assert(cupid_gb_read_u8(gb, 0xd030) == 0);
    record.address = 0xf030;
    record.bank = 0;
    record.value = 0x65;
    assert(cupid_gb_cheats_add(cheats, &record) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xf030) == 0x65);
    assert(cupid_gb_read_u8(gb, 0xd030) == 0);

    gb->boot_rom[0] = 0x24;
    gb->boot_rom_size = 0x900;
    gb->boot_rom_enabled = true;
    assert(cupid_gb_read_u8(gb, 0xf030) == 0);
    gb->boot_rom_enabled = false;
    release(gb, cheats);
}

static void test_priority_and_transactional_files(void) {
    CupidGb *gb = cartridge(0x1b);
    CupidGbCheats *cheats = cupid_gb_cheats_create();
    assert(cheats);
    cupid_gb_set_cheats(gb, cheats);
    cupid_gb_cheats_set_enabled(cheats, true);
    CupidGbCheat record = {.address = 0xc000,
                           .bank = CUPID_GB_CHEAT_ANY_BANK,
                           .value = 1,
                           .compare_value = 0,
                           .use_compare = true,
                           .enabled = true};
    assert(cupid_gb_cheats_add(cheats, &record) == CUPID_GB_CHEAT_OK);
    record.value = 2;
    assert(cupid_gb_cheats_add(cheats, &record) == CUPID_GB_CHEAT_OK);
    record.value = 3;
    assert(cupid_gb_cheats_add(cheats, &record) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xc000) == 1);
    assert(cupid_gb_cheats_remove(cheats, 0));
    assert(cupid_gb_read_u8(gb, 0xc000) == 3);
    record = *cupid_gb_cheats_get(cheats, 0);
    record.enabled = false;
    assert(cupid_gb_cheats_update(cheats, 0, &record) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xc000) == 2);
    record.enabled = true;
    record.address = 0xc100;
    assert(cupid_gb_cheats_update(cheats, 0, &record) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xc100) == 3);
    record = *cupid_gb_cheats_get(cheats, 1);
    record.address = 0xc100;
    assert(cupid_gb_cheats_update(cheats, 1, &record) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xc100) == 3);

    const uint8_t empty_file[] = {0x68, 0x43, 0x42, 0x53, 136, 0, 0, 0};
    record = *cupid_gb_cheats_get(cheats, 0);
    record.address = 0xc000;
    assert(cupid_gb_cheats_update(cheats, 0, &record) == CUPID_GB_CHEAT_OK);
    record.address = 0xc100;
    assert(cupid_gb_cheats_update(cheats, 0, &record) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xc100) == 2);
    CupidGbCheats *copy = cupid_gb_cheats_clone(cheats);
    assert(copy && cupid_gb_cheats_enabled(copy) && cupid_gb_cheats_count(copy) == 2);
    cupid_gb_set_cheats(gb, copy);
    assert(cupid_gb_read_u8(gb, 0xc100) == 2);
    cupid_gb_set_cheats(gb, cheats);
    cupid_gb_cheats_destroy(copy);
    assert(cupid_gb_cheats_decode(cheats, empty_file, sizeof(empty_file), false) ==
           CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xc100) == 2);
    record = *cupid_gb_cheats_get(cheats, 1);
    record.address = 0xc000;
    assert(cupid_gb_cheats_update(cheats, 1, &record) == CUPID_GB_CHEAT_OK);
    record.address = 0xc100;
    assert(cupid_gb_cheats_update(cheats, 1, &record) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_cheats_decode(cheats, empty_file, sizeof(empty_file), false) ==
           CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xc100) == 3);
    size_t size = cupid_gb_cheats_encoded_size(cheats), written = 0;
    uint8_t *bytes = malloc(size);
    assert(bytes && size == 8 + 2 * 136);
    assert(cupid_gb_cheats_encode(cheats, bytes, size, &written) == CUPID_GB_CHEAT_OK);
    assert(written == size && memcmp(bytes, empty_file, sizeof(empty_file)) == 0);
    assert(bytes[8] == 0 && bytes[9] == 0xc1 && bytes[12] == 3);
    bytes[8 + 136 + 7] = 2;
    assert(cupid_gb_cheats_decode(cheats, bytes, size, true) == CUPID_GB_CHEAT_INVALID_DATA);
    assert(cupid_gb_cheats_decode(cheats, bytes, size - 1, true) == CUPID_GB_CHEAT_INVALID_DATA);
    assert(cupid_gb_cheats_count(cheats) == 2 && cupid_gb_read_u8(gb, 0xc100) == 3);
    bytes[8 + 136 + 7] = 1;
    assert(cupid_gb_cheats_decode(cheats, bytes, size, false) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_cheats_count(cheats) == 4 && cupid_gb_cheats_enabled(cheats));
    assert(cupid_gb_cheats_decode(cheats, bytes, size, true) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_cheats_count(cheats) == 2);

    /* Cross-endian files reverse address and bank independently of text. */
    const uint8_t big_header[] = {0x53, 0x42, 0x43, 0x68, 0, 0, 0, 136};
    memcpy(bytes, big_header, sizeof(big_header));
    for (size_t offset = 8; offset < size; offset += 136) {
        uint8_t byte = bytes[offset];
        bytes[offset] = bytes[offset + 1];
        bytes[offset + 1] = byte;
        byte = bytes[offset + 2];
        bytes[offset + 2] = bytes[offset + 3];
        bytes[offset + 3] = byte;
    }
    assert(cupid_gb_cheats_decode(cheats, bytes, size, true) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_read_u8(gb, 0xc100) == 3);
    assert(cupid_gb_cheats_decode(cheats, empty_file, sizeof(empty_file), true) ==
           CUPID_GB_CHEAT_OK);
    assert(cupid_gb_cheats_count(cheats) == 0 && cupid_gb_cheats_enabled(cheats));
    free(bytes);
    release(gb, cheats);
}

static void test_execution_and_host_state(void) {
    CupidGb *gb = cartridge(0x1b);
    CupidGbCheats *cheats = cupid_gb_cheats_create();
    assert(cheats &&
           cupid_gb_cheats_import(cheats, "019901C0", "read override", true) == CUPID_GB_CHEAT_OK);
    cupid_gb_cheats_set_enabled(cheats, true);
    cupid_gb_set_cheats(gb, cheats);
    CupidGbCheat opcode = {
        .address = 0x100, .bank = CUPID_GB_CHEAT_ANY_BANK, .value = 0x3e, .enabled = true};
    assert(cupid_gb_cheats_add(cheats, &opcode) == CUPID_GB_CHEAT_OK);
    opcode.address = 0x101;
    opcode.value = 0x77;
    assert(cupid_gb_cheats_add(cheats, &opcode) == CUPID_GB_CHEAT_OK);
    assert(cupid_gb_step(gb) && gb->cpu.a == 0x77 && gb->cpu.pc == 0x102);
    assert(gb->rom[0x100] == 0xfa && gb->rom[0x101] == 1);
    assert(cupid_gb_cheats_remove(cheats, 2) && cupid_gb_cheats_remove(cheats, 1));
    gb->cpu.pc = 0x100;
    gb->work_ram[1] = 0x23;
    assert(cupid_gb_step(gb) && gb->cpu.a == 0x99);
    assert(cupid_gb_step(gb) && gb->work_ram[2] == 0x99 && gb->work_ram[1] == 0x23);
    const size_t size = cupid_gb_state_size(gb);
    uint8_t *state = malloc(size), *disabled = malloc(size);
    assert(state && disabled);
    assert(cupid_gb_save_state(gb, state, size, NULL) == CUPID_GB_STATE_OK);
    cupid_gb_cheats_set_enabled(cheats, false);
    assert(cupid_gb_save_state(gb, disabled, size, NULL) == CUPID_GB_STATE_OK);
    assert(memcmp(state, disabled, size) == 0);
    assert(cupid_gb_load_state(gb, state, size) == CUPID_GB_STATE_OK);
    assert(gb->cheats == cheats && !cupid_gb_cheats_enabled(cheats));
    cupid_gb_init(gb);
    assert(gb->cheats == cheats);
    free(disabled);
    free(state);
    release(gb, cheats);
}

int main(void) {
    test_code_formats();
    test_read_interception_and_bank_aliases();
    test_priority_and_transactional_files();
    test_execution_and_host_state();
    return 0;
}

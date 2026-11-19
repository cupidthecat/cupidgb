/**
 * @file    test_sgb_protocol.c
 * @brief   Bus-level SGB boot header, receiver permission, controller and reset regressions.
 * @author  cupidthecat
 * @date    2026-11-17
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "cupid/gb/gb.h"
#include "cupid/gb/input.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/sgb.h"
#include "cupid/gb/sgb_startup.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CupidGb *machine_with_title(CupidGbModel model, bool declared, uint8_t license,
                                   bool firmware, const char *title) {
    uint8_t rom[0x8000] = {0};
    assert(strlen(title) <= 16);
    memcpy(rom + 0x134, title, strlen(title));
    rom[0x146] = declared ? 3 : 0;
    rom[0x14b] = license;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[address] - 1u);
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 0);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    if (firmware) {
        gb->boot_rom_size = model == CUPID_GB_MODEL_CGB ? 0x900u : 0x100u;
        memset(gb->boot_rom, 0, sizeof(gb->boot_rom));
        cupid_gb_start_boot_rom(gb);
        assert(gb->boot_rom_enabled);
        /* Isolate the zero-time wire protocol from host introduction rendering.
         * The synthetic firmware overlay stays mapped until an explicit FF50 write. */
        cupid_gb_sgb_startup_reset(gb, false);
    }
    return gb;
}

static CupidGb *machine(CupidGbModel model, bool declared, uint8_t license, bool firmware) {
    return machine_with_title(model, declared, license, firmware, "CUPID PROTOCOL");
}

static void release(CupidGb *gb) {
    cupid_gb_cleanup(gb);
    free(gb);
}

static void packet_prefix(CupidGb *gb, const uint8_t packet[16], unsigned bits) {
    assert(bits <= 128);
    cupid_gb_input_select(gb, 0x30);
    cupid_gb_input_select(gb, 0);
    cupid_gb_input_select(gb, 0x30);
    for (unsigned bit = 0; bit < bits; ++bit) {
        cupid_gb_input_select(gb, packet[bit / 8] & (1u << (bit % 8)) ? 0x10 : 0x20);
        cupid_gb_input_select(gb, 0x30);
    }
}

static void stop_packet(CupidGb *gb) {
    cupid_gb_input_select(gb, 0x20);
    cupid_gb_input_select(gb, 0x30);
}

static void send_packet(CupidGb *gb, const uint8_t packet[16]) {
    packet_prefix(gb, packet, 128);
    stop_packet(gb);
}

static void make_header(uint8_t header[CUPID_SGB_HEADER_BYTES], uint8_t flag, uint8_t license) {
    memset(header, 0, CUPID_SGB_HEADER_BYTES);
    memcpy(header + 0x30, "CUPID PROTOCOL", 14);
    header[0x42] = flag;
    header[0x47] = license;
}

static void make_section(uint8_t packet[16], const uint8_t *header, unsigned section,
                         bool bad_checksum) {
    assert(section < 8);
    memset(packet, 0, 16);
    packet[0] = (uint8_t)(0xf1u + section * 2u);
    if (section < 6)
        memcpy(packet + 2, header + section * 14u, 14);
    else
        memset(packet + 2, 0x5a, 14);
    for (unsigned byte = 2; byte < 16; ++byte)
        packet[1] = (uint8_t)(packet[1] + packet[byte]);
    if (bad_checksum)
        packet[1] ^= 0x80;
}

static void send_section(CupidGb *gb, const uint8_t *header, unsigned section, bool bad) {
    uint8_t packet[16];
    make_section(packet, header, section, bad);
    send_packet(gb, packet);
}

static void set_color(CupidGb *gb, uint16_t color) {
    const uint8_t palette[16] = {1, (uint8_t)color, (uint8_t)(color >> 8)};
    const uint8_t mask[16] = {0xb9, CUPID_SGB_MASK_COLOR0};
    send_packet(gb, palette);
    send_packet(gb, mask);
}

static void expect_color(const CupidGb *gb, uint32_t expected) {
    const size_t count = CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT;
    uint32_t *pixels = malloc(count * sizeof(*pixels));
    assert(pixels);
    cupid_gb_sgb_render_argb(gb, pixels, count);
    const unsigned left = CUPID_SGB_VIEWPORT_X;
    const unsigned top = CUPID_SGB_VIEWPORT_Y;
    assert(pixels[top * CUPID_SGB_SCREEN_WIDTH + left] == expected);
    assert(pixels[(top + 72u) * CUPID_SGB_SCREEN_WIDTH + left + 80u] == expected);
    assert(pixels[(top + 143u) * CUPID_SGB_SCREEN_WIDTH + left + 159u] == expected);
    free(pixels);
}

static void expect_empty_header(const CupidGb *gb) {
    for (unsigned index = 0; index < CUPID_SGB_HEADER_BYTES; ++index)
        assert(gb->sgb.received_header[index] == 0);
}

static void physical_receiver(CupidGbModel model, bool declared) {
    CupidGb *gb = machine(model, declared, declared ? 0x33 : 0, true);
    assert(cupid_gb_sgb_active(gb) == declared);
    assert(cupid_gb_sgb_commands_enabled(gb));
    set_color(gb, 0x7c00);
    expect_color(gb, 0xff0000ffu);
    uint8_t header[CUPID_SGB_HEADER_BYTES];
    make_header(header, 3, 0x33);
    for (unsigned section = 0; section < 6; ++section)
        send_section(gb, header, section, false);
    assert(!memcmp(gb->sgb.received_header, header, sizeof(header)));
    assert(cupid_gb_sgb_commands_enabled(gb));
    assert(cupid_gb_sgb_active(gb) == declared);
    set_color(gb, 0x03e0);
    expect_color(gb, 0xff00ff00u);
    release(gb);
}

static void title_palette(CupidGbModel model) {
    const uint16_t power_on[] = {0x67bf, 0x265b, 0x10b5, 0x2866};
    const uint16_t tetris[] = {0x4f5f, 0x630e, 0x159f, 0x3126};

    const struct {
        uint8_t flag, license, title_tail;
        bool checksum_error, select_palette;
    } cases[] = {
        {0, 1, 0, false, true},     {3, 1, 0, false, true}, {0, 0x33, 0, false, true},
        {3, 0x33, 0, false, false}, {0, 1, 0, true, false}, {0, 1, 0x20, false, false},
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        /* Firmware uses the title it received, even when the ROM title differs. */
        CupidGb *gb = machine(model, false, 1, true);
        uint8_t header[CUPID_SGB_HEADER_BYTES];
        make_header(header, cases[index].flag, cases[index].license);
        memset(header + 0x30, 0, 16);
        memcpy(header + 0x30, "TETRIS", 6);
        header[0x3f] = cases[index].title_tail;
        send_section(gb, header, 3, false);
        send_section(gb, header, 4, false);
        uint8_t packet[16];
        make_section(packet, header, 5, cases[index].checksum_error);
        packet_prefix(gb, packet, 128);
        assert(memcmp(gb->sgb.screen_palettes, power_on, sizeof(power_on)) == 0);
        gb->sgb.screen_palettes[4] = 0x001f;
        stop_packet(gb);
        const uint16_t *expected = cases[index].select_palette ? tetris : power_on;
        assert(memcmp(gb->sgb.screen_palettes, expected, sizeof(power_on)) == 0);
        assert(gb->sgb.screen_palettes[4] == 0x001f);
        release(gb);
    }

    CupidGb *gb = machine_with_title(model, false, 1, false, "TETRIS");
    assert(memcmp(gb->sgb.screen_palettes, tetris, sizeof(tetris)) == 0);
    assert(!cupid_gb_sgb_commands_enabled(gb));
    expect_empty_header(gb);
    release(gb);

    gb = machine_with_title(model, false, 1, true, "TETRIS");
    assert(memcmp(gb->sgb.screen_palettes, power_on, sizeof(power_on)) == 0);
    assert(cupid_gb_sgb_commands_enabled(gb));
    release(gb);
}

static void header_order(CupidGbModel model, bool declared) {
    const struct {
        uint8_t sections[6];
        unsigned count;
        uint8_t flag, license;
        bool accepts;
    } cases[] = {
        {{0, 1, 2, 3, 4, 5}, 6, 3, 0x33, true},
        {{4, 3, 2, 1, 0, 5}, 6, 3, 0x33, true},
        {{4, 5}, 2, 3, 0x33, true},
        {{4, 4, 5}, 3, 3, 0x33, true},
        {{0, 1, 2, 3, 4}, 5, 0, 0, true},
        {{0, 1, 2, 3, 5}, 5, 3, 0x33, false},
        {{5, 4}, 2, 3, 0x33, false},
        {{4, 5}, 2, 0, 0x33, false},
        {{4, 5}, 2, 3, 0, false},
    };

    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        CupidGb *gb = machine(model, declared, declared ? 0x33 : 0, true);
        uint8_t header[CUPID_SGB_HEADER_BYTES];
        make_header(header, cases[index].flag, cases[index].license);
        set_color(gb, 0x7c00);
        for (unsigned item = 0; item < cases[index].count; ++item)
            send_section(gb, header, cases[index].sections[item], false);
        assert(cupid_gb_sgb_commands_enabled(gb) == cases[index].accepts);
        set_color(gb, 0x001f);
        expect_color(gb, cases[index].accepts ? 0xffff0000u : 0xff0000ffu);
        release(gb);
    }
}

static void replacement_and_ignored_sections(CupidGbModel model) {
    for (unsigned valid_last = 0; valid_last < 2; ++valid_last) {
        CupidGb *gb = machine(model, false, 0, true);
        uint8_t header[CUPID_SGB_HEADER_BYTES];
        make_header(header, valid_last ? 0 : 3, 0x33);
        send_section(gb, header, 4, false);
        header[0x42] = valid_last ? 3 : 0;
        send_section(gb, header, 4, false);
        send_section(gb, header, 5, false);
        assert(cupid_gb_sgb_commands_enabled(gb) == (valid_last != 0));
        release(gb);
    }
    CupidGb *gb = machine(model, false, 0, true);
    uint8_t header[CUPID_SGB_HEADER_BYTES];
    make_header(header, 3, 0x33);
    send_section(gb, header, 6, false);
    send_section(gb, header, 7, false);
    assert(cupid_gb_sgb_commands_enabled(gb));
    expect_empty_header(gb);
    /* A checksum wraps to eight bits; the section identifier is not included. */
    memset(header, 0xff, 14);
    send_section(gb, header, 0, false);
    assert(cupid_gb_sgb_commands_enabled(gb));
    assert(!memcmp(gb->sgb.received_header, header, 14));
    send_section(gb, header, 4, false);
    send_section(gb, header, 5, false);
    set_color(gb, 0x03e0);
    expect_color(gb, 0xff00ff00u);
    release(gb);
}

static void checksum_failure_is_sticky(CupidGbModel model, bool declared) {
    uint8_t header[CUPID_SGB_HEADER_BYTES];
    make_header(header, 3, 0x33);
    for (unsigned section = 0; section < 8; ++section) {
        CupidGb *gb = machine(model, declared, 0x33, true);
        set_color(gb, 0x7c00);
        send_section(gb, header, section, true);
        assert(!cupid_gb_sgb_commands_enabled(gb));
        expect_empty_header(gb);
        for (unsigned replacement = 0; replacement < 6; ++replacement)
            send_section(gb, header, replacement, false);
        set_color(gb, 0x001f);
        expect_color(gb, 0xff0000ffu);
        assert(!cupid_gb_sgb_commands_enabled(gb));
        expect_empty_header(gb);
        release(gb);
    }
}

static void handoff_ignores_headers(CupidGbModel model, bool disabled) {
    CupidGb *gb = machine(model, false, 0, true);
    uint8_t header[CUPID_SGB_HEADER_BYTES], saved[CUPID_SGB_HEADER_BYTES];
    make_header(header, 3, 0x33);
    set_color(gb, 0x7c00);
    send_section(gb, header, 4, false);
    if (disabled)
        send_section(gb, header, 5, true);
    memcpy(saved, gb->sgb.received_header, sizeof(saved));
    cupid_gb_write_u8(gb, 0xff50, 1);
    assert(!gb->boot_rom_enabled);
    make_header(header, 0, 0);
    send_section(gb, header, 0, true);
    send_section(gb, header, 5, false);
    assert(cupid_gb_sgb_commands_enabled(gb) == !disabled);
    assert(!memcmp(saved, gb->sgb.received_header, sizeof(saved)));
    set_color(gb, 0x03e0);
    expect_color(gb, disabled ? 0xff0000ffu : 0xff00ff00u);
    release(gb);
}

static void packet_commit_edge(CupidGbModel model) {
    CupidGb *gb = machine(model, false, 0, true);
    uint8_t header[CUPID_SGB_HEADER_BYTES], packet[16];
    make_header(header, 3, 0x33);
    make_section(packet, header, 4, false);
    packet_prefix(gb, packet, 127);
    expect_empty_header(gb);
    cupid_gb_input_select(gb, 0);
    assert(!gb->sgb.command_write_index && !gb->sgb.ready_for_stop);
    send_section(gb, header, 4, false);
    assert(gb->sgb.received_header[0x42] == 3);
    make_section(packet, header, 5, false);
    packet_prefix(gb, packet, 128);
    assert(gb->sgb.received_header[0x47] == 0 && gb->sgb.ready_for_stop);
    cupid_gb_input_select(gb, 0x30);
    cupid_gb_input_select(gb, 0x20);
    assert(gb->sgb.received_header[0x47] == 0);
    cupid_gb_input_select(gb, 0x30);
    assert(gb->sgb.received_header[0x47] == 0x33);
    assert(cupid_gb_sgb_commands_enabled(gb));
    make_section(packet, header, 0, true);
    packet_prefix(gb, packet, 128);
    assert(cupid_gb_sgb_commands_enabled(gb));
    stop_packet(gb);
    assert(!cupid_gb_sgb_commands_enabled(gb));
    release(gb);
}

static void retained_player_after_disable(CupidGbModel model, bool declared) {
    CupidGb *gb = machine(model, declared, 0x33, true);
    const uint8_t masks[] = {0xef, 0xdf, 0xbf, 0x7f};
    const uint8_t selected[] = {0xde, 0xdd, 0xdb, 0xd7};
    for (unsigned player = 0; player < 4; ++player)
        assert(cupid_gb_set_buttons_for_player(gb, player, masks[player]));
    const uint8_t multiplayer[16] = {0x89, 3};
    send_packet(gb, multiplayer);
    for (unsigned turn = 0; turn < 6; ++turn) {
        assert(cupid_gb_input_read(gb) == 0xffu - (turn & 3u));
        cupid_gb_input_select(gb, 0x10);
        assert(cupid_gb_input_read(gb) == selected[turn & 3u]);
        cupid_gb_input_select(gb, 0x30);
    }
    /* Six one-bits plus the reset edge advance pad 3 to pad 2 before rejection. */
    const uint8_t invalid[16] = {0xf1, 1};
    send_packet(gb, invalid);
    assert(!cupid_gb_sgb_commands_enabled(gb));
    assert(gb->sgb.player_count == 4 && cupid_gb_input_read(gb) == 0xfe);
    for (unsigned turn = 0; turn < 8; ++turn) {
        cupid_gb_input_select(gb, 0x10);
        assert(cupid_gb_input_read(gb) == 0xdd);
        cupid_gb_input_select(gb, 0x30);
        assert(cupid_gb_input_read(gb) == 0xfe);
    }
    release(gb);
}

static void reset_header_history(CupidGbModel model, CupidGbResetKind kind) {
    CupidGb *gb = machine(model, false, 0, true);
    uint8_t header[CUPID_SGB_HEADER_BYTES];
    make_header(header, 3, 0x33);
    send_section(gb, header, 4, false);
    send_section(gb, header, 5, true);
    gb->work_ram[0x100] = 0x64;
    assert(!cupid_gb_sgb_commands_enabled(gb));
    assert(cupid_gb_reset(gb, kind));
    assert(gb->boot_rom_enabled && cupid_gb_sgb_commands_enabled(gb));
    assert(gb->sgb.player_count == 1 && gb->sgb.current_player == 0);
    expect_empty_header(gb);
    if (kind == CUPID_GB_RESET_QUICK)
        assert(gb->work_ram[0x100] == 0x64);
    /* The final section cannot borrow the enhancement byte from the old session. */
    send_section(gb, header, 5, false);
    assert(!cupid_gb_sgb_commands_enabled(gb));
    release(gb);
}

static void explicit_boot_reentry(CupidGbModel model) {
    CupidGb *gb = machine(model, false, 0, true);
    uint8_t header[CUPID_SGB_HEADER_BYTES], packet[16];
    make_header(header, 3, 0x33);
    send_section(gb, header, 4, false);
    set_color(gb, 0x7c00);
    const uint8_t transfer[16] = {0x59};
    send_packet(gb, transfer);
    gb->sgb.border_tiles[100] = 0xa5;
    gb->sgb.attribute_map[19] = 2;
    make_section(packet, header, 5, false);
    packet_prefix(gb, packet, 128);
    assert(gb->sgb.ready_for_stop);
    cupid_gb_enter_boot_rom(gb);
    assert(cupid_gb_sgb_commands_enabled(gb));
    expect_empty_header(gb);
    assert(!gb->sgb.command_write_index && !gb->sgb.ready_for_write && !gb->sgb.ready_for_stop &&
           !gb->sgb.ready_for_pulse);
    assert(gb->sgb.player_count == 1 && gb->sgb.current_player == 0);
    assert(gb->sgb.border_tiles[100] == 0xa5 && gb->sgb.attribute_map[19] == 2);
    assert(gb->sgb.transfer.destination == CUPID_SGB_TRANSFER_PALETTES &&
           gb->sgb.transfer.frames_remaining == 3);
    assert(gb->sgb.mask_mode == CUPID_SGB_MASK_COLOR0 && gb->sgb.screen_palettes[0] == 0x7c00);
    send_section(gb, header, 0, true);
    assert(!cupid_gb_sgb_commands_enabled(gb));
    cupid_gb_enter_boot_rom(gb);
    assert(cupid_gb_sgb_commands_enabled(gb));
    cupid_gb_sgb_startup_reset(gb, false);
    expect_color(gb, 0xff0000ffu);
    release(gb);
}

static void approximate_handoff(CupidGbModel model) {
    for (unsigned declared = 0; declared < 2; ++declared)
        for (unsigned license = 0; license < 2; ++license) {
            CupidGb *gb = machine(model, declared != 0, license ? 0x33 : 0, false);
            const bool accepts = declared && license;
            assert(cupid_gb_sgb_commands_enabled(gb) == accepts);
            assert(cupid_gb_sgb_active(gb) == (declared != 0));
            expect_empty_header(gb);
            set_color(gb, 0x7c00);
            expect_color(gb, accepts ? 0xff0000ffu : 0xffffefceu);
            release(gb);
        }
}

static void handheld_cannot_receive(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG0, CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB,
                                   CUPID_GB_MODEL_CGB};
    for (unsigned index = 0; index < sizeof(models) / sizeof(models[0]); ++index) {
        CupidGb *gb = machine(models[index], true, 0x33, true);
        assert(!cupid_gb_sgb_active(gb) && !cupid_gb_sgb_commands_enabled(gb));
        /* Stale declaration metadata cannot create a physical receiver. */
        gb->sgb.enabled = true;
        uint8_t header[CUPID_SGB_HEADER_BYTES];
        make_header(header, 3, 0x33);
        send_section(gb, header, 4, true);
        set_color(gb, 0x001f);
        expect_empty_header(gb);
        assert(!gb->sgb.commands_disabled && !gb->sgb.command_write_index);
        assert(gb->sgb.screen_palettes[0] == 0x7fff);
        release(gb);
    }
}

int main(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2, CUPID_GB_MODEL_SGB_PAL};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned declared = 0; declared < 2; ++declared) {
            physical_receiver(models[model], declared != 0);
            header_order(models[model], declared != 0);
            checksum_failure_is_sticky(models[model], declared != 0);
            retained_player_after_disable(models[model], declared != 0);
        }
        replacement_and_ignored_sections(models[model]);
        title_palette(models[model]);
        handoff_ignores_headers(models[model], false);
        handoff_ignores_headers(models[model], true);
        packet_commit_edge(models[model]);
        reset_header_history(models[model], CUPID_GB_RESET_HARD);
        reset_header_history(models[model], CUPID_GB_RESET_QUICK);
        explicit_boot_reentry(models[model]);
        approximate_handoff(models[model]);
    }
    handheld_cannot_receive();
    cupid_gb_sgb_begin_boot(NULL);
    assert(!cupid_gb_sgb_commands_enabled(NULL));
    puts("SGB boot header, receiver and controller regressions passed");
    return 0;
}

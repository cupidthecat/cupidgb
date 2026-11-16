/**
 * @file    test_camera.c
 * @brief   Regression tests for Camera cartridge banking and capture behavior.
 * @author  cupidthecat
 * @date    2026-11-16
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "cupid/gb/battery.h"
#include "cupid/gb/camera.h"
#include "cupid/gb/debugger.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/state.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void load_camera(CupidGb *gb, CupidGbModel model) {
    enum { ROM_SIZE = 8 * 0x4000 };

    uint8_t *rom = malloc(ROM_SIZE);
    assert(rom);
    for (unsigned bank = 0; bank < 8; ++bank)
        memset(rom + bank * 0x4000, (int)bank, 0x4000);
    rom[0x147] = 0xfc;
    rom[0x148] = 2;
    rom[0x149] = 4;
    uint8_t checksum = 0;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        checksum = (uint8_t)(checksum - rom[address] - 1u);
    rom[0x14d] = checksum;
    cupid_gb_set_model(gb, model);
    assert(cupid_gb_load_rom(gb, rom, ROM_SIZE));
    free(rom);
    assert(gb->header.mbc_type == CUPID_GB_CAMERA && gb->header.has_battery);
    assert(cupid_gb_battery_size(gb) == 0x20000);
}

static void test_banking(CupidGbModel model) {
    CupidGb gb = {0};
    load_camera(&gb, model);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 1);
    cupid_gb_write_u8(&gb, 0x3fff, 0);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 0);
    cupid_gb_write_u8(&gb, 0x2000, 0x86);
    assert(cupid_gb_read_u8(&gb, 0x4000) == 6);
    assert(cupid_gb_read_u8(&gb, 0x0000) == 0);

    cupid_gb_write_u8(&gb, 0x1fff, 0x1a);
    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x4000, (uint8_t)bank);
        assert(cupid_gb_read_u8(&gb, 0xa020) == 0xff);
        cupid_gb_write_u8(&gb, 0xa020, (uint8_t)(bank + 0x80u));
    }
    cupid_gb_write_u8(&gb, 0x0000, 0);
    for (unsigned bank = 0; bank < 16; ++bank) {
        cupid_gb_write_u8(&gb, 0x5fff, (uint8_t)(bank | 0x80u));
        assert(cupid_gb_read_u8(&gb, 0xa020) == bank + 0x80u);
        cupid_gb_write_u8(&gb, 0xa020, 0xaa);
        assert(cupid_gb_read_u8(&gb, 0xa020) == bank + 0x80u);
    }
    cupid_gb_cleanup(&gb);
}

static void test_capture(CupidGbModel model) {
    CupidGb gb = {0};
    load_camera(&gb, model);
    cupid_gb_write_u8(&gb, 0x0000, 0x0a);
    cupid_gb_write_u8(&gb, 0x4000, 2);
    cupid_gb_write_u8(&gb, 0xa020, 0x51);
    cupid_gb_write_u8(&gb, 0x0000, 0);
    cupid_gb_write_u8(&gb, 0x4000, 0x10);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 0);
    cupid_gb_write_u8(&gb, 0xa001, 4);
    cupid_gb_write_u8(&gb, 0xa002, 1);
    cupid_gb_write_u8(&gb, 0xa003, 0);
    assert(cupid_gb_read_u8(&gb, 0xa001) == 0);
    for (unsigned cell = 0; cell < 16; ++cell) {
        uint16_t address = (uint16_t)(0xa006u + cell * 3u);
        cupid_gb_write_u8(&gb, address, 2);
        cupid_gb_write_u8(&gb, (uint16_t)(address + 1u), 4);
        cupid_gb_write_u8(&gb, (uint16_t)(address + 2u), 6);
    }

    cupid_gb_tick(&gb, 1);
    cupid_gb_write_u8(&gb, 0xa080, 0xff);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 7);
    assert(cupid_gb_debug_peek(&gb, 0xa000) == 7);
    assert(!cupid_gb_debug_poke(&gb, 0xa000, 0));
    cupid_gb_write_u8(&gb, 0xb000, 6);
    assert(cupid_gb_read_u8(&gb, 0xa080) == 7);
    gb.cpu.halted = true;
    cupid_gb_tick(&gb, 40000);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 7);
    gb.cpu.halted = false;

    cupid_gb_write_u8(&gb, 0x0000, 0x0a);
    cupid_gb_write_u8(&gb, 0x4000, 2);
    assert(cupid_gb_read_u8(&gb, 0xa020) == 0);
    cupid_gb_write_u8(&gb, 0xa020, 0x99);
    const size_t size = cupid_gb_state_size(&gb);
    uint8_t *before = malloc(size), *after = malloc(size), *replayed = malloc(size);
    assert(before && after && replayed);
    assert(cupid_gb_save_state(&gb, before, size, NULL) == CUPID_GB_STATE_OK &&
           before[8] == CUPID_GB_STATE_VERSION);
    /* The first machine cycle leaves PHI in its longer shutter phase. */
    cupid_gb_tick(&gb, 37056);
    assert(cupid_gb_read_u8(&gb, 0xa020) == 0);
    cupid_gb_tick(&gb, 1);
    assert(cupid_gb_read_u8(&gb, 0xa020) == 0x51);
    cupid_gb_write_u8(&gb, 0x4000, 0x10);
    assert(cupid_gb_read_u8(&gb, 0xa000) == 6);
    cupid_gb_write_u8(&gb, 0x4000, 0);
    /* Eight neighboring pixels exercise all four 2-bit output levels. */
    assert(cupid_gb_read_u8(&gb, 0xa100) == 0x9c);
    assert(cupid_gb_debug_peek(&gb, 0xa100) == 0x9c);
    assert(cupid_gb_read_u8(&gb, 0xa101) == 0xe0);
    assert(cupid_gb_read_u8(&gb, 0xaf00) == 0xff);
    assert(cupid_gb_save_state(&gb, after, size, NULL) == CUPID_GB_STATE_OK);

    assert(cupid_gb_load_state(&gb, before, size) == CUPID_GB_STATE_OK);
    assert(cupid_gb_read_u8(&gb, 0xa020) == 0);
    cupid_gb_tick(&gb, 37057);
    cupid_gb_write_u8(&gb, 0x4000, 0);
    assert(cupid_gb_read_u8(&gb, 0xa100) == 0x9c);
    assert(cupid_gb_save_state(&gb, replayed, size, NULL) == CUPID_GB_STATE_OK);
    assert(memcmp(after, replayed, size) == 0);
    free(replayed);
    free(after);
    free(before);
    cupid_gb_cleanup(&gb);
}

static void set_sensor_case(CupidGbCamera *camera, unsigned gain, unsigned edge, unsigned exposure,
                            uint32_t capture, unsigned palette) {
    memset(camera, 0, sizeof(*camera));
    camera->capture_number = capture;
    camera->registers[1] = (uint8_t)(gain | (edge ? 0xe0u : 0u));
    camera->registers[2] = (uint8_t)(exposure >> 8);
    camera->registers[3] = (uint8_t)exposure;
    camera->registers[4] = (uint8_t)((edge ? edge - 1u : 0u) << 4);
    for (unsigned cell = 0; cell < 16; ++cell) {
        for (unsigned shade = 0; shade < 3; ++shade) {
            unsigned value;
            if (!palette)
                value = 31u + 73u * shade + cell * 3u;
            else if (palette == 1)
                value = (cell & 1u) ? 253u + shade : 1u + shade;
            else
                value = (255u - 79u * shade + cell * 11u) & 255u;
            camera->registers[6 + cell * 3 + shade] = (uint8_t)value;
        }
    }
}

static uint32_t digest_tile(uint32_t digest, const CupidGbCamera *camera, unsigned offset) {
    return (digest ^ cupid_gb_camera_read_tile(camera, offset)) * 16777619u;
}

static void test_sensor_processing(void) {
    /* Recorded image digests cover all 32 gains, disabled edge enhancement and
     * all eight edge strengths. Every full image includes both tile bitplanes. */
    static const uint32_t image_digests[32][9] = {
        {0xd12c5277u, 0x4f28a403u, 0x17115e60u, 0x8ad2795fu, 0xf15ba7cdu, 0x2b743a31u, 0x8d65d325u,
         0x46603236u, 0x8aef3060u},
        {0x5b44b2e9u, 0x3bc28cc2u, 0x10f11798u, 0xdfd142b3u, 0xa8af9151u, 0x863bedf0u, 0xe4ae5437u,
         0x9ddb4c20u, 0xdaf92394u},
        {0x488958e3u, 0xf593ad85u, 0xf07c88ecu, 0x1bbd9c8eu, 0x7f851428u, 0x08c961efu, 0x7f122247u,
         0xe95cf72eu, 0x91f65028u},
        {0x59fcfa70u, 0x3424dadcu, 0xb7556f50u, 0x0fdfca6bu, 0xc38320fbu, 0x7388fc97u, 0x118dcc95u,
         0x4e3367e6u, 0x24f2a857u},
        {0xe185a6d9u, 0xb5710eb1u, 0x92c42115u, 0xcfa864adu, 0xece4fc44u, 0xb87f0962u, 0xc95ea6c3u,
         0x8c789f6cu, 0x1f88d393u},
        {0x730a4727u, 0x83b4b52au, 0xcc9a312au, 0x92f65304u, 0xcffc7bbfu, 0x3fba679eu, 0xd2b0b8f2u,
         0x04b8fa32u, 0xd08f60f2u},
        {0x7a02f2ebu, 0xea5e3ad9u, 0x48b9ead2u, 0xd9e96515u, 0x57247841u, 0xeca91d98u, 0xe01a3719u,
         0xefbca3eau, 0x6b015fb5u},
        {0xd9ca3fabu, 0xfdf067ccu, 0x6d8c7ed2u, 0x219f305eu, 0x554d5721u, 0xbc590040u, 0x53ab6cefu,
         0x53f49366u, 0x2e223697u},
        {0x5d6bc35cu, 0xd407aa81u, 0x99fe7a9bu, 0xdc18df6du, 0x87167d1bu, 0xdc0b5627u, 0x012ef5f3u,
         0x64c87dfdu, 0x1a4fd35au},
        {0x30fd6ebbu, 0x952a441fu, 0x5440ffe0u, 0x5a85fae4u, 0xf4049e0eu, 0xe19fcd42u, 0x62817183u,
         0x944a858cu, 0x4da6a07eu},
        {0x2cfc01b0u, 0xc970cf4eu, 0x56f22f2cu, 0x36ee0bc3u, 0x4e1e9076u, 0x1dc5fa5eu, 0x1ecc664au,
         0x744fde64u, 0x2e65a2b4u},
        {0x002bb33fu, 0x01bd40ecu, 0xc6b17a95u, 0x16ce0ac2u, 0x924326afu, 0xed18c8b5u, 0xaaf3c67du,
         0x5c2dc4bfu, 0xc7e965ccu},
        {0x975fc945u, 0x29deb60fu, 0x91b94235u, 0xdd611f5du, 0xc9a7af29u, 0x3a091a02u, 0x9ef465d2u,
         0x84f7b0fdu, 0x71695a1du},
        {0x3728a72du, 0xeb87b5cau, 0x88f5e972u, 0x6e8367f9u, 0x504c11fdu, 0x1d855bc9u, 0x5cdd2e91u,
         0xaf540554u, 0x81197eafu},
        {0x1613e731u, 0x8c30d850u, 0x596240ccu, 0x1e1d9325u, 0x24f3a984u, 0x53d5b9dbu, 0x681aa9c6u,
         0x27c0ec7bu, 0x07e3a115u},
        {0x7db62f49u, 0x1634895fu, 0x9c78731fu, 0x0b5e7d5cu, 0x14611233u, 0xb7851841u, 0xf933016au,
         0xfaff80a6u, 0x4cbad240u},
        {0xd7e96b78u, 0xc410e619u, 0xf836f54au, 0x13e4b515u, 0x41b07c6bu, 0x27aa69b2u, 0xee4136edu,
         0x077bf213u, 0xd4ee31ccu},
        {0x4b556b47u, 0x2ae6c5f4u, 0xc67ab567u, 0x7e955456u, 0x98f88864u, 0xe2bdb4d0u, 0x392c4d7cu,
         0x3476d0f6u, 0xc576cfd7u},
        {0xece64d1bu, 0x1dd01e4eu, 0xf7bcbbcau, 0x38549845u, 0x583a3123u, 0xf04bec8cu, 0xfdc021dcu,
         0x10569d97u, 0x297bf09eu},
        {0xd76752f3u, 0xa17e2cd8u, 0xf36e3a2du, 0x44e4b1a7u, 0x5c414175u, 0xe37e8c5bu, 0xdeef2d7cu,
         0x0826c979u, 0xf3e674bdu},
        {0xcbc759ceu, 0xad348900u, 0xc23fea53u, 0x3ef216d1u, 0x80bc79adu, 0xdf3efd39u, 0xb52a5430u,
         0x906c3d81u, 0xb0541626u},
        {0xb58830f8u, 0x208ff856u, 0xf0d21964u, 0x4ec622adu, 0xb1f95345u, 0x490cb74du, 0xa8c229aau,
         0x8c6c1372u, 0x2724d536u},
        {0x7c7699eau, 0x5fc8665bu, 0xa35d3329u, 0x3b3ab3e2u, 0x2e73d8d2u, 0x000ad0a5u, 0xc079576cu,
         0x602a3996u, 0x98c0d91fu},
        {0x5df9bba7u, 0x79e8bcf6u, 0x915491deu, 0x4a6db56au, 0x5842d57bu, 0xe127eb4fu, 0xef58279du,
         0xb74ca232u, 0xf92d3694u},
        {0x08d6a81bu, 0xc1549a3fu, 0x52ed6212u, 0xe5abfcd2u, 0xbef15114u, 0xfb5da3ffu, 0x81bdb87bu,
         0xed234cd0u, 0xb30b5200u},
        {0x46264a86u, 0x5cea1e98u, 0x07b76fd3u, 0x9e88eb27u, 0xbc18cac9u, 0x9645b94cu, 0x043688f7u,
         0x0a98191bu, 0x56cf165du},
        {0x1cd867beu, 0xd6b682cdu, 0x162fda23u, 0x6dbe7915u, 0xf5f44e37u, 0x0a3a4cc8u, 0x99faeadeu,
         0x26968f96u, 0x1fbad9cdu},
        {0x60a8c3cfu, 0xb2a72dd5u, 0x3d9bf857u, 0x1b770d5bu, 0x3192e7bcu, 0xa90214ffu, 0x4cc73a9cu,
         0x2e029010u, 0xa05cab05u},
        {0x9432a33bu, 0xcbd4f3e7u, 0x15eb52acu, 0x67f8cb45u, 0x9fbffb54u, 0xa660b7beu, 0x5efca634u,
         0xc4ae01acu, 0xdad80addu},
        {0x9da6593eu, 0xbe59153au, 0xfbe58b88u, 0xacac51c1u, 0x51cd63bfu, 0x51aa96feu, 0x786607a9u,
         0x5743f201u, 0xaaafe678u},
        {0x5e8d5561u, 0x157fa8f4u, 0xe17a65acu, 0x7acfac5cu, 0x6e0ed9dfu, 0x2e6edc2au, 0x83d00fbcu,
         0x9d2d5befu, 0x0c7324a1u},
        {0xc7dc93c3u, 0xbd79f206u, 0x44fa4917u, 0xb8c1a954u, 0x21d38adfu, 0x5fc9cb6au, 0x037fd9edu,
         0xc8e59c72u, 0xfa94818du}};
    /* The second matrix isolates fractional exposure, extreme thresholds,
     * capture-counter wraparound and clamped sensor neighbors at each border. */
    static const uint32_t edge_digests[32][9] = {
        {0xe01f5310u, 0x5cf058ecu, 0xba4f5d77u, 0xa448cf60u, 0x4177ead9u, 0xa74de912u, 0x8d1ba742u,
         0x9bbeeca9u, 0x8ed224b3u},
        {0x1d0dcc16u, 0xd560a5b5u, 0x40067293u, 0x96110fdfu, 0x9ef76ae2u, 0x57ca9401u, 0x3049cd15u,
         0x88426fd8u, 0xc3bdb4b9u},
        {0x948d2aefu, 0x3d5a2423u, 0x6e97928au, 0x7fa4ebd0u, 0x983972b8u, 0x767e1997u, 0x99667da6u,
         0xb7e1c990u, 0x0be083f8u},
        {0xac87e260u, 0xa2ad7a34u, 0x2fa27fecu, 0x00f774eau, 0x47783036u, 0x3a5c624du, 0x54a567a7u,
         0xddffdb5cu, 0xdb245805u},
        {0x1b6ddea9u, 0xf04ac2b3u, 0x5eec8622u, 0x7d6cf7ebu, 0x0dda51deu, 0xf26e8d32u, 0x0ae54a8eu,
         0x2628b10cu, 0x06d360bbu},
        {0x59868f64u, 0xf8de78bcu, 0x3f55857du, 0xe571b45fu, 0xfa798f6du, 0x639a12d2u, 0x55143f6eu,
         0xf644a728u, 0xef56ec62u},
        {0x2e5a36ebu, 0x6dd8e4c8u, 0x3fce4a83u, 0xe447500cu, 0x7988abf3u, 0x72f3f6f7u, 0x6e1df18cu,
         0xba3a515du, 0x5dcd246eu},
        {0xa6cdabd8u, 0x7ffa8f9du, 0x99fd8a0bu, 0x2d12bcb0u, 0xf1452632u, 0x3ea7b657u, 0x0b60e3e5u,
         0xd78a1ffau, 0xa42d853bu},
        {0x571e1829u, 0x13c357feu, 0x6c5181a2u, 0xe4902ee4u, 0x81399564u, 0xa5613747u, 0x6896132cu,
         0xce9e3d5eu, 0x0f989dd1u},
        {0x52e05950u, 0xaeb52ccfu, 0x74e84432u, 0x505c29dau, 0x546fd18du, 0x4e1ba953u, 0x48a0216bu,
         0x609ab906u, 0x0f776189u},
        {0xccbcbd4au, 0x79e3205du, 0x0393efa4u, 0xdf339b1du, 0xfca87670u, 0x701455d6u, 0x347b29ceu,
         0xd493d50au, 0x42a47831u},
        {0xcfcd4da6u, 0xa636647fu, 0x2d2d0630u, 0xbbe0e8d0u, 0x014184c7u, 0x4f4cd176u, 0x9af9c0eeu,
         0x97a2aad7u, 0xef7d4c64u},
        {0x53be3e13u, 0x94432fe5u, 0xb501cb33u, 0xf7b7a7c2u, 0xa98c4b93u, 0x6c3c14feu, 0x8a9f86bbu,
         0x270aebc1u, 0xff3665eeu},
        {0x3e90c1a1u, 0xb9a15cf1u, 0xeefc3039u, 0x9c5d9eebu, 0x5f22eaeeu, 0xc0c50d41u, 0xe68d5233u,
         0xf553ba2au, 0xce5bfe36u},
        {0x60aa427du, 0x7239240eu, 0x9ede31e7u, 0xb1c77cf1u, 0x39e7e1b5u, 0xcd83a216u, 0x0a0479fau,
         0x860a6c33u, 0x801bb6f1u},
        {0x49f3534eu, 0x83d5be60u, 0x515d225au, 0x3db3778fu, 0xfdba602eu, 0xc79f907cu, 0xfa566191u,
         0x6ae856ffu, 0x03911af5u},
        {0x72252abeu, 0x06c23a34u, 0xd60139f2u, 0x57964e9eu, 0x1edccb0fu, 0x6bd5bc0du, 0xc5fe29dcu,
         0x50d10d51u, 0x6094b5d5u},
        {0xd4ae0cb2u, 0xd5f3e063u, 0xc4ff27eeu, 0x2c743c36u, 0x2ce261bau, 0x76f28711u, 0x81cacc4eu,
         0x4c68ffbeu, 0xfc11a906u},
        {0x09ee6bd1u, 0xa8544008u, 0x042444ecu, 0xc2409959u, 0x1d6b1595u, 0x1cb39bafu, 0xb794e57fu,
         0xdfc394fcu, 0x98a8ccdfu},
        {0xa3455906u, 0x97776b01u, 0xeb357277u, 0x790b52e2u, 0x099395bdu, 0x32699787u, 0xf444c111u,
         0x8ca9ae22u, 0x34df9f61u},
        {0x54f4cb90u, 0xfc702fb5u, 0xd51ee571u, 0x5d65994au, 0xa7598530u, 0x8091014du, 0xb31533d7u,
         0x88199405u, 0xfd4830d9u},
        {0xed9d0bc1u, 0x4f20a898u, 0xdc7df7dfu, 0x8eb5a84au, 0x8eb08598u, 0xf1102ed9u, 0x71c434e7u,
         0x8ebbc3d6u, 0x9a7c3c47u},
        {0xc54d6674u, 0xd74be5b6u, 0xeaa69e59u, 0xe9e3f04cu, 0x1a2619abu, 0x65554962u, 0x7ee16f6bu,
         0x577ba619u, 0x3617ecb6u},
        {0x8fee4e5du, 0xfb53a9a8u, 0x6afb0588u, 0x178967a2u, 0x1d0f3499u, 0x0519e4fau, 0xa4164fecu,
         0x49db3448u, 0x7dd3b792u},
        {0x755a621fu, 0x90f5f845u, 0x0c2cd578u, 0xe32e3b5cu, 0x2c2a09a2u, 0x1b0ea74du, 0xcc876d1du,
         0x40553d8au, 0x3c56593bu},
        {0x9b850475u, 0x3e669ae7u, 0xce083216u, 0x7e4636c6u, 0x9ecc282fu, 0x294d6e6fu, 0x080b37a2u,
         0x32d81e9eu, 0x8aab035bu},
        {0x704cdc5fu, 0x3543b513u, 0x7565d686u, 0x8b7914b9u, 0x8e3c4036u, 0xa962c1b0u, 0x187e918au,
         0xd4b8fe3fu, 0x97ca5458u},
        {0xd55977d1u, 0x7811520au, 0xdb7ffcdfu, 0x15b86b37u, 0x04c834dbu, 0x3b670accu, 0x57bf890au,
         0x5320c79fu, 0x67a15afeu},
        {0x7dc64949u, 0xe1726b21u, 0xf99f0586u, 0x18d7151au, 0xe4937a1eu, 0x9a017342u, 0x9c4c263fu,
         0x508f296cu, 0x3e2f6290u},
        {0xaa65de54u, 0x3585dfa6u, 0x16e3fb28u, 0x224376adu, 0x6c0e104cu, 0x9e0f3500u, 0xba7b8732u,
         0x16b6fc1bu, 0x8ea14fa4u},
        {0x86dc0427u, 0xcc71f951u, 0x1a36b401u, 0x96962132u, 0x1a182966u, 0x0a6de81bu, 0x78fdfaebu,
         0x9429e7a8u, 0xe5299774u},
        {0x0e9be0f4u, 0xd131970du, 0x6d99368fu, 0x79719a29u, 0x260064c8u, 0x5f497329u, 0x4dd5d871u,
         0xdc650567u, 0x094c051du}};
    static const unsigned exposures[] = {0, 1, 255, 256, 4095, 4097, 8192, 65535};
    static const uint32_t captures[] = {0, 2, 11, UINT32_MAX};
    static const unsigned offsets[] = {0x000, 0x001, 0x00e, 0x00f, 0x010, 0x011, 0x0e0, 0x0e1,
                                       0x0f0, 0x0f1, 0x0fe, 0x0ff, 0x100, 0x101, 0x6e6, 0x6e7,
                                       0x706, 0x707, 0x7f0, 0x7f1, 0xcfe, 0xcff, 0xd00, 0xd01,
                                       0xd0e, 0xd0f, 0xde0, 0xde1, 0xdf0, 0xdf1, 0xdfe, 0xdff};
    CupidGbCamera camera;
    for (unsigned gain = 0; gain < 32; ++gain) {
        for (unsigned edge = 0; edge < 9; ++edge) {
            set_sensor_case(&camera, gain, edge, 4096, 1, 0);
            uint32_t image_digest = 2166136261u;
            for (unsigned offset = 0; offset < 0xe00; ++offset)
                image_digest = digest_tile(image_digest, &camera, offset);
            assert(image_digest == image_digests[gain][edge]);

            uint32_t edge_digest = 2166136261u;
            for (unsigned e = 0; e < sizeof(exposures) / sizeof(exposures[0]); ++e) {
                for (unsigned c = 0; c < sizeof(captures) / sizeof(captures[0]); ++c) {
                    for (unsigned palette = 0; palette < 3; ++palette) {
                        set_sensor_case(&camera, gain, edge, exposures[e], captures[c], palette);
                        for (unsigned o = 0; o < sizeof(offsets) / sizeof(offsets[0]); ++o)
                            edge_digest = digest_tile(edge_digest, &camera, offsets[o]);
                    }
                }
            }
            assert(edge_digest == edge_digests[gain][edge]);
        }
    }
}

int main(void) {
    test_banking(CUPID_GB_MODEL_DMG_ABC);
    test_banking(CUPID_GB_MODEL_CGB);
    test_capture(CUPID_GB_MODEL_DMG_ABC);
    test_capture(CUPID_GB_MODEL_CGB);
    test_capture(CUPID_GB_MODEL_SGB);
    test_sensor_processing();
    puts("Camera banking, capture, and state regressions passed.");
    return 0;
}

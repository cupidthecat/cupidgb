/**
 * @file    cgb_palette.c
 * @brief   Color palette ports and monochrome-cartridge compatibility palettes.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Compatibility colors and CPU palette ports share the same physical palette RAM.
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

#include "cupid/gbc/cgb.h"

#include "../gb/audio_output.h"
#include "../gb/startup_internal.h"
#include "cgb_internal.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"

#include <string.h>

typedef struct CupidCgbCompatPaletteSet {
    uint8_t boot_palette_periods;    /* Palette-specific handoff time in 360-T periods. */
    uint8_t boot_compatibility_slot; /* Palette slot traversed in monochrome setup. */
    uint16_t bg[4];
    uint16_t obj0[4];
    uint16_t obj1[4];
} CupidCgbCompatPaletteSet;

typedef struct CupidCgbCompatPaletteKey {
    uint8_t title_checksum;
    uint8_t fourth_letter;
    bool require_fourth_letter;
    uint16_t compatibility_tilemap_cycles;
    const CupidCgbCompatPaletteSet *palette_set;
} CupidCgbCompatPaletteKey;

#define CUPID_CGB_RGB8(r, g, b)                                                                    \
    (uint16_t)((((uint16_t)((b) >> 3u)) << 10u) | (((uint16_t)((g) >> 3u)) << 5u) |                \
               ((uint16_t)((r) >> 3u)))

static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_00 = {
    23u,
    28u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x63, 0xc5), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_02 = {
    27u,
    6u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_03 = {
    27u,
    7u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_default_palette = {
    0u,
    28u,
    {0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u},
    {0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u},
    {0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u}};
static const CupidCgbCompatPaletteSet *const cupid_cgb_compat_default =
    &cupid_cgb_compat_default_palette;
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_04 = {
    27u,
    8u,
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_05 = {
    27u,
    18u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_06 = {
    27u,
    19u,
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x84, 0x84),
     CUPID_CGB_RGB8(0xff, 0xde, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x84, 0x84),
     CUPID_CGB_RGB8(0xff, 0xde, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x84, 0x84),
     CUPID_CGB_RGB8(0xff, 0xde, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_07 = {
    27u,
    22u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xa5, 0xa5, 0xa5),
     CUPID_CGB_RGB8(0x52, 0x52, 0x52), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xa5, 0xa5, 0xa5),
     CUPID_CGB_RGB8(0x52, 0x52, 0x52), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xa5, 0xa5, 0xa5),
     CUPID_CGB_RGB8(0x52, 0x52, 0x52), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_09 = {
    27u,
    27u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xce, 0x00),
     CUPID_CGB_RGB8(0x9c, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xce, 0x00),
     CUPID_CGB_RGB8(0x9c, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xce, 0x00),
     CUPID_CGB_RGB8(0x9c, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_10 = {
    25u,
    0u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_11 = {
    25u,
    11u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_12 = {
    25u,
    13u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_13 = {
    25u,
    16u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_14 = {
    25u,
    28u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x63, 0xc5), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x63, 0xc5), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_15 = {
    25u,
    11u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_16 = {
    25u,
    12u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_17 = {
    23u,
    0u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_18 = {
    23u,
    4u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x00),
     CUPID_CGB_RGB8(0xb5, 0x73, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_19 = {
    23u,
    5u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x52, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_20 = {
    23u,
    6u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_21 = {
    23u,
    8u,
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0x63, 0x52), CUPID_CGB_RGB8(0xd6, 0x00, 0x00),
     CUPID_CGB_RGB8(0x63, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0x63, 0x52), CUPID_CGB_RGB8(0xd6, 0x00, 0x00),
     CUPID_CGB_RGB8(0x63, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_22 = {
    23u,
    10u,
    {CUPID_CGB_RGB8(0xb5, 0xb5, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x94),
     CUPID_CGB_RGB8(0xad, 0x5a, 0x42), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0xff, 0x84, 0x84), CUPID_CGB_RGB8(0x94, 0x3a, 0x3a)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0xff, 0x84, 0x84), CUPID_CGB_RGB8(0x94, 0x3a, 0x3a)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_23 = {
    23u,
    12u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_24 = {
    23u,
    13u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_25 = {
    23u,
    14u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_26 = {
    23u,
    15u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_27 = {
    23u,
    18u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_29 = {
    30u,
    5u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x52, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x52, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_30 = {
    30u,
    6u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_31 = {
    30u,
    7u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_32 = {
    28u,
    0u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_33 = {
    28u,
    1u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0x9c), CUPID_CGB_RGB8(0x94, 0xb5, 0xff),
     CUPID_CGB_RGB8(0x63, 0x94, 0x73), CUPID_CGB_RGB8(0x00, 0x3a, 0x3a)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_34 = {
    28u,
    2u,
    {CUPID_CGB_RGB8(0x6b, 0xff, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0xff, 0x52, 0x4a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0x63, 0xa5, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0xff)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_35 = {
    28u,
    3u,
    {CUPID_CGB_RGB8(0x52, 0xde, 0x00), CUPID_CGB_RGB8(0xff, 0x84, 0x00),
     CUPID_CGB_RGB8(0xff, 0xff, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0x63, 0xa5, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0xff)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_36 = {
    28u,
    8u,
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0x63, 0x52), CUPID_CGB_RGB8(0xd6, 0x00, 0x00),
     CUPID_CGB_RGB8(0x63, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0xff, 0xff, 0x7b), CUPID_CGB_RGB8(0x00, 0x84, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_37 = {
    28u,
    9u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xce), CUPID_CGB_RGB8(0x63, 0xef, 0xef),
     CUPID_CGB_RGB8(0x9c, 0x84, 0x31), CUPID_CGB_RGB8(0x5a, 0x5a, 0x5a)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_38 = {
    28u,
    11u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x7b),
     CUPID_CGB_RGB8(0x00, 0x84, 0xff), CUPID_CGB_RGB8(0xff, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_39 = {
    28u,
    12u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_40 = {
    28u,
    13u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_41 = {
    28u,
    14u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_42 = {
    28u,
    15u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_44 = {
    28u,
    17u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x00, 0xff, 0x00),
     CUPID_CGB_RGB8(0x31, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x4a, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_45 = {
    28u,
    18u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_46 = {
    28u,
    20u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0x00), CUPID_CGB_RGB8(0xff, 0x00, 0x00),
     CUPID_CGB_RGB8(0x63, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_47 = {
    28u,
    21u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_50 = {
    28u,
    28u,
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x63, 0xc5), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};

/* The handoff clock uses the position of each key in the palette search. */
static const CupidCgbCompatPaletteKey cupid_cgb_compat_palette_keys[] = {
    {0x00u, 0x00u, false, 0u, &cupid_cgb_compat_auto_00},
    {0x88u, 0x00u, false, 0u, &cupid_cgb_compat_auto_04},
    {0x16u, 0x00u, false, 0u, &cupid_cgb_compat_auto_05},
    {0x36u, 0x00u, false, 0u, &cupid_cgb_compat_auto_35},
    {0xd1u, 0x00u, false, 0u, &cupid_cgb_compat_auto_34},
    {0xdbu, 0x00u, false, 0u, &cupid_cgb_compat_auto_03},
    {0xf2u, 0x00u, false, 0u, &cupid_cgb_compat_auto_31},
    {0x3cu, 0x00u, false, 0u, &cupid_cgb_compat_auto_15},
    {0x8cu, 0x00u, false, 0u, &cupid_cgb_compat_auto_10},
    {0x92u, 0x00u, false, 0u, &cupid_cgb_compat_auto_05},
    {0x3du, 0x00u, false, 0u, &cupid_cgb_compat_auto_19},
    {0x5cu, 0x00u, false, 0u, &cupid_cgb_compat_auto_36},
    {0x58u, 0x00u, false, 1400u, &cupid_cgb_compat_auto_07},
    {0xc9u, 0x00u, false, 0u, &cupid_cgb_compat_auto_37},
    {0x3eu, 0x00u, false, 0u, &cupid_cgb_compat_auto_30},
    {0x70u, 0x00u, false, 0u, &cupid_cgb_compat_auto_44},
    {0x1du, 0x00u, false, 0u, &cupid_cgb_compat_auto_21},
    {0x59u, 0x00u, false, 0u, &cupid_cgb_compat_auto_32},
    {0x69u, 0x00u, false, 0u, &cupid_cgb_compat_auto_31},
    {0x19u, 0x00u, false, 0u, &cupid_cgb_compat_auto_20},
    {0x35u, 0x00u, false, 0u, &cupid_cgb_compat_auto_05},
    {0xa8u, 0x00u, false, 0u, &cupid_cgb_compat_auto_33},
    {0x14u, 0x00u, false, 0u, &cupid_cgb_compat_auto_13},
    {0xaau, 0x00u, false, 0u, &cupid_cgb_compat_auto_14},
    {0x75u, 0x00u, false, 0u, &cupid_cgb_compat_auto_05},
    {0x95u, 0x00u, false, 0u, &cupid_cgb_compat_auto_29},
    {0x99u, 0x00u, false, 0u, &cupid_cgb_compat_auto_05},
    {0x34u, 0x00u, false, 0u, &cupid_cgb_compat_auto_18},
    {0x6fu, 0x00u, false, 0u, &cupid_cgb_compat_auto_09},
    {0x15u, 0x00u, false, 0u, &cupid_cgb_compat_auto_03},
    {0xffu, 0x00u, false, 0u, &cupid_cgb_compat_auto_02},
    {0x97u, 0x00u, false, 0u, &cupid_cgb_compat_auto_26},
    {0x4bu, 0x00u, false, 0u, &cupid_cgb_compat_auto_25},
    {0x90u, 0x00u, false, 0u, &cupid_cgb_compat_auto_25},
    {0x17u, 0x00u, false, 0u, &cupid_cgb_compat_auto_41},
    {0x10u, 0x00u, false, 0u, &cupid_cgb_compat_auto_42},
    {0x39u, 0x00u, false, 0u, &cupid_cgb_compat_auto_26},
    {0xf7u, 0x00u, false, 0u, &cupid_cgb_compat_auto_45},
    {0xf6u, 0x00u, false, 0u, &cupid_cgb_compat_auto_42},
    {0xa2u, 0x00u, false, 0u, &cupid_cgb_compat_auto_45},
    {0x49u, 0x00u, false, 0u, &cupid_cgb_compat_auto_36},
    {0x4eu, 0x00u, false, 0u, &cupid_cgb_compat_auto_38},
    {0x43u, 0x00u, false, 1440u, &cupid_cgb_compat_auto_26},
    {0x68u, 0x00u, false, 0u, &cupid_cgb_compat_auto_42},
    {0xe0u, 0x00u, false, 0u, &cupid_cgb_compat_auto_30},
    {0x8bu, 0x00u, false, 0u, &cupid_cgb_compat_auto_41},
    {0xf0u, 0x00u, false, 0u, &cupid_cgb_compat_auto_34},
    {0xceu, 0x00u, false, 0u, &cupid_cgb_compat_auto_34},
    {0x0cu, 0x00u, false, 0u, &cupid_cgb_compat_auto_05},
    {0x29u, 0x00u, false, 0u, &cupid_cgb_compat_auto_42},
    {0xe8u, 0x00u, false, 0u, &cupid_cgb_compat_auto_06},
    {0xb7u, 0x00u, false, 0u, &cupid_cgb_compat_auto_05},
    {0x86u, 0x00u, false, 0u, &cupid_cgb_compat_auto_33},
    {0x9au, 0x00u, false, 0u, &cupid_cgb_compat_auto_25},
    {0x52u, 0x00u, false, 0u, &cupid_cgb_compat_auto_42},
    {0x01u, 0x00u, false, 0u, &cupid_cgb_compat_auto_42},
    {0x9du, 0x00u, false, 0u, &cupid_cgb_compat_auto_40},
    {0x71u, 0x00u, false, 0u, &cupid_cgb_compat_auto_02},
    {0x9cu, 0x00u, false, 0u, &cupid_cgb_compat_auto_16},
    {0xbdu, 0x00u, false, 0u, &cupid_cgb_compat_auto_25},
    {0x5du, 0x00u, false, 0u, &cupid_cgb_compat_auto_42},
    {0x6du, 0x00u, false, 0u, &cupid_cgb_compat_auto_42},
    {0x67u, 0x00u, false, 0u, &cupid_cgb_compat_auto_05},
    {0x3fu, 0x00u, false, 0u, &cupid_cgb_compat_auto_00},
    {0x6bu, 0x00u, false, 0u, &cupid_cgb_compat_auto_39},
    {0xb3u, 'B', true, 0u, &cupid_cgb_compat_auto_36},
    {0x46u, 'E', true, 0u, &cupid_cgb_compat_auto_22},
    {0x28u, 'F', true, 0u, &cupid_cgb_compat_auto_25},
    {0xa5u, 'A', true, 0u, &cupid_cgb_compat_auto_06},
    {0xc6u, 'A', true, 0u, &cupid_cgb_compat_auto_32},
    {0xd3u, 'R', true, 0u, &cupid_cgb_compat_auto_12},
    {0x27u, 'B', true, 0u, &cupid_cgb_compat_auto_36},
    {0x61u, 'E', true, 0u, &cupid_cgb_compat_auto_11},
    {0x18u, 'K', true, 0u, &cupid_cgb_compat_auto_39},
    {0x66u, 'E', true, 0u, &cupid_cgb_compat_auto_18},
    {0x6au, 'K', true, 0u, &cupid_cgb_compat_auto_39},
    {0xbfu, ' ', true, 0u, &cupid_cgb_compat_auto_24},
    {0x0du, 'R', true, 0u, &cupid_cgb_compat_auto_31},
    {0xf4u, '-', true, 0u, &cupid_cgb_compat_auto_50},
    {0xb3u, 'U', true, 0u, &cupid_cgb_compat_auto_17},
    {0x46u, 'R', true, 0u, &cupid_cgb_compat_auto_46},
    {0x28u, 'A', true, 0u, &cupid_cgb_compat_auto_06},
    {0xa5u, 'R', true, 0u, &cupid_cgb_compat_auto_27},
    {0xc6u, ' ', true, 0u, &cupid_cgb_compat_auto_00},
    {0xd3u, 'I', true, 0u, &cupid_cgb_compat_auto_47},
    {0x27u, 'N', true, 0u, &cupid_cgb_compat_auto_41},
    {0x61u, 'A', true, 0u, &cupid_cgb_compat_auto_41},
    {0x18u, 'I', true, 0u, &cupid_cgb_compat_auto_00},
    {0x66u, 'L', true, 0u, &cupid_cgb_compat_auto_00},
    {0x6au, 'I', true, 0u, &cupid_cgb_compat_auto_19},
    {0xbfu, 'C', true, 0u, &cupid_cgb_compat_auto_34},
    {0x0du, 'E', true, 0u, &cupid_cgb_compat_auto_23},
    {0xf4u, ' ', true, 0u, &cupid_cgb_compat_auto_18},
    {0xb3u, 'R', true, 0u, &cupid_cgb_compat_auto_29}};

/**
 * @brief Returns whether the loaded ROM was published by Nintendo.
 *
 * Checks the old licensee code field (0x014B). If it equals 0x33, the
 * new licensee code (0x0144–0x0145) must be "01" to count as Nintendo.
 * Otherwise old code 0x01 is accepted directly.
 *
 * Used by the CGB compatibility-palette lookup to restrict palette
 * assignment to first-party titles, matching hardware behavior.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return `true` if the ROM has a Nintendo licensee code, `false` otherwise.
 */
static bool cupid_cgb_has_nintendo_licensee(const CupidGb *gb) {
    if (gb == 0) {
        return false;
    }

    if (gb->header.old_licensee_code == 0x33u) {
        return gb->header.new_licensee_code[0] == '0' && gb->header.new_licensee_code[1] == '1';
    }

    return gb->header.old_licensee_code == 0x01u;
}

/**
 * @brief Computes the CGB title checksum used for compatibility-palette lookup.
 *
 * Sums the bytes at ROM addresses 0x0134–0x0143 (the cartridge title
 * field) as a `uint8_t`, discarding overflow.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return The 8-bit checksum, or 0 if @p gb or its ROM buffer is NULL, or the
 *         ROM is too small.
 */
static uint8_t cupid_cgb_title_checksum(const CupidGb *gb) {
    uint8_t checksum = 0u;
    size_t index;

    if (gb == 0 || gb->rom == 0 || gb->rom_size < 0x0144u) {
        return 0u;
    }

    for (index = 0x0134u; index <= 0x0143u; ++index) {
        checksum = (uint8_t)(checksum + gb->rom[index]);
    }

    return checksum;
}

uint8_t cupid_cgb_boot_title_checksum(const CupidGb *gb) {
    return cupid_cgb_has_nintendo_licensee(gb) ? cupid_cgb_title_checksum(gb) : 0;
}

/**
 * @brief Returns the fourth letter of the ROM title (ROM address 0x0137).
 *
 * Used alongside @ref cupid_cgb_title_checksum to disambiguate palette
 * table entries that share the same checksum.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return The byte at 0x0137, or 0 if the ROM is NULL or too short.
 */
static uint8_t cupid_cgb_title_fourth_letter(const CupidGb *gb) {
    if (gb == 0 || gb->rom == 0 || gb->rom_size <= 0x0137u) {
        return 0u;
    }

    return gb->rom[0x0137u];
}

typedef struct CupidCgbPaletteMatch {
    const CupidCgbCompatPaletteKey *key;
    size_t first_rejected;
    unsigned rejected;
} CupidCgbPaletteMatch;

static CupidCgbPaletteMatch cupid_cgb_find_palette(const CupidGb *gb) {
    const uint8_t checksum = cupid_cgb_title_checksum(gb);
    const uint8_t fourth_letter = cupid_cgb_title_fourth_letter(gb);
    CupidCgbPaletteMatch match = {0};
    for (size_t index = 0;
         index < sizeof(cupid_cgb_compat_palette_keys) / sizeof(cupid_cgb_compat_palette_keys[0]);
         ++index) {
        const CupidCgbCompatPaletteKey *key = &cupid_cgb_compat_palette_keys[index];
        if (key->title_checksum != checksum)
            continue;
        if (!key->require_fourth_letter) {
            match.key = key;
        } else if (key->fourth_letter == fourth_letter) {
            match.key = key;
            break;
        } else {
            if (!match.rejected)
                match.first_rejected = index;
            ++match.rejected;
        }
    }
    return match;
}

/**
 * @brief Looks up the CGB compatibility palette for the loaded ROM.
 *
 * Searches `cupid_cgb_compat_palette_keys` for an entry whose checksum
 * matches the ROM title checksum. Entries that additionally require a
 * fourth-letter match are only selected when the fourth letter also
 * matches. Entries without the fourth-letter requirement are kept as a
 * fallback and returned if no exact match is found.
 *
 * Returns the default greyscale palette if @p gb is NULL or the ROM
 * does not have a Nintendo licensee code.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return Pointer to the matched @ref CupidCgbCompatPaletteSet (never NULL).
 */
static const CupidCgbCompatPaletteSet *cupid_cgb_lookup_compat_palette(const CupidGb *gb) {
    if (!cupid_cgb_has_nintendo_licensee(gb))
        return cupid_cgb_compat_default;
    const CupidCgbPaletteMatch match = cupid_cgb_find_palette(gb);
    return match.key ? match.key->palette_set : cupid_cgb_compat_default;
}

unsigned cupid_cgb_apply_cartridge_handoff(CupidGb *gb) {
    enum { FIRST_CONDITIONAL_KEY = 65, CONDITIONAL_GROUP_SIZE = 14 };

    const bool extended_license = gb->header.old_licensee_code == 0x33u;
    const bool palette_search = cupid_cgb_has_nintendo_licensee(gb);
    unsigned phase = 0x1ea0u;
    const CupidCgbCompatPaletteSet *palette = cupid_cgb_compat_default;
    unsigned tilemap_cycles = 0u;
    if (palette_search) {
        const CupidCgbPaletteMatch match = cupid_cgb_find_palette(gb);
        if (match.key) {
            /* The ordered title search charges 48 T-cycles per entry. Each
             * fourth-byte group shifts the palette transfer to a different phase. */
            const unsigned index = (unsigned)(match.key - cupid_cgb_compat_palette_keys);
            phase = 160u + 360u * match.key->palette_set->boot_palette_periods + 48u * index;
            palette = match.key->palette_set;
            tilemap_cycles = match.key->compatibility_tilemap_cycles;
            if (match.key->require_fourth_letter) {
                const unsigned group = (index - FIRST_CONDITIONAL_KEY) / CONDITIONAL_GROUP_SIZE;
                phase = phase + 72u - 580u * group;
            }
        } else if (match.rejected) {
            /* A rejected fourth byte resumes at the next group for this checksum. */
            phase = 11776u + 48u * (unsigned)(match.first_rejected - FIRST_CONDITIONAL_KEY);
            if (match.rejected == 3)
                phase += 92u;
        } else {
            phase = 0x2fa8u;
        }
        if (extended_license)
            phase += 32u;
    } else if (extended_license) {
        phase = gb->header.new_licensee_code[0] == '0' ? 0x1ec0u : 0x1e9cu;
    }

    const bool legacy_tilemap = !gb->cgb_mode && tilemap_cycles != 0;
    if (legacy_tilemap) {
        cupid_gb_startup_logo_tilemap(gb);
        gb->cpu.h = 0x99u;
        gb->cpu.l = 0x1au;
    }
    /* Revision 0 uses its own divider and LCD handoff. */
    if (gb->model == CUPID_GB_MODEL_CGB_0)
        return 0;

    /* Startup calls leave their last return addresses and saved registers
     * below SP. The final compatibility call depends on the tilemap path. */
    static const uint8_t boot_stack[CUPID_GB_CGB_BOOT_STACK_END - CUPID_GB_CGB_BOOT_STACK_START] = {
        0x71, 0x02, 0x4d, 0x01, 0x00, 0xff, 0x0d, 0x00, 0x00, 0x00, 0xf9, 0x00,
    };
    uint8_t *stack = gb->high_ram + CUPID_GB_CGB_BOOT_STACK_START;
    memcpy(stack, boot_stack, sizeof(boot_stack));
    const uint16_t return_address = gb->cgb_mode ? 0x05d3u : legacy_tilemap ? 0x0603u : 0x05f5u;
    stack[4] = gb->cgb_mode ? 0xc1u : 0;
    stack[8] = (uint8_t)return_address;
    stack[9] = (uint8_t)(return_address >> 8);

    /* The monochrome setup cost follows the selected palette slot. Two keys
     * also wait for the firmware's legacy tilemap transfer. */
    const unsigned extra_cycles =
        gb->cgb_mode ? 0u : 1116u + 32u * palette->boot_compatibility_slot + tilemap_cycles;
    cupid_gb_apu_apply_cgb_boot_pulse(gb, phase, extra_cycles);
    phase += extra_cycles;
    /* Install the complete phase before the first cartridge clock. */
    gb->io_registers[0x04] = (uint8_t)(phase >> 8u);
    gb->div_counter = (uint16_t)((phase >> 2u) & 63u);
    gb->divider_subcycle = (uint8_t)(phase & 3u);
    gb->apu.fs_step = palette_search || extra_cycles ? 1u : 0u;
    gb->apu.fs_skip_div_event = 0u;
    return extra_cycles;
}

/**
 * @brief Writes four RGB555 colors into a CGB palette RAM block.
 *
 * Each color occupies 2 bytes in palette RAM (little-endian). The
 * block for @p palette_index starts at byte offset `palette_index * 8`.
 *
 * @param palette_ram   Pointer to the 64-byte BG or OBJ palette RAM.
 * @param palette_index Which palette slot to write (0–7).
 * @param colors        Array of exactly 4 RGB555 color values.
 */
static void cupid_cgb_store_palette(uint8_t *palette_ram, unsigned int palette_index,
                                    const uint16_t colors[4]) {
    unsigned int color_index;
    size_t base_offset = (size_t)palette_index * 8u;

    for (color_index = 0u; color_index < 4u; ++color_index) {
        uint16_t color = colors[color_index];
        size_t color_offset = base_offset + (size_t)color_index * 2u;

        palette_ram[color_offset] = (uint8_t)(color & 0xffu);
        palette_ram[color_offset + 1u] = (uint8_t)(color >> 8u);
    }
}

/**
 * @brief Returns the CGB compatibility palette for the loaded DMG ROM.
 *
 * Looks up the palette via @ref cupid_cgb_lookup_compat_palette and
 * copies the BG, OBJ0, and OBJ1 palette arrays into the provided
 * output buffers.
 *
 * @param gb   Pointer to the Game Boy state.
 * @param bg   Output array of 4 RGB555 colors for the background palette.
 * @param obj0 Output array of 4 RGB555 colors for object palette 0.
 * @param obj1 Output array of 4 RGB555 colors for object palette 1.
 *
 * @return `true` if a named compatibility palette was found (i.e. the ROM
 *         has a matching Nintendo licensee entry); `false` if the default
 *         greyscale palette is being used or any pointer is NULL.
 */
bool cupid_cgb_get_compatibility_palette(const CupidGb *gb, uint16_t bg[4], uint16_t obj0[4],
                                         uint16_t obj1[4]) {
    const CupidCgbCompatPaletteSet *palette_set;

    if (bg == 0 || obj0 == 0 || obj1 == 0) {
        return false;
    }

    palette_set = cupid_cgb_lookup_compat_palette(gb);
    memcpy(bg, palette_set->bg, sizeof(palette_set->bg));
    memcpy(obj0, palette_set->obj0, sizeof(palette_set->obj0));
    memcpy(obj1, palette_set->obj1, sizeof(palette_set->obj1));

    return gb != 0 && cupid_cgb_has_nintendo_licensee(gb) &&
           palette_set != cupid_cgb_compat_default;
}

/**
 * @brief Applies the CGB compatibility palette to the CGB palette RAM.
 *
 * Retrieves the palette via @ref cupid_cgb_get_compatibility_palette and
 * writes the BG, OBJ0, and OBJ1 palettes into slots 0 of the respective
 * palette RAMs. Called after a ROM is loaded on a CGB running in DMG
 * compatibility mode.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_cgb_apply_compatibility_palette(CupidGb *gb) {
    uint16_t bg[4];
    uint16_t obj0[4];
    uint16_t obj1[4];

    if (gb == 0) {
        return;
    }

    cupid_cgb_get_compatibility_palette(gb, bg, obj0, obj1);
    cupid_cgb_store_palette(gb->cgb_bg_palette_ram, 0u, bg);
    cupid_cgb_store_palette(gb->cgb_obj_palette_ram, 0u, obj0);
    cupid_cgb_store_palette(gb->cgb_obj_palette_ram, 1u, obj1);
}

/**
 * @brief Returns whether the CGB is running a DMG ROM in compatibility mode.
 *
 * Compatibility mode is active when the model is CGB (@ref CUPID_GB_MODEL_CGB)
 * but `cgb_mode` is `false` (the ROM did not set the CGB flag byte).
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return `true` if CGB compatibility mode is active.
 */
bool cupid_cgb_compat_active(const CupidGb *gb) {
    return gb != 0 && cupid_gb_model_is_cgb(gb->model) && !gb->cgb_mode;
}

/**
 * @brief Returns the CGB background color for a DMG shade index in compatibility mode.
 *
 * Reads color @p shade from BG palette slot 0 of the CGB palette RAM.
 * Only valid when @ref cupid_cgb_compat_active returns `true`.
 *
 * @param gb    Pointer to the Game Boy state.
 * @param shade The DMG shade index (0–3).
 *
 * @return The RGB555 color value, or 0 if compatibility mode is not active.
 */
uint16_t cupid_cgb_compat_bg_color(const CupidGb *gb, uint8_t shade) {
    size_t color_offset;

    if (!cupid_cgb_compat_active(gb)) {
        return 0x0000u;
    }

    color_offset = (size_t)(shade & 0x03u) * 2u;
    /* Palette storage retains bit 15; RGB555 output has only fifteen color lines. */
    return (uint16_t)(((unsigned)gb->cgb_bg_palette_ram[color_offset] |
                       ((unsigned)gb->cgb_bg_palette_ram[color_offset + 1u] << 8u)) &
                      0x7fffu);
}

/**
 * @brief Returns the CGB object color for a DMG shade index in compatibility mode.
 *
 * Reads color @p shade from the specified OBJ palette slot (0 or 1)
 * of the CGB OBJ palette RAM. Only valid when @ref cupid_cgb_compat_active
 * returns `true`.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param palette OBJ palette index (0 or 1).
 * @param shade   The DMG shade index (0–3).
 *
 * @return The RGB555 color value, or 0 if compatibility mode is not active.
 */
uint16_t cupid_cgb_compat_obj_color(const CupidGb *gb, unsigned int palette, uint8_t shade) {
    size_t base_offset;
    size_t color_offset;

    if (!cupid_cgb_compat_active(gb)) {
        return 0x0000u;
    }

    base_offset = (palette & 0x01u) * 8u;
    color_offset = base_offset + (size_t)(shade & 0x03u) * 2u;
    return (uint16_t)(((unsigned)gb->cgb_obj_palette_ram[color_offset] |
                       ((unsigned)gb->cgb_obj_palette_ram[color_offset + 1u] << 8u)) &
                      0x7fffu);
}

/**
 * @brief Returns whether CGB palette RAM is currently inaccessible to the CPU.
 *
 * Physical color hardware claims palette RAM after mode 3 begins and releases
 * it after the last pixel. Startup and normal scanlines have separate claim
 * edges; normal and double speed have different trailing release phases.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return `true` if palette RAM reads/writes should be blocked.
 */
bool cupid_cgb_palettes_cpu_blocked(const CupidGb *gb) {
    if (gb == 0 || !cupid_gb_model_is_cgb(gb->model) || !cupid_gb_lcd_enabled(gb) ||
        gb->io_registers[CUPID_GB_IO_LY] >= CUPID_GB_PPU_VISIBLE_SCANLINES) {
        return false;
    }
    const CupidGbPixels *p = &gb->pixels;
    if (p->initialized && gb->ppu_transfer_end_dot) {
        /* The palette port releases later than VRAM. At normal speed it
         * remains owned through both trailing two-dot phases. */
        const unsigned tail = gb->double_speed ? 3u : 5u;
        return p->dot < gb->ppu_transfer_end_dot + tail;
    }
    if ((gb->io_registers[CUPID_GB_IO_STAT] & 3u) != CUPID_GB_PPU_MODE_TRANSFER)
        return false;
    /* Startup and ordinary mode 3 expose STAT before claiming palette RAM. */
    return !p->initialized || p->dot >= (gb->ppu_lcd_startup ? 81u : 84u);
}

/**
 * @brief Reads one byte from CGB BG or OBJ palette RAM via the index register.
 *
 * The index is taken from 0xFF68 (BG) or 0xFF6A (OBJ), bits 5–0.
 * While boot firmware is mapped, data ports remain accessible on every model.
 * After boot they require color mode; index ports always require color hardware.
 *
 * @param gb             Pointer to the Game Boy state.
 * @param object_palette `true` to read from OBJ palette RAM, `false` for BG.
 *
 * @return The byte at the selected palette RAM index.
 */
uint8_t cupid_cgb_palette_read(const CupidGb *gb, bool object_palette) {
    const uint8_t *palette_ram;
    uint8_t index;

    if (gb == 0 || (!gb->cgb_mode && !gb->boot_rom_enabled) || cupid_cgb_palettes_cpu_blocked(gb)) {
        return 0xffu;
    }

    palette_ram = object_palette ? gb->cgb_obj_palette_ram : gb->cgb_bg_palette_ram;
    index = gb->io_registers[object_palette ? 0x6au : 0x68u] & 0x3fu;
    return palette_ram[index];
}

/**
 * @brief Writes one byte to CGB BG or OBJ palette RAM via the index register.
 *
 * The index is taken from 0xFF68 (BG) or 0xFF6A (OBJ), bits 5–0. If
 * auto-increment (bit 7) is set, the index is incremented after the
 * write. The corresponding read-back register (0xFF69 / 0xFF6B) is
 * updated regardless of palette-blocked status, but the palette RAM
 * itself is only modified when the PPU is not blocking it.
 *
 * @param gb             Pointer to the Game Boy state.
 * @param object_palette `true` to write to OBJ palette RAM, `false` for BG.
 * @param value          The byte to write.
 *
 * @note Compatibility mode closes the data ports only after boot completes.
 */
void cupid_cgb_palette_write(CupidGb *gb, bool object_palette, uint8_t value) {
    uint8_t *palette_ram;
    uint8_t index_register;
    uint8_t index;
    bool auto_increment;

    if (gb == 0 || (!gb->cgb_mode && !gb->boot_rom_enabled)) {
        return;
    }

    palette_ram = object_palette ? gb->cgb_obj_palette_ram : gb->cgb_bg_palette_ram;
    index_register = object_palette ? 0x6au : 0x68u;
    index = gb->io_registers[index_register] & 0x3fu;
    auto_increment = (gb->io_registers[index_register] & 0x80u) != 0u;

    if (!cupid_cgb_palettes_cpu_blocked(gb)) {
        palette_ram[index] = value;
    }

    if (auto_increment) {
        gb->io_registers[index_register] =
            (uint8_t)((gb->io_registers[index_register] & 0x80u) | ((index + 1u) & 0x3fu));
    }

    gb->io_registers[object_palette ? 0x6bu : 0x69u] = value;
}

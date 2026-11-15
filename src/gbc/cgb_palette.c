/* Compatibility colors and CPU palette ports share the same physical palette RAM. */
#include "cupid/gbc/cgb.h"

#include "cgb_internal.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"

#include <string.h>

typedef struct CupidCgbCompatPaletteSet {
    uint16_t bg[4];
    uint16_t obj0[4];
    uint16_t obj1[4];
} CupidCgbCompatPaletteSet;

typedef struct CupidCgbCompatPaletteKey {
    uint8_t title_checksum;
    uint8_t fourth_letter;
    bool require_fourth_letter;
    const CupidCgbCompatPaletteSet *palette_set;
} CupidCgbCompatPaletteKey;

#define CUPID_CGB_RGB8(r, g, b)                                                                    \
    (uint16_t)((((uint16_t)((b) >> 3u)) << 10u) | (((uint16_t)((g) >> 3u)) << 5u) |                \
               ((uint16_t)((r) >> 3u)))

static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_00 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x63, 0xc5), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_02 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_03 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_default_palette = {
    {0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u},
    {0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u},
    {0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u}};
static const CupidCgbCompatPaletteSet *const cupid_cgb_compat_default =
    &cupid_cgb_compat_default_palette;
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_04 = {
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_05 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_06 = {
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x84, 0x84),
     CUPID_CGB_RGB8(0xff, 0xde, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x84, 0x84),
     CUPID_CGB_RGB8(0xff, 0xde, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x84, 0x84),
     CUPID_CGB_RGB8(0xff, 0xde, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_07 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xa5, 0xa5, 0xa5),
     CUPID_CGB_RGB8(0x52, 0x52, 0x52), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xa5, 0xa5, 0xa5),
     CUPID_CGB_RGB8(0x52, 0x52, 0x52), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xa5, 0xa5, 0xa5),
     CUPID_CGB_RGB8(0x52, 0x52, 0x52), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_09 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xce, 0x00),
     CUPID_CGB_RGB8(0x9c, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xce, 0x00),
     CUPID_CGB_RGB8(0x9c, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xce, 0x00),
     CUPID_CGB_RGB8(0x9c, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_10 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_11 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_12 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_13 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_14 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x63, 0xc5), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x63, 0xc5), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_15 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_16 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_17 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_18 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x00),
     CUPID_CGB_RGB8(0xb5, 0x73, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_19 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x52, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_20 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_21 = {
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0x63, 0x52), CUPID_CGB_RGB8(0xd6, 0x00, 0x00),
     CUPID_CGB_RGB8(0x63, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0x63, 0x52), CUPID_CGB_RGB8(0xd6, 0x00, 0x00),
     CUPID_CGB_RGB8(0x63, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_22 = {
    {CUPID_CGB_RGB8(0xb5, 0xb5, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x94),
     CUPID_CGB_RGB8(0xad, 0x5a, 0x42), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0xff, 0x84, 0x84), CUPID_CGB_RGB8(0x94, 0x3a, 0x3a)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0xff, 0x84, 0x84), CUPID_CGB_RGB8(0x94, 0x3a, 0x3a)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_23 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_24 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_25 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_26 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_27 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_29 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x52, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x52, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_30 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x9c, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_31 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_32 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_33 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0x9c), CUPID_CGB_RGB8(0x94, 0xb5, 0xff),
     CUPID_CGB_RGB8(0x63, 0x94, 0x73), CUPID_CGB_RGB8(0x00, 0x3a, 0x3a)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_34 = {
    {CUPID_CGB_RGB8(0x6b, 0xff, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0xff, 0x52, 0x4a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0x63, 0xa5, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0xff)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_35 = {
    {CUPID_CGB_RGB8(0x52, 0xde, 0x00), CUPID_CGB_RGB8(0xff, 0x84, 0x00),
     CUPID_CGB_RGB8(0xff, 0xff, 0x00), CUPID_CGB_RGB8(0xff, 0xff, 0xff)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0x63, 0xa5, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0xff)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_36 = {
    {CUPID_CGB_RGB8(0xa5, 0x9c, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x00),
     CUPID_CGB_RGB8(0x00, 0x63, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0x63, 0x52), CUPID_CGB_RGB8(0xd6, 0x00, 0x00),
     CUPID_CGB_RGB8(0x63, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0xff),
     CUPID_CGB_RGB8(0xff, 0xff, 0x7b), CUPID_CGB_RGB8(0x00, 0x84, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_37 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xce), CUPID_CGB_RGB8(0x63, 0xef, 0xef),
     CUPID_CGB_RGB8(0x9c, 0x84, 0x31), CUPID_CGB_RGB8(0x5a, 0x5a, 0x5a)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x73, 0x00),
     CUPID_CGB_RGB8(0x94, 0x42, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_38 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xff, 0x7b),
     CUPID_CGB_RGB8(0x00, 0x84, 0xff), CUPID_CGB_RGB8(0xff, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_39 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xc5, 0x42), CUPID_CGB_RGB8(0xff, 0xd6, 0x00),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x00), CUPID_CGB_RGB8(0x4a, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x5a, 0xbd, 0xff),
     CUPID_CGB_RGB8(0xff, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0xff)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_40 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x8c, 0x8c, 0xde),
     CUPID_CGB_RGB8(0x52, 0x52, 0x8c), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_41 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_42 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_44 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x00, 0xff, 0x00),
     CUPID_CGB_RGB8(0x31, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x4a, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_45 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_46 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0x00), CUPID_CGB_RGB8(0xff, 0x00, 0x00),
     CUPID_CGB_RGB8(0x63, 0x00, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x84, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_47 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xad, 0xad, 0x84),
     CUPID_CGB_RGB8(0x42, 0x73, 0x7b), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0xad, 0x63),
     CUPID_CGB_RGB8(0x84, 0x31, 0x00), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_50 = {
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x7b, 0xff, 0x31),
     CUPID_CGB_RGB8(0x00, 0x63, 0xc5), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0xff, 0x84, 0x84),
     CUPID_CGB_RGB8(0x94, 0x3a, 0x3a), CUPID_CGB_RGB8(0x00, 0x00, 0x00)},
    {CUPID_CGB_RGB8(0xff, 0xff, 0xff), CUPID_CGB_RGB8(0x63, 0xa5, 0xff),
     CUPID_CGB_RGB8(0x00, 0x00, 0xff), CUPID_CGB_RGB8(0x00, 0x00, 0x00)}};

static const CupidCgbCompatPaletteKey cupid_cgb_compat_palette_keys[] = {
    {0x00u, 0x00u, false, &cupid_cgb_compat_auto_00},
    {0x88u, 0x00u, false, &cupid_cgb_compat_auto_04},
    {0x16u, 0x00u, false, &cupid_cgb_compat_auto_05},
    {0x36u, 0x00u, false, &cupid_cgb_compat_auto_35},
    {0xd1u, 0x00u, false, &cupid_cgb_compat_auto_34},
    {0xdbu, 0x00u, false, &cupid_cgb_compat_auto_03},
    {0xf2u, 0x00u, false, &cupid_cgb_compat_auto_31},
    {0x3cu, 0x00u, false, &cupid_cgb_compat_auto_15},
    {0x8cu, 0x00u, false, &cupid_cgb_compat_auto_10},
    {0x92u, 0x00u, false, &cupid_cgb_compat_auto_05},
    {0x3du, 0x00u, false, &cupid_cgb_compat_auto_19},
    {0x5cu, 0x00u, false, &cupid_cgb_compat_auto_36},
    {0x58u, 0x00u, false, &cupid_cgb_compat_auto_07},
    {0xc9u, 0x00u, false, &cupid_cgb_compat_auto_37},
    {0x3eu, 0x00u, false, &cupid_cgb_compat_auto_30},
    {0x70u, 0x00u, false, &cupid_cgb_compat_auto_44},
    {0x1du, 0x00u, false, &cupid_cgb_compat_auto_21},
    {0x59u, 0x00u, false, &cupid_cgb_compat_auto_32},
    {0x69u, 0x00u, false, &cupid_cgb_compat_auto_31},
    {0x19u, 0x00u, false, &cupid_cgb_compat_auto_20},
    {0x35u, 0x00u, false, &cupid_cgb_compat_auto_05},
    {0xa8u, 0x00u, false, &cupid_cgb_compat_auto_33},
    {0x14u, 0x00u, false, &cupid_cgb_compat_auto_13},
    {0xaau, 0x00u, false, &cupid_cgb_compat_auto_14},
    {0x75u, 0x00u, false, &cupid_cgb_compat_auto_05},
    {0x95u, 0x00u, false, &cupid_cgb_compat_auto_29},
    {0x99u, 0x00u, false, &cupid_cgb_compat_auto_05},
    {0x34u, 0x00u, false, &cupid_cgb_compat_auto_18},
    {0x6fu, 0x00u, false, &cupid_cgb_compat_auto_09},
    {0x15u, 0x00u, false, &cupid_cgb_compat_auto_03},
    {0xffu, 0x00u, false, &cupid_cgb_compat_auto_02},
    {0x97u, 0x00u, false, &cupid_cgb_compat_auto_26},
    {0x4bu, 0x00u, false, &cupid_cgb_compat_auto_25},
    {0x90u, 0x00u, false, &cupid_cgb_compat_auto_25},
    {0x17u, 0x00u, false, &cupid_cgb_compat_auto_41},
    {0x10u, 0x00u, false, &cupid_cgb_compat_auto_42},
    {0x39u, 0x00u, false, &cupid_cgb_compat_auto_26},
    {0xf7u, 0x00u, false, &cupid_cgb_compat_auto_45},
    {0xf6u, 0x00u, false, &cupid_cgb_compat_auto_42},
    {0xa2u, 0x00u, false, &cupid_cgb_compat_auto_45},
    {0x49u, 0x00u, false, &cupid_cgb_compat_auto_36},
    {0x4eu, 0x00u, false, &cupid_cgb_compat_auto_38},
    {0x43u, 0x00u, false, &cupid_cgb_compat_auto_26},
    {0x68u, 0x00u, false, &cupid_cgb_compat_auto_42},
    {0xe0u, 0x00u, false, &cupid_cgb_compat_auto_30},
    {0x8bu, 0x00u, false, &cupid_cgb_compat_auto_41},
    {0xf0u, 0x00u, false, &cupid_cgb_compat_auto_34},
    {0xceu, 0x00u, false, &cupid_cgb_compat_auto_34},
    {0x0cu, 0x00u, false, &cupid_cgb_compat_auto_05},
    {0x29u, 0x00u, false, &cupid_cgb_compat_auto_42},
    {0xe8u, 0x00u, false, &cupid_cgb_compat_auto_06},
    {0xb7u, 0x00u, false, &cupid_cgb_compat_auto_05},
    {0x86u, 0x00u, false, &cupid_cgb_compat_auto_33},
    {0x9au, 0x00u, false, &cupid_cgb_compat_auto_25},
    {0x52u, 0x00u, false, &cupid_cgb_compat_auto_42},
    {0x01u, 0x00u, false, &cupid_cgb_compat_auto_42},
    {0x9du, 0x00u, false, &cupid_cgb_compat_auto_40},
    {0x71u, 0x00u, false, &cupid_cgb_compat_auto_02},
    {0x9cu, 0x00u, false, &cupid_cgb_compat_auto_16},
    {0xbdu, 0x00u, false, &cupid_cgb_compat_auto_25},
    {0x5du, 0x00u, false, &cupid_cgb_compat_auto_42},
    {0x6du, 0x00u, false, &cupid_cgb_compat_auto_42},
    {0x67u, 0x00u, false, &cupid_cgb_compat_auto_05},
    {0x3fu, 0x00u, false, &cupid_cgb_compat_auto_00},
    {0x6bu, 0x00u, false, &cupid_cgb_compat_auto_39},
    {0xb3u, 'B', true, &cupid_cgb_compat_auto_36},
    {0x46u, 'E', true, &cupid_cgb_compat_auto_22},
    {0x28u, 'F', true, &cupid_cgb_compat_auto_25},
    {0xa5u, 'A', true, &cupid_cgb_compat_auto_06},
    {0xc6u, 'A', true, &cupid_cgb_compat_auto_32},
    {0xd3u, 'R', true, &cupid_cgb_compat_auto_12},
    {0x27u, 'B', true, &cupid_cgb_compat_auto_36},
    {0x61u, 'E', true, &cupid_cgb_compat_auto_11},
    {0x18u, 'K', true, &cupid_cgb_compat_auto_39},
    {0x66u, 'E', true, &cupid_cgb_compat_auto_18},
    {0x6au, 'K', true, &cupid_cgb_compat_auto_39},
    {0xbfu, ' ', true, &cupid_cgb_compat_auto_24},
    {0x0du, 'R', true, &cupid_cgb_compat_auto_31},
    {0xf4u, '-', true, &cupid_cgb_compat_auto_50},
    {0xb3u, 'U', true, &cupid_cgb_compat_auto_17},
    {0x46u, 'R', true, &cupid_cgb_compat_auto_46},
    {0x28u, 'A', true, &cupid_cgb_compat_auto_06},
    {0xa5u, 'R', true, &cupid_cgb_compat_auto_27},
    {0xc6u, ' ', true, &cupid_cgb_compat_auto_00},
    {0xd3u, 'I', true, &cupid_cgb_compat_auto_47},
    {0x27u, 'N', true, &cupid_cgb_compat_auto_41},
    {0x61u, 'A', true, &cupid_cgb_compat_auto_41},
    {0x18u, 'I', true, &cupid_cgb_compat_auto_00},
    {0x66u, 'L', true, &cupid_cgb_compat_auto_00},
    {0x6au, 'I', true, &cupid_cgb_compat_auto_19},
    {0xbfu, 'C', true, &cupid_cgb_compat_auto_34},
    {0x0du, 'E', true, &cupid_cgb_compat_auto_23},
    {0xf4u, ' ', true, &cupid_cgb_compat_auto_18},
    {0xb3u, 'R', true, &cupid_cgb_compat_auto_29}};

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
    uint8_t checksum;
    uint8_t fourth_letter;
    size_t index;
    const CupidCgbCompatPaletteSet *fallback = cupid_cgb_compat_default;

    if (gb == 0 || !cupid_cgb_has_nintendo_licensee(gb)) {
        return cupid_cgb_compat_default;
    }

    checksum = cupid_cgb_title_checksum(gb);
    fourth_letter = cupid_cgb_title_fourth_letter(gb);
    for (index = 0u;
         index < sizeof(cupid_cgb_compat_palette_keys) / sizeof(cupid_cgb_compat_palette_keys[0]);
         ++index) {
        const CupidCgbCompatPaletteKey *key = &cupid_cgb_compat_palette_keys[index];

        if (key->title_checksum != checksum) {
            continue;
        }
        if (key->require_fourth_letter) {
            if (key->fourth_letter == fourth_letter) {
                return key->palette_set;
            }
            continue;
        }
        fallback = key->palette_set;
    }

    return fallback;
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
    return gb != 0 && gb->model == CUPID_GB_MODEL_CGB && !gb->cgb_mode;
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
    return (uint16_t)((uint16_t)gb->cgb_bg_palette_ram[color_offset] |
                      ((uint16_t)gb->cgb_bg_palette_ram[color_offset + 1u] << 8u));
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
    return (uint16_t)((uint16_t)gb->cgb_obj_palette_ram[color_offset] |
                      ((uint16_t)gb->cgb_obj_palette_ram[color_offset + 1u] << 8u));
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
    if (gb == 0 || gb->model != CUPID_GB_MODEL_CGB || !cupid_gb_lcd_enabled(gb) ||
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
 * Color boot firmware can access palette data after selecting compatibility
 * mode. After boot, data ports require color mode; index ports remain visible.
 *
 * @param gb             Pointer to the Game Boy state.
 * @param object_palette `true` to read from OBJ palette RAM, `false` for BG.
 *
 * @return The byte at the selected palette RAM index.
 */
uint8_t cupid_cgb_palette_read(const CupidGb *gb, bool object_palette) {
    const uint8_t *palette_ram;
    uint8_t index;

    if (gb == 0 || gb->model != CUPID_GB_MODEL_CGB || (!gb->cgb_mode && !gb->boot_rom_enabled) ||
        cupid_cgb_palettes_cpu_blocked(gb)) {
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

    if (gb == 0 || gb->model != CUPID_GB_MODEL_CGB || (!gb->cgb_mode && !gb->boot_rom_enabled)) {
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

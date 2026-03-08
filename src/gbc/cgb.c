#include "cupid/gbc/cgb.h"

#include <string.h>

#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"

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

#define CUPID_CGB_RGB8(r, g, b) \
    (uint16_t)((((uint16_t)((b) >> 3u)) << 10u) | \
               (((uint16_t)((g) >> 3u)) << 5u) | \
               ((uint16_t)((r) >> 3u)))

static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_00 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x63,0xc5), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_02 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x9c,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x9c,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x9c,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_03 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_default_palette = {
    { 0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u },
    { 0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u },
    { 0x7fffu, 0x56b5u, 0x2d6bu, 0x0000u }
};
static const CupidCgbCompatPaletteSet *const cupid_cgb_compat_default = &cupid_cgb_compat_default_palette;
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_04 = {
    { CUPID_CGB_RGB8(0xa5,0x9c,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0x00,0x63,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xa5,0x9c,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0x00,0x63,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xa5,0x9c,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0x00,0x63,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_05 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_06 = {
    { CUPID_CGB_RGB8(0x00,0x00,0x00), CUPID_CGB_RGB8(0x00,0x84,0x84), CUPID_CGB_RGB8(0xff,0xde,0x00), CUPID_CGB_RGB8(0xff,0xff,0xff) },
    { CUPID_CGB_RGB8(0x00,0x00,0x00), CUPID_CGB_RGB8(0x00,0x84,0x84), CUPID_CGB_RGB8(0xff,0xde,0x00), CUPID_CGB_RGB8(0xff,0xff,0xff) },
    { CUPID_CGB_RGB8(0x00,0x00,0x00), CUPID_CGB_RGB8(0x00,0x84,0x84), CUPID_CGB_RGB8(0xff,0xde,0x00), CUPID_CGB_RGB8(0xff,0xff,0xff) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_07 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xa5,0xa5,0xa5), CUPID_CGB_RGB8(0x52,0x52,0x52), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xa5,0xa5,0xa5), CUPID_CGB_RGB8(0x52,0x52,0x52), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xa5,0xa5,0xa5), CUPID_CGB_RGB8(0x52,0x52,0x52), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_09 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xce,0x00), CUPID_CGB_RGB8(0x9c,0x63,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xce,0x00), CUPID_CGB_RGB8(0x9c,0x63,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xce,0x00), CUPID_CGB_RGB8(0x9c,0x63,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_10 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xad,0xad,0x84), CUPID_CGB_RGB8(0x42,0x73,0x7b), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x73,0x00), CUPID_CGB_RGB8(0x94,0x42,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xad,0xad,0x84), CUPID_CGB_RGB8(0x42,0x73,0x7b), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_11 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_12 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x8c,0x8c,0xde), CUPID_CGB_RGB8(0x52,0x52,0x8c), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x8c,0x8c,0xde), CUPID_CGB_RGB8(0x52,0x52,0x8c), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_13 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x84,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_14 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x63,0xc5), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x63,0xc5), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_15 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_16 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x8c,0x8c,0xde), CUPID_CGB_RGB8(0x52,0x52,0x8c), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xc5,0x42), CUPID_CGB_RGB8(0xff,0xd6,0x00), CUPID_CGB_RGB8(0x94,0x3a,0x00), CUPID_CGB_RGB8(0x4a,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xc5,0x42), CUPID_CGB_RGB8(0xff,0xd6,0x00), CUPID_CGB_RGB8(0x94,0x3a,0x00), CUPID_CGB_RGB8(0x4a,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_17 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xad,0xad,0x84), CUPID_CGB_RGB8(0x42,0x73,0x7b), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x73,0x00), CUPID_CGB_RGB8(0x94,0x42,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x73,0x00), CUPID_CGB_RGB8(0x94,0x42,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_18 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x00), CUPID_CGB_RGB8(0xb5,0x73,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_19 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x52,0xff,0x00), CUPID_CGB_RGB8(0xff,0x42,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_20 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x9c,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_21 = {
    { CUPID_CGB_RGB8(0xa5,0x9c,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0x00,0x63,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0x63,0x52), CUPID_CGB_RGB8(0xd6,0x00,0x00), CUPID_CGB_RGB8(0x63,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0x63,0x52), CUPID_CGB_RGB8(0xd6,0x00,0x00), CUPID_CGB_RGB8(0x63,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_22 = {
    { CUPID_CGB_RGB8(0xb5,0xb5,0xff), CUPID_CGB_RGB8(0xff,0xff,0x94), CUPID_CGB_RGB8(0xad,0x5a,0x42), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0x00,0x00,0x00), CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a) },
    { CUPID_CGB_RGB8(0x00,0x00,0x00), CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_23 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x8c,0x8c,0xde), CUPID_CGB_RGB8(0x52,0x52,0x8c), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xc5,0x42), CUPID_CGB_RGB8(0xff,0xd6,0x00), CUPID_CGB_RGB8(0x94,0x3a,0x00), CUPID_CGB_RGB8(0x4a,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xc5,0x42), CUPID_CGB_RGB8(0xff,0xd6,0x00), CUPID_CGB_RGB8(0x94,0x3a,0x00), CUPID_CGB_RGB8(0x4a,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_24 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x8c,0x8c,0xde), CUPID_CGB_RGB8(0x52,0x52,0x8c), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_25 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x84,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_26 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_27 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x84,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x84,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_29 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x52,0xff,0x00), CUPID_CGB_RGB8(0xff,0x42,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x52,0xff,0x00), CUPID_CGB_RGB8(0xff,0x42,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x5a,0xbd,0xff), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0xff) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_30 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x9c,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x9c,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x5a,0xbd,0xff), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0xff) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_31 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x5a,0xbd,0xff), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0xff) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_32 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xad,0xad,0x84), CUPID_CGB_RGB8(0x42,0x73,0x7b), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x73,0x00), CUPID_CGB_RGB8(0x94,0x42,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x5a,0xbd,0xff), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0xff) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_33 = {
    { CUPID_CGB_RGB8(0xff,0xff,0x9c), CUPID_CGB_RGB8(0x94,0xb5,0xff), CUPID_CGB_RGB8(0x63,0x94,0x73), CUPID_CGB_RGB8(0x00,0x3a,0x3a) },
    { CUPID_CGB_RGB8(0xff,0xc5,0x42), CUPID_CGB_RGB8(0xff,0xd6,0x00), CUPID_CGB_RGB8(0x94,0x3a,0x00), CUPID_CGB_RGB8(0x4a,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_34 = {
    { CUPID_CGB_RGB8(0x6b,0xff,0x00), CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x52,0x4a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_35 = {
    { CUPID_CGB_RGB8(0x52,0xde,0x00), CUPID_CGB_RGB8(0xff,0x84,0x00), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0xff,0xff,0xff) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_36 = {
    { CUPID_CGB_RGB8(0xa5,0x9c,0xff), CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0x00,0x63,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0x63,0x52), CUPID_CGB_RGB8(0xd6,0x00,0x00), CUPID_CGB_RGB8(0x63,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0x7b), CUPID_CGB_RGB8(0x00,0x84,0xff) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_37 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xce), CUPID_CGB_RGB8(0x63,0xef,0xef), CUPID_CGB_RGB8(0x9c,0x84,0x31), CUPID_CGB_RGB8(0x5a,0x5a,0x5a) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x73,0x00), CUPID_CGB_RGB8(0x94,0x42,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_38 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xff,0x7b), CUPID_CGB_RGB8(0x00,0x84,0xff), CUPID_CGB_RGB8(0xff,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_39 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x8c,0x8c,0xde), CUPID_CGB_RGB8(0x52,0x52,0x8c), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xc5,0x42), CUPID_CGB_RGB8(0xff,0xd6,0x00), CUPID_CGB_RGB8(0x94,0x3a,0x00), CUPID_CGB_RGB8(0x4a,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x5a,0xbd,0xff), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0xff) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_40 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x8c,0x8c,0xde), CUPID_CGB_RGB8(0x52,0x52,0x8c), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_41 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x84,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_42 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x84,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_44 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x00,0xff,0x00), CUPID_CGB_RGB8(0x31,0x84,0x00), CUPID_CGB_RGB8(0x00,0x4a,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_45 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x84,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_46 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0x00), CUPID_CGB_RGB8(0xff,0x00,0x00), CUPID_CGB_RGB8(0x63,0x00,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x84,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_47 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xad,0xad,0x84), CUPID_CGB_RGB8(0x42,0x73,0x7b), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0xad,0x63), CUPID_CGB_RGB8(0x84,0x31,0x00), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};
static const CupidCgbCompatPaletteSet cupid_cgb_compat_auto_50 = {
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x7b,0xff,0x31), CUPID_CGB_RGB8(0x00,0x63,0xc5), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0xff,0x84,0x84), CUPID_CGB_RGB8(0x94,0x3a,0x3a), CUPID_CGB_RGB8(0x00,0x00,0x00) },
    { CUPID_CGB_RGB8(0xff,0xff,0xff), CUPID_CGB_RGB8(0x63,0xa5,0xff), CUPID_CGB_RGB8(0x00,0x00,0xff), CUPID_CGB_RGB8(0x00,0x00,0x00) }
};

static const CupidCgbCompatPaletteKey cupid_cgb_compat_palette_keys[] = {
    { 0x00u, 0x00u, false, &cupid_cgb_compat_auto_00 },
    { 0x88u, 0x00u, false, &cupid_cgb_compat_auto_04 },
    { 0x16u, 0x00u, false, &cupid_cgb_compat_auto_05 },
    { 0x36u, 0x00u, false, &cupid_cgb_compat_auto_35 },
    { 0xd1u, 0x00u, false, &cupid_cgb_compat_auto_34 },
    { 0xdbu, 0x00u, false, &cupid_cgb_compat_auto_03 },
    { 0xf2u, 0x00u, false, &cupid_cgb_compat_auto_31 },
    { 0x3cu, 0x00u, false, &cupid_cgb_compat_auto_15 },
    { 0x8cu, 0x00u, false, &cupid_cgb_compat_auto_10 },
    { 0x92u, 0x00u, false, &cupid_cgb_compat_auto_05 },
    { 0x3du, 0x00u, false, &cupid_cgb_compat_auto_19 },
    { 0x5cu, 0x00u, false, &cupid_cgb_compat_auto_36 },
    { 0x58u, 0x00u, false, &cupid_cgb_compat_auto_07 },
    { 0xc9u, 0x00u, false, &cupid_cgb_compat_auto_37 },
    { 0x3eu, 0x00u, false, &cupid_cgb_compat_auto_30 },
    { 0x70u, 0x00u, false, &cupid_cgb_compat_auto_44 },
    { 0x1du, 0x00u, false, &cupid_cgb_compat_auto_21 },
    { 0x59u, 0x00u, false, &cupid_cgb_compat_auto_32 },
    { 0x69u, 0x00u, false, &cupid_cgb_compat_auto_31 },
    { 0x19u, 0x00u, false, &cupid_cgb_compat_auto_20 },
    { 0x35u, 0x00u, false, &cupid_cgb_compat_auto_05 },
    { 0xa8u, 0x00u, false, &cupid_cgb_compat_auto_33 },
    { 0x14u, 0x00u, false, &cupid_cgb_compat_auto_13 },
    { 0xaau, 0x00u, false, &cupid_cgb_compat_auto_14 },
    { 0x75u, 0x00u, false, &cupid_cgb_compat_auto_05 },
    { 0x95u, 0x00u, false, &cupid_cgb_compat_auto_29 },
    { 0x99u, 0x00u, false, &cupid_cgb_compat_auto_05 },
    { 0x34u, 0x00u, false, &cupid_cgb_compat_auto_18 },
    { 0x6fu, 0x00u, false, &cupid_cgb_compat_auto_09 },
    { 0x15u, 0x00u, false, &cupid_cgb_compat_auto_03 },
    { 0xffu, 0x00u, false, &cupid_cgb_compat_auto_02 },
    { 0x97u, 0x00u, false, &cupid_cgb_compat_auto_26 },
    { 0x4bu, 0x00u, false, &cupid_cgb_compat_auto_25 },
    { 0x90u, 0x00u, false, &cupid_cgb_compat_auto_25 },
    { 0x17u, 0x00u, false, &cupid_cgb_compat_auto_41 },
    { 0x10u, 0x00u, false, &cupid_cgb_compat_auto_42 },
    { 0x39u, 0x00u, false, &cupid_cgb_compat_auto_26 },
    { 0xf7u, 0x00u, false, &cupid_cgb_compat_auto_45 },
    { 0xf6u, 0x00u, false, &cupid_cgb_compat_auto_42 },
    { 0xa2u, 0x00u, false, &cupid_cgb_compat_auto_45 },
    { 0x49u, 0x00u, false, &cupid_cgb_compat_auto_36 },
    { 0x4eu, 0x00u, false, &cupid_cgb_compat_auto_38 },
    { 0x43u, 0x00u, false, &cupid_cgb_compat_auto_26 },
    { 0x68u, 0x00u, false, &cupid_cgb_compat_auto_42 },
    { 0xe0u, 0x00u, false, &cupid_cgb_compat_auto_30 },
    { 0x8bu, 0x00u, false, &cupid_cgb_compat_auto_41 },
    { 0xf0u, 0x00u, false, &cupid_cgb_compat_auto_34 },
    { 0xceu, 0x00u, false, &cupid_cgb_compat_auto_34 },
    { 0x0cu, 0x00u, false, &cupid_cgb_compat_auto_05 },
    { 0x29u, 0x00u, false, &cupid_cgb_compat_auto_42 },
    { 0xe8u, 0x00u, false, &cupid_cgb_compat_auto_06 },
    { 0xb7u, 0x00u, false, &cupid_cgb_compat_auto_05 },
    { 0x86u, 0x00u, false, &cupid_cgb_compat_auto_33 },
    { 0x9au, 0x00u, false, &cupid_cgb_compat_auto_25 },
    { 0x52u, 0x00u, false, &cupid_cgb_compat_auto_42 },
    { 0x01u, 0x00u, false, &cupid_cgb_compat_auto_42 },
    { 0x9du, 0x00u, false, &cupid_cgb_compat_auto_40 },
    { 0x71u, 0x00u, false, &cupid_cgb_compat_auto_02 },
    { 0x9cu, 0x00u, false, &cupid_cgb_compat_auto_16 },
    { 0xbdu, 0x00u, false, &cupid_cgb_compat_auto_25 },
    { 0x5du, 0x00u, false, &cupid_cgb_compat_auto_42 },
    { 0x6du, 0x00u, false, &cupid_cgb_compat_auto_42 },
    { 0x67u, 0x00u, false, &cupid_cgb_compat_auto_05 },
    { 0x3fu, 0x00u, false, &cupid_cgb_compat_auto_00 },
    { 0x6bu, 0x00u, false, &cupid_cgb_compat_auto_39 },
    { 0xb3u, 'B',  true,  &cupid_cgb_compat_auto_36 },
    { 0x46u, 'E',  true,  &cupid_cgb_compat_auto_22 },
    { 0x28u, 'F',  true,  &cupid_cgb_compat_auto_25 },
    { 0xa5u, 'A',  true,  &cupid_cgb_compat_auto_06 },
    { 0xc6u, 'A',  true,  &cupid_cgb_compat_auto_32 },
    { 0xd3u, 'R',  true,  &cupid_cgb_compat_auto_12 },
    { 0x27u, 'B',  true,  &cupid_cgb_compat_auto_36 },
    { 0x61u, 'E',  true,  &cupid_cgb_compat_auto_11 },
    { 0x18u, 'K',  true,  &cupid_cgb_compat_auto_39 },
    { 0x66u, 'E',  true,  &cupid_cgb_compat_auto_18 },
    { 0x6au, 'K',  true,  &cupid_cgb_compat_auto_39 },
    { 0xbfu, ' ',  true,  &cupid_cgb_compat_auto_24 },
    { 0x0du, 'R',  true,  &cupid_cgb_compat_auto_31 },
    { 0xf4u, '-',  true,  &cupid_cgb_compat_auto_50 },
    { 0xb3u, 'U',  true,  &cupid_cgb_compat_auto_17 },
    { 0x46u, 'R',  true,  &cupid_cgb_compat_auto_46 },
    { 0x28u, 'A',  true,  &cupid_cgb_compat_auto_06 },
    { 0xa5u, 'R',  true,  &cupid_cgb_compat_auto_27 },
    { 0xc6u, ' ',  true,  &cupid_cgb_compat_auto_00 },
    { 0xd3u, 'I',  true,  &cupid_cgb_compat_auto_47 },
    { 0x27u, 'N',  true,  &cupid_cgb_compat_auto_41 },
    { 0x61u, 'A',  true,  &cupid_cgb_compat_auto_41 },
    { 0x18u, 'I',  true,  &cupid_cgb_compat_auto_00 },
    { 0x66u, 'L',  true,  &cupid_cgb_compat_auto_00 },
    { 0x6au, 'I',  true,  &cupid_cgb_compat_auto_19 },
    { 0xbfu, 'C',  true,  &cupid_cgb_compat_auto_34 },
    { 0x0du, 'E',  true,  &cupid_cgb_compat_auto_23 },
    { 0xf4u, ' ',  true,  &cupid_cgb_compat_auto_18 },
    { 0xb3u, 'R',  true,  &cupid_cgb_compat_auto_29 }
};

static bool cupid_cgb_has_nintendo_licensee(const CupidGb *gb)
{
    if (gb == 0) {
        return false;
    }

    if (gb->header.old_licensee_code == 0x33u) {
        return gb->header.new_licensee_code[0] == '0' &&
               gb->header.new_licensee_code[1] == '1';
    }

    return gb->header.old_licensee_code == 0x01u;
}

static uint8_t cupid_cgb_title_checksum(const CupidGb *gb)
{
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

static uint8_t cupid_cgb_title_fourth_letter(const CupidGb *gb)
{
    if (gb == 0 || gb->rom == 0 || gb->rom_size <= 0x0137u) {
        return 0u;
    }

    return gb->rom[0x0137u];
}

static const CupidCgbCompatPaletteSet *cupid_cgb_lookup_compat_palette(const CupidGb *gb)
{
    uint8_t checksum;
    uint8_t fourth_letter;
    size_t index;
    const CupidCgbCompatPaletteSet *fallback = cupid_cgb_compat_default;

    if (gb == 0 || !cupid_cgb_has_nintendo_licensee(gb)) {
        return cupid_cgb_compat_default;
    }

    checksum = cupid_cgb_title_checksum(gb);
    fourth_letter = cupid_cgb_title_fourth_letter(gb);
    for (index = 0u; index < sizeof(cupid_cgb_compat_palette_keys) / sizeof(cupid_cgb_compat_palette_keys[0]); ++index) {
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

static void cupid_cgb_store_palette(uint8_t *palette_ram, unsigned int palette_index, const uint16_t colors[4])
{
    unsigned int color_index;
    size_t base_offset = (size_t)palette_index * 8u;

    for (color_index = 0u; color_index < 4u; ++color_index) {
        uint16_t color = colors[color_index];
        size_t color_offset = base_offset + (size_t)color_index * 2u;

        palette_ram[color_offset] = (uint8_t)(color & 0xffu);
        palette_ram[color_offset + 1u] = (uint8_t)(color >> 8u);
    }
}

void cupid_cgb_init_state(CupidGb *gb)
{
    size_t palette_index;

    if (gb == 0) {
        return;
    }

    gb->cgb_vram_bank = 0u;
    gb->cgb_wram_bank = 1u;
    memset(gb->cgb_bg_palette_ram, 0u, sizeof(gb->cgb_bg_palette_ram));
    memset(gb->cgb_obj_palette_ram, 0u, sizeof(gb->cgb_obj_palette_ram));
    for (palette_index = 0u; palette_index < sizeof(gb->cgb_bg_palette_ram); palette_index += 2u) {
        gb->cgb_bg_palette_ram[palette_index] = 0xffu;
        gb->cgb_bg_palette_ram[palette_index + 1u] = 0x7fu;
    }
    gb->hdma_source = 0u;
    gb->hdma_destination = 0x8000u;
    gb->hdma_blocks_remaining = 0u;
    gb->hdma_active = false;
    gb->io_registers[0x4fu] = 0xfeu;
    gb->io_registers[0x68u] = 0x00u;
    gb->io_registers[0x69u] = 0xffu;
    gb->io_registers[0x6au] = 0x00u;
    gb->io_registers[0x6bu] = 0xffu;
    gb->io_registers[0x6cu] = 0xfeu;
    gb->io_registers[0x70u] = 0xf9u;
}

size_t cupid_cgb_vram_offset(const CupidGb *gb, uint16_t address)
{
    size_t bank = 0u;

    if (gb != 0 && gb->cgb_mode) {
        bank = (size_t)(gb->cgb_vram_bank & 0x01u);
    }

    return bank * 0x2000u + (size_t)(address - 0x8000u);
}

size_t cupid_cgb_wram_offset(const CupidGb *gb, uint16_t address)
{
    if (address >= 0xc000u && address <= 0xcfffu) {
        return (size_t)(address - 0xc000u);
    }

    if (gb != 0 && gb->cgb_mode) {
        uint8_t bank = gb->cgb_wram_bank & 0x07u;

        if (bank == 0u) {
            bank = 1u;
        }

        return (size_t)bank * 0x1000u + (size_t)(address - 0xd000u);
    }

    return 0x1000u + (size_t)(address - 0xd000u);
}

bool cupid_cgb_get_compatibility_palette(const CupidGb *gb,
                                         uint16_t bg[4],
                                         uint16_t obj0[4],
                                         uint16_t obj1[4])
{
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

void cupid_cgb_apply_compatibility_palette(CupidGb *gb)
{
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

bool cupid_cgb_compat_active(const CupidGb *gb)
{
    return gb != 0 && gb->model == CUPID_GB_MODEL_CGB && !gb->cgb_mode;
}

uint16_t cupid_cgb_compat_bg_color(const CupidGb *gb, uint8_t shade)
{
    size_t color_offset;

    if (!cupid_cgb_compat_active(gb)) {
        return 0x0000u;
    }

    color_offset = (size_t)(shade & 0x03u) * 2u;
    return (uint16_t)((uint16_t)gb->cgb_bg_palette_ram[color_offset] |
                      ((uint16_t)gb->cgb_bg_palette_ram[color_offset + 1u] << 8u));
}

uint16_t cupid_cgb_compat_obj_color(const CupidGb *gb, unsigned int palette, uint8_t shade)
{
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

static bool cupid_cgb_palette_blocked(const CupidGb *gb)
{
    uint8_t mode;

    if (gb == 0 || !gb->cgb_mode || !cupid_gb_lcd_enabled(gb) ||
        gb->io_registers[CUPID_GB_IO_LY] >= CUPID_GB_PPU_VISIBLE_SCANLINES) {
        return false;
    }

    mode = (uint8_t)(gb->io_registers[CUPID_GB_IO_STAT] & 0x03u);
    return mode == CUPID_GB_PPU_MODE_TRANSFER;
}

static uint8_t cupid_cgb_palette_read(const CupidGb *gb, bool object_palette)
{
    const uint8_t *palette_ram;
    uint8_t index;

    if (gb == 0 || !gb->cgb_mode || cupid_cgb_palette_blocked(gb)) {
        return 0xffu;
    }

    palette_ram = object_palette ? gb->cgb_obj_palette_ram : gb->cgb_bg_palette_ram;
    index = gb->io_registers[object_palette ? 0x6au : 0x68u] & 0x3fu;
    return palette_ram[index];
}

static void cupid_cgb_palette_write(CupidGb *gb, bool object_palette, uint8_t value)
{
    uint8_t *palette_ram;
    uint8_t index_register;
    uint8_t index;
    bool auto_increment;

    if (gb == 0 || !gb->cgb_mode) {
        return;
    }

    palette_ram = object_palette ? gb->cgb_obj_palette_ram : gb->cgb_bg_palette_ram;
    index_register = object_palette ? 0x6au : 0x68u;
    index = gb->io_registers[index_register] & 0x3fu;
    auto_increment = (gb->io_registers[index_register] & 0x80u) != 0u;

    if (!cupid_cgb_palette_blocked(gb)) {
        palette_ram[index] = value;
    }

    if (auto_increment) {
        gb->io_registers[index_register] = (uint8_t)((gb->io_registers[index_register] & 0x80u) |
                                                     ((index + 1u) & 0x3fu));
    }

    gb->io_registers[object_palette ? 0x6bu : 0x69u] = value;
}

static void cupid_cgb_hdma_copy_block(CupidGb *gb)
{
    uint8_t byte_index;

    if (gb == 0 || !gb->cgb_mode || gb->hdma_blocks_remaining == 0u) {
        return;
    }

    for (byte_index = 0u; byte_index < 0x10u; ++byte_index) {
        uint16_t source = (uint16_t)(gb->hdma_source + byte_index);
        uint16_t destination = (uint16_t)(gb->hdma_destination + byte_index);

        if (destination >= 0x8000u && destination <= 0x9fffu) {
            gb->video_ram[cupid_cgb_vram_offset(gb, destination)] = cupid_gb_read_u8(gb, source);
        }
    }

    gb->hdma_source = (uint16_t)(gb->hdma_source + 0x10u);
    gb->hdma_destination = (uint16_t)(gb->hdma_destination + 0x10u);
    gb->io_registers[0x51u] = (uint8_t)(gb->hdma_source >> 8u);
    gb->io_registers[0x52u] = (uint8_t)(gb->hdma_source & 0xf0u);
    gb->io_registers[0x53u] = (uint8_t)((gb->hdma_destination >> 8u) & 0x1fu);
    gb->io_registers[0x54u] = (uint8_t)(gb->hdma_destination & 0xf0u);
    gb->hdma_blocks_remaining = (uint8_t)(gb->hdma_blocks_remaining - 1u);
    if (gb->hdma_blocks_remaining == 0u) {
        gb->hdma_active = false;
        gb->io_registers[0x55u] = 0xffu;
    } else {
        gb->io_registers[0x55u] = gb->hdma_active
            ? (uint8_t)((gb->hdma_blocks_remaining - 1u) & 0x7fu)
            : 0xffu;
    }
}

bool cupid_cgb_handle_read_register(const CupidGb *gb, uint16_t address, uint8_t *value)
{
    if (value == 0) {
        return false;
    }

    switch (address) {
    case 0xff4cu:
    case 0xff56u:
        *value = gb != 0 && gb->cgb_mode ? gb->io_registers[address - 0xff00u] : 0xffu;
        return true;
    case 0xff4du:
        if (gb == 0 || !gb->cgb_mode) {
            *value = 0xffu;
        } else {
            *value = (uint8_t)(0x7eu | (gb->io_registers[0x4du] & 0x81u));
        }
        return true;
    case 0xff4fu:
        *value = gb != 0 && gb->cgb_mode ? (uint8_t)(0xfeu | (gb->cgb_vram_bank & 0x01u)) : 0xffu;
        return true;
    case 0xff51u:
    case 0xff52u:
    case 0xff53u:
    case 0xff54u:
    case 0xff55u:
        *value = gb != 0 && gb->cgb_mode ? gb->io_registers[address - 0xff00u] : 0xffu;
        return true;
    case 0xff68u:
        *value = gb != 0 && gb->cgb_mode ? (uint8_t)(0x40u | gb->io_registers[0x68u]) : 0xffu;
        return true;
    case 0xff69u:
        *value = cupid_cgb_palette_read(gb, false);
        return true;
    case 0xff6au:
        *value = gb != 0 && gb->cgb_mode ? (uint8_t)(0x40u | gb->io_registers[0x6au]) : 0xffu;
        return true;
    case 0xff6bu:
        *value = cupid_cgb_palette_read(gb, true);
        return true;
    case 0xff6cu:
        *value = gb != 0 && gb->cgb_mode ? (uint8_t)(0xfeu | (gb->io_registers[0x6cu] & 0x01u)) : 0xffu;
        return true;
    case 0xff70u:
        *value = gb != 0 && gb->cgb_mode ? (uint8_t)(0xf8u | (gb->cgb_wram_bank & 0x07u)) : 0xffu;
        return true;
    default:
        return false;
    }
}

bool cupid_cgb_handle_write_register(CupidGb *gb, uint16_t address, uint8_t value)
{
    if (gb == 0) {
        return false;
    }

    switch (address) {
    case 0xff4cu:
    case 0xff56u:
        if (gb->cgb_mode) {
            gb->io_registers[address - 0xff00u] = value;
        }
        return true;
    case 0xff4du:
        if (gb->cgb_mode) {
            gb->speed_switch_armed = (value & 0x01u) != 0u;
            gb->io_registers[0x4du] = (uint8_t)((gb->double_speed ? 0x80u : 0x00u) |
                                                (gb->speed_switch_armed ? 0x01u : 0x00u));
        }
        return true;
    case 0xff4fu:
        if (gb->cgb_mode) {
            gb->cgb_vram_bank = (uint8_t)(value & 0x01u);
            gb->io_registers[0x4fu] = (uint8_t)(0xfeu | gb->cgb_vram_bank);
        }
        return true;
    case 0xff51u:
    case 0xff52u:
    case 0xff53u:
    case 0xff54u:
        if (gb->cgb_mode) {
            gb->io_registers[address - 0xff00u] = value;
            gb->hdma_source = (uint16_t)(((uint16_t)gb->io_registers[0x51u] << 8u) |
                                         (uint16_t)(gb->io_registers[0x52u] & 0xf0u));
            gb->hdma_destination = (uint16_t)(0x8000u |
                                              (((uint16_t)gb->io_registers[0x53u] & 0x1fu) << 8u) |
                                              (uint16_t)(gb->io_registers[0x54u] & 0xf0u));
        }
        return true;
    case 0xff55u:
        if (gb->cgb_mode) {
            if (gb->hdma_active && (value & 0x80u) == 0u) {
                gb->hdma_active = false;
                gb->io_registers[0x55u] = (uint8_t)(0x80u | ((gb->hdma_blocks_remaining - 1u) & 0x7fu));
            } else {
                gb->hdma_source = (uint16_t)(((uint16_t)gb->io_registers[0x51u] << 8u) |
                                             (uint16_t)(gb->io_registers[0x52u] & 0xf0u));
                gb->hdma_destination = (uint16_t)(0x8000u |
                                                  (((uint16_t)gb->io_registers[0x53u] & 0x1fu) << 8u) |
                                                  (uint16_t)(gb->io_registers[0x54u] & 0xf0u));
                gb->hdma_blocks_remaining = (uint8_t)((value & 0x7fu) + 1u);
                gb->hdma_active = (value & 0x80u) != 0u;
                gb->io_registers[0x55u] = gb->hdma_active
                    ? (uint8_t)((gb->hdma_blocks_remaining - 1u) & 0x7fu)
                    : 0xffu;
                if (!gb->hdma_active) {
                    while (gb->hdma_blocks_remaining > 0u) {
                        cupid_cgb_hdma_copy_block(gb);
                    }
                }
            }
        }
        return true;
    case 0xff68u:
        if (gb->cgb_mode) {
            gb->io_registers[0x68u] = (uint8_t)(value & 0xbfu);
        }
        return true;
    case 0xff69u:
        cupid_cgb_palette_write(gb, false, value);
        return true;
    case 0xff6au:
        if (gb->cgb_mode) {
            gb->io_registers[0x6au] = (uint8_t)(value & 0xbfu);
        }
        return true;
    case 0xff6bu:
        cupid_cgb_palette_write(gb, true, value);
        return true;
    case 0xff6cu:
        if (gb->cgb_mode) {
            gb->io_registers[0x6cu] = (uint8_t)(0xfeu | (value & 0x01u));
        }
        return true;
    case 0xff70u:
        if (gb->cgb_mode) {
            gb->cgb_wram_bank = (uint8_t)(value & 0x07u);
            if (gb->cgb_wram_bank == 0u) {
                gb->cgb_wram_bank = 1u;
            }
            gb->io_registers[0x70u] = (uint8_t)(0xf8u | gb->cgb_wram_bank);
        }
        return true;
    default:
        return false;
    }
}

static uint16_t cupid_cgb_palette_color(const uint8_t *palette_ram, uint8_t palette_number, uint8_t color_index)
{
    size_t color_offset = (size_t)palette_number * 8u + (size_t)color_index * 2u;

    return (uint16_t)((uint16_t)palette_ram[color_offset] |
                      ((uint16_t)palette_ram[color_offset + 1u] << 8u));
}

void cupid_cgb_render_scanline(CupidGb *gb, uint8_t lcdc, uint8_t ly, size_t base_index)
{
    uint8_t bg_color_indices[CUPID_GB_SCREEN_WIDTH];
    uint8_t bg_priority[CUPID_GB_SCREEN_WIDTH];
    bool window_visible = false;
    uint8_t scx;
    uint8_t scy;
    uint8_t wy;
    uint8_t wx;
    unsigned int wx_screen;
    unsigned int x;

    if (gb == 0) {
        return;
    }

    scx = gb->io_registers[CUPID_GB_IO_SCX];
    scy = gb->io_registers[CUPID_GB_IO_SCY];
    wy = gb->io_registers[CUPID_GB_IO_WY];
    wx = gb->io_registers[CUPID_GB_IO_WX];
    wx_screen = (wx >= 7u) ? (unsigned int)(wx - 7u) : 0u;

    for (x = 0u; x < CUPID_GB_SCREEN_WIDTH; ++x) {
        bool use_window = false;
        size_t tile_map_base;
        size_t tile_map_index;
        uint8_t tile_number;
        uint8_t attributes;
        uint8_t pixel_x;
        uint8_t pixel_y;
        uint8_t tile_line;
        uint8_t bit_index;
        size_t tile_address;
        size_t tile_bank_base;
        uint8_t low;
        uint8_t high;
        uint8_t color_index;
        uint8_t palette_number;

        if ((lcdc & 0x20u) != 0u && (int)ly >= (int)wy &&
            wx_screen < CUPID_GB_SCREEN_WIDTH && x >= wx_screen) {
            use_window = true;
            window_visible = true;
        }

        if (use_window) {
            uint8_t win_x = (uint8_t)(x - wx_screen);
            uint8_t win_y = gb->window_line_counter;

            tile_map_base = (lcdc & 0x40u) != 0u ? 0x1c00u : 0x1800u;
            tile_map_index = tile_map_base + (size_t)((win_y >> 3u) * 32u) + (size_t)(win_x >> 3u);
            pixel_x = win_x;
            pixel_y = win_y;
        } else {
            uint8_t bg_x = (uint8_t)(x + scx);
            uint8_t bg_y = (uint8_t)(ly + scy);

            tile_map_base = (lcdc & 0x08u) != 0u ? 0x1c00u : 0x1800u;
            tile_map_index = tile_map_base + (size_t)((bg_y >> 3u) * 32u) + (size_t)(bg_x >> 3u);
            pixel_x = bg_x;
            pixel_y = bg_y;
        }

        tile_number = gb->video_ram[tile_map_index];
        attributes = gb->video_ram[0x2000u + tile_map_index];
        if ((attributes & 0x40u) != 0u) {
            pixel_y = (uint8_t)((pixel_y & 0xf8u) | (7u - (pixel_y & 0x07u)));
        }
        if ((attributes & 0x20u) != 0u) {
            pixel_x = (uint8_t)((pixel_x & 0xf8u) | (7u - (pixel_x & 0x07u)));
        }

        if ((lcdc & 0x10u) != 0u) {
            tile_address = (size_t)tile_number * 16u;
        } else {
            tile_address = (size_t)(0x1000 + ((int16_t)(int8_t)tile_number * 16));
        }

        tile_bank_base = ((attributes & 0x08u) != 0u) ? 0x2000u : 0u;
        tile_line = (uint8_t)((pixel_y & 0x07u) * 2u);
        low = gb->video_ram[tile_bank_base + tile_address + tile_line];
        high = gb->video_ram[tile_bank_base + tile_address + tile_line + 1u];
        bit_index = (uint8_t)(7u - (pixel_x & 0x07u));
        color_index = (uint8_t)((((high >> bit_index) & 0x01u) << 1u) |
                                ((low >> bit_index) & 0x01u));
        palette_number = (uint8_t)(attributes & 0x07u);

        gb->frame_buffer[base_index + x] = color_index;
        gb->frame_buffer_color[base_index + x] =
            cupid_cgb_palette_color(gb->cgb_bg_palette_ram, palette_number, color_index);
        bg_color_indices[x] = color_index;
        bg_priority[x] = (uint8_t)((attributes & 0x80u) != 0u);
    }

    if (window_visible) {
        gb->window_line_counter = (uint8_t)(gb->window_line_counter + 1u);
    }

    if ((lcdc & 0x02u) != 0u) {
        int obj_height = ((lcdc & 0x04u) != 0u) ? 16 : 8;
        unsigned int sprite_count = 0u;
        unsigned int i;
        bool sprite_drawn[CUPID_GB_SCREEN_WIDTH] = { false };

        for (i = 0u; i < 40u && sprite_count < 10u; ++i) {
            int obj_y = (int)gb->object_attribute_memory[i * 4u] - 16;
            int obj_x = (int)gb->object_attribute_memory[i * 4u + 1u] - 8;
            uint8_t tile_idx = gb->object_attribute_memory[i * 4u + 2u];
            uint8_t attrs = gb->object_attribute_memory[i * 4u + 3u];
            int tile_row;
            size_t tile_addr;
            size_t tile_bank_base;
            uint8_t olow;
            uint8_t ohigh;
            int px;

            if ((int)ly < obj_y || (int)ly >= obj_y + obj_height) {
                continue;
            }
            ++sprite_count;

            tile_row = (int)ly - obj_y;

            if (obj_height == 16) {
                if ((attrs & 0x40u) != 0u) {
                    tile_row = obj_height - 1 - tile_row;
                }
                if (tile_row >= 8) {
                    tile_idx = (uint8_t)(tile_idx | 0x01u);
                    tile_row -= 8;
                } else {
                    tile_idx = (uint8_t)(tile_idx & 0xfeu);
                }
            } else if ((attrs & 0x40u) != 0u) {
                tile_row = 7 - tile_row;
            }

            tile_bank_base = ((attrs & 0x08u) != 0u) ? 0x2000u : 0u;
            tile_addr = tile_bank_base + (size_t)tile_idx * 16u + (size_t)(tile_row * 2);
            olow = gb->video_ram[tile_addr];
            ohigh = gb->video_ram[tile_addr + 1u];

            for (px = 0; px < 8; ++px) {
                int screen_x;
                uint8_t bit_pos;
                uint8_t color_index;
                bool bg_wins;

                if ((attrs & 0x20u) != 0u) {
                    screen_x = obj_x + (7 - px);
                    bit_pos = (uint8_t)px;
                } else {
                    screen_x = obj_x + px;
                    bit_pos = (uint8_t)px;
                }

                if (screen_x < 0 || screen_x >= (int)CUPID_GB_SCREEN_WIDTH || sprite_drawn[screen_x]) {
                    continue;
                }

                color_index = (uint8_t)((((ohigh >> (7u - bit_pos)) & 0x01u) << 1u) |
                                        ((olow >> (7u - bit_pos)) & 0x01u));
                if (color_index == 0u) {
                    continue;
                }

                bg_wins = bg_color_indices[screen_x] != 0u &&
                          (lcdc & 0x01u) != 0u &&
                          (bg_priority[screen_x] != 0u || (attrs & 0x80u) != 0u);
                if (bg_wins) {
                    continue;
                }

                gb->frame_buffer_color[base_index + (size_t)screen_x] =
                    cupid_cgb_palette_color(gb->cgb_obj_palette_ram,
                                           (uint8_t)(attrs & 0x07u),
                                           color_index);
                sprite_drawn[screen_x] = true;
            }
        }
    }
}

void cupid_cgb_tick_hdma(CupidGb *gb)
{
    if (gb == 0 || !gb->hdma_active || gb->hdma_blocks_remaining == 0u) {
        return;
    }

    cupid_cgb_hdma_copy_block(gb);
}

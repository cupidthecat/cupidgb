/**
 * @file cgb.c
 * @brief Game Boy Color (CGB) hardware extension emulation.
 *
 * Covers all CGB-specific hardware that is layered on top of the DMG core:
 *   - VRAM banking (0xFF4F)
 *   - WRAM banking (0xFF70)
 *   - BG and OBJ CGB palette RAM (0xFF68–0xFF6B) with PPU-mode blocking
 *   - HDMA / GDMA transfers (0xFF51–0xFF55)
 *   - Double-speed mode preparation (0xFF4D)
 *   - CGB ppu scanline renderer (BG/Window tile attributes, sprite tile bank)
 *   - CGB compatibility palettes for DMG-only ROMs with Nintendo licensee codes
 *
 * Functions in this file are called from @ref gb.c memory map handlers and
 * the PPU tick in @ref ppu.c.
 */
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
static uint8_t cupid_cgb_title_fourth_letter(const CupidGb *gb)
{
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

/**
 * @brief Initializes all CGB-specific hardware state.
 *
 * Resets VRAM bank to 0, WRAM bank to 1, clears both palette RAMs
 * (setting all BG colors to white 0x7FFF), and initializes HDMA
 * source/destination registers. Also sets the polarity bits of several
 * I/O registers to their power-on values.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
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

/**
 * @brief Computes the linear VRAM array offset for a given VRAM address.
 *
 * In CGB mode, bit 0 of the VRAM bank register (0xFF4F) selects which
 * 8 KiB bank to access; in DMG mode bank 0 is always used.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address VRAM address in the range 0x8000–0x9FFF.
 *
 * @return Byte offset into `gb->video_ram`, in the range 0x0000–0x3FFF.
 */
size_t cupid_cgb_vram_offset(const CupidGb *gb, uint16_t address)
{
    size_t bank = 0u;

    if (gb != 0 && gb->cgb_mode) {
        bank = (size_t)(gb->cgb_vram_bank & 0x01u);
    }

    return bank * 0x2000u + (size_t)(address - 0x8000u);
}

/**
 * @brief Computes the linear WRAM array offset for a given WRAM address.
 *
 * The fixed bank at 0xC000–0xCFFF always maps to bank 0. In CGB mode
 * the switchable bank at 0xD000–0xDFFF is mapped according to the WRAM
 * bank register (0xFF70, values 1–7; 0 is treated as 1). In DMG mode
 * bank 1 is always used.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address WRAM address in the range 0xC000–0xDFFF.
 *
 * @return Byte offset into the internal WRAM buffer.
 */
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
bool cupid_cgb_compat_active(const CupidGb *gb)
{
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

/**
 * @brief Returns whether CGB palette RAM is currently inaccessible to the CPU.
 *
 * The palette RAM is blocked during PPU mode 3 (pixel transfer) when the
 * LCD is enabled, CGB mode is active, and the current scanline is within
 * the visible area (LY < 144).
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return `true` if palette RAM reads/writes should be blocked.
 */
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

/**
 * @brief Reads one byte from CGB BG or OBJ palette RAM via the index register.
 *
 * The index is taken from 0xFF68 (BG) or 0xFF6A (OBJ), bits 5–0.
 * Returns 0xFF if CGB mode is not active, @p gb is NULL, or the
 * palette RAM is blocked by the PPU.
 *
 * @param gb             Pointer to the Game Boy state.
 * @param object_palette `true` to read from OBJ palette RAM, `false` for BG.
 *
 * @return The byte at the selected palette RAM index.
 */
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
 * @note Does nothing if @p gb is NULL or CGB mode is not active.
 */
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

/**
 * @brief Copies one 16-byte HDMA block from the source address to VRAM.
 *
 * Reads 16 bytes beginning at `gb->hdma_source` via @ref cupid_gb_read_u8
 * and writes them to `gb->hdma_destination` (which must be in the VRAM
 * range 0x8000–0x9FFF). After the copy, both pointers are advanced by
 * 16 bytes. The I/O shadow registers (0xFF51–0xFF55) are updated and
 * `hdma_blocks_remaining` is decremented; when it reaches zero,
 * `hdma_active` is cleared and 0xFF55 is set to 0xFF.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL, CGB mode is inactive, or there
 *       are no remaining blocks.
 */
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

/**
 * @brief Handles a CPU read from a CGB-specific I/O register.
 *
 * Covers the following addresses:
 *   - 0xFF4C, 0xFF56 — CGB-mode-only registers (0xFF if DMG)
 *   - 0xFF4D — KEY1 speed switch register (read-back with polarity bits)
 *   - 0xFF4F — VRAM bank register (bits 7–1 forced to 1)
 *   - 0xFF51–0xFF55 — HDMA source/destination and control
 *   - 0xFF68, 0xFF6A — BG/OBJ palette index registers (bit 6 forced to 1)
 *   - 0xFF69, 0xFF6B — BG/OBJ palette data (via @ref cupid_cgb_palette_read)
 *   - 0xFF6C — Object priority mode (bits 7–1 forced to 1)
 *   - 0xFF70 — WRAM bank register (bits 7–3 forced to 1)
 *
 * Non-CGB-aware addresses return `false` so the caller can fall through
 * to DMG register handling.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address The I/O address being read (0xFF00–0xFFFF).
 * @param value   Output pointer filled with the register value on success.
 *
 * @return `true` if the address was handled, `false` otherwise.
 */
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

/**
 * @brief Handles a CPU write to a CGB-specific I/O register.
 *
 * Covers the following addresses:
 *   - 0xFF4C, 0xFF56 — CGB-mode-only registers
 *   - 0xFF4D — KEY1: arms the double-speed speed switch
 *   - 0xFF4F — VRAM bank select (bit 0 only)
 *   - 0xFF51–0xFF54 — HDMA source and destination bytes
 *   - 0xFF55 — HDMA/GDMA trigger: starts or cancels H-Blank DMA, or
 *              executes a general-purpose DMA immediately
 *   - 0xFF68 — BG palette index register
 *   - 0xFF69 — BG palette data (via @ref cupid_cgb_palette_write)
 *   - 0xFF6A — OBJ palette index register
 *   - 0xFF6B — OBJ palette data (via @ref cupid_cgb_palette_write)
 *   - 0xFF6C — Object priority mode bit
 *   - 0xFF70 — WRAM bank select (values 1–7; 0 treated as 1)
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address The I/O address being written (0xFF00–0xFFFF).
 * @param value   The byte value to write.
 *
 * @return `true` if the address was handled, `false` otherwise.
 *
 * @note Does nothing (returns `false`) if @p gb is NULL.
 */
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

/**
 * @brief Reads a single RGB555 color from a CGB palette RAM block.
 *
 * Returns the 16-bit little-endian color stored at the slot for
 * `palette_number` and `color_index` within @p palette_ram.
 *
 * @param palette_ram   Pointer to a 64-byte BG or OBJ palette RAM buffer.
 * @param palette_number Palette slot index (0–7).
 * @param color_index   Color index within the palette (0–3).
 *
 * @return The RGB555 color value.
 */
static uint16_t cupid_cgb_palette_color(const uint8_t *palette_ram, uint8_t palette_number, uint8_t color_index)
{
    size_t color_offset = (size_t)palette_number * 8u + (size_t)color_index * 2u;

    return (uint16_t)((uint16_t)palette_ram[color_offset] |
                      ((uint16_t)palette_ram[color_offset + 1u] << 8u));
}

/**
 * @brief Renders one CGB scanline into the frame buffer.
 *
 * Implements the full CGB background/window/sprite pipeline:
 *   - BG and Window tiles are fetched from the signed or unsigned tile
 *     data area (controlled by LCDC bit 4) using the tile attribute byte
 *     from VRAM bank 1 for flip flags, palette number, and tile bank.
 *   - The BG priority bit (attribute bit 7) and LCDC master priority
 *     (bit 0) are tracked per pixel to resolve BG-over-sprite conflicts.
 *   - Sprites read tile data from the bank selected by OAM attribute
 *     bit 3; color 0 is transparent; up to 10 sprites per scanline are
 *     processed with earlier OAM entries taking priority.
 *
 * Both `gb->frame_buffer` (shade indices) and `gb->frame_buffer_color`
 * (RGB555 colors from CGB palette RAM) are written.
 *
 * @param gb         Pointer to the Game Boy state.
 * @param lcdc       The LCDC register value for this scanline.
 * @param ly         The current scanline number (0–143).
 * @param base_index Starting offset into the frame buffers for this line
 *                   (`ly * CUPID_GB_SCREEN_WIDTH`).
 *
 * @note Does nothing if @p gb is NULL.
 */
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

/**
 * @brief Advances H-Blank DMA by one 16-byte block.
 *
 * Called once per H-Blank period (from the PPU tick) while an H-Blank
 * DMA transfer is active. Delegates to @ref cupid_cgb_hdma_copy_block
 * to transfer the next 16-byte block.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL, HDMA is not active, or there are
 *       no remaining blocks.
 */
void cupid_cgb_tick_hdma(CupidGb *gb)
{
    if (gb == 0 || !gb->hdma_active || gb->hdma_blocks_remaining == 0u) {
        return;
    }

    cupid_cgb_hdma_copy_block(gb);
}

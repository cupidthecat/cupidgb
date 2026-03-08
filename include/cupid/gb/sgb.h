#ifndef CUPID_GB_SGB_H
#define CUPID_GB_SGB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CUPID_SGB_SCREEN_WIDTH   256u
#define CUPID_SGB_SCREEN_HEIGHT  224u
#define CUPID_SGB_VIEWPORT_X      48u
#define CUPID_SGB_VIEWPORT_Y      40u
#define CUPID_SGB_TILE_WIDTH      32u
#define CUPID_SGB_TILE_HEIGHT     28u
#define CUPID_SGB_ATTR_WIDTH      20u
#define CUPID_SGB_ATTR_HEIGHT     18u
#define CUPID_SGB_PACKET_BYTES    16u
#define CUPID_SGB_MAX_PACKETS      7u
#define CUPID_SGB_MAX_COMMAND_BYTES (CUPID_SGB_PACKET_BYTES * CUPID_SGB_MAX_PACKETS)

enum {
    CUPID_SGB_MASK_DISABLED = 0u,
    CUPID_SGB_MASK_FREEZE = 1u,
    CUPID_SGB_MASK_BLACK = 2u,
    CUPID_SGB_MASK_COLOR0 = 3u
};

typedef struct CupidGbSgb {
    bool enabled;
    uint8_t player_count;
    uint8_t current_player;
    uint8_t mask_mode;

    uint8_t command[CUPID_SGB_MAX_COMMAND_BYTES];
    uint16_t command_write_index;
    bool ready_for_pulse;
    bool ready_for_write;
    bool ready_for_stop;

    uint8_t attribute_map[CUPID_SGB_ATTR_WIDTH * CUPID_SGB_ATTR_HEIGHT];
    uint8_t attribute_files[0x0fd2];
    uint16_t screen_palettes[4u * 4u];
    uint16_t ram_palettes[512u * 4u];

    uint8_t border_tiles[256u * 32u];
    uint16_t border_map[CUPID_SGB_TILE_WIDTH * CUPID_SGB_TILE_HEIGHT];
    uint16_t border_palettes[8u * 16u];

    uint8_t freeze_buffer[160u * 144u];
} CupidGbSgb;

struct CupidGb;

void cupid_gb_sgb_init(struct CupidGb *gb);
void cupid_gb_sgb_apply_compatibility_palette(struct CupidGb *gb);
void cupid_gb_sgb_write_joyp(struct CupidGb *gb, uint8_t value);
void cupid_gb_sgb_render_argb(const struct CupidGb *gb, uint32_t *pixels, size_t pixel_count);
bool cupid_gb_sgb_active(const struct CupidGb *gb);

#endif

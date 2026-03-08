#ifndef CUPID_GBC_CGB_H
#define CUPID_GBC_CGB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct CupidGb;

void cupid_cgb_init_state(struct CupidGb *gb);
size_t cupid_cgb_vram_offset(const struct CupidGb *gb, uint16_t address);
size_t cupid_cgb_wram_offset(const struct CupidGb *gb, uint16_t address);
bool cupid_cgb_get_compatibility_palette(const struct CupidGb *gb,
										 uint16_t bg[4],
										 uint16_t obj0[4],
										 uint16_t obj1[4]);
void cupid_cgb_apply_compatibility_palette(struct CupidGb *gb);
bool cupid_cgb_compat_active(const struct CupidGb *gb);
uint16_t cupid_cgb_compat_bg_color(const struct CupidGb *gb, uint8_t shade);
uint16_t cupid_cgb_compat_obj_color(const struct CupidGb *gb, unsigned int palette, uint8_t shade);
bool cupid_cgb_handle_read_register(const struct CupidGb *gb, uint16_t address, uint8_t *value);
bool cupid_cgb_handle_write_register(struct CupidGb *gb, uint16_t address, uint8_t value);
void cupid_cgb_render_scanline(struct CupidGb *gb, uint8_t lcdc, uint8_t ly, size_t base_index);
void cupid_cgb_tick_hdma(struct CupidGb *gb);

#endif

#ifndef CUPID_GBC_INTERNAL_H
#define CUPID_GBC_INTERNAL_H

#include "cupid/gbc/cgb.h"

uint8_t cupid_cgb_palette_read(const struct CupidGb *gb, bool object_palette);
void cupid_cgb_palette_write(struct CupidGb *gb, bool object_palette, uint8_t value);
uint8_t cupid_cgb_dma_status(const struct CupidGb *gb);

#endif

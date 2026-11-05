#ifndef CUPID_GB_INPUT_H
#define CUPID_GB_INPUT_H

#include "cupid/gb/gb.h"

/* Bits 0..7 are Right, Left, Up, Down, A, B, Select, Start; zero means pressed.
 * Use this entry point instead of writing joypad directly so selected-line
 * transitions also request the hardware interrupt. */
void cupid_gb_set_buttons(CupidGb *gb, uint8_t released_mask);

/* Memory-map helpers. Physical contact bounce and monochrome row settling are
 * not modeled here; the selected rows are evaluated immediately. */
uint8_t cupid_gb_input_read(const CupidGb *gb);
void cupid_gb_input_select(CupidGb *gb, uint8_t value);

#endif

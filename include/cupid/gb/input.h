#ifndef CUPID_GB_INPUT_H
#define CUPID_GB_INPUT_H

#include "cupid/gb/gb.h"

/* Bits 0..7 are Right, Left, Up, Down, A, B, Select, Start; zero means pressed.
 * Use this entry point instead of writing joypad directly so selected-line
 * transitions also request the hardware interrupt. */
void cupid_gb_set_buttons(CupidGb *gb, uint8_t released_mask);
/* Players are numbered 0..3. Additional pads are selected by the SGB protocol;
 * changing an unselected pad does not produce a selected-line interrupt. */
bool cupid_gb_set_buttons_for_player(CupidGb *gb, unsigned player, uint8_t released_mask);

/* Memory-map helpers. Monochrome row release is delayed while select-bit
 * readback changes immediately. Physical contact bounce is not modeled. */
uint8_t cupid_gb_input_read(const CupidGb *gb);
void cupid_gb_input_select(CupidGb *gb, uint8_t value);
/* Advance row settling by base-clock dots, including during HALT and STOP. */
void cupid_gb_input_tick(CupidGb *gb, unsigned base_cycles);

#endif

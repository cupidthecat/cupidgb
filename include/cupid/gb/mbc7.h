#ifndef CUPID_GB_MBC7_H
#define CUPID_GB_MBC7_H

#include "cupid/gb/gb.h"

enum { CUPID_GB_MBC7_EEPROM_SIZE = 256 };

/* Supply host motion relative to the neutral reading. Approximately 112
 * counts correspond to one g. Only samples latched by the guest enter saves. */
void cupid_gb_mbc7_set_motion(CupidGb *gb, int16_t x, int16_t y);

/* Internal cartridge bus entry points. */
void cupid_gb_mbc7_reset(CupidGb *gb);
void cupid_gb_mbc7_reset_serial(CupidGb *gb);
uint8_t cupid_gb_mbc7_read(const CupidGb *gb, uint16_t address);
void cupid_gb_mbc7_write(CupidGb *gb, uint16_t address, uint8_t value);

#endif

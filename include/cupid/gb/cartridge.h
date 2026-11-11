#ifndef CUPID_GB_CARTRIDGE_H
#define CUPID_GB_CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cupid/gb/gb.h"

enum { CUPID_GB_ENTRY_POINT = 0x0100 };

size_t cupid_gb_ram_size_from_code(uint8_t code);
size_t cupid_gb_effective_rom_bank(const CupidGb *gb, bool lower_region);
/* Controller-selected bank before physical ROM address-line aliasing. */
size_t cupid_gb_selected_rom_bank(const CupidGb *gb, bool lower_region);
size_t cupid_gb_selected_ram_bank(const CupidGb *gb);
size_t cupid_gb_effective_ram_bank(const CupidGb *gb);
/* Extended MBC3 wiring exposes eight ROM address bits and eight RAM banks. */
bool cupid_gb_has_extended_mbc3(const CupidGb *gb);
/* Dispatch only HuC1 cartridge control writes below 0x8000. */
void cupid_gb_huc1_write(CupidGb *gb, uint16_t address, uint8_t value);
/* Dispatch only HuC3 cartridge control writes below 0x8000. */
void cupid_gb_huc3_write(CupidGb *gb, uint16_t address, uint8_t value);
void cupid_gb_mmm01_write(CupidGb *gb, uint16_t address, uint8_t value);

#endif

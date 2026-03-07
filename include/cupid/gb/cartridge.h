#ifndef CUPID_GB_CARTRIDGE_H
#define CUPID_GB_CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cupid/gb/gb.h"

enum {
    CUPID_GB_ENTRY_POINT = 0x0100
};

size_t cupid_gb_effective_rom_bank(const CupidGb *gb, bool lower_region);
size_t cupid_gb_effective_ram_bank(const CupidGb *gb);

#endif

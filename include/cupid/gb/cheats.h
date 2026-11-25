/**
 * @file    cheats.h
 * @brief   Cheat records and public parsing, application, and persistence interfaces.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#ifndef CUPID_GB_CHEATS_H
#define CUPID_GB_CHEATS_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { CUPID_GB_CHEAT_ANY_BANK = 0xffff, CUPID_GB_CHEAT_DESCRIPTION_SIZE = 128 };

typedef struct CupidGbCheat {
    uint16_t address, bank;
    uint8_t value, compare_value;
    bool use_compare, enabled;
    char description[CUPID_GB_CHEAT_DESCRIPTION_SIZE];
} CupidGbCheat;

typedef enum CupidGbCheatResult {
    CUPID_GB_CHEAT_OK,
    CUPID_GB_CHEAT_INVALID_ARGUMENT,
    CUPID_GB_CHEAT_INVALID_DATA,
    CUPID_GB_CHEAT_BUFFER_TOO_SMALL,
    CUPID_GB_CHEAT_OUT_OF_MEMORY
} CupidGbCheatResult;

/* The host owns the collection. Detach it before destruction. Mutations and
 * binding require an instruction boundary without concurrent core access.
 * Cheats survive a core reset and native state loading, outside the snapshot. */
CupidGbCheats *cupid_gb_cheats_create(void);
/* Preserve both presentation order and read precedence during host edits. */
CupidGbCheats *cupid_gb_cheats_clone(const CupidGbCheats *cheats);
void cupid_gb_cheats_destroy(CupidGbCheats *cheats);
void cupid_gb_set_cheats(CupidGb *gb, const CupidGbCheats *cheats);
void cupid_gb_cheats_clear(CupidGbCheats *cheats);
size_t cupid_gb_cheats_count(const CupidGbCheats *cheats);
const CupidGbCheat *cupid_gb_cheats_get(const CupidGbCheats *cheats, size_t index);
bool cupid_gb_cheats_enabled(const CupidGbCheats *cheats);
void cupid_gb_cheats_set_enabled(CupidGbCheats *cheats, bool enabled);
CupidGbCheatResult cupid_gb_cheats_add(CupidGbCheats *cheats, const CupidGbCheat *cheat);
CupidGbCheatResult cupid_gb_cheats_update(CupidGbCheats *cheats, size_t index,
                                          const CupidGbCheat *cheat);
bool cupid_gb_cheats_remove(CupidGbCheats *cheats, size_t index);

/* Indices and returned record pointers can change after any mutation. Import
 * accepts Game Genie and GameShark codes, with case-insensitive hexadecimal. */
bool cupid_gb_cheat_decode(const char *code, const char *description, bool enabled,
                           CupidGbCheat *cheat);
CupidGbCheatResult cupid_gb_cheats_import(CupidGbCheats *cheats, const char *code,
                                          const char *description, bool enabled);

/* .cht bytes use an eight-byte header followed by 136-byte records. The codec
 * imports either byte order and writes little endian. Failed decoding leaves
 * the collection intact. Global enable is a host setting, outside the file. */
size_t cupid_gb_cheats_encoded_size(const CupidGbCheats *cheats);
CupidGbCheatResult cupid_gb_cheats_encode(const CupidGbCheats *cheats, uint8_t *data,
                                          size_t capacity, size_t *written);
CupidGbCheatResult cupid_gb_cheats_decode(CupidGbCheats *cheats, const uint8_t *data, size_t size,
                                          bool replace_existing);

/* Read interception preserves ROM/RAM bytes and applies the first matching
 * enabled record, after bank mapping and before the cartridge bus is driven. */
uint8_t cupid_gb_cheats_apply(const CupidGb *gb, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif

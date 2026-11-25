/**
 * @file    oam_bus.c
 * @brief   Monochrome object-memory bus conflicts and scan-row corruption.
 * @author  cupidthecat
 * @date    2026-11-18
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

#include "oam_bus.h"
#include "cupid/gb/ppu.h"
#include "display_clock.h"
#include <string.h>

static size_t extra_oam_index(CupidGbModel model, uint8_t address) {
    const unsigned offset = address - 0xa0u;
    if (model == CUPID_GB_MODEL_CGB_D)
        return offset < 32u ? offset : 32u + (offset & 15u);
    /* Each 32-address block on revisions 0 through C selects eight cells. */
    return (offset >> 5) * 8u + (offset & 7u);
}

uint8_t cupid_gb_extra_oam_read(const CupidGb *gb, uint8_t address) {
    if (gb->model == CUPID_GB_MODEL_CGB_E)
        return (uint8_t)((address & 0xf0u) | (address >> 4));
    if (cupid_gb_extra_oam_size(gb->model))
        return gb->extra_oam[extra_oam_index(gb->model, address)];
    return 0;
}

void cupid_gb_extra_oam_write(CupidGb *gb, uint8_t address, uint8_t value) {
    if (cupid_gb_extra_oam_size(gb->model))
        gb->extra_oam[extra_oam_index(gb->model, address)] = value;
}

// DMG OAM corruption bug

/**
 * @brief Checks whether a DMG OAM corruption glitch would be triggered.
 *
 * Monochrome hardware exposes OAM row conflicts while an actual search is
 * active and the address falls in 0xFE00–0xFEFF. The initial LCD scanline has
 * no OAM search. Physical color hardware stays unaffected in compatibility mode.
 *
 * @param gb          Pointer to the Game Boy state.
 * @param address     The address being accessed.
 * @param row_offset  If non-NULL and the bug is active, receives the byte
 *                    offset of the current OAM row being scanned.
 *
 * @return `true` if the OAM bug is active for this access, `false` otherwise.
 *
 * @note Always returns `false` on color hardware or when the LCD is disabled.
 */
static bool cupid_gb_oam_bug_active(CupidGb *gb, uint16_t address, uint16_t *row_offset) {
    uint8_t ly;

    /* Increment and decrement bus pulses can synchronize OAM without an
     * ordinary memory access, including when no corruption follows. */
    if (gb && !cupid_gb_model_is_cgb(gb->model) && address >= 0xfe00u && address < 0xff00u)
        cupid_gb_display_clock_sync(gb);

    if (gb == 0 || cupid_gb_model_is_cgb(gb->model) || !cupid_gb_lcd_enabled(gb) ||
        gb->ppu_lcd_startup) {
        return false;
    }

    if (address < 0xfe00u || address >= 0xff00u) {
        return false;
    }

    ly = gb->pixels.initialized ? gb->pixels.line : gb->io_registers[CUPID_GB_IO_LY];
    const unsigned dot = gb->pixels.initialized ? gb->pixels.dot : gb->ppu_counter * 4u;
    if (ly >= CUPID_GB_PPU_VISIBLE_SCANLINES || dot > 80u) {
        return false;
    }

    if (row_offset != 0) {
        /* Two objects share one eight-byte row. The search word is sampled
         * halfway through the four-dot row slot, independently of STAT. */
        *row_offset = (uint16_t)(((dot + 1u) / 4u) * 8u);
    }
    return true;
}

/**
 * @brief Reads a 16-bit word from OAM at a given row and word index.
 *
 * @param gb         Pointer to the Game Boy state.
 * @param row_offset Byte offset of the OAM row.
 * @param word_index 0-based index of the 16-bit word within the row.
 *
 * @return The 16-bit little-endian value at that position.
 */
static uint16_t cupid_gb_oam_bug_get_word(const CupidGb *gb, size_t row_offset,
                                          uint8_t word_index) {
    size_t offset = (size_t)row_offset + (size_t)word_index * 2u;

    return (uint16_t)((uint16_t)gb->object_attribute_memory[offset] |
                      (uint16_t)((uint16_t)gb->object_attribute_memory[offset + 1u] << 8u));
}

/**
 * @brief Writes a 16-bit word into OAM at a given row and word index.
 *
 * @param gb         Pointer to the Game Boy state.
 * @param row_offset Byte offset of the OAM row.
 * @param word_index 0-based index of the 16-bit word within the row.
 * @param value      The value to write (little-endian).
 */
static void cupid_gb_oam_bug_set_word(CupidGb *gb, size_t row_offset, uint8_t word_index,
                                      uint16_t value) {
    size_t offset = (size_t)row_offset + (size_t)word_index * 2u;

    gb->object_attribute_memory[offset] = (uint8_t)(value & 0x00ffu);
    gb->object_attribute_memory[offset + 1u] = (uint8_t)(value >> 8u);
}

/**
 * @brief Applies the DMG OAM corruption pattern for a write access.
 *
 * When triggered, corrupts the current OAM scan row using a bitwise
 * combination of the current, previous, and two-rows-back row data,
 * then copies a portion of the prior row into the current row.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address The address involved in the write.
 */
void cupid_gb_trigger_oam_bug_write(CupidGb *gb, uint16_t address) {
    uint16_t row_offset;
    uint16_t a;
    uint16_t b;
    uint16_t c;
    size_t index;

    if (!cupid_gb_oam_bug_active(gb, address, &row_offset) || row_offset < 8u ||
        row_offset >= 0x00a0u) {
        return;
    }

    a = cupid_gb_oam_bug_get_word(gb, row_offset, 0u);
    b = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 8u), 0u);
    c = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 4u), 0u);

    cupid_gb_oam_bug_set_word(gb, row_offset, 0u, (uint16_t)(((a ^ c) & (b ^ c)) ^ c));
    for (index = 0u; index < 6u; ++index) {
        gb->object_attribute_memory[(size_t)row_offset + 2u + index] =
            gb->object_attribute_memory[(size_t)row_offset - 6u + index];
    }
}

/**
 * @brief Public wrapper for the DMG OAM write corruption bug.
 *
 * Called by the memory subsystem when a write to the OAM region occurs
 * during Mode 2 (OAM scan) on DMG hardware.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address The address being written.
 */
void cupid_gb_trigger_oam_bug_write_access(CupidGb *gb, uint16_t address) {
    cupid_gb_trigger_oam_bug_write(gb, address);
}

/* Reads couple adjacent scan rows. Capture every input before changing the
 * shared row, then propagate its complete eight-byte contents. The corruption
 * circuit is selected by physical hardware even for monochrome cartridges. */
void cupid_gb_trigger_oam_bug_read_access(CupidGb *gb, uint16_t address) {
    uint16_t offset;
    if (!cupid_gb_oam_bug_active(gb, address, &offset))
        return;

    /* The scan's edge slots expose an address-selected row while ordinary
     * writes are already available. Unused-tail reads have no storage row. */
    if (offset == 0 || offset == 160u) {
        if (address >= 0xfea0u)
            return;
        unsigned selected = address & 0xf8u;
        if (offset == 0) {
            uint16_t first = cupid_gb_oam_bug_get_word(gb, 0, 0);
            uint16_t source = cupid_gb_oam_bug_get_word(gb, selected, 0);
            uint16_t addressed = cupid_gb_oam_bug_get_word(gb, address & 0xfeu, 0);
            uint16_t value = (uint16_t)(source | (first & addressed));
            cupid_gb_oam_bug_set_word(gb, 0, 0, value);
            cupid_gb_oam_bug_set_word(gb, selected, 0, value);
            memmove(gb->object_attribute_memory + 2, gb->object_attribute_memory + selected + 2, 6);
        } else {
            unsigned target = 152u + (address & 6u);
            uint16_t a = cupid_gb_oam_bug_get_word(gb, 156, 0);
            uint16_t b = cupid_gb_oam_bug_get_word(gb, target, 0);
            uint16_t c = cupid_gb_oam_bug_get_word(gb, selected, 0);
            unsigned column = address & 6u;
            if (column == 2u)
                c = cupid_gb_oam_bug_get_word(gb, address & 0xfeu, 0);
            if (column != 4u) {
                bool majority =
                    column == 2u || (column == 0u && gb->model != CUPID_GB_MODEL_MGB &&
                                     cupid_gb_model_base(gb->model) != CUPID_GB_MODEL_SGB2);
                uint16_t value =
                    majority ? (uint16_t)((a & b) | (b & c) | (a & c)) : (uint16_t)(b | (a & c));
                cupid_gb_oam_bug_set_word(gb, target, 0, value);
            }
            memmove(gb->object_attribute_memory + selected, gb->object_attribute_memory + 152, 8);
        }
        return;
    }

    uint8_t *memory = gb->object_attribute_memory;
    unsigned row = offset / 8u;
    uint16_t current = cupid_gb_oam_bug_get_word(gb, offset, 0);
    uint16_t previous = cupid_gb_oam_bug_get_word(gb, offset - 8u, 0);
    uint16_t middle = cupid_gb_oam_bug_get_word(gb, offset - 4u, 0);
    uint16_t merged = previous;

    if ((row & 3u) == 2u) {
        if (offset < 152u) {
            uint16_t older = cupid_gb_oam_bug_get_word(gb, offset - 16u, 0);
            merged =
                (uint16_t)((previous & (older | current | middle)) | (older & current & middle));
            cupid_gb_oam_bug_set_word(gb, offset - 8u, 0, merged);
            memmove(memory + offset - 16u, memory + offset - 8u, 8);
        }
    } else if ((row & 3u) == 0u) {
        uint16_t older = cupid_gb_oam_bug_get_word(gb, offset - 16u, 0);
        uint16_t distant = cupid_gb_oam_bug_get_word(gb, offset - 32u, 0);
        uint16_t all = current & middle & older & distant;
        uint16_t any = current | middle | older | distant;
        if (gb->model == CUPID_GB_MODEL_MGB ||
            (offset == 96u && cupid_gb_model_base(gb->model) != CUPID_GB_MODEL_SGB2)) {
            merged = (uint16_t)((previous & any) | (middle & older & distant));
        } else if (offset == 64u) {
            uint16_t first = cupid_gb_oam_bug_get_word(gb, 0, 0);
            uint16_t near = cupid_gb_oam_bug_get_word(gb, offset - 6u, 0);
            uint16_t far = cupid_gb_oam_bug_get_word(gb, offset - 14u, 0);
            if (cupid_gb_model_base(gb->model) == CUPID_GB_MODEL_SGB2) {
                merged =
                    (uint16_t)((previous & (distant | older | middle | (first & current))) |
                               ((middle & older & distant) & (current | first | (uint16_t)~far)));
            } else {
                merged = (uint16_t)((previous & (distant | older | middle | current |
                                                 ((uint16_t)~near & far))) |
                                    (middle & older & distant));
            }
        } else if (offset == 32u || cupid_gb_model_base(gb->model) == CUPID_GB_MODEL_SGB2) {
            merged = (uint16_t)((previous & any) | all);
        } else {
            merged = (uint16_t)(previous | all);
        }
        cupid_gb_oam_bug_set_word(gb, offset - 8u, 0, merged);
        memmove(memory + offset - 16u, memory + offset - 8u, 8);
        memmove(memory + offset - 32u, memory + offset - 8u, 8);
    } else {
        merged = (uint16_t)(previous | (current & middle));
        cupid_gb_oam_bug_set_word(gb, offset - 8u, 0, merged);
    }
    memmove(memory + offset, memory + offset - 8u, 8);
    if (offset == 128u || (offset == 64u && gb->model == CUPID_GB_MODEL_MGB))
        memmove(memory, memory + offset, 8);
}

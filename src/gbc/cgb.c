/**
 * @file    cgb.c
 * @brief   Color hardware reset, bank addressing, and register dispatch.
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

/* Color hardware reset, bank addressing and register dispatch. */
#include "cupid/gbc/cgb.h"

#include "cgb_internal.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/infrared.h"
#include "cupid/gb/ppu.h"

#include <string.h>

/**
 * @brief Initializes all CGB-specific hardware state.
 *
 * Resets VRAM bank to 0, WRAM bank to 1, clears both palette RAMs
 * (setting all BG colors to white 0x7FFF), and initializes HDMA
 * source/destination registers. Also sets the polarity bits of several
 * I/O registers to their power-on values.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_cgb_init_state(CupidGb *gb) {
    size_t palette_index;

    if (gb == 0) {
        return;
    }

    gb->cgb_vram_bank = 0u;
    gb->cgb_wram_bank = 1u;
    gb->cgb_object_priority_x = false;
    memset(gb->cgb_bg_palette_ram, 0u, sizeof(gb->cgb_bg_palette_ram));
    memset(gb->cgb_obj_palette_ram, 0u, sizeof(gb->cgb_obj_palette_ram));
    for (palette_index = 0u; palette_index < sizeof(gb->cgb_bg_palette_ram); palette_index += 2u) {
        gb->cgb_bg_palette_ram[palette_index] = 0xffu;
        gb->cgb_bg_palette_ram[palette_index + 1u] = 0x7fu;
    }
    gb->hdma_source = 0u;
    gb->hdma_destination = 0x8000u;
    gb->hdma_blocks_remaining = 0u;
    gb->hdma_active = false;
    gb->hdma_pending = false;
    gb->hdma_hblank_delay = 0;
    gb->hdma_allow_on_wake = false;
    gb->io_registers[0x4fu] = 0xfeu;
    gb->io_registers[0x4cu] = 0u;
    gb->io_registers[0x68u] = 0x00u;
    gb->io_registers[0x69u] = 0xffu;
    gb->io_registers[0x6au] = 0x00u;
    gb->io_registers[0x6bu] = 0xffu;
    gb->io_registers[0x6cu] = 0xfeu;
    gb->io_registers[0x70u] = 0xf9u;
}

/**
 * @brief Computes the linear VRAM array offset for a given VRAM address.
 *
 * Physical CGB hardware retains its selected 8 KiB bank when KEY0 changes
 * cartridge compatibility mode. Other hardware always uses bank 0.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address VRAM address in the range 0x8000–0x9FFF.
 *
 * @return Byte offset into `gb->video_ram`, in the range 0x0000–0x3FFF.
 */
size_t cupid_cgb_vram_offset(const CupidGb *gb, uint16_t address) {
    size_t bank = 0u;

    if (gb != 0 && cupid_gb_model_is_cgb(gb->model)) {
        bank = (size_t)(gb->cgb_vram_bank & 0x01u);
    }

    return bank * 0x2000u + (size_t)(address - 0x8000u);
}

/**
 * @brief Computes the linear WRAM array offset for a given WRAM address.
 *
 * The fixed bank at 0xC000–0xCFFF always maps to bank 0. Physical CGB hardware
 * maps 0xD000–0xDFFF through the retained WRAM bank latch (0 selects bank 1),
 * including after KEY0 changes mode or FF50 closes the boot overlay. Other
 * hardware always uses bank 1.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address WRAM address in the range 0xC000–0xDFFF.
 *
 * @return Byte offset into the internal WRAM buffer.
 */
size_t cupid_cgb_wram_offset(const CupidGb *gb, uint16_t address) {
    if (address >= 0xc000u && address <= 0xcfffu) {
        return (size_t)(address - 0xc000u);
    }

    if (gb != 0 && cupid_gb_model_is_cgb(gb->model)) {
        uint8_t bank = gb->cgb_wram_bank & 0x07u;

        if (bank == 0u) {
            bank = 1u;
        }

        return (size_t)bank * 0x1000u + (size_t)(address - 0xd000u);
    }

    return 0x1000u + (size_t)(address - 0xd000u);
}

/**
 * @brief Handles a CPU read from a CGB-specific I/O register.
 *
 * Covers the following addresses:
 *   - 0xFF4C, 0xFF56 — CGB-mode-only registers (0xFF if DMG)
 *   - 0xFF4D — KEY1 speed switch register (read-back with polarity bits)
 *   - 0xFF4F — VRAM bank register (bits 7–1 forced to 1)
 *   - 0xFF51–0xFF55 — HDMA source/destination and control
 *   - 0xFF68, 0xFF6A — BG/OBJ palette index registers (bit 6 forced to 1)
 *   - 0xFF69, 0xFF6B — BG/OBJ palette data (via @ref cupid_cgb_palette_read)
 *   - 0xFF6C — Object priority mode (bits 7–1 forced to 1)
 *   - 0xFF70 — WRAM bank latch (writes set bits 7–3)
 *
 * Non-CGB-aware addresses return `false` so the caller can fall through
 * to DMG register handling.
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address The I/O address being read (0xFF00–0xFFFF).
 * @param value   Output pointer filled with the register value on success.
 *
 * @return `true` if the address was handled, `false` otherwise.
 */
bool cupid_cgb_handle_read_register(const CupidGb *gb, uint16_t address, uint8_t *value) {
    if (value == 0) {
        return false;
    }

    switch (address) {
        case 0xff4cu:
        case 0xff51u:
        case 0xff52u:
        case 0xff53u:
        case 0xff54u:
            /* Control and DMA address registers are write-only on the bus. */
            *value = 0xffu;
            return true;
        case 0xff56u: {
            *value = 0xffu;
            if (gb == 0 || !gb->cgb_mode)
                return true;
            const uint8_t port = gb->io_registers[0x56u];
            const unsigned fixed_bits = gb->model == CUPID_GB_MODEL_CGB_E ? 0x2eu : 0x3eu;
            *value = (uint8_t)(fixed_bits | (port & 0xc1u));
            if ((port & 0xc0u) == 0xc0u && cupid_gb_infrared_detected(gb))
                *value &= 0xfdu;
            return true;
        }
        case 0xff4du:
            if (gb == 0 || !gb->cgb_mode) {
                *value = 0xffu;
            } else {
                *value = (uint8_t)(0x7eu | (gb->io_registers[0x4du] & 1u) |
                                   (gb->double_speed ? 0x80u : 0));
            }
            return true;
        case 0xff4fu:
            *value = gb != 0 && cupid_gb_model_is_cgb(gb->model)
                         ? (uint8_t)(0xfeu | (gb->cgb_vram_bank & 0x01u))
                         : 0xffu;
            return true;
        case 0xff55u:
            *value = gb != 0 && gb->cgb_mode ? cupid_cgb_dma_status(gb) : 0xffu;
            return true;
        case 0xff68u:
            *value = gb != 0 && cupid_gb_model_is_cgb(gb->model)
                         ? (uint8_t)(0x40u | gb->io_registers[0x68u])
                         : 0xffu;
            return true;
        case 0xff69u:
            *value = cupid_cgb_palette_read(gb, false);
            return true;
        case 0xff6au:
            *value = gb != 0 && cupid_gb_model_is_cgb(gb->model)
                         ? (uint8_t)(0x40u | gb->io_registers[0x6au])
                         : 0xffu;
            return true;
        case 0xff6bu:
            *value = cupid_cgb_palette_read(gb, true);
            return true;
        case 0xff6cu:
            *value = gb != 0 && cupid_gb_model_is_cgb(gb->model)
                         ? (uint8_t)(0xfeu | (gb->io_registers[0x6cu] & 0x01u))
                         : 0xffu;
            return true;
        case 0xff70u:
            /* Cold boot retains zero here until the first write sets the unused bits. */
            *value = gb != 0 && gb->cgb_mode ? gb->io_registers[0x70u] : 0xffu;
            return true;
        case 0xff72u:
        case 0xff73u:
            *value = gb != 0 && cupid_gb_model_is_cgb(gb->model)
                         ? gb->io_registers[address - 0xff00u]
                         : 0xffu;
            return true;
        case 0xff74u:
            *value = gb != 0 && gb->cgb_mode ? gb->io_registers[0x74u] : 0xffu;
            return true;
        case 0xff75u:
            *value = gb != 0 && cupid_gb_model_is_cgb(gb->model)
                         ? (uint8_t)(0x8fu | gb->io_registers[0x75u])
                         : 0xffu;
            return true;
        case 0xff76u:
            *value = cupid_gb_apu_read_pcm12(gb);
            return true;
        case 0xff77u:
            *value = cupid_gb_apu_read_pcm34(gb);
            return true;
        default:
            return false;
    }
}

/**
 * @brief Handles a CPU write to a CGB-specific I/O register.
 *
 * Covers the following addresses:
 *   - 0xFF4C, 0xFF56 — CGB-mode-only registers
 *   - 0xFF4D — KEY1: arms the double-speed speed switch
 *   - 0xFF4F — VRAM bank select (bit 0 only)
 *   - 0xFF51–0xFF54 — HDMA source and destination bytes
 *   - 0xFF55 — HDMA/GDMA trigger: starts or cancels H-Blank DMA, or
 *              executes a general-purpose DMA immediately
 *   - 0xFF68 — BG palette index register
 *   - 0xFF69 — BG palette data (via @ref cupid_cgb_palette_write)
 *   - 0xFF6A — OBJ palette index register
 *   - 0xFF6B — OBJ palette data (via @ref cupid_cgb_palette_write)
 *   - 0xFF6C — Object priority mode bit
 *   - 0xFF70 — WRAM bank select (values 1–7; 0 treated as 1)
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address The I/O address being written (0xFF00–0xFFFF).
 * @param value   The byte value to write.
 *
 * @return `true` if the address was handled, `false` otherwise.
 *
 * @note Does nothing (returns `false`) if @p gb is NULL.
 */
bool cupid_cgb_handle_write_register(CupidGb *gb, uint16_t address, uint8_t value) {
    if (gb == 0) {
        return false;
    }

    switch (address) {
        case 0xff4cu:
            if (cupid_gb_model_is_cgb(gb->model) && gb->boot_rom_enabled) {
                gb->io_registers[0x4cu] = value;
                gb->cgb_mode = (value & 0x0cu) == 0;
            }
            return true;
        case 0xff56u:
            cupid_gb_infrared_write_port(gb, value);
            return true;
        case 0xff4du:
            if (gb->cgb_mode) {
                gb->speed_switch_armed = (value & 0x01u) != 0u;
                gb->io_registers[0x4du] = (uint8_t)((gb->double_speed ? 0x80u : 0x00u) |
                                                    (gb->speed_switch_armed ? 0x01u : 0x00u));
            }
            return true;
        case 0xff4fu:
            if (gb->cgb_mode) {
                gb->cgb_vram_bank = (uint8_t)(value & 0x01u);
                gb->io_registers[0x4fu] = (uint8_t)(0xfeu | gb->cgb_vram_bank);
            }
            return true;
        case 0xff51u:
            if (gb->cgb_mode) {
                gb->hdma_source = (uint16_t)((gb->hdma_source & 0xf0u) | ((unsigned)value << 8u));
                if (gb->hdma_source >= 0xe000u)
                    gb->hdma_source |= 0xf000u;
            }
            return true;
        case 0xff52u:
            if (gb->cgb_mode)
                gb->hdma_source = (uint16_t)((gb->hdma_source & 0xff00u) | (value & 0xf0u));
            return true;
        case 0xff53u:
            if (gb->cgb_mode)
                gb->hdma_destination =
                    (uint16_t)((gb->hdma_destination & 0xf0u) | ((unsigned)value << 8u));
            return true;
        case 0xff54u:
            if (gb->cgb_mode) {
                gb->hdma_destination =
                    (uint16_t)((gb->hdma_destination & 0xff00u) | (value & 0xf0u));
            }
            return true;
        case 0xff55u:
            if (gb->cgb_mode) {
                gb->hdma_blocks_remaining = (uint8_t)((value & 0x7fu) + 1u);
                gb->hdma_hblank_delay = 0;
                if (gb->hdma_active && (value & 0x80u) == 0u) {
                    gb->hdma_active = false;
                    gb->hdma_pending = false;
                } else {
                    gb->hdma_active = (value & 0x80u) != 0u;
                    const bool oam_edge = gb->pixels.initialized && !gb->ppu_lcd_startup &&
                                          gb->pixels.line < 144 && gb->pixels.dot == 0;
                    gb->hdma_pending =
                        !gb->hdma_active ||
                        (((gb->io_registers[CUPID_GB_IO_STAT] & 3u) == 0) && !oam_edge);
                }
                gb->io_registers[0x55u] = cupid_cgb_dma_status(gb);
            }
            return true;
        case 0xff68u:
            if (cupid_gb_model_is_cgb(gb->model)) {
                gb->io_registers[0x68u] = (uint8_t)(value & 0xbfu);
            }
            return true;
        case 0xff69u:
            cupid_cgb_palette_write(gb, false, value);
            return true;
        case 0xff6au:
            if (cupid_gb_model_is_cgb(gb->model)) {
                gb->io_registers[0x6au] = (uint8_t)(value & 0xbfu);
            }
            return true;
        case 0xff6bu:
            cupid_cgb_palette_write(gb, true, value);
            return true;
        case 0xff6cu:
            if (cupid_gb_model_is_cgb(gb->model)) {
                /* Boot firmware can select X priority. Normal color software
                 * can change readback but cannot change the selected ordering. */
                bool writable = gb->boot_rom_enabled || (gb->io_registers[0x4cu] & 8u);
                if (writable) {
                    gb->cgb_object_priority_x = (value & 1u) != 0;
                }
                if (writable || gb->cgb_mode) {
                    gb->io_registers[0x6cu] = (uint8_t)(0xfeu | (value & 0x01u));
                }
            }
            return true;
        case 0xff70u:
            if (gb->cgb_mode || (cupid_gb_model_is_cgb(gb->model) && gb->boot_rom_enabled)) {
                gb->cgb_wram_bank = (uint8_t)(value & 0x07u);
                if (gb->cgb_wram_bank == 0u) {
                    gb->cgb_wram_bank = 1u;
                }
                /* Selector zero maps bank 1 without changing the readable selector. */
                gb->io_registers[0x70u] = (uint8_t)(0xf8u | value);
            }
            return true;
        default:
            return false;
    }
}

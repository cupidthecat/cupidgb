/**
 * @file gb.c
 * @brief Game Boy system glue — initialization, step loop, and memory map.
 *
 * Ties together all Game Boy subsystems (CPU, PPU, APU, timer, cartridge,
 * SGB, CGB). This file owns:
 *   - Hardware model selection and HLE boot-profile setup
 *   - Optional external boot ROM loading and overlay
 *   - The full 0x0000–0xFFFF memory map (reads and writes)
 *   - MBC3 real-time clock helpers
 *   - The per-instruction step function
 *
 * All subsystem implementations live in their own translation units.
 */

#include "cupid/gb/gb.h"
#include "cupid/gb/battery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cupid/common/log.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/cpu.h"
#include "cupid/gb/input.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/sgb.h"
#include "cupid/gb/timer.h"
#include "cupid/gbc/cgb.h"

// Serial transfer (memory-map helper)

/**
 * @brief Handles a write to the SC (serial control) register at 0xFF02.
 *
 * Commits the old clock phase, applies the writable control bits, and
 * establishes the remaining byte length for a disconnected serial port.
 *
 * @param gb    Pointer to the Game Boy state.
 * @param value The value being written to SC.
 *
 * @note The memory-map dispatcher has already checked that @p gb is valid.
 */
static void cupid_gb_handle_serial_transfer(CupidGb *gb, uint8_t value) {
    /* A control access begins a new byte count but also drives the old clock
     * low. An active internal transfer therefore contributes its outgoing bit
     * before the newly written clock selection takes effect. */
    bool old_bit = gb->serial_master_clock && (gb->io_registers[2] & 0x81u) == 0x81u;
    if (old_bit)
        gb->io_registers[1] = (uint8_t)(2u * gb->io_registers[1] + 1u);
    gb->serial_master_clock = false;

    uint8_t writable = gb->cgb_mode ? 0x83u : 0x81u;
    gb->io_registers[2] = (uint8_t)((value & writable) | (uint8_t)~writable);
    gb->serial_bits_remaining = (value & 0x80u) ? (uint8_t)(8u - old_bit) : 0u;
    if (value & 0x80u)
        gb->serial_tx_latch = gb->io_registers[1];
}

// Init

/**
 * @brief Returns a short lowercase string name for a hardware model.
 *
 * @param model The hardware model to query.
 *
 * @return A null-terminated string such as `"dmgabc"`, `"cgb"`, `"sgb2"`.
 *
 * @note The returned string is a literal and must not be modified or freed.
 */
const char *cupid_gb_model_name(CupidGbModel model) {
    switch (model) {
        case CUPID_GB_MODEL_DMG0:
            return "dmg0";
        case CUPID_GB_MODEL_MGB:
            return "mgb";
        case CUPID_GB_MODEL_CGB:
            return "cgb";
        case CUPID_GB_MODEL_SGB:
            return "sgb";
        case CUPID_GB_MODEL_SGB2:
            return "sgb2";
        case CUPID_GB_MODEL_DMG_ABC:
        default:
            return "dmgabc";
    }
}

/**
 * @brief Sets the hardware model for the emulated Game Boy.
 *
 * Must be called before @ref cupid_gb_init or @ref cupid_gb_load_rom.
 *
 * @param gb    Pointer to the Game Boy state.
 * @param model The hardware model to emulate.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_gb_set_model(CupidGb *gb, CupidGbModel model) {
    if (gb == 0) {
        return;
    }

    gb->model = model;
}

uint32_t cupid_gb_base_clock_hz(const CupidGb *gb) {
    /* The first SGB divides its NTSC host clock by five. SGB2 has an
     * independent oscillator matching the handheld base clock. */
    return gb && gb->model == CUPID_GB_MODEL_SGB ? 4295454u : 4194304u;
}

/**
 * @brief Applies the post-boot-ROM CPU and I/O register state for the selected model.
 *
 * Sets the register file, DIV counter, PPU counter, and a handful of I/O
 * registers to the values they would have at the end of the boot ROM for
 * each supported hardware revision (DMG0, DMG-ABC, MGB, CGB, SGB, SGB2).
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_apply_boot_profile(CupidGb *gb) {
    if (gb->model == CUPID_GB_MODEL_DMG0) {
        gb->cpu.a = 0x01u;
        gb->cpu.f = 0x00u;
        gb->cpu.b = 0xffu;
        gb->cpu.c = 0x13u;
        gb->cpu.d = 0x00u;
        gb->cpu.e = 0xc1u;
        gb->cpu.h = 0x84u;
        gb->cpu.l = 0x03u;
        gb->io_registers[0x04u] = 0x18u;
        gb->io_registers[CUPID_GB_IO_STAT] = 0x83u;
        gb->io_registers[CUPID_GB_IO_DMA] = 0x01u;
        gb->io_registers[CUPID_GB_IO_LY] = 0x91u;
        gb->div_counter = 12u;
        gb->ppu_counter = 0x29u;
    } else if (gb->model == CUPID_GB_MODEL_CGB) {
        gb->cpu.a = 0x11u;
        gb->cpu.f = 0x80u;
        gb->cpu.b = 0x00u;
        gb->cpu.c = 0x00u;
        gb->cpu.d = 0xffu;
        gb->cpu.e = 0x56u;
        gb->cpu.h = 0x00u;
        gb->cpu.l = 0x0du;
        gb->io_registers[0x04u] = 0x1eu;
        gb->io_registers[CUPID_GB_IO_STAT] = 0x80u;
        gb->io_registers[CUPID_GB_IO_DMA] = 0xffu;
        gb->io_registers[CUPID_GB_IO_LY] = 0x00u;
        gb->div_counter = 52u;
        gb->ppu_counter = 0u;
    } else if (gb->model == CUPID_GB_MODEL_SGB) {
        gb->cpu.a = 0x01u;
        gb->cpu.f = 0x00u;
        gb->cpu.b = 0x00u;
        gb->cpu.c = 0x14u;
        gb->cpu.d = 0x00u;
        gb->cpu.e = 0x00u;
        gb->cpu.h = 0xc0u;
        gb->cpu.l = 0x60u;
        gb->io_registers[0x04u] = 0xd8u;
        gb->io_registers[CUPID_GB_IO_STAT] = 0x00u;
        gb->io_registers[CUPID_GB_IO_DMA] = 0xffu;
        gb->io_registers[CUPID_GB_IO_LY] = 0xffu;
        gb->div_counter = 24u;
        gb->ppu_counter = 0u;
    } else if (gb->model == CUPID_GB_MODEL_SGB2) {
        gb->cpu.a = 0xffu;
        gb->cpu.f = 0x00u;
        gb->cpu.b = 0x00u;
        gb->cpu.c = 0x14u;
        gb->cpu.d = 0x00u;
        gb->cpu.e = 0x00u;
        gb->cpu.h = 0xc0u;
        gb->cpu.l = 0x60u;
        gb->io_registers[0x04u] = 0xd8u;
        gb->io_registers[CUPID_GB_IO_STAT] = 0x00u;
        gb->io_registers[CUPID_GB_IO_DMA] = 0xffu;
        gb->io_registers[CUPID_GB_IO_LY] = 0xffu;
        gb->div_counter = 24u;
        gb->ppu_counter = 0u;
    } else if (gb->model == CUPID_GB_MODEL_MGB) {
        gb->cpu.a = 0xffu;
        gb->cpu.f = 0xb0u;
        gb->cpu.b = 0x00u;
        gb->cpu.c = 0x13u;
        gb->cpu.d = 0x00u;
        gb->cpu.e = 0xd8u;
        gb->cpu.h = 0x01u;
        gb->cpu.l = 0x4du;
        gb->io_registers[0x04u] = 0xabu;
        gb->io_registers[CUPID_GB_IO_STAT] = 0x80u;
        gb->io_registers[CUPID_GB_IO_DMA] = 0x0au;
        gb->io_registers[CUPID_GB_IO_LY] = 0x00u;
        gb->div_counter = 51u;
        gb->ppu_counter = 0u;
    } else {
        gb->cpu.a = 0x01u;
        gb->cpu.f = 0xb0u;
        gb->cpu.b = 0x00u;
        gb->cpu.c = 0x13u;
        gb->cpu.d = 0x00u;
        gb->cpu.e = 0xd8u;
        gb->cpu.h = 0x01u;
        gb->cpu.l = 0x4du;
        gb->io_registers[0x04u] = 0xabu;
        gb->io_registers[CUPID_GB_IO_STAT] = 0x80u;
        gb->io_registers[CUPID_GB_IO_DMA] = 0x0au;
        gb->io_registers[CUPID_GB_IO_LY] = 0x00u;
        gb->div_counter = 51u;
        gb->ppu_counter = 0u;
    }

    gb->cpu.sp = 0xfffeu;
    gb->cpu.pc = CUPID_GB_ENTRY_POINT;
    gb->interrupt_flags = 0x01u;
}

/**
 * @brief Initializes (or re-initializes) all Game Boy subsystem state.
 *
 * Allocates the ROM buffer if one has not already been provided, clears
 * all hardware state, and applies the HLE post-boot-ROM register profile
 * for the selected model. Must be called before loading a ROM.
 *
 * @param gb Pointer to the Game Boy state to initialize.
 *
 * @note The instance must be zero-initialized before its first initialization.
 * @note The `model`, `rom`, and host observer registrations survive a re-init.
 * @note Does nothing if @p gb is NULL.
 */
void cupid_gb_init(CupidGb *gb) {
    CupidGbModel model;
    uint8_t *rom;
    CupidGbSerialOutputCallback serial_output_callback;
    void *serial_output_user_data;
    CupidGbRumbleCallback rumble_callback;
    void *rumble_user_data;

    if (gb == 0) {
        return;
    }

    model = gb->model;
    rom = gb->rom;
    serial_output_callback = gb->serial_output_callback;
    serial_output_user_data = gb->serial_output_user_data;
    rumble_callback = gb->rumble_callback;
    rumble_user_data = gb->rumble_user_data;
    memset(gb, 0, sizeof(*gb));
    gb->model = model;
    gb->rom = rom;
    gb->serial_output_callback = serial_output_callback;
    gb->serial_output_user_data = serial_output_user_data;
    gb->rumble_callback = rumble_callback;
    gb->rumble_user_data = rumble_user_data;

    if (gb->rom == 0) {
        gb->rom = malloc(CUPID_GB_MAX_ROM_SIZE);
        if (gb->rom == 0) {
            cupid_log_error("Failed to allocate Game Boy ROM buffer.");
            return;
        }
    }

    gb->cgb_mode = gb->model == CUPID_GB_MODEL_CGB;
    memset(gb->io_registers, 0xff, sizeof(gb->io_registers));
    cupid_gb_apply_boot_profile(gb);
    gb->io_registers[CUPID_GB_IO_LCDC] = 0x91u;
    gb->io_registers[0x01u] = 0x00u;
    gb->io_registers[0x02u] = 0x7eu;
    gb->io_registers[0x05u] = 0x00u;
    gb->io_registers[0x06u] = 0x00u;
    gb->io_registers[0x07u] = 0x00u;
    gb->io_registers[CUPID_GB_IO_BGP] = 0xfcu;
    gb->io_registers[CUPID_GB_IO_OBP0] = 0xffu;
    gb->io_registers[CUPID_GB_IO_OBP1] = 0xffu;
    gb->io_registers[CUPID_GB_IO_SCY] = 0x00u;
    gb->io_registers[CUPID_GB_IO_SCX] = 0x00u;
    gb->io_registers[CUPID_GB_IO_LYC] = 0x00u;
    gb->io_registers[CUPID_GB_IO_WY] = 0x00u;
    gb->io_registers[CUPID_GB_IO_WX] = 0x00u;
    /* APU post-boot-ROM register state */
    gb->io_registers[0x10u] = 0x80u; /* NR10 */
    gb->io_registers[0x11u] = 0xbfu; /* NR11 */
    gb->io_registers[0x12u] = 0xf3u; /* NR12 */
    gb->io_registers[0x14u] = 0xbfu; /* NR14 */
    gb->io_registers[0x16u] = 0x3fu; /* NR21 */
    gb->io_registers[0x17u] = 0x00u; /* NR22 */
    gb->io_registers[0x19u] = 0xbfu; /* NR24 */
    gb->io_registers[0x1au] = 0x7fu; /* NR30 */
    gb->io_registers[0x1bu] = 0xffu; /* NR31 */
    gb->io_registers[0x1cu] = 0x9fu; /* NR32 */
    gb->io_registers[0x1eu] = 0xbfu; /* NR34 */
    gb->io_registers[0x20u] = 0xffu; /* NR41 */
    gb->io_registers[0x21u] = 0x00u; /* NR42 */
    gb->io_registers[0x22u] = 0x00u; /* NR43 */
    gb->io_registers[0x23u] = 0xbfu; /* NR44 */
    gb->io_registers[0x24u] = 0x77u; /* NR50 */
    gb->io_registers[0x25u] = 0xf3u; /* NR51 */
    gb->io_registers[0x26u] = (gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2)
                                  ? 0xf0u
                                  : 0xf1u; /* NR52 */
    /* Sync APU struct with boot-ROM state */
    gb->apu.apu_on = true;
    gb->apu.ch1_dac = true;
    gb->apu.ch1_on = !(gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2);
    gb->apu.ch1_duty = 2u; /* 50% (NR11 = 0xBF) */
    gb->apu.ch1_len = 63u; /* NR11 length bits = 0x3F → 64-63=1 → stored 1 */
    gb->apu.ch1_vol = 15u;
    gb->apu.ch1_env_add = false;
    gb->apu.ch1_env_period = 3u;
    gb->apu.ch1_env_timer = 3u;
    gb->apu.ch1_freq = 0u;
    gb->apu.ch1_timer = 8192u;
    gb->apu.ch4_lfsr = 0x7fffu;
    cupid_cgb_init_state(gb);
    gb->io_registers[0x4du] = 0x00u; /* KEY1: normal speed, switch not armed */
    gb->io_registers[0x00u] = (gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2)
                                  ? 0xffu
                                  : 0xcfu; /* P1 */
    gb->joypad = 0xffu;                    /* all buttons released (0=pressed) */
    memset(gb->joypad_extra, 0xff, sizeof(gb->joypad_extra));
    gb->frame_ready = false;
    gb->stat_irq_delay = 0u;
    gb->ppu_lcd_warmup_lines = 0u;
    gb->ppu_lcd_startup = false;
    gb->ppu_line_boundary_hold = false;
    gb->stat_irq_line = false;
    gb->window_line_counter = 0u;
    cupid_gb_sgb_init(gb);
    cupid_gb_apu_import_legacy(gb);
}

// Boot ROM support

/**
 * @brief Loads an external boot ROM dump from a file.
 *
 * DMG boot ROMs must be exactly 256 (0x100) bytes.
 * CGB boot ROMs must be exactly 2304 (0x900) bytes.
 * The loaded data is stored in `gb->boot_rom` and the size in
 * `gb->boot_rom_size`. Call @ref cupid_gb_enter_boot_rom after
 * this to activate the overlay.
 *
 * @param gb   Pointer to the Game Boy state.
 * @param path Null-terminated path to the boot ROM file.
 *
 * @return `true` on success, `false` on any I/O or size error.
 *
 * @note Returns `false` if @p gb or @p path is NULL.
 */
bool cupid_gb_load_boot_rom_file(CupidGb *gb, const char *path) {
    FILE *fp;
    long file_size;

    if (gb == 0 || path == 0) {
        return false;
    }

    fp = fopen(path, "rb");
    if (fp == 0) {
        return false;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size != 0x0100 && file_size != 0x0900) {
        cupid_log_errorf("Boot ROM '%s' has invalid size %ld (expected 256 or 2304 bytes)", path,
                         file_size);
        fclose(fp);
        return false;
    }

    if ((size_t)file_size > sizeof(gb->boot_rom)) {
        fclose(fp);
        return false;
    }

    if (fread(gb->boot_rom, 1u, (size_t)file_size, fp) != (size_t)file_size) {
        cupid_log_errorf("Failed to read boot ROM '%s'", path);
        fclose(fp);
        return false;
    }

    fclose(fp);
    gb->boot_rom_size = (size_t)file_size;
    return true;
}

/**
 * @brief Clears the boot ROM buffer and disables the boot ROM overlay.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_gb_clear_boot_rom(CupidGb *gb) {
    if (gb == 0) {
        return;
    }
    memset(gb->boot_rom, 0, sizeof(gb->boot_rom));
    gb->boot_rom_size = 0u;
    gb->boot_rom_enabled = false;
}

/**
 * @brief Resets the CPU and hardware to true power-on state and activates the boot ROM.
 *
 * Zeroes all CPU registers, resets all I/O registers, APU, DIV, timer,
 * and serial state, then maps the boot ROM overlay so execution starts
 * at address 0x0000. Call this after loading both the boot ROM
 * (@ref cupid_gb_load_boot_rom_file) and the game ROM
 * (@ref cupid_gb_load_rom or @ref cupid_gb_load_rom_file).
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL or no boot ROM has been loaded.
 */
void cupid_gb_enter_boot_rom(CupidGb *gb) {
    if (gb == 0 || gb->boot_rom_size == 0u) {
        return;
    }

    /* Zero out CPU registers - boot ROM initializes everything */
    gb->cpu.a = 0u;
    gb->cpu.f = 0u;
    gb->cpu.b = 0u;
    gb->cpu.c = 0u;
    gb->cpu.d = 0u;
    gb->cpu.e = 0u;
    gb->cpu.h = 0u;
    gb->cpu.l = 0u;
    gb->cpu.sp = 0u;
    gb->cpu.pc = 0x0000u; /* start executing boot ROM from address 0 */
    gb->cpu.ime = false;
    gb->cpu.ime_delay = 0u;
    gb->cpu.halted = false;
    gb->cpu.just_halted = false;
    gb->cpu.stopped = false;
    gb->cpu.halt_bug = false;

    /* Reset IO to power-on defaults */
    /* Color boot firmware selects cartridge compatibility later through KEY0.
     * The cartridge header governs the post-boot profile only when no firmware runs. */
    gb->cgb_mode = gb->model == CUPID_GB_MODEL_CGB;
    gb->hdma_pending = false;
    gb->hdma_active = false;
    gb->hdma_hblank_delay = 0;
    gb->hdma_allow_on_wake = false;
    gb->hdma_blocks_remaining = 0;
    gb->double_speed = false;
    gb->speed_phase = false;
    gb->speed_switch_armed = false;
    gb->speed_switch_halt_cycles = 0;
    gb->speed_switch_toggle_cycles = 0;
    gb->speed_switch_freeze_cycles = 0;
    gb->stop_divider_hold_cycles = 0;
    gb->speed_switch_alignment = 0;
    memset(gb->io_registers, 0x00u, sizeof(gb->io_registers));
    gb->io_registers[0x00u] = 0xcfu; /* P1: no buttons selected */
    gb->io_registers[0x02u] = 0x7eu; /* SC: disconnected, fast clock not selected. */

    /* Reset PPU state */
    gb->io_registers[CUPID_GB_IO_LCDC] = 0x00u; /* LCD off at power-on */
    gb->io_registers[CUPID_GB_IO_STAT] = 0x00u;
    gb->io_registers[CUPID_GB_IO_SCY] = 0x00u;
    gb->io_registers[CUPID_GB_IO_SCX] = 0x00u;
    gb->io_registers[CUPID_GB_IO_LY] = 0x00u;
    gb->io_registers[CUPID_GB_IO_LYC] = 0x00u;
    gb->io_registers[CUPID_GB_IO_BGP] = 0x00u;
    /* DMA and both object palette registers have model-specific power-on values. */
    const uint8_t reset_value = gb->cgb_mode ? 0x00u : 0xffu;
    gb->io_registers[CUPID_GB_IO_DMA] = reset_value;
    gb->io_registers[CUPID_GB_IO_OBP0] = reset_value;
    gb->io_registers[CUPID_GB_IO_OBP1] = reset_value;
    gb->io_registers[CUPID_GB_IO_WY] = 0x00u;
    gb->io_registers[CUPID_GB_IO_WX] = 0x00u;

    /* Firmware begins eight T-cycles into the divider phase, stored in M-cycles. */
    gb->div_counter = 2u;
    gb->ppu_counter = 0u;
    gb->serial_master_clock = false;
    gb->interrupt_flags = 0x00u;
    gb->interrupt_enable = 0x00u;

    /* Reset APU to power-on state */
    memset(&gb->apu, 0, sizeof(gb->apu));

    /* Activate boot ROM overlay */
    gb->boot_rom_enabled = true;
    gb->frame_ready = false;
    gb->ppu_lcd_startup = false;
    gb->ppu_lcd_warmup_lines = 0u;
    gb->stat_irq_delay = 0u;
    gb->stat_irq_line = false;
    gb->window_line_counter = 0u;
}

/**
 * @brief Frees all dynamically allocated Game Boy resources.
 *
 * Releases the ROM buffer. Safe to call on a partially initialized
 * or already-cleaned-up instance.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_gb_cleanup(CupidGb *gb) {
    if (gb == 0) {
        return;
    }

    free(gb->rom);
    gb->rom = 0;
    gb->serial_output_callback = NULL;
    gb->serial_output_user_data = NULL;
    gb->rumble_callback = NULL;
    gb->rumble_user_data = NULL;
    gb->mbc5_rumble_on = false;
}

void cupid_gb_set_serial_output_callback(CupidGb *gb, CupidGbSerialOutputCallback callback,
                                         void *user_data) {
    if (!gb) {
        return;
    }
    gb->serial_output_callback = callback;
    gb->serial_output_user_data = callback ? user_data : NULL;
}

void cupid_gb_set_rumble_callback(CupidGb *gb, CupidGbRumbleCallback callback, void *user_data) {
    if (!gb) {
        return;
    }
    gb->rumble_callback = callback;
    gb->rumble_user_data = callback ? user_data : NULL;
}

bool cupid_gb_rumble_enabled(const CupidGb *gb) {
    return gb && gb->header.mbc_type == CUPID_GB_MBC5 && gb->header.has_rumble &&
           gb->mbc5_rumble_on;
}

// Memory map: read

/**
 * @brief Reads one byte from the full 0x0000–0xFFFF Game Boy address space.
 *
 * Implements the complete DMG/CGB memory map, including:
 *   - Boot ROM overlay (0x0000–0x00FF, and 0x0200–0x08FF for CGB)
 *   - ROM banks via MBC bank-switching logic
 *   - VRAM (with Mode 3 lock)
 *   - Cartridge RAM / RTC / MBC7 sensor / HuC registers
 *   - WRAM and echo RAM
 *   - OAM (with Mode 2/3 lock)
 *   - APU I/O registers (with read masks)
 *   - Wave RAM (with CH3 access conflict handling)
 *   - P1/JOYP joypad register
 *   - HRAM and IE
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address The 16-bit address to read.
 *
 * @return The byte at @p address, or 0xFF if the address is open-bus,
 *         locked during DMA, or @p gb is NULL.
 */
uint8_t cupid_gb_read_u8(const CupidGb *gb, uint16_t address) {
    static const uint8_t apu_read_masks[0x20] = {
        0x80u, 0x3fu, 0x00u, 0xffu, 0xbfu, 0xffu, 0x3fu, 0x00u, 0xffu, 0xbfu, 0x7fu,
        0xffu, 0x9fu, 0xffu, 0xbfu, 0xffu, 0xffu, 0x00u, 0x00u, 0xbfu, 0x00u, 0x00u,
        0x70u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu};

    if (gb == 0) {
        return 0xffu;
    }

    if (gb->dma_active && address >= 0xfe00u && address <= 0xfeffu) {
        return 0xffu;
    }

    if (address <= 0x3fffu) {
        /* Boot ROM overlay: intercept reads while the boot ROM is mapped */
        if (gb->boot_rom_enabled && gb->boot_rom_size > 0u) {
            /* DMG boot ROM: 0x0000-0x00FF (256 bytes) */
            if (address <= 0x00ffu && address < gb->boot_rom_size) {
                return gb->boot_rom[address];
            }
            /* CGB boot ROM: also 0x0200-0x08FF */
            if (gb->boot_rom_size > 0x0100u && address >= 0x0200u && address <= 0x08ffu) {
                return gb->boot_rom[address];
            }
        }

        size_t bank = cupid_gb_effective_rom_bank(gb, true);
        size_t offset = bank * 0x4000u + (size_t)address;

        if (offset < gb->rom_size) {
            return gb->rom[offset];
        }
        return 0xffu;
    }

    if (address <= 0x7fffu) {
        size_t bank = cupid_gb_effective_rom_bank(gb, false);
        size_t offset = bank * 0x4000u + (size_t)(address - 0x4000u);

        if (offset < gb->rom_size) {
            return gb->rom[offset];
        }
        return 0xffu;
    }

    if (address >= 0x8000u && address <= 0x9fffu) {
        uint8_t mode = (uint8_t)(gb->io_registers[CUPID_GB_IO_STAT] & 0x03u);

        if (cupid_gb_lcd_enabled(gb) &&
            gb->io_registers[CUPID_GB_IO_LY] < CUPID_GB_PPU_VISIBLE_SCANLINES &&
            (mode == CUPID_GB_PPU_MODE_TRANSFER ||
             (mode == CUPID_GB_PPU_MODE_OAM && gb->ppu_counter >= CUPID_GB_PPU_OAM_CYCLES))) {
            return 0xffu;
        }
        return gb->video_ram[cupid_cgb_vram_offset(gb, address)];
    }

    if (address >= 0xa000u && address <= 0xbfffu) {
        if (!gb->ram_enabled && gb->header.mbc_type != CUPID_GB_HUC1) {
            return 0xffu;
        }
        /* MBC2: built-in 512x4-bit RAM, only bottom nibble valid */
        if (gb->header.mbc_type == CUPID_GB_MBC2) {
            size_t offset = (size_t)(address - 0xa000u) & 0x01ffu;
            return (uint8_t)(gb->cartridge_ram[offset] | 0xf0u);
        }
        /* MBC3: RTC register reads when selected */
        if (gb->header.mbc_type == CUPID_GB_MBC3 && gb->rtc_register >= 0x08u) {
            if (gb->rtc_register > 0x0cu)
                return 0xffu;
            const uint8_t latched[] = {gb->rtc.ls, gb->rtc.lm, gb->rtc.lh, gb->rtc.ldl,
                                       gb->rtc.ldh};
            const uint8_t masks[] = {0x3f, 0x3f, 0x1f, 0xff, 0xc1};
            unsigned selected = gb->rtc_register - 0x08u;
            return latched[selected] & masks[selected];
        }
        /* Ordinary timer-equipped MBC3 wiring leaves banks 4-7 disconnected;
         * the extended controller connects them to additional SRAM. */
        if (gb->header.mbc_type == CUPID_GB_MBC3 && gb->header.has_timer &&
            !cupid_gb_has_extended_mbc3(gb) && gb->current_ram_bank > 3u) {
            return 0xffu;
        }
        /* MBC7: accelerometer / EEPROM registers */
        if (gb->header.mbc_type == CUPID_GB_MBC7) {
            if (address >= 0xa020u && address <= 0xa02fu) {
                return (uint8_t)(gb->mbc7_sensor_x & 0xffu);
            }
            if (address >= 0xa030u && address <= 0xa03fu) {
                return (uint8_t)((gb->mbc7_sensor_x >> 8u) & 0xffu);
            }
            if (address >= 0xa040u && address <= 0xa04fu) {
                return (uint8_t)(gb->mbc7_sensor_y & 0xffu);
            }
            if (address >= 0xa050u && address <= 0xa05fu) {
                return (uint8_t)((gb->mbc7_sensor_y >> 8u) & 0xffu);
            }
            return 0xffu;
        }
        /* HuC3: command interface */
        if (gb->header.mbc_type == CUPID_GB_HUC3 && gb->huc3_mode == 0x0cu) {
            return gb->huc3_value;
        }
        /* HuC1: IR mode returns 0xC0 (no IR signal) */
        if (gb->header.mbc_type == CUPID_GB_HUC1 && gb->huc1_ir_mode) {
            return 0xc0u;
        }
        {
            size_t offset = cupid_gb_effective_ram_bank(gb) * 0x2000u + (size_t)(address - 0xa000u);
            /* Every configured SRAM size is a power of two. Missing address
             * lines mirror a small chip throughout the cartridge window. */
            if (gb->cartridge_ram_size)
                offset &= gb->cartridge_ram_size - 1u;
            if (offset < gb->cartridge_ram_size) {
                return gb->cartridge_ram[offset];
            }
        }
        return 0xffu;
    }

    if (address >= 0xc000u && address <= 0xdfffu) {
        return gb->work_ram[cupid_cgb_wram_offset(gb, address)];
    }

    if (address >= 0xe000u && address <= 0xfdffu) {
        return gb->work_ram[cupid_cgb_wram_offset(gb, (uint16_t)(address - 0x2000u))];
    }

    if (address >= 0xfe00u && address <= 0xfe9fu) {
        uint8_t mode = (uint8_t)(gb->io_registers[CUPID_GB_IO_STAT] & 0x03u);

        if (cupid_gb_lcd_enabled(gb) &&
            gb->io_registers[CUPID_GB_IO_LY] < CUPID_GB_PPU_VISIBLE_SCANLINES &&
            /* OAM is already claimed at the line boundary. The mode bits
             * still show the previous HBlank until the following LCD dot. */
            (gb->ppu_line_boundary_hold ||
             (gb->pixels.initialized && !gb->ppu_lcd_startup && gb->pixels.dot == 0) ||
             mode == CUPID_GB_PPU_MODE_OAM || mode == CUPID_GB_PPU_MODE_TRANSFER)) {
            return 0xffu;
        }
        return gb->object_attribute_memory[address - 0xfe00u];
    }

    if (address >= 0xfea0u && address <= 0xfeffu) {
        return 0xffu;
    }

    if (address == 0xff26u) {
        /* NR52: rebuild on-the-fly with current channel status */
        return (uint8_t)((gb->apu.apu_on ? 0x80u : 0u) | 0x70u | (gb->apu.ch4_on ? 0x08u : 0u) |
                         (gb->apu.ch3_on ? 0x04u : 0u) | (gb->apu.ch2_on ? 0x02u : 0u) |
                         (gb->apu.ch1_on ? 0x01u : 0u));
    }

    if (address == 0xff00u) {
        return cupid_gb_input_read(gb);
    }

    if (address == 0xff0fu) {
        return (uint8_t)(gb->interrupt_flags | 0xe0u);
    }

    if (address == 0xff41u) {
        /* The unused STAT input is pulled high independently of startup RAM. */
        return (uint8_t)(gb->io_registers[CUPID_GB_IO_STAT] | 0x80u);
    }

    if (address == 0xff02u) {
        uint8_t writable = gb->cgb_mode ? 0x83u : 0x81u;
        return (uint8_t)(gb->io_registers[2] | (uint8_t)~writable);
    }

    if (address == 0xff50u) {
        return gb->boot_rom_enabled ? 0xfeu : 0xffu;
    }

    /* Decode model-specific registers before applying unused-address pull-ups.
     * Their visibility can depend on physical hardware rather than color mode. */
    {
        uint8_t cgb_value;
        if (cupid_cgb_handle_read_register(gb, address, &cgb_value))
            return cgb_value;
    }

    if (address == 0xff03u || (address >= 0xff08u && address <= 0xff0eu) || address == 0xff4eu ||
        (address >= 0xff51u && address <= 0xff67u) || (address >= 0xff6du && address <= 0xff6fu) ||
        (address >= 0xff71u && address <= 0xff7fu)) {
        return 0xffu;
    }

    if (address == 0xff07u) {
        return (uint8_t)(0xf8u | (gb->io_registers[0x07u] & 0x07u));
    }

    if (address >= 0xff10u && address <= 0xff2fu) {
        return (uint8_t)(gb->io_registers[address - 0xff00u] | apu_read_masks[address - 0xff10u]);
    }

    if (address >= 0xff30u && address <= 0xff3fu) {
        if (gb->apu.apu_on && gb->apu.ch3_on) {
            if (gb->model != CUPID_GB_MODEL_CGB) {
                if (gb->apu.ch3_wave_access_ticks == 0u) {
                    return 0xffu;
                }
            }
            return gb->io_registers[0x30u + gb->apu.ch3_current_byte];
        }
        return gb->io_registers[address - 0xff00u];
    }

    if (address >= 0xff00u && address <= 0xff7fu) {
        return gb->io_registers[address - 0xff00u];
    }

    if (address >= 0xff80u && address <= 0xfffeu) {
        return gb->high_ram[address - 0xff80u];
    }

    if (address == 0xffffu) {
        return gb->interrupt_enable;
    }

    return 0xffu;
}

// MBC3 real-time clock helpers

/**
 * @brief Advances the MBC3 RTC registers using wall-clock time.
 *
 * Computes the elapsed real time since `gb->rtc.base_time`, carries it
 * through seconds → minutes → hours → days, and updates the live RTC
 * registers. Sets the carry flag in DH if the day counter overflows 511.
 * Leaves counters unchanged when halted or when the wall clock moves backward.
 * The host-time baseline is refreshed in both cases.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_rtc_update(CupidGb *gb) {
    cupid_gb_rtc_advance(&gb->rtc, (int64_t)time(NULL));
}

/**
 * @brief Latches a snapshot of the live MBC3 RTC registers.
 *
 * Calls @ref cupid_gb_rtc_update to bring the live registers current,
 * then copies S/M/H/DL/DH into the latched shadow registers (LS/LM/LH/LDL/LDH).
 * The latched values are what the game reads via the 0xA000–0xBFFF window.
 *
 * @param gb Pointer to the Game Boy state.
 */
static void cupid_gb_rtc_latch(CupidGb *gb) {
    cupid_gb_rtc_update(gb);
    gb->rtc.ls = gb->rtc.s;
    gb->rtc.lm = gb->rtc.m;
    gb->rtc.lh = gb->rtc.h;
    gb->rtc.ldl = gb->rtc.dl;
    gb->rtc.ldh = gb->rtc.dh;
}

// Memory map: write

/**
 * @brief Writes one byte to the full 0x0000–0xFFFF Game Boy address space.
 *
 * Implements the complete DMG/CGB memory map for writes, including:
 *   - MBC register writes (RAM enable, ROM/RAM bank select, mode)
 *   - MBC3 RTC latch sequence
 *   - VRAM (with Mode 3 lock)
 *   - Cartridge RAM / RTC register writes / MBC7 latch / HuC3 commands
 *   - WRAM and echo RAM
 *   - OAM (with Mode 2/3 lock and DMG OAM corruption bug emulation)
 *   - P1/JOYP, LCDC, STAT, LY, LYC, DMA, timer, APU, and boot register
 *   - HRAM and IE
 *
 * @param gb      Pointer to the Game Boy state.
 * @param address The 16-bit address to write.
 * @param value   The byte value to write.
 *
 * @note Does nothing if @p gb is NULL or the write is blocked by DMA.
 */
void cupid_gb_write_u8_phase(CupidGb *gb, uint16_t address, uint8_t value, bool half_dot) {
    if (gb == 0) {
        return;
    }

    if (gb->dma_active && address >= 0xfe00u && address <= 0xfeffu) {
        return;
    }

    if (address < 0x8000u && gb->header.mbc_type == CUPID_GB_HUC1) {
        cupid_gb_huc1_write(gb, address, value);
        return;
    }

    if (address <= 0x3fffu && gb->header.mbc_type == CUPID_GB_MBC2) {
        if ((address & 0x0100u) == 0u) {
            gb->ram_enabled = (value & 0x0fu) == 0x0au;
        } else {
            gb->current_rom_bank = (size_t)(value & 0x0fu);
            if (gb->current_rom_bank == 0u) {
                gb->current_rom_bank = 1u;
            }
        }
        return;
    }

    if (address <= 0x1fffu) {
        switch (gb->header.mbc_type) {
            case CUPID_GB_MBC1:
            case CUPID_GB_MBC3:
            case CUPID_GB_MBC6:
            case CUPID_GB_MBC7:
            case CUPID_GB_MMM01:
                gb->ram_enabled = (value & 0x0fu) == 0x0au;
                break;
            case CUPID_GB_MBC5:
                /* All data bits participate in this controller's enable decode. */
                gb->ram_enabled = value == 0x0au;
                break;
            case CUPID_GB_HUC3:
                gb->huc3_mode = value;
                gb->ram_enabled = ((value & 0x0fu) == 0x0au);
                break;
            default:
                break;
        }
        return;
    }

    if (address <= 0x3fffu) {
        switch (gb->header.mbc_type) {
            case CUPID_GB_MBC1:
                gb->mbc1_bank_low5 = (uint8_t)(value & 0x1fu);
                if (gb->mbc1_bank_low5 == 0u) {
                    gb->mbc1_bank_low5 = 1u;
                }
                gb->current_rom_bank = cupid_gb_effective_rom_bank(gb, false);
                break;
            case CUPID_GB_MBC3:
                gb->current_rom_bank = value & (cupid_gb_has_extended_mbc3(gb) ? 0xffu : 0x7fu);
                if (gb->current_rom_bank == 0u)
                    gb->current_rom_bank = 1u;
                break;
            case CUPID_GB_HUC3:
                gb->current_rom_bank = (size_t)(value & 0x7fu);
                if (gb->current_rom_bank == 0u) {
                    gb->current_rom_bank = 1u;
                }
                break;
            case CUPID_GB_MBC5:
                if (address <= 0x2fffu) {
                    gb->mbc5_rom_bank = (uint16_t)((gb->mbc5_rom_bank & 0x0100u) | value);
                } else {
                    gb->mbc5_rom_bank = (uint16_t)((gb->mbc5_rom_bank & 0x00ffu) |
                                                   ((unsigned)(value & 0x01u) << 8u));
                }
                gb->current_rom_bank = (size_t)gb->mbc5_rom_bank;
                break;
            case CUPID_GB_MBC6:
            case CUPID_GB_MBC7:
            case CUPID_GB_MMM01:
                gb->current_rom_bank = (size_t)(value & 0x7fu);
                if (gb->current_rom_bank == 0u) {
                    gb->current_rom_bank = 1u;
                }
                break;
            default:
                break;
        }
        return;
    }

    if (address <= 0x5fffu) {
        switch (gb->header.mbc_type) {
            case CUPID_GB_MBC1:
                gb->mbc1_bank_high2 = (uint8_t)(value & 0x03u);
                gb->current_rom_bank = cupid_gb_effective_rom_bank(gb, false);
                gb->current_ram_bank = cupid_gb_effective_ram_bank(gb);
                break;
            case CUPID_GB_MBC3:
                /* RAM address bits and the RTC select line are independent;
                 * upper control bits alias the same low-nibble selection. */
                gb->current_ram_bank = value & 7u;
                gb->rtc_register = gb->header.has_timer && (value & 8u) ? value & 15u : 0u;
                break;
            case CUPID_GB_HUC3:
                if (value <= 0x03u) {
                    gb->current_ram_bank = (size_t)value;
                    gb->rtc_register = 0u;
                } else if (value >= 0x08u && value <= 0x0cu) {
                    gb->rtc_register = value;
                }
                break;
            case CUPID_GB_MBC5:
                if (gb->header.has_rumble) {
                    gb->current_ram_bank = (size_t)(value & 0x07u);
                    const bool enabled = (value & 0x08u) != 0u;
                    if (enabled != gb->mbc5_rumble_on) {
                        gb->mbc5_rumble_on = enabled;
                        if (gb->rumble_callback) {
                            gb->rumble_callback(gb, enabled, gb->elapsed_cycles,
                                                gb->rumble_user_data);
                        }
                    }
                } else {
                    gb->current_ram_bank = (size_t)(value & 0x0fu);
                }
                break;
            case CUPID_GB_MBC6:
            case CUPID_GB_MBC7:
            case CUPID_GB_MMM01:
                gb->current_ram_bank = (size_t)(value & 0x03u);
                break;
            default:
                break;
        }
        return;
    }

    if (address <= 0x7fffu) {
        switch (gb->header.mbc_type) {
            case CUPID_GB_MBC1:
                gb->mbc1_ram_banking_mode = (value & 0x01u) != 0u;
                gb->current_rom_bank = cupid_gb_effective_rom_bank(gb, false);
                gb->current_ram_bank = cupid_gb_effective_ram_bank(gb);
                break;
            case CUPID_GB_MBC3:
                /* Latch clock data: write 0x00 then 0x01 */
                if (value == 0x01u && gb->rtc.latch_prev == 0x00u) {
                    cupid_gb_rtc_latch(gb);
                }
                gb->rtc.latch_prev = value;
                break;
            default:
                break;
        }
        return;
    }

    if (address >= 0x8000u && address <= 0x9fffu) {
        uint8_t mode = (uint8_t)(gb->io_registers[CUPID_GB_IO_STAT] & 0x03u);

        if (cupid_gb_lcd_enabled(gb) &&
            gb->io_registers[CUPID_GB_IO_LY] < CUPID_GB_PPU_VISIBLE_SCANLINES &&
            mode == CUPID_GB_PPU_MODE_TRANSFER) {
            return;
        }
        gb->video_ram[cupid_cgb_vram_offset(gb, address)] = value;
        return;
    }

    if (address >= 0xa000u && address <= 0xbfffu) {
        if (!gb->ram_enabled && gb->header.mbc_type != CUPID_GB_HUC1) {
            return;
        }
        /* An IR write drives the cartridge LED and must never overwrite SRAM.
         * The disconnected accessory currently has no external LED callback. */
        if (gb->header.mbc_type == CUPID_GB_HUC1 && gb->huc1_ir_mode) {
            return;
        }
        /* MBC2: built-in 512x4-bit RAM */
        if (gb->header.mbc_type == CUPID_GB_MBC2) {
            size_t offset = (size_t)(address - 0xa000u) & 0x01ffu;
            if ((gb->cartridge_ram[offset] & 0x0fu) != (value & 0x0fu) && gb->header.has_battery)
                ++gb->battery_write_generation;
            gb->cartridge_ram[offset] = (uint8_t)(value & 0x0fu);
            return;
        }
        /* MBC3: write to RTC register */
        if (gb->header.mbc_type == CUPID_GB_MBC3 && gb->rtc_register >= 0x08u) {
            if (!gb->header.has_timer || gb->rtc_register > 0x0cu)
                return;
            const int64_t previous_time = gb->rtc.base_time;
            cupid_gb_rtc_update(gb);
            uint8_t *registers[] = {&gb->rtc.s, &gb->rtc.m, &gb->rtc.h, &gb->rtc.dl, &gb->rtc.dh};
            uint8_t *selected = registers[gb->rtc_register - 0x08u];
            /* The seconds port also starts a new clock interval. Clock latches
             * and host time updates must not count as cartridge writes.
             */
            if (gb->header.has_battery &&
                (*selected != value ||
                 (gb->rtc_register == 0x08u && previous_time != gb->rtc.base_time &&
                  !(gb->rtc.dh & 0x40u))))
                ++gb->battery_write_generation;
            *selected = value;
            return;
        }
        if (gb->header.mbc_type == CUPID_GB_MBC3 && gb->header.has_timer &&
            !cupid_gb_has_extended_mbc3(gb) && gb->current_ram_bank > 3u) {
            return;
        }
        /* MBC7: accelerometer latch control */
        if (gb->header.mbc_type == CUPID_GB_MBC7) {
            if (address >= 0xa000u && address <= 0xa00fu) {
                if (value == 0x55u) {
                    gb->mbc7_latch = true;
                }
            }
            return;
        }
        /* HuC3: command write */
        if (gb->header.mbc_type == CUPID_GB_HUC3 && gb->huc3_mode == 0x0bu) {
            gb->huc3_value = value;
            return;
        }
        {
            size_t offset = cupid_gb_effective_ram_bank(gb) * 0x2000u + (size_t)(address - 0xa000u);
            if (gb->cartridge_ram_size)
                offset &= gb->cartridge_ram_size - 1u;
            if (offset < gb->cartridge_ram_size) {
                if (gb->cartridge_ram[offset] != value && gb->header.has_battery)
                    ++gb->battery_write_generation;
                gb->cartridge_ram[offset] = value;
            }
        }
        return;
    }

    if (address >= 0xc000u && address <= 0xdfffu) {
        gb->work_ram[cupid_cgb_wram_offset(gb, address)] = value;
        return;
    }

    if (address >= 0xe000u && address <= 0xfdffu) {
        gb->work_ram[cupid_cgb_wram_offset(gb, (uint16_t)(address - 0x2000u))] = value;
        return;
    }

    if (address >= 0xfe00u && address <= 0xfeffu && cupid_gb_lcd_enabled(gb) &&
        gb->io_registers[CUPID_GB_IO_LY] < CUPID_GB_PPU_VISIBLE_SCANLINES) {
        uint8_t mode = (uint8_t)(gb->io_registers[CUPID_GB_IO_STAT] & 0x03u);
        bool blocked = mode == CUPID_GB_PPU_MODE_TRANSFER ||
                       (mode == CUPID_GB_PPU_MODE_OAM && gb->ppu_counter < CUPID_GB_PPU_OAM_CYCLES);

        if (blocked) {
            if (!gb->cgb_mode && mode == CUPID_GB_PPU_MODE_OAM &&
                gb->ppu_counter < CUPID_GB_PPU_OAM_CYCLES && address <= 0xfe9fu) {
                cupid_gb_trigger_oam_bug_write_access(gb, address);
            }
            return;
        }
    }

    if (address >= 0xfe00u && address <= 0xfe9fu) {
        gb->object_attribute_memory[address - 0xfe00u] = value;
        return;
    }

    if (address >= 0xfea0u && address <= 0xfeffu) {
        return;
    }

    if (address == 0xff0fu) {
        gb->interrupt_flags = (uint8_t)(value & 0x1fu);
        return;
    }

    if (address >= 0xff00u && address <= 0xff7fu) {
        if (address == 0xff00u) {
            cupid_gb_input_select(gb, value);
            return;
        }
        if (cupid_cgb_handle_write_register(gb, address, value)) {
            return;
        }
        if (address == 0xff02u) {
            cupid_gb_handle_serial_transfer(gb, value);
        } else if (address == 0xff04u) {
            cupid_gb_timer_apply_div_reset(gb);
        } else if (address == 0xff40u) {
            bool lcd_was_enabled = cupid_gb_lcd_enabled(gb);
            const uint8_t previous_mode = gb->io_registers[CUPID_GB_IO_STAT] & 3u;
            if (lcd_was_enabled != ((value & 0x80u) != 0))
                gb->speed_switch_alignment = 0;

            gb->io_registers[CUPID_GB_IO_LCDC] = value;
            if ((value & 0x80u) == 0u) {
                if (lcd_was_enabled)
                    cupid_cgb_hdma_lcd_off(gb, previous_mode);
                gb->ppu_counter = 0u;
                gb->frame_ready = false;
                gb->ppu_lcd_warmup_lines = 0u;
                gb->ppu_lcd_startup = false;
                gb->ppu_line_boundary_hold = false;
                gb->window_line_counter = 0u;
                gb->io_registers[CUPID_GB_IO_LY] = 0u;
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
                cupid_gb_update_stat_irq(gb);
            } else if (!lcd_was_enabled) {
                cupid_gb_reset_ppu(gb);
            }
        } else if (address == 0xff41u) {
            gb->io_registers[CUPID_GB_IO_STAT] =
                (uint8_t)(0x80u | (value & 0x78u) | (gb->io_registers[CUPID_GB_IO_STAT] & 0x07u));
            cupid_gb_update_stat_write(gb, half_dot, true);
        } else if (address == 0xff44u) {
            gb->ppu_counter = 0u;
            gb->io_registers[CUPID_GB_IO_LY] = 0u;
            if (cupid_gb_lcd_enabled(gb)) {
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_OAM);
            } else {
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
            }
            cupid_gb_update_stat_irq(gb);
        } else if (address == 0xff45u) {
            gb->io_registers[CUPID_GB_IO_LYC] = value;
            cupid_gb_update_stat_write(gb, half_dot, false);
        } else if (address == 0xff46u) {
            gb->io_registers[CUPID_GB_IO_DMA] = value;
            cupid_gb_run_dma_transfer(gb, value);
        } else if (address == 0xff05u) {
            cupid_gb_timer_apply_tima_write(gb, value);
        } else if (address == 0xff06u) {
            cupid_gb_timer_apply_tma_write(gb, value);
        } else if (address == 0xff07u) {
            cupid_gb_timer_apply_tac_write(gb, value);
        } else if (address >= 0xff10u && address <= 0xff3fu) {
            cupid_gb_apu_on_write(gb, (uint8_t)(address - 0xff00u), value);
        } else if (address == 0xff50u) {
            /* BOOT register: writing bit 0 = 1 permanently disables boot ROM */
            if ((value & 0x01u) != 0u && gb->boot_rom_enabled) {
                gb->boot_rom_enabled = false;
            }
        } else {
            gb->io_registers[address - 0xff00u] = value;
        }
        return;
    }

    if (address >= 0xff80u && address <= 0xfffeu) {
        gb->high_ram[address - 0xff80u] = value;
        return;
    }

    if (address == 0xffffu) {
        gb->interrupt_enable = value;
    }
}

void cupid_gb_write_u8(CupidGb *gb, uint16_t address, uint8_t value) {
    cupid_gb_write_u8_phase(gb, address, value, false);
}

// Step

/**
 * @brief Executes one SM83 instruction (or one halt/stop cycle).
 *
 * Each call to this function advances the emulator by one instruction,
 * including all mid-instruction PPU/APU/timer ticks. Also handles:
 *   - Interrupt service routine dispatch (if IME and a pending interrupt)
 *   - HALT and STOP low-power modes
 *   - IME enable delay (EI takes effect after the following instruction)
 *   - Phase-sensitive LDH A,(n) and LDH (n),A handling for DIV/LCDC
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return `true` if an instruction (or halt cycle) was executed,
 *         `false` if @p gb is NULL or no ROM is loaded.
 */
static bool cupid_gb_step_internal(CupidGb *gb, CupidGbStepTrace *trace) {
    uint8_t cycles;
    uint8_t opcode;
    bool ime_enable_pending;
    CupidGbCpuBus bus = {0};

    if (trace) {
        trace->event = CUPID_GB_STEP_IDLE;
        trace->address = gb ? gb->cpu.pc : 0;
        trace->opcode = 0;
    }
    if (gb == 0 || !gb->loaded) {
        return false;
    }

    if (gb->cpu.stopped) {
        cupid_gb_tick(gb, CUPID_GB_HALT_CYCLES);
        if ((cupid_gb_read_u8(gb, 0xff00u) & 0x0fu) != 0x0fu) {
            gb->cpu.stopped = false;
            cupid_cgb_hdma_wake(gb);
            cupid_gb_tick_dma(gb);
            cupid_gb_tick(gb, 2u);
        }
        return true;
    }

    if (gb->cpu.halted) {
        /* Sample requests at the model's HALT polling point before deciding
         * whether this cycle leaves the CPU asleep or begins interrupt entry. */
        const uint8_t pending = cupid_gb_tick_halt(gb);
        const bool asleep = gb->cpu.halted;
        if (asleep) {
            if (!pending)
                return true;
            gb->cpu.halted = false;
            gb->speed_switch_halt_cycles = 0;
            cupid_cgb_hdma_wake(gb);
            cupid_gb_tick_dma(gb);
        }
        if (pending && cupid_gb_service_interrupt(gb)) {
            if (trace)
                trace->event = CUPID_GB_STEP_INTERRUPT;
            return true;
        }
        if (asleep)
            return true;
    } else if (cupid_gb_service_interrupt(gb)) {
        if (trace)
            trace->event = CUPID_GB_STEP_INTERRUPT;
        return true;
    }

    ime_enable_pending = gb->cpu.ime_delay > 0u;
    if (trace) {
        trace->event = CUPID_GB_STEP_INSTRUCTION;
        trace->address = gb->cpu.pc;
    }
    opcode = cupid_gb_fetch_u8(gb);
    cupid_cgb_run_pending_dma(gb, opcode);
    if (trace)
        trace->opcode = opcode;

    if (opcode == 0xf0u) {
        uint8_t n = cupid_gb_fetch_u8(gb);

        /* The data read is the start of M3, after the opcode and immediate
         * fetches. DIV has the same read phase as the other I/O registers. */
        cupid_gb_tick(gb, 2u);
        gb->cpu.a = cupid_gb_read_u8(gb, (uint16_t)(0xff00u + n));
        cycles = 1u;
    } else if (opcode == 0xe0u) {
        uint8_t n = cupid_gb_fetch_u8(gb);

        cupid_gb_cpu_write(gb, (uint16_t)(0xff00u + n), gb->cpu.a, 2u, &bus);
        cycles = 1u;
    } else {
        cycles = cupid_gb_execute_unprefixed(gb, opcode, &bus);
    }

    if (cycles > 0u) {
        cupid_gb_cpu_finish(gb, cycles, &bus);
    }

    if (cycles > 0u || opcode == 0x10u || opcode == 0x76u || opcode == 0xf0u || opcode == 0xe0u ||
        opcode == 0xc3u || opcode == 0xc2u || opcode == 0xcau || opcode == 0xd2u ||
        opcode == 0xdau) {
        if (opcode == 0xfbu) {
            if (ime_enable_pending) {
                gb->cpu.ime = true;
            }
            gb->cpu.ime_delay = 1u;
        } else if (opcode != 0xf3u && ime_enable_pending) {
            gb->cpu.ime_delay = 0u;
            gb->cpu.ime = true;
        }
    }

    return cycles > 0u || opcode == 0x10u || opcode == 0x76u || opcode == 0xf0u ||
           opcode == 0xe0u || opcode == 0xc3u || opcode == 0xc2u || opcode == 0xcau ||
           opcode == 0xd2u || opcode == 0xdau;
}

bool cupid_gb_step(CupidGb *gb) {
    return cupid_gb_step_internal(gb, NULL);
}

bool cupid_gb_step_observed(CupidGb *gb, CupidGbStepTrace *trace) {
    return cupid_gb_step_internal(gb, trace);
}

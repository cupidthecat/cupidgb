/**
 * @file    reset.c
 * @brief   Transactional native reset with separate guest, storage, and host lifetimes.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "cupid/gb/reset.h"

#include "cartridge_internal.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/printer.h"
#include "cupid/gb/startup.h"
#include "printer_internal.h"
#include "rtc_internal.h"

#include <stdlib.h>
#include <string.h>

static bool valid_request(const CupidGb *gb, CupidGbResetKind kind) {
    if (!gb || !gb->loaded || !gb->rom || !gb->rom_size || gb->rom_size > CUPID_GB_MAX_ROM_SIZE ||
        gb->rom_size > gb->rom_capacity || gb->cartridge_ram_size > CUPID_GB_MAX_RAM_SIZE ||
        gb->model < CUPID_GB_MODEL_DMG_ABC || gb->model > CUPID_GB_MODEL_SGB2 ||
        (kind != CUPID_GB_RESET_HARD && kind != CUPID_GB_RESET_QUICK))
        return false;
    return !gb->boot_rom_size ||
           gb->boot_rom_size == (gb->model == CUPID_GB_MODEL_CGB ? 0x900u : 0x100u);
}

static void restore_cartridge(CupidGb *next, const CupidGb *previous) {
    next->header = previous->header;
    next->rom_size = previous->rom_size;
    next->cartridge_ram_size = previous->cartridge_ram_size;
    next->rom_bank_count = previous->rom_bank_count;
    next->ram_bank_count = previous->ram_bank_count;
    next->mbc1_multicart = previous->mbc1_multicart;
    next->mmm01_menu_first = previous->mmm01_menu_first;
    memcpy(next->save_path, previous->save_path, sizeof(next->save_path));
    memcpy(next->boot_rom, previous->boot_rom, sizeof(next->boot_rom));
    next->boot_rom_size = previous->boot_rom_size;
    next->loaded = true;
}

static void restore_retained_memory(CupidGb *next, const CupidGb *previous) {
    memcpy(next->work_ram, previous->work_ram, sizeof(next->work_ram));
    memcpy(next->high_ram, previous->high_ram, sizeof(next->high_ram));
    memcpy(next->object_attribute_memory, previous->object_attribute_memory,
           sizeof(next->object_attribute_memory));
    memcpy(next->cgb_bg_palette_ram, previous->cgb_bg_palette_ram,
           sizeof(next->cgb_bg_palette_ram));
    memcpy(next->cgb_obj_palette_ram, previous->cgb_obj_palette_ram,
           sizeof(next->cgb_obj_palette_ram));
    next->io_registers[0x46] = previous->io_registers[0x46];
    next->io_registers[0x48] = previous->io_registers[0x48];
    next->io_registers[0x49] = previous->io_registers[0x49];
}

static void restore_clocks(CupidGb *next, const CupidGb *previous) {
    next->rtc = previous->rtc;
    next->rtc.latch_prev = 0;
    next->rtc_cycles = previous->rtc_cycles;
    next->tpp1.status = previous->tpp1.status;
    memcpy(next->tpp1.live, previous->tpp1.live, sizeof(next->tpp1.live));
    memcpy(next->tpp1.latched, previous->tpp1.latched, sizeof(next->tpp1.latched));
    next->tpp1.rtc_base_time = previous->tpp1.rtc_base_time;
    /* HuC3 stores time and alarm digits in its controller domain. Those reset;
     * only the host timestamp is established again after all fallible preparation. */
}

bool cupid_gb_reset(CupidGb *gb, CupidGbResetKind kind) {
    if (!valid_request(gb, kind))
        return false;
    CupidGb *next = calloc(1, sizeof(*next));
    if (!next)
        return false;

    next->model = gb->model;
    next->rom = gb->rom;
    next->rom_capacity = gb->rom_capacity;
    next->rom_generation = gb->rom_generation;
    next->power_on = gb->power_on;
    next->rtc_policy = gb->rtc_policy;
    if (kind == CUPID_GB_RESET_QUICK) {
        /* Temporary generation is deterministic and never consumes fresh entropy.
         * The live physical bytes and the complete host policy are restored below. */
        next->power_on.fixed_seed = true;
        next->power_on.seed = gb->power_on.generation_seed;
    }
    /* The candidate has no attached accessory or observers. Initialization cannot
     * mutate the live printer or publish an intermediate machine through callbacks. */
    cupid_gb_init(next);
    restore_cartridge(next, gb);
    cupid_gb_cartridge_post_boot(next);
    cupid_gb_cartridge_reset_controls(next);
    restore_clocks(next, gb);
    if (kind == CUPID_GB_RESET_QUICK || gb->header.has_battery)
        memcpy(next->cartridge_ram, gb->cartridge_ram, sizeof(next->cartridge_ram));
    else
        memset(next->cartridge_ram, 0xff, next->cartridge_ram_size);

    if (next->boot_rom_size)
        cupid_gb_start_boot_rom(next);
    if (kind == CUPID_GB_RESET_QUICK) {
        restore_retained_memory(next, gb);
        next->power_on = gb->power_on;
        if (next->boot_rom_size) {
            /* Wave storage belongs to I/O, not to the retained physical RAM. */
            memset(next->io_registers + 0x30, 0, 16);
        } else if (next->model == CUPID_GB_MODEL_CGB) {
            /* The approximate handoff includes the color firmware's clearing work.
             * This is a post-boot effect, separate from native boundary retention. */
            memset(next->work_ram + 0x2000, 0, 0x1000);
            memset(next->object_attribute_memory, 0, sizeof(next->object_attribute_memory));
        }
    }

    next->apu.output = gb->apu.output;
    next->cheats = gb->cheats;
    next->serial_output_callback = gb->serial_output_callback;
    next->serial_output_user_data = gb->serial_output_user_data;
    next->serial_link = gb->serial_link;
    next->rumble_callback = gb->rumble_callback;
    next->rumble_user_data = gb->rumble_user_data;
    next->rumble_strength_callback = gb->rumble_strength_callback;
    next->rumble_strength_user_data = gb->rumble_strength_user_data;
    next->rumble_sample_callback = gb->rumble_sample_callback;
    next->rumble_sample_user_data = gb->rumble_sample_user_data;
    next->presentation_callback = gb->presentation_callback;
    next->presentation_user_data = gb->presentation_user_data;
    next->infrared_callback = gb->infrared_callback;
    next->infrared_user_data = gb->infrared_user_data;
    next->memory_access_callback = gb->memory_access_callback;
    next->memory_access_user_data = gb->memory_access_user_data;
    next->mbc7.motion_x = gb->mbc7.motion_x;
    next->mbc7.motion_y = gb->mbc7.motion_y;
    next->joypad = gb->joypad;
    memcpy(next->joypad_extra, gb->joypad_extra, sizeof(next->joypad_extra));
    next->joyp_sample_pending = gb->joyp_sample_pending;
    next->vblank_events = gb->vblank_events;
    next->battery_write_generation = gb->battery_write_generation;
    if (gb->header.mbc_type == CUPID_GB_HUC3 &&
        (gb->huc3.minutes || gb->huc3.days || gb->huc3.alarm_minutes || gb->huc3.alarm_days ||
         gb->huc3.alarm_enabled))
        ++next->battery_write_generation;

    /* Nothing after the time observation can fail. Clock providers must obey the
     * documented non-reentrant observer contract. Rebase without advancing digits. */
    cupid_gb_rtc_bind_cartridge(next);
    *gb = *next;
    free(next);
    CupidGbPrinter *printer = cupid_gb_printer_attached(gb);
    if (printer)
        cupid_gb_printer_state_clear(&printer->state, gb->elapsed_cycles);
    return true;
}

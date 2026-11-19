/**
 * @file    audio_output.h
 * @brief   Internal interfaces for channel reconstruction and audio delivery.
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
#ifndef CUPID_GB_AUDIO_OUTPUT_INTERNAL_H
#define CUPID_GB_AUDIO_OUTPUT_INTERNAL_H

#include "cupid/gb/gb.h"

enum { CUPID_GB_POST_BOOT_NR50 = 0x77, CUPID_GB_POST_BOOT_NR51 = 0xf3 };

/* Only built-in CGB A–E cartridge entry supplies settled filter history. */
static inline bool cupid_gb_audio_startup_filter_supported(const CupidGb *gb) {
    return cupid_gb_model_is_cgb(gb->model) && gb->model != CUPID_GB_MODEL_CGB_0 &&
           gb->program_kind == CUPID_GB_PROGRAM_CARTRIDGE && !gb->boot_rom_enabled;
}

void cupid_gb_audio_configure(CupidGb *gb);
uint8_t cupid_gb_audio_dac_mask(const CupidGb *gb);
void cupid_gb_audio_credit(CupidGb *gb, unsigned base_cycles);
void cupid_gb_audio_advance(CupidGb *gb, unsigned base_cycles);
void cupid_gb_audio_render(CupidGb *gb);
void cupid_gb_audio_update(CupidGb *gb, unsigned channel, uint8_t value, unsigned base_offset,
                           bool force);
void cupid_gb_audio_update_channels(CupidGb *gb, uint8_t channels, unsigned base_offset,
                                    bool force);
void cupid_gb_apu_channel_samples(const CupidGb *gb, uint8_t samples[4]);
void cupid_gb_apu_apply_cgb_boot_pulse(CupidGb *gb, unsigned native_divider,
                                       unsigned compatibility_cycles);

#endif

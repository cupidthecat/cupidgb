/**
 * @file    pixels.c
 * @brief   Dot-driven background and object fetching, pixel queues, and composition.
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

#include "cupid/gb/ppu.h"

#include "cupid/gbc/cgb.h"
#include "oam_bus.h"
#include "vram_bus.h"

#include <limits.h>
#include <string.h>

static uint16_t palette_color(const uint8_t *palette, unsigned index) {
    return (uint16_t)((palette[index * 2] | (uint16_t)palette[index * 2 + 1] << 8) & 0x7fff);
}

void cupid_gb_pixels_begin_line(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    gb->object_fetch_stage = CUPID_GB_OBJECT_IDLE;
    p->object_delay = 0;
    p->object_count = 0;
    p->fetched_objects = 0;
    p->active = false;
    gb->pixel_timing = (CupidGbPixelTiming){0};
    gb->ppu_transfer_end_dot = 0;
    if (p->line == gb->io_registers[CUPID_GB_IO_WY])
        p->window_armed = true;
}

static uint8_t ppu_oam_read(const CupidGb *gb, uint8_t requested) {
    if (gb->cpu.stopped && gb->stop_ppu_oam_blocked)
        return 0xff;
    uint8_t address = requested;
    if ((gb->dma_active || gb->dma_start_delay) && gb->dma_index && gb->dma_index <= 160u) {
        if (gb->hdma_running)
            address = (uint8_t)((gb->hdma_source & ~1u) | (requested & 1u));
        else if (gb->dma_index != 160u)
            address = (uint8_t)((gb->dma_index & ~1u) | (requested & 1u));
    }
    return cupid_gb_oam_read_raw(gb, address);
}

static uint16_t read_oam_word(const CupidGb *gb, unsigned requested) {
    uint8_t lo = ppu_oam_read(gb, (uint8_t)requested);
    uint8_t hi = ppu_oam_read(gb, (uint8_t)(requested + 1u));
    return (uint16_t)(lo | (uint16_t)hi << 8);
}

void cupid_gb_pixels_scan_object(CupidGb *gb, unsigned index) {
    CupidGbPixels *p = &gb->pixels;
    bool dma = gb->dma_active || gb->dma_start_delay;
    bool stationary = gb->cpu.halted || gb->cpu.stopped;
    if (!dma || stationary)
        gb->oam_search_word = read_oam_word(gb, index * 4);
    if (dma && stationary && !cupid_gb_model_is_cgb(gb->model))
        return;
    if (gb->cpu.stopped && gb->stop_ppu_oam_blocked)
        return;
    unsigned height = gb->io_registers[CUPID_GB_IO_LCDC] & 4 ? 16 : 8;
    uint8_t y = (uint8_t)gb->oam_search_word;
    if ((unsigned)p->line + 16 < y || (unsigned)p->line + 16 >= (unsigned)y + height ||
        p->object_count == 10)
        return;
    unsigned slot = p->object_count++;
    p->objects[slot] = (uint8_t)index;
    p->object_x[slot] = (uint8_t)(gb->oam_search_word >> 8);
    p->object_y[slot] = y;
}

void cupid_gb_pixels_begin_transfer(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    cupid_gb_vram_bus_begin_transfer(gb);
    p->x = p->read = p->phase = p->column = 0;
    /* The right-edge monochrome window leaves its next tile selected. */
    if (p->window_active)
        p->column = 1;
    /* These initial slots are discarded, but still arbitrate object fetching. */
    p->count = 8;
    memset(p->colors, 0, sizeof p->colors);
    memset(p->palettes, 0, sizeof p->palettes);
    memset(p->priorities, 0, sizeof p->priorities);
    p->discard = gb->io_registers[CUPID_GB_IO_SCX] & 7;
    p->delay = 12;
    p->object_delay = 0;
    p->window_used = p->window_active;
    gb->pixel_timing = (CupidGbPixelTiming){.logical_x = -16};
    memset(p->sprite_colors, 0, sizeof(p->sprite_colors));
    memset(p->sprite_priorities, 0xff, sizeof(p->sprite_priorities));
    p->active = true;
}

static uint16_t tile_address(CupidGb *gb, bool upper) {
    CupidGbPixels *p = &gb->pixels;
    uint8_t lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    unsigned row;
    if (cupid_gb_model_is_cgb(gb->model))
        row = gb->pixel_timing.fetch_y;
    else
        row = p->window_active ? gb->window_line_counter
                               : (uint8_t)(p->line + gb->io_registers[CUPID_GB_IO_SCY]);
    int base = lcdc & 0x10 ? p->tile * 16 : 0x1000 + (int8_t)p->tile * 16;
    row = (row & 7) ^ (p->attributes & 0x40 ? 7u : 0u);
    return (uint16_t)((unsigned)base + (p->attributes & 8 ? 0x2000u : 0u) + row * 2 + upper);
}

static uint8_t fetch_vram(CupidGb *gb, uint16_t address) {
    if (gb->cpu.stopped && gb->stop_ppu_vram_blocked)
        return 0xff;
    if (gb->hdma_running) {
        gb->hdma_vram_fetch = address;
        return 0;
    }
    const bool shared = gb->dma_active && gb->dma_index && gb->dma_index <= 160u &&
                        (gb->dma_source_base & 0xe000u) == 0x8000u;
    if (!shared)
        return gb->video_ram[address];

    const bool color = cupid_gb_model_is_cgb(gb->model);
    const bool stationary = gb->cpu.halted || gb->cpu.stopped;
    const unsigned destination = gb->dma_index - (stationary ? 0u : 1u);
    const unsigned sourceRow = (gb->dma_source_base + destination) & 0x1fffu;

    enum Port { Retained, DmaLines, SharedLines };
    const enum Port port = color && gb->oam_vram_row != UINT16_MAX ? Retained
                           : color && !stationary && gb->dma_phase ? DmaLines
                                                                   : SharedLines;
    unsigned row;
    switch (port) {
        case Retained:
            row = gb->oam_vram_row;
            break;
        case DmaLines:
            row = sourceRow;
            break;
        default:
            row = color ? sourceRow & address : sourceRow | address;
            row &= 0x1fffu;
            if (color && !stationary)
                gb->oam_vram_row = (uint16_t)row;
            break;
    }
    const unsigned resolved = (address & 0x2000u) | row;
    if (destination < 160u) {
        const unsigned dmaBank = gb->cgb_vram_bank ? 0x2000u : 0;
        const uint8_t value = gb->video_ram[dmaBank | row];
        gb->object_attribute_memory[destination] = value;
    }
    return gb->video_ram[resolved];
}

void cupid_gb_pixels_window_write(CupidGb *gb) {
    /* WX is sampled on each address half and can change between the two halves
     * of a fetch. Retain that result until the corresponding byte-read edge. */
    CupidGbPixelTiming *timing = &gb->pixel_timing;
    const int position = timing->logical_x;
    const unsigned wx = gb->io_registers[CUPID_GB_IO_WX];
    bool glitch = false;
    if (cupid_gb_model_is_cgb(gb->model) && gb->pixels.window_armed &&
        (gb->io_registers[CUPID_GB_IO_LCDC] & 0x20)) {
        if (wx == 0)
            glitch =
                position <= -8 || (position == -7 && (timing->flags & CUPID_GB_PIXEL_FRACTIONAL));
        else
            glitch = position + 7 + ((timing->flags & CUPID_GB_PIXEL_WINDOW_FIRST) != 0) == (int)wx;
    }
    timing->flags = (uint8_t)((timing->flags & ~CUPID_GB_PIXEL_WX_GLITCH) |
                              (glitch ? CUPID_GB_PIXEL_WX_GLITCH : 0));
}

static void fetch_background(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    uint8_t lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    switch (p->phase) {
        case 0: {
            cupid_gb_pixels_window_write(gb);
            if (!(lcdc & 0x20)) {
                p->window_active = false;
                gb->pixel_timing.flags &= (uint8_t)~CUPID_GB_PIXEL_WINDOW_FIRST;
            }
            unsigned y = p->window_active ? gb->window_line_counter
                                          : (uint8_t)(p->line + gb->io_registers[CUPID_GB_IO_SCY]);
            if (cupid_gb_model_is_cgb(gb->model))
                gb->pixel_timing.fetch_y = (uint8_t)y;
            unsigned column = p->column;
            if (!p->window_active) {
                const int position = gb->pixel_timing.logical_x;
                const bool fetching_object =
                    gb->object_fetch_stage == CUPID_GB_OBJECT_WAIT_BACKGROUND ||
                    gb->object_fetch_stage == CUPID_GB_OBJECT_READ_WORD;
                const unsigned ahead =
                    cupid_gb_model_is_cgb(gb->model) && !fetching_object ? 7u : 8u;
                column = position < -8
                             ? gb->io_registers[CUPID_GB_IO_SCX] >> 3
                             : (uint8_t)(gb->io_registers[CUPID_GB_IO_SCX] + position + ahead) / 8u;
            }
            unsigned map = lcdc & (p->window_active ? 0x40 : 8) ? 0x1c00 : 0x1800;
            p->address = (uint16_t)(map + (y >> 3) * 32 + (column & 31));
            gb->vram_bus.fetch_map_address = p->address;
            ++p->phase;
            break;
        }
        case 1:
            if (!(gb->pixel_timing.flags & CUPID_GB_PIXEL_WX_GLITCH)) {
                p->tile = fetch_vram(gb, p->address);
                /* Physical color hardware reads both banks in compatibility mode. */
                p->attributes =
                    cupid_gb_model_is_cgb(gb->model) ? fetch_vram(gb, p->address + 0x2000u) : 0;
            }
            ++p->phase;
            break;
        case 2:
            cupid_gb_pixels_window_write(gb);
            p->address = tile_address(gb, false);
            gb->vram_bus.fetch_data_address = p->address;
            ++p->phase;
            break;
        case 3:
            p->low = gb->pixel_timing.flags & CUPID_GB_PIXEL_WX_GLITCH ? p->high
                                                                       : fetch_vram(gb, p->address);
            ++p->phase;
            break;
        case 4:
            cupid_gb_pixels_window_write(gb);
            p->address = tile_address(gb, true);
            gb->vram_bus.fetch_data_address = p->address;
            ++p->phase;
            break;
        case 5:
            ++p->phase;
            if (gb->pixel_timing.flags & CUPID_GB_PIXEL_WX_GLITCH) {
                p->high = p->low;
                /* A repeated byte still occupies the read edge; enqueue follows. */
                break;
            }
            p->high = fetch_vram(gb, p->address);
            /* The completed row may enter an empty queue on this dot. */
            /* fall through */
        case 6:
            if (p->count)
                break;
            for (unsigned i = 0; i < 8; ++i) {
                unsigned bit = p->attributes & 0x20 ? i : 7 - i;
                p->colors[i] = (uint8_t)(((p->low >> bit) & 1) | ((p->high >> bit) & 1) * 2);
                p->palettes[i] = p->attributes & 7;
                p->priorities[i] = p->attributes >> 7;
            }
            p->read = 0;
            p->count = 8;
            p->column = (p->column + 1) & 31;
            p->phase = 0;
            break;
    }
}

static int due_object(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    if (!p->object_count)
        return -1;
    if (!(gb->io_registers[CUPID_GB_IO_LCDC] & 2) && !cupid_gb_model_is_cgb(gb->model))
        return -1;
    /* Older snapshots may retain fetched bits above the selected slot count. */
    const unsigned selected_mask = (1u << p->object_count) - 1u;
    if ((p->fetched_objects & selected_mask) == selected_mask)
        return -1;
    const unsigned match =
        gb->pixel_timing.logical_x < -8 ? 0u : (unsigned)(gb->pixel_timing.logical_x + 8);
    for (unsigned slot = 0; slot < p->object_count; slot++) {
        uint16_t bit = (uint16_t)(1u << slot);
        if (p->fetched_objects & bit)
            continue;
        if (p->object_x[slot] < match) {
            p->fetched_objects |= bit;
            continue;
        }
        if (p->object_x[slot] == match && p->object_x[slot] < 168)
            return (int)slot;
    }
    return -1;
}

static void begin_object(CupidGb *gb, unsigned slot) {
    CupidGbPixels *p = &gb->pixels;
    p->fetched_objects |= (uint16_t)(1u << slot);
    p->object_index = (uint8_t)slot;
    gb->object_fetch_stage = CUPID_GB_OBJECT_WAIT_BACKGROUND;
}

static uint16_t object_address(const CupidGb *gb) {
    const CupidGbPixels *p = &gb->pixels;
    unsigned height = gb->io_registers[CUPID_GB_IO_LCDC] & 4 ? 16 : 8;
    unsigned row = ((unsigned)p->line + 16 - p->object_y[p->object_index]) & (height - 1);
    if (p->object_attributes & 0x40)
        row ^= height - 1;
    unsigned tile = (uint8_t)gb->oam_search_word;
    if (height == 16)
        tile &= 0xfe;
    unsigned bank = gb->cgb_mode && (p->object_attributes & 8) ? 0x2000 : 0;
    return (uint16_t)(bank + tile * 16 + row * 2);
}

static void overlay_object(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    unsigned slot = p->object_index;
    bool coordinate_priority = !gb->cgb_mode || gb->cgb_object_priority_x;
    unsigned priority =
        coordinate_priority ? p->object_x[slot] * 64u + p->objects[slot] : p->objects[slot];
    for (unsigned i = 0; i < 8; ++i) {
        int screen = (int)p->object_x[slot] - 8 + (int)i;
        int queue = screen - p->x;
        if (queue < 0 || queue >= 8)
            continue;
        unsigned bit = p->object_attributes & 0x20 ? i : 7 - i;
        uint8_t color = (uint8_t)(((p->object_low >> bit) & 1) | ((p->object_high >> bit) & 1) * 2);
        if (!color || priority >= p->sprite_priorities[queue])
            continue;
        p->sprite_colors[queue] = color;
        p->sprite_palettes[queue] =
            gb->cgb_mode ? p->object_attributes & 7 : (p->object_attributes >> 4) & 1;
        p->sprite_flags[queue] = p->object_attributes >> 7;
        p->sprite_priorities[queue] = (uint16_t)priority;
    }
}

#if defined(__GNUC__) && !defined(__clang__) && defined(__x86_64__)
/* GCC can lower the lane loop to four out-of-line memmoves per pixel. The
 * fixed-size chunk path avoids those calls on the measured x86-64 target.
 * Other architectures and compilers retain the loop.
 * Capture both chunks before either store. Their overlapping destination bytes
 * agree, preserving every lane with fixed-size copies that can remain inline.
 * memcpy avoids alignment and host-byte-order assumptions. */
static void shift_sprite_bytes(uint8_t values[8]) {
    uint32_t first, last;
    memcpy(&first, values + 1, sizeof first);
    memcpy(&last, values + 4, sizeof last);
    memcpy(values, &first, sizeof first);
    memcpy(values + 3, &last, sizeof last);
}

static void shift_sprite_priorities(uint16_t values[8]) {
    uint64_t first, last;
    memcpy(&first, values + 1, sizeof first);
    memcpy(&last, values + 4, sizeof last);
    memcpy(values, &first, sizeof first);
    memcpy(values + 3, &last, sizeof last);
}
#endif

static void emit_pixel(CupidGb *gb, bool inserted) {
    CupidGbPixels *p = &gb->pixels;
    uint8_t lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    uint8_t raw = inserted ? 0 : p->colors[p->read];
    bool background_priority = !inserted && p->priorities[p->read] != 0;
    if (!(lcdc & 1)) {
        background_priority = false;
        if (!gb->cgb_mode)
            raw = 0;
    }
    bool object = p->sprite_colors[0] && (lcdc & 2);
    if (raw && (lcdc & 1) && (background_priority || p->sprite_flags[0]))
        object = false;
    uint8_t color = object ? p->sprite_colors[0] : raw;
    size_t index = (size_t)p->line * CUPID_GB_SCREEN_WIDTH + p->x;
    if (gb->cgb_mode) {
        const uint8_t *palette = object ? gb->cgb_obj_palette_ram : gb->cgb_bg_palette_ram;
        unsigned number = object ? p->sprite_palettes[0] : inserted ? 0 : p->palettes[p->read];
        gb->frame_buffer_color[index] = gb->cpu.stopped && gb->stop_ppu_palette_blocked
                                            ? 0
                                            : palette_color(palette, number * 4 + color);
        gb->frame_buffer[index] = color;
    } else {
        unsigned reg = object ? CUPID_GB_IO_OBP0 + p->sprite_palettes[0] : CUPID_GB_IO_BGP;
        uint8_t shade = (gb->io_registers[reg] >> (color * 2)) & 3;
        gb->frame_buffer[index] = shade;
        if (cupid_cgb_compat_active(gb))
            gb->frame_buffer_color[index] =
                gb->cpu.stopped && gb->stop_ppu_palette_blocked ? 0
                : object ? cupid_cgb_compat_obj_color(gb, p->sprite_palettes[0], shade)
                         : cupid_cgb_compat_bg_color(gb, shade);
    }
    if (p->window_active)
        p->window_used = true;
    ++p->x;
#if defined(__GNUC__) && !defined(__clang__) && defined(__x86_64__)
    shift_sprite_bytes(p->sprite_colors);
    shift_sprite_bytes(p->sprite_palettes);
    shift_sprite_bytes(p->sprite_flags);
    shift_sprite_priorities(p->sprite_priorities);
#else
    for (unsigned i = 0; i < 7; ++i) {
        p->sprite_colors[i] = p->sprite_colors[i + 1];
        p->sprite_palettes[i] = p->sprite_palettes[i + 1];
        p->sprite_flags[i] = p->sprite_flags[i + 1];
        p->sprite_priorities[i] = p->sprite_priorities[i + 1];
    }
#endif
    p->sprite_colors[7] = 0;
    p->sprite_priorities[7] = UINT16_MAX;
}

/* True means the restart consumed this dot before any fetch work. */
static bool start_window(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    CupidGbPixelTiming *timing = &gb->pixel_timing;
    const unsigned wx = gb->io_registers[CUPID_GB_IO_WX];
    const unsigned scroll = gb->io_registers[CUPID_GB_IO_SCX] & 7u;
    const bool color = cupid_gb_model_is_cgb(gb->model);
    const int position = timing->logical_x;
    if (p->window_active || !p->window_armed || !(gb->io_registers[CUPID_GB_IO_LCDC] & 0x20))
        return false;
    bool reached = wx < (color ? 167u : 166u) && position + 7 == (int)wx;
    if (wx == 0)
        reached = position >= -15 && position <= -7;
    if (wx == 0 && scroll && position == -16)
        reached = true;
    if (!reached)
        return false;

    if (p->window_used)
        ++gb->window_line_counter;
    p->window_active = p->window_used = true;
    p->count = p->read = p->phase = p->column = p->delay = 0;
    timing->flags |= CUPID_GB_PIXEL_WINDOW_FIRST;
    if (wx == 0 && scroll && !color) {
        /* The monochrome window restart spends one dot before its first read. */
        timing->flags |= CUPID_GB_PIXEL_WINDOW_HOLD;
        return true;
    }
    return false;
}

static void pop_background(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    CupidGbPixelTiming *timing = &gb->pixel_timing;
    int position = timing->logical_x;
    const bool inserted = (timing->flags & CUPID_GB_PIXEL_BG_INSERT) != 0;
    timing->flags &= (uint8_t)~CUPID_GB_PIXEL_BG_INSERT;
    if (position < -8) {
        const unsigned alignment = (unsigned)(position + 16) & 7u;
        const unsigned scroll = gb->io_registers[CUPID_GB_IO_SCX] & 7u;
        const bool window_edge =
            (timing->flags & CUPID_GB_PIXEL_WINDOW_FIRST) && alignment == 6 && scroll == 7;
        if (alignment == scroll || window_edge)
            position = -8;
        else if (position == -9) {
            timing->logical_x = -16;
            if (!inserted) {
                p->read = (p->read + 1) & 7;
                --p->count;
            }
            return;
        } else
            timing->flags |= CUPID_GB_PIXEL_FRACTIONAL;
    }
    timing->flags &= (uint8_t)~CUPID_GB_PIXEL_WINDOW_FIRST;
    if (position >= 0)
        emit_pixel(gb, inserted);
    timing->logical_x = (int16_t)(position + 1);
    p->discard = position < -1 && position >= -8 ? (uint8_t)(-position - 1) : 0;
    /* A zero inserted at the window edge leaves the fetched tile queued. */
    if (!inserted) {
        p->read = (p->read + 1) & 7;
        --p->count;
    }
}

bool cupid_gb_pixels_abort_object(CupidGb *gb) {
    const CupidGbObjectFetchStage stage = gb->object_fetch_stage;
    if (cupid_gb_model_is_cgb(gb->model) ||
        (stage != CUPID_GB_OBJECT_WAIT_BACKGROUND && stage != CUPID_GB_OBJECT_READ_WORD &&
         !(stage == CUPID_GB_OBJECT_IDLE && gb->pixels.object_delay >= 2)))
        return false;
    /* Keep the slot until the next pipeline edge. Re-enabling objects before
     * that edge must not turn the aborted transaction into a completed one. */
    gb->object_fetch_stage = CUPID_GB_OBJECT_ABORTED;
    gb->pixels.object_delay = 0;
    return true;
}

void cupid_gb_pixels_finish_line(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    const bool carry_window = !cupid_gb_model_is_cgb(gb->model) && p->window_armed &&
                              (gb->io_registers[CUPID_GB_IO_LCDC] & 0x20) &&
                              gb->io_registers[CUPID_GB_IO_WX] == 166;
    if (p->window_used || carry_window)
        ++gb->window_line_counter;
    p->window_active = carry_window;
    gb->pixel_timing = (CupidGbPixelTiming){.logical_x = 160};
}

bool cupid_gb_pixels_tick(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    if (p->x == CUPID_GB_SCREEN_WIDTH)
        return true;
    if (p->delay > 7) {
        --p->delay;
        return false;
    }
    gb->pixel_timing.flags &= (uint8_t)~CUPID_GB_PIXEL_WINDOW_HOLD;
    const bool aborted = gb->object_fetch_stage == CUPID_GB_OBJECT_ABORTED;
    if (aborted) {
        p->fetched_objects &= (uint16_t)~(1u << p->object_index);
        gb->object_fetch_stage = CUPID_GB_OBJECT_IDLE;
    } else if (gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE && !p->object_delay &&
               start_window(gb))
        return false;
    if (!aborted && gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE && !p->object_delay &&
        p->window_active && !(gb->pixel_timing.flags & CUPID_GB_PIXEL_WINDOW_FIRST) && !p->phase &&
        p->count == 8 &&
        (!cupid_gb_model_is_cgb(gb->model) || gb->io_registers[CUPID_GB_IO_WX] == 0) &&
        gb->pixel_timing.logical_x + 7 == gb->io_registers[CUPID_GB_IO_WX])
        gb->pixel_timing.flags |= CUPID_GB_PIXEL_BG_INSERT;
    /* Older imported transactions finish using their captured tile address. */
    if (gb->object_fetch_stage == CUPID_GB_OBJECT_LEGACY) {
        if (p->object_delay == 4)
            p->object_low = fetch_vram(gb, p->object_address);
        if (p->object_delay == 2)
            p->object_high = fetch_vram(gb, p->object_address + 1u);
        if (--p->object_delay == 0) {
            overlay_object(gb);
            gb->object_fetch_stage = CUPID_GB_OBJECT_IDLE;
        }
        return false;
    }
    /* The stage owns alignment and the OAM-word edge. A zero stage may still
     * have a nonzero data countdown while sprite low/high reads complete. */
    if (p->object_delay) {
        if (p->object_delay == 4) {
            p->object_address = object_address(gb);
            p->object_low = fetch_vram(gb, p->object_address);
        }
        if (p->object_delay == 2) {
            p->object_address = object_address(gb);
            p->object_high = fetch_vram(gb, p->object_address + 1u);
        }
        if (--p->object_delay)
            return false;
        overlay_object(gb);
    }
    if (!aborted && gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE && p->count) {
        int slot = due_object(gb);
        if (slot >= 0)
            begin_object(gb, (unsigned)slot);
    }
    if (gb->object_fetch_stage == CUPID_GB_OBJECT_WAIT_BACKGROUND) {
        bool ready = p->phase >= 5 && p->count;
        fetch_background(gb);
        if (ready)
            gb->object_fetch_stage = CUPID_GB_OBJECT_READ_WORD;
        return false;
    }
    if (gb->object_fetch_stage == CUPID_GB_OBJECT_READ_WORD) {
        fetch_background(gb);
        unsigned index = p->objects[p->object_index];
        gb->oam_search_word = read_oam_word(gb, index * 4 + 2);
        p->object_attributes = (uint8_t)(gb->oam_search_word >> 8);
        p->object_delay = 5;
        gb->object_fetch_stage = CUPID_GB_OBJECT_IDLE;
        return false;
    }
    if (p->count)
        pop_background(gb);
    if (p->delay)
        --p->delay;
    fetch_background(gb);
    /* Transfer ends on the last emitted pixel's dot, after its fetch advance. */
    return p->x == CUPID_GB_SCREEN_WIDTH;
}

bool cupid_gb_pixels_timing_valid(const CupidGb *gb) {
    if (!gb)
        return false;
    const CupidGbPixelTiming *timing = &gb->pixel_timing;
    const CupidGbPixels *p = &gb->pixels;
    if (timing->logical_x < -16 || timing->logical_x > 160 ||
        (timing->flags & ~CUPID_GB_PIXEL_TIMING_FLAGS) ||
        ((timing->flags & CUPID_GB_PIXEL_WX_GLITCH) && !cupid_gb_model_is_cgb(gb->model)))
        return false;
    if (timing->flags & CUPID_GB_PIXEL_WINDOW_HOLD)
        return !cupid_gb_model_is_cgb(gb->model) && p->active && p->window_active &&
               (timing->flags & CUPID_GB_PIXEL_WINDOW_FIRST) && !p->count && !p->phase &&
               !p->delay && !p->object_delay && gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE &&
               timing->logical_x <= -7 && !(timing->flags & CUPID_GB_PIXEL_BG_INSERT);
    if (timing->flags & CUPID_GB_PIXEL_BG_INSERT) {
        const bool waiting = gb->object_fetch_stage == CUPID_GB_OBJECT_WAIT_BACKGROUND ||
                             gb->object_fetch_stage == CUPID_GB_OBJECT_READ_WORD ||
                             gb->object_fetch_stage == CUPID_GB_OBJECT_ABORTED ||
                             (gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE &&
                              p->object_delay >= 1 && p->object_delay <= 5);
        return p->active && p->line < 144 && p->x < 160 && p->count == 8 && waiting &&
               timing->logical_x >= -7 && timing->logical_x < 160 &&
               !(timing->flags & CUPID_GB_PIXEL_WINDOW_FIRST);
    }
    return true;
}

bool cupid_gb_pixels_timing_encode(const CupidGb *gb, uint8_t *data, size_t size) {
    if (!data || size != CUPID_GB_PIXEL_TIMING_STATE_SIZE || !cupid_gb_pixels_timing_valid(gb))
        return false;
    const uint16_t position = (uint16_t)gb->pixel_timing.logical_x;
    data[0] = (uint8_t)position;
    data[1] = (uint8_t)(position >> 8);
    data[2] = gb->pixel_timing.flags;
    data[3] = gb->pixel_timing.fetch_y;
    return true;
}

bool cupid_gb_pixels_timing_decode(CupidGb *gb, const uint8_t *data, size_t size) {
    if (!gb || !data || size != CUPID_GB_PIXEL_TIMING_STATE_SIZE)
        return false;
    const unsigned position = data[0] | (unsigned)data[1] << 8;
    const CupidGbPixelTiming previous = gb->pixel_timing;
    gb->pixel_timing.logical_x =
        (int16_t)(position < 0x8000u ? (int)position : (int)position - 0x10000);
    gb->pixel_timing.flags = data[2];
    gb->pixel_timing.fetch_y = data[3];
    if (cupid_gb_pixels_timing_valid(gb))
        return true;
    gb->pixel_timing = previous;
    return false;
}

void cupid_gb_pixels_import_legacy(CupidGb *gb) {
    if (!gb)
        return;
    CupidGbPixelTiming *timing = &gb->pixel_timing;
    const CupidGbPixels *p = &gb->pixels;
    *timing = (CupidGbPixelTiming){.logical_x = p->x};
    /* The tile-map row is absent from these files. Reconstruct it from their
     * saved window/scroll registers; an intervening old SCY write is unknowable. */
    timing->fetch_y = p->window_active ? gb->window_line_counter
                                       : (uint8_t)(p->line + gb->io_registers[CUPID_GB_IO_SCY]);
    if (!p->active)
        return;
    const unsigned scroll = gb->io_registers[CUPID_GB_IO_SCX] & 7u;
    if (!p->window_active && !p->column) {
        const unsigned consumed = p->delay > 7 ? 0u : 8u - p->count;
        timing->logical_x = consumed <= scroll ? (int16_t)((int)consumed - 16)
                                               : (int16_t)((int)consumed - (int)scroll - 8);
    } else if (p->discard)
        timing->logical_x = -(int16_t)p->discard;
    if (scroll)
        timing->flags |= CUPID_GB_PIXEL_FRACTIONAL;
    if (p->window_active && p->delay)
        timing->flags |= CUPID_GB_PIXEL_WINDOW_FIRST;
    cupid_gb_pixels_window_write(gb);
}

#include "cupid/gb/ppu.h"

#include "cupid/gbc/cgb.h"

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
    if (p->line == gb->io_registers[CUPID_GB_IO_WY])
        p->window_armed = true;
}

static uint8_t raw_oam_read(const CupidGb *gb, uint8_t address) {
    /* Unused OAM addresses use a hardware readback circuit, not storage. */
    if (address < sizeof gb->object_attribute_memory)
        return gb->object_attribute_memory[address];
    return gb->model == CUPID_GB_MODEL_CGB ? (uint8_t)((address & 0xf0u) | (address >> 4)) : 0;
}

static uint8_t ppu_oam_read(const CupidGb *gb, uint8_t requested) {
    uint8_t address = requested;
    if ((gb->dma_active || gb->dma_start_delay) && gb->dma_index && gb->dma_index <= 160u) {
        if (gb->hdma_running)
            address = (uint8_t)((gb->hdma_source & ~1u) | (requested & 1u));
        else if (gb->dma_index != 160u)
            address = (uint8_t)((gb->dma_index & ~1u) | (requested & 1u));
    }
    return raw_oam_read(gb, address);
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
    if (dma && stationary && gb->model != CUPID_GB_MODEL_CGB)
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
    p->x = p->read = p->phase = p->column = 0;
    /* These initial slots are discarded, but still arbitrate object fetching. */
    p->count = 8;
    memset(p->colors, 0, sizeof p->colors);
    memset(p->palettes, 0, sizeof p->palettes);
    memset(p->priorities, 0, sizeof p->priorities);
    p->discard = gb->io_registers[CUPID_GB_IO_SCX] & 7;
    p->delay = 12;
    p->object_delay = 0;
    p->window_active = p->window_used = false;
    memset(p->sprite_colors, 0, sizeof(p->sprite_colors));
    memset(p->sprite_priorities, 0xff, sizeof(p->sprite_priorities));
    p->active = true;
}

static uint16_t tile_address(CupidGb *gb, bool upper) {
    CupidGbPixels *p = &gb->pixels;
    uint8_t lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    unsigned row = p->window_active ? gb->window_line_counter
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

    const bool color = gb->model == CUPID_GB_MODEL_CGB;
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

static void fetch_background(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    uint8_t lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    switch (p->phase) {
        case 0: {
            unsigned y = p->window_active ? gb->window_line_counter
                                          : (uint8_t)(p->line + gb->io_registers[CUPID_GB_IO_SCY]);
            unsigned column = p->column;
            if (!p->window_active)
                column += gb->io_registers[CUPID_GB_IO_SCX] >> 3;
            unsigned map = lcdc & (p->window_active ? 0x40 : 8) ? 0x1c00 : 0x1800;
            p->address = (uint16_t)(map + (y >> 3) * 32 + (column & 31));
            ++p->phase;
            break;
        }
        case 1:
            p->tile = fetch_vram(gb, p->address);
            p->attributes = gb->cgb_mode ? fetch_vram(gb, p->address + 0x2000u) : 0;
            ++p->phase;
            break;
        case 2:
            p->address = tile_address(gb, false);
            ++p->phase;
            break;
        case 3:
            p->low = fetch_vram(gb, p->address);
            ++p->phase;
            break;
        case 4:
            p->address = tile_address(gb, true);
            ++p->phase;
            break;
        case 5:
            p->high = fetch_vram(gb, p->address);
            ++p->phase;
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
    if (!(gb->io_registers[CUPID_GB_IO_LCDC] & 2) && !gb->cgb_mode)
        return -1;
    unsigned match = p->x + 8;
    if (!p->window_active && p->column == 0) {
        unsigned dropped = 7 - p->delay;
        match = dropped > p->discard ? dropped - p->discard : 0;
    } else if (p->discard)
        match = 8 - p->discard;
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

static void emit_pixel(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    uint8_t lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    uint8_t raw = p->colors[p->read];
    bool background_priority = p->priorities[p->read] != 0;
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
        unsigned number = object ? p->sprite_palettes[0] : p->palettes[p->read];
        gb->frame_buffer_color[index] = palette_color(palette, number * 4 + color);
        gb->frame_buffer[index] = color;
    } else {
        unsigned reg = object ? CUPID_GB_IO_OBP0 + p->sprite_palettes[0] : CUPID_GB_IO_BGP;
        uint8_t shade = (gb->io_registers[reg] >> (color * 2)) & 3;
        gb->frame_buffer[index] = shade;
        if (cupid_cgb_compat_active(gb))
            gb->frame_buffer_color[index] =
                object ? cupid_cgb_compat_obj_color(gb, p->sprite_palettes[0], shade)
                       : cupid_cgb_compat_bg_color(gb, shade);
    }
    if (p->window_active)
        p->window_used = true;
    ++p->x;
    for (unsigned i = 0; i < 7; ++i) {
        p->sprite_colors[i] = p->sprite_colors[i + 1];
        p->sprite_palettes[i] = p->sprite_palettes[i + 1];
        p->sprite_flags[i] = p->sprite_flags[i + 1];
        p->sprite_priorities[i] = p->sprite_priorities[i + 1];
    }
    p->sprite_colors[7] = 0;
    p->sprite_priorities[7] = UINT16_MAX;
}

bool cupid_gb_pixels_tick(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    if (p->x == CUPID_GB_SCREEN_WIDTH)
        return true;
    uint8_t lcdc = gb->io_registers[CUPID_GB_IO_LCDC];
    if (gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE && !p->object_delay && !p->delay &&
        !p->discard && !p->window_active && p->window_armed && (lcdc & 0x20) &&
        (gb->cgb_mode || (lcdc & 1)) && gb->io_registers[CUPID_GB_IO_WX] < 167 &&
        (int)p->x >= (int)gb->io_registers[CUPID_GB_IO_WX] - 7) {
        p->window_active = true;
        p->count = p->read = p->phase = p->column = 0;
        p->discard =
            gb->io_registers[CUPID_GB_IO_WX] < 7 ? 7 - gb->io_registers[CUPID_GB_IO_WX] : 0;
        p->delay = 6;
    }
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
    if (gb->object_fetch_stage == CUPID_GB_OBJECT_IDLE && p->delay <= 7 && p->count) {
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
    if (p->delay > 7) {
        --p->delay;
        return false;
    }
    if (p->count) {
        bool junk = !p->window_active && p->column == 0;
        if (!junk) {
            if (p->discard)
                --p->discard;
            else
                emit_pixel(gb);
        }
        p->read = (p->read + 1) & 7;
        --p->count;
    }
    if (p->delay)
        --p->delay;
    fetch_background(gb);
    /* Transfer ends on the last emitted pixel's dot, after its fetch advance. */
    return p->x == CUPID_GB_SCREEN_WIDTH;
}

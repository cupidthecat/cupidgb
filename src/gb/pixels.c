#include "cupid/gb/ppu.h"

#include "cupid/gbc/cgb.h"

#include <limits.h>
#include <string.h>

static uint16_t palette_color(const uint8_t *palette, unsigned index) {
    return (uint16_t)((palette[index * 2] | (uint16_t)palette[index * 2 + 1] << 8) & 0x7fff);
}

void cupid_gb_pixels_begin_line(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    p->object_count = 0;
    p->fetched_objects = 0;
    p->active = false;
    if (p->line == gb->io_registers[CUPID_GB_IO_WY])
        p->window_armed = true;
}

void cupid_gb_pixels_scan_object(CupidGb *gb, unsigned index) {
    CupidGbPixels *p = &gb->pixels;
    unsigned height = gb->io_registers[CUPID_GB_IO_LCDC] & 4 ? 16 : 8;
    uint8_t y = gb->object_attribute_memory[index * 4];
    if ((unsigned)p->line + 16 < y || (unsigned)p->line + 16 >= (unsigned)y + height ||
        p->object_count == 10)
        return;
    unsigned slot = p->object_count++;
    p->objects[slot] = (uint8_t)index;
    p->object_x[slot] = gb->object_attribute_memory[index * 4 + 1];
    p->object_y[slot] = y;
}

void cupid_gb_pixels_begin_transfer(CupidGb *gb) {
    CupidGbPixels *p = &gb->pixels;
    p->x = p->read = p->count = p->phase = p->column = 0;
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
    if (gb->hdma_running) {
        gb->hdma_vram_fetch = address;
        return 0;
    }
    return gb->video_ram[address];
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
    int chosen = -1;
    for (unsigned i = 0; i < p->object_count; ++i) {
        if ((p->fetched_objects & (1u << i)) || p->object_x[i] >= 168 ||
            p->object_x[i] > (unsigned)p->x + 8)
            continue;
        if (chosen < 0 || p->object_x[i] < p->object_x[chosen])
            chosen = (int)i;
    }
    return chosen;
}

static void begin_object(CupidGb *gb, unsigned slot) {
    CupidGbPixels *p = &gb->pixels;
    unsigned scroll = gb->io_registers[CUPID_GB_IO_SCX];
    unsigned group = ((unsigned)p->object_x[slot] + scroll) >> 3;
    unsigned alignment = p->object_x[slot] ? (p->object_x[slot] + scroll) & 7 : 0;
    unsigned wait = alignment < 5 ? 5 - alignment : 0;
    /* Object fetches sharing a background tile wait for its fetch only once.
     * Every object still consumes its own six-dot tile-data transaction. */
    for (unsigned previous = 0; previous < p->object_count; ++previous) {
        if ((p->fetched_objects & (1u << previous)) &&
            ((p->object_x[previous] + scroll) >> 3) == group) {
            wait = 0;
            break;
        }
    }
    p->fetched_objects |= (uint16_t)(1u << slot);
    p->object_index = (uint8_t)slot;
    unsigned index = p->objects[slot];
    p->object_attributes = gb->object_attribute_memory[index * 4 + 3];
    unsigned height = gb->io_registers[CUPID_GB_IO_LCDC] & 4 ? 16 : 8;
    unsigned row = ((unsigned)p->line + 16 - p->object_y[slot]) & (height - 1);
    if (p->object_attributes & 0x40)
        row ^= height - 1;
    unsigned tile = gb->object_attribute_memory[index * 4 + 2];
    if (height == 16)
        tile &= 0xfe;
    unsigned bank = gb->cgb_mode && (p->object_attributes & 8) ? 0x2000 : 0;
    p->object_address = (uint16_t)(bank + tile * 16 + row * 2);
    p->object_delay = (uint8_t)(6 + wait);
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
    if (!p->delay && !p->discard && !p->window_active && p->window_armed && (lcdc & 0x20) &&
        (gb->cgb_mode || (lcdc & 1)) && gb->io_registers[CUPID_GB_IO_WX] < 167 &&
        (int)p->x >= (int)gb->io_registers[CUPID_GB_IO_WX] - 7) {
        p->window_active = true;
        p->count = p->read = p->phase = p->column = 0;
        p->discard =
            gb->io_registers[CUPID_GB_IO_WX] < 7 ? 7 - gb->io_registers[CUPID_GB_IO_WX] : 0;
        p->delay = 6;
    }
    if (p->object_delay) {
        if (p->object_delay == 4)
            p->object_low = fetch_vram(gb, p->object_address);
        if (p->object_delay == 2)
            p->object_high = fetch_vram(gb, p->object_address + 1u);
        if (--p->object_delay == 0)
            overlay_object(gb);
        return false;
    }
    if (!p->delay && !p->discard) {
        int slot = due_object(gb);
        if (slot >= 0) {
            begin_object(gb, (unsigned)slot);
            --p->object_delay;
            return false;
        }
    }
    fetch_background(gb);
    if (p->delay) {
        --p->delay;
        return false;
    }
    if (!p->count)
        return false;
    if (p->discard)
        --p->discard;
    else
        emit_pixel(gb);
    p->read = (p->read + 1) & 7;
    --p->count;
    return false;
}

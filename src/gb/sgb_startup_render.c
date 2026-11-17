/**
 * @file    sgb_startup_render.c
 * @brief   Pure CupidGB startup artwork for the adapter viewport.
 * @author  cupidthecat
 * @date    2026-11-17
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

#include "cupid/gb/sgb_startup.h"

#include "cupid/gb/gb.h"

/* Original compact lettering and heart, authored as row masks. The picture is
 * derived solely from retained phase; paused redraws never advance the intro. */
static const uint8_t title[7][7] = {
    {14, 17, 16, 16, 16, 17, 14}, /* C */
    {17, 17, 17, 17, 17, 17, 14}, /* U */
    {30, 17, 17, 30, 16, 16, 16}, /* P */
    {31, 4, 4, 4, 4, 4, 31},      /* I */
    {30, 17, 17, 17, 17, 17, 30}, /* D */
    {14, 17, 16, 23, 17, 17, 14}, /* G */
    {30, 17, 17, 30, 17, 17, 30}  /* B */
};

static const uint16_t heart[12] = {0x0630, 0x0ff8, 0x1ffc, 0x1ffc, 0x1ffc, 0x1ffc,
                                   0x0ff8, 0x07f0, 0x03e0, 0x01c0, 0x0080, 0x0000};

static uint32_t color(unsigned red, unsigned green, unsigned blue, unsigned fade) {
    red = red > fade ? red - fade : 0;
    green = green > fade ? green - fade : 0;
    blue = blue > fade ? blue - fade : 0;
    return UINT32_C(0xff000000) | ((red * 255u / 31u) << 16u) | ((green * 255u / 31u) << 8u) |
           (blue * 255u / 31u);
}

static void pixel(uint32_t *pixels, unsigned x, unsigned y, uint32_t value) {
    pixels[(y + CUPID_SGB_VIEWPORT_Y) * CUPID_SGB_SCREEN_WIDTH + x + CUPID_SGB_VIEWPORT_X] = value;
}

void cupid_gb_sgb_startup_render(const CupidGb *gb, uint32_t *pixels, size_t pixel_count) {
    if (!gb || !pixels || pixel_count < CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT ||
        !cupid_gb_sgb_startup_active(gb))
        return;
    for (unsigned y = 0; y < CUPID_GB_SCREEN_HEIGHT; ++y)
        for (unsigned x = 0; x < CUPID_GB_SCREEN_WIDTH; ++x)
            pixel(pixels, x, y, UINT32_C(0xff000000));

    const int phase = gb->sgb_startup.phase;
    if (phase < 0)
        return;
    const unsigned final_fade = phase > 168 ? (unsigned)(phase - 168) : 0u;
    const unsigned lettering_fade = phase < 48   ? 32u
                                    : phase < 80 ? (unsigned)(80 - phase)
                                                 : final_fade;
    const uint32_t ink = color(3, 25, 31, lettering_fade);
    const uint32_t edge = color(10, 10, 10, lettering_fade);
    for (unsigned letter = 0; letter < 7; ++letter) {
        for (unsigned row = 0; row < 7; ++row) {
            for (unsigned column = 0; column < 5; ++column) {
                if (!(title[letter][row] & (1u << (4u - column))))
                    continue;
                const unsigned x = 39u + letter * 12u + column * 2u;
                const unsigned y = 84u + row * 2u;
                pixel(pixels, x + 1u, y + 2u, edge);
                pixel(pixels, x + 2u, y + 2u, edge);
                for (unsigned dy = 0; dy < 2; ++dy)
                    for (unsigned dx = 0; dx < 2; ++dx)
                        pixel(pixels, x + dx, y + dy, ink);
            }
        }
    }
    for (unsigned row = 0; row < 12; ++row) {
        for (unsigned column = 0; column < 13; ++column) {
            if (!(heart[row] & (1u << (12u - column))))
                continue;
            const int age = phase / 2 - (int)column - 6;
            if (age < 0)
                continue;
            const unsigned reveal_fade = age < 2 ? (unsigned)(2 - age) * 8u : 0u;
            const uint32_t fill = color(31, 5, 12, final_fade + reveal_fade);
            for (unsigned dy = 0; dy < 2; ++dy)
                for (unsigned dx = 0; dx < 2; ++dx)
                    pixel(pixels, 67u + column * 2u + dx, 48u + row * 2u + dy, fill);
        }
    }
}

/**
 * @file    color_pipeline.h
 * @brief   Hardware color-correction modes, settings, and transformation interface.
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

#pragma once

#include <QtGui/qrgb.h>
#include <array>

enum class ColorCorrection {
    Disabled = 0,
    Curves = 1,
    Balanced = 2,
    BoostContrast = 3,
    ReduceContrast = 4,
    LowContrast = 5,
    Accurate = 6
};

enum class ColorHardware { Cgb, Sgb };

struct ColorSettings {
    ColorCorrection correction = ColorCorrection::Balanced;
    int temperature = 10;
};

using ColorTable = std::array<QRgb, 32768>;

// Build when display settings change. RGB555 pixels then need one lookup each.
// Native monochrome palettes use their configured RGB values directly.
ColorTable makeColorTable(ColorHardware hardware, ColorSettings settings = {});

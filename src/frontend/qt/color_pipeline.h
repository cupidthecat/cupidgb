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

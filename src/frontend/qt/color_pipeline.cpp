#include "color_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
using Channels = std::array<int, 3>;

// The 32 input levels describe each display's transfer curve.
constexpr std::array<int, 32> cgbLevels = {0,   6,   12,  20,  28,  36,  45,  56,  66,  76,  88,
                                           100, 113, 125, 137, 149, 161, 172, 182, 192, 202, 210,
                                           218, 225, 232, 238, 243, 247, 250, 252, 254, 255};
constexpr std::array<int, 32> sgbLevels = {0,   2,   5,   9,   15,  20,  27,  34,  42,  50,  58,
                                           67,  76,  85,  94,  104, 114, 123, 133, 143, 153, 163,
                                           173, 182, 192, 202, 211, 220, 229, 238, 247, 255};

std::array<std::array<uint8_t, 256>, 3> temperatureChannels(int step) {
    const double warmth = (std::clamp(step, 0, 20) - 10) / 10.0;
    std::array<double, 3> gain{1.0, 1.0, 1.0};
    if (warmth > 0) {
        gain[1] = std::pow(1.0 - warmth, 0.375);
        gain[2] = std::sqrt(std::max(0.0, (0.75 - warmth) / 0.75));
    } else if (warmth < 0) {
        gain[0] += warmth * (0.5 + 0.21875 * warmth);
        gain[1] += warmth * (0.3 + 0.125 * warmth);
    }
    std::array<std::array<uint8_t, 256>, 3> result{};
    for (size_t channel = 0; channel < result.size(); ++channel)
        for (size_t level = 0; level < result[channel].size(); ++level)
            result[channel][level] = uint8_t(std::lround(level * gain[channel]));
    return result;
}

std::array<uint8_t, 1024> mixedGreen(ColorCorrection mode) {
    const double exponent =
        mode == ColorCorrection::Balanced || mode == ColorCorrection::BoostContrast ? 1.6 : 2.2;
    std::array<double, 32> linear{};
    for (size_t i = 0; i < linear.size(); ++i)
        linear[i] = std::pow(cgbLevels[i] / 255.0, exponent);
    std::array<uint8_t, 1024> result{};
    for (size_t green = 0; green < 32; ++green) {
        for (size_t blue = 0; blue < 32; ++blue) {
            const double light = (linear[green] * 3.0 + linear[blue]) / 4.0;
            result[green * 32 + blue] =
                uint8_t(std::lround(255.0 * std::pow(light, 1.0 / exponent)));
        }
    }
    return result;
}

Channels compressContrast(const Channels &color, ColorCorrection mode) {
    const bool reduced = mode == ColorCorrection::ReduceContrast;
    const Channels floor = reduced ? Channels{40, 36, 32} : Channels{45, 41, 38};
    const Channels span = reduced ? Channels{180, 188, 184} : Channels{117, 126, 119};
    Channels result{};
    for (size_t channel = 0; channel < result.size(); ++channel) {
        // Each division truncates separately before the limited display range is applied.
        const int neighbors = color[(channel + 1) % 3] + color[(channel + 2) % 3];
        const int softened = color[channel] * 15 / 16 + neighbors / 32;
        result[channel] = softened * span[channel] / 255 + floor[channel];
    }
    return result;
}

Channels restoreContrast(Channels color, const Channels &original) {
    // Peak and trough normalization are separate integer stages; combining them changes colors.
    const int peak = *std::max_element(color.begin(), color.end());
    const int originalPeak = *std::max_element(original.begin(), original.end());
    if (peak)
        for (int &channel : color)
            channel = channel * originalPeak / peak;
    const int trough = *std::min_element(color.begin(), color.end());
    const int originalTrough = *std::min_element(original.begin(), original.end());
    if (trough < 255)
        for (int &channel : color)
            channel = 255 - (255 - channel) * (255 - originalTrough) / (255 - trough);
    return color;
}
} // namespace

ColorTable makeColorTable(ColorHardware hardware, ColorSettings settings) {
    if (int(settings.correction) < 0 || int(settings.correction) > int(ColorCorrection::Accurate))
        settings.correction = ColorCorrection::Balanced;
    const auto mode = settings.correction;
    std::array<int, 32> levels = hardware == ColorHardware::Sgb ? sgbLevels : cgbLevels;
    if (mode == ColorCorrection::Disabled)
        for (size_t level = 0; level < levels.size(); ++level)
            levels[level] = int((level << 3) | (level >> 2));
    const bool mix = hardware == ColorHardware::Cgb && mode != ColorCorrection::Disabled &&
                     mode != ColorCorrection::Curves;
    std::array<uint8_t, 1024> green{};
    if (mix)
        green = mixedGreen(mode);
    const auto temperature = temperatureChannels(settings.temperature);
    ColorTable result{};
    for (size_t pixel = 0; pixel < result.size(); ++pixel) {
        const size_t r = pixel & 31;
        const size_t g = (pixel >> 5) & 31;
        const size_t b = pixel >> 10;
        const Channels original{levels[r], levels[g], levels[b]};
        Channels corrected = original;
        if (mix) {
            corrected[1] = green[g * 32 + b];
            if (mode == ColorCorrection::BoostContrast)
                corrected = restoreContrast(corrected, original);
            else if (mode == ColorCorrection::ReduceContrast ||
                     mode == ColorCorrection::LowContrast)
                corrected = compressContrast(corrected, mode);
        }
        result[pixel] = qRgb(temperature[0][corrected[0]], temperature[1][corrected[1]],
                             temperature[2][corrected[2]]);
    }
    return result;
}

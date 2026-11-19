/**
 * @file    display_pipeline.cpp
 * @brief   Display viewport scaling, monochrome colors, and frame blending.
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

#include "display_pipeline.h"

#include <QColor>
#include <QColorSpace>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

QImage scaleNearestDisplayImage(const QImage &source, QSize output) {
    if (source.isNull() || output.isEmpty())
        return {};
    if (source.size() == output)
        return source;
    QImage pixels = source;
    if (source.depth() != 32) {
        const auto format = source.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32;
        pixels = source.convertToFormat(format);
    }
    if (pixels.isNull())
        return {};
    QImage result(output, pixels.format());
    if (result.isNull())
        return {};
    result.setColorSpace(source.colorSpace());
    result.setDevicePixelRatio(source.devicePixelRatio());
    std::vector<int> columns(size_t(output.width()));
    for (int x = 0; x < output.width(); ++x) {
        // Integer arithmetic preserves exact ties at arbitrary host dimensions.
        columns[size_t(x)] = int(((uint64_t(x) * 2 + 1) * uint64_t(source.width())) /
                                 (uint64_t(output.width()) * 2));
    }
    int previousRow = -1;
    const size_t rowBytes = size_t(output.width()) * sizeof(QRgb);
    for (int y = 0; y < output.height(); ++y) {
        const int row = int(((uint64_t(y) * 2 + 1) * uint64_t(source.height())) /
                            (uint64_t(output.height()) * 2));
        auto *destination = reinterpret_cast<QRgb *>(result.scanLine(y));
        if (row == previousRow) {
            std::memcpy(destination, result.constScanLine(y - 1), rowBytes);
        } else {
            const auto *input = reinterpret_cast<const QRgb *>(pixels.constScanLine(row));
            for (int x = 0; x < output.width(); ++x)
                destination[x] = input[columns[size_t(x)]];
        }
        previousRow = row;
    }
    return result;
}

QImage filterDisplayImage(const QImage &source, QSize output, ScalingFilter filter) {
    return renderDisplayFrames(source, {}, output, filter, FrameBlending::Disabled, false);
}

QImage renderDisplayFrames(const QImage &source, const QImage &previous, QSize output,
                           ScalingFilter filter, FrameBlending blending, bool oddFrame) {
    if (source.isNull() || output.isEmpty())
        return {};
    if (source.size() == output || filter == ScalingFilter::Nearest) {
        const auto mixed = blendDisplayFrames(source, previous, blending, oddFrame);
        return scaleNearestDisplayImage(mixed, output);
    }
    const bool blend = blending != FrameBlending::Disabled && !previous.isNull() &&
                       previous.size() == source.size() && previous.cacheKey() != source.cacheKey();

    struct Coordinate {
        int first;
        int second;
        double weight;
    };

    const auto coordinates = [filter](int input, int count) {
        std::vector<Coordinate> result;
        result.reserve(size_t(count));
        for (int pixel = 0; pixel < count; ++pixel) {
            // Samples are centered in physical output pixels. Clamp both taps
            // independently so the image edge extends across the outer half texel.
            const double position = (pixel + 0.5) * input / count - 0.5;
            const int first = int(std::floor(position));
            double weight = position - first;
            if (filter == ScalingFilter::SmoothBilinear)
                weight = weight * weight * (3.0 - 2.0 * weight);
            result.push_back(
                {std::clamp(first, 0, input - 1), std::clamp(first + 1, 0, input - 1), weight});
        }
        return result;
    };
    static const auto linear = [] {
        std::array<double, 256> table{};
        for (size_t value = 0; value < table.size(); ++value)
            table[value] = std::pow(double(value) / 255.0, 2.2);
        return table;
    }();
    static const auto encoded = [] {
        std::array<uint8_t, 65536> table{};
        for (size_t value = 0; value < table.size(); ++value)
            table[value] = uint8_t(std::lround(std::pow(double(value) / 65535.0, 1.0 / 2.2) * 255));
        return table;
    }();
    const auto horizontal = coordinates(source.width(), output.width());
    const auto vertical = coordinates(source.height(), output.height());
    const QImage pixels = source.convertToFormat(QImage::Format_RGB32);
    const QImage history = blend ? previous.convertToFormat(QImage::Format_RGB32) : QImage();
    QImage result(output, QImage::Format_RGB32);
    if (result.isNull())
        return {};

    struct LinearPixel {
        std::array<double, 3> channels{};
        QRgb flatColor = 0;
    };

    struct HorizontalRow {
        int sourceRow = -1;
        std::vector<LinearPixel> current;
        std::vector<LinearPixel> previous;
    };

    HorizontalRow top, bottom;
    for (auto *row : {&top, &bottom}) {
        row->current.resize(size_t(output.width()));
        if (blend)
            row->previous.resize(size_t(output.width()));
    }
    const auto horizontalRow = [&](const QImage &image, int sourceRow,
                                   std::vector<LinearPixel> &destination) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(sourceRow));
        for (int x = 0; x < output.width(); ++x) {
            const auto &h = horizontal[size_t(x)];
            const QRgb first = row[h.first], second = row[h.second];
            auto &pixel = destination[size_t(x)];
            pixel.flatColor = first == second ? first : 0;
            for (size_t channel = 0; channel < pixel.channels.size(); ++channel) {
                const unsigned shift = unsigned(16 - channel * 8);
                pixel.channels[channel] = ((1.0 - h.weight) * linear[(first >> shift) & 255] +
                                           h.weight * linear[(second >> shift) & 255]);
            }
        }
    };
    const auto prepareRow = [&](HorizontalRow &row, int sourceRow) {
        horizontalRow(pixels, sourceRow, row.current);
        if (blend)
            horizontalRow(history, sourceRow, row.previous);
        row.sourceRow = sourceRow;
    };
    for (int y = 0; y < output.height(); ++y) {
        const auto &v = vertical[size_t(y)];
        // Each source row is horizontally sampled once while this pair serves
        // several physical output rows. Exchanging the caches retains the overlap.
        if (top.sourceRow != v.first) {
            if (bottom.sourceRow == v.first)
                std::swap(top, bottom);
            else
                prepareRow(top, v.first);
        }
        if (v.second != v.first && bottom.sourceRow != v.second)
            prepareRow(bottom, v.second);
        const auto &lower = v.first == v.second ? top : bottom;
        // LCD polarity follows the nearest source row, independently of the
        // spatial kernel. All four weights apply before the one final encoding.
        const int sourceRow = int((y + 0.5) * source.height() / output.height());
        const double oldWeight = !blend                               ? 0.0
                                 : blending == FrameBlending::Simple  ? 0.5
                                 : ((sourceRow & 1) != int(oddFrame)) ? 2.0 / 3.0
                                                                      : 1.0 / 3.0;
        const double currentTop = ((1 - v.weight) * (1 - oldWeight));
        const double currentBottom = (v.weight * (1 - oldWeight));
        const double previousTop = ((1 - v.weight) * oldWeight);
        const double previousBottom = (v.weight * oldWeight);
        auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < output.width(); ++x) {
            const auto &a = top.current[size_t(x)], &b = lower.current[size_t(x)];
            const auto &c = blend ? top.previous[size_t(x)] : a;
            const auto &d = blend ? lower.previous[size_t(x)] : b;
            if (a.flatColor && a.flatColor == b.flatColor && a.flatColor == c.flatColor &&
                a.flatColor == d.flatColor) {
                // Preserve exact flat fields, including dark encoded shades
                // whose emitted light occupies less than one table step.
                row[x] = a.flatColor;
                continue;
            }
            const auto channel = [&](size_t index) {
                const double light =
                    currentTop * a.channels[index] + currentBottom * b.channels[index] +
                    previousTop * c.channels[index] + previousBottom * d.channels[index];
                return encoded[size_t(light * 65535.0 + 0.5)];
            };
            row[x] = qRgb(channel(0), channel(1), channel(2));
        }
    }
    return result;
}

QRectF displayViewport(QSize source, QSizeF available, DisplayScaling scaling,
                       qreal devicePixelRatio) {
    if (source.isEmpty() || available.isEmpty() || devicePixelRatio <= 0)
        return {};
    if (scaling == DisplayScaling::Fill)
        return {QPointF(), available};
    qreal factor = std::min(available.width() * devicePixelRatio / source.width(),
                            available.height() * devicePixelRatio / source.height());
    if (scaling == DisplayScaling::Integer)
        factor = std::floor(factor);
    const QSizeF pixels(std::floor(source.width() * factor), std::floor(source.height() * factor));
    // Center on whole physical pixels so fractional desktop scaling cannot move
    // an integer-scaled source between the host's pixel columns.
    return {std::floor((available.width() * devicePixelRatio - pixels.width()) / 2) /
                devicePixelRatio,
            std::floor((available.height() * devicePixelRatio - pixels.height()) / 2) /
                devicePixelRatio,
            pixels.width() / devicePixelRatio, pixels.height() / devicePixelRatio};
}

std::array<QRgb, 4> monochromeColors(MonochromePalette palette, const std::array<QRgb, 4> &custom) {
    // Entries follow the framebuffer's lightest-to-darkest shade order.
    switch (palette) {
        case MonochromePalette::Grayscale:
            return {qRgb(255, 255, 255), qRgb(170, 170, 170), qRgb(85, 85, 85), qRgb(0, 0, 0)};
        case MonochromePalette::Olive:
            return {QColor("#c2ce93").rgb(), QColor("#818d66").rgb(), QColor("#3a4c3a").rgb(),
                    QColor("#07100e").rgb()};
        case MonochromePalette::Teal:
            return {QColor("#7fe2c3").rgb(), QColor("#56b495").rgb(), QColor("#357862").rgb(),
                    QColor("#0a1c15").rgb()};
        case MonochromePalette::Custom:
            return custom;
        case MonochromePalette::Lime:
        default:
            return {QColor("#c6de8c").rgb(), QColor("#84a563").rgb(), QColor("#396139").rgb(),
                    QColor("#081810").rgb()};
    }
}

QRgb monochromeOffColor(MonochromePalette palette, QRgb custom) {
    switch (palette) {
        case MonochromePalette::Grayscale:
            return qRgb(255, 255, 255);
        case MonochromePalette::Olive:
            return qRgb(207, 218, 172);
        case MonochromePalette::Teal:
            return qRgb(145, 234, 208);
        case MonochromePalette::Custom:
            return custom;
        case MonochromePalette::Lime:
        default:
            return qRgb(210, 230, 166);
    }
}

namespace {
using BlendTable = std::array<uint8_t, 256 * 256>;

BlendTable makeBlendTable(double previousWeight) {
    BlendTable result{};
    std::array<double, 256> linear{};
    for (size_t value = 0; value < linear.size(); ++value)
        linear[value] = std::pow(double(value) / 255.0, 2.2);
    for (size_t current = 0; current < linear.size(); ++current) {
        for (size_t previous = 0; previous < linear.size(); ++previous) {
            const double light =
                linear[current] * (1.0 - previousWeight) + linear[previous] * previousWeight;
            result[current * 256 + previous] =
                uint8_t(std::lround(255.0 * std::pow(light, 1.0 / 2.2)));
        }
    }
    return result;
}

const BlendTable &blendTable(FrameBlending mode) {
    // Mix emitted light. Reversing the two inputs gives the complementary weight
    // without building a third table or using floating point in the pixel loop.
    if (mode == FrameBlending::Accurate) {
        static const auto weighted = makeBlendTable(1.0 / 3.0);
        return weighted;
    }
    static const auto midpoint = makeBlendTable(0.5);
    return midpoint;
}
} // namespace

QImage blendDisplayFrames(const QImage &current, const QImage &previous, FrameBlending mode,
                          bool oddFrame) {
    if (mode == FrameBlending::Disabled || current.isNull() || previous.isNull() ||
        current.size() != previous.size() || current.cacheKey() == previous.cacheKey())
        return current;
    const auto &mixed = blendTable(mode);
    const auto first = current.convertToFormat(QImage::Format_RGB32);
    const auto second = previous.convertToFormat(QImage::Format_RGB32);
    QImage output(first.size(), QImage::Format_RGB32);
    const int width = first.width();
    const int height = first.height();
    const size_t rowBytes = size_t(width) * sizeof(QRgb);
    for (int y = 0; y < height; ++y) {
        const auto *a = reinterpret_cast<const QRgb *>(first.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(second.constScanLine(y));
        auto *destination = reinterpret_cast<QRgb *>(output.scanLine(y));
        // Equal RGB rows produce the same bytes at either weight.
        if (std::memcmp(a, b, rowBytes) == 0) {
            std::memcpy(destination, a, rowBytes);
            continue;
        }
        // Alternate the favored frame at each LCD row and invert it next frame.
        if (mode == FrameBlending::Accurate && ((y & 1) != int(oddFrame)))
            std::swap(a, b);
        for (int x = 0; x < width; ++x)
            destination[x] = qRgb(mixed[size_t(qRed(a[x])) * 256 + qRed(b[x])],
                                  mixed[size_t(qGreen(a[x])) * 256 + qGreen(b[x])],
                                  mixed[size_t(qBlue(a[x])) * 256 + qBlue(b[x])]);
    }
    return output;
}

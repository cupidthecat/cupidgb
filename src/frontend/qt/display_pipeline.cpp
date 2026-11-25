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
#include <QFutureSynchronizer>
#include <QPromise>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {
using BlendTable = std::array<uint8_t, 256 * 256>;

const BlendTable &blendTable(FrameBlending mode);

const auto &linearLight() {
    static const auto values = [] {
        std::array<double, 256> table{};
        for (size_t value = 0; value < table.size(); ++value)
            table[value] = std::pow(double(value) / 255.0, 2.2);
        return table;
    }();
    return values;
}

const auto &encodedLight() {
    static const auto values = [] {
        std::array<uint8_t, 65536> table{};
        for (size_t value = 0; value < table.size(); ++value)
            table[value] = uint8_t(std::lround(std::pow(double(value) / 65535.0, 1.0 / 2.2) * 255));
        return table;
    }();
    return values;
}

uint8_t encodeLinear(double light) {
    static const auto thresholds = [] {
        std::array<double, 255> table{};
        for (size_t level = 0; level < table.size(); ++level)
            table[level] = std::pow((double(level) + 0.5) / 255.0, 2.2);
        return table;
    }();
    // The 16-bit estimate is at most one output code from the exact result.
    const auto estimate = encodedLight()[size_t(light * 65535.0 + 0.5)];
    if (estimate < 255 && light >= thresholds[estimate])
        return estimate + 1;
    if (estimate > 0 && light < thresholds[estimate - 1])
        return estimate - 1;
    return estimate;
}

QRgb mixPixel(QRgb a, QRgb b, const BlendTable &table) {
    return a == b ? a
                  : qRgb(table[size_t(qRed(a)) * 256 + qRed(b)],
                         table[size_t(qGreen(a)) * 256 + qGreen(b)],
                         table[size_t(qBlue(a)) * 256 + qBlue(b)]);
}

struct EdgeCells {
    QRgb center;
    QRgb north;
    QRgb south;
    QRgb west;
    QRgb east;

    QRgb corner(bool right, bool lower) const {
        const QRgb horizontal = right ? east : west;
        const QRgb vertical = lower ? south : north;
        return west != east && north != south && horizontal == vertical ? horizontal : center;
    }
};

struct EdgeRows {
    const QRgb *above;
    const QRgb *row;
    const QRgb *below;
    int width;

    EdgeCells at(int x) const {
        return {row[x], above[x], below[x], row[std::max(x - 1, 0)],
                row[std::min(x + 1, width - 1)]};
    }
};

EdgeRows edgeRows(const QImage &source, int y) {
    return {
        reinterpret_cast<const QRgb *>(source.constScanLine(std::max(y - 1, 0))),
        reinterpret_cast<const QRgb *>(source.constScanLine(y)),
        reinterpret_cast<const QRgb *>(source.constScanLine(std::min(y + 1, source.height() - 1))),
        source.width()};
}

QImage expandEdges(const QImage &source) {
    if (source.width() > std::numeric_limits<int>::max() / 2 ||
        source.height() > std::numeric_limits<int>::max() / 2)
        return {};
    QImage expanded(source.size() * 2, QImage::Format_RGB32);
    if (expanded.isNull())
        return {};
    for (int y = 0; y < source.height(); ++y) {
        const auto lines = edgeRows(source, y);
        auto *top = reinterpret_cast<QRgb *>(expanded.scanLine(2 * y));
        auto *bottom = reinterpret_cast<QRgb *>(expanded.scanLine(2 * y + 1));
        for (int x = 0; x < source.width(); ++x) {
            const auto cells = lines.at(x);
            top[2 * x] = cells.corner(false, false);
            top[2 * x + 1] = cells.corner(true, false);
            bottom[2 * x] = cells.corner(false, true);
            bottom[2 * x + 1] = cells.corner(true, true);
        }
    }
    return expanded;
}

struct EdgePosition {
    int cell;
    bool secondHalf;
};

EdgePosition edgePosition(float position, int count) {
    // Keep the half-cell decision in single precision. The vertical sample
    // starts at the bottom of the output and is then turned right side up.
    const float location = position * float(count);
    const int cell = int(location);
    return {std::min(cell, count - 1), location - float(cell) > 0.5f};
}

QRgb mixAntialiased(QRgb current, QRgb currentCenter, QRgb previous, QRgb previousCenter,
                    const BlendTable &temporal, double oldHalfWeight) {
    if (current == currentCenter && previous == previousCenter)
        return mixPixel(current, previous, temporal);
    const auto &linear = linearLight();
    const double newHalfWeight = 0.5 - oldHalfWeight;
    const auto channel = [&](unsigned shift) {
        return encodeLinear(
            (linear[(current >> shift) & 255u] + linear[(currentCenter >> shift) & 255u]) *
                newHalfWeight +
            (linear[(previous >> shift) & 255u] + linear[(previousCenter >> shift) & 255u]) *
                oldHalfWeight);
    };
    return qRgb(channel(16), channel(8), channel(0));
}

QImage renderEdgeFrames(const QImage &source, const QImage &previous, QSize output,
                        ScalingFilter filter, FrameBlending blending, bool oddFrame) {
    const bool expand =
        filter == ScalingFilter::Scale4x || filter == ScalingFilter::AntialiasedScale4x;
    const bool antialias =
        filter == ScalingFilter::AntialiasedScale2x || filter == ScalingFilter::AntialiasedScale4x;
    const bool blend = !previous.isNull();
    auto frame = source.convertToFormat(QImage::Format_RGB32);
    auto history = blend ? previous.convertToFormat(QImage::Format_RGB32) : QImage();
    if (expand) {
        frame = expandEdges(frame);
        if (blend)
            history = expandEdges(history);
    }
    QImage result(output, QImage::Format_RGB32);
    if (frame.isNull() || result.isNull() || (blend && history.isNull()))
        return {};

    struct ColumnSpan {
        EdgePosition position;
        int end;
    };

    std::vector<ColumnSpan> spans;
    spans.reserve(std::min(size_t(output.width()), size_t(frame.width()) * 2));
    for (int x = 0; x < output.width(); ++x) {
        const auto position =
            edgePosition((float(x) + 0.5f) / float(output.width()), frame.width());
        if (!spans.empty() && spans.back().position.cell == position.cell &&
            spans.back().position.secondHalf == position.secondHalf)
            spans.back().end = x + 1;
        else
            spans.push_back({position, x + 1});
    }
    const auto &temporal = blendTable(blending);
    const auto &midpoint = blendTable(FrameBlending::Simple);
    const double oldHalfWeight = blending == FrameBlending::Accurate ? 1.0 / 6.0 : 0.25;
    auto *pixels = result.bits();
    const qsizetype stride = result.bytesPerLine();
    const auto renderRows = [&](int first, int end) {
        EdgePosition last{-1, false};
        for (int y = first; y < end; ++y) {
            const float position =
                1.0f - (float(output.height() - y) - 0.5f) / float(output.height());
            const auto vertical = edgePosition(position, frame.height());
            auto *outputRow = reinterpret_cast<QRgb *>(pixels + y * stride);
            if (vertical.cell == last.cell && vertical.secondHalf == last.secondHalf) {
                std::memcpy(outputRow, pixels + (y - 1) * stride,
                            size_t(output.width()) * sizeof(QRgb));
                continue;
            }
            last = vertical;
            const auto currentLines = edgeRows(frame, vertical.cell);
            const auto previousLines = blend ? edgeRows(history, vertical.cell) : currentLines;
            const int lcdRow = vertical.cell / (expand ? 2 : 1);
            const bool reverse =
                blending == FrameBlending::Accurate && (lcdRow & 1) != int(oddFrame);
            auto *out = outputRow;
            for (const auto &span : spans) {
                const auto current = currentLines.at(span.position.cell);
                auto color = current.corner(span.position.secondHalf, vertical.secondHalf);
                if (blend) {
                    const auto previousCells = previousLines.at(span.position.cell);
                    auto old = previousCells.corner(span.position.secondHalf, vertical.secondHalf);
                    auto center = current.center;
                    auto oldCenter = previousCells.center;
                    if (reverse) {
                        std::swap(color, old);
                        std::swap(center, oldCenter);
                    }
                    color = antialias ? mixAntialiased(color, center, old, oldCenter, temporal,
                                                       oldHalfWeight)
                                      : mixPixel(color, old, temporal);
                } else if (antialias) {
                    color = mixPixel(color, current.center, midpoint);
                }
                auto *end = outputRow + span.end;
                std::fill(out, end, color);
                out = end;
            }
        }
    };

    // The caller renders one band and joins the others before exposing the image.
    // Each band reads immutable frames and writes its own rows, including row copies.
    constexpr int64_t minimumParallelPixels = 160000;
    const int height = output.height();
    const int workers =
        int64_t(output.width()) * height < minimumParallelPixels
            ? 0
            : std::clamp(QThread::idealThreadCount() - 1, 0, std::min(2, height - 1));
    if (workers == 0) {
        renderRows(0, height);
        return result;
    }

    static const auto pool = [] {
        auto threads = std::make_unique<QThreadPool>();
        threads->setMaxThreadCount(2);
        return threads;
    }();
    QFutureSynchronizer<void> jobs;
    const auto boundary = [&](int band) { return int(int64_t(height) * band / (workers + 1)); };
    for (int band = 0; band < workers; ++band) {
        const int first = boundary(band), end = boundary(band + 1);
        auto promise = std::make_shared<QPromise<void>>();
        promise->start();
        jobs.addFuture(promise->future());
        // Concurrent views share the worker limit. A saturated pool uses the calling thread.
        if (!pool->tryStart([promise, &renderRows, first, end] {
                renderRows(first, end);
                promise->finish();
            })) {
            renderRows(first, end);
            promise->finish();
        }
    }
    renderRows(boundary(workers), height);
    jobs.waitForFinished();
    return result;
}
} // namespace

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
    const bool blend = blending != FrameBlending::Disabled && !previous.isNull() &&
                       previous.size() == source.size() && previous.cacheKey() != source.cacheKey();
    switch (filter) {
        case ScalingFilter::Scale2x:
        case ScalingFilter::Scale4x:
        case ScalingFilter::AntialiasedScale2x:
        case ScalingFilter::AntialiasedScale4x:
            return renderEdgeFrames(source, blend ? previous : QImage(), output, filter, blending,
                                    oddFrame);
        default:
            break;
    }
    const bool pixelated = filter == ScalingFilter::Pixelated;
    const bool aligned =
        output.width() % source.width() == 0 && output.height() % source.height() == 0;
    if (source.size() == output || filter == ScalingFilter::Nearest || (pixelated && aligned)) {
        const auto mixed = blendDisplayFrames(source, previous, blending, oddFrame);
        return scaleNearestDisplayImage(mixed, output);
    }

    struct Coordinate {
        int first;
        int second;
        double weight;
    };

    const auto coordinates = [filter, pixelated](int input, int count) {
        std::vector<Coordinate> result;
        result.reserve(size_t(count));
        for (int pixel = 0; pixel < count; ++pixel) {
            // Samples are centered in physical output pixels. Clamp both taps
            // independently so the image edge extends across the outer half texel.
            const double position = (pixel + 0.5) * input / count - 0.5;
            int first = int(std::floor(position));
            int second = first + 1;
            double weight = position - first;
            if (pixelated) {
                // Measure how much of the output pixel extends past the boundary.
                // At a source center both taps select the same texel, even when reducing.
                const int64_t center = (2 * int64_t(pixel) + 1) * input - count;
                const double boundary = double(first + 1) * count / input;
                weight = center % (2 * int64_t(count)) == 0
                             ? 0.0
                             : std::clamp(pixel + 1.0 - boundary, 0.0, 1.0);
                if (weight == 0.0)
                    second = first;
                else if (weight == 1.0)
                    first = second;
            } else if (filter == ScalingFilter::SmoothBilinear) {
                weight = weight * weight * (3.0 - 2.0 * weight);
            }
            result.push_back(
                {std::clamp(first, 0, input - 1), std::clamp(second, 0, input - 1), weight});
        }
        return result;
    };
    const auto &linear = linearLight();
    const auto &encoded = encodedLight();
    const auto *pixelatedBlend = pixelated && blend ? &blendTable(blending) : nullptr;
    const auto horizontal = coordinates(source.width(), output.width());
    const auto vertical = coordinates(source.height(), output.height());
    const QImage pixels = source.convertToFormat(QImage::Format_RGB32);
    const QImage history = blend ? previous.convertToFormat(QImage::Format_RGB32) : QImage();
    QImage result(output, QImage::Format_RGB32);
    if (result.isNull() || pixels.isNull() || (blend && history.isNull()))
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
                const double a = linear[(first >> shift) & 255];
                pixel.channels[channel] =
                    pixelated && pixel.flatColor
                        ? a
                        : (1.0 - h.weight) * a + h.weight * linear[(second >> shift) & 255];
            }
        }
    };
    const auto prepareRow = [&](HorizontalRow &row, int sourceRow) {
        horizontalRow(pixels, sourceRow, row.current);
        if (blend)
            horizontalRow(history, sourceRow, row.previous);
        row.sourceRow = sourceRow;
    };
    int previousFlatRow = -1;
    bool previousReversed = false;
    const size_t rowBytes = size_t(output.width()) * sizeof(QRgb);
    for (int y = 0; y < output.height(); ++y) {
        const auto &v = vertical[size_t(y)];
        auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        // LCD polarity follows the nearest source row, independently of the kernel.
        const int sourceRow = int((y + 0.5) * source.height() / output.height());
        const bool reverse =
            blend && blending != FrameBlending::Simple && (sourceRow & 1) != int(oddFrame);
        if (pixelated && v.first == v.second) {
            if (previousFlatRow == v.first && previousReversed == reverse) {
                std::memcpy(row, result.constScanLine(y - 1), rowBytes);
                continue;
            }
            previousFlatRow = v.first;
            previousReversed = reverse;
        } else {
            previousFlatRow = -1;
        }
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
        // All spatial and temporal weights apply before the final encoding.
        const double oldWeight = !blend                              ? 0.0
                                 : blending == FrameBlending::Simple ? 0.5
                                 : reverse                           ? 2.0 / 3.0
                                                                     : 1.0 / 3.0;
        const double currentTop = ((1 - v.weight) * (1 - oldWeight));
        const double currentBottom = (v.weight * (1 - oldWeight));
        const double previousTop = ((1 - v.weight) * oldWeight);
        const double previousBottom = (v.weight * oldWeight);
        for (int x = 0; x < output.width(); ++x) {
            const auto &a = top.current[size_t(x)], &b = lower.current[size_t(x)];
            const auto &c = blend ? top.previous[size_t(x)] : a;
            const auto &d = blend ? lower.previous[size_t(x)] : b;
            if (a.flatColor && a.flatColor == b.flatColor && c.flatColor &&
                c.flatColor == d.flatColor) {
                if (a.flatColor == c.flatColor) {
                    // Preserve exact flat fields, including very dark shades.
                    row[x] = a.flatColor;
                    continue;
                }
                if (pixelatedBlend) {
                    row[x] = reverse ? mixPixel(c.flatColor, a.flatColor, *pixelatedBlend)
                                     : mixPixel(a.flatColor, c.flatColor, *pixelatedBlend);
                    continue;
                }
            }
            const auto channel = [&](size_t index) {
                const double light =
                    currentTop * a.channels[index] + currentBottom * b.channels[index] +
                    previousTop * c.channels[index] + previousBottom * d.channels[index];
                return pixelated ? encodeLinear(light) : encoded[size_t(light * 65535.0 + 0.5)];
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
BlendTable makeBlendTable(double previousWeight) {
    BlendTable result{};
    const auto &linear = linearLight();
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
            destination[x] = mixPixel(a[x], b[x], mixed);
    }
    return output;
}

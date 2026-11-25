/**
 * @file    display_crt_geometry.h
 * @brief   Curved display projection and physical-pixel edge coverage.
 * @author  cupidthecat
 * @date    2026-11-24
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

#include <QPoint>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace display_crt {

struct Projection {
    double u = 0;
    double v = 0;
    double mask = 0;
};

namespace detail {

struct Column {
    double offset;
    double verticalScale;
};

struct Position {
    double u;
    double v;
};

inline Column column(int x, int width) {
    const float normalized = (float(x) + 0.5f) / float(width);
    const double offset = double(normalized) - 0.5;
    return {offset, 1.0 / (0.7 + 0.3 * std::cos(offset))};
}

inline double row(int y, int height) {
    // Keep the physical bottom-origin normalization before flipping to image rows.
    const float bottom = (float(int64_t(height) - y) - 0.5f) / float(height);
    return double(1.0f - bottom) - 0.5;
}

inline Position warp(const Column &column, double row) {
    const double v = 0.5 + row * column.verticalScale;
    return {0.5 + column.offset / (0.7 + 0.3 * std::cos(v - 0.5)), v};
}

inline double sixteenthPower(double value) {
    value *= value;
    value *= value;
    value *= value;
    return value * value;
}

inline double corner(const Position &position) {
    return sixteenthPower(2.0 * position.u - 1.0) + sixteenthPower(2.0 * position.v - 1.0);
}

inline double interiorReach(QSize extent) {
    if (extent.width() < 4 || extent.height() < 4)
        return 0;
    // On the normalized domain including one neighboring pixel, each projection
    // partial derivative has magnitude below two. Include float coordinate error.
    return 4.0 * (1.0 / extent.width() + 1.0 / extent.height() +
                  4.0 * std::numeric_limits<float>::epsilon());
}

inline bool interior(const Position &position, double reach) {
    if (reach <= 0 || position.u < 0 || position.u > 1 || position.v < 0 || position.v > 1)
        return false;
    const double maximum = sixteenthPower(std::abs(2.0 * position.u - 1.0) + reach) +
                           sixteenthPower(std::abs(2.0 * position.v - 1.0) + reach);
    // Center and both derivative siblings have corner values in [0, maximum].
    // Their radius is at most max(2*maximum, 1e-5), so coverage is exactly one.
    return 3.0 * maximum < 1.0;
}

inline int sibling(int coordinate, int origin) {
    // Quad alignment follows the top-left framebuffer raster in either axis.
    return coordinate + (((uint32_t(coordinate) ^ uint32_t(origin)) & 1u) ? -1 : 1);
}

inline Projection project(const Column &column, double row, const Column &beside, double above,
                          double reach) {
    const Position center = warp(column, row);
    Projection result{std::clamp(center.u, 0.0, 1.0), std::clamp(center.v, 0.0, 1.0), 1.0};
    if (interior(center, reach))
        return result;

    const Position horizontal = warp(beside, row);
    const Position vertical = warp(column, above);
    const auto width = [](double center, double horizontal, double vertical) {
        return std::max(std::abs(horizontal - center) + std::abs(vertical - center), 1e-5);
    };
    const double horizontalWidth = width(center.u, horizontal.u, vertical.u);
    const double verticalWidth = width(center.v, horizontal.v, vertical.v);
    if (std::min({horizontalWidth + center.u, horizontalWidth + 1.0 - center.u,
                  verticalWidth + center.v, verticalWidth + 1.0 - center.v}) <= 0) {
        result.mask = 0;
        return result;
    }

    const double value = corner(center);
    const double radius = width(value, corner(horizontal), corner(vertical));
    const double fraction = std::clamp((1.0 + radius - value) / (2.0 * radius), 0.0, 1.0);
    result.mask = fraction * fraction * (3.0 - 2.0 * fraction);
    return result;
}

inline bool contains(QSize extent, int x, int y) {
    return x >= 0 && y >= 0 && x < extent.width() && y < extent.height();
}

} // namespace detail

// Immutable shape data can be shared by row workers. Only one dimension's
// coordinates and column curvature are stored, including derivative edge siblings.
class CurvedGeometry {
  public:
    CurvedGeometry(QSize extent, QPoint origin)
        : extent_(extent), origin_(origin), reach_(detail::interiorReach(extent)) {
        if (extent.isEmpty())
            return;
        columns_.reserve(size_t(extent.width()) + 2);
        rows_.reserve(size_t(extent.height()) + 2);
        for (int64_t x = -1; x <= extent.width(); ++x)
            columns_.push_back(detail::column(int(x), extent.width()));
        for (int64_t y = -1; y <= extent.height(); ++y)
            rows_.push_back(detail::row(int(y), extent.height()));
    }

    Projection at(int x, int y) const {
        if (!detail::contains(extent_, x, y))
            return {};
        const size_t horizontal = size_t(detail::sibling(x, origin_.x()) + 1);
        const size_t vertical = size_t(detail::sibling(y, origin_.y()) + 1);
        return detail::project(columns_[size_t(x) + 1], rows_[size_t(y) + 1], columns_[horizontal],
                               rows_[vertical], reach_);
    }

  private:
    QSize extent_;
    QPoint origin_;
    double reach_;
    std::vector<detail::Column> columns_;
    std::vector<double> rows_;
};

} // namespace display_crt

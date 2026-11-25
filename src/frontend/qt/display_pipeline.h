/**
 * @file    display_pipeline.h
 * @brief   Display scaling, monochrome palette, and frame-blending interfaces.
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

#include <QImage>
#include <QRectF>
#include <array>

enum class DisplayScaling { Integer = 0, Aspect = 1, Fill = 2 };
enum class ScalingFilter {
    Nearest = 0,
    Bilinear = 1,
    SmoothBilinear = 2,
    Pixelated = 3,
    Scale2x = 4,
    Scale4x = 5,
    AntialiasedScale2x = 6,
    AntialiasedScale4x = 7,
    Lcd = 8,
    MonochromeLcd = 9,
    Count,
};
enum class MonochromePalette { Lime = 0, Grayscale = 1, Olive = 2, Teal = 3, Custom = 4 };
enum class FrameBlending { Disabled = 0, Simple = 1, Accurate = 2 };
enum class SgbBorderMode { SgbOnly = 0, Never = 1, Always = 2 };

QRectF displayViewport(QSize source, QSizeF available, DisplayScaling scaling,
                       qreal devicePixelRatio = 1.0);
QImage scaleNearestDisplayImage(const QImage &source, QSize output);
QImage filterDisplayImage(const QImage &source, QSize output, ScalingFilter filter);
QImage renderDisplayFrames(const QImage &current, const QImage &previous, QSize output,
                           ScalingFilter filter, FrameBlending blending, bool oddFrame);
std::array<QRgb, 4> monochromeColors(MonochromePalette palette, const std::array<QRgb, 4> &custom);
QRgb monochromeOffColor(MonochromePalette palette, QRgb custom);
QImage blendDisplayFrames(const QImage &current, const QImage &previous, FrameBlending mode,
                          bool oddFrame);

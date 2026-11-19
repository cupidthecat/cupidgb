/**
 * @file    test_display_pipeline.cpp
 * @brief   Regression tests for viewport geometry, palettes, and frame blending.
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
#include <cassert>
#include <cmath>

namespace {
void testPreciseNearestSampling() {
    QImage source(160, 144, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixel(x, y, qRgb(x, y, 31));
    for (auto size :
         {QSize(320, 288), QSize(400, 360), QSize(401, 359), QSize(200, 180), QSize(79, 71)}) {
        const auto output = filterDisplayImage(source, size, ScalingFilter::Nearest);
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x) {
                const int sx = int(std::floor((static_cast<long double>(x) + 0.5L) *
                                              source.width() / size.width()));
                const int sy = int(std::floor((static_cast<long double>(y) + 0.5L) *
                                              source.height() / size.height()));
                assert(output.pixel(x, y) == source.pixel(sx, sy));
            }
    }
    assert(scaleNearestDisplayImage({}, {4, 4}).isNull());
    assert(scaleNearestDisplayImage(source, {}).isNull());
    assert(scaleNearestDisplayImage(source, source.size()) == source);

    QImage translucent(2, 2, QImage::Format_ARGB32_Premultiplied);
    translucent.fill(QColor(170, 84, 33, 129));
    translucent.setColorSpace(QColorSpace::DisplayP3);
    translucent.setDevicePixelRatio(1.25);
    const auto painted = filterDisplayImage(translucent, {5, 3}, ScalingFilter::Nearest);
    assert(painted.format() == translucent.format());
    assert(painted.pixel(2, 1) == translucent.pixel(1, 1));
    assert(painted.colorSpace() == translucent.colorSpace());
    assert(painted.devicePixelRatio() == translucent.devicePixelRatio());
}

void testFilteredFrameBlending() {
    QImage white(160, 144, QImage::Format_RGB32);
    QImage black(160, 144, QImage::Format_RGB32);
    white.fill(Qt::white);
    black.fill(Qt::black);
    for (auto filter : {ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
        for (auto size : {QSize(320, 288), QSize(401, 359), QSize(79, 71)}) {
            for (bool odd : {false, true}) {
                const auto output =
                    renderDisplayFrames(white, black, size, filter, FrameBlending::Accurate, odd);
                for (int y = 0; y < size.height(); ++y) {
                    const int sourceRow = int((y + 0.5) * white.height() / size.height());
                    const double oldWeight = ((sourceRow & 1) != int(odd)) ? 2.0 / 3.0 : 1.0 / 3.0;
                    const int expected =
                        int(std::lround(255 * std::pow(1.0 - oldWeight, 1.0 / 2.2)));
                    assert(std::abs(qRed(output.pixel(size.width() / 2, y)) - expected) <= 1);
                }
            }
            const auto simple =
                renderDisplayFrames(white, black, size, filter, FrameBlending::Simple, false);
            assert(simple.pixel(size.width() / 2, size.height() / 2) == qRgb(186, 186, 186));
            assert(renderDisplayFrames(white, black, size, filter, FrameBlending::Disabled,
                                       false) == filterDisplayImage(white, size, filter));
        }
    }
    QImage current(3, 4, QImage::Format_RGB32);
    QImage previous(3, 4, QImage::Format_RGB32);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 3; ++x) {
            current.setPixel(x, y, qRgb(12 + x * 70, 30 + y * 50, 230 - x * 60 - y * 10));
            previous.setPixel(x, y, qRgb(220 - y * 40, 20 + x * 80, 20 + y * 45));
        }
    for (auto size : {QSize(6, 8), QSize(7, 11), QSize(2, 2), current.size()}) {
        for (auto mode :
             {FrameBlending::Disabled, FrameBlending::Simple, FrameBlending::Accurate}) {
            for (bool odd : {false, true}) {
                assert(renderDisplayFrames(current, previous, size, ScalingFilter::Nearest, mode,
                                           odd) ==
                       scaleNearestDisplayImage(blendDisplayFrames(current, previous, mode, odd),
                                                size));
                for (auto filter : {ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
                    const auto output =
                        renderDisplayFrames(current, previous, size, filter, mode, odd);
                    for (int y = 0; y < size.height(); ++y)
                        for (int x = 0; x < size.width(); ++x) {
                            const double px = (x + 0.5) * current.width() / size.width() - 0.5;
                            const double py = (y + 0.5) * current.height() / size.height() - 0.5;
                            const int left = int(std::floor(px)), top = int(std::floor(py));
                            double wx = px - left, wy = py - top;
                            if (filter == ScalingFilter::SmoothBilinear) {
                                wx = wx * wx * (3 - 2 * wx);
                                wy = wy * wy * (3 - 2 * wy);
                            }
                            const int sourceRow = int((y + 0.5) * current.height() / size.height());
                            const double weight = mode == FrameBlending::Disabled ? 0
                                                  : mode == FrameBlending::Simple ? 0.5
                                                  : ((sourceRow & 1) != int(odd)) ? 2.0 / 3.0
                                                                                  : 1.0 / 3.0;
                            const QRgb observed = output.pixel(x, y);
                            for (int shift : {0, 8, 16}) {
                                double light = 0;
                                for (int dy = 0; dy < 2; ++dy)
                                    for (int dx = 0; dx < 2; ++dx) {
                                        const int sx =
                                            std::clamp(left + dx, 0, current.width() - 1);
                                        const int sy =
                                            std::clamp(top + dy, 0, current.height() - 1);
                                        const double a = std::pow(
                                            ((current.pixel(sx, sy) >> shift) & 255) / 255.0, 2.2);
                                        const double b = std::pow(
                                            ((previous.pixel(sx, sy) >> shift) & 255) / 255.0, 2.2);
                                        light += (dx ? wx : 1 - wx) * (dy ? wy : 1 - wy) *
                                                 ((1 - weight) * a + weight * b);
                                    }
                                const int expected =
                                    int(std::lround(255 * std::pow(light, 1.0 / 2.2)));
                                assert(std::abs(int((observed >> shift) & 255) - expected) <= 1);
                            }
                        }
                }
            }
        }
    }
    assert(renderDisplayFrames(current, {}, {}, ScalingFilter::Bilinear, FrameBlending::Accurate,
                               false)
               .isNull());
    assert(renderDisplayFrames(current, QImage(1, 1, QImage::Format_RGB32), {7, 11},
                               ScalingFilter::Bilinear, FrameBlending::Accurate, false) ==
           filterDisplayImage(current, {7, 11}, ScalingFilter::Bilinear));
}
} // namespace

int main() {
    testPreciseNearestSampling();
    testFilteredFrameBlending();
    QImage corners(2, 2, QImage::Format_RGB32);
    corners.setPixel(0, 0, qRgb(0, 0, 0));
    corners.setPixel(1, 0, qRgb(255, 0, 0));
    corners.setPixel(0, 1, qRgb(0, 255, 0));
    corners.setPixel(1, 1, qRgb(0, 0, 255));
    assert(filterDisplayImage({}, {4, 4}, ScalingFilter::Bilinear).isNull());
    assert(filterDisplayImage(corners, {}, ScalingFilter::SmoothBilinear).isNull());
    for (auto filter :
         {ScalingFilter::Nearest, ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
        assert(filterDisplayImage(corners, corners.size(), filter) == corners);
        QImage solid(1, 1, QImage::Format_RGB32);
        solid.fill(qRgb(37, 154, 226));
        const auto scaledSolid = filterDisplayImage(solid, {7, 9}, filter);
        assert(scaledSolid.size() == QSize(7, 9));
        assert(scaledSolid.pixel(0, 0) == solid.pixel(0, 0));
        assert(scaledSolid.pixel(6, 8) == solid.pixel(0, 0));
        const auto scaled = filterDisplayImage(corners, {6, 6}, filter);
        assert(scaled.pixel(0, 0) == corners.pixel(0, 0));
        assert(scaled.pixel(5, 5) == corners.pixel(1, 1));
    }
    for (int value = 0; value < 256; ++value) {
        QImage solid(2, 2, QImage::Format_RGB32);
        solid.fill(qRgb(value, 255 - value, value));
        for (auto filter : {ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
            const auto scaled = filterDisplayImage(solid, {5, 7}, filter);
            assert(scaled.pixel(2, 3) == solid.pixel(0, 0));
            assert(scaled.pixel(0, 0) == solid.pixel(0, 0));
        }
    }
    const auto nearest = filterDisplayImage(corners, {6, 6}, ScalingFilter::Nearest);
    assert(nearest.pixel(2, 2) == corners.pixel(0, 0));
    assert(nearest.pixel(3, 3) == corners.pixel(1, 1));
    for (auto filter : {ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
        const auto scaled = filterDisplayImage(corners, {7, 5}, filter);
        for (int y = 0; y < scaled.height(); ++y) {
            for (int x = 0; x < scaled.width(); ++x) {
                double wx = std::clamp((x + 0.5) * 2.0 / scaled.width() - 0.5, 0.0, 1.0);
                double wy = std::clamp((y + 0.5) * 2.0 / scaled.height() - 0.5, 0.0, 1.0);
                if (filter == ScalingFilter::SmoothBilinear) {
                    wx = wx * wx * (3 - 2 * wx);
                    wy = wy * wy * (3 - 2 * wy);
                }
                const std::array<double, 3> light = {wx * (1 - wy), (1 - wx) * wy, wx * wy};
                const QRgb color = scaled.pixel(x, y);
                const std::array<int, 3> observed = {qRed(color), qGreen(color), qBlue(color)};
                for (size_t channel = 0; channel < light.size(); ++channel) {
                    const int expected =
                        int(std::lround(std::pow(light[channel], 1.0 / 2.2) * 255));
                    assert(std::abs(observed[channel] - expected) <= 1);
                }
            }
        }
        assert(scaled.pixel(3, 2) == qRgb(136, 136, 136));
    }

    const QSize native(160, 144);
    assert(displayViewport(native, {500, 400}, DisplayScaling::Integer) ==
           QRectF(90, 56, 320, 288));
    const auto aspect = displayViewport(native, {500, 400}, DisplayScaling::Aspect);
    assert(aspect == QRectF(28, 0, 444, 400));
    assert(displayViewport(native, {500, 400}, DisplayScaling::Fill) == QRectF(0, 0, 500, 400));
    // Integer factors refer to physical display pixels, including fractional scaling.
    const auto fractional = displayViewport(native, {500, 400}, DisplayScaling::Integer, 1.25);
    assert(std::abs(fractional.x() - 57.6) < 1e-10 && fractional.width() == 384);
    assert(std::abs(fractional.y() - 27.2) < 1e-10);
    assert(std::abs(fractional.height() - 345.6) < 1e-10);
    assert(displayViewport({256, 224}, {700, 600}, DisplayScaling::Integer) ==
           QRectF(94, 76, 512, 448));
    assert(displayViewport({}, {500, 400}, DisplayScaling::Integer).isEmpty());
    assert(displayViewport(native, {}, DisplayScaling::Aspect).isEmpty());

    const std::array<QRgb, 4> custom = {qRgb(1, 2, 3), qRgb(4, 5, 6), qRgb(7, 8, 9),
                                        qRgb(10, 11, 12)};
    const auto gray = monochromeColors(MonochromePalette::Grayscale, custom);
    assert(gray[0] == qRgb(255, 255, 255));
    assert(gray[1] == qRgb(170, 170, 170));
    assert(gray[2] == qRgb(85, 85, 85));
    assert(gray[3] == qRgb(0, 0, 0));
    assert(monochromeColors(MonochromePalette::Custom, custom) == custom);
    assert(monochromeColors(MonochromePalette::Lime, custom)[0] == QColor("#c6de8c").rgb());
    assert(monochromeColors(MonochromePalette::Olive, custom)[3] == QColor("#07100e").rgb());
    assert(monochromeColors(MonochromePalette::Teal, custom)[0] == QColor("#7fe2c3").rgb());
    assert(monochromeOffColor(MonochromePalette::Lime, custom[0]) == QColor("#d2e6a6").rgb());
    assert(monochromeOffColor(MonochromePalette::Grayscale, custom[0]) == qRgb(255, 255, 255));
    assert(monochromeOffColor(MonochromePalette::Olive, custom[0]) == QColor("#cfdaac").rgb());
    assert(monochromeOffColor(MonochromePalette::Teal, custom[0]) == QColor("#91ead0").rgb());
    assert(monochromeOffColor(MonochromePalette::Custom, custom[0]) == custom[0]);

    QImage black(160, 144, QImage::Format_RGB32);
    QImage white(160, 144, QImage::Format_RGB32);
    black.fill(Qt::black);
    white.fill(Qt::white);
    const auto simple = FrameBlending::Simple;
    const auto accurate = FrameBlending::Accurate;
    const auto blended = blendDisplayFrames(white, black, simple, false);
    assert(blended.pixel(0, 0) == qRgb(186, 186, 186));
    assert(blended == blendDisplayFrames(black, white, simple, true));
    assert(blendDisplayFrames(black, black, simple, false) == black);
    assert(blendDisplayFrames(white, white, accurate, true) == white);
    assert(blendDisplayFrames(white, {}, accurate, false) == white);
    assert(blendDisplayFrames(white, QImage(1, 1, QImage::Format_RGB32), accurate, false) == white);
    assert(blendDisplayFrames({}, black, simple, false).isNull());
    assert(blendDisplayFrames(white, black, FrameBlending::Disabled, true) == white);
    const auto even = blendDisplayFrames(white, black, accurate, false);
    const auto odd = blendDisplayFrames(white, black, accurate, true);
    for (int y = 0; y < white.height(); ++y) {
        assert(even.pixel(0, y) == (y & 1 ? qRgb(155, 155, 155) : qRgb(212, 212, 212)));
        assert(odd.pixel(159, y) == even.pixel(0, y ^ 1));
    }
    assert(odd == blendDisplayFrames(black, white, accurate, false));
    QImage oneChangedRow = white.copy();
    for (int x = 0; x < oneChangedRow.width(); ++x)
        oneChangedRow.setPixel(x, 0, qRgb(0, 0, 0));
    const auto partial = blendDisplayFrames(white, oneChangedRow, accurate, false);
    assert(partial.pixel(159, 0) == qRgb(212, 212, 212));
    assert(partial.pixel(159, 1) == qRgb(255, 255, 255));
    // Cover all channel values and the table's rounding independently.
    QImage ramp(256, 1, QImage::Format_RGB32);
    QImage inverse(256, 1, QImage::Format_RGB32);
    for (int x = 0; x < 256; ++x) {
        ramp.setPixel(x, 0, qRgb(x, x, x));
        inverse.setPixel(x, 0, qRgb(255 - x, 255 - x, 255 - x));
    }
    const auto blendedRamp = blendDisplayFrames(ramp, inverse, simple, false);
    const auto weightedRamp = blendDisplayFrames(ramp, inverse, accurate, true);
    for (int x = 0; x < 256; ++x) {
        const double a = std::pow(x / 255.0, 2.2);
        const double b = std::pow((255 - x) / 255.0, 2.2);
        const auto value = int(std::lround(std::pow((a + b) / 2.0, 1.0 / 2.2) * 255.0));
        assert(blendedRamp.pixel(x, 0) == qRgb(value, value, value));
        const auto weighted = int(std::lround(std::pow((a + 2 * b) / 3.0, 1.0 / 2.2) * 255.0));
        assert(weightedRamp.pixel(x, 0) == qRgb(weighted, weighted, weighted));
    }
}

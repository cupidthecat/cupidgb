#include "display_pipeline.h"

#include <QColor>
#include <cassert>
#include <cmath>

int main() {
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

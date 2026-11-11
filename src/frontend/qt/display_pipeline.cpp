#include "display_pipeline.h"

#include <QColor>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

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

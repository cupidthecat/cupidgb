#include "display_pipeline.h"

#include <QColor>
#include <algorithm>
#include <cmath>
#include <cstdint>

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

QImage blendDisplayFrames(const QImage &current, const QImage &previous) {
    if (current.isNull() || previous.isNull() || current.size() != previous.size())
        return current;
    // Mix emitted light, rather than averaging gamma-encoded bytes. The table keeps
    // floating-point exponentiation out of the per-frame pixel loop.
    static const auto mixed = [] {
        std::array<uint8_t, 256 * 256> result{};
        std::array<double, 256> linear{};
        for (size_t value = 0; value < linear.size(); ++value)
            linear[value] = std::pow(double(value) / 255.0, 2.2);
        for (size_t first = 0; first < linear.size(); ++first)
            for (size_t second = 0; second < linear.size(); ++second)
                result[first * 256 + second] = uint8_t(std::lround(
                    255.0 * std::pow((linear[first] + linear[second]) / 2.0, 1.0 / 2.2)));
        return result;
    }();
    const auto first = current.convertToFormat(QImage::Format_RGB32);
    const auto second = previous.convertToFormat(QImage::Format_RGB32);
    QImage output(first.size(), QImage::Format_RGB32);
    for (int y = 0; y < first.height(); ++y) {
        const auto *a = reinterpret_cast<const QRgb *>(first.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(second.constScanLine(y));
        auto *destination = reinterpret_cast<QRgb *>(output.scanLine(y));
        for (int x = 0; x < first.width(); ++x)
            destination[x] = qRgb(mixed[size_t(qRed(a[x])) * 256 + qRed(b[x])],
                                  mixed[size_t(qGreen(a[x])) * 256 + qGreen(b[x])],
                                  mixed[size_t(qBlue(a[x])) * 256 + qBlue(b[x])]);
    }
    return output;
}

#pragma once

#include <QImage>
#include <QRectF>
#include <array>

enum class DisplayScaling { Integer = 0, Aspect = 1, Fill = 2 };
enum class MonochromePalette { Lime = 0, Grayscale = 1, Olive = 2, Teal = 3, Custom = 4 };

QRectF displayViewport(QSize source, QSizeF available, DisplayScaling scaling,
                       qreal devicePixelRatio = 1.0);
std::array<QRgb, 4> monochromeColors(MonochromePalette palette, const std::array<QRgb, 4> &custom);
QImage blendDisplayFrames(const QImage &current, const QImage &previous);

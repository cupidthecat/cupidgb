#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>
#include <array>

struct DisplayPaletteFile {
    std::array<QRgb, 4> shades{};
    QRgb off = qRgb(255, 255, 255);
};

// The wire format stores darkest first, followed by a separate LCD-off color.
// Runtime display shades use the opposite order. Host structure layout is never serialized.
bool decodeDisplayPalette(const QByteArray &bytes, DisplayPaletteFile &palette);
QByteArray encodeDisplayPalette(const DisplayPaletteFile &palette);

// Creates a new PNG beside the cartridge without replacing any existing file.
// An empty result reports failure through error and leaves existing files untouched.
QString saveTimestampedScreenshot(const QImage &image, const QString &cartridge,
                                  const QDateTime &time, QString *error = nullptr);
bool saveScreenshotFile(const QImage &image, const QString &path, QString *error = nullptr);

/**
 * @file    display_files.h
 * @brief   Palette-file records and screenshot persistence interfaces.
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

/**
 * @file    palette_library.h
 * @brief   Palette-theme records and named-library storage interface.
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

#include "display_files.h"

#include <QVector>

struct PaletteTheme {
    QString name;
    DisplayPaletteFile colors;
    bool manual = true;
    bool separateOff = true;
    double brightness = 0;
    double hue = 0;
    double strength = 0;
};

void resolvePaletteTheme(PaletteTheme &theme);
bool decodePaletteTheme(const QByteArray &bytes, PaletteTheme &theme);
QByteArray encodePaletteTheme(const PaletteTheme &theme);

class PaletteLibrary final {
  public:
    explicit PaletteLibrary(const DisplayPaletteFile &fallback = {});
    static PaletteLibrary decode(const QByteArray &bytes, const DisplayPaletteFile &fallback,
                                 bool *valid = nullptr);
    QByteArray encode() const;
    const QVector<PaletteTheme> &themes() const;
    PaletteTheme &current();
    const PaletteTheme &current() const;
    bool select(const QString &name);
    bool add(PaletteTheme theme, const QString &name = "Untitled Palette");
    bool renameCurrent(const QString &name);
    void removeCurrent();
    bool restoreDefaults(const QVector<PaletteTheme> &defaults);

  private:
    QString uniqueName(const QString &name, const QString &excluding = {}) const;
    void sort();
    QVector<PaletteTheme> themes_;
    QString selected_;
};

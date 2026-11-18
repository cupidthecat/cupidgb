/**
 * @file    inspector_renderer.h
 * @brief   Physical memory views and graphics-inspector rendering interfaces.
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

#include "color_pipeline.h"

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QRegion>
#include <QString>
#include <array>

class QPainter;

extern "C" {
#include "cupid/gb/gb.h"
}

enum class InspectorMemoryRegion { Mapped, Rom, VideoRam, WorkRam, CartridgeRam };

struct InspectorMemoryBank {
    const uint8_t *data = nullptr;
    size_t size = 0;
};

size_t inspectorMemoryBankCount(const CupidGb &gb, InspectorMemoryRegion region);
InspectorMemoryBank inspectorMemoryBank(const CupidGb &gb, InspectorMemoryRegion region,
                                        size_t bank);

enum class InspectorPalette { Raw, Automatic, Background, Object };
enum class InspectorMap { Background, Window, Low, High };
enum class InspectorTileAddressing { Automatic, Unsigned, Signed };
enum class InspectorGrid { None, Tiles, Map };

struct InspectorOverlays {
    InspectorGrid grid = InspectorGrid::None;
    bool scrolling = false;
    QPoint scroll;
};

struct InspectorColors {
    InspectorPalette type = InspectorPalette::Raw;
    unsigned index = 0;
};

struct InspectorObject {
    unsigned index = 0;
    uint16_t address = 0;
    int x = 0, y = 0;
    uint8_t rawX = 0, rawY = 0, tile = 0, flags = 0;
    unsigned bank = 0, palette = 0, height = 8;
    bool visibleLines = false, lineLimited = false;
    QImage image;
};

QRgb inspectorRgb555(uint16_t color, const ColorTable *colors = nullptr);
std::array<uint16_t, 4> inspectorPaletteValues(const CupidGb &gb, bool objects, unsigned index);
QImage inspectorTiles(const CupidGb &gb, int bank, InspectorColors colors = {},
                      const ColorTable *colorTable = nullptr);
QImage inspectorMap(const CupidGb &gb, InspectorMap map,
                    InspectorTileAddressing addressing = InspectorTileAddressing::Automatic,
                    InspectorColors colors = {InspectorPalette::Automatic, 0},
                    const ColorTable *colorTable = nullptr);
InspectorObject inspectorObject(const CupidGb &gb, unsigned index,
                                const ColorTable *colorTable = nullptr);
QString inspectorMapCell(const CupidGb &gb, InspectorMap map, int x, int y);
QRegion inspectorViewport(QPoint scroll);
void paintInspectorOverlays(QPainter &painter, const QRectF &target, const QSize &imageSize,
                            const InspectorOverlays &overlays);

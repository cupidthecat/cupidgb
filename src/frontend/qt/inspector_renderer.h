#pragma once

#include <QImage>
#include <QString>
#include <array>

extern "C" {
#include "cupid/gb/gb.h"
}

enum class InspectorPalette { Raw, Automatic, Background, Object };
enum class InspectorMap { Background, Window, Low, High };
enum class InspectorTileAddressing { Automatic, Unsigned, Signed };

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

QRgb inspectorRgb555(uint16_t color);
std::array<uint16_t, 4> inspectorPaletteValues(const CupidGb &gb, bool objects, unsigned index);
QImage inspectorTiles(const CupidGb &gb, int bank, InspectorColors colors = {});
QImage inspectorMap(const CupidGb &gb, InspectorMap map,
                    InspectorTileAddressing addressing = InspectorTileAddressing::Automatic,
                    InspectorColors colors = {InspectorPalette::Automatic, 0});
InspectorObject inspectorObject(const CupidGb &gb, unsigned index);
QString inspectorMapCell(const CupidGb &gb, InspectorMap map, int x, int y);

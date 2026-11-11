#include "inspector_renderer.h"

#include <algorithm>

extern "C" {
#include "cupid/gbc/cgb.h"
}

namespace {
QRgb gray(unsigned shade) {
    const int value = 255 - int(shade & 3u) * 85;
    return qRgb(value, value, value);
}

bool colorHardware(const CupidGb &gb) {
    return gb.model == CUPID_GB_MODEL_CGB;
}

unsigned mapOffset(const CupidGb &gb, InspectorMap map) {
    bool high = map == InspectorMap::High;
    if (map == InspectorMap::Background)
        high = (gb.io_registers[0x40] & 8u) != 0;
    else if (map == InspectorMap::Window)
        high = (gb.io_registers[0x40] & 0x40u) != 0;
    return high ? 0x1c00u : 0x1800u;
}

unsigned tilePixel(const CupidGb &gb, unsigned tile, unsigned bank, unsigned x, unsigned y) {
    const size_t row = size_t(bank) * 0x2000u + size_t(tile) * 16u + y * 2u;
    const unsigned bit = 7u - x;
    return ((gb.video_ram[row] >> bit) & 1u) | (((gb.video_ram[row + 1u] >> bit) & 1u) << 1u);
}

QRgb color(const CupidGb &gb, unsigned pixel, InspectorColors selection, uint8_t attributes,
           const ColorTable *colorTable, bool mapPreview = false) {
    if (selection.type == InspectorPalette::Raw)
        return gray(pixel);
    const bool objects = selection.type == InspectorPalette::Object;
    const unsigned index = selection.type == InspectorPalette::Automatic
                               ? (gb.cgb_mode ? attributes & 7u : 0u)
                               : selection.index & 7u;
    if (gb.cgb_mode) {
        const auto values = inspectorPaletteValues(gb, objects, index);
        return inspectorRgb555(values[pixel], colorTable);
    }
    const unsigned registerIndex = objects ? (index ? 0x49u : 0x48u) : 0x47u;
    const unsigned shade = (gb.io_registers[registerIndex] >> (pixel * 2u)) & 3u;
    // A monochrome map's forced object palette displays the raw pixel index.
    // Tile and object previews instead apply the selected OBP register mapping.
    const unsigned displayed = mapPreview && objects ? pixel : shade;
    if (colorHardware(gb))
        return inspectorRgb555(inspectorPaletteValues(gb, objects, index)[displayed], colorTable);
    return gray(displayed);
}
} // namespace

QRgb inspectorRgb555(uint16_t color, const ColorTable *colors) {
    if (colors)
        return (*colors)[color & 0x7fffu];
    const auto expand = [](unsigned value) { return int((value << 3u) | (value >> 2u)); };
    return qRgb(expand(color & 31u), expand((color >> 5u) & 31u), expand((color >> 10u) & 31u));
}

std::array<uint16_t, 4> inspectorPaletteValues(const CupidGb &gb, bool objects, unsigned index) {
    std::array<uint16_t, 4> result{};
    const auto *bytes = objects ? gb.cgb_obj_palette_ram : gb.cgb_bg_palette_ram;
    for (unsigned entry = 0; entry < result.size(); ++entry) {
        const unsigned offset = (index & 7u) * 8u + entry * 2u;
        result[entry] = uint16_t(bytes[offset] | (unsigned(bytes[offset + 1u]) << 8u));
    }
    return result;
}

QImage inspectorTiles(const CupidGb &gb, int bank, InspectorColors colors,
                      const ColorTable *colorTable) {
    if (bank < -1 || bank > 1)
        return {};
    const unsigned banks = bank == -1 ? 2u : 1u;
    QImage image(int(banks * 128u), 192, QImage::Format_RGB32);
    for (unsigned panel = 0; panel < banks; ++panel) {
        const unsigned selected = bank == -1 ? panel : unsigned(bank);
        for (unsigned tile = 0; tile < 384; ++tile) {
            for (unsigned y = 0; y < 8; ++y) {
                auto *row = reinterpret_cast<QRgb *>(image.scanLine(int(tile / 16u * 8u + y)));
                for (unsigned x = 0; x < 8; ++x) {
                    const unsigned pixel =
                        selected && !colorHardware(gb) ? 0u : tilePixel(gb, tile, selected, x, y);
                    row[panel * 128u + tile % 16u * 8u + x] =
                        selected && !colorHardware(gb) ? gray(0)
                                                       : color(gb, pixel, colors, 0, colorTable);
                }
            }
        }
    }
    return image;
}

QImage inspectorMap(const CupidGb &gb, InspectorMap map, InspectorTileAddressing addressing,
                    InspectorColors colors, const ColorTable *colorTable) {
    QImage image(256, 256, QImage::Format_RGB32);
    const unsigned base = mapOffset(gb, map);
    const bool unsignedTiles =
        addressing == InspectorTileAddressing::Unsigned ||
        (addressing == InspectorTileAddressing::Automatic && (gb.io_registers[0x40] & 0x10u));
    for (unsigned cell = 0; cell < 1024; ++cell) {
        const unsigned tileByte = gb.video_ram[base + cell];
        // Signed tile numbers address the block centered on 0x9000. Keeping
        // the physical tile number positive makes both modes use one decoder.
        const int signedNumber = tileByte < 128u ? int(tileByte) : int(tileByte) - 256;
        const unsigned tile = unsignedTiles ? tileByte : unsigned(256 + signedNumber);
        const uint8_t attributes = gb.cgb_mode ? gb.video_ram[base + 0x2000u + cell] : 0u;
        const unsigned bank = attributes & 8u ? 1u : 0u;
        for (unsigned y = 0; y < 8; ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(int(cell / 32u * 8u + y)));
            for (unsigned x = 0; x < 8; ++x) {
                const unsigned pixel = tilePixel(gb, tile, bank, x ^ (attributes & 0x20u ? 7u : 0u),
                                                 y ^ (attributes & 0x40u ? 7u : 0u));
                row[cell % 32u * 8u + x] = color(gb, pixel, colors, attributes, colorTable, true);
            }
        }
    }
    return image;
}

InspectorObject inspectorObject(const CupidGb &gb, unsigned index, const ColorTable *colorTable) {
    InspectorObject result;
    if (index >= 40)
        return result;
    const uint8_t *bytes = gb.object_attribute_memory + index * 4u;
    result.index = index;
    result.address = uint16_t(0xfe00u + index * 4u);
    result.rawY = bytes[0];
    result.rawX = bytes[1];
    result.y = int(bytes[0]) - 16;
    result.x = int(bytes[1]) - 8;
    result.height = gb.io_registers[0x40] & 4u ? 16u : 8u;
    result.tile = uint8_t(result.height == 16 ? bytes[2] & 0xfeu : bytes[2]);
    result.flags = bytes[3];
    result.bank = colorHardware(gb) && (bytes[3] & 8u) ? 1u : 0u;
    result.palette = gb.cgb_mode ? bytes[3] & 7u : (bytes[3] & 0x10u ? 1u : 0u);
    result.visibleLines = result.y < 144 && result.y + int(result.height) > 0;
    for (int y = std::max(0, result.y); y < std::min(144, result.y + int(result.height)); ++y) {
        unsigned earlier = 0;
        for (unsigned previous = 0; previous < index; ++previous) {
            const int top = int(gb.object_attribute_memory[previous * 4u]) - 16;
            if (y >= top && y < top + int(result.height))
                ++earlier;
        }
        result.lineLimited |= earlier >= 10;
    }
    result.image = QImage(8, int(result.height), QImage::Format_RGB32);
    for (unsigned y = 0; y < result.height; ++y) {
        auto *row = reinterpret_cast<QRgb *>(result.image.scanLine(int(y)));
        const unsigned sourceY = result.flags & 0x40u ? result.height - 1u - y : y;
        for (unsigned x = 0; x < 8; ++x) {
            const unsigned sourceX = result.flags & 0x20u ? 7u - x : x;
            const unsigned pixel =
                tilePixel(gb, result.tile + sourceY / 8u, result.bank, sourceX, sourceY % 8u);
            // Fully white/black DMG object registers otherwise hide every
            // shape. Inspector previews retain a distinct transparent shade.
            CupidGb const *paletteGb = &gb;
            uint8_t registerValue = gb.io_registers[result.palette ? 0x49 : 0x48];
            unsigned shade = pixel;
            if (!gb.cgb_mode && (registerValue == 0 || registerValue == 0xff)) {
                registerValue = registerValue ? 0xfc : 3;
                shade = (registerValue >> (pixel * 2u)) & 3u;
                row[x] = cupid_cgb_compat_active(paletteGb)
                             ? inspectorRgb555(cupid_cgb_compat_obj_color(paletteGb, result.palette,
                                                                          uint8_t(shade)),
                                               colorTable)
                             : gray(shade);
            } else {
                row[x] =
                    color(gb, pixel, {InspectorPalette::Object, result.palette}, 0, colorTable);
            }
        }
    }
    return result;
}

QString inspectorMapCell(const CupidGb &gb, InspectorMap map, int x, int y) {
    if (x < 0 || x >= 256 || y < 0 || y >= 256)
        return {};
    const unsigned offset = mapOffset(gb, map) + unsigned(y / 8) * 32u + unsigned(x / 8);
    const unsigned attributes = gb.cgb_mode ? gb.video_ram[offset + 0x2000u] : 0u;
    return QString("Map $%1  Tile $%2  Bank %3  Palette %4  X flip %5  Y flip %6  Priority %7")
        .arg(offset + 0x8000u, 4, 16, QLatin1Char('0'))
        .arg(gb.video_ram[offset], 2, 16, QLatin1Char('0'))
        .arg(attributes & 8u ? 1 : 0)
        .arg(attributes & 7u)
        .arg(bool(attributes & 0x20u))
        .arg(bool(attributes & 0x40u))
        .arg(bool(attributes & 0x80u));
}

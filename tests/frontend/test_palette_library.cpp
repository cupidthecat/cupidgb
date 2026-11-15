#include "palette_library.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cassert>
#include <cmath>
#include <limits>

namespace {
DisplayPaletteFile grayscale() {
    return {{qRgb(255, 255, 255), qRgb(170, 170, 170), qRgb(85, 85, 85), qRgb(0, 0, 0)},
            qRgb(255, 255, 255)};
}

bool sameColors(const DisplayPaletteFile &first, const DisplayPaletteFile &second) {
    return first.shades == second.shades && first.off == second.off;
}
} // namespace

int main() {
    PaletteTheme theme;
    theme.colors = grayscale();
    theme.manual = false;
    theme.separateOff = false;
    resolvePaletteTheme(theme);
    assert(sameColors(theme.colors, grayscale()));
    theme.separateOff = true;
    resolvePaletteTheme(theme);
    assert((theme.colors.shades == std::array<QRgb, 4>{qRgb(245, 245, 245), qRgb(163, 163, 163),
                                                       qRgb(82, 82, 82), qRgb(0, 0, 0)}));
    theme.brightness = 1;
    resolvePaletteTheme(theme);
    assert((theme.colors.shades == std::array<QRgb, 4>{qRgb(252, 252, 252), qRgb(228, 228, 228),
                                                       qRgb(192, 192, 192), qRgb(0, 0, 0)}));
    theme.brightness = -1;
    resolvePaletteTheme(theme);
    assert((theme.colors.shades == std::array<QRgb, 4>{qRgb(217, 217, 217), qRgb(43, 43, 43),
                                                       qRgb(3, 3, 3), qRgb(0, 0, 0)}));
    theme.brightness = 0;
    theme.separateOff = false;
    theme.strength = 1;
    theme.hue = 0;
    resolvePaletteTheme(theme);
    assert((theme.colors.shades == std::array<QRgb, 4>{qRgb(255, 255, 255), qRgb(230, 50, 50),
                                                       qRgb(194, 3, 3), qRgb(0, 0, 0)}));
    const auto redRamp = theme.colors;
    theme.hue = 1;
    resolvePaletteTheme(theme);
    assert(sameColors(theme.colors, redRamp));
    theme.hue = 1.0 / 3;
    resolvePaletteTheme(theme);
    assert(theme.colors.shades[1] == qRgb(50, 230, 50));
    theme.hue = 2.0 / 3;
    resolvePaletteTheme(theme);
    assert(theme.colors.shades[1] == qRgb(50, 50, 230));

    theme.brightness = -0.5;
    theme.hue = 0.25;
    theme.strength = 0.5;
    theme.separateOff = true;
    resolvePaletteTheme(theme);
    const auto encoded = encodePaletteTheme(theme);
    assert(encoded.size() == 32 && encoded[4] == 2);
    assert(encoded.mid(20) == QByteArray::fromHex("000000e00000002000000040"));
    PaletteTheme decoded;
    assert(decodePaletteTheme(encoded, decoded));
    assert(!decoded.manual && decoded.separateOff && decoded.brightness == -0.5 &&
           decoded.hue == 0.25 && decoded.strength == 0.5);
    assert(sameColors(decoded.colors, theme.colors));
    assert(encodePaletteTheme(decoded) == encoded);
    auto fractional = theme;
    fractional.brightness = -17.75 / 1073741824.0;
    fractional.hue = 27.75 / 2147483648.0;
    fractional.strength = 35.75 / 2147483648.0;
    assert(encodePaletteTheme(fractional).mid(20) ==
           QByteArray::fromHex("efffffff1c00000023000000"));
    auto storedSamples = encoded;
    storedSamples[8] = char(17);
    assert(decodePaletteTheme(storedSamples, decoded));
    assert(encodePaletteTheme(decoded) == storedSamples);
    const auto before = decoded;
    assert(!decodePaletteTheme(QByteArray("broken"), decoded));
    assert(sameColors(decoded.colors, before.colors) && decoded.hue == before.hue);

    theme.manual = true;
    assert(encodePaletteTheme(theme).size() == 20);
    theme.separateOff = false;
    const auto fourColors = encodePaletteTheme(theme);
    assert(fourColors.size() == 17 && fourColors[4] == 1);
    assert(decodePaletteTheme(fourColors, decoded));
    assert(decoded.manual && !decoded.separateOff &&
           decoded.colors.off == decoded.colors.shades[0]);
    theme.brightness = std::numeric_limits<double>::quiet_NaN();
    theme.hue = std::numeric_limits<double>::infinity();
    theme.strength = -100;
    resolvePaletteTheme(theme);
    assert(theme.brightness == 0 && theme.hue == 0 && theme.strength == 0);

    bool valid = false;
    auto library = PaletteLibrary::decode({}, grayscale(), &valid);
    assert(valid && library.themes().size() == 1 && library.current().name == "Custom Palette");
    assert(sameColors(library.current().colors, grayscale()));
    assert(library.add(library.current()) && library.current().name == "Untitled Palette");
    assert(library.add(library.current()) && library.current().name == "Untitled Palette 2");
    assert(library.renameCurrent("Custom Palette") && library.current().name == "Custom Palette 2");
    assert(library.renameCurrent("  ") && library.current().name == "Untitled Palette 2");
    assert(library.renameCurrent(QString::fromUtf8("Caf\xc3\xa9 \"blue\"")));
    library.current().manual = false;
    library.current().brightness = -0.25;
    library.current().hue = 0.5;
    library.current().strength = 0.125;
    const auto saved = library.encode();
    auto restored = PaletteLibrary::decode(saved, {}, &valid);
    assert(valid && restored.encode() == saved && restored.themes().size() == 3);
    assert(restored.current().name == library.current().name &&
           restored.current().brightness == -0.25);
    assert(!restored.select("missing") && restored.current().name == library.current().name);
    restored.current().manual = true;
    const auto manualSaved = restored.encode();
    restored = PaletteLibrary::decode(manualSaved, {}, &valid);
    assert(valid && restored.current().manual && restored.current().brightness == -0.25 &&
           restored.current().hue == 0.5 && restored.current().strength == 0.125);
    restored.removeCurrent();
    assert(restored.themes().size() == 2);
    restored.removeCurrent();
    restored.removeCurrent();
    assert(restored.themes().size() == 1 && restored.current().name == "Untitled Palette");
    PaletteTheme builtin;
    builtin.name = "Built-in";
    builtin.colors = grayscale();
    restored.restoreDefaults({builtin});
    assert(restored.themes().size() == 2 && restored.select("Built-in"));
    restored.current().colors.off = qRgb(1, 2, 3);
    restored.restoreDefaults({builtin});
    assert(restored.themes().size() == 2 && restored.current().colors.off == builtin.colors.off);
    assert(restored.select("Untitled Palette"));
    assert(restored.current().name == "Untitled Palette");

    for (const QByteArray &invalid : {QByteArray("no json"), QByteArray("[]"),
                                      QByteArray("{\"version\":2}"), QByteArray(2100000, 'x')}) {
        restored = PaletteLibrary::decode(invalid, grayscale(), &valid);
        assert(!valid && restored.themes().size() == 1 &&
               sameColors(restored.current().colors, grayscale()));
    }
    auto malformed = QJsonDocument::fromJson(saved).object();
    auto entries = malformed.value("themes").toArray();
    entries.append(entries[0]);
    malformed["themes"] = entries;
    restored = PaletteLibrary::decode(QJsonDocument(malformed).toJson(), grayscale(), &valid);
    assert(!valid && restored.themes().size() == 1);
    malformed = QJsonDocument::fromJson(saved).object();
    malformed["selected"] = "not present";
    restored = PaletteLibrary::decode(QJsonDocument(malformed).toJson(), grayscale(), &valid);
    assert(!valid && restored.themes().size() == 1);
    PaletteLibrary full(grayscale());
    for (unsigned i = 1; i < 256; ++i)
        assert(full.add(full.current()));
    assert(!full.add(full.current()) && full.themes().size() == 256);
    const auto fullSaved = full.encode();
    auto replacement = full.current();
    replacement.colors.off = qRgb(12, 34, 56);
    // Failure after updating an existing name must roll back the whole operation.
    assert(!full.restoreDefaults({replacement, builtin}));
    assert(full.encode() == fullSaved);
    assert(full.restoreDefaults({replacement}));
    assert(full.current().colors.off == replacement.colors.off && full.themes().size() == 256);
    full.removeCurrent();
    const QString selected = full.current().name;
    assert(full.restoreDefaults({builtin}));
    assert(full.themes().size() == 256 && full.current().name == selected);
}

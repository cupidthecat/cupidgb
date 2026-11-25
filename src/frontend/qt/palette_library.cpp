/**
 * @file    palette_library.cpp
 * @brief   Palette-theme conversion, validation, and named-library persistence.
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

#include "palette_library.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qsizetype maximumThemes = 256;
constexpr qsizetype maximumName = 128;

double bounded(double value, double lower, double upper) {
    return std::isfinite(value) ? std::clamp(value, lower, upper) : 0;
}

QRgb normalizedRgb(QRgb value) {
    return qRgb(qRed(value), qGreen(value), qBlue(value));
}

void appendWord(QByteArray &bytes, quint32 value) {
    const quint32 little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&little), sizeof(little));
}
} // namespace

void resolvePaletteTheme(PaletteTheme &theme) {
    theme.brightness = bounded(theme.brightness, -1, 1);
    theme.hue = bounded(theme.hue, 0, 1);
    theme.strength = bounded(theme.strength, 0, 1);
    for (auto &color : theme.colors.shades)
        color = normalizedRgb(color);
    theme.colors.off = normalizedRgb(theme.colors.off);
    if (theme.manual) {
        if (!theme.separateOff)
            theme.colors.off = theme.colors.shades[0];
        return;
    }
    const auto darkest = theme.colors.shades[3];
    const auto lightest = theme.colors.off;
    const std::array<int, 3> dark = {qRed(darkest), qGreen(darkest), qBlue(darkest)};
    const std::array<int, 3> light = {qRed(lightest), qGreen(lightest), qBlue(lightest)};
    std::array<double, 3> exponents{};
    // Hue is a six-sector RGB ramp. Brightness and per-channel hue bias bend
    // interpolation between the dark endpoint and the disabled-LCD endpoint.
    constexpr std::array<int, 3> sectors = {0, 4, 2};
    for (size_t channel = 0; channel < exponents.size(); ++channel) {
        const double ramp = std::clamp(
            std::abs(std::fmod(theme.hue * 6 + sectors[channel], 6.0) - 3) - 1, 0.0, 1.0);
        exponents[channel] = std::pow(4.0, -theme.brightness - (ramp * 2 - 1) * theme.strength);
    }
    for (size_t shade = 0; shade < 3; ++shade) {
        const double position =
            theme.separateOff ? double(3 - shade) * 8 / 25 : double(3 - shade) / 3;
        std::array<int, 3> channels{};
        for (size_t channel = 0; channel < channels.size(); ++channel) {
            const double weight = std::pow(position, exponents[channel]);
            channels[channel] =
                int(std::lround(dark[channel] + (light[channel] - dark[channel]) * weight));
        }
        theme.colors.shades[shade] = qRgb(channels[0], channels[1], channels[2]);
    }
}

bool decodePaletteTheme(const QByteArray &bytes, PaletteTheme &theme) {
    PaletteTheme candidate;
    if (!decodeDisplayPalette(bytes, candidate.colors))
        return false;
    const auto flags = static_cast<unsigned char>(bytes[4]);
    candidate.manual = (flags & 1u) || bytes.size() < 32;
    candidate.separateOff = (flags & 2u) != 0;
    if (bytes.size() >= 32) {
        const auto *metadata = reinterpret_cast<const uchar *>(bytes.constData() + 20);
        candidate.brightness = qFromLittleEndian<qint32>(metadata) / 1073741824.0;
        candidate.hue = qFromLittleEndian<quint32>(metadata + 4) / 2147483648.0;
        candidate.strength = qFromLittleEndian<quint32>(metadata + 8) / 2147483648.0;
    }
    candidate.brightness = bounded(candidate.brightness, -1, 1);
    candidate.hue = bounded(candidate.hue, 0, 1);
    candidate.strength = bounded(candidate.strength, 0, 1);
    // Imported resolved samples remain authoritative until an editor control changes.
    // Opening an automatic palette must not introduce a second quantization pass.
    theme = candidate;
    return true;
}

QByteArray encodePaletteTheme(const PaletteTheme &theme) {
    auto candidate = theme;
    candidate.brightness = bounded(candidate.brightness, -1, 1);
    candidate.hue = bounded(candidate.hue, 0, 1);
    candidate.strength = bounded(candidate.strength, 0, 1);
    if (candidate.manual && !candidate.separateOff)
        candidate.colors.off = candidate.colors.shades[0];
    QByteArray bytes = encodeDisplayPalette(candidate.colors);
    bytes[4] = char((candidate.manual ? 1 : 0) | (candidate.separateOff ? 2 : 0));
    if (candidate.manual) {
        if (!candidate.separateOff)
            bytes.truncate(17);
    } else {
        // The signed brightness and unsigned strength fields truncate toward zero;
        // the hue field rounds to the nearest fixed-point step.
        appendWord(bytes, quint32(qint32(candidate.brightness * 1073741824.0)));
        appendWord(bytes, quint32(std::llround(candidate.hue * 2147483648.0)));
        appendWord(bytes, quint32(candidate.strength * 2147483648.0));
    }
    return bytes;
}

PaletteLibrary::PaletteLibrary(const DisplayPaletteFile &fallback) {
    PaletteTheme theme;
    theme.name = "Custom Palette";
    theme.colors = fallback;
    themes_.append(theme);
    selected_ = theme.name;
}

PaletteLibrary PaletteLibrary::decode(const QByteArray &bytes, const DisplayPaletteFile &fallback,
                                      bool *valid) {
    PaletteLibrary library(fallback);
    if (valid)
        *valid = bytes.isEmpty();
    if (bytes.isEmpty() || bytes.size() > 2 * 1024 * 1024)
        return library;
    const auto document = QJsonDocument::fromJson(bytes);
    const auto root = document.object();
    if (!document.isObject() || root.value("version").toInt() != 1 ||
        !root.value("themes").isArray())
        return library;
    const auto themes = root.value("themes").toArray();
    if (themes.isEmpty() || themes.size() > maximumThemes)
        return library;
    library.themes_.clear();
    for (const auto &entry : themes) {
        const auto value = entry.toObject();
        const QString name = value.value("name").toString();
        if (name.isEmpty() || name.size() > maximumName || name.trimmed() != name ||
            library.uniqueName(name) != name)
            return PaletteLibrary(fallback);
        const auto encoded = QByteArray::fromBase64(value.value("data").toString().toLatin1(),
                                                    QByteArray::AbortOnBase64DecodingErrors);
        PaletteTheme theme;
        if (!decodePaletteTheme(encoded, theme))
            return PaletteLibrary(fallback);
        const std::array<const char *, 3> parameters = {"brightness", "hue", "strength"};
        std::array<double *, 3> targets = {&theme.brightness, &theme.hue, &theme.strength};
        for (size_t parameter = 0; parameter < parameters.size(); ++parameter) {
            if (!value.contains(parameters[parameter]))
                continue;
            const auto stored = value.value(parameters[parameter]);
            const double number = stored.toDouble(std::numeric_limits<double>::quiet_NaN());
            if (!stored.isDouble() || !std::isfinite(number) || number > 1 ||
                number < (parameter == 0 ? -1 : 0))
                return PaletteLibrary(fallback);
            *targets[parameter] = number;
        }
        theme.name = name;
        library.themes_.append(theme);
    }
    library.sort();
    if (!library.select(root.value("selected").toString()))
        return PaletteLibrary(fallback);
    if (valid)
        *valid = true;
    return library;
}

QByteArray PaletteLibrary::encode() const {
    QJsonArray themes;
    // Compact manual palette files omit editor parameters. Keep those parameters
    // separately so switching modes across application restarts does not erase them.
    for (const auto &theme : themes_)
        themes.append(
            QJsonObject{{"name", theme.name},
                        {"data", QString::fromLatin1(encodePaletteTheme(theme).toBase64())},
                        {"brightness", bounded(theme.brightness, -1, 1)},
                        {"hue", bounded(theme.hue, 0, 1)},
                        {"strength", bounded(theme.strength, 0, 1)}});
    return QJsonDocument(QJsonObject{{"version", 1}, {"selected", selected_}, {"themes", themes}})
        .toJson(QJsonDocument::Compact);
}

const QVector<PaletteTheme> &PaletteLibrary::themes() const {
    return themes_;
}

PaletteTheme &PaletteLibrary::current() {
    const auto found = std::find_if(themes_.begin(), themes_.end(),
                                    [this](const auto &theme) { return theme.name == selected_; });
    return found != themes_.end() ? *found : themes_.front();
}

const PaletteTheme &PaletteLibrary::current() const {
    const auto found = std::find_if(themes_.begin(), themes_.end(),
                                    [this](const auto &theme) { return theme.name == selected_; });
    return found != themes_.end() ? *found : themes_.front();
}

bool PaletteLibrary::select(const QString &name) {
    for (const auto &theme : themes_)
        if (theme.name == name) {
            selected_ = name;
            return true;
        }
    return false;
}

QString PaletteLibrary::uniqueName(const QString &requested, const QString &excluding) const {
    const QString base =
        requested.trimmed().isEmpty() ? "Untitled Palette" : requested.trimmed().left(maximumName);
    QString candidate = base;
    unsigned suffix = 2;
    while (std::any_of(themes_.begin(), themes_.end(), [&](const auto &theme) {
        return theme.name != excluding && theme.name == candidate;
    }))
        candidate = base.left(maximumName - 12) + QString(" %1").arg(suffix++);
    return candidate;
}

void PaletteLibrary::sort() {
    std::sort(themes_.begin(), themes_.end(), [](const auto &first, const auto &second) {
        const int comparison = QString::localeAwareCompare(first.name, second.name);
        return comparison ? comparison < 0 : first.name < second.name;
    });
}

bool PaletteLibrary::add(PaletteTheme theme, const QString &name) {
    if (themes_.size() >= maximumThemes)
        return false;
    theme.name = uniqueName(name);
    selected_ = theme.name;
    themes_.append(theme);
    sort();
    return true;
}

bool PaletteLibrary::renameCurrent(const QString &name) {
    const QString replacement = uniqueName(name, selected_);
    current().name = replacement;
    selected_ = replacement;
    sort();
    return true;
}

void PaletteLibrary::removeCurrent() {
    if (themes_.size() == 1) {
        current().name = "Untitled Palette";
        selected_ = current().name;
        return;
    }
    themes_.erase(std::remove_if(themes_.begin(), themes_.end(),
                                 [this](const auto &theme) { return theme.name == selected_; }),
                  themes_.end());
    selected_ = themes_.front().name;
}

bool PaletteLibrary::restoreDefaults(const QVector<PaletteTheme> &defaults) {
    // Stage the entire restore: a full library must not partially replace named
    // entries or discard user palettes to make space for missing built-ins.
    auto restored = *this;
    const QString previous = selected_;
    for (auto theme : defaults) {
        if (restored.select(theme.name))
            restored.current() = theme;
        else if (!restored.add(theme, theme.name))
            return false;
    }
    restored.select(previous);
    *this = std::move(restored);
    return true;
}

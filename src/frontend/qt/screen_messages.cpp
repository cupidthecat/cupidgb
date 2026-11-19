/**
 * @file    screen_messages.cpp
 * @brief   Guest-frame message lifetime, cartridge labels, and image composition.
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

#include "screen_messages.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QStringList>
#include <algorithm>
#include <array>

namespace {
constexpr std::array<quint32, 256> makeCrcTable() {
    std::array<quint32, 256> result{};
    for (quint32 byte = 0; byte < result.size(); ++byte) {
        quint32 remainder = byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            remainder = (remainder >> 1) ^ ((remainder & 1) ? 0xedb88320u : 0u);
        result[byte] = remainder;
    }
    return result;
}

constexpr auto crcTable = makeCrcTable();
} // namespace

QImage composeScreenMessage(const QImage &frame, const QImage &message) {
    if (frame.isNull() || message.isNull() || frame.size() != message.size())
        return frame;
    QImage composed = frame;
    {
        QPainter painter(&composed);
        painter.drawImage(0, 0, message);
    }
    return composed;
}

void ScreenMessages::setEnabled(bool enabled) {
    if (enabled_ != enabled) {
        enabled_ = enabled;
        clear();
    }
}

bool ScreenMessages::enabled() const {
    return enabled_;
}

void ScreenMessages::show(const QString &message) {
    if (!enabled_)
        return;
    // File names may be unbounded. Keep presentation storage and text shaping
    // bounded without constraining the paths accepted by the cartridge loader.
    const QString bounded = message.left(512);
    if (text_ != bounded) {
        text_ = bounded;
        image_ = {};
    }
    remaining_ = text_.isEmpty() ? 0 : unsigned(text_.count('\n') + 1) * 30u;
}

void ScreenMessages::showCartridge(const QString &version, QByteArrayView rom) {
    if (!enabled_)
        return;
    if (rom.size() < 0x150) {
        clear();
        return;
    }
    // Display identity follows the populated address lines, including erased
    // padding. MMM01 menu-first dumps present the first game before the menu.
    // Stream that logical image without changing ROM bytes or native identity.
    const quint64 size = quint64(rom.size());
    quint64 populatedSize = 0x8000;
    while (populatedSize < size)
        populatedSize *= 2;
    const auto type = quint8(rom[0x147]);
    const quint64 rotation = type >= 0x0b && type <= 0x0d && populatedSize > 0x8000 ? 0x8000 : 0;
    const auto byteAt = [&](quint64 offset) {
        return offset < size ? quint8(rom[qsizetype(offset)]) : quint8(0xff);
    };
    QString title;
    for (quint64 index = 0; index < 16; ++index) {
        const quint8 byte = byteAt(rotation + 0x134 + index);
        if (byte < 0x20 || byte >= 0x80)
            break;
        title += QChar(byte);
    }
    quint32 crc = 0xffffffffu;
    const auto append = [&](quint8 byte) { crc = (crc >> 8) ^ crcTable[quint8(crc) ^ byte]; };
    const auto appendRange = [&](quint64 begin, quint64 end) {
        const quint64 dataEnd = std::min(end, size);
        for (quint64 offset = begin; offset < dataEnd; ++offset)
            append(quint8(rom[qsizetype(offset)]));
        for (quint64 offset = std::max(begin, dataEnd); offset < end; ++offset)
            append(0xff);
    };
    appendRange(rotation, populatedSize);
    appendRange(0, rotation);
    const QString checksum =
        QString::number(crc ^ 0xffffffffu, 16).rightJustified(8, QLatin1Char('0')).toUpper();
    show(QStringLiteral("CupidGB v%1\n%2\n%3").arg(version, title, checksum));
}

void ScreenMessages::advanceFrame(const QString &heldMode) {
    if (!enabled_)
        return;
    if (!heldMode.isEmpty())
        show(heldMode);
    if (remaining_ && --remaining_ == 0)
        clear();
}

void ScreenMessages::clear() {
    text_.clear();
    remaining_ = 0;
    image_ = {};
}

QString ScreenMessages::text() const {
    return text_;
}

unsigned ScreenMessages::remainingFrames() const {
    return remaining_;
}

const QImage &ScreenMessages::image(const QSize &size) const {
    if (text_.isEmpty() || size.isEmpty()) {
        image_ = {};
        return image_;
    }
    if (image_.size() == size)
        return image_;
    image_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
    image_.fill(Qt::transparent);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setFamilies(
        {QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"), font.family()});
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(10);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::NoAntialias);
    const QFontMetrics metrics(font);
    const QStringList lines = text_.split('\n');
    const int top = size.height() - 8 - int(lines.size()) * 12;
    QPainter painter(&image_);
    painter.setFont(font);
    const std::array<QPoint, 4> outline = {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1),
                                           QPoint(0, 1)};
    // Keep glyphs in the hinted text rasterizer. Converting tiny letters into
    // unhinted curves can break their thin white faces into disconnected dots.
    // Integer baselines and monochrome coverage also avoid color fringes when
    // native pixels pass through frame blending and display scaling.
    for (qsizetype line = 0; line < lines.size(); ++line) {
        const QPoint baseline(8, top + int(line) * 12 + metrics.ascent());
        painter.setPen(Qt::black);
        for (const QPoint &offset : outline)
            painter.drawText(baseline + offset, lines[line]);
        painter.setPen(Qt::white);
        painter.drawText(baseline, lines[line]);
    }
    return image_;
}

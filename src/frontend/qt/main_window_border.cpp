/**
 * @file    main_window_border.cpp
 * @brief   Qt border composition and isolated cartridge border capture.
 * @author  cupidthecat
 * @date    2026-11-24
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "main_window_internal.h"

#include "cupid/gb/sgb_border.h"

#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>

namespace {
constexpr uint16_t rgb555(unsigned red, unsigned green, unsigned blue) {
    return uint16_t(red | (green << 5) | (blue << 10));
}

struct BezelColors {
    uint16_t surface;
    uint16_t shade;
    uint16_t highlight;
};

constexpr BezelColors classicBezel{rgb555(24, 23, 20), rgb555(15, 14, 17), rgb555(28, 27, 24)};
constexpr BezelColors pocketBezel{rgb555(22, 23, 23), rgb555(13, 15, 16), rgb555(27, 28, 28)};
constexpr std::array<BezelColors, 5> colorBezels = {{
    {rgb555(24, 7, 9), rgb555(14, 4, 6), rgb555(29, 11, 13)},
    {rgb555(11, 10, 23), rgb555(6, 6, 15), rgb555(17, 16, 28)},
    {rgb555(14, 23, 12), rgb555(8, 14, 7), rgb555(19, 28, 16)},
    {rgb555(28, 23, 8), rgb555(18, 14, 5), rgb555(31, 28, 15)},
    {rgb555(9, 22, 22), rgb555(5, 13, 15), rgb555(13, 27, 27)},
}};

BezelColors bezelColors(CupidGbModel model, unsigned checksumHigh) {
    if (model == CUPID_GB_MODEL_MGB)
        return pocketBezel;
    if (!cupid_gb_model_is_cgb(model))
        return classicBezel;
    if (model == CUPID_GB_MODEL_CGB_0)
        return colorBezels[1];
    if (model == CUPID_GB_MODEL_CGB_A)
        return colorBezels[0];
    return colorBezels[checksumHigh % colorBezels.size()];
}

void drawHandheldBezel(QImage &image, BezelColors colors, int temperature) {
    const auto table = makeColorTable(ColorHardware::Sgb, {ColorCorrection::Disabled, temperature});
    const auto color = [&](uint16_t rgb) { return QColor::fromRgb(table[rgb]); };
    image.fill(table[colors.surface]);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color(colors.shade));
    painter.drawRoundedRect(QRect(26, 19, 204, 186), 13, 13);
    painter.setBrush(color(rgb555(7, 7, 8)));
    painter.drawRoundedRect(QRect(30, 23, 196, 178), 10, 10);
    painter.fillRect(QRect(44, 36, 168, 152), color(rgb555(3, 4, 4)));
    painter.fillRect(QRect(37, 34, 3, 3), color(rgb555(26, 10, 14)));
    painter.fillRect(QRect(20, 210, 36, 2), color(colors.highlight));
    painter.fillRect(QRect(200, 210, 36, 2), color(colors.highlight));
}
} // namespace

bool MainWindow::showSgbBorder(const CupidGb &gb) const {
    return sgbBorderMode_ == SgbBorderMode::Always ||
           (sgbBorderMode_ == SgbBorderMode::SgbOnly && cupid_gb_sgb_host_present(&gb));
}

void MainWindow::invalidateSgbBorder() {
    ++sgbBorderCartridge_;
    sgbBorderAttempted_ = false;
    borrowedSgbBorder_.reset();
    sgbBorderImage_ = {};
}

void MainWindow::borrowSgbBorder() {
    if (sgbBorderAttempted_ || sgbBorderJobPending_ || !emulator_ || musicInfo_ ||
        sgbBorderMode_ != SgbBorderMode::Always || cupid_gb_model_is_sgb(emulator_->gb.model))
        return;

    sgbBorderAttempted_ = true;
    if (romData_.size() <= 0x146 || romData_.size() > CUPID_GB_SGB_BORROW_MAX_ROM_SIZE ||
        uint8_t(romData_.at(0x146)) != 3)
        return;
    const QString folder = bootDirectory();
    if (folder.isEmpty())
        return;
    QFile boot(QDir(folder).filePath("sgb_boot.bin"));
    if (!boot.open(QIODevice::ReadOnly) || boot.size() != 0x100)
        return;
    const QByteArray firmware = boot.read(0x100);
    if (firmware.size() != 0x100)
        return;

    using BorderResult = std::shared_ptr<const CupidGbSgbBorder>;
    auto promise = std::make_shared<QPromise<BorderResult>>();
    auto *watcher = new QFutureWatcher<BorderResult>(this);
    const uint64_t cartridge = sgbBorderCartridge_;
    sgbBorderJobPending_ = true;
    connect(watcher, &QFutureWatcher<BorderResult>::finished, this, [this, watcher, cartridge] {
        sgbBorderJobPending_ = false;
        const auto border = watcher->result();
        watcher->deleteLater();
        // A newer cartridge may be waiting while the old donor clears its execution budget.
        if (cartridge != sgbBorderCartridge_) {
            borrowSgbBorder();
            return;
        }
        if (!border)
            return;
        borrowedSgbBorder_ = border;
        sgbBorderImage_ = {};
        if (emulator_ && sgbBorderMode_ == SgbBorderMode::Always &&
            !cupid_gb_model_is_sgb(emulator_->gb.model))
            present(Presentation::Reset);
    });
    promise->start();
    watcher->setFuture(promise->future());
    // Owning byte arrays cross the worker boundary. The live core and window stay on the UI thread.
    QThreadPool::globalInstance()->start([promise, rom = romData_, firmware] {
        auto border = std::make_shared<CupidGbSgbBorder>();
        const bool captured = cupid_gb_sgb_borrow_border(
            reinterpret_cast<const uint8_t *>(rom.constData()), size_t(rom.size()),
            reinterpret_cast<const uint8_t *>(firmware.constData()), size_t(firmware.size()),
            border.get());
        promise->addResult(captured ? BorderResult(std::move(border)) : BorderResult{});
        promise->finish();
    });
}

QImage MainWindow::handheldBorder(const CupidGb &gb) {
    if (sgbBorderImage_.isNull() || sgbBorderImageModel_ != gb.model) {
        sgbBorderImage_ =
            QImage(CUPID_SGB_SCREEN_WIDTH, CUPID_SGB_SCREEN_HEIGHT, QImage::Format_RGB32);
        sgbBorderImageModel_ = gb.model;
        if (cupid_gb_model_is_sgb_external(gb.model)) {
            // The handheld supplies no surround on this connector.
            sgbBorderImage_.fill(Qt::black);
        } else if (borrowedSgbBorder_) {
            sgbBorderImage_.fill(Qt::black);
            auto *pixels = reinterpret_cast<uint32_t *>(sgbBorderImage_.bits());
            cupid_gb_sgb_render_borrowed_border(borrowedSgbBorder_.get(), pixels,
                                                CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT,
                                                sgbColorTable_->data());
        } else {
            const auto checksum =
                romData_.size() > 0x14e ? uint8_t(romData_.at(0x14e)) : uint8_t(0);
            drawHandheldBezel(sgbBorderImage_, bezelColors(gb.model, checksum),
                              colorSettings_.temperature);
        }
    }
    // The caller writes the 160x144 viewport, detaching from the cached surround.
    return sgbBorderImage_;
}

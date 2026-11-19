/**
 * @file    printer_window.cpp
 * @brief   Qt printer paper display, image export, and system printing.
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

#include "printer_window.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <utility>

extern "C" {
#include "cupid/gb/printer.h"
}

PrinterWindow::PrinterWindow(QWidget *parent, ModalAction runWhilePaused)
    : QDialog(parent), runWhilePaused_(std::move(runWhilePaused)), image_(new QLabel),
      status_(new QLabel), scroll_(new QScrollArea(this)),
      save_(new QPushButton("Save PNG…", this)), print_(new QPushButton("Print…", this)) {
    setWindowTitle("CupidGB Printer Feed");
    setAttribute(Qt::WA_ShowWithoutActivating);
    resize(366, 420);
    scroll_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    scroll_->setWidget(image_);
    auto *controls = new QHBoxLayout;
    controls->addWidget(status_, 1);
    controls->addWidget(save_);
    controls->addWidget(print_);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(scroll_);
    layout->addLayout(controls);
    save_->setEnabled(false);
    print_->setEnabled(false);
    connect(save_, &QPushButton::clicked, this, [this] { runWhilePaused_([this] { save(); }); });
    connect(print_, &QPushButton::clicked, this, [this] { runWhilePaused_([this] { print(); }); });
    connect(scroll_->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this](int, int maximum) { scroll_->verticalScrollBar()->setValue(maximum); });
}

bool PrinterWindow::append(const uint8_t *pixels, int height, int top, int bottom) {
    if (height < 0 || top < 0 || bottom < 0 || (height && !pixels)) {
        feedError_ = "The printer image is invalid.";
        return false;
    }
    const qint64 rows = qint64(top) + height + bottom;
    if (!rows)
        return false;
    const int width = CUPID_GB_PRINTER_WIDTH;
    const int offset = isVisible() ? feed_.height() : 0;
    if (rows > QWIDGETSIZE_MAX / 2 - offset) {
        feedError_ = "Printer feed exceeds the display's maximum height.";
        return false;
    }
    QImage paper(width, offset + int(rows), QImage::Format_Grayscale8);
    if (paper.isNull()) {
        feedError_ = "Printer feed is too large.";
        return false;
    }
    paper.fill(255);
    if (offset)
        std::memcpy(paper.bits(), feed_.constBits(), size_t(offset) * width);
    if (height)
        std::memcpy(paper.scanLine(offset + top), pixels, size_t(height) * width);
    feed_ = std::move(paper);
    feedError_.clear();
    feedDirty_ = true;
    printing_ = true;
    return true;
}

void PrinterWindow::setPrinting(bool printing) {
    printing_ = printing;
}

void PrinterWindow::refresh(bool reveal) {
    if (feedDirty_) {
        const QPixmap preview = QPixmap::fromImage(feed_.scaled(feed_.size() * 2));
        image_->setPixmap(preview);
        image_->resize(preview.size());
        feedDirty_ = false;
    }
    save_->setEnabled(!feed_.isNull());
    print_->setEnabled(!feed_.isNull());
    status_->setText(feedError_.isEmpty() ? (printing_ ? "Printing…" : QString()) : feedError_);
    if (reveal && !feed_.isNull())
        show();
}

bool PrinterWindow::saveFeed(const QString &path) const {
    QSaveFile file(path);
    return !feed_.isNull() && file.open(QIODevice::WriteOnly) && feed_.save(&file, "PNG") &&
           file.commit();
}

void PrinterWindow::save() {
    const QString path = QFileDialog::getSaveFileName(this, "Save Printer Feed",
                                                      "CupidGB-print.png", "PNG image (*.png)");
    if (path.isEmpty())
        return;
    if (saveFeed(path))
        hide();
    else
        QMessageBox::warning(this, "CupidGB Printer Feed", "Could not save the printer feed.");
}

void PrinterWindow::print() {
    QPrinter printer;
    QPrintDialog dialog(&printer, this);
    if (feed_.isNull() || dialog.exec() != QDialog::Accepted)
        return;
    if (!printFeed(printer, printer.resolution()))
        QMessageBox::warning(this, "CupidGB Printer Feed", "Could not complete printing.");
}

bool PrinterWindow::printFeed(QPagedPaintDevice &printer, int resolution) const {
    if (feed_.isNull())
        return false;
    QPainter painter;
    const QRect page = printer.pageLayout().paintRectPixels(resolution);
    const int width = std::min(page.width(), feed_.width() * 2 * resolution / 72);
    if (width <= 0 || page.height() <= 0 || !painter.begin(&printer))
        return false;
    const int rowsPerPage = std::max(1, page.height() * feed_.width() / width);
    bool complete = true;
    for (int row = 0; row < feed_.height(); row += rowsPerPage) {
        if (row && !printer.newPage()) {
            complete = false;
            break;
        }
        const int rows = std::min(rowsPerPage, feed_.height() - row);
        painter.drawImage(QRect(0, 0, width, rows * width / feed_.width()), feed_,
                          QRect(0, row, feed_.width(), rows));
    }
    return painter.end() && complete;
}

/**
 * @file    printer_window.h
 * @brief   Qt printer paper storage, export, and print-dialog interface.
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

#include <QDialog>
#include <QImage>
#include <QString>
#include <cstdint>
#include <functional>

class QLabel;
class QPagedPaintDevice;
class QPushButton;
class QScrollArea;

class PrinterWindow final : public QDialog {
  public:
    using ModalAction = std::function<void(const std::function<void()> &)>;

    PrinterWindow(QWidget *parent, ModalAction runWhilePaused);
    // Commit host paper without presenting widgets or dispatching guest work.
    bool append(const uint8_t *pixels, int height, int top, int bottom);
    void setPrinting(bool printing);
    void refresh(bool reveal);
    bool saveFeed(const QString &path) const;
    bool printFeed(QPagedPaintDevice &printer, int resolution) const;

  private:
    void save();
    void print();

    ModalAction runWhilePaused_;
    QImage feed_;
    QLabel *image_;
    QLabel *status_;
    QScrollArea *scroll_;
    QPushButton *save_;
    QPushButton *print_;
    QString feedError_;
    bool feedDirty_ = false;
    bool printing_ = false;
};

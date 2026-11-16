/**
 * @file    screen_messages.h
 * @brief   On-screen message lifetime and image-composition interface.
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

#include <QByteArrayView>
#include <QImage>
#include <QString>

QImage composeScreenMessage(const QImage &frame, const QImage &message);

// Messages follow guest presentation events. Host repaints never consume their
// lifetime, and the overlay is separate from screenshots and emulator state.
class ScreenMessages {
  public:
    void setEnabled(bool enabled);
    bool enabled() const;
    void show(const QString &message);
    void showCartridge(const QString &version, QByteArrayView rom);
    void advanceFrame(const QString &heldMode = {});
    void clear();
    QString text() const;
    unsigned remainingFrames() const;
    const QImage &image(const QSize &size) const;

  private:
    bool enabled_ = false;
    QString text_;
    unsigned remaining_ = 0;
    mutable QImage image_;
};

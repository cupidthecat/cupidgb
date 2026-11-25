/**
 * @file    palette_editor.h
 * @brief   Qt palette-theme editor and change-notification interface.
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

#include "palette_library.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class PaletteEditor final : public QDialog {
  public:
    using Changed = std::function<void(const PaletteLibrary &)>;
    PaletteEditor(PaletteLibrary library, QVector<PaletteTheme> defaults, Changed changed,
                  QWidget *parent = nullptr);
    const PaletteLibrary &library() const;

  private:
    void refresh(bool refreshList = false);
    void changed();
    void chooseColor(size_t shade);
    PaletteLibrary library_;
    QVector<PaletteTheme> defaults_;
    Changed changed_;
    bool refreshing_ = false;
    QListWidget *list_;
    QLineEdit *name_;
    QCheckBox *manual_;
    QCheckBox *separateOff_;
    std::array<QPushButton *, 5> colors_{};
    std::array<QDoubleSpinBox *, 3> controls_{};
    QLabel *preview_;
};

/**
 * @file    cheats_window.h
 * @brief   Qt cheat-list and memory-search window interface.
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

#include "cheat_search.h"

#include <QDialog>
#include <functional>

extern "C" {
#include "cupid/gb/cheats.h"
}

class QAbstractItemModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QTableWidget;
class QTabWidget;

class CheatsWindow final : public QDialog {
  public:
    using Getter = std::function<const CupidGbCheats *()>;
    using StateGetter = std::function<const CupidGb *()>;
    using Mutator = std::function<bool(const std::function<bool(CupidGbCheats *)> &)>;
    using Writer = std::function<bool(const std::function<bool(CupidGb &)> &)>;

    explicit CheatsWindow(QWidget *parent = nullptr);
    void setSession(Getter getter, StateGetter state, Mutator mutate, Writer write);
    void sessionChanged();
    void refresh();
    void selectSearch(bool search);
    void setSaveWarning(const QString &warning);
    std::function<void()> importFileRequested;
    std::function<void()> exportFileRequested;

  private:
    void editCheat(bool existing);
    void importCode();
    void removeCheat();
    void filterSearch();
    void resetSearch();
    void addSearchCheat();
    void writeSearchValue();

    Getter getter_;
    StateGetter state_;
    Mutator mutate_;
    Writer write_;
    CheatSearch search_;
    QTabWidget *tabs_;
    QCheckBox *enabled_;
    QTableWidget *cheats_;
    QLabel *status_;
    QLabel *saveWarning_;
    uint64_t sessionGeneration_ = 0;
    QPushButton *edit_;
    QPushButton *remove_;
    QComboBox *type_;
    QComboBox *condition_;
    QLineEdit *operand_;
    QLabel *resultCount_;
    QTableView *results_;
    QAbstractItemModel *resultModel_;
    QPushButton *addResult_;
    QPushButton *writeResult_;
};

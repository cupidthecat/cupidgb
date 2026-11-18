/**
 * @file    inspectors_window.h
 * @brief   Qt mapped/physical memory and graphics-inspector window interface.
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

#include "inspector_renderer.h"

#include <QMainWindow>
#include <QTimer>
#include <functional>

extern "C" {
#include "cupid/gb/gb.h"
}

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTableWidget;
class InspectorImageView;

class InspectorsWindow final : public QMainWindow {
  public:
    using Getter = std::function<CupidGb *()>;
    // The writer pauses emulation and invalidates debugger history before editing.
    using Writer = std::function<bool(uint16_t, uint8_t)>;

    using PhysicalWriter = std::function<bool(InspectorMemoryRegion, size_t, uint16_t, uint8_t)>;

    explicit InspectorsWindow(QWidget *parent = nullptr);
    void setSession(Getter getter, Writer writer, PhysicalWriter physicalWriter = {});
    void setColorTable(const ColorTable *colors);
    // Cancel uncommitted edits when the host replaces or restores hardware state.
    void sessionChanged();
    void refresh();
    void selectTab(int tab);
    bool writeByte(uint16_t address, uint8_t value);

  private:
    CupidGb *state() const;
    void refreshMemory(const CupidGb &gb);
    void changeMemoryRegion();
    void changeMemoryBank();
    bool writeSelectedByte(uint16_t address, uint8_t value);
    InspectorMemoryRegion memoryRegion() const;
    uint16_t memoryBase() const;
    unsigned memorySize() const;
    void refreshTiles(const CupidGb &gb);
    void refreshMap(const CupidGb &gb);
    void refreshObjects(const CupidGb &gb);
    void refreshPalettes(const CupidGb &gb);
    void jumpToAddress();
    void writeFromFields();
    void memoryEdited(int row, int column);
    void clearViews();
    void showObject();

    Getter getter_;
    Writer writer_;
    PhysicalWriter physicalWriter_;
    const ColorTable *colorTable_ = nullptr;
    QTimer timer_;
    QTabWidget *tabs_ = nullptr;
    QLabel *context_ = nullptr;
    QLabel *status_ = nullptr;
    QCheckBox *live_ = nullptr;
    QCheckBox *grid_ = nullptr;
    QCheckBox *scrolling_ = nullptr;
    QComboBox *memoryRegion_ = nullptr;
    QComboBox *memoryBank_ = nullptr;
    QLineEdit *address_ = nullptr;
    QLineEdit *editAddress_ = nullptr;
    QLineEdit *editValue_ = nullptr;
    QTableWidget *memory_ = nullptr;
    QComboBox *tileBank_ = nullptr;
    QComboBox *tilePalette_ = nullptr;
    QComboBox *mapKind_ = nullptr;
    QComboBox *mapAddressing_ = nullptr;
    QComboBox *mapPalette_ = nullptr;
    InspectorImageView *tilesImage_ = nullptr;
    InspectorImageView *mapImage_ = nullptr;
    InspectorImageView *objectImage_ = nullptr;
    QLabel *mapInfo_ = nullptr;
    QLabel *objectInfo_ = nullptr;
    QTableWidget *objects_ = nullptr;
    QTableWidget *palettes_ = nullptr;
    uint16_t page_ = 0;
    bool refreshing_ = false;
};

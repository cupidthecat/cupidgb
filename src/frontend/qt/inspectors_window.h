#pragma once

#include "color_pipeline.h"

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

    explicit InspectorsWindow(QWidget *parent = nullptr);
    void setSession(Getter getter, Writer writer);
    void setColorTable(const ColorTable *colors);
    // Cancel uncommitted edits when the host replaces or restores hardware state.
    void sessionChanged();
    void refresh();
    void selectTab(int tab);
    bool writeByte(uint16_t address, uint8_t value);

  private:
    CupidGb *state() const;
    void refreshMemory(const CupidGb &gb);
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
    const ColorTable *colorTable_ = nullptr;
    QTimer timer_;
    QTabWidget *tabs_ = nullptr;
    QLabel *context_ = nullptr;
    QLabel *status_ = nullptr;
    QCheckBox *live_ = nullptr;
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

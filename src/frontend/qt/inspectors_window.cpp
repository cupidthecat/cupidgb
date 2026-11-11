#include "inspectors_window.h"

#include "inspector_renderer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

extern "C" {
#include "cupid/gb/cartridge.h"
#include "cupid/gb/debugger.h"
#include "cupid/gbc/cgb.h"
}

namespace {
class InspectorMemoryTable final : public QTableWidget {
  public:
    InspectorMemoryTable() : QTableWidget(16, 16) {
    }

    bool editing() const {
        return state() == QAbstractItemView::EditingState;
    }
};

QString hex(unsigned value, int width) {
    return QString::number(value, 16).rightJustified(width, QLatin1Char('0')).toUpper();
}

bool parseHex(QString text, unsigned maximum, unsigned &value) {
    text = text.trimmed();
    if (text.startsWith('$'))
        text.remove(0, 1);
    else if (text.startsWith("0x", Qt::CaseInsensitive))
        text.remove(0, 2);
    if (text.isEmpty() || text.size() > 4)
        return false;
    for (const QChar letter : text)
        if (!((letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'f') ||
              (letter >= 'A' && letter <= 'F')))
            return false;
    bool valid = false;
    value = text.toUInt(&valid, 16);
    return valid && value <= maximum;
}

InspectorColors paletteSelection(const QComboBox *box) {
    const int selected = box->currentData().toInt();
    if (selected == -2)
        return {InspectorPalette::Automatic, 0};
    if (selected < 0)
        return {};
    return {selected < 8 ? InspectorPalette::Background : InspectorPalette::Object,
            unsigned(selected) & 7u};
}

void addPaletteChoices(QComboBox *box, bool automatic) {
    if (automatic)
        box->addItem("Automatic", -2);
    box->addItem("Raw grayscale", -1);
    for (int index = 0; index < 8; ++index)
        box->addItem(QString("Background %1").arg(index), index);
    for (int index = 0; index < 8; ++index)
        box->addItem(QString("Object %1").arg(index), index + 8);
}

void setupTable(QTableWidget *table) {
    table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    table->setAlternatingRowColors(true);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setDefaultSectionSize(24);
}
} // namespace

class InspectorImageView final : public QWidget {
  public:
    explicit InspectorImageView(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(128, 144);
        setMouseTracking(true);
    }

    QImage image;
    std::function<void(QPoint)> pointChanged;

    QSize sizeHint() const override {
        return image.isNull() ? QSize(256, 256) : image.size() * 2;
    }

  protected:
    QRect target() const {
        if (image.isNull())
            return {};
        const int factor =
            std::max(1, std::min(width() / image.width(), height() / image.height()));
        const QSize size = image.size() * factor;
        return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Dark));
        if (!image.isNull())
            painter.drawImage(target(), image);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        const QRect area = target();
        const QPoint position = event->position().toPoint();
        if (pointChanged && area.contains(position))
            pointChanged({(position.x() - area.x()) * image.width() / area.width(),
                          (position.y() - area.y()) * image.height() / area.height()});
    }
};

InspectorsWindow::InspectorsWindow(QWidget *parent) : QMainWindow(parent, Qt::Window) {
    setWindowTitle("CupidGB Hardware Inspectors");
    setObjectName("hardwareInspectors");
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    auto *controls = new QHBoxLayout;
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("inspectorRefresh");
    live_ = new QCheckBox("Live refresh (4 Hz)");
    live_->setObjectName("inspectorLive");
    live_->setChecked(true);
    controls->addWidget(refreshButton);
    controls->addWidget(live_);
    controls->addStretch();
    layout->addLayout(controls);
    context_ = new QLabel;
    context_->setObjectName("inspectorContext");
    context_->setWordWrap(true);
    layout->addWidget(context_);
    tabs_ = new QTabWidget;
    tabs_->setObjectName("inspectorTabs");
    layout->addWidget(tabs_);
    status_ = new QLabel("Open a cartridge to inspect its hardware state.");
    status_->setObjectName("inspectorStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    setCentralWidget(container);

    auto *memoryPage = new QWidget;
    auto *memoryLayout = new QVBoxLayout(memoryPage);
    auto *navigation = new QHBoxLayout;
    address_ = new QLineEdit("0000");
    address_->setObjectName("inspectorAddress");
    address_->setMaximumWidth(100);
    auto *go = new QPushButton("Go");
    go->setObjectName("inspectorGo");
    auto *previous = new QPushButton("Previous page");
    auto *next = new QPushButton("Next page");
    navigation->addWidget(new QLabel("Address (hex)"));
    navigation->addWidget(address_);
    navigation->addWidget(go);
    navigation->addWidget(previous);
    navigation->addWidget(next);
    navigation->addStretch();
    memoryLayout->addLayout(navigation);
    memory_ = new InspectorMemoryTable;
    memory_->setObjectName("inspectorMemory");
    setupTable(memory_);
    QStringList columns;
    for (unsigned column = 0; column < 16; ++column)
        columns << hex(column, 1);
    memory_->setHorizontalHeaderLabels(columns);
    memory_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    memoryLayout->addWidget(memory_);
    auto *edit = new QHBoxLayout;
    editAddress_ = new QLineEdit("C000");
    editAddress_->setObjectName("inspectorWriteAddress");
    editValue_ = new QLineEdit("00");
    editValue_->setObjectName("inspectorWriteValue");
    auto *write = new QPushButton("Write byte");
    write->setObjectName("inspectorWrite");
    edit->addWidget(new QLabel("Address (hex)"));
    edit->addWidget(editAddress_);
    edit->addWidget(new QLabel("Value (hex)"));
    edit->addWidget(editValue_);
    edit->addWidget(write);
    memoryLayout->addLayout(edit);
    memoryLayout->addWidget(
        new QLabel("ROM and unusable memory are read-only. I/O writes can change hardware state."));
    tabs_->addTab(memoryPage, "Memory");

    auto *tilesPage = new QWidget;
    auto *tilesLayout = new QVBoxLayout(tilesPage);
    auto *tilesControls = new QHBoxLayout;
    tileBank_ = new QComboBox;
    tileBank_->setObjectName("inspectorTileBank");
    tileBank_->addItem("Both VRAM banks", -1);
    tileBank_->addItem("VRAM bank 0", 0);
    tileBank_->addItem("VRAM bank 1", 1);
    tilePalette_ = new QComboBox;
    tilePalette_->setObjectName("inspectorTilePalette");
    addPaletteChoices(tilePalette_, false);
    tilesControls->addWidget(tileBank_);
    tilesControls->addWidget(tilePalette_);
    tilesControls->addStretch();
    tilesLayout->addLayout(tilesControls);
    tilesImage_ = new InspectorImageView;
    tilesImage_->setObjectName("inspectorTileImage");
    tilesLayout->addWidget(tilesImage_, 1);
    auto *tileInfo = new QLabel("Each bank contains 384 tiles from $8000 through $97FF.");
    tileInfo->setObjectName("inspectorTileInfo");
    tilesLayout->addWidget(tileInfo);
    tilesImage_->pointChanged = [this, tileInfo](QPoint position) {
        const int selection = tileBank_->currentData().toInt();
        const unsigned bank = selection < 0 ? unsigned(position.x() / 128) : unsigned(selection);
        const unsigned tile = unsigned(position.y() / 8) * 16u + unsigned(position.x() % 128) / 8u;
        tileInfo->setText(QString("Bank %1  Tile $%2  Address $%3")
                              .arg(bank)
                              .arg(hex(tile, 3))
                              .arg(hex(0x8000u + tile * 16u, 4)));
    };
    tabs_->addTab(tilesPage, "Tiles");

    auto *mapPage = new QWidget;
    auto *mapLayout = new QVBoxLayout(mapPage);
    auto *mapControls = new QHBoxLayout;
    mapKind_ = new QComboBox;
    mapKind_->setObjectName("inspectorMapKind");
    mapKind_->addItems({"Background map (LCDC)", "Window map (LCDC)", "$9800 map", "$9C00 map"});
    mapAddressing_ = new QComboBox;
    mapAddressing_->setObjectName("inspectorMapAddressing");
    mapAddressing_->addItems({"Tile addressing (LCDC)", "Unsigned $8000", "Signed $8800/$9000"});
    mapPalette_ = new QComboBox;
    mapPalette_->setObjectName("inspectorMapPalette");
    addPaletteChoices(mapPalette_, true);
    mapControls->addWidget(mapKind_);
    mapControls->addWidget(mapAddressing_);
    mapControls->addWidget(mapPalette_);
    mapLayout->addLayout(mapControls);
    mapImage_ = new InspectorImageView;
    mapImage_->setObjectName("inspectorMapImage");
    mapLayout->addWidget(mapImage_, 1);
    mapInfo_ = new QLabel;
    mapInfo_->setObjectName("inspectorMapInfo");
    mapInfo_->setWordWrap(true);
    mapLayout->addWidget(mapInfo_);
    mapImage_->pointChanged = [this](QPoint point) {
        if (const auto *gb = state())
            mapInfo_->setText(inspectorMapCell(*gb, InspectorMap(mapKind_->currentIndex()),
                                               point.x(), point.y()));
    };
    tabs_->addTab(mapPage, "Maps");

    auto *objectPage = new QWidget;
    auto *objectLayout = new QHBoxLayout(objectPage);
    objects_ = new QTableWidget(40, 9);
    objects_->setObjectName("inspectorObjects");
    setupTable(objects_);
    objects_->setSelectionBehavior(QAbstractItemView::SelectRows);
    objects_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    objects_->setHorizontalHeaderLabels(
        {"OAM", "X", "Y", "Tile", "Bank", "Palette", "Flags", "Lines", "Limit"});
    objects_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    objectLayout->addWidget(objects_, 3);
    auto *preview = new QVBoxLayout;
    objectImage_ = new InspectorImageView;
    objectImage_->setObjectName("inspectorObjectImage");
    preview->addWidget(objectImage_, 1);
    objectInfo_ = new QLabel;
    objectInfo_->setObjectName("inspectorObjectInfo");
    objectInfo_->setWordWrap(true);
    preview->addWidget(objectInfo_);
    objectLayout->addLayout(preview, 1);
    tabs_->addTab(objectPage, "OAM");

    palettes_ = new QTableWidget(16, 5);
    palettes_->setObjectName("inspectorPalettes");
    setupTable(palettes_);
    palettes_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    palettes_->setHorizontalHeaderLabels({"Palette", "Color 0", "Color 1", "Color 2", "Color 3"});
    palettes_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tabs_->addTab(palettes_, "Palettes");

    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    connect(live_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled)
            timer_.start();
        else
            timer_.stop();
    });
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (isVisible())
            refresh();
    });
    timer_.start();
    connect(go, &QPushButton::clicked, this, [this] { jumpToAddress(); });
    connect(address_, &QLineEdit::returnPressed, this, [this] { jumpToAddress(); });
    connect(previous, &QPushButton::clicked, this, [this] {
        page_ = uint16_t(page_ - 0x100u);
        address_->setText(hex(page_, 4));
        refresh();
    });
    connect(next, &QPushButton::clicked, this, [this] {
        page_ = uint16_t(page_ + 0x100u);
        address_->setText(hex(page_, 4));
        refresh();
    });
    connect(write, &QPushButton::clicked, this, [this] { writeFromFields(); });
    connect(memory_, &QTableWidget::cellChanged, this,
            [this](int row, int column) { memoryEdited(row, column); });
    connect(memory_, &QTableWidget::currentCellChanged, this, [this](int row, int column) {
        if (row >= 0 && column >= 0)
            editAddress_->setText(hex(unsigned(page_) + unsigned(row * 16 + column), 4));
    });
    for (auto *box : {tileBank_, tilePalette_, mapKind_, mapAddressing_, mapPalette_})
        connect(box, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { refresh(); });
    connect(objects_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (!refreshing_)
            showObject();
    });
    resize(960, 680);
    clearViews();
}

void InspectorsWindow::setSession(Getter getter, Writer writer) {
    clearViews();
    getter_ = std::move(getter);
    writer_ = std::move(writer);
    refresh();
}

void InspectorsWindow::sessionChanged() {
    clearViews();
    refresh();
}

CupidGb *InspectorsWindow::state() const {
    auto *gb = getter_ ? getter_() : nullptr;
    return gb && gb->loaded ? gb : nullptr;
}

void InspectorsWindow::selectTab(int tab) {
    if (tab >= 0 && tab < tabs_->count())
        tabs_->setCurrentIndex(tab);
    refresh();
}

void InspectorsWindow::refresh() {
    QScopedValueRollback<bool> update(refreshing_, true);
    const auto *gb = state();
    if (!gb) {
        clearViews();
        return;
    }
    tabs_->setEnabled(true);
    if (status_->text() == "Open a cartridge to inspect its hardware state.")
        status_->setText(
            "Double-click a writable memory cell to edit it. Editing pauses emulation.");
    context_->setText(QString("ROM0 bank %1 | ROMX bank %2 | VRAM bank %3 | WRAM bank %4 | RAM "
                              "bank %5 | LY %6 | LCD mode %7")
                          .arg(cupid_gb_effective_rom_bank(gb, true))
                          .arg(cupid_gb_effective_rom_bank(gb, false))
                          .arg(cupid_cgb_vram_offset(gb, 0x8000) / 0x2000u)
                          .arg(cupid_cgb_wram_offset(gb, 0xd000) / 0x1000u)
                          .arg(cupid_gb_effective_ram_bank(gb))
                          .arg(gb->io_registers[0x44])
                          .arg(gb->io_registers[0x41] & 3u));
    switch (tabs_->currentIndex()) {
        case 0:
            refreshMemory(*gb);
            break;
        case 1:
            refreshTiles(*gb);
            break;
        case 2:
            refreshMap(*gb);
            break;
        case 3:
            refreshObjects(*gb);
            break;
        case 4:
            refreshPalettes(*gb);
            break;
    }
}

void InspectorsWindow::clearViews() {
    QScopedValueRollback<bool> update(refreshing_, true);
    tabs_->setEnabled(false);
    context_->setText("No cartridge loaded");
    status_->setText("Open a cartridge to inspect its hardware state.");
    memory_->clearContents();
    objects_->clearContents();
    palettes_->clearContents();
    for (auto *view : {tilesImage_, mapImage_, objectImage_}) {
        view->image = {};
        view->update();
    }
    mapInfo_->clear();
    objectInfo_->clear();
}

void InspectorsWindow::refreshMemory(const CupidGb &gb) {
    // Replacing table items also destroys an active delegate editor. A live
    // observation must leave an unfinished byte edit intact until it commits.
    if (static_cast<const InspectorMemoryTable *>(memory_)->editing())
        return;
    QStringList rows;
    for (unsigned row = 0; row < 16; ++row) {
        rows << hex(unsigned(page_) + row * 16u, 4);
        for (unsigned column = 0; column < 16; ++column) {
            const unsigned address = unsigned(page_) + row * 16u + column;
            auto *item = new QTableWidgetItem(hex(cupid_gb_debug_peek(&gb, uint16_t(address)), 2));
            const bool writable = address >= 0x8000u && !(address >= 0xfea0u && address < 0xff00u);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                           (writable ? Qt::ItemIsEditable : Qt::NoItemFlags));
            memory_->setItem(int(row), int(column), item);
        }
    }
    memory_->setVerticalHeaderLabels(rows);
}

void InspectorsWindow::setColorTable(const ColorTable *colors) {
    colorTable_ = colors;
    refresh();
}

void InspectorsWindow::refreshTiles(const CupidGb &gb) {
    tilesImage_->image = inspectorTiles(gb, tileBank_->currentData().toInt(),
                                        paletteSelection(tilePalette_), colorTable_);
    tilesImage_->update();
}

void InspectorsWindow::refreshMap(const CupidGb &gb) {
    mapImage_->image = inspectorMap(gb, InspectorMap(mapKind_->currentIndex()),
                                    InspectorTileAddressing(mapAddressing_->currentIndex()),
                                    paletteSelection(mapPalette_), colorTable_);
    mapImage_->update();
    mapInfo_->setText(QString("SCX %1  SCY %2  WX %3  WY %4. Hover for tile attributes.")
                          .arg(gb.io_registers[0x43])
                          .arg(gb.io_registers[0x42])
                          .arg(gb.io_registers[0x4b])
                          .arg(gb.io_registers[0x4a]));
}

void InspectorsWindow::refreshObjects(const CupidGb &gb) {
    for (unsigned index = 0; index < 40; ++index) {
        const auto object = inspectorObject(gb, index, colorTable_);
        const QStringList values = {
            "$" + hex(object.address, 4),        QString::number(object.x),
            QString::number(object.y),           "$" + hex(object.tile, 2),
            QString::number(object.bank),        QString::number(object.palette),
            "$" + hex(object.flags, 2),          object.visibleLines ? "Visible" : "Offscreen",
            object.lineLimited ? "Limited" : "-"};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values[column]);
            item->setToolTip(QString("Raw X %1, raw Y %2; %3-pixel object")
                                 .arg(object.rawX)
                                 .arg(object.rawY)
                                 .arg(object.height));
            objects_->setItem(int(index), column, item);
        }
    }
    if (objects_->currentRow() < 0)
        objects_->selectRow(0);
    showObject();
}

void InspectorsWindow::showObject() {
    const auto *gb = state();
    if (!gb)
        return;
    const auto object =
        inspectorObject(*gb, unsigned(std::max(0, objects_->currentRow())), colorTable_);
    objectImage_->image = object.image;
    objectImage_->update();
    objectInfo_->setText(QString("OAM %1 ($%2)\nX %3, Y %4\nBank %5, palette %6\nX flip %7, Y flip "
                                 "%8\nBehind background %9")
                             .arg(object.index)
                             .arg(hex(object.address, 4))
                             .arg(object.x)
                             .arg(object.y)
                             .arg(object.bank)
                             .arg(object.palette)
                             .arg(bool(object.flags & 0x20u))
                             .arg(bool(object.flags & 0x40u))
                             .arg(bool(object.flags & 0x80u)));
}

void InspectorsWindow::refreshPalettes(const CupidGb &gb) {
    const bool cgb = gb.model == CUPID_GB_MODEL_CGB;
    palettes_->setRowCount(cgb ? 16 : 3);
    for (unsigned row = 0; row < unsigned(palettes_->rowCount()); ++row) {
        const QString name =
            cgb ? QString("%1 %2").arg(row < 8 ? "Background" : "Object").arg(row & 7u)
                : QStringList{"BGP ($FF47)", "OBP0 ($FF48)", "OBP1 ($FF49)"}[int(row)];
        palettes_->setItem(int(row), 0, new QTableWidgetItem(name));
        const auto values = inspectorPaletteValues(gb, row >= 8, row & 7u);
        for (unsigned entry = 0; entry < 4; ++entry) {
            const unsigned shade = (gb.io_registers[0x47u + row] >> (entry * 2u)) & 3u;
            const QRgb color =
                cgb ? inspectorRgb555(values[entry], colorTable_)
                    : qRgb(255 - int(shade) * 85, 255 - int(shade) * 85, 255 - int(shade) * 85);
            auto *item =
                new QTableWidgetItem(cgb ? "$" + hex(values[entry], 4) : QString::number(shade));
            item->setBackground(QColor::fromRgb(color));
            item->setForeground(qGray(color) > 127 ? Qt::black : Qt::white);
            item->setToolTip(cgb ? QString("Red %1, green %2, blue %3 (5-bit channels)")
                                       .arg(values[entry] & 31u)
                                       .arg((values[entry] >> 5u) & 31u)
                                       .arg((values[entry] >> 10u) & 31u)
                                 : QString("Shade %1 selected by register bits %2-%3")
                                       .arg(shade)
                                       .arg(entry * 2u)
                                       .arg(entry * 2u + 1u));
            palettes_->setItem(int(row), int(entry + 1u), item);
        }
    }
}

void InspectorsWindow::jumpToAddress() {
    unsigned address = 0;
    if (!parseHex(address_->text(), 0xffffu, address)) {
        status_->setText("Enter an address from $0000 through $FFFF.");
        return;
    }
    page_ = uint16_t(address & 0xff00u);
    tabs_->setCurrentIndex(0);
    refresh();
    memory_->setCurrentCell(int((address & 0xffu) / 16u), int(address & 15u));
}

bool InspectorsWindow::writeByte(uint16_t address, uint8_t value) {
    if (!state() || !writer_ || address < 0x8000u || (address >= 0xfea0u && address < 0xff00u)) {
        status_->setText("This address is read-only or no cartridge is loaded.");
        return false;
    }
    const bool written = writer_(address, value);
    refresh();
    status_->setText(
        written
            ? QString("Wrote $%1 at $%2; emulation paused.").arg(hex(value, 2)).arg(hex(address, 4))
            : "The current cartridge or hardware window rejected this write.");
    return written;
}

void InspectorsWindow::writeFromFields() {
    unsigned address = 0, value = 0;
    if (!parseHex(editAddress_->text(), 0xffffu, address) ||
        !parseHex(editValue_->text(), 0xffu, value)) {
        status_->setText("Enter a hexadecimal address ($0000-$FFFF) and byte ($00-$FF).");
        return;
    }
    writeByte(uint16_t(address), uint8_t(value));
}

void InspectorsWindow::memoryEdited(int row, int column) {
    if (refreshing_ || !memory_->item(row, column))
        return;
    unsigned value = 0;
    if (!parseHex(memory_->item(row, column)->text(), 0xffu, value)) {
        refresh();
        status_->setText("Memory cells accept one hexadecimal byte ($00-$FF).");
        return;
    }
    writeByte(uint16_t(unsigned(page_) + unsigned(row * 16 + column)), uint8_t(value));
}

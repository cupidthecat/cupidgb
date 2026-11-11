#include "cheats_window.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cstring>

namespace {
QString hexNumber(unsigned value, int width) {
    return QString("%1").arg(value, width, 16, QChar('0')).toUpper();
}

bool readNumber(QString text, unsigned maximum, uint16_t &value) {
    text = text.trimmed();
    if (text.startsWith('$'))
        text.remove(0, 1);
    else if (text.startsWith("0x", Qt::CaseInsensitive))
        text.remove(0, 2);
    bool valid = false;
    const unsigned number = text.toUInt(&valid, 16);
    if (!valid || number > maximum)
        return false;
    value = uint16_t(number);
    return true;
}

class SearchResults final : public QAbstractTableModel {
  public:
    SearchResults(CheatSearch &search, const CheatsWindow::StateGetter &state, QObject *parent)
        : QAbstractTableModel(parent), search_(search), state_(state) {
    }

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(search_.count());
    }

    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 3;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
            return QStringList{"Address", "Bank", "Current value"}.value(section);
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || role != Qt::DisplayRole || !state_ || !state_())
            return {};
        const auto result = search_.result(size_t(index.row()));
        if (index.column() == 0)
            return hexNumber(result.address, 4);
        if (index.column() == 1)
            return hexNumber(result.bank, 2);
        return hexNumber(search_.currentValue(*state_(), size_t(index.row())),
                         search_.type() == CheatSearchType::Byte ? 2 : 4);
    }

    void resetResults(const std::function<void()> &operation) {
        beginResetModel();
        operation();
        endResetModel();
    }

    void refreshValues() {
        if (rowCount())
            emit dataChanged(index(0, 2), index(rowCount() - 1, 2));
    }

  private:
    CheatSearch &search_;
    const CheatsWindow::StateGetter &state_;
};
} // namespace

CheatsWindow::CheatsWindow(QWidget *parent) : QDialog(parent) {
    setWindowTitle("CupidGB Cheats");
    resize(700, 480);
    tabs_ = new QTabWidget(this);
    auto *collection = new QWidget(tabs_);
    auto *layout = new QVBoxLayout(collection);
    enabled_ = new QCheckBox("Enable cheats", collection);
    enabled_->setObjectName("cheatsEnabled");
    saveWarning_ = new QLabel(collection);
    saveWarning_->setWordWrap(true);
    cheats_ = new QTableWidget(0, 5, collection);
    cheats_->setObjectName("cheatsTable");
    cheats_->setHorizontalHeaderLabels({"Enabled", "Description", "Address", "Value", "Compare"});
    cheats_->setSelectionBehavior(QAbstractItemView::SelectRows);
    cheats_->setSelectionMode(QAbstractItemView::SingleSelection);
    cheats_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cheats_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton("Add…", collection);
    add->setObjectName("addCheat");
    edit_ = new QPushButton("Edit…", collection);
    edit_->setObjectName("editCheat");
    remove_ = new QPushButton("Remove", collection);
    auto *import = new QPushButton("Import Code…", collection);
    import->setObjectName("importCheatCode");
    auto *load = new QPushButton("Import File…", collection);
    auto *save = new QPushButton("Export File…", collection);
    for (auto *button : {add, edit_, remove_, import, load, save})
        buttons->addWidget(button);
    layout->addWidget(enabled_);
    layout->addWidget(saveWarning_);
    layout->addWidget(cheats_, 1);
    layout->addLayout(buttons);
    tabs_->addTab(collection, "Cheats");
    auto *searchPage = new QWidget(tabs_);
    auto *searchLayout = new QVBoxLayout(searchPage);
    auto *options = new QHBoxLayout;
    type_ = new QComboBox(searchPage);
    type_->setObjectName("cheatSearchType");
    type_->addItems({"8-bit", "16-bit little endian", "16-bit big endian"});
    condition_ = new QComboBox(searchPage);
    condition_->setObjectName("cheatSearchCondition");
    condition_->addItems({"Any value", "Equals", "Does not equal", "Greater than", "At least",
                          "Less than", "At most", "Changed", "Unchanged", "Increased",
                          "Decreased"});
    operand_ = new QLineEdit(searchPage);
    operand_->setObjectName("cheatSearchOperand");
    operand_->setPlaceholderText("Hex value");
    auto *filter = new QPushButton("Search", searchPage);
    filter->setObjectName("filterCheatSearch");
    auto *reset = new QPushButton("Reset", searchPage);
    options->addWidget(type_);
    options->addWidget(condition_);
    options->addWidget(operand_);
    options->addWidget(filter);
    options->addWidget(reset);
    results_ = new QTableView(searchPage);
    results_->setObjectName("cheatSearchResults");
    resultModel_ = new SearchResults(search_, state_, this);
    results_->setModel(resultModel_);
    results_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultCount_ = new QLabel(searchPage);
    auto *resultButtons = new QHBoxLayout;
    addResult_ = new QPushButton("Add Cheat", searchPage);
    addResult_->setObjectName("addSearchCheat");
    writeResult_ = new QPushButton("Write Value…", searchPage);
    resultButtons->addWidget(resultCount_, 1);
    resultButtons->addWidget(addResult_);
    resultButtons->addWidget(writeResult_);
    searchLayout->addLayout(options);
    searchLayout->addWidget(results_, 1);
    searchLayout->addLayout(resultButtons);
    tabs_->addTab(searchPage, "Cheat Search");
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    auto *outer = new QVBoxLayout(this);
    outer->addWidget(tabs_);
    outer->addWidget(status_);
    connect(add, &QPushButton::clicked, this, [this] { editCheat(false); });
    connect(edit_, &QPushButton::clicked, this, [this] { editCheat(true); });
    connect(remove_, &QPushButton::clicked, this, [this] { removeCheat(); });
    connect(import, &QPushButton::clicked, this, [this] { importCode(); });
    connect(load, &QPushButton::clicked, this, [this] {
        if (importFileRequested)
            importFileRequested();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        if (exportFileRequested)
            exportFileRequested();
    });
    connect(enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (mutate_)
            mutate_([enabled](CupidGbCheats *cheats) {
                cupid_gb_cheats_set_enabled(cheats, enabled);
                return true;
            });
        refresh();
    });
    connect(cheats_, &QTableWidget::itemSelectionChanged, this, [this] {
        edit_->setEnabled(cheats_->currentRow() >= 0);
        remove_->setEnabled(cheats_->currentRow() >= 0);
    });
    connect(cheats_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() || !getter_ || !getter_() || !mutate_)
            return;
        const size_t index = size_t(item->row());
        const auto *current = cupid_gb_cheats_get(getter_(), index);
        if (!current)
            return;
        CupidGbCheat edited = *current;
        edited.enabled = item->checkState() == Qt::Checked;
        mutate_([index, edited](CupidGbCheats *cheats) {
            return cupid_gb_cheats_update(cheats, index, &edited) == CUPID_GB_CHEAT_OK;
        });
        refresh();
    });
    connect(filter, &QPushButton::clicked, this, [this] { filterSearch(); });
    connect(reset, &QPushButton::clicked, this, [this] { resetSearch(); });
    connect(condition_, &QComboBox::currentIndexChanged, this,
            [this](int condition) { operand_->setEnabled(condition >= 1 && condition <= 6); });
    operand_->setEnabled(false);
    connect(addResult_, &QPushButton::clicked, this, [this] { addSearchCheat(); });
    connect(writeResult_, &QPushButton::clicked, this, [this] { writeSearchValue(); });
    connect(results_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        const bool selected = results_->currentIndex().isValid();
        addResult_->setEnabled(selected);
        writeResult_->setEnabled(selected);
    });
    auto *timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            static_cast<SearchResults *>(resultModel_)->refreshValues();
    });
    timer->start();
    refresh();
    resetSearch();
}

void CheatsWindow::setSession(Getter getter, StateGetter state, Mutator mutate, Writer write) {
    getter_ = std::move(getter);
    state_ = std::move(state);
    mutate_ = std::move(mutate);
    write_ = std::move(write);
    sessionChanged();
}

void CheatsWindow::sessionChanged() {
    ++sessionGeneration_;
    resetSearch();
    refresh();
}

void CheatsWindow::setSaveWarning(const QString &warning) {
    saveWarning_->setText(warning);
}

void CheatsWindow::selectSearch(bool search) {
    tabs_->setCurrentIndex(search ? 1 : 0);
}

void CheatsWindow::refresh() {
    const CupidGbCheats *collection = getter_ ? getter_() : nullptr;
    tabs_->setEnabled(collection != nullptr);
    const QSignalBlocker block(cheats_), enableBlock(enabled_);
    const int selected = cheats_->currentRow();
    enabled_->setChecked(cupid_gb_cheats_enabled(collection));
    cheats_->setRowCount(int(cupid_gb_cheats_count(collection)));
    for (int row = 0; row < cheats_->rowCount(); ++row) {
        const auto &cheat = *cupid_gb_cheats_get(collection, size_t(row));
        auto *enabled = new QTableWidgetItem;
        enabled->setCheckState(cheat.enabled ? Qt::Checked : Qt::Unchecked);
        cheats_->setItem(row, 0, enabled);
        cheats_->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(cheat.description)));
        const QString address =
            (cheat.bank == CUPID_GB_CHEAT_ANY_BANK ? QString() : hexNumber(cheat.bank, 1) + ":") +
            hexNumber(cheat.address, 4);
        cheats_->setItem(row, 2, new QTableWidgetItem(address));
        cheats_->setItem(row, 3, new QTableWidgetItem(hexNumber(cheat.value, 2)));
        cheats_->setItem(row, 4,
                         new QTableWidgetItem(cheat.use_compare ? hexNumber(cheat.compare_value, 2)
                                                                : QString()));
    }
    if (selected >= 0 && selected < cheats_->rowCount())
        cheats_->selectRow(selected);
    edit_->setEnabled(cheats_->currentRow() >= 0);
    remove_->setEnabled(cheats_->currentRow() >= 0);
}

void CheatsWindow::editCheat(bool existing) {
    if (!getter_ || !getter_() || !mutate_)
        return;
    const size_t index = size_t(cheats_->currentRow());
    const auto generation = sessionGeneration_;
    CupidGbCheat record = {};
    record.bank = CUPID_GB_CHEAT_ANY_BANK;
    record.enabled = true;
    if (existing) {
        const auto *selected = cupid_gb_cheats_get(getter_(), index);
        if (!selected)
            return;
        record = *selected;
    }
    QDialog editor(this);
    editor.setWindowTitle(existing ? "Edit Cheat" : "Add Cheat");
    auto *form = new QFormLayout(&editor);
    auto *name = new QLineEdit(QString::fromUtf8(record.description), &editor);
    name->setObjectName("cheatDescription");
    auto *address = new QLineEdit(
        (record.bank == CUPID_GB_CHEAT_ANY_BANK ? QString() : hexNumber(record.bank, 1) + ":") +
            hexNumber(record.address, 4),
        &editor);
    address->setObjectName("cheatAddress");
    auto *value = new QLineEdit(hexNumber(record.value, 2), &editor);
    value->setObjectName("cheatValue");
    auto *compare = new QCheckBox("Replace only when current value matches", &editor);
    compare->setChecked(record.use_compare);
    auto *original = new QLineEdit(hexNumber(record.compare_value, 2), &editor);
    original->setEnabled(record.use_compare);
    auto *error = new QLabel(&editor);
    form->addRow("Description", name);
    form->addRow("Hex address or bank:address", address);
    form->addRow("Hex value", value);
    form->addRow(compare);
    form->addRow("Original hex value", original);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &editor);
    form->addRow(buttons);
    connect(compare, &QCheckBox::toggled, original, &QLineEdit::setEnabled);
    connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
        if (generation != sessionGeneration_) {
            editor.reject();
            return;
        }
        const auto parts = address->text().split(':');
        uint16_t mapped, bank = CUPID_GB_CHEAT_ANY_BANK, byte, old = 0;
        const QByteArray description = name->text().toUtf8();
        if (parts.size() > 2 || !readNumber(parts.last(), 0xffff, mapped) ||
            (parts.size() == 2 && !readNumber(parts.first(), 0xffff, bank)) ||
            !readNumber(value->text(), 0xff, byte) ||
            (compare->isChecked() && !readNumber(original->text(), 0xff, old)) ||
            description.size() >= CUPID_GB_CHEAT_DESCRIPTION_SIZE) {
            error->setText(
                "Enter valid hexadecimal values and a description up to 127 UTF-8 bytes.");
            return;
        }
        record.address = mapped;
        record.bank = bank;
        record.value = uint8_t(byte);
        record.compare_value = uint8_t(old);
        record.use_compare = compare->isChecked();
        std::memset(record.description, 0, sizeof(record.description));
        std::memcpy(record.description, description.constData(), size_t(description.size()));
        if (mutate_([existing, index, record](CupidGbCheats *collection) {
                return (existing ? cupid_gb_cheats_update(collection, index, &record)
                                 : cupid_gb_cheats_add(collection, &record)) == CUPID_GB_CHEAT_OK;
            }))
            editor.accept();
    });
    editor.exec();
    refresh();
}

void CheatsWindow::importCode() {
    const auto generation = sessionGeneration_;
    QDialog dialog(this);
    dialog.setWindowTitle("Import Cheat Code");
    auto *form = new QFormLayout(&dialog);
    auto *code = new QLineEdit(&dialog);
    code->setObjectName("cheatCode");
    auto *description = new QLineEdit(&dialog);
    description->setObjectName("cheatCodeDescription");
    auto *error = new QLabel(&dialog);
    form->addRow("Game Genie or GameShark code", code);
    form->addRow("Description", description);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (generation != sessionGeneration_) {
            dialog.reject();
            return;
        }
        const QByteArray bytes = code->text().trimmed().toLatin1();
        const QByteArray name = description->text().toUtf8();
        CupidGbCheat record;
        if (name.size() >= CUPID_GB_CHEAT_DESCRIPTION_SIZE ||
            !cupid_gb_cheat_decode(bytes.constData(), name.constData(), true, &record)) {
            error->setText("Enter a valid Game Genie or GameShark code.");
            return;
        }
        if (mutate_ && mutate_([record](CupidGbCheats *collection) {
                return cupid_gb_cheats_add(collection, &record) == CUPID_GB_CHEAT_OK;
            }))
            dialog.accept();
    });
    dialog.exec();
    refresh();
}

void CheatsWindow::removeCheat() {
    const int row = cheats_->currentRow();
    if (row >= 0 && mutate_)
        mutate_([row](CupidGbCheats *collection) {
            return cupid_gb_cheats_remove(collection, size_t(row));
        });
    refresh();
}

void CheatsWindow::filterSearch() {
    const CupidGb *gb = state_ ? state_() : nullptr;
    uint16_t operand = 0;
    if (!gb)
        return;
    if (operand_->isEnabled() &&
        !readNumber(operand_->text(), type_->currentIndex() ? 0xffff : 0xff, operand)) {
        status_->setText("Enter a hexadecimal value within the selected width.");
        return;
    }
    static_cast<SearchResults *>(resultModel_)->resetResults([&] {
        search_.filter(*gb, CheatSearchType(type_->currentIndex()),
                       CheatSearchCondition(condition_->currentIndex()), operand);
    });
    type_->setEnabled(search_.count() == 0);
    resultCount_->setText(QString("%1 results").arg(search_.count()));
    addResult_->setEnabled(false);
    writeResult_->setEnabled(false);
    status_->clear();
}

void CheatsWindow::resetSearch() {
    static_cast<SearchResults *>(resultModel_)->resetResults([this] { search_.reset(); });
    type_->setEnabled(true);
    resultCount_->clear();
    addResult_->setEnabled(false);
    writeResult_->setEnabled(false);
    status_->clear();
}

void CheatsWindow::addSearchCheat() {
    const int row = results_->currentIndex().row();
    const CupidGb *gb = state_ ? state_() : nullptr;
    if (row < 0 || !gb || !mutate_)
        return;
    const auto found = search_.result(size_t(row));
    const auto number = search_.currentValue(*gb, size_t(row));
    const bool word = search_.type() != CheatSearchType::Byte;
    const auto second = search_.byteResult(size_t(row), word ? 1u : 0u);
    const uint16_t encoded = search_.type() == CheatSearchType::WordBigEndian
                                 ? uint16_t(number >> 8 | number << 8)
                                 : number;
    mutate_([found, second, encoded, word](CupidGbCheats *collection) {
        CupidGbCheat record = {};
        record.address = found.address;
        record.bank = found.bank;
        record.value = uint8_t(encoded);
        record.enabled = true;
        std::strcpy(record.description, word ? "New Cheat (Part 1)" : "New Cheat");
        if (cupid_gb_cheats_add(collection, &record) != CUPID_GB_CHEAT_OK)
            return false;
        if (word) {
            record.address = second.address;
            record.bank = second.bank;
            record.value = uint8_t(encoded >> 8);
            std::strcpy(record.description, "New Cheat (Part 2)");
            if (cupid_gb_cheats_add(collection, &record) != CUPID_GB_CHEAT_OK)
                return false;
        }
        cupid_gb_cheats_set_enabled(collection, true);
        return true;
    });
    tabs_->setCurrentIndex(0);
    refresh();
}

void CheatsWindow::writeSearchValue() {
    const int row = results_->currentIndex().row();
    const CupidGb *gb = state_ ? state_() : nullptr;
    if (row < 0 || !gb || !write_)
        return;
    bool accepted = false;
    const auto generation = sessionGeneration_;
    const QString text =
        QInputDialog::getText(this, "Write Value", "Hexadecimal value", QLineEdit::Normal,
                              hexNumber(search_.currentValue(*gb, size_t(row)),
                                        search_.type() == CheatSearchType::Byte ? 2 : 4),
                              &accepted);
    uint16_t number = 0;
    if (!accepted || generation != sessionGeneration_)
        return;
    if (!readNumber(text, search_.type() == CheatSearchType::Byte ? 0xff : 0xffff, number)) {
        status_->setText("Enter a hexadecimal value within the selected width.");
        return;
    }
    write_([this, row, number](CupidGb &state) {
        return search_.writeValue(state, size_t(row), number);
    });
    static_cast<SearchResults *>(resultModel_)->refreshValues();
}

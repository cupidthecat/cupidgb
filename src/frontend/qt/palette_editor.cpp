#include "palette_editor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

PaletteEditor::PaletteEditor(PaletteLibrary library, QVector<PaletteTheme> defaults,
                             Changed changedCallback, QWidget *parent)
    : QDialog(parent), library_(std::move(library)), defaults_(std::move(defaults)),
      changed_(std::move(changedCallback)) {
    setObjectName("paletteEditor");
    setWindowTitle("CupidGB Palette Library");
    auto *layout = new QHBoxLayout(this);
    auto *libraryPanel = new QVBoxLayout;
    list_ = new QListWidget;
    list_->setObjectName("palette/list");
    list_->setMinimumWidth(170);
    libraryPanel->addWidget(list_);
    auto *libraryButtons = new QHBoxLayout;
    auto *add = new QPushButton("Add");
    add->setObjectName("palette/add");
    auto *remove = new QPushButton("Delete");
    remove->setObjectName("palette/delete");
    libraryButtons->addWidget(add);
    libraryButtons->addWidget(remove);
    libraryPanel->addLayout(libraryButtons);
    auto *restore = new QPushButton("Restore Built-in Palettes");
    restore->setObjectName("palette/restore");
    libraryPanel->addWidget(restore);
    layout->addLayout(libraryPanel);
    auto *editor = new QVBoxLayout;
    auto *form = new QFormLayout;
    name_ = new QLineEdit;
    name_->setObjectName("palette/name");
    name_->setMaxLength(128);
    form->addRow("Name", name_);
    manual_ = new QCheckBox("Choose each shade manually");
    manual_->setObjectName("palette/manual");
    form->addRow(manual_);
    separateOff_ = new QCheckBox("Separate LCD-off color");
    separateOff_->setObjectName("palette/separateOff");
    form->addRow(separateOff_);
    const std::array<const char *, 5> labels = {"Lightest", "Light", "Dark", "Darkest", "LCD off"};
    for (size_t i = 0; i < colors_.size(); ++i) {
        auto *button = new QPushButton;
        colors_[i] = button;
        button->setObjectName(QString("palette/color/%1").arg(i));
        button->setAccessibleName(labels[i]);
        form->addRow(labels[i], button);
        connect(button, &QPushButton::clicked, this, [this, i] { chooseColor(i); });
    }
    const std::array<const char *, 3> controlNames = {"Brightness bias", "Hue bias",
                                                      "Hue strength"};
    for (size_t i = 0; i < controls_.size(); ++i) {
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setObjectName(QString("palette/slider/%1").arg(i));
        slider->setRange(i == 0 ? -128 : 0, i == 1 ? 360 : i == 0 ? 128 : 256);
        slider->setAccessibleName(controlNames[i]);
        auto *control = new QDoubleSpinBox;
        controls_[i] = control;
        control->setObjectName(QString("palette/parameter/%1").arg(i));
        control->setDecimals(i == 1 ? 8 : 10);
        control->setRange(i == 0 ? -1 : 0, i == 1 ? 360 : 1);
        control->setSingleStep(i == 1 ? 1 : i == 0 ? 1.0 / 128 : 1.0 / 256);
        control->setMinimumWidth(125);
        rowLayout->addWidget(slider);
        rowLayout->addWidget(control);
        form->addRow(controlNames[i], row);
        const double multiplier = i == 0 ? 128 : i == 2 ? 256 : 1;
        connect(slider, &QSlider::valueChanged, control,
                [control, multiplier](int value) { control->setValue(value / multiplier); });
        connect(control, &QDoubleSpinBox::valueChanged, this,
                [this, i, slider, multiplier](double value) {
                    const QSignalBlocker blocker(slider);
                    slider->setValue(int(std::lround(value * multiplier)));
                    if (refreshing_)
                        return;
                    auto &theme = library_.current();
                    if (i == 0)
                        theme.brightness = value;
                    else if (i == 1)
                        theme.hue = value / 360;
                    else
                        theme.strength = value;
                    resolvePaletteTheme(theme);
                    refresh();
                    changed();
                });
    }
    editor->addLayout(form);
    preview_ = new QLabel;
    preview_->setObjectName("palette/preview");
    preview_->setAccessibleName("Four shades and LCD-off color preview");
    editor->addWidget(preview_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText("Done");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    editor->addWidget(buttons);
    layout->addLayout(editor, 1);
    connect(list_, &QListWidget::currentTextChanged, this, [this](const QString &name) {
        if (!refreshing_ && library_.select(name)) {
            refresh();
            changed();
        }
    });
    connect(name_, &QLineEdit::editingFinished, this, [this] {
        if (refreshing_ || name_->text() == library_.current().name)
            return;
        library_.renameCurrent(name_->text());
        refresh(true);
        changed();
    });
    connect(manual_, &QCheckBox::toggled, this, [this](bool value) {
        if (refreshing_)
            return;
        library_.current().manual = value;
        resolvePaletteTheme(library_.current());
        refresh();
        changed();
    });
    connect(separateOff_, &QCheckBox::toggled, this, [this](bool value) {
        if (refreshing_)
            return;
        library_.current().separateOff = value;
        resolvePaletteTheme(library_.current());
        refresh();
        changed();
    });
    connect(add, &QPushButton::clicked, this, [this] {
        if (!library_.add(library_.current())) {
            QMessageBox::warning(this, "Palette Library", "The palette library is full.");
            return;
        }
        refresh(true);
        changed();
        name_->setFocus();
        name_->selectAll();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        library_.removeCurrent();
        refresh(true);
        changed();
    });
    connect(restore, &QPushButton::clicked, this, [this] {
        if (!library_.restoreDefaults(defaults_)) {
            QMessageBox::warning(this, "Palette Library",
                                 "The palette library is full. Delete unused palettes before "
                                 "restoring the built-in palettes.");
            return;
        }
        refresh(true);
        changed();
    });
    refresh(true);
    resize(650, sizeHint().height());
}

const PaletteLibrary &PaletteEditor::library() const {
    return library_;
}

void PaletteEditor::refresh(bool refreshList) {
    const QScopedValueRollback<bool> guard(refreshing_, true);
    const auto &theme = library_.current();
    if (refreshList) {
        list_->clear();
        for (const auto &entry : library_.themes())
            list_->addItem(entry.name);
        for (int row = 0; row < list_->count(); ++row)
            if (list_->item(row)->text() == theme.name)
                list_->setCurrentRow(row);
    }
    name_->setText(theme.name);
    manual_->setChecked(theme.manual);
    separateOff_->setChecked(theme.separateOff);
    QPixmap preview(300, 42);
    QPainter painter(&preview);
    for (size_t i = 0; i < colors_.size(); ++i) {
        const QColor color(i == 4 ? theme.colors.off : theme.colors.shades[i]);
        colors_[i]->setText(color.name().toUpper());
        colors_[i]->setStyleSheet(
            QString("background-color:%1; color:%2;")
                .arg(color.name(), color.lightness() < 128 ? "white" : "black"));
        colors_[i]->setEnabled(i == 4 ? !theme.manual || theme.separateOff
                                      : theme.manual || i == 3);
        painter.fillRect(int(i) * 60, 0, 60, preview.height(), color);
    }
    painter.end();
    preview_->setPixmap(preview);
    const std::array<double, 3> values = {theme.brightness, theme.hue * 360, theme.strength};
    for (size_t i = 0; i < controls_.size(); ++i) {
        controls_[i]->setValue(values[i]);
        controls_[i]->parentWidget()->setEnabled(!theme.manual);
    }
}

void PaletteEditor::changed() {
    if (changed_)
        changed_(library_);
}

void PaletteEditor::chooseColor(size_t shade) {
    auto &theme = library_.current();
    const QColor previous(shade == 4 ? theme.colors.off : theme.colors.shades[shade]);
    const QColor color = QColorDialog::getColor(previous, this, "Choose Palette Color");
    if (!color.isValid())
        return;
    if (shade == 4)
        theme.colors.off = color.rgb();
    else
        theme.colors.shades[shade] = color.rgb();
    resolvePaletteTheme(theme);
    refresh();
    changed();
}

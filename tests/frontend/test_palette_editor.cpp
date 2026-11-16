/**
 * @file    test_palette_editor.cpp
 * @brief   Qt tests for palette-theme editing and persistence workflows.
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

#include "palette_editor.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <cassert>

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    assert(argc == 1 || (argc == 3 && QByteArray(argv[1]) == "--snapshot"));
    PaletteTheme theme;
    theme.name = "Gray";
    theme.colors = {{qRgb(255, 255, 255), qRgb(170, 170, 170), qRgb(85, 85, 85), qRgb(0, 0, 0)},
                    qRgb(255, 255, 255)};
    PaletteLibrary library(theme.colors);
    library.renameCurrent(theme.name);
    QByteArray persisted;
    unsigned callbacks = 0;
    PaletteEditor editor(library, {theme}, [&](const auto &changed) {
        persisted = changed.encode();
        ++callbacks;
    });
    editor.show();
    QApplication::processEvents();
    auto *manual = editor.findChild<QCheckBox *>("palette/manual");
    auto *off = editor.findChild<QCheckBox *>("palette/separateOff");
    auto *brightness = editor.findChild<QDoubleSpinBox *>("palette/parameter/0");
    auto *hue = editor.findChild<QDoubleSpinBox *>("palette/parameter/1");
    auto *strength = editor.findChild<QDoubleSpinBox *>("palette/parameter/2");
    auto *list = editor.findChild<QListWidget *>("palette/list");
    auto *name = editor.findChild<QLineEdit *>("palette/name");
    assert(manual && off && brightness && hue && strength && list && name);
    assert(manual->isChecked() && off->isChecked() && !brightness->isEnabled());
    assert(editor.findChild<QPushButton *>("palette/color/1")->isEnabled());
    manual->click();
    assert(!manual->isChecked() && brightness->isEnabled() && hue->isEnabled() &&
           strength->isEnabled());
    assert(!editor.findChild<QPushButton *>("palette/color/0")->isEnabled());
    assert(editor.findChild<QPushButton *>("palette/color/3")->isEnabled());
    assert(editor.findChild<QPushButton *>("palette/color/4")->isEnabled());
    off->click();
    assert(!editor.library().current().separateOff);
    assert(editor.library().current().colors.shades[1] == qRgb(170, 170, 170));
    hue->setValue(120);
    strength->setValue(1);
    assert(editor.library().current().colors.shades[1] == qRgb(50, 230, 50));
    editor.findChild<QSlider *>("palette/slider/0")->setValue(64);
    assert(brightness->value() == 0.5 && editor.library().current().brightness == 0.5);
    if (argc == 3)
        assert(editor.grab().save(QString::fromLocal8Bit(argv[2])));
    manual->click();
    assert(!brightness->isEnabled() &&
           !editor.findChild<QPushButton *>("palette/color/4")->isEnabled());
    application.setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QTimer picker;
    picker.setInterval(1);
    bool colorPicked = false;
    QObject::connect(&picker, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        picker.stop();
        dialog->setCurrentColor(QColor(11, 22, 33));
        colorPicked = true;
        dialog->accept();
    });
    picker.start();
    editor.findChild<QPushButton *>("palette/color/0")->click();
    assert(colorPicked && editor.library().current().colors.shades[0] == qRgb(11, 22, 33));
    assert(editor.library().current().colors.off == qRgb(11, 22, 33));
    editor.findChild<QPushButton *>("palette/add")->click();
    assert(list->count() == 2 && editor.library().current().name == "Untitled Palette");
    name->setText("Gray");
    QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
    assert(editor.library().current().name == "Gray 2");
    editor.findChild<QPushButton *>("palette/restore")->click();
    assert(list->count() == 2 && editor.library().current().name == "Gray 2");
    list->setCurrentRow(0);
    assert(editor.library().current().name == "Gray");
    assert(editor.library().current().manual &&
           editor.library().current().colors.off == qRgb(255, 255, 255));
    editor.findChild<QPushButton *>("palette/delete")->click();
    editor.findChild<QPushButton *>("palette/delete")->click();
    assert(list->count() == 1 && editor.library().current().name == "Untitled Palette");
    bool valid = false;
    const auto restored = PaletteLibrary::decode(persisted, {}, &valid);
    assert(valid && restored.current().name == editor.library().current().name && callbacks >= 10);
}

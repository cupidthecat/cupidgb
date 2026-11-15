#include "display_files.h"
#include "main_window_internal.h"
#include "palette_editor.h"

#include <QClipboard>
#include <QDateTime>

namespace {
DisplayPaletteFile customPalette(const QSettings &settings) {
    DisplayPaletteFile colors;
    colors.shades = monochromeColors(MonochromePalette::Lime, {});
    for (size_t shade = 0; shade < colors.shades.size(); ++shade) {
        const QColor value(settings
                               .value(QString("video/customPalette/%1").arg(shade),
                                      QColor(colors.shades[shade]).name())
                               .toString());
        if (value.isValid())
            colors.shades[shade] = value.rgb();
    }
    const QColor off(
        settings.value("video/customPalette/off", QColor(colors.shades[0]).name()).toString());
    colors.off = off.isValid() ? off.rgb() : colors.shades[0];
    return colors;
}

QVector<PaletteTheme> builtInThemes() {
    QVector<PaletteTheme> themes;
    const std::array<const char *, 4> names = {"Lime (Game Boy)", "Grayscale", "Olive (Pocket)",
                                               "Teal (Light)"};
    for (size_t index = 0; index < names.size(); ++index) {
        PaletteTheme theme;
        theme.name = names[index];
        theme.colors.shades = monochromeColors(MonochromePalette(index), {});
        theme.colors.off = monochromeOffColor(MonochromePalette(index), theme.colors.shades[0]);
        themes.append(theme);
    }
    return themes;
}

PaletteLibrary configuredLibrary(const QSettings &settings) {
    const auto colors = customPalette(settings);
    const auto bytes = settings.value("video/paletteLibrary").toByteArray();
    auto library = PaletteLibrary::decode(bytes, colors);
    if (bytes.isEmpty())
        library.restoreDefaults(builtInThemes());
    // Existing custom-color settings remain editable by the ordinary Settings dialog.
    // Fold a later manual edit into the selected named entry without discarding the library.
    if (library.current().colors.shades != colors.shades ||
        library.current().colors.off != colors.off) {
        library.current().colors = colors;
        library.current().manual = true;
        library.current().separateOff = true;
    }
    return library;
}
} // namespace

void MainWindow::buildPaletteFileMenu(QMenu *palette) {
    palette->addSeparator();
    auto *saved = palette->addMenu("Saved Palettes");
    saved->setObjectName("video/savedPalettes");
    connect(saved, &QMenu::aboutToShow, this, [this, saved] {
        saved->clear();
        const auto library = configuredLibrary(settings_);
        for (const auto &theme : library.themes()) {
            QString label = theme.name;
            label.replace('&', "&&");
            auto *action = saved->addAction(label);
            action->setCheckable(true);
            action->setChecked(settings_.value("video/palette", 0).toInt() ==
                                   int(MonochromePalette::Custom) &&
                               library.current().name == theme.name);
            connect(action, &QAction::triggered, this, [this, name = theme.name] {
                auto selected = configuredLibrary(settings_);
                if (selected.select(name))
                    applyPaletteLibrary(selected);
            });
        }
    });
    auto *edit = palette->addAction("Edit Palette Library…");
    edit->setObjectName("video/editPalettes");
    connect(edit, &QAction::triggered, this, [this] { openPaletteEditor(); });
    auto *import = palette->addAction("&Import Palette…");
    import->setObjectName("video/importPalette");
    connect(import, &QAction::triggered, this, [this] { importPalette(); });
    auto *exportAction = palette->addAction("&Export Palette…");
    exportAction->setObjectName("video/exportPalette");
    connect(exportAction, &QAction::triggered, this, [this] { exportPalette(); });
}

bool MainWindow::loadPaletteFile(const QString &path) {
    QFile file(path);
    PaletteTheme theme;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096 ||
        !decodePaletteTheme(file.readAll(), theme)) {
        reportError("Could not read the palette file: " + path);
        return false;
    }
    auto library = configuredLibrary(settings_);
    if (!library.add(theme, QFileInfo(path).completeBaseName())) {
        reportError("The palette library is full. Delete an unused palette before importing.");
        return false;
    }
    applyPaletteLibrary(library);
    settings_.setValue("video/paletteDirectory", QFileInfo(path).absolutePath());
    statusBar()->showMessage("Palette imported", 3000);
    return true;
}

void MainWindow::applyPaletteLibrary(const PaletteLibrary &library) {
    const auto &palette = library.current().colors;
    settings_.setValue("video/paletteLibrary", library.encode());
    for (size_t shade = 0; shade < palette.shades.size(); ++shade)
        settings_.setValue(QString("video/customPalette/%1").arg(shade),
                           QColor(palette.shades[shade]).name());
    settings_.setValue("video/customPalette/off", QColor(palette.off).name());
    settings_.setValue("video/palette", int(MonochromePalette::Custom));
    applyDisplaySettings();
}

void MainWindow::openPaletteEditor() {
    const bool wasPaused = paused_;
    setPaused(true);
    PaletteEditor editor(
        configuredLibrary(settings_), builtInThemes(),
        [this](const auto &library) { applyPaletteLibrary(library); }, this);
    editor.exec();
    setPaused(wasPaused);
}

void MainWindow::importPalette() {
    const bool wasPaused = paused_;
    setPaused(true);
    const QString path = QFileDialog::getOpenFileName(
        this, "Import Palette", settings_.value("video/paletteDirectory").toString(),
        "Palette files (*.sbp);;All files (*)");
    if (!path.isEmpty())
        loadPaletteFile(path);
    setPaused(wasPaused);
}

void MainWindow::exportPalette() {
    const bool wasPaused = paused_;
    setPaused(true);
    PaletteTheme theme;
    theme.name = "CupidGB";
    theme.colors = {displayPalette_, displayOffColor_};
    if (settings_.value("video/palette", 0).toInt() == int(MonochromePalette::Custom))
        theme = configuredLibrary(settings_).current();
    QString suggested = theme.name;
    suggested.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
    QString path = QFileDialog::getSaveFileName(
        this, "Export Palette",
        QDir(settings_.value("video/paletteDirectory").toString()).filePath(suggested + ".sbp"),
        "Palette files (*.sbp)");
    if (!path.isEmpty()) {
        if (QFileInfo(path).suffix().isEmpty())
            path += ".sbp";
        if (QFileInfo(path).suffix().compare("sbp", Qt::CaseInsensitive) != 0 ||
            (emulator_ && cheatDestinationProtected(path, romPath_, savePath_))) {
            reportError("Choose a .sbp palette file outside cartridge, battery and state paths.");
        } else {
            const auto bytes = encodePaletteTheme(theme);
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
                !file.commit()) {
                reportError("Could not export the palette: " + file.errorString());
            } else {
                settings_.setValue("video/paletteDirectory", QFileInfo(path).absolutePath());
                statusBar()->showMessage("Palette exported", 3000);
            }
        }
    }
    setPaused(wasPaused);
}

void MainWindow::screenshot() {
    if (image_.isNull())
        return;
    QString error;
    const QString path =
        saveTimestampedScreenshot(image_, romPath_, QDateTime::currentDateTime(), &error);
    if (path.isEmpty()) {
        notifyOnScreen("Failed to save screenshot");
        reportError("Could not save screenshot: " + error);
    } else {
        statusBar()->showMessage("Screenshot saved: " + QDir::toNativeSeparators(path), 5000);
        notifyOnScreen("Screenshot saved");
    }
}

bool MainWindow::saveScreenshot(const QString &path) {
    if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0 ||
        cheatDestinationProtected(path, romPath_, savePath_)) {
        reportError("Choose a .png screenshot outside cartridge, battery and state paths.");
        return false;
    }
    QString error;
    if (!saveScreenshotFile(image_, path, &error)) {
        notifyOnScreen("Failed to save screenshot");
        reportError("Could not save screenshot: " + error);
        return false;
    }
    statusBar()->showMessage("Screenshot saved: " + QDir::toNativeSeparators(path), 5000);
    notifyOnScreen("Screenshot saved");
    return true;
}

void MainWindow::screenshotAs() {
    if (image_.isNull())
        return;
    const bool wasPaused = paused_;
    setPaused(true);
    const QFileInfo cartridge(romPath_);
    QString path = QFileDialog::getSaveFileName(
        this, "Save Screenshot As", cartridge.dir().filePath(cartridge.completeBaseName() + ".png"),
        "PNG image (*.png)");
    if (!path.isEmpty()) {
        if (QFileInfo(path).suffix().isEmpty())
            path += ".png";
        saveScreenshot(path);
    }
    setPaused(wasPaused);
}

void MainWindow::copyScreenshot() {
    if (image_.isNull())
        return;
    QApplication::clipboard()->setImage(image_);
    statusBar()->showMessage("Screenshot copied", 3000);
    notifyOnScreen("Screenshot copied");
}

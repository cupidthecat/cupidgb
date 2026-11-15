#include "display_files.h"
#include "main_window_internal.h"
#include "palette_editor.h"

#include <QClipboard>
#include <QDoubleSpinBox>

bool MainWindow::displayFilesSmokeTest(const QString &directory) {
    MainWindow session(true, true);
    for (const char *action : {"screenshot", "screenshotAs", "copyScreenshot"})
        if (!session.findChild<QAction *>(action) ||
            session.findChild<QAction *>(action)->isEnabled())
            return smokeFailure(__FILE__, __LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom[0x14d] = char(0xe7);
    const QString cartridge = QDir(directory).filePath("display-files.gb");
    QFile output(cartridge);
    if (!output.open(QIODevice::WriteOnly) || output.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    output.close();
    if (!session.openRom(cartridge) || !session.runFrame())
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    const auto state = session.captureState();
    const auto image = session.image_;
    for (const char *action : {"screenshot", "screenshotAs", "copyScreenshot"})
        if (!session.findChild<QAction *>(action)->isEnabled())
            return smokeFailure(__FILE__, __LINE__);
    const auto previousFiles = QDir(directory).entryList({"display-files *.png"}, QDir::Files);
    session.screenshotAction_->trigger();
    const auto files = QDir(directory).entryList({"display-files *.png"}, QDir::Files);
    if (files.size() != previousFiles.size() + 1 || !session.paused_ ||
        session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    for (const QString &file : files)
        if (!previousFiles.contains(file) && QImage(QDir(directory).filePath(file)) != image)
            return smokeFailure(__FILE__, __LINE__);
    session.copyScreenshotAction_->trigger();
    if (QApplication::clipboard()->image().convertToFormat(QImage::Format_RGB32) != image ||
        session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    const QString selectedCapture = QDir(directory).filePath("selected-capture.png");
    if (!session.saveScreenshot(selectedCapture) || QImage(selectedCapture) != image)
        return smokeFailure(__FILE__, __LINE__);
    auto fileDialog = [&session](QAction *action, const QString &path) {
        bool checked = false;
        QTimer driver;
        driver.setInterval(1);
        QObject::connect(&driver, &QTimer::timeout, &session, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            driver.stop();
            checked = session.paused_;
            if (path.isEmpty()) {
                dialog->reject();
            } else {
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        driver.start();
        // This driver edits QFileDialog widgets after showing them. OS-native
        // pickers do not support that operation consistently; qualify those separately.
        const bool widgetDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        action->trigger();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, widgetDialogs);
        return checked;
    };
    session.setPaused(false);
    const QString dialogCapture = QDir(directory).filePath("dialog-capture.png");
    const bool acceptedCapture = fileDialog(session.screenshotAsAction_, dialogCapture);
    const bool captureUnchanged = session.captureState() == state;
    const auto savedImage = QImage(dialogCapture);
    if (!acceptedCapture || session.paused_ || savedImage != image || !captureUnchanged) {
        qCritical("Screenshot dialog check: accepted=%d paused=%d file=%d image=%dx%d/%d "
                  "expected=%dx%d/%d state=%d failed=%d",
                  acceptedCapture, session.paused_, QFileInfo::exists(dialogCapture),
                  savedImage.width(), savedImage.height(), int(savedImage.format()), image.width(),
                  image.height(), int(image.format()), captureUnchanged, session.failed_);
        return smokeFailure(__FILE__, __LINE__);
    }
    if (!fileDialog(session.screenshotAsAction_, {}) || session.paused_)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    if (session.saveScreenshot(cartridge) || session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    const auto originalColors = session.displayPalette_;
    const auto originalOff = session.displayOffColor_;
    const auto originalPalette = settings_.value("video/palette");
    const auto originalDirectory = settings_.value("video/paletteDirectory");
    const auto originalLibrary = settings_.value("video/paletteLibrary");
    std::array<QVariant, 4> custom;
    for (size_t i = 0; i < custom.size(); ++i)
        custom[i] = settings_.value(QString("video/customPalette/%1").arg(i));
    const auto originalCustomOff = settings_.value("video/customPalette/off");
    const DisplayPaletteFile imported{
        {qRgb(240, 230, 220), qRgb(190, 170, 150), qRgb(90, 70, 50), qRgb(30, 20, 10)},
        qRgb(250, 240, 230)};
    const QString palettePath = QDir(directory).filePath("imported.sbp");
    QFile palette(palettePath);
    const auto bytes = encodeDisplayPalette(imported);
    if (!palette.open(QIODevice::WriteOnly) || palette.write(bytes) != bytes.size())
        return smokeFailure(__FILE__, __LINE__);
    palette.close();
    if (!fileDialog(session.findChild<QAction *>("video/importPalette"), palettePath) ||
        !session.paused_ || session.displayPalette_ != imported.shades ||
        session.displayOffColor_ != imported.off ||
        !session.paletteActions_[size_t(MonochromePalette::Custom)]->isChecked() ||
        session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    const QString exportedPath = QDir(directory).filePath("exported.sbp");
    if (!fileDialog(session.findChild<QAction *>("video/exportPalette"), exportedPath))
        return smokeFailure(__FILE__, __LINE__);
    QFile exported(exportedPath);
    DisplayPaletteFile exportedColors;
    if (!exported.open(QIODevice::ReadOnly) ||
        !decodeDisplayPalette(exported.readAll(), exportedColors) ||
        exportedColors.shades != imported.shades || exportedColors.off != imported.off ||
        !fileDialog(session.findChild<QAction *>("video/importPalette"), {}) ||
        session.displayPalette_ != imported.shades)
        return smokeFailure(__FILE__, __LINE__);
    if (session.loadPaletteFile(cartridge) || session.displayPalette_ != imported.shades ||
        session.displayOffColor_ != imported.off)
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    bool edited = false;
    QTimer editorDriver;
    editorDriver.setInterval(1);
    QObject::connect(&editorDriver, &QTimer::timeout, &session, [&] {
        auto *editor = dynamic_cast<PaletteEditor *>(QApplication::activeModalWidget());
        if (!editor)
            return;
        editorDriver.stop();
        edited = session.paused_;
        editor->findChild<QPushButton *>("palette/add")->click();
        auto *name = editor->findChild<QLineEdit *>("palette/name");
        name->setText("Workflow Palette");
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        editor->findChild<QCheckBox *>("palette/manual")->click();
        editor->findChild<QDoubleSpinBox *>("palette/parameter/0")->setValue(0.5);
        editor->findChild<QDoubleSpinBox *>("palette/parameter/1")->setValue(120);
        editor->findChild<QDoubleSpinBox *>("palette/parameter/2")->setValue(0.25);
        editor->accept();
    });
    editorDriver.start();
    session.findChild<QAction *>("video/editPalettes")->trigger();
    bool validLibrary = false;
    const auto storedLibrary = PaletteLibrary::decode(
        settings_.value("video/paletteLibrary").toByteArray(), {}, &validLibrary);
    if (!edited || !validLibrary || !session.paused_ || session.captureState() != state ||
        storedLibrary.current().name != "Workflow Palette" || storedLibrary.current().manual ||
        storedLibrary.current().brightness != 0.5 ||
        session.displayPalette_ != storedLibrary.current().colors.shades)
        return smokeFailure(__FILE__, __LINE__);
    auto *savedPalettes = session.findChild<QMenu *>("video/savedPalettes");
    QMetaObject::invokeMethod(savedPalettes, "aboutToShow", Qt::DirectConnection);
    bool selectedImported = false;
    for (auto *action : savedPalettes->actions())
        if (action->text() == "imported") {
            action->trigger();
            selectedImported = true;
            break;
        }
    QMetaObject::invokeMethod(savedPalettes, "aboutToHide", Qt::DirectConnection);
    if (!selectedImported || session.displayPalette_ != imported.shades ||
        session.displayOffColor_ != imported.off || session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    for (size_t i = 0; i < custom.size(); ++i)
        if (custom[i].isValid())
            settings_.setValue(QString("video/customPalette/%1").arg(i), custom[i]);
        else
            settings_.remove(QString("video/customPalette/%1").arg(i));
    for (const auto &entry : {std::make_pair("video/palette", originalPalette),
                              std::make_pair("video/paletteDirectory", originalDirectory),
                              std::make_pair("video/paletteLibrary", originalLibrary),
                              std::make_pair("video/customPalette/off", originalCustomOff)})
        if (entry.second.isValid())
            settings_.setValue(entry.first, entry.second);
        else
            settings_.remove(entry.first);
    session.applyDisplaySettings();
    if (session.displayPalette_ != originalColors || session.displayOffColor_ != originalOff ||
        !session.closeRom())
        return smokeFailure(__FILE__, __LINE__);
    for (const char *action : {"screenshot", "screenshotAs", "copyScreenshot"})
        if (session.findChild<QAction *>(action)->isEnabled())
            return smokeFailure(__FILE__, __LINE__);
    return !session.failed_;
}

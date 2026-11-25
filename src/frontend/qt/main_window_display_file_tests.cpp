/**
 * @file    main_window_display_file_tests.cpp
 * @brief   Qt workflow regressions for palette files and screenshot actions.
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

#include "display_files.h"
#include "main_window_internal.h"
#include "palette_editor.h"

#include <QClipboard>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QPointer>
#include <QScopeGuard>
#include <cmath>

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
    // Render the actual widget so filter selection, cache invalidation and
    // resized output are exercised through the same entry point as desktop paints.
    DisplayWidget filterProbe(nullptr);
    filterProbe.scaling = DisplayScaling::Fill;
    filterProbe.resize(321, 289);
    filterProbe.image = QImage(2, 2, QImage::Format_RGB32);
    filterProbe.image.fill(Qt::black);
    filterProbe.image.setPixel(1, 1, qRgb(255, 255, 255));
    for (int mode = 1; mode < int(ScalingFilter::Count); ++mode) {
        const auto selected = ScalingFilter(mode);
        filterProbe.filter = selected;
        const QSize output =
            (QSizeF(filterProbe.size()) * filterProbe.devicePixelRatioF()).toSize();
        QImage rendered(output, QImage::Format_RGB32);
        rendered.setDevicePixelRatio(filterProbe.devicePixelRatioF());
        filterProbe.render(&rendered);
        QImage expected = filterDisplayImage(filterProbe.image, output, selected);
        expected.setDevicePixelRatio(filterProbe.devicePixelRatioF());
        if (rendered != expected ||
            (selected != ScalingFilter::Pixelated &&
             (filterProbe.filteredSourceKey != filterProbe.image.cacheKey() ||
              filterProbe.cachedFilter != selected)))
            return smokeFailure(__FILE__, __LINE__);
    }
    // This cache-resize probe expects unchanged solid RGB, including when the
    // preceding loop ends on a filter that simulates a monochrome display.
    filterProbe.filter = ScalingFilter::Bilinear;
    filterProbe.image.fill(Qt::red);
    filterProbe.resize(400, 300);
    const QSize resizedOutput =
        (QSizeF(filterProbe.size()) * filterProbe.devicePixelRatioF()).toSize();
    QImage resized(resizedOutput, QImage::Format_RGB32);
    resized.setDevicePixelRatio(filterProbe.devicePixelRatioF());
    filterProbe.render(&resized);
    if (filterProbe.filteredImage.size() != resizedOutput ||
        filterProbe.filteredSourceKey != filterProbe.image.cacheKey() ||
        resized.pixel(200, 150) != qRgb(255, 0, 0))
        return smokeFailure(__FILE__, __LINE__);
    QImage white(160, 144, QImage::Format_RGB32);
    QImage black(160, 144, QImage::Format_RGB32);
    white.fill(Qt::white);
    black.fill(Qt::black);
    filterProbe.filter = ScalingFilter::Bilinear;
    for (auto size : {QSize(320, 288), QSize(401, 359)}) {
        filterProbe.resize(size);
        for (bool odd : {false, true}) {
            filterProbe.setFrames(white, black, FrameBlending::Accurate, odd);
            const QSize output =
                (QSizeF(filterProbe.size()) * filterProbe.devicePixelRatioF()).toSize();
            QImage painted(output, QImage::Format_RGB32);
            painted.setDevicePixelRatio(filterProbe.devicePixelRatioF());
            filterProbe.render(&painted);
            for (int y = 0; y < output.height(); ++y) {
                const int sourceRow = int((y + 0.5) * white.height() / output.height());
                const double weight = ((sourceRow & 1) != int(odd)) ? 2.0 / 3.0 : 1.0 / 3.0;
                const int expected = int(std::lround(255 * std::pow(1 - weight, 1.0 / 2.2)));
                if (std::abs(qRed(painted.pixel(output.width() / 2, y)) - expected) > 1)
                    return smokeFailure(__FILE__, __LINE__);
            }
        }
    }
    filterProbe.setFrames({}, {}, FrameBlending::Disabled, false);
    filterProbe.resize(360, 324);
    QImage cleared((QSizeF(filterProbe.size()) * filterProbe.devicePixelRatioF()).toSize(),
                   QImage::Format_RGB32);
    cleared.setDevicePixelRatio(filterProbe.devicePixelRatioF());
    filterProbe.render(&cleared);
    if (!filterProbe.image.isNull() || !filterProbe.filteredImage.isNull())
        return smokeFailure(__FILE__, __LINE__);
    filterProbe.setFrames(white, black, FrameBlending::Simple, false);
    filterProbe.render(&cleared);
    if (cleared.pixel(cleared.width() / 2, cleared.height() / 2) != qRgb(186, 186, 186))
        return smokeFailure(__FILE__, __LINE__);
    // Display-only interpolation never replaces the native capture source.
    session.display_->filter = ScalingFilter::Bilinear;
    session.display_->setFrames(white, black, FrameBlending::Accurate, false);
    session.copyScreenshot();
    const QString nativeCapture = QDir(directory).filePath("filtered-native.png");
    if (QApplication::clipboard()->image().convertToFormat(QImage::Format_RGB32) != image ||
        !session.saveScreenshot(nativeCapture) || QImage(nativeCapture) != image ||
        session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    session.applyDisplaySettings();
    QImage tagged(160, 144, QImage::Format_RGB32);
    for (int y = 0; y < tagged.height(); ++y)
        for (int x = 0; x < tagged.width(); ++x)
            tagged.setPixel(x, y, qRgb(x, y, 31));
    filterProbe.filter = ScalingFilter::Nearest;
    for (auto size : {QSize(320, 288), QSize(401, 359)}) {
        filterProbe.setFrames({}, {}, FrameBlending::Disabled, false);
        filterProbe.resize(size);
        filterProbe.setFrames(tagged, {}, FrameBlending::Disabled, false);
        const QSize physical =
            (QSizeF(filterProbe.size()) * filterProbe.devicePixelRatioF()).toSize();
        QImage painted(physical, QImage::Format_RGB32);
        painted.setDevicePixelRatio(filterProbe.devicePixelRatioF());
        filterProbe.render(&painted);
        for (int y = 0; y < physical.height(); ++y)
            for (int x = 0; x < physical.width(); ++x) {
                const int sx = int(std::floor((static_cast<long double>(x) + 0.5L) *
                                              tagged.width() / physical.width()));
                const int sy = int(std::floor((static_cast<long double>(y) + 0.5L) *
                                              tagged.height() / physical.height()));
                if (painted.pixel(x, y) != tagged.pixel(sx, sy))
                    return smokeFailure(__FILE__, __LINE__);
            }
    }
    // Pixelated integer replication uses the already blended native image.
    filterProbe.filter = ScalingFilter::Pixelated;
    for (const auto size : {QSize(320, 288), QSize(640, 576)}) {
        filterProbe.setFrames({}, {}, FrameBlending::Disabled, false);
        filterProbe.resize(size);
        filterProbe.setFrames(tagged, black, FrameBlending::Accurate, true);
        const QSize physical =
            (QSizeF(filterProbe.size()) * filterProbe.devicePixelRatioF()).toSize();
        QImage painted(physical, QImage::Format_RGB32);
        painted.setDevicePixelRatio(filterProbe.devicePixelRatioF());
        filterProbe.render(&painted);
        auto expected = renderDisplayFrames(tagged, black, physical, ScalingFilter::Pixelated,
                                            FrameBlending::Accurate, true);
        expected.setDevicePixelRatio(filterProbe.devicePixelRatioF());
        if (painted != expected)
            return smokeFailure(__FILE__, __LINE__);
        const QSizeF exact = QSizeF(filterProbe.size()) * filterProbe.devicePixelRatioF();
        if (physical.width() % tagged.width() == 0 && physical.height() % tagged.height() == 0 &&
            qFuzzyCompare(exact.width(), qreal(physical.width())) &&
            qFuzzyCompare(exact.height(), qreal(physical.height())) &&
            (!filterProbe.filteredImage.isNull() || filterProbe.filteredSourceKey))
            return smokeFailure(__FILE__, __LINE__);
    }
    // Nonlinear edge decisions use each unblended source frame independently.
    QImage edges(3, 3, QImage::Format_RGB32), history(3, 3, QImage::Format_RGB32);
    edges.fill(Qt::black);
    history.fill(Qt::white);
    for (const auto point : {QPoint(1, 0), QPoint(1, 1), QPoint(0, 1)})
        edges.setPixel(point, qRgb(255, 255, 255));
    for (const auto point : {QPoint(0, 0), QPoint(1, 1), QPoint(2, 2)})
        history.setPixel(point, qRgb(0, 0, 0));
    for (int mode = int(ScalingFilter::Pixelated); mode < int(ScalingFilter::Count); ++mode) {
        filterProbe.filter = ScalingFilter(mode);
        filterProbe.setFrames({}, {}, FrameBlending::Disabled, false);
        filterProbe.resize(401, 359);
        filterProbe.setFrames(edges, history, FrameBlending::Accurate, false);
        const QSize physical =
            (QSizeF(filterProbe.size()) * filterProbe.devicePixelRatioF()).toSize();
        QImage painted(physical, QImage::Format_RGB32);
        painted.setDevicePixelRatio(filterProbe.devicePixelRatioF());
        filterProbe.render(&painted);
        auto expected = renderDisplayFrames(edges, history, physical, ScalingFilter(mode),
                                            FrameBlending::Accurate, false);
        expected.setDevicePixelRatio(filterProbe.devicePixelRatioF());
        if (painted != expected)
            return smokeFailure(__FILE__, __LINE__);
        if (mode != int(ScalingFilter::Pixelated) &&
            (filterProbe.filteredSourceKey != filterProbe.image.cacheKey() ||
             filterProbe.cachedFilter != ScalingFilter(mode)))
            return smokeFailure(__FILE__, __LINE__);
    }
    // Moving a letterboxed picture by one physical pixel changes the curved
    // mask even when the source frame and the picture dimensions stay fixed.
    filterProbe.filter = ScalingFilter::Crt;
    filterProbe.scaling = DisplayScaling::Aspect;
    const qreal ratio = filterProbe.devicePixelRatioF();
    const auto physicalViewport = [&] {
        const QRectF viewport =
            displayViewport(edges.size(), filterProbe.size(), filterProbe.scaling, ratio);
        return QRect((viewport.topLeft() * ratio).toPoint(), (viewport.size() * ratio).toSize());
    };
    const auto paintProbe = [&] {
        QImage canvas((QSizeF(filterProbe.size()) * ratio).toSize(), QImage::Format_RGB32);
        canvas.setDevicePixelRatio(ratio);
        filterProbe.render(&canvas);
    };
    const auto verifyCrtShift = [&](QSize base, QSize step) {
        filterProbe.resize(base);
        const QRect firstViewport = physicalViewport();
        paintProbe();
        const QImage firstCrt = filterProbe.filteredImage;
        for (int amount = 1; amount <= 8; ++amount) {
            filterProbe.resize(base + amount * step);
            const QRect viewport = physicalViewport();
            const QPoint shift = viewport.topLeft() - firstViewport.topLeft();
            if (viewport.size() != firstViewport.size() || ((shift.x() | shift.y()) & 1) == 0)
                continue;
            paintProbe();
            const auto expected =
                renderDisplayFrames(edges, history, viewport.size(), ScalingFilter::Crt,
                                    FrameBlending::Accurate, false, viewport.topLeft());
            return filterProbe.filteredImage == expected && filterProbe.filteredImage != firstCrt;
        }
        return false;
    };
    if (!verifyCrtShift({401, 359}, {1, 0}) || !verifyCrtShift({359, 401}, {0, 1}))
        return smokeFailure(__FILE__, __LINE__);

    const QVariant originalFilter = settings_.value("video/filter");
    const auto restoreFilter = qScopeGuard([&] {
        if (originalFilter.isValid())
            settings_.setValue("video/filter", originalFilter);
        else
            settings_.remove("video/filter");
    });
    const std::array<std::pair<ScalingFilter, const char *>, 4> panelFilters = {
        std::make_pair(ScalingFilter::Lcd, "LCD Display"),
        std::make_pair(ScalingFilter::MonochromeLcd, "Monochrome LCD"),
        std::make_pair(ScalingFilter::FlatCrt, "Flat CRT Display"),
        std::make_pair(ScalingFilter::Crt, "CRT Display")};
    const auto configureFilter = [&](int expected, int selected, bool accept) {
        bool checked = false;
        bool timedOut = false;
        QTimer driver;
        driver.setInterval(10);
        connect(&driver, &QTimer::timeout, &session, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "settings" || dialog->parentWidget() != &session)
                return;
            driver.stop();
            auto *choice = dialog->findChild<QComboBox *>("scalingFilter");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            checked = choice && buttons && choice->count() == int(ScalingFilter::Count) &&
                      choice->currentIndex() == expected;
            for (const auto &[filter, label] : panelFilters)
                checked = checked && choice->itemText(int(filter)) == QString::fromLatin1(label);
            if (checked) {
                choice->setCurrentIndex(selected);
                buttons->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
            } else {
                dialog->reject();
            }
        });
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &session, [&] {
            timedOut = true;
            if (auto *dialog = session.findChild<QDialog *>("settings"))
                dialog->reject();
        });
        driver.start();
        watchdog.start(3000);
        session.configure();
        driver.stop();
        watchdog.stop();
        return checked && !timedOut;
    };
    settings_.remove("video/filter");
    session.applyDisplaySettings();
    if (session.display_->filter != ScalingFilter::Pixelated ||
        !session.scalingFilterActions_[size_t(ScalingFilter::Pixelated)]->isChecked() ||
        !configureFilter(int(ScalingFilter::Pixelated), int(ScalingFilter::Nearest), false))
        return smokeFailure(__FILE__, __LINE__);
    {
        MainWindow defaults(true, true);
        if (defaults.display_->filter != ScalingFilter::Pixelated)
            return smokeFailure(__FILE__, __LINE__);
    }
    for (int mode = 0; mode < int(ScalingFilter::Count); ++mode) {
        auto *action = session.findChild<QAction *>(QString("video/filter/%1").arg(mode));
        if (!action)
            return smokeFailure(__FILE__, __LINE__);
        for (const auto &[filter, label] : panelFilters)
            if (mode == int(filter) && action->text() != QString::fromLatin1(label))
                return smokeFailure(__FILE__, __LINE__);
        action->trigger();
        if (!action->isChecked() || session.display_->filter != ScalingFilter(mode) ||
            settings_.value("video/filter").toInt() != mode || session.image_ != image ||
            session.captureState() != state)
            return smokeFailure(__FILE__, __LINE__);
        MainWindow restored(true, true);
        if (restored.display_->filter != ScalingFilter(mode))
            return smokeFailure(__FILE__, __LINE__);
        if (!configureFilter(mode, (mode + 1) % int(ScalingFilter::Count), false) ||
            session.display_->filter != ScalingFilter(mode) ||
            settings_.value("video/filter").toInt() != mode)
            return smokeFailure(__FILE__, __LINE__);
        session.copyScreenshot();
        const auto nativePath = QDir(directory).filePath(QString("native-filter-%1.png").arg(mode));
        if (QApplication::clipboard()->image().convertToFormat(QImage::Format_RGB32) != image ||
            !session.saveScreenshot(nativePath) || QImage(nativePath) != image)
            return smokeFailure(__FILE__, __LINE__);
    }
    int configuredFilter = int(ScalingFilter::Count) - 1;
    for (const auto &[filter, label] : panelFilters) {
        if (!configureFilter(configuredFilter, int(filter), true) ||
            session.display_->filter != filter ||
            !session.scalingFilterActions_[size_t(filter)]->isChecked() ||
            settings_.value("video/filter").toInt() != int(filter) ||
            session.captureState() != state)
            return smokeFailure(__FILE__, __LINE__);
        MainWindow restored(true, true);
        auto *action = restored.scalingFilterActions_[size_t(filter)];
        if (restored.display_->filter != filter || !action->isChecked() ||
            action->text() != QString::fromLatin1(label))
            return smokeFailure(__FILE__, __LINE__);
        configuredFilter = int(filter);
    }
    if (!configureFilter(int(ScalingFilter::Count) - 1, 1, true) ||
        session.display_->filter != ScalingFilter::Bilinear ||
        settings_.value("video/filter").toInt() != 1 || session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    for (const QVariant &invalid :
         {QVariant(-1), QVariant(int(ScalingFilter::Count)), QVariant("invalid")}) {
        settings_.setValue("video/filter", invalid);
        session.applyDisplaySettings();
        if (session.display_->filter != ScalingFilter::Nearest ||
            !session.scalingFilterActions_[0]->isChecked())
            return smokeFailure(__FILE__, __LINE__);
    }
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
    const auto rejectOwnedDialog = [](QDialog *dialog) {
        if (!dialog)
            return;
        QPointer<QDialog> owner(dialog);
        auto *prompt = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (prompt && prompt != dialog && dialog->isAncestorOf(prompt))
            prompt->reject();
        if (owner)
            owner->reject();
    };
    auto fileDialog = [&session, &rejectOwnedDialog](QAction *action, const QString &path) {
        bool checked = false;
        bool timedOut = false;
        QPointer<QFileDialog> ownedDialog;
        QElapsedTimer elapsed;
        elapsed.start();
        qInfo("Display file dialog begin: action=%s target=%s", qPrintable(action->objectName()),
              qPrintable(path));
        const auto reportDialog = [&](const char *phase) {
            qInfo("Display file dialog %s: action=%s elapsed_ms=%lld visible=%d result=%d "
                  "directory=%s selected=%s",
                  phase, qPrintable(action->objectName()),
                  static_cast<long long>(elapsed.elapsed()),
                  ownedDialog && ownedDialog->isVisible(), ownedDialog ? ownedDialog->result() : -1,
                  ownedDialog ? qPrintable(ownedDialog->directory().absolutePath()) : "<none>",
                  ownedDialog ? qPrintable(ownedDialog->selectedFiles().join('|')) : "<none>");
        };
        QTimer driver, watchdog;
        driver.setInterval(10);
        QObject::connect(&driver, &QTimer::timeout, &session, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->parentWidget() != &session)
                return;
            ownedDialog = dialog;
            checked = session.paused_;
            if (path.isEmpty()) {
                driver.stop();
                reportDialog("before reject");
                dialog->reject();
                reportDialog("after reject");
            } else {
                // A visible Qt picker can ignore selectFile while the filename
                // editor has focus. Enter the path in that editor as a user would.
                const auto matchesTarget = [&] {
                    const auto selection = dialog->selectedFiles();
                    return selection.size() == 1 &&
                           QFileInfo(selection.front()).absoluteFilePath() ==
                               QFileInfo(path).absoluteFilePath();
                };
                if (!matchesTarget()) {
                    dialog->selectFile(path);
                    auto *editor = dialog->findChild<QLineEdit *>("fileNameEdit");
                    if (!editor)
                        return;
                    editor->setText(path);
                    if (!matchesTarget())
                        return;
                }
                auto *buttons = dialog->findChild<QDialogButtonBox *>();
                auto *accept = buttons
                                   ? buttons->button(dialog->acceptMode() == QFileDialog::AcceptOpen
                                                         ? QDialogButtonBox::Open
                                                         : QDialogButtonBox::Save)
                                   : nullptr;
                if (!accept || !accept->isEnabled())
                    return;
                reportDialog("before accept");
                accept->click();
                reportDialog("after accept");
                if (!ownedDialog || !ownedDialog->isVisible())
                    driver.stop();
            }
        });
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &session, [&] {
            timedOut = true;
            driver.stop();
            if (!ownedDialog) {
                for (auto *dialog : session.findChildren<QFileDialog *>())
                    if (dialog->parentWidget() == &session && dialog->isVisible()) {
                        ownedDialog = dialog;
                        break;
                    }
            }
            reportDialog("watchdog failure");
            const auto *modal = QApplication::activeModalWidget();
            qCritical("Display file dialog timed out: action=%s target=%s elapsed_ms=%lld "
                      "modal=%s title=%s",
                      qPrintable(action->objectName()), qPrintable(path),
                      static_cast<long long>(elapsed.elapsed()),
                      modal ? modal->metaObject()->className() : "<none>",
                      modal ? qPrintable(modal->windowTitle()) : "<none>");
            rejectOwnedDialog(ownedDialog);
        });
        driver.start();
        watchdog.start(3000);
        // This driver edits QFileDialog widgets after showing them. OS-native
        // pickers do not support that operation consistently; qualify those separately.
        const bool widgetDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        action->trigger();
        driver.stop();
        watchdog.stop();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, widgetDialogs);
        qInfo("Display file dialog end: action=%s elapsed_ms=%lld checked=%d timeout=%d",
              qPrintable(action->objectName()), static_cast<long long>(elapsed.elapsed()), checked,
              timedOut);
        return checked && !timedOut;
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
    bool editorTimedOut = false;
    QPointer<PaletteEditor> ownedEditor;
    QElapsedTimer editorElapsed;
    editorElapsed.start();
    qInfo("Display palette editor begin");
    QTimer editorDriver, editorWatchdog;
    editorDriver.setInterval(1);
    QObject::connect(&editorDriver, &QTimer::timeout, &session, [&] {
        auto *editor = dynamic_cast<PaletteEditor *>(QApplication::activeModalWidget());
        if (!editor || editor->parentWidget() != &session)
            return;
        ownedEditor = editor;
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
        qInfo("Display palette editor before accept: elapsed_ms=%lld visible=%d result=%d",
              static_cast<long long>(editorElapsed.elapsed()), editor->isVisible(),
              editor->result());
        editor->accept();
        qInfo("Display palette editor after accept: elapsed_ms=%lld visible=%d result=%d",
              static_cast<long long>(editorElapsed.elapsed()),
              ownedEditor && ownedEditor->isVisible(), ownedEditor ? ownedEditor->result() : -1);
    });
    editorWatchdog.setSingleShot(true);
    QObject::connect(&editorWatchdog, &QTimer::timeout, &session, [&] {
        editorTimedOut = true;
        editorDriver.stop();
        if (!ownedEditor) {
            for (auto *dialog : session.findChildren<QDialog *>())
                if (auto *editor = dynamic_cast<PaletteEditor *>(dialog);
                    editor && editor->parentWidget() == &session && editor->isVisible()) {
                    ownedEditor = editor;
                    break;
                }
        }
        const auto *modal = QApplication::activeModalWidget();
        qCritical("Display palette editor timed out: elapsed_ms=%lld modal=%s title=%s",
                  static_cast<long long>(editorElapsed.elapsed()),
                  modal ? modal->metaObject()->className() : "<none>",
                  modal ? qPrintable(modal->windowTitle()) : "<none>");
        rejectOwnedDialog(ownedEditor);
    });
    editorDriver.start();
    editorWatchdog.start(3000);
    session.findChild<QAction *>("video/editPalettes")->trigger();
    editorDriver.stop();
    editorWatchdog.stop();
    qInfo("Display palette editor end: elapsed_ms=%lld edited=%d timeout=%d",
          static_cast<long long>(editorElapsed.elapsed()), edited, editorTimedOut);
    bool validLibrary = false;
    const auto storedLibrary = PaletteLibrary::decode(
        settings_.value("video/paletteLibrary").toByteArray(), {}, &validLibrary);
    if (editorTimedOut || !edited || !validLibrary || !session.paused_ ||
        session.captureState() != state || storedLibrary.current().name != "Workflow Palette" ||
        storedLibrary.current().manual || storedLibrary.current().brightness != 0.5 ||
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

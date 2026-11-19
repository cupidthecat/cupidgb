/**
 * @file    main_window_display.cpp
 * @brief   Qt display presentation and persistent settings workflows.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "main_window_internal.h"

#include <QWidgetAction>

extern "C" {
#include "cupid/gb/ppu.h"
}

namespace {
constexpr std::array<const char *, 7> correctionNames = {
    "Disabled",        "Correct Color Curves", "Modern - Balanced", "Modern - Boost Contrast",
    "Reduce Contrast", "Harsh Reality",        "Modern - Accurate"};
constexpr std::array<ColorCorrection, 7> correctionOrder = {
    ColorCorrection::Disabled,   ColorCorrection::Curves,        ColorCorrection::Balanced,
    ColorCorrection::Accurate,   ColorCorrection::BoostContrast, ColorCorrection::ReduceContrast,
    ColorCorrection::LowContrast};

ColorSettings configuredColorSettings(const QSettings &settings) {
    bool valid = false;
    int mode =
        settings.value("video/colorCorrection", int(ColorCorrection::Balanced)).toInt(&valid);
    if (!valid || mode < 0 || mode >= int(correctionNames.size()))
        mode = int(ColorCorrection::Balanced);
    const int temperature = settings.value("video/colorTemperature", 10).toInt(&valid);
    return {ColorCorrection(mode), valid ? std::clamp(temperature, 0, 20) : 10};
}

void configureTemperatureSlider(QSlider *slider) {
    slider->setRange(0, 20);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(5);
    slider->setAccessibleName("Ambient light temperature");
    slider->setToolTip("Cooler light at the left, warmer light at the right. Center is neutral.");
}
} // namespace

void MainWindow::buildColorMenus(QMenu *view) {
    auto *correctionMenu = view->addMenu("&Color Correction");
    auto *group = new QActionGroup(this);
    for (ColorCorrection mode : correctionOrder) {
        const auto index = size_t(mode);
        auto *action = correctionMenu->addAction(correctionNames[index]);
        action->setObjectName(QString("video/colorCorrection/%1").arg(index));
        action->setCheckable(true);
        group->addAction(action);
        colorCorrectionActions_[index] = action;
        connect(action, &QAction::triggered, this, [this, mode] {
            settings_.setValue("video/colorCorrection", int(mode));
            applyDisplaySettings();
        });
    }
    auto *temperatureMenu = view->addMenu("Ambient Light &Temperature");
    auto *temperatureAction = new QWidgetAction(this);
    colorTemperatureSlider_ = new QSlider(Qt::Horizontal);
    colorTemperatureSlider_->setObjectName("video/colorTemperature");
    colorTemperatureSlider_->setMinimumWidth(220);
    configureTemperatureSlider(colorTemperatureSlider_);
    temperatureAction->setDefaultWidget(colorTemperatureSlider_);
    temperatureMenu->addAction(temperatureAction);
    connect(colorTemperatureSlider_, &QSlider::valueChanged, this, [this](int value) {
        settings_.setValue("video/colorTemperature", value);
        applyDisplaySettings();
    });
}

void MainWindow::applyDisplaySettings() {
    const bool messages = settings_.value("video/osd", false).toBool();
    display_->messages.setEnabled(messages);
    if (auto *action = findChild<QAction *>("video/osd"))
        action->setChecked(messages);
    const int scaling = std::clamp(settings_.value("video/scaling", 0).toInt(), 0, 2);
    display_->scaling = DisplayScaling(scaling);
    bool validFilter = false;
    int filter = settings_.value("video/filter", 0).toInt(&validFilter);
    if (!validFilter || filter < 0 || filter >= int(scalingFilterActions_.size()))
        filter = 0;
    display_->filter = ScalingFilter(filter);
    for (size_t i = 0; i < scalingFilterActions_.size(); ++i)
        scalingFilterActions_[i]->setChecked(int(i) == filter);
    display_->update();
    const int palette = std::clamp(settings_.value("video/palette", 0).toInt(), 0, 4);
    for (size_t i = 0; i < scalingActions_.size(); ++i)
        scalingActions_[i]->setChecked(int(i) == scaling);
    for (size_t i = 0; i < paletteActions_.size(); ++i)
        paletteActions_[i]->setChecked(int(i) == palette);
    const int scale = std::clamp(settings_.value("video/defaultScale", 2).toInt(), 1, 8);
    for (size_t i = 0; i < windowSizeActions_.size(); ++i)
        windowSizeActions_[i]->setChecked(int(i) + 1 == scale);
    auto custom = monochromeColors(MonochromePalette::Lime, {});
    for (size_t i = 0; i < custom.size(); ++i) {
        const QColor value(
            settings_.value(QString("video/customPalette/%1").arg(i), QColor(custom[i]).name())
                .toString());
        if (value.isValid())
            custom[i] = value.rgb();
    }
    const auto colors = monochromeColors(MonochromePalette(palette), custom);
    const QColor customOff(
        settings_.value("video/customPalette/off", QColor(custom[0]).name()).toString());
    const QRgb offColor = monochromeOffColor(MonochromePalette(palette),
                                             customOff.isValid() ? customOff.rgb() : custom[0]);
    const auto colorSettings = configuredColorSettings(settings_);
    const bool colorChanged = !cgbColorTable_ ||
                              colorSettings.correction != colorSettings_.correction ||
                              colorSettings.temperature != colorSettings_.temperature;
    if (colorChanged) {
        cgbColorTable_ =
            std::make_unique<ColorTable>(makeColorTable(ColorHardware::Cgb, colorSettings));
        sgbColorTable_ =
            std::make_unique<ColorTable>(makeColorTable(ColorHardware::Sgb, colorSettings));
        colorSettings_ = colorSettings;
        if (inspectorsWindow_)
            inspectorsWindow_->setColorTable(cgbColorTable_.get());
    }
    colorCorrectionActions_[size_t(colorSettings.correction)]->setChecked(true);
    const QSignalBlocker blockTemperature(colorTemperatureSlider_);
    colorTemperatureSlider_->setValue(colorSettings.temperature);
    const bool showBorder = settings_.value("video/sgbBorder", true).toBool();
    const bool borderChanged = emulator_ && cupid_gb_sgb_host_present(&emulator_->gb) &&
                               !image_.isNull() &&
                               (image_.width() == CUPID_SGB_SCREEN_WIDTH) != showBorder;
    const bool colorHardware = emulator_ && (cupid_gb_model_is_cgb(emulator_->gb.model) ||
                                             cupid_gb_sgb_host_present(&emulator_->gb));
    const bool resetHistory = displayPalette_ != colors || displayOffColor_ != offColor ||
                              borderChanged || (colorChanged && colorHardware);
    displayPalette_ = colors;
    displayOffColor_ = offColor;
    setFrameBlending(configuredFrameBlending(settings_));
    borderAction_->setChecked(showBorder);
    if (emulator_)
        present(resetHistory ? Presentation::Reset : Presentation::Redraw);
    else
        display_->update();
}

void MainWindow::setFrameBlending(FrameBlending mode) {
    frameBlending_ = mode;
    blendingActions_[size_t(mode)]->setChecked(true);
}

QSize MainWindow::logicalWindowSize(const QSize &nativeSize, int scale) const {
    scale = std::clamp(scale, 1, 8);
    // Native menu layout can reserve more height than the bars' size hints.
    const int chromeHeight = minimumSizeHint().height() - display_->minimumHeight();
    return {std::max(nativeSize.width() * scale, menuBar()->sizeHint().width()),
            nativeSize.height() * scale + chromeHeight};
}

void MainWindow::resizeWindowCentered(const QSize &windowSize, const QRect &previousGeometry) {
    const bool preserveCenter = isVisible();
    // A changed child minimum must reach QMainWindow before an explicit shrink.
    // The geometry snapshot predates any intermediate growth from that update.
    layout()->activate();
    resize(windowSize);
    if (preserveCenter)
        move(previousGeometry.topLeft() + QPoint((previousGeometry.width() - width()) / 2,
                                                 (previousGeometry.height() - height()) / 2));
}

void MainWindow::resizeDisplay(int scale) {
    if (musicPresentation_)
        return;
    scale = std::clamp(scale, 1, 8);
    if (isFullScreen()) {
        showNormal();
        if (auto *fullscreen = findChild<QAction *>("fullscreen"))
            fullscreen->setChecked(false);
    }
    const QSize source = image_.isNull() ? QSize(160, 144) : image_.size();
    displayNativeSize_ = source;
    resizeWindowCentered(logicalWindowSize(source, scale), QRect(pos(), size()));
    for (size_t i = 0; i < windowSizeActions_.size(); ++i)
        windowSizeActions_[i]->setChecked(int(i) + 1 == scale);
}

void MainWindow::notifyOnScreen(const QString &message) {
    if (!emulator_)
        return;
    if (musicInfo_) {
        statusBar()->showMessage(message, 3000);
        return;
    }
    display_->messages.show(message);
}

void MainWindow::advanceScreenMessages() {
    if (musicInfo_ || !display_->messages.enabled())
        return;
    const QString mode = turbo_      ? QStringLiteral("Fast forward...")
                         : slowHeld_ ? QStringLiteral("Slow motion...")
                         : rewind_   ? QStringLiteral("Rewinding...")
                                     : QString();
    display_->messages.advanceFrame(mode);
}

void MainWindow::present(Presentation presentation) {
    if (!emulator_)
        return;
    if (presentMusic())
        return;
    const auto &gb = emulator_->gb;
    const bool sgb = cupid_gb_sgb_host_present(&gb);
    // A loaded adapter surface can change independently of its handheld image.
    if (!sgb && presentation == Presentation::Frame &&
        gb.display_frame_type == CUPID_GB_FRAME_REPEAT)
        return;
    const QSize nativeSize = sgb && borderAction_->isChecked()
                                 ? QSize(CUPID_SGB_SCREEN_WIDTH, CUPID_SGB_SCREEN_HEIGHT)
                                 : QSize(160, 144);
    bool resizeWindow = false;
    QRect previousWindowGeometry;
    int defaultScale = 2;
    if (nativeSize != displayNativeSize_) {
        defaultScale = std::clamp(settings_.value("video/defaultScale", 2).toInt(), 1, 8);
        // Preserve custom and fullscreen geometry. The previous native size
        // survives cartridge closure because closing does not resize the window.
        resizeWindow =
            !isFullScreen() && size() == logicalWindowSize(displayNativeSize_, defaultScale);
        if (resizeWindow)
            previousWindowGeometry = QRect(pos(), size());
        displayNativeSize_ = nativeSize;
    }
    if (sgb) {
        image_ = QImage(CUPID_SGB_SCREEN_WIDTH, CUPID_SGB_SCREEN_HEIGHT, QImage::Format_RGB32);
        cupid_gb_sgb_render_with_color_table(&gb, reinterpret_cast<uint32_t *>(image_.bits()),
                                             size_t(image_.width() * image_.height()),
                                             sgbColorTable_->data());
        if (!borderAction_->isChecked())
            image_ = image_.copy(CUPID_SGB_VIEWPORT_X, CUPID_SGB_VIEWPORT_Y, 160, 144);
    } else {
        image_ = QImage(160, 144, QImage::Format_RGB32);
        const bool offImage = gb.display_frame_type == CUPID_GB_FRAME_LCD_OFF ||
                              gb.display_frame_type == CUPID_GB_FRAME_BLANK_FIRST;
        if (offImage || gb.display_frame_type == CUPID_GB_FRAME_ARTIFICIAL) {
            image_.fill(cupid_gb_model_is_cgb(gb.model) ? (*cgbColorTable_)[0x7fff]
                        : offImage                      ? displayOffColor_
                                                        : displayPalette_[0]);
        } else {
            auto *pixels = reinterpret_cast<QRgb *>(image_.bits());
            // Hardware mode stays fixed while this framebuffer is converted.
            if (gb.cgb_mode || cupid_cgb_compat_active(&gb)) {
                for (size_t i = 0; i < 160 * 144; ++i)
                    pixels[i] = (*cgbColorTable_)[gb.frame_buffer_color[i] & 0x7fffu];
            } else {
                for (size_t i = 0; i < 160 * 144; ++i)
                    pixels[i] = displayPalette_[gb.frame_buffer[i] & 3];
            }
        }
    }
    // State restores and incompatible display conversions explicitly start a
    // new history. An empty history alone does not identify a reset: a paused
    // startup image can already contain text before the first guest frame.
    if (presentation == Presentation::Reset) {
        previousDisplayFrame_ = {};
        currentDisplayFrame_ = {};
        previousMessageFrame_ = {};
        currentMessageFrame_ = {};
    }
    // Only completed LCD frames advance blend history. A restored VBlank frame
    // may seed it once so the next guest frame has a valid predecessor.
    if (presentation == Presentation::Frame) {
        previousDisplayFrame_ = currentDisplayFrame_;
        previousMessageFrame_ = currentDisplayFrame_.isNull() ? QImage() : currentMessageFrame_;
        currentDisplayFrame_ = image_;
    } else if (presentation == Presentation::Reset && gb.frame_ready) {
        currentDisplayFrame_ = image_;
    }
    // Toggling notifications clears pending text, but the submitted LCD image
    // survives until another guest frame. Its text can still blend into that
    // next frame; host settings changes must not advance this history.
    if (presentation != Presentation::Redraw)
        currentMessageFrame_ =
            display_->messages.enabled() ? display_->messages.image(image_.size()) : QImage();
    // SGB sends the image to the TV without the handheld LCD's row polarity.
    const auto mode =
        sgb && frameBlending_ == FrameBlending::Accurate ? FrameBlending::Simple : frameBlending_;
    if (currentMessageFrame_.isNull() && previousMessageFrame_.isNull()) {
        display_->setFrames(image_, previousDisplayFrame_, mode, gb.lcd_frame_odd);
    } else {
        // Draw before blending so appearing and expiring text uses the same
        // weights as LCD pixels. Repeat events retain the submitted composite.
        const QImage current = composeScreenMessage(image_, currentMessageFrame_);
        const QImage previous = composeScreenMessage(previousDisplayFrame_, previousMessageFrame_);
        display_->setFrames(current, previous, mode, gb.lcd_frame_odd);
    }
    display_->setMinimumSize(image_.size());
    if (resizeWindow)
        resizeWindowCentered(logicalWindowSize(nativeSize, defaultScale), previousWindowGeometry);
    ++presentedImages_;
    display_->update();
}

void MainWindow::configure() {
    const bool wasPaused = paused_;
    setPaused(true);
    QDialog dialog(this);
    dialog.setObjectName("settings");
    dialog.setWindowTitle("CupidGB Settings");
    auto *layout = new QVBoxLayout(&dialog);
    auto *formWidget = new QWidget;
    auto *form = new QFormLayout(formWidget);
    auto *model = new QComboBox;
    model->setObjectName("hardwareModel");
    model->addItem("Automatic", -1);
    model->addItem("Game Boy", CUPID_GB_MODEL_DMG_ABC);
    model->addItem("Game Boy (DMG0)", CUPID_GB_MODEL_DMG0);
    model->addItem("Game Boy Pocket / Light", CUPID_GB_MODEL_MGB);
    model->addItem("Game Boy Color (CPU 0)", CUPID_GB_MODEL_CGB_0);
    model->addItem("Game Boy Color (CPU A)", CUPID_GB_MODEL_CGB_A);
    model->addItem("Game Boy Color (CPU B)", CUPID_GB_MODEL_CGB_B);
    model->addItem("Game Boy Color (CPU C)", CUPID_GB_MODEL_CGB_C);
    model->addItem("Game Boy Color (CPU D)", CUPID_GB_MODEL_CGB_D);
    model->addItem("Game Boy Color (CPU E)", CUPID_GB_MODEL_CGB);
    model->addItem("Super Game Boy NTSC", CUPID_GB_MODEL_SGB);
    model->addItem("Super Game Boy PAL", CUPID_GB_MODEL_SGB_PAL);
    model->addItem("Super Game Boy 2", CUPID_GB_MODEL_SGB2);
    model->setCurrentIndex(
        model->findData(modelOverride_.value_or(settings_.value("emulation/model", -1).toInt())));
    if (modelOverride_) {
        model->setEnabled(false);
        model->setToolTip("Hardware is fixed by --model for this session. "
                          "Your saved preference is unchanged.");
    }
    form->addRow(
        modelOverride_ ? "Hardware (session override)" : "Hardware (applies on reset/open)", model);
    auto *bootRow = new QWidget;
    auto *bootLayout = new QHBoxLayout(bootRow);
    bootLayout->setContentsMargins(0, 0, 0, 0);
    auto *boot = new QLineEdit(bootDirectory());
    boot->setObjectName("bootDirectory");
    boot->setToolTip("Color CPU 0 uses cgb0_boot.bin. CPUs A through D use cgb_boot.bin. "
                     "CPU E uses cgbE_boot.bin when present, then cgb_boot.bin. "
                     "Color firmware files must contain 2304 bytes.");
    boot->setPlaceholderText("Empty: start from post-boot state");
    auto *browse = new QPushButton("Browse…");
    bootLayout->addWidget(boot);
    bootLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString path =
            QFileDialog::getExistingDirectory(&dialog, "Boot ROM directory", boot->text());
        if (!path.isEmpty())
            boot->setText(path);
    });
    form->addRow("Boot ROM directory", bootRow);
    auto *rtcMode = new QComboBox;
    rtcMode->setObjectName("rtcMode");
    rtcMode->addItem("Sync to System Clock", int(CUPID_GB_RTC_SYNC_TO_HOST));
    rtcMode->addItem("Accurate", int(CUPID_GB_RTC_ACCURATE));
    rtcMode->setCurrentIndex(rtcMode->findData(int(configuredRtcMode(settings_))));
    form->addRow("Real Time Clock:", rtcMode);
    auto *turboCap = new QComboBox;
    turboCap->setObjectName("turboSpeedCap");
    for (const unsigned quarters : turboCapChoices)
        turboCap->addItem(quarters ? QString("%1%").arg(quarters * 25) : QStringLiteral("Uncapped"),
                          quarters);
    turboCap->setCurrentIndex(turboCap->findData(turboCapQuarters_));
    turboCap->setToolTip("Upper speed while Turbo is held. Slow motion scales this limit.");
    form->addRow("Turbo speed cap", turboCap);
    auto *scaling = new QComboBox;
    scaling->setObjectName("displayScaling");
    scaling->addItems({"Integer Scale", "Retain Aspect Ratio", "Fill Window"});
    scaling->setCurrentIndex(int(display_->scaling));
    form->addRow("Display scaling", scaling);
    auto *filter = new QComboBox;
    filter->setObjectName("scalingFilter");
    filter->addItems({"Nearest Neighbor", "Bilinear", "Smooth Bilinear"});
    filter->setCurrentIndex(int(display_->filter));
    form->addRow("Scaling filter", filter);
    auto *windowScale = new QComboBox;
    windowScale->setObjectName("windowScale");
    for (int factor = 1; factor <= 8; ++factor)
        windowScale->addItem(QString("%1x").arg(factor), factor);
    windowScale->setCurrentIndex(
        std::clamp(settings_.value("video/defaultScale", 2).toInt(), 1, 8) - 1);
    form->addRow("Default window size", windowScale);
    auto *palette = new QComboBox;
    palette->setObjectName("monochromePalette");
    palette->addItems({"Lime (Game Boy)", "Grayscale", "Olive (Pocket)", "Teal (Light)", "Custom"});
    palette->setCurrentIndex(std::clamp(settings_.value("video/palette", 0).toInt(), 0, 4));
    form->addRow("Monochrome palette", palette);
    const auto defaultColors = monochromeColors(MonochromePalette::Lime, {});
    std::array<QLineEdit *, 4> customColors{};
    for (size_t i = 0; i < customColors.size(); ++i) {
        customColors[i] = new QLineEdit(
            settings_
                .value(QString("video/customPalette/%1").arg(i), QColor(defaultColors[i]).name())
                .toString());
        customColors[i]->setObjectName(QString("customPalette/%1").arg(i));
        customColors[i]->setMaxLength(7);
        customColors[i]->setPlaceholderText("#RRGGBB");
        customColors[i]->setEnabled(palette->currentIndex() == int(MonochromePalette::Custom));
        form->addRow(QString("Custom shade %1 (%2)")
                         .arg(i)
                         .arg(i == 0   ? "lightest"
                              : i == 3 ? "darkest"
                                       : "middle"),
                     customColors[i]);
    }
    auto *offColor = new QLineEdit(
        settings_.value("video/customPalette/off", customColors[0]->text()).toString());
    offColor->setObjectName("customPalette/off");
    offColor->setMaxLength(7);
    offColor->setPlaceholderText("#RRGGBB");
    offColor->setEnabled(palette->currentIndex() == int(MonochromePalette::Custom));
    form->addRow("Custom LCD off color", offColor);
    connect(palette, &QComboBox::currentIndexChanged, &dialog, [customColors, offColor](int index) {
        for (auto *color : customColors)
            color->setEnabled(index == int(MonochromePalette::Custom));
        offColor->setEnabled(index == int(MonochromePalette::Custom));
    });
    auto *blending = new QComboBox;
    blending->setObjectName("frameBlending");
    for (const char *name : frameBlendingNames)
        blending->addItem(name);
    blending->setCurrentIndex(int(configuredFrameBlending(settings_)));
    form->addRow("Frame blending", blending);
    auto *correction = new QComboBox;
    correction->setObjectName("colorCorrection");
    for (ColorCorrection mode : correctionOrder)
        correction->addItem(correctionNames[size_t(mode)], int(mode));
    correction->setCurrentIndex(correction->findData(int(colorSettings_.correction)));
    form->addRow("Color correction", correction);
    auto *temperature = new QSlider(Qt::Horizontal);
    temperature->setObjectName("colorTemperature");
    configureTemperatureSlider(temperature);
    temperature->setValue(colorSettings_.temperature);
    form->addRow("Ambient light temperature", temperature);
    auto *border = new QCheckBox("Show the Super Game Boy border");
    border->setObjectName("sgbBorder");
    border->setChecked(settings_.value("video/sgbBorder", true).toBool());
    form->addRow("SGB display", border);
    auto *messages = new QCheckBox("Show on-screen messages");
    messages->setObjectName("onScreenMessages");
    messages->setChecked(settings_.value("video/osd", false).toBool());
    form->addRow("Notifications", messages);
    auto *cameraSource = new QComboBox;
    cameraSource->setObjectName("cameraSource");
    cameraSource->addItem("System Camera", "system");
    cameraSource->addItem("Generated Pattern", "generated");
    cameraSource->setCurrentIndex(cameraSource_ == CameraSource::Generated ? 1 : 0);
    cameraSource->setToolTip("System Camera uses the operating system default device when the "
                             "cartridge requests an image. Generated Pattern does not access a "
                             "camera. Changing input cancels the current capture.");
    if (cameraSourceOverride_) {
        cameraSource->setEnabled(false);
        cameraSource->setToolTip("Set by --camera-source for this session.");
    }
    form->addRow(cameraSourceOverride_ ? "Camera input (session override)" : "Camera input",
                 cameraSource);
    auto *outputDevice = new QComboBox;
    outputDevice->setObjectName("audioOutputDevice");
    outputDevice->addItem("System Default", QByteArray());
    for (const auto &device : QMediaDevices::audioOutputs())
        outputDevice->addItem(device.description(), device.id());
    const QByteArray requestedOutput = settings_.value("audio/outputDevice").toByteArray();
    int outputIndex = outputDevice->findData(requestedOutput);
    if (outputIndex < 0) {
        outputDevice->addItem("Unavailable device (using System Default)", requestedOutput);
        outputIndex = outputDevice->count() - 1;
    }
    outputDevice->setCurrentIndex(outputIndex);
    form->addRow("Audio output", outputDevice);
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, 100);
    volume->setValue(settings_.value("audio/volume", 100).toInt());
    form->addRow("Volume", volume);
    auto *audioFilter = new QComboBox;
    audioFilter->setObjectName("audioFilterMode");
    for (size_t mode = 0; mode < audioFilterNames.size(); ++mode)
        audioFilter->addItem(audioFilterNames[mode], int(mode));
    audioFilter->setCurrentIndex(audioFilter->findData(int(configuredAudioFilter(settings_))));
    form->addRow("Highpass Filter:", audioFilter);
    auto *interferenceRow = new QWidget;
    interferenceRow->setObjectName("audioInterferenceRow");
    auto *interferenceLayout = new QHBoxLayout(interferenceRow);
    interferenceLayout->setContentsMargins(0, 0, 0, 0);
    auto *interference = new QSlider(Qt::Horizontal);
    interference->setObjectName("audioInterferenceVolume");
    interference->setRange(0, 100);
    interference->setSingleStep(5);
    interference->setPageStep(5);
    interference->setValue(configuredAudioInterference(settings_));
    auto *interferenceValue = new QLabel(QString("%1%").arg(interference->value()));
    interferenceValue->setObjectName("audioInterferenceVolumeValue");
    interferenceValue->setMinimumWidth(interferenceValue->fontMetrics().horizontalAdvance("100%"));
    connect(
        interference, &QSlider::valueChanged, interferenceValue,
        [interferenceValue](int value) { interferenceValue->setText(QString("%1%").arg(value)); });
    interferenceLayout->addWidget(interference);
    interferenceLayout->addWidget(interferenceValue);
    form->addRow("Interference Volume:", interferenceRow);
    auto *rewindLength = new QComboBox;
    for (const int seconds : {0, 10, 30, 60, 120, 300, 600})
        rewindLength->addItem(seconds ? QString("Up to %1 seconds").arg(seconds) : "Disabled",
                              seconds);
    rewindLength->setCurrentIndex(
        rewindLength->findData(settings_.value("emulation/rewindSeconds", 120).toInt()));
    form->addRow("Rewind history", rewindLength);
    auto *rewindBudget = new QComboBox;
    for (const int memory : {16, 32, 64, 128, 256})
        rewindBudget->addItem(QString("%1 MiB").arg(memory), memory);
    rewindBudget->setCurrentIndex(
        rewindBudget->findData(settings_.value("emulation/rewindMiB", 256).toInt()));
    form->addRow("Rewind memory limit", rewindBudget);
    const double available =
        rewindStates_.size() < 2
            ? 0.0
            : double(rewindStates_.back().cycles - rewindStates_.front().cycles) /
                  cupid_gb_base_clock_hz(emulator_ ? &emulator_->gb : nullptr);
    form->addRow("Current retained history",
                 new QLabel(QString("%1 seconds, %2 MiB")
                                .arg(available, 0, 'f', 1)
                                .arg(double(rewindBytes_) / (1024 * 1024), 0, 'f', 1)));
    std::array<QKeySequenceEdit *, 8> edits;
    for (size_t i = 0; i < edits.size(); ++i) {
        edits[i] = new InputKeyEdit(keys_[i]);
        edits[i]->setObjectName(QString("input/%1").arg(i));
        form->addRow(buttonNames[i], edits[i]);
    }
    std::array<QKeySequenceEdit *, 8> linkedEdits;
    for (size_t i = 0; i < linkedEdits.size(); ++i) {
        linkedEdits[i] = new InputKeyEdit(linkedKeys_[i]);
        linkedEdits[i]->setObjectName(QString("input/linkPlayer2/%1").arg(i));
        form->addRow(QString("Link player 2: %1").arg(buttonNames[i]), linkedEdits[i]);
    }
    std::array<QKeySequenceEdit *, 5> specialEdits;
    for (size_t i = 0; i < specialEdits.size(); ++i) {
        specialEdits[i] = new InputKeyEdit(specialKeys_[i]);
        specialEdits[i]->setObjectName(QString("input/special/%1").arg(i));
        form->addRow(specialNames[i], specialEdits[i]);
    }
    std::vector<QKeySequenceEdit *> commandEdits;
    for (auto *action : shortcutActions_) {
        auto *edit = new QKeySequenceEdit(action->shortcut());
        edit->setObjectName("shortcut/" + action->objectName());
        commandEdits.push_back(edit);
        QString label = action->text();
        label.remove('&');
        if (action->objectName().startsWith("save/"))
            label.prepend("Save ");
        if (action->objectName().startsWith("load/"))
            label.prepend("Load ");
        form->addRow(label, edit);
    }
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(formWidget);
    layout->addWidget(scroll);
    auto *note =
        new QLabel("Choose one key per button or mode. Empty mappings disable an input.\n"
                   "Command shortcuts may include modifiers. Each mapping must be unique.\n"
                   "Link player 2 controls the other game from the focused window.\n"
                   "Rewind duration is bounded by the selected memory limit.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::array<QString, 4> colorNames{};
        for (size_t i = 0; i < customColors.size(); ++i) {
            const QString text = customColors[i]->text().trimmed();
            const QColor color(text);
            if (text.size() != 7 || !text.startsWith('#') || !color.isValid()) {
                QMessageBox::warning(
                    &dialog, "Display",
                    "Enter each custom color as #RRGGBB using hexadecimal digits.");
                return;
            }
            colorNames[i] = color.name();
        }
        const QString offColorName = offColor->text().trimmed();
        const QColor offColorValue(offColorName);
        if (offColorName.size() != 7 || !offColorName.startsWith('#') || !offColorValue.isValid()) {
            QMessageBox::warning(&dialog, "Display", "Enter the LCD off color as #RRGGBB.");
            return;
        }
        std::array<int, 8> values{};
        std::array<int, 8> linkedValues{};
        std::array<int, 5> specialValues{};
        std::vector<int> used;
        auto readInput = [&](QKeySequenceEdit *edit, int &value) {
            const auto sequence = edit->keySequence();
            if (sequence.isEmpty()) {
                value = 0;
                return true;
            }
            const auto key = sequence[0].key();
            const auto modifiers = sequence[0].keyboardModifiers();
            if (sequence.count() != 1 || key == Qt::Key_unknown ||
                (modifiers != Qt::NoModifier && modifiers != keyModifier(key)) ||
                std::find(used.begin(), used.end(), int(key)) != used.end())
                return false;
            value = int(key);
            used.push_back(value);
            return true;
        };
        for (size_t i = 0; i < edits.size(); ++i)
            if (!readInput(edits[i], values[i])) {
                QMessageBox::warning(&dialog, "Input",
                                     "Choose unique single keys for buttons and modes.");
                return;
            }
        for (size_t i = 0; i < linkedEdits.size(); ++i)
            if (!readInput(linkedEdits[i], linkedValues[i])) {
                QMessageBox::warning(&dialog, "Input",
                                     "Choose unique single keys for both link players and modes.");
                return;
            }
        for (size_t i = 0; i < specialEdits.size(); ++i)
            if (!readInput(specialEdits[i], specialValues[i])) {
                QMessageBox::warning(&dialog, "Input",
                                     "Choose unique single keys for buttons and modes.");
                return;
            }
        std::vector<QKeySequence> commands;
        for (auto *edit : commandEdits) {
            const auto sequence = edit->keySequence();
            if (!sequence.isEmpty() &&
                (sequence.count() != 1 ||
                 std::find(commands.begin(), commands.end(), sequence) != commands.end() ||
                 (sequence[0].keyboardModifiers() == Qt::NoModifier &&
                  std::find(used.begin(), used.end(), int(sequence[0].key())) != used.end()))) {
                QMessageBox::warning(
                    &dialog, "Input",
                    "Command shortcuts must be unique and cannot overlap button or mode keys.");
                return;
            }
            commands.push_back(sequence);
        }
        keys_ = values;
        linkedKeys_ = linkedValues;
        specialKeys_ = specialValues;
        for (size_t i = 0; i < values.size(); ++i)
            settings_.setValue(QString("input/%1").arg(i), values[i]);
        for (size_t i = 0; i < linkedValues.size(); ++i)
            settings_.setValue(QString("input/linkPlayer2/%1").arg(i), linkedValues[i]);
        for (size_t i = 0; i < specialValues.size(); ++i)
            settings_.setValue(QString("input/special/%1").arg(i), specialValues[i]);
        for (size_t i = 0; i < commands.size(); ++i) {
            shortcutActions_[i]->setShortcut(commands[i]);
            settings_.setValue("shortcuts/" + shortcutActions_[i]->objectName(),
                               commands[i].toString(QKeySequence::PortableText));
        }
        synchronizeInputBindings();
        if (!modelOverride_)
            settings_.setValue("emulation/model", model->currentData());
        const auto clockMode = CupidGbRtcMode(rtcMode->currentData().toInt());
        settings_.setValue("emulation/rtcMode", int(clockMode));
        if (emulator_)
            cupid_gb_set_rtc_mode(&emulator_->gb, clockMode);
        setTurboCap(turboCap->currentData().toUInt());
        settings_.setValue("emulation/bootDirectory", boot->text());
        bootDirectoryOverride_ = QString();
        settings_.setValue("video/scaling", scaling->currentIndex());
        settings_.setValue("video/filter", filter->currentIndex());
        const int oldScale = settings_.value("video/defaultScale", 2).toInt();
        settings_.setValue("video/defaultScale", windowScale->currentData());
        settings_.setValue("video/palette", palette->currentIndex());
        settings_.setValue("video/blendingMode", blending->currentIndex());
        settings_.setValue("video/colorCorrection", correction->currentData());
        settings_.setValue("video/colorTemperature", temperature->value());
        settings_.setValue("video/sgbBorder", border->isChecked());
        settings_.setValue("video/osd", messages->isChecked());
        for (size_t i = 0; i < colorNames.size(); ++i)
            settings_.setValue(QString("video/customPalette/%1").arg(i), colorNames[i]);
        settings_.setValue("video/customPalette/off", offColorValue.name());
        if (!cameraSourceOverride_) {
            settings_.setValue("camera/source", cameraSource->currentData());
            const auto selected =
                cameraSource->currentIndex() == 1 ? CameraSource::Generated : CameraSource::System;
            if (selected != cameraSource_) {
                cancelCameraCapture();
                cameraError_.clear();
                cameraSource_ = selected;
                if (emulator_)
                    applyCameraSource(&emulator_->gb);
            }
        }
        const bool outputChanged = requestedOutput != outputDevice->currentData().toByteArray();
        settings_.setValue("audio/outputDevice", outputDevice->currentData().toByteArray());
        settings_.setValue("audio/volume", volume->value());
        settings_.setValue("audio/highpassMode", audioFilter->currentData());
        settings_.setValue("audio/interferenceVolume", interference->value());
        if (emulator_)
            applyAudioPolicy(&emulator_->gb);
        if (settings_.value("emulation/rewindSeconds", 120) != rewindLength->currentData() ||
            settings_.value("emulation/rewindMiB", 256) != rewindBudget->currentData()) {
            settings_.setValue("emulation/rewindSeconds", rewindLength->currentData());
            settings_.setValue("emulation/rewindMiB", rewindBudget->currentData());
            resetRewind();
            rememberFrame();
        }
        applyDisplaySettings();
        if (oldScale != windowScale->currentData().toInt())
            resizeDisplay(windowScale->currentData().toInt());
        if (outputChanged && emulator_)
            setupAudio();
        else if (audio_)
            audio_->setVolume(muteAction_->isChecked() ? 0.0f : float(volume->value()) / 100);
        dialog.accept();
    });
    dialog.resize(620, std::min(780, screen()->availableGeometry().height() - 80));
    dialog.exec();
    setPaused(wasPaused);
}

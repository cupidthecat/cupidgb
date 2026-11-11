#include "main_window_internal.h"

#include <QDebug>

extern "C" {
#include "cupid/gb/ppu.h"
}

bool MainWindow::modelSmokeTest(const QString &directory) {
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    const auto writeFixture = [&](const QString &name, uint8_t color, uint8_t licensee,
                                  uint8_t sgb = 0) {
        rom[0x143] = char(color);
        rom[0x146] = char(sgb);
        rom[0x14b] = char(licensee);
        uint8_t checksum = 0;
        for (int i = 0x134; i <= 0x14c; ++i)
            checksum = uint8_t(checksum - uint8_t(rom[i]) - 1);
        rom[0x14d] = char(checksum);
        const QString path = QDir(directory).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
            return QString();
        return path;
    };
    const QString plain = writeFixture("model-plain.gb", 0, 0);
    const QString licensed = writeFixture("model-licensed.gb", 0, 1);
    const QString colorOnly = writeFixture("model-color-only.gbc", 0xc0, 1);
    const QString enhanced = writeFixture("model-enhanced.gb", 0, 1, 3);
    if (plain.isEmpty() || licensed.isEmpty() || colorOnly.isEmpty() || enhanced.isEmpty())
        return smokeFailure(__FILE__, __LINE__);
    const bool hadModel = settings_.contains("emulation/model");
    const QVariant savedModel = settings_.value("emulation/model");
    settings_.setValue("emulation/bootDirectory", QString());
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    {
        MainWindow forced(true, false, false, QString(), CUPID_GB_MODEL_DMG_ABC);
        if (!forced.openRom(licensed) || forced.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC ||
            !forced.restart() || forced.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC)
            return smokeFailure(__FILE__, __LINE__);
        for (const bool accept : {false, true}) {
            bool checked = false;
            QTimer command;
            command.setInterval(1);
            connect(&command, &QTimer::timeout, &forced, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "settings")
                    return;
                command.stop();
                auto *model = dialog->findChild<QComboBox *>("hardwareModel");
                checked = model && !model->isEnabled() &&
                          model->currentData().toInt() == CUPID_GB_MODEL_DMG_ABC;
                // Disabled controls must not become a persisted override even
                // if another UI path changes the selection programmatically.
                if (model)
                    model->setCurrentIndex(model->findData(int(CUPID_GB_MODEL_CGB)));
                dialog->findChild<QDialogButtonBox *>()
                    ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
                    ->click();
            });
            command.start();
            forced.configure();
            if (!checked || settings_.value("emulation/model").toInt() != CUPID_GB_MODEL_CGB ||
                !forced.restart() || forced.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC)
                return smokeFailure(__FILE__, __LINE__);
        }
        const QByteArray beforeRejected = forced.captureState();
        if (forced.openRom(colorOnly) || forced.captureState() != beforeRejected ||
            !forced.closeRom() || !forced.openRom(plain) ||
            forced.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC)
            return smokeFailure(__FILE__, __LINE__);
    }
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
    {
        MainWindow automatic(true, false, false, QString(), -1);
        if (!automatic.openRom(licensed) || automatic.emulator_->gb.model != CUPID_GB_MODEL_CGB ||
            !automatic.restart() || automatic.emulator_->gb.model != CUPID_GB_MODEL_CGB ||
            settings_.value("emulation/model").toInt() != CUPID_GB_MODEL_DMG_ABC ||
            !automatic.openRom(plain) || automatic.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC ||
            !automatic.openRom(colorOnly) || automatic.emulator_->gb.model != CUPID_GB_MODEL_CGB)
            return smokeFailure(__FILE__, __LINE__);
    }
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_CGB));
    {
        MainWindow saved(true);
        if (!saved.openRom(plain) || saved.emulator_->gb.model != CUPID_GB_MODEL_CGB)
            return smokeFailure(__FILE__, __LINE__);
    }
    const QVariant savedScale = settings_.value("video/defaultScale", 2);
    const QVariant savedBorder = settings_.value("video/sgbBorder", true);
    settings_.setValue("video/defaultScale", 2);
    settings_.setValue("video/sgbBorder", true);
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
    {
        MainWindow hidden(true);
        const QPoint hiddenPosition = hidden.pos();
        hidden.resizeDisplay(3);
        if (hidden.pos() != hiddenPosition)
            return smokeFailure(__FILE__, __LINE__);
        MainWindow sized(true);
        sized.show();
        QApplication::processEvents();
        const QRect available = sized.screen()->availableGeometry();
        sized.move(available.center() - QPoint(sized.width() / 2, sized.height() / 2));
        QApplication::processEvents();
        const auto doubledCenter = [&] {
            return QPoint(sized.x() * 2 + sized.width(), sized.y() * 2 + sized.height());
        };
        const auto centerPreserved = [&](const QPoint &previous) {
            const QPoint difference = doubledCenter() - previous;
            return std::abs(difference.x()) <= 2 && std::abs(difference.y()) <= 2;
        };
        if (!sized.openRom(enhanced))
            return smokeFailure(__FILE__, __LINE__);
        sized.setPaused(true);
        const QSize handheldSize = sized.size();
        const QPoint handheldCenter = doubledCenter();
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_SGB2));
        if (!sized.restart() || sized.image_.size() != QSize(256, 224) ||
            !centerPreserved(handheldCenter))
            return smokeFailure(__FILE__, __LINE__);
        sized.setPaused(true);
        const QSize automaticBorderedSize = sized.size();
        const QPoint borderedCenter = doubledCenter();
        sized.resizeDisplay(3);
        if (!centerPreserved(borderedCenter))
            return smokeFailure(__FILE__, __LINE__);
        const QPoint enlargedCenter = doubledCenter();
        sized.resizeDisplay(2);
        if (sized.size() != automaticBorderedSize ||
            sized.size() != sized.logicalWindowSize(QSize(256, 224), 2) ||
            sized.size().height() <= handheldSize.height() || !centerPreserved(enlargedCenter))
            return smokeFailure(__FILE__, __LINE__);
        sized.borderAction_->trigger();
        if (sized.image_.size() != QSize(160, 144) || sized.size() != handheldSize)
            return smokeFailure(__FILE__, __LINE__);
        sized.resizeDisplay(2);
        if (sized.size() != handheldSize)
            return smokeFailure(__FILE__, __LINE__);
        sized.borderAction_->trigger();
        if (sized.size() != automaticBorderedSize)
            return smokeFailure(__FILE__, __LINE__);
        settings_.setValue("video/defaultScale", 1);
        sized.resizeDisplay(1);
        const QPoint smallBorderedCenter = doubledCenter();
        const QRect smallBorderedGeometry(sized.pos(), sized.size());
        const QSize smallBorderedMinimum = sized.minimumSize();
        const QSize smallBorderedHint = sized.minimumSizeHint();
        const QSize smallBorderedLayoutMinimum = sized.layout()->minimumSize();
        sized.borderAction_->trigger();
        if (sized.size() != sized.logicalWindowSize(QSize(160, 144), 1) ||
            !centerPreserved(smallBorderedCenter)) {
            qWarning() << "Window sizing before border hide:" << smallBorderedGeometry << "minimum"
                       << smallBorderedMinimum << "minimum hint" << smallBorderedHint
                       << "layout minimum" << smallBorderedLayoutMinimum;
            qWarning() << "Window sizing after border hide:" << QRect(sized.pos(), sized.size())
                       << "expected" << sized.logicalWindowSize(QSize(160, 144), 1)
                       << "center before/after" << smallBorderedCenter << doubledCenter() << "DPR"
                       << sized.devicePixelRatioF() << "minimum" << sized.minimumSize()
                       << "minimum hint" << sized.minimumSizeHint() << "layout minimum"
                       << sized.layout()->minimumSize() << "layout geometry"
                       << sized.layout()->geometry() << "display minimum/actual"
                       << sized.display_->minimumSize() << sized.display_->size()
                       << "menu/status hints" << sized.menuBar()->sizeHint()
                       << sized.statusBar()->sizeHint();
            return smokeFailure(__FILE__, __LINE__);
        }
        const QPoint smallHandheldCenter = doubledCenter();
        sized.borderAction_->trigger();
        if (sized.size() != sized.logicalWindowSize(QSize(256, 224), 1) ||
            !centerPreserved(smallHandheldCenter))
            return smokeFailure(__FILE__, __LINE__);
        settings_.setValue("video/defaultScale", 2);
        sized.resizeDisplay(2);
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
        if (!sized.openRom(enhanced) || sized.size() != handheldSize)
            return smokeFailure(__FILE__, __LINE__);
        sized.setPaused(true);
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_SGB2));
        if (!sized.openRom(enhanced) || sized.size() != automaticBorderedSize)
            return smokeFailure(__FILE__, __LINE__);
        sized.setPaused(true);
        const QSize customSize = automaticBorderedSize + QSize(73, 61);
        sized.resize(customSize);
        const QPoint customPosition = sized.pos();
        sized.borderAction_->trigger();
        if (sized.size() != customSize || sized.pos() != customPosition)
            return smokeFailure(__FILE__, __LINE__);
        sized.borderAction_->trigger();
        if (sized.size() != customSize || sized.pos() != customPosition)
            return smokeFailure(__FILE__, __LINE__);
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
        if (!sized.restart() || sized.size() != customSize || sized.pos() != customPosition)
            return smokeFailure(__FILE__, __LINE__);
        sized.setPaused(true);
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_SGB2));
        if (!sized.restart() || sized.size() != customSize || sized.pos() != customPosition)
            return smokeFailure(__FILE__, __LINE__);
        sized.setPaused(true);
        auto *fullscreen = sized.findChild<QAction *>("fullscreen");
        if (!fullscreen)
            return smokeFailure(__FILE__, __LINE__);
        fullscreen->trigger();
        const QPoint fullscreenPosition = sized.pos();
        sized.borderAction_->trigger();
        if (!sized.isFullScreen() || !fullscreen->isChecked() || sized.pos() != fullscreenPosition)
            return smokeFailure(__FILE__, __LINE__);
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
        if (!sized.restart() || !sized.isFullScreen() || !fullscreen->isChecked() ||
            sized.pos() != fullscreenPosition)
            return smokeFailure(__FILE__, __LINE__);
        sized.setPaused(true);
        fullscreen->trigger();
        sized.resizeDisplay(2);
        settings_.setValue("video/sgbBorder", true);
        sized.applyDisplaySettings();
        settings_.setValue("emulation/model", int(CUPID_GB_MODEL_SGB2));
        if (!sized.restart() || sized.size() != automaticBorderedSize || !sized.closeRom())
            return smokeFailure(__FILE__, __LINE__);
        const QPoint beforeEmptyShrink = doubledCenter();
        sized.resizeDisplay(1);
        if (sized.size() != sized.logicalWindowSize(QSize(160, 144), 1) ||
            !centerPreserved(beforeEmptyShrink))
            return smokeFailure(__FILE__, __LINE__);
        // Explicit scaling while empty uses the handheld canvas, and the next
        // cartridge must compare against that new size rather than stale SGB dimensions.
        sized.resizeDisplay(2);
        if (!sized.openRom(enhanced) || sized.size() != automaticBorderedSize)
            return smokeFailure(__FILE__, __LINE__);
        sized.setPaused(true);
    }
    settings_.setValue("video/defaultScale", savedScale);
    settings_.setValue("video/sgbBorder", savedBorder);
    if (hadModel)
        settings_.setValue("emulation/model", savedModel);
    else
        settings_.remove("emulation/model");
    return true;
}

bool MainWindow::pacingSmokeTest(const QString &path) {
    for (const auto model : {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2}) {
        settings_.setValue("emulation/model", int(model));
        if (!openRom(path))
            return smokeFailure(__FILE__, __LINE__);
        auto &gb = emulator_->gb;
        const uint32_t clockHz = cupid_gb_base_clock_hz(&gb);
        // Approach VBlank using real hardware steps so the next image covers only
        // a scanline tail. A presentation-count deadline would overcharge it.
        unsigned steps = 0;
        while ((gb.pixels.line != 143 || gb.pixels.dot < 440) && steps++ < 70000) {
            if (!cupid_gb_step(&gb))
                return smokeFailure(__FILE__, __LINE__);
        }
        if (steps >= 70000)
            return smokeFailure(__FILE__, __LINE__);
        setPaused(false);
        const uint64_t partialStart = gb.elapsed_cycles;
        const qint64 partialDeadline = nextFrame_;
        tick();
        const uint64_t partialCycles = gb.elapsed_cycles - partialStart;
        if (paused_ || !partialCycles || partialCycles >= 70224 ||
            nextFrame_ != partialDeadline + qint64(partialCycles * 1000000000 / clockHz))
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_write_u8(&gb, 0xff40, 0);
        resetPacing();
        const uint64_t blankStart = gb.elapsed_cycles;
        const uint32_t blankCounter = gb.display_cycles_since_frame;
        const qint64 blankDeadline = nextFrame_;
        tick();
        const uint64_t blankCycles = gb.elapsed_cycles - blankStart;
        if (paused_ || !gb.frame_ready || gb.display_frame_type != CUPID_GB_FRAME_LCD_OFF ||
            blankCycles + blankCounter != 70224u + gb.display_cycles_since_frame ||
            nextFrame_ != blankDeadline + qint64(blankCycles * 1000000000 / clockHz))
            return smokeFailure(__FILE__, __LINE__);
        if (model == CUPID_GB_MODEL_SGB) {
            const auto priorDuration = settings_.value("emulation/rewindSeconds", 120);
            settings_.setValue("emulation/rewindSeconds", 1);
            resetRewind();
            rememberFrame();
            for (unsigned frame = 0; frame < 64; ++frame)
                if (!runFrame())
                    return smokeFailure(__FILE__, __LINE__);
            if (rewindFailed_ || rewindStates_.size() < 2)
                return smokeFailure(__FILE__, __LINE__);
            const uint64_t retained = rewindStates_.back().cycles - rewindStates_.front().cycles;
            if (retained > clockHz || retained <= clockHz - 70224)
                return smokeFailure(__FILE__, __LINE__);
            settings_.setValue("emulation/rewindSeconds", priorDuration);
        }
        if (!closeRom())
            return smokeFailure(__FILE__, __LINE__);
    }
    return displayCadenceSmokeTest(path);
}

bool MainWindow::displayCadenceSmokeTest(const QString &path) {
    for (const auto model : {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB}) {
        MainWindow session(true, false, false, QStringLiteral(""), model);
        if (!session.openRom(path))
            return smokeFailure(__FILE__, __LINE__);
        session.setPaused(true);
        session.displayPalette_ = monochromeColors(MonochromePalette::Grayscale, {});
        session.displayOffColor_ = qRgb(210, 230, 166);
        session.setFrameBlending(FrameBlending::Accurate);
        auto &gb = session.emulator_->gb;
        cupid_gb_write_u8(&gb, 0xff47, 0xe4);
        for (unsigned row = 0; row < 8; ++row) {
            gb.video_ram[row * 2] = 0xff;
            gb.video_ram[row * 2 + 1] = 0xff;
        }
        session.previousDisplayFrame_ = {};
        session.currentDisplayFrame_ = {};
        if (!session.runFrame() || gb.display_frame_type != CUPID_GB_FRAME_NORMAL)
            return smokeFailure(__FILE__, __LINE__);
        const QImage lastActive = session.image_;
        cupid_gb_write_u8(&gb, 0xff40, 0);
        const uint64_t beforeIdle = gb.elapsed_cycles;
        const uint32_t beforeCounter = gb.display_cycles_since_frame;
        if (!session.runFrame() || gb.display_frame_type != CUPID_GB_FRAME_LCD_OFF ||
            gb.elapsed_cycles - beforeIdle + beforeCounter !=
                70224u + gb.display_cycles_since_frame)
            return smokeFailure(__FILE__, __LINE__);
        const QRgb offColor = model == CUPID_GB_MODEL_CGB ? (*session.cgbColorTable_)[0x7fff]
                                                          : session.displayOffColor_;
        if (session.image_.pixel(0, 0) != offColor || session.image_.pixel(159, 143) != offColor ||
            session.previousDisplayFrame_ != lastActive ||
            session.display_->image != blendDisplayFrames(session.image_, lastActive,
                                                          FrameBlending::Accurate,
                                                          gb.lcd_frame_odd))
            return smokeFailure(__FILE__, __LINE__);
        if (!session.runFrame() || session.display_->image != session.image_ ||
            session.previousDisplayFrame_ != session.image_)
            return smokeFailure(__FILE__, __LINE__);
        const bool savedPhase = gb.lcd_frame_odd;
        if (!session.saveSlot(7) || !session.runFrame() || gb.lcd_frame_odd == savedPhase ||
            !session.loadSlot(7) || gb.display_frame_type != CUPID_GB_FRAME_LCD_OFF ||
            gb.lcd_frame_odd != savedPhase || session.image_.pixel(0, 0) != offColor ||
            session.display_->image != session.image_)
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_write_u8(&gb, 0xff40, 0x91);
        if (!session.runFrame() || gb.display_frame_type != CUPID_GB_FRAME_BLANK_FIRST ||
            session.image_.pixel(0, 0) != offColor || !session.runFrame() ||
            gb.display_frame_type != CUPID_GB_FRAME_NORMAL)
            return smokeFailure(__FILE__, __LINE__);
        if (model == CUPID_GB_MODEL_CGB) {
            const QImage lastImage = session.image_;
            const QImage lastVisible = session.display_->image;
            const QImage currentHistory = session.currentDisplayFrame_;
            const QImage previousHistory = session.previousDisplayFrame_;
            const uint64_t images = session.presentedImages_;
            std::memset(gb.video_ram, 0, 16);
            cupid_gb_write_u8(&gb, 0xff40, 0);
            cupid_gb_write_u8(&gb, 0xff40, 0x91);
            if (!session.runFrame() || gb.display_frame_type != CUPID_GB_FRAME_REPEAT ||
                session.image_ != lastImage || session.display_->image != lastVisible ||
                session.currentDisplayFrame_ != currentHistory ||
                session.previousDisplayFrame_ != previousHistory ||
                session.presentedImages_ != images)
                return smokeFailure(__FILE__, __LINE__);
            if (!session.runFrame() || gb.display_frame_type != CUPID_GB_FRAME_NORMAL ||
                session.previousDisplayFrame_ != currentHistory ||
                session.presentedImages_ != images + 1)
                return smokeFailure(__FILE__, __LINE__);
        }
    }
    const QVariant savedPalette = settings_.value("video/palette", 0);
    const bool hadOffColor = settings_.contains("video/customPalette/off");
    const QVariant savedOffColor = settings_.value("video/customPalette/off");
    settings_.setValue("video/palette", int(MonochromePalette::Custom));
    settings_.remove("video/customPalette/off");
    {
        MainWindow custom(true);
        const auto originalShades = custom.displayPalette_;
        if (custom.displayOffColor_ != originalShades[0])
            return smokeFailure(__FILE__, __LINE__);
        for (const bool accept : {false, true}) {
            bool checked = false;
            QTimer::singleShot(0, &custom, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                auto *off = dialog->findChild<QLineEdit *>("customPalette/off");
                checked =
                    off && off->isEnabled() && off->text() == QColor(originalShades[0]).name();
                if (off)
                    off->setText("#2468ac");
                dialog->findChild<QDialogButtonBox *>()
                    ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
                    ->click();
            });
            custom.configure();
            if (!checked || custom.displayPalette_ != originalShades ||
                settings_.contains("video/customPalette/off") != accept ||
                custom.displayOffColor_ != (accept ? qRgb(36, 104, 172) : originalShades[0]))
                return smokeFailure(__FILE__, __LINE__);
        }
        MainWindow restored(true);
        if (restored.displayOffColor_ != qRgb(36, 104, 172) ||
            restored.displayPalette_ != originalShades)
            return smokeFailure(__FILE__, __LINE__);
    }
    settings_.setValue("video/palette", savedPalette);
    if (hadOffColor)
        settings_.setValue("video/customPalette/off", savedOffColor);
    else
        settings_.remove("video/customPalette/off");
    return true;
}

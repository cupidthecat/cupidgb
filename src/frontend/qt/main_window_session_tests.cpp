#include "main_window_internal.h"

bool MainWindow::modelSmokeTest(const QString &directory) {
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    const auto writeFixture = [&](const QString &name, uint8_t color, uint8_t licensee) {
        rom[0x143] = char(color);
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
    if (plain.isEmpty() || licensed.isEmpty() || colorOnly.isEmpty())
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
        const qint64 blankDeadline = nextFrame_;
        tick();
        const uint64_t blankCycles = gb.elapsed_cycles - blankStart;
        if (paused_ || blankCycles != 70224 ||
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
    return true;
}

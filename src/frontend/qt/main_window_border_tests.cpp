/**
 * @file    main_window_border_tests.cpp
 * @brief   Qt border modes, isolated donor capture, cache ownership, and crop regressions.
 * @author  cupidthecat
 * @date    2026-11-24
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

#include <QScopeGuard>
#include <QThread>

extern "C" {
#include "cupid/gb/sgb_border.h"
}

namespace {
bool writeBorderFixture(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

void updateBorderFixtureChecksum(QByteArray &rom) {
    uint8_t checksum = 0;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        checksum = uint8_t(checksum - uint8_t(rom[qsizetype(address)]) - 1);
    rom[0x14d] = char(checksum);
}

QByteArray borderDonorFixture() {
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0xc3);
    rom[0x101] = char(0x50);
    rom[0x102] = 1;
    rom.replace(0x134, 9, QByteArray("SGB FRAME"));
    rom[0x146] = 3;
    rom[0x14b] = 0x33;
    updateBorderFixtureChecksum(rom);
    QByteArray code;
    const auto append = [&](std::initializer_list<uint8_t> bytes) {
        for (const auto byte : bytes)
            code.append(char(byte));
    };
    const auto io = [&](uint8_t address, uint8_t value) { append({0x3e, value, 0xe0, address}); };
    const auto packet = [&](uint8_t command) {
        io(0, 0);
        io(0, 0x30);
        for (unsigned byte = 0; byte < 16; ++byte) {
            const uint8_t value = byte ? 0 : command;
            for (unsigned bit = 0; bit < 8; ++bit) {
                io(0, value & (1u << bit) ? 0x10 : 0x20);
                io(0, 0x30);
            }
        }
        io(0, 0x20);
        io(0, 0x30);
    };
    const auto branch = [&](uint8_t opcode, qsizetype target) {
        const auto displacement = target - code.size() - 2;
        if (displacement < -128 || displacement > 127)
            return false;
        append({opcode, uint8_t(displacement)});
        return true;
    };
    io(0x40, 0);
    append({0x21, 0, 0x80});
    for (unsigned repeat = 0; repeat < 2; ++repeat)
        for (const auto value : {0x10, 0x08, 0x02, 0x20, 0x40, 0x01, 0x08, 0x80})
            append({0x36, uint8_t(value), 0x23});
    io(0x47, 0xe4);
    io(0x40, 0x91);
    packet(0x99);
    append({0x06, 4});
    const auto visible = code.size();
    append({0xf0, 0x44, 0xfe, 144});
    if (!branch(0x30, visible))
        return {};
    const auto vblank = code.size();
    append({0xf0, 0x44, 0xfe, 144});
    if (!branch(0x38, vblank))
        return {};
    append({0x05});
    if (!branch(0x20, visible))
        return {};
    packet(0xa1);
    if (!branch(0x18, code.size()))
        return {};
    rom.replace(0x150, code.size(), code);
    return rom;
}

QByteArray borderFirmwareFixture() {
    QByteArray boot(256, char(0));
    boot[0] = char(0xc3);
    boot[1] = char(0xfc);
    boot[0xfc] = char(0x3e);
    boot[0xfd] = 1;
    boot[0xfe] = char(0xe0);
    boot[0xff] = char(0x50);
    return boot;
}

bool blackBorderOutsideViewport(const QImage &image) {
    if (image.size() != QSize(256, 224))
        return false;
    const QRect viewport(48, 40, 160, 144);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (!viewport.contains(x, y) && image.pixel(x, y) != qRgb(0, 0, 0))
                return false;
    return true;
}
} // namespace

bool MainWindow::sgbBorderModesSmokeTest(const QString &directory) {
    std::vector<std::pair<QString, QVariant>> preferences;
    for (const QString &key : settings_.allKeys())
        preferences.emplace_back(key, settings_.value(key));
    const auto restorePreferences = qScopeGuard([&] {
        settings_.clear();
        for (const auto &[key, value] : preferences)
            settings_.setValue(key, value);
    });
    QTemporaryDir fixtures(QDir(directory).filePath("border-modes-XXXXXX"));
    if (!fixtures.isValid())
        return smokeFailure(__FILE__, __LINE__);
    const auto donor = borderDonorFixture();
    auto plain = donor;
    if (donor.isEmpty())
        return smokeFailure(__FILE__, __LINE__);
    plain[0x146] = 0;
    updateBorderFixtureChecksum(plain);
    const QString donorPath = fixtures.filePath("enhanced.gb");
    const QString plainPath = fixtures.filePath("plain.gb");
    const QString secondPath = fixtures.filePath("enhanced-copy.gb");
    const auto firmware = borderFirmwareFixture();
    if (!writeBorderFixture(donorPath, donor) || !writeBorderFixture(plainPath, plain) ||
        !writeBorderFixture(secondPath, donor) ||
        !writeBorderFixture(fixtures.filePath("dmg_boot.bin"), firmware) ||
        !writeBorderFixture(fixtures.filePath("mgb_boot.bin"), firmware) ||
        !writeBorderFixture(fixtures.filePath("sgb_boot.bin"), firmware))
        return smokeFailure(__FILE__, __LINE__);

    const auto wait = [](const auto &complete) {
        QElapsedTimer deadline;
        deadline.start();
        while (!complete() && deadline.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(1);
        }
        return complete();
    };
    const auto modeAction = [](MainWindow &window, SgbBorderMode mode) {
        auto *action = window.sgbBorderActions_[size_t(mode)];
        if (!action || !action->isEnabled())
            return false;
        action->trigger();
        return action->isChecked() && window.sgbBorderMode_ == mode &&
               window.settings_.value("video/sgbBorderMode").toInt() == int(mode);
    };
    for (const bool legacy : {false, true}) {
        settings_.remove("video/sgbBorderMode");
        settings_.setValue("video/sgbBorder", legacy);
        MainWindow migrated(true, true);
        migrated.timer_.stop();
        const auto expected = legacy ? SgbBorderMode::SgbOnly : SgbBorderMode::Never;
        if (migrated.sgbBorderMode_ != expected ||
            !migrated.sgbBorderActions_[size_t(expected)]->isChecked())
            return smokeFailure(__FILE__, __LINE__);
    }
    settings_.setValue("video/sgbBorder", false);
    settings_.setValue("video/sgbBorderMode", int(SgbBorderMode::Always));
    {
        MainWindow configured(true, true);
        configured.timer_.stop();
        bool selected = false;
        QTimer driver;
        driver.setInterval(1);
        connect(&driver, &QTimer::timeout, &configured, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "settings")
                return;
            driver.stop();
            auto *choice = dialog->findChild<QComboBox *>("sgbBorderMode");
            selected = choice && choice->currentIndex() == int(SgbBorderMode::Always) &&
                       choice->count() == 3;
            if (selected)
                choice->setCurrentIndex(int(SgbBorderMode::Never));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        driver.start();
        configured.configure();
        if (!selected || configured.sgbBorderMode_ != SgbBorderMode::Never ||
            settings_.value("video/sgbBorderMode").toInt() != int(SgbBorderMode::Never))
            return smokeFailure(__FILE__, __LINE__);
    }

    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
    settings_.setValue("video/sgbBorderMode", int(SgbBorderMode::SgbOnly));
    MainWindow session(true, true, false, fixtures.path(), std::nullopt, uint64_t(0));
    session.timer_.stop();
    if (!session.openRom(donorPath) || session.image_.size() != QSize(160, 144) ||
        session.borrowedSgbBorder_)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    session.setFrameBlending(FrameBlending::Disabled);
    session.display_->messages.setEnabled(false);
    session.present(Presentation::Reset);
    const auto native = session.captureState();
    const auto handheld = session.image_;
    if (native.isEmpty() || !modeAction(session, SgbBorderMode::Always) ||
        session.image_.size() != QSize(256, 224) ||
        session.image_.copy(48, 40, 160, 144) != handheld)
        return smokeFailure(__FILE__, __LINE__);
    const auto fallback = session.image_;
    if (!wait([&] { return !session.sgbBorderJobPending_; }) || !session.borrowedSgbBorder_ ||
        session.captureState() != native || !session.paused_ ||
        session.image_.copy(48, 40, 160, 144) != handheld)
        return smokeFailure(__FILE__, __LINE__);
    const auto borrowed = session.borrowedSgbBorder_;
    QImage expected(256, 224, QImage::Format_RGB32);
    expected.fill(qRgb(0, 0, 0));
    cupid_gb_sgb_render_borrowed_border(
        borrowed.get(), reinterpret_cast<uint32_t *>(expected.bits()),
        size_t(expected.width() * expected.height()), session.sgbColorTable_->data());
    {
        QPainter painter(&expected);
        painter.drawImage(48, 40, handheld);
    }
    if (session.image_ != expected || session.image_ == fallback || !session.sgbBorderAttempted_ ||
        !modeAction(session, SgbBorderMode::Never) || session.image_ != handheld ||
        session.borrowedSgbBorder_ != borrowed || !modeAction(session, SgbBorderMode::Always) ||
        session.image_ != expected || session.captureState() != native)
        return smokeFailure(__FILE__, __LINE__);
    if (!session.saveSlot(0) || !session.loadSlot(0) || session.borrowedSgbBorder_ != borrowed ||
        !session.quickReset() || session.borrowedSgbBorder_ != borrowed || !session.restart() ||
        session.borrowedSgbBorder_ != borrowed)
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_MGB));
    if (!session.restart() || session.emulator_->gb.model != CUPID_GB_MODEL_MGB ||
        session.borrowedSgbBorder_ != borrowed || session.image_.size() != QSize(256, 224))
        return smokeFailure(__FILE__, __LINE__);
    const auto retainedCartridge = session.sgbBorderCartridge_;
    if (session.openRom(fixtures.filePath("missing.gb")) ||
        session.borrowedSgbBorder_ != borrowed || session.sgbBorderCartridge_ != retainedCartridge)
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    settings_.setValue("emulation/model", int(CUPID_GB_MODEL_DMG_ABC));
    if (!session.restart() || session.borrowedSgbBorder_ != borrowed ||
        !session.openRom(plainPath) || session.borrowedSgbBorder_ ||
        session.image_.size() != QSize(256, 224) || blackBorderOutsideViewport(session.image_))
        return smokeFailure(__FILE__, __LINE__);
    const auto plainFramed = session.image_;
    if (!modeAction(session, SgbBorderMode::Never) ||
        session.image_ != plainFramed.copy(48, 40, 160, 144))
        return smokeFailure(__FILE__, __LINE__);
    if (!session.openRom(donorPath) || session.borrowedSgbBorder_)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    if (!modeAction(session, SgbBorderMode::Always) || !session.sgbBorderJobPending_)
        return smokeFailure(__FILE__, __LINE__);
    const auto staleCartridge = session.sgbBorderCartridge_;
    if (!session.openRom(plainPath) || session.sgbBorderCartridge_ == staleCartridge)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    const auto newCartridgeState = session.captureState();
    const auto newCartridgeImage = session.image_;
    if (!wait([&] { return !session.sgbBorderJobPending_; }) || session.borrowedSgbBorder_ ||
        session.captureState() != newCartridgeState || session.image_ != newCartridgeImage ||
        session.romPath_ != plainPath || !session.paused_)
        return smokeFailure(__FILE__, __LINE__);

    {
        MainWindow integrated(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_SGB2,
                              uint64_t(0));
        integrated.timer_.stop();
        if (!integrated.openRom(donorPath))
            return smokeFailure(__FILE__, __LINE__);
        integrated.setPaused(true);
        integrated.setFrameBlending(FrameBlending::Disabled);
        if (!modeAction(integrated, SgbBorderMode::SgbOnly) ||
            integrated.image_.size() != QSize(256, 224))
            return smokeFailure(__FILE__, __LINE__);
        const auto full = integrated.image_;
        if (!modeAction(integrated, SgbBorderMode::Never) ||
            integrated.image_ != full.copy(48, 40, 160, 144))
            return smokeFailure(__FILE__, __LINE__);
        integrated.display_->messages.setEnabled(true);
        integrated.notifyOnScreen("Border crop");
        integrated.present(Presentation::Frame);
        if (integrated.image_ != full.copy(48, 40, 160, 144) ||
            integrated.currentMessageFrame_.size() != QSize(160, 144) ||
            integrated.display_->image == integrated.image_ ||
            integrated.display_->image !=
                composeScreenMessage(integrated.image_, integrated.currentMessageFrame_) ||
            integrated.borrowedSgbBorder_)
            return smokeFailure(__FILE__, __LINE__);
        integrated.display_->messages.clear();
        integrated.display_->messages.setEnabled(false);
    }
    {
        MainWindow external(true, true, false, fixtures.path(), CUPID_GB_MODEL_SGB_EXTERNAL,
                            uint64_t(0));
        external.timer_.stop();
        if (!external.openRom(donorPath))
            return smokeFailure(__FILE__, __LINE__);
        external.setPaused(true);
        if (!modeAction(external, SgbBorderMode::Always) ||
            !blackBorderOutsideViewport(external.image_) || external.borrowedSgbBorder_ ||
            external.sgbBorderJobPending_ || external.sgbBorderAttempted_)
            return smokeFailure(__FILE__, __LINE__);
        const auto framed = external.image_;
        if (!modeAction(external, SgbBorderMode::Never) ||
            external.image_ != framed.copy(48, 40, 160, 144))
            return smokeFailure(__FILE__, __LINE__);
    }
    {
        QTemporaryDir missing(QDir(directory).filePath("border-missing-XXXXXX"));
        if (!missing.isValid() || !writeBorderFixture(missing.filePath("dmg_boot.bin"), firmware))
            return smokeFailure(__FILE__, __LINE__);
        settings_.setValue("video/sgbBorderMode", int(SgbBorderMode::Always));
        MainWindow unavailable(true, true, false, missing.path(), CUPID_GB_MODEL_DMG_ABC,
                               uint64_t(0));
        unavailable.timer_.stop();
        if (!unavailable.openRom(donorPath))
            return smokeFailure(__FILE__, __LINE__);
        unavailable.setPaused(true);
        if (!unavailable.sgbBorderAttempted_ || unavailable.borrowedSgbBorder_ ||
            unavailable.sgbBorderJobPending_ || unavailable.image_.size() != QSize(256, 224) ||
            blackBorderOutsideViewport(unavailable.image_))
            return smokeFailure(__FILE__, __LINE__);
        if (!writeBorderFixture(missing.filePath("sgb_boot.bin"), firmware) ||
            !modeAction(unavailable, SgbBorderMode::Never) ||
            !modeAction(unavailable, SgbBorderMode::Always) || unavailable.borrowedSgbBorder_ ||
            unavailable.sgbBorderJobPending_ || !unavailable.openRom(secondPath) ||
            !wait([&] { return !unavailable.sgbBorderJobPending_; }) ||
            !unavailable.borrowedSgbBorder_ || !unavailable.closeRom() ||
            unavailable.borrowedSgbBorder_)
            return smokeFailure(__FILE__, __LINE__);
    }
    if (!session.closeRom() || session.borrowedSgbBorder_)
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB border modes, isolated borrowing, cache, crop, and fallback workflows passed");
    return true;
}

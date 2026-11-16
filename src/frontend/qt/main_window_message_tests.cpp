/**
 * @file    main_window_message_tests.cpp
 * @brief   Qt regression checks for on-screen messages and display history.
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

#include "main_window_internal.h"

#include <QClipboard>
#include <QScopeGuard>

extern "C" {
#include "cupid/gb/ppu.h"
}

bool MainWindow::screenMessagesSmokeTest(const QString &, const QString &directory) {
    const bool hadPreference = settings_.contains("video/osd");
    const QVariant preference = settings_.value("video/osd");
    const auto restorePreference = qScopeGuard([&] {
        if (hadPreference)
            settings_.setValue("video/osd", preference);
        else
            settings_.remove("video/osd");
        applyDisplaySettings();
    });
    settings_.setValue("video/osd", false);
    MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC);
    session.timer_.stop();
    auto *toggle = session.findChild<QAction *>("video/osd");
    if (!toggle || toggle->isChecked() || session.display_->messages.enabled())
        return smokeFailure(__FILE__, __LINE__);
    toggle->trigger();
    if (!toggle->isChecked() || !session.display_->messages.enabled() ||
        !session.settings_.value("video/osd").toBool())
        return smokeFailure(__FILE__, __LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom.replace(0x134, 11, "OSD FIXTURE");
    const auto updateChecksum = [](QByteArray &bytes) {
        uint8_t checksum = 0;
        for (unsigned index = 0x134; index < 0x14d; ++index)
            checksum = uint8_t(checksum - uint8_t(bytes[index]) - 1);
        bytes[0x14d] = char(checksum);
    };
    updateChecksum(rom);
    const QString cartridge = QDir(directory).filePath("messages.gb");
    QFile file(cartridge);
    if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    file.close();
    if (!session.openRom(cartridge) ||
        session.display_->messages.text() != QString("CupidGB v%1\nOSD FIXTURE\nA17D3BFB")
                                                 .arg(QCoreApplication::applicationVersion()) ||
        session.display_->messages.remainingFrames() != 90)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    const auto state = session.captureState();
    const QImage original = session.image_;
    const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QDir captures(captureDirectory);
        if (!captures.mkpath(".") ||
            !session.display_->grab().save(captures.filePath("messages-startup.png")))
            return smokeFailure(__FILE__, __LINE__);
    }
    const QImage startupPresentation = session.display_->image;
    const QImage startupOverlay = session.currentMessageFrame_;
    if (session.emulator_->gb.frame_ready || !session.currentDisplayFrame_.isNull() ||
        startupOverlay.isNull())
        return smokeFailure(__FILE__, __LINE__);
    toggle->trigger();
    session.present();
    session.applyDisplaySettings();
    toggle->trigger();
    session.present();
    session.applyDisplaySettings();
    if (!session.display_->messages.text().isEmpty() ||
        session.display_->image != startupPresentation || session.captureState() != state ||
        session.currentMessageFrame_.cacheKey() != startupOverlay.cacheKey() ||
        !session.currentDisplayFrame_.isNull() || !session.previousDisplayFrame_.isNull() ||
        !session.previousMessageFrame_.isNull() || !session.paused_)
        return smokeFailure(__FILE__, __LINE__);
    session.notifyOnScreen("State saved");
    const auto remaining = session.display_->messages.remainingFrames();
    for (unsigned paint = 0; paint < 5; ++paint) {
        session.present();
        if (session.display_->grab().isNull() || session.display_->image != startupPresentation ||
            session.currentMessageFrame_.cacheKey() != startupOverlay.cacheKey())
            return smokeFailure(__FILE__, __LINE__);
    }
    if (session.captureState() != state || session.image_ != original ||
        session.display_->messages.remainingFrames() != remaining)
        return smokeFailure(__FILE__, __LINE__);
    session.present(Presentation::Frame);
    if (session.currentMessageFrame_ == startupOverlay || !session.previousMessageFrame_.isNull() ||
        session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    if (!captureDirectory.isEmpty()) {
        QDir captures(captureDirectory);
        if (!captures.mkpath(".") ||
            !session.display_->grab().save(captures.filePath("messages.png")))
            return smokeFailure(__FILE__, __LINE__);
    }
    const QImage priorPresentation = session.display_->image;
    const QImage priorOverlay = session.currentMessageFrame_;
    const auto frameType = session.emulator_->gb.display_frame_type;
    session.emulator_->gb.display_frame_type = CUPID_GB_FRAME_REPEAT;
    session.notifyOnScreen("Next presentation");
    session.advanceScreenMessages();
    session.present(Presentation::Frame);
    if (session.display_->image != priorPresentation ||
        session.currentMessageFrame_ != priorOverlay ||
        session.display_->messages.remainingFrames() != 29)
        return smokeFailure(__FILE__, __LINE__);
    session.emulator_->gb.display_frame_type = frameType;
    session.present();
    if (session.display_->image != priorPresentation ||
        session.currentMessageFrame_.cacheKey() != priorOverlay.cacheKey())
        return smokeFailure(__FILE__, __LINE__);
    session.present(Presentation::Frame);
    if (session.currentMessageFrame_ == priorOverlay || session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    const QString screenshot = QDir(directory).filePath("messages-clean.png");
    if (!session.saveScreenshot(screenshot) || QImage(screenshot) != original ||
        session.display_->messages.text() != "Screenshot saved")
        return smokeFailure(__FILE__, __LINE__);
    session.copyScreenshot();
    if (QApplication::clipboard()->image() != original ||
        session.display_->messages.text() != "Screenshot copied")
        return smokeFailure(__FILE__, __LINE__);
    session.turbo_ = session.slowHeld_ = session.rewind_ = true;
    session.advanceScreenMessages();
    if (session.display_->messages.text() != "Fast forward...")
        return smokeFailure(__FILE__, __LINE__);
    session.turbo_ = false;
    session.advanceScreenMessages();
    if (session.display_->messages.text() != "Slow motion...")
        return smokeFailure(__FILE__, __LINE__);
    session.slowHeld_ = false;
    session.advanceScreenMessages();
    if (session.display_->messages.text() != "Rewinding...")
        return smokeFailure(__FILE__, __LINE__);
    session.rewind_ = false;
    for (unsigned frame = 0; frame < 29; ++frame)
        session.advanceScreenMessages();
    if (!session.display_->messages.text().isEmpty() || session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);

    const auto checkBlend = [&](MainWindow &window, FrameBlending mode, bool odd) {
        auto &gb = window.emulator_->gb;
        const QScopedValueRollback<uint8_t> frame(gb.display_frame_type, CUPID_GB_FRAME_NORMAL);
        const QScopedValueRollback<bool> polarity(gb.lcd_frame_odd, odd);
        const QByteArray before = window.captureState();
        const bool sgb = cupid_gb_sgb_active(&gb);
        constexpr std::array<const char *, 9> preferenceKeys = {"video/osd",
                                                                "video/palette",
                                                                "video/blendingMode",
                                                                "video/sgbBorder",
                                                                "video/customPalette/0",
                                                                "video/customPalette/1",
                                                                "video/customPalette/2",
                                                                "video/customPalette/3",
                                                                "video/customPalette/off"};
        std::array<std::optional<QVariant>, preferenceKeys.size()> preferences;
        for (size_t index = 0; index < preferenceKeys.size(); ++index)
            if (window.settings_.contains(preferenceKeys[index]))
                preferences[index] = window.settings_.value(preferenceKeys[index]);
        const auto restorePreferences = qScopeGuard([&] {
            for (size_t index = 0; index < preferenceKeys.size(); ++index) {
                if (preferences[index])
                    window.settings_.setValue(preferenceKeys[index], *preferences[index]);
                else
                    window.settings_.remove(preferenceKeys[index]);
            }
            window.applyDisplaySettings();
        });
        window.settings_.setValue("video/osd", true);
        window.settings_.setValue("video/palette", int(MonochromePalette::Custom));
        window.settings_.setValue("video/blendingMode", int(mode));
        window.settings_.setValue("video/sgbBorder", window.borderAction_->isChecked());
        for (const char *key :
             {"video/customPalette/0", "video/customPalette/1", "video/customPalette/2",
              "video/customPalette/3", "video/customPalette/off"})
            window.settings_.setValue(key, "#000000");
        window.applyDisplaySettings();
        if (sgb)
            window.sgbColorTable_->fill(qRgb(0, 0, 0));
        window.display_->messages.clear();
        window.present(Presentation::Reset);
        window.present(Presentation::Frame);
        const QImage clean = window.image_;
        const QSize expectedSize =
            sgb && window.borderAction_->isChecked() ? QSize(256, 224) : QSize(160, 144);
        if (clean.size() != expectedSize || window.display_->image != clean ||
            !window.previousMessageFrame_.isNull())
            return false;
        window.notifyOnScreen("State saved");
        window.advanceScreenMessages();
        window.present(Presentation::Frame);
        const QImage overlay = window.currentMessageFrame_;
        const auto checkPixels = [&](bool appearing, bool steady = false) {
            unsigned whitePixels[2] = {};
            for (int y = 0; y < overlay.height(); ++y) {
                int level = appearing ? 255 : 0;
                if (!steady && mode != FrameBlending::Disabled) {
                    if (mode == FrameBlending::Simple || sgb)
                        level = 186;
                    else
                        level = (((y & 1) == int(odd)) == appearing) ? 212 : 155;
                }
                for (int x = 0; x < overlay.width(); ++x) {
                    if (overlay.pixel(x, y) != qRgba(255, 255, 255, 255))
                        continue;
                    ++whitePixels[y & 1];
                    if (window.display_->image.pixel(x, y) != qRgb(level, level, level)) {
                        qWarning("CupidGB message blend mismatch: mode %d, SGB %d, odd %d, "
                                 "appearing %d, steady %d, pixel %d,%d, actual %08x, expected %08x",
                                 int(mode), int(sgb), int(odd), int(appearing), int(steady), x, y,
                                 window.display_->image.pixel(x, y), qRgb(level, level, level));
                        return false;
                    }
                }
            }
            return whitePixels[0] && whitePixels[1] && window.image_ == clean &&
                   window.currentDisplayFrame_ == clean && window.previousDisplayFrame_ == clean;
        };
        if (!checkPixels(true))
            return false;
        window.present(Presentation::Frame);
        if (!checkPixels(true, true))
            return false;
        // Expiration removes current text first, leaving exactly one predecessor
        // contribution at the existing gamma-correct blend weights.
        for (unsigned event = 0; event < 29; ++event)
            window.advanceScreenMessages();
        window.present(Presentation::Frame);
        if (!window.display_->messages.text().isEmpty() || !checkPixels(false))
            return false;
        window.present(Presentation::Frame);
        if (window.display_->image != clean || !window.currentMessageFrame_.isNull() ||
            !window.previousMessageFrame_.isNull())
            return false;
        // A host-only toggle retains submitted pixels until a new guest frame.
        // Repeated events retain that same raster, even while text is disabled.
        window.notifyOnScreen("State saved");
        window.present(Presentation::Frame);
        window.present(Presentation::Frame);
        const QImage submitted = window.display_->image;
        const QImage currentMessage = window.currentMessageFrame_;
        const QImage previousMessage = window.previousMessageFrame_;
        const QImage currentFrame = window.currentDisplayFrame_;
        const QImage previousFrame = window.previousDisplayFrame_;
        auto *messageToggle = window.findChild<QAction *>("video/osd");
        if (!messageToggle || !messageToggle->isChecked())
            return false;
        messageToggle->trigger();
        const auto historyHeld = [&](const char *stage) {
            const bool held =
                window.display_->messages.text().isEmpty() &&
                window.display_->messages.remainingFrames() == 0 &&
                window.display_->image == submitted &&
                window.currentMessageFrame_.cacheKey() == currentMessage.cacheKey() &&
                window.previousMessageFrame_.cacheKey() == previousMessage.cacheKey() &&
                window.currentDisplayFrame_.cacheKey() == currentFrame.cacheKey() &&
                window.previousDisplayFrame_.cacheKey() == previousFrame.cacheKey() &&
                window.captureState() == before && window.paused_;
            if (!held)
                qWarning("CupidGB paused message redraw changed submitted history: %s, mode %d, "
                         "SGB %d, odd %d",
                         stage, int(mode), int(sgb), int(odd));
            return held;
        };
        bool redrawsHeld = historyHeld("notification toggle");
        for (auto *action : window.blendingActions_) {
            action->trigger();
            redrawsHeld = historyHeld("blend action") && redrawsHeld;
        }
        window.blendingActions_[size_t(mode)]->trigger();
        window.applyDisplaySettings();
        redrawsHeld = historyHeld("unchanged settings") && redrawsHeld;
        window.applyDisplaySettings();
        redrawsHeld = historyHeld("second unchanged settings") && redrawsHeld;
        if (!redrawsHeld)
            return false;
        {
            const QScopedValueRollback<uint8_t> repeat(gb.display_frame_type,
                                                       CUPID_GB_FRAME_REPEAT);
            window.advanceScreenMessages();
            window.present(Presentation::Frame);
            if (window.display_->image != submitted)
                return false;
        }
        window.present(Presentation::Frame);
        if (!checkPixels(false))
            return false;
        window.present(Presentation::Frame);
        messageToggle->trigger();
        if (window.display_->image != clean || !window.display_->messages.text().isEmpty())
            return false;
        // An explicit restore discards both pairs of submitted rasters before
        // publishing the restored image, even when no new frame is ready.
        window.notifyOnScreen("State saved");
        window.present(Presentation::Frame);
        window.present(Presentation::Frame);
        window.display_->messages.clear();
        window.present(Presentation::Reset);
        return window.display_->image == clean && window.previousMessageFrame_.isNull() &&
               window.currentMessageFrame_.isNull() && window.captureState() == before &&
               window.paused_;
    };
    {
        const auto palette = session.displayPalette_;
        const auto offColor = session.displayOffColor_;
        const auto blending = session.frameBlending_;
        const auto restoreDisplay = qScopeGuard([&] {
            session.displayPalette_ = palette;
            session.displayOffColor_ = offColor;
            session.setFrameBlending(blending);
            session.display_->messages.clear();
            session.present(Presentation::Reset);
        });
        for (const auto mode :
             {FrameBlending::Disabled, FrameBlending::Simple, FrameBlending::Accurate})
            for (const bool odd : {false, true})
                if (!checkBlend(session, mode, odd))
                    return smokeFailure(__FILE__, __LINE__);
    }
    if (session.captureState() != state || session.image_ != original)
        return smokeFailure(__FILE__, __LINE__);
    {
        QByteArray enhanced = rom;
        enhanced[0x146] = char(3);
        updateChecksum(enhanced);
        const QString enhancedPath = QDir(directory).filePath("messages-enhanced.gb");
        QFile output(enhancedPath);
        if (!output.open(QIODevice::WriteOnly) || output.write(enhanced) != enhanced.size())
            return smokeFailure(__FILE__, __LINE__);
        output.close();
        MainWindow enhancedSession(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_SGB2);
        enhancedSession.timer_.stop();
        if (!enhancedSession.openRom(enhancedPath))
            return smokeFailure(__FILE__, __LINE__);
        enhancedSession.setPaused(true);
        for (const bool border : {false, true}) {
            enhancedSession.borderAction_->setChecked(border);
            for (const auto mode :
                 {FrameBlending::Disabled, FrameBlending::Simple, FrameBlending::Accurate})
                for (const bool odd : {false, true})
                    if (!checkBlend(enhancedSession, mode, odd))
                        return smokeFailure(__FILE__, __LINE__);
            const QImage clean = enhancedSession.image_;
            const QByteArray beforeExport = enhancedSession.captureState();
            enhancedSession.notifyOnScreen("State saved");
            enhancedSession.present(Presentation::Frame);
            const QString outputPath = QDir(directory).filePath(
                border ? "messages-enhanced-bordered-clean.png" : "messages-enhanced-clean.png");
            if (!enhancedSession.saveScreenshot(outputPath) || QImage(outputPath) != clean)
                return smokeFailure(__FILE__, __LINE__);
            enhancedSession.copyScreenshot();
            if (QApplication::clipboard()->image() != clean ||
                enhancedSession.captureState() != beforeExport || !enhancedSession.paused_)
                return smokeFailure(__FILE__, __LINE__);
        }
    }
    session.notifyOnScreen("Pending message");
    session.present(Presentation::Frame);
    session.present(Presentation::Frame);
    const QImage toggledImage = session.display_->image;
    toggle->trigger();
    if (!session.display_->messages.text().isEmpty() || session.display_->image != toggledImage ||
        !session.paused_ || session.captureState() != state)
        return smokeFailure(__FILE__, __LINE__);
    toggle->trigger();
    if (!session.display_->messages.text().isEmpty() || session.display_->image != toggledImage)
        return smokeFailure(__FILE__, __LINE__);
    session.present(Presentation::Frame);
    session.present(Presentation::Frame);
    if (session.display_->image != original || !session.currentMessageFrame_.isNull() ||
        !session.previousMessageFrame_.isNull())
        return smokeFailure(__FILE__, __LINE__);
    if (!session.closeRom() || !session.display_->messages.text().isEmpty() ||
        !session.currentMessageFrame_.isNull() || !session.previousMessageFrame_.isNull())
        return smokeFailure(__FILE__, __LINE__);
    MainWindow reopened(true, true);
    if (!reopened.display_->messages.enabled())
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB on-screen message workflows passed");
    return true;
}

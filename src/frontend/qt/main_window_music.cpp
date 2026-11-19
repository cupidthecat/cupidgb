/**
 * @file    main_window_music.cpp
 * @brief   Music loading, track transactions, compact presentation and audio visualization.
 * @author  cupidthecat
 * @date    2026-11-18
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

#include "gbs_player.h"
#include "main_window_internal.h"

#include <QScopeGuard>

bool MainWindow::prepareMusicCandidate(std::unique_ptr<CupidEmulator> &candidate,
                                       const QByteArray &data, int &model, CupidGbGbsInfo &info) {
    const bool automatic = model < 0;
    int selected = automatic ? CUPID_GB_MODEL_CGB : model;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        candidate = std::make_unique<CupidEmulator>();
        if (powerOnSeedOverride_)
            cupid_gb_set_power_on_seed(&candidate->gb, *powerOnSeedOverride_);
        else
            cupid_gb_clear_power_on_seed(&candidate->gb);
        cupid_gb_set_rtc_mode(&candidate->gb, configuredRtcMode(settings_));
        cupid_emulator_init(candidate.get(), cupid_gb_model_is_cgb(CupidGbModel(selected))
                                                 ? CUPID_SYSTEM_GBC
                                                 : CUPID_SYSTEM_GB);
        cupid_gb_set_model(&candidate->gb, CupidGbModel(selected));
        // Banking history belongs to the document even when its active program
        // is replaced or Automatic prepares the image again for another model.
        if (emulator_)
            candidate->gb.mbc3_extended_history = emulator_->gb.mbc3_extended_history;
        const auto result =
            cupid_gb_load_gbs(&candidate->gb, reinterpret_cast<const uint8_t *>(data.constData()),
                              size_t(data.size()), &info);
        if (result != CUPID_GB_GBS_OK) {
            cupid_emulator_shutdown(candidate.get());
            reportError("The music file could not be loaded: " +
                        QString::fromLatin1(cupid_gb_gbs_result_name(result)));
            return false;
        }
        if (automatic && !attempt) {
            // Capability bytes belong to the prepared program image. The file
            // begins with music metadata, not a cartridge header at these offsets.
            const auto image =
                QByteArray::fromRawData(reinterpret_cast<const char *>(candidate->gb.rom),
                                        qsizetype(candidate->gb.rom_size));
            const int inferred = automaticModel(image);
            if (inferred != selected) {
                cupid_emulator_shutdown(candidate.get());
                selected = inferred;
                continue;
            }
        }
        model = selected;
        return true;
    }
    return false;
}

void MainWindow::setMusicSession(const std::optional<CupidGbGbsInfo> &info, uint8_t track) {
    musicInfo_ = info;
    musicTrack_ = track;
    musicFramePending_ = false;
    if (info) {
        if (!musicPlayer_) {
            musicPlayer_ = new GbsPlayer(this);
            musicPlayer_->trackRequested = [this](uint8_t selected) { startMusicTrack(selected); };
            musicPlayer_->restartRequested = [this] { startMusicTrack(musicTrack_); };
            musicPlayer_->previousRequested = [this] { moveMusicTrack(-1); };
            musicPlayer_->nextRequested = [this] { moveMusicTrack(1); };
            musicPlayer_->pauseRequested = [this] {
                if (!stateOperation_ && musicInfo_)
                    setPaused(!paused_);
            };
        }
        musicPlayer_->setInfo(*info, track);
        musicPlayer_->setPaused(paused_);
        musicPlayer_->setColors(displayPalette_);
        musicPlayer_->clearWaveform();
        if (!musicPresentation_) {
            gameGeometry_ = saveGeometry();
            gameMinimumSize_ = minimumSize();
            gameMaximumSize_ = maximumSize();
            if (isFullScreen() || isMaximized())
                showNormal();
            auto *previous = takeCentralWidget();
            previous->hide();
            previous->setParent(this);
            setCentralWidget(musicPlayer_);
            musicPresentation_ = true;
        }
        musicPlayer_->show();
        layout()->activate();
        const int chrome = minimumSizeHint().height() - musicPlayer_->minimumSizeHint().height();
        const QSize content = musicPlayer_->sizeHint();
        setFixedSize(std::max(content.width(), menuBar()->sizeHint().width()),
                     content.height() + std::max(0, chrome));
    } else if (musicPresentation_) {
        auto *previous = takeCentralWidget();
        previous->hide();
        previous->setParent(this);
        setMinimumSize(gameMinimumSize_);
        setMaximumSize(gameMaximumSize_);
        setCentralWidget(display_);
        musicPresentation_ = false;
        display_->show();
        layout()->activate();
        restoreGeometry(gameGeometry_);
        gameGeometry_.clear();
    }
    if (auto *fullscreen = findChild<QAction *>("fullscreen")) {
        const QSignalBlocker blocker(fullscreen);
        fullscreen->setChecked(isFullScreen());
    }
    // A music view never exposes the previously loaded cartridge image through
    // capture actions or a transient native repaint when returning to gameplay.
    image_ = {};
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    previousMessageFrame_ = {};
    currentMessageFrame_ = {};
    display_->setFrames({}, {}, FrameBlending::Disabled, false);
}

bool MainWindow::startMusicTrack(uint8_t track) {
    if (stateOperation_ || !emulator_ || !musicInfo_)
        return false;
    const QPointer<MainWindow> self(this);
    const bool previousOperation = stateOperation_;
    stateOperation_ = true;
    const auto operation = qScopeGuard([self, previousOperation] {
        if (self)
            self->stateOperation_ = previousOperation;
    });
    const auto result = cupid_gb_gbs_start_track(&emulator_->gb, track);
    if (result != CUPID_GB_GBS_OK) {
        refreshMusicControls();
        reportError("The music track could not be started: " +
                    QString::fromLatin1(cupid_gb_gbs_result_name(result)));
        return false;
    }
    musicTrack_ = track;
    ++sessionId_;
    const uint64_t committedSession = sessionId_;
    rebaseLink();
    resetRewind();
    resetDebuggerTimeline();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    refreshMusicControls();
    resetPacing();
    // A running track switch preserves already-generated PCM and the waveform
    // tail. Resume only when needed; pause transitions own their audio reset.
    if (paused_)
        setPaused(false);
    if (!self || !emulator_ || sessionId_ != committedSession || !musicInfo_)
        return true;
    rememberFrame();
    refreshActions();
    return true;
}

void MainWindow::moveMusicTrack(int direction) {
    if (!musicInfo_ || !musicInfo_->track_count || stateOperation_)
        return;
    const int count = musicInfo_->track_count;
    const int next = musicTrack_ >= count ? (direction < 0 ? count - 1 : 0)
                                          : (int(musicTrack_) + count + direction) % count;
    startMusicTrack(uint8_t(next));
}

void MainWindow::refreshMusicControls() {
    if (!musicPlayer_ || !musicInfo_)
        return;
    musicPlayer_->setTrack(musicTrack_);
    musicPlayer_->setPaused(paused_);
}

void MainWindow::appendMusicSamples(const int16_t *samples, size_t frames) {
    if (musicInfo_ && musicPlayer_)
        musicPlayer_->appendSamples(samples, frames);
}

void MainWindow::publishMusicFrame() {
    if (musicInfo_ && musicPlayer_ && musicFramePending_) {
        musicFramePending_ = false;
        musicPlayer_->publishWaveform();
        ++musicWaveformsPublished_;
    }
}

bool MainWindow::presentMusic() {
    if (!musicInfo_ || !musicPlayer_)
        return false;
    musicPlayer_->setColors(displayPalette_);
    return true;
}

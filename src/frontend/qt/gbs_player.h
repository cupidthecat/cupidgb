/**
 * @file    gbs_player.h
 * @brief   Compact music-player controls and a bounded stereo waveform view.
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

#pragma once

#include <QWidget>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

extern "C" {
#include "cupid/gb/gbs.h"
}

class QComboBox;
class QLabel;
class QToolButton;
class GbsWaveform;

class GbsPlayer final : public QWidget {
  public:
    explicit GbsPlayer(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    void setInfo(const CupidGbGbsInfo &info, uint8_t track);
    void setTrack(uint8_t track);
    void setPaused(bool paused);
    void setColors(const std::array<QRgb, 4> &colors);
    void appendSamples(const int16_t *samples, size_t frames);
    void publishWaveform();
    void clearWaveform();
    uint64_t waveformPaintCount() const;
    void resetTelemetry();

    std::function<void(uint8_t)> trackRequested;
    std::function<void()> restartRequested;
    std::function<void()> previousRequested;
    std::function<void()> nextRequested;
    std::function<void()> pauseRequested;

  private:
    QLabel *title_ = nullptr;
    QLabel *author_ = nullptr;
    QLabel *copyright_ = nullptr;
    QLabel *trackNotice_ = nullptr;
    QComboBox *tracks_ = nullptr;
    QToolButton *playPause_ = nullptr;
    QToolButton *previous_ = nullptr;
    QToolButton *next_ = nullptr;
    GbsWaveform *waveform_ = nullptr;
    uint8_t trackCount_ = 0;
};

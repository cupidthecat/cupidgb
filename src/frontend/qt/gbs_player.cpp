/**
 * @file    gbs_player.cpp
 * @brief   Music metadata, track navigation, transport controls and waveform painting.
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

#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

class GbsWaveform final : public QWidget {
  public:
    explicit GbsWaveform(QWidget *parent) : QWidget(parent) {
        setObjectName("gbsWaveform");
        setAccessibleName("Audio waveform");
        setFixedHeight(76);
    }

    void append(const int16_t *samples, size_t frames) {
        if (!samples)
            return;
        // Keep the last 1024 stereo frames. A host volume or mute change does
        // not change this core-sample history.
        for (size_t frame = 0; frame < frames; ++frame) {
            pending_[position_] =
                (int32_t(samples[frame * 2]) + int32_t(samples[frame * 2 + 1])) / 32768.0;
            position_ = (position_ + 1) % pending_.size();
        }
    }

    void publish() {
        // Expose only a completed guest frame's prefix. An unrelated native
        // repaint must not reveal a partly collected next frame.
        for (size_t i = 0; i < displayed_.size(); ++i)
            displayed_[i] = pending_[(position_ + i) % pending_.size()];
        update();
    }

    void clear() {
        pending_.fill(0);
        displayed_.fill(0);
        position_ = 0;
        update();
    }

    void setColors(const std::array<QRgb, 4> &colors) {
        if (colors_ == colors)
            return;
        colors_ = colors;
        update();
    }

    uint64_t paintCount = 0;

  protected:
    void paintEvent(QPaintEvent *) override {
        ++paintCount;
        QPainter painter(this);
        painter.fillRect(rect(), colors_[0]);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal baseline = height() / 2.0;
        QPainterPath shape;
        shape.moveTo(0, baseline);
        for (size_t i = 0; i < displayed_.size(); ++i)
            shape.lineTo(width() * (qreal(i) + 0.5) / displayed_.size(),
                         baseline * (1 - displayed_[i]));
        shape.lineTo(width(), baseline);
        painter.fillPath(shape, QColor(colors_[2]));
        painter.fillRect(QRectF(0, baseline - 0.5, width(), 1), colors_[1]);
        painter.setPen(QPen(QColor(colors_[3]), 1));
        painter.drawPath(shape);
    }

  private:
    std::array<qreal, 1024> pending_{};
    std::array<qreal, 1024> displayed_{};
    size_t position_ = 0;
    std::array<QRgb, 4> colors_{qRgb(224, 248, 208), qRgb(136, 192, 112), qRgb(52, 104, 86),
                                qRgb(8, 24, 32)};
};

GbsPlayer::GbsPlayer(QWidget *parent) : QWidget(parent) {
    setObjectName("gbsPlayer");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);
    const auto label = [this, layout](const char *name) {
        auto *widget = new QLabel(this);
        widget->setObjectName(name);
        widget->setTextFormat(Qt::PlainText);
        widget->setAlignment(Qt::AlignCenter);
        layout->addWidget(widget);
        return widget;
    };
    title_ = label("gbsTitle");
    auto titleFont = title_->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 3);
    titleFont.setBold(true);
    title_->setFont(titleFont);
    title_->setFixedHeight(title_->fontMetrics().height() + 2);
    author_ = label("gbsAuthor");
    author_->setFixedHeight(author_->fontMetrics().height() + 2);
    waveform_ = new GbsWaveform(this);
    layout->addWidget(waveform_);
    auto *transport = new QHBoxLayout;
    transport->setSpacing(4);
    layout->addLayout(transport);
    const auto button = [this, transport](const char *name, const QString &text,
                                          QStyle::StandardPixmap icon) {
        auto *widget = new QToolButton(this);
        widget->setObjectName(name);
        widget->setText(text);
        widget->setToolTip(text);
        widget->setAccessibleName(text);
        widget->setIcon(style()->standardIcon(icon));
        widget->setIconSize(QSize(20, 20));
        widget->setFixedSize(32, 32);
        transport->addWidget(widget);
        return widget;
    };
    auto *restart = button("gbsRestart", "Restart track", QStyle::SP_MediaSeekBackward);
    playPause_ = button("gbsPlayPause", "Pause", QStyle::SP_MediaPause);
    tracks_ = new QComboBox(this);
    tracks_->setObjectName("gbsTracks");
    tracks_->setAccessibleName("Music track");
    tracks_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    tracks_->setMinimumContentsLength(8);
    transport->addWidget(tracks_, 1);
    previous_ = button("gbsPrevious", "Previous track", QStyle::SP_MediaSkipBackward);
    next_ = button("gbsNext", "Next track", QStyle::SP_MediaSkipForward);
    trackNotice_ = label("gbsTrackNotice");
    trackNotice_->setFixedHeight(trackNotice_->fontMetrics().height() + 2);
    copyright_ = label("gbsCopyright");
    copyright_->setFixedHeight(copyright_->fontMetrics().height() + 2);
    connect(restart, &QToolButton::clicked, this, [this] {
        if (restartRequested)
            restartRequested();
    });
    connect(playPause_, &QToolButton::clicked, this, [this] {
        if (pauseRequested)
            pauseRequested();
    });
    connect(previous_, &QToolButton::clicked, this, [this] {
        if (previousRequested)
            previousRequested();
    });
    connect(next_, &QToolButton::clicked, this, [this] {
        if (nextRequested)
            nextRequested();
    });
    connect(tracks_, &QComboBox::activated, this, [this](int index) {
        if (index >= 0 && index < trackCount_ && trackRequested)
            trackRequested(uint8_t(index));
    });
}

QSize GbsPlayer::sizeHint() const {
    return QSize(360, layout()->sizeHint().height());
}

void GbsPlayer::setInfo(const CupidGbGbsInfo &info, uint8_t track) {
    const auto text = [](QLabel *label, const QString &value) {
        label->setToolTip(value);
        label->setText(label->fontMetrics().elidedText(value, Qt::ElideRight, 328));
    };
    text(title_, QString::fromLatin1(info.title));
    text(author_, QString::fromLatin1(info.author));
    text(copyright_, QString(QChar(0x00a9)) + QString::fromLatin1(info.copyright));
    const QSignalBlocker blocker(tracks_);
    tracks_->clear();
    trackCount_ = info.track_count;
    for (unsigned i = 0; i < trackCount_; ++i)
        tracks_->addItem(QString("Track %1").arg(i + 1));
    tracks_->setEnabled(trackCount_ != 0);
    previous_->setEnabled(trackCount_ != 0);
    next_->setEnabled(trackCount_ != 0);
    setTrack(track);
}

void GbsPlayer::setTrack(uint8_t track) {
    const QSignalBlocker blocker(tracks_);
    tracks_->setCurrentIndex(track < trackCount_ ? int(track) : -1);
    trackNotice_->setText(!trackCount_           ? QStringLiteral("No indexed tracks")
                          : track >= trackCount_ ? QStringLiteral("Track outside the declared list")
                                                 : QString());
}

void GbsPlayer::setPaused(bool paused) {
    const QString text = paused ? QStringLiteral("Play") : QStringLiteral("Pause");
    playPause_->setText(text);
    playPause_->setToolTip(text);
    playPause_->setAccessibleName(text);
    playPause_->setIcon(
        style()->standardIcon(paused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
}

void GbsPlayer::setColors(const std::array<QRgb, 4> &colors) {
    waveform_->setColors(colors);
}

void GbsPlayer::appendSamples(const int16_t *samples, size_t frames) {
    waveform_->append(samples, frames);
}

void GbsPlayer::publishWaveform() {
    waveform_->publish();
}

void GbsPlayer::clearWaveform() {
    waveform_->clear();
}

uint64_t GbsPlayer::waveformPaintCount() const {
    return waveform_->paintCount;
}

void GbsPlayer::resetTelemetry() {
    waveform_->paintCount = 0;
}

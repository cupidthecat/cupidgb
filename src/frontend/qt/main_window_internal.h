/**
 * @file    main_window_internal.h
 * @brief   Shared Qt window helpers, validated preferences, and display widget.
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

#pragma once

#include "main_window.h"

#include "audio_buffer.h"
#include "audio_converter.h"
#include "audio_sink_lifetime.h"
#include "cartridge_file.h"
#include "controller_dialog.h"
#include "core_audio_collector.h"
#include "cupid/compat/native_v1.h"
#include "cupid/gb/battery.h"
#include "debugger_window.h"
#include "inspectors_window.h"
#include "screen_messages.h"
#include "wave_recorder.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <limits>

extern "C" {
#include "cupid/gb/apu.h"
#include "cupid/gb/input.h"
#include "cupid/gb/mbc7.h"
#include "cupid/gb/state.h"
#include "cupid/gbc/cgb.h"
}

struct MainWindow::PlaybackWork {
    Playback playback = Playback::Forward;
    CoreAudioCollector collected;
    uint64_t startPresentation = 0;
    uint64_t startGuest = 0;
    uint64_t session = 0;
    uint64_t timeline = 0;
    unsigned steps = 0;
    bool collectionFailed = false;
    bool executionFailed = false;
    bool attemptedHardware = false;
    bool attemptedGuest = false;
    bool watching = false;
    QString debuggerStop;
};

namespace {
constexpr std::array<int, 8> defaultKeys = {Qt::Key_Right,     Qt::Key_Left,  Qt::Key_Up,
                                            Qt::Key_Down,      Qt::Key_X,     Qt::Key_Z,
                                            Qt::Key_Backspace, Qt::Key_Return};
constexpr std::array<const char *, 8> buttonNames = {"Right", "Left", "Up",     "Down",
                                                     "A",     "B",    "Select", "Start"};
constexpr std::array<const char *, 5> specialNames = {"Turbo", "Rewind", "Slow motion", "Rapid A",
                                                      "Rapid B"};
constexpr std::array<int, 5> defaultSpecialKeys = {Qt::Key_Space, Qt::Key_Tab, Qt::Key_Shift, 0, 0};
constexpr std::array<const char *, 3> frameBlendingNames = {"Disabled", "Simple", "Accurate"};
constexpr std::array<const char *, size_t(ScalingFilter::Count)> scalingFilterNames = {
    "Nearest Neighbor", "Bilinear",      "Smooth Bilinear",     "Pixelated",
    "Scale2x",          "Scale4x",       "Antialiased Scale2x", "Antialiased Scale4x",
    "LCD Display",      "Monochrome LCD"};
constexpr std::array<const char *, 3> audioFilterNames = {"None (Keep DC Offset)", "Accurate",
                                                          "Preserve Waveform"};
constexpr std::array<const char *, 4> audioChannelNames = {"Square Channel 1", "Square Channel 2",
                                                           "Wave Channel", "Noise Channel"};
constexpr std::array<unsigned, 12> turboCapChoices = {0, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

unsigned configuredTurboCapQuarters(const QSettings &settings) {
    bool valid = false;
    const int percent = settings.value("emulation/turboCap", 0).toString().toInt(&valid);
    return valid && percent >= 150 && percent <= 400 && percent % 25 == 0 ? unsigned(percent / 25)
                                                                          : 0;
}

CupidGbRtcMode configuredRtcMode(const QSettings &settings) {
    const QVariant value = settings.value("emulation/rtcMode");
    // INI values may be strings. Do not round malformed floating-point preferences.
    switch (value.metaType().id()) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::QString:
            return value.toString() == "1" ? CUPID_GB_RTC_ACCURATE : CUPID_GB_RTC_SYNC_TO_HOST;
        default:
            return CUPID_GB_RTC_SYNC_TO_HOST;
    }
}

int audioPreferenceInteger(const QVariant &value, int maximum, int fallback) {
    switch (value.metaType().id()) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::QString: {
            // INI values can be strings; signs, whitespace and rounded floats
            // must not turn a malformed preference into a different audio policy.
            const QString text = value.toString();
            bool valid = false;
            const int number = text.toInt(&valid);
            if (valid && number >= 0 && number <= maximum && text == QString::number(number))
                return number;
            return fallback;
        }
        default:
            return fallback;
    }
}

CupidGbAudioFilter configuredAudioFilter(const QSettings &settings) {
    return CupidGbAudioFilter(audioPreferenceInteger(settings.value("audio/highpassMode"), 2,
                                                     CUPID_GB_AUDIO_FILTER_ACCURATE));
}

int configuredAudioInterference(const QSettings &settings) {
    return audioPreferenceInteger(settings.value("audio/interferenceVolume"), 100, 0);
}

FrameBlending configuredFrameBlending(const QSettings &settings) {
    bool valid = false;
    const int mode = settings.value("video/blendingMode").toInt(&valid);
    if (valid && mode >= 0 && mode < int(frameBlendingNames.size()))
        return FrameBlending(mode);
    if (settings.contains("video/blending"))
        return settings.value("video/blending").toBool() ? FrameBlending::Simple
                                                         : FrameBlending::Disabled;
    return FrameBlending::Accurate;
}

// LCD geometry stays at 70,224 base cycles; elapsed time follows the model oscillator.
qint64 nominalFrameNanoseconds(const CupidGb *gb, unsigned speedSteps = 16,
                               unsigned turboQuarters = 4) {
    return qint64(uint64_t(70224) * 1000000000 * 64 /
                  (uint64_t(cupid_gb_base_clock_hz(gb)) * speedSteps * turboQuarters));
}

bool smokeFailure(const char *file, int line) {
    qCritical("CupidGB Qt workflow check failed at %s:%d", file, line);
    return false;
}

Qt::KeyboardModifiers keyModifier(int key) {
    switch (key) {
        case Qt::Key_Shift:
            return Qt::ShiftModifier;
        case Qt::Key_Control:
            return Qt::ControlModifier;
        case Qt::Key_Alt:
            return Qt::AltModifier;
        case Qt::Key_Meta:
            return Qt::MetaModifier;
        default:
            return Qt::NoModifier;
    }
}

int inputKey(int key) {
    // Qt names Shift+Tab separately, although the same physical Tab key remains held.
    return key == Qt::Key_Backtab ? Qt::Key_Tab : key;
}

// Button bindings also accept standalone modifiers and Tab, which ordinary shortcut
// editors treat as unfinished combinations or focus traversal.
class InputKeyEdit final : public QKeySequenceEdit {
  public:
    explicit InputKeyEdit(int key) : QKeySequenceEdit(QKeySequence(key)) {
    }

  protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            const int value = inputKey(key->key());
            if (value == Qt::Key_Tab || keyModifier(value) != Qt::NoModifier) {
                setKeySequence(QKeySequence(value));
                event->accept();
                return true;
            }
        }
        return QKeySequenceEdit::event(event);
    }
};

bool isStatePath(const QString &path) {
    const QString extension = QFileInfo(path).suffix().toLower();
    const auto digit = [](QChar character) { return character >= '0' && character <= '9'; };
    return extension == "bess" ||
           (extension.size() == 2 && extension[0] == 's' && digit(extension[1])) ||
           (extension.size() == 3 && extension.startsWith("sn") && digit(extension[2]));
}

CupidGbModel automaticModel(const QByteArray &rom) {
    if (rom.size() <= 0x14b)
        return CUPID_GB_MODEL_DMG_ABC;
    const bool color = (uint8_t(rom[0x143]) & 0x80) != 0;
    const bool super = uint8_t(rom[0x146]) == 3;
    const bool licensed = uint8_t(rom[0x14b]) == 1 ||
                          (uint8_t(rom[0x14b]) == 0x33 && rom[0x144] == '0' && rom[0x145] == '1');
    return color      ? CUPID_GB_MODEL_CGB
           : super    ? CUPID_GB_MODEL_SGB
           : licensed ? CUPID_GB_MODEL_CGB
                      : CUPID_GB_MODEL_DMG_ABC;
}
} // namespace

class DisplayWidget final : public QWidget {
  public:
    explicit DisplayWidget(QWidget *parent) : QWidget(parent) {
        setMinimumSize(160, 144);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        paintClock.start();
    }

    // The native-sized image keeps exact integer nearest and pixelated paints fast. Filtered
    // paints retain both original frames until spatial sampling is complete.
    void setFrames(const QImage &current, const QImage &previous, FrameBlending blending,
                   bool oddFrame) {
        currentFrame_ = current;
        previousFrame_ = previous;
        blending_ = blending;
        oddFrame_ = oddFrame;
        image = blendDisplayFrames(current, previous, blending, oddFrame);
        filteredSourceKey = 0;
        if (current.isNull())
            filteredImage = {};
    }

    QImage image;
    ScreenMessages messages;
    DisplayScaling scaling = DisplayScaling::Integer;
    ScalingFilter filter = ScalingFilter::Pixelated;
    QImage filteredImage;
    qint64 filteredSourceKey = 0;
    ScalingFilter cachedFilter = ScalingFilter::Nearest;
    uint64_t paintCount = 0;
    HostElapsedTimer paintClock;
    qint64 lastPaintAt = 0;
    bool collectPaintIntervals = false;
    std::vector<double> paintIntervals;

    QSize sizeHint() const override {
        return {320, 288};
    }

    QJsonObject paintTelemetry(size_t first = 0) const {
        if (first >= paintIntervals.size())
            return {};
        std::vector<double> intervals(paintIntervals.begin() + first, paintIntervals.end());
        std::sort(intervals.begin(), intervals.end());
        return {{"minimum", intervals.front()},
                {"median", intervals[intervals.size() / 2]},
                {"percentile_95", intervals[(intervals.size() - 1) * 95 / 100]},
                {"percentile_99", intervals[(intervals.size() - 1) * 99 / 100]},
                {"maximum", intervals.back()}};
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (image.isNull()) {
            painter.setPen(Qt::white);
            painter.drawText(rect(), Qt::AlignCenter,
                             "CupidGB\nOpen a Game Boy cartridge or GBS music file");
            return;
        }
        ++paintCount;
        if (collectPaintIntervals) {
            const qint64 now = paintClock.nsecsElapsed();
            if (lastPaintAt)
                paintIntervals.push_back(double(now - lastPaintAt) / 1e6);
            lastPaintAt = now;
        }
        const QRectF target = displayViewport(image.size(), size(), scaling, devicePixelRatioF());
        const QSizeF physicalTarget = target.size() * devicePixelRatioF();
        const QPointF physicalOrigin = target.topLeft() * devicePixelRatioF();
        const QSize output = physicalTarget.toSize();
        if ((filter == ScalingFilter::Nearest || filter == ScalingFilter::Pixelated) &&
            output.width() > 0 && output.height() > 0 && output.width() % image.width() == 0 &&
            output.height() % image.height() == 0 &&
            qFuzzyCompare(physicalTarget.width(), qreal(output.width())) &&
            qFuzzyCompare(physicalTarget.height(), qreal(output.height())) &&
            qFuzzyIsNull(physicalOrigin.x() - qRound(physicalOrigin.x())) &&
            qFuzzyIsNull(physicalOrigin.y() - qRound(physicalOrigin.y()))) {
            painter.drawImage(target, image);
            return;
        }
        if (filteredSourceKey != image.cacheKey() || filteredImage.size() != output ||
            cachedFilter != filter) {
            if (filter == ScalingFilter::Nearest)
                filteredImage = scaleNearestDisplayImage(image, output);
            else
                filteredImage = currentFrame_.isNull()
                                    ? filterDisplayImage(image, output, filter)
                                    : renderDisplayFrames(currentFrame_, previousFrame_, output,
                                                          filter, blending_, oddFrame_);
            filteredSourceKey = image.cacheKey();
            cachedFilter = filter;
        }
        painter.drawImage(target, filteredImage);
    }

  private:
    QImage currentFrame_;
    QImage previousFrame_;
    FrameBlending blending_ = FrameBlending::Disabled;
    bool oddFrame_ = false;
};

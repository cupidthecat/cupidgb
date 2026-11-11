#pragma once

#include "main_window.h"

#include "audio_converter.h"
#include "cartridge_file.h"
#include "controller_dialog.h"
#include "cupid/compat/native_v1.h"
#include "cupid/gb/battery.h"
#include "debugger_window.h"
#include "inspectors_window.h"
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
#include <QMutex>
#include <QMutexLocker>
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
#include "cupid/gb/state.h"
#include "cupid/gbc/cgb.h"
}

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
qint64 nominalFrameNanoseconds(const CupidGb *gb, unsigned speedSteps = 16) {
    return qint64(uint64_t(70224) * 1000000000 * 16 /
                  (uint64_t(cupid_gb_base_clock_hz(gb)) * speedSteps));
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
    return (extension.size() == 2 && extension[0] == 's' && digit(extension[1])) ||
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
        paintClock.start();
    }

    QImage image;
    DisplayScaling scaling = DisplayScaling::Integer;
    uint64_t paintCount = 0;
    QElapsedTimer paintClock;
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
                             "CupidGB\nOpen a Game Boy or Game Boy Color ROM");
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
        painter.drawImage(target, image);
    }
};

// Keep generated audio until the host accepts it. Backpressure pauses guest execution.
class AudioBuffer final {
  public:
    explicit AudioBuffer(int bytesPerFrame = 4) : bytesPerFrame_(bytesPerFrame) {
    }

    void append(const char *data, qsizetype size) {
        {
            QMutexLocker lock(&mutex_);
            queue_.append(data, size);
            maximumQueued_ = std::max(maximumQueued_, queue_.size());
        }
    }

    void clear() {
        QMutexLocker lock(&mutex_);
        queue_.clear();
    }

    qint64 bytesAvailable() const {
        QMutexLocker lock(&mutex_);
        return queue_.size();
    }

    void resetTelemetry() {
        QMutexLocker lock(&mutex_);
        bytesRead_ = 0;
        maximumQueued_ = queue_.size();
    }

    QJsonObject telemetry() const {
        QMutexLocker lock(&mutex_);
        return {{"host_frames_written_to_sink", double(bytesRead_) / bytesPerFrame_},
                {"maximum_queued_bytes", double(maximumQueued_)},
                {"remaining_queued_bytes", double(queue_.size())}};
    }

    qint64 writeTo(QIODevice *device, qint64 maximum) {
        QMutexLocker lock(&mutex_);
        const qint64 available = std::min(maximum, qint64(queue_.size()));
        const qint64 aligned = available - available % bytesPerFrame_;
        if (aligned <= 0)
            return 0;
        const qint64 written = device->write(queue_.constData(), aligned);
        if (written > 0) {
            queue_.remove(0, qsizetype(written));
            bytesRead_ += uint64_t(written);
        }
        return written;
    }

  private:
    mutable QMutex mutex_;
    QByteArray queue_;
    uint64_t bytesRead_ = 0;
    int bytesPerFrame_ = 4;
    qsizetype maximumQueued_ = 0;
};

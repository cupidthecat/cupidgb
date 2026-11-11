#include "main_window_internal.h"

#include <cmath>

std::optional<QPoint> MainWindow::pointerMotionSample(QPointF position, QSize source,
                                                      QSizeF available, DisplayScaling scaling,
                                                      qreal devicePixelRatio) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) ||
        !std::isfinite(available.width()) || !std::isfinite(available.height()) ||
        !std::isfinite(devicePixelRatio))
        return std::nullopt;
    const QRectF viewport = displayViewport(source, available, scaling, devicePixelRatio);
    if (viewport.width() <= 0 || viewport.height() <= 0)
        return std::nullopt;
    const auto guestCoordinate = [](qreal pointer, qreal origin, qreal extent, int sourceSize,
                                    int guestSize) {
        const double mapped = (pointer - origin) * sourceSize / extent;
        // Logical desktop scaling can place an exact guest pixel a few floating
        // point bits below its integer boundary. Keep integer pixel conversion
        // stable before applying the centered sensor window and clamping.
        const double pixel = mapped < 0 ? std::ceil(mapped - 1e-9) : std::floor(mapped + 1e-9);
        return std::clamp(pixel - (sourceSize - guestSize) / 2, 0.0, double(guestSize));
    };
    const double x =
        guestCoordinate(position.x(), viewport.left(), viewport.width(), source.width(), 160);
    const double y =
        guestCoordinate(position.y(), viewport.top(), viewport.height(), source.height(), 144);
    // The guest adds a positive neutral base before integer conversion. Taking
    // floor here retains the extra count for negative fractional accelerations.
    return QPoint(int(std::floor(112.0 * (1.0 - x / 80.0))),
                  int(std::floor(112.0 * (1.0 - y / 72.0))));
}

bool MainWindow::motionInputAllowed(bool mouse) const {
    if (!emulator_ || emulator_->gb.header.mbc_type != CUPID_GB_MBC7 || paused_ ||
        stateOperation_ || menuDepth_ || !inputTrace_.empty())
        return false;
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return false;
    const auto *active = QApplication::activeWindow();
    if (active && active != this)
        return false;
    if (!mouse && controllers_ && controllers_->configuration().background)
        return true;
    return motionFocusActive_ && (QApplication::applicationState() == Qt::ApplicationActive ||
                                  (isolated_ && !isVisible()));
}

void MainWindow::releaseMouseInput() {
    const bool released = mouseButtonA_;
    mouseButtonA_ = false;
    if (motionSource_ == MotionSource::Mouse) {
        if (emulator_ && emulator_->gb.header.mbc_type == CUPID_GB_MBC7)
            cupid_gb_mbc7_set_motion(&emulator_->gb, 0, 0);
        motionSource_ = MotionSource::None;
    }
    if (released && !stateOperation_ && !(paused_ && debuggerInputPaused_))
        applyHeldButtons();
}

void MainWindow::resetMotionInput() {
    releaseMouseInput();
    if (emulator_ && emulator_->gb.header.mbc_type == CUPID_GB_MBC7)
        cupid_gb_mbc7_set_motion(&emulator_->gb, 0, 0);
    motionSource_ = MotionSource::None;
    motionGateOpen_ = false;
}

void MainWindow::refreshMotionGate() {
    // Atomic loads retain live host input. Backend event dispatch during a
    // state operation must not turn a delivery gate into a new neutral sample.
    if (stateOperation_) {
        motionGateOpen_ = false;
        return;
    }
    const bool reopened = !motionGateOpen_;
    const bool allowed = motionInputAllowed();
    if (!allowed && (motionGateOpen_ || motionSource_ != MotionSource::None || mouseButtonA_))
        resetMotionInput();
    else if (!motionInputAllowed(true))
        releaseMouseInput();
    motionGateOpen_ = allowed;
    if (allowed && reopened)
        applyHeldButtons();
}

void MainWindow::controllerMotionChanged(const cupid::input::ControllerOutput &previous) {
    if (previous.motionAvailable == controllerOutput_.motionAvailable &&
        previous.motionX == controllerOutput_.motionX &&
        previous.motionY == controllerOutput_.motionY)
        return;
    const bool coordinatesChanged = lastControllerMotionX_ != controllerOutput_.motionX ||
                                    lastControllerMotionY_ != controllerOutput_.motionY;
    if (controllerOutput_.motionAvailable) {
        lastControllerMotionX_ = controllerOutput_.motionX;
        lastControllerMotionY_ = controllerOutput_.motionY;
    }
    if (!motionInputAllowed()) {
        refreshMotionGate();
        return;
    }
    motionGateOpen_ = true;
    if (!controllerOutput_.motionAvailable) {
        // A disconnected controller does not own the pointer's current sample.
        if (motionSource_ == MotionSource::Controller) {
            cupid_gb_mbc7_set_motion(&emulator_->gb, 0, 0);
            motionSource_ = MotionSource::None;
        }
        return;
    }
    // Availability-only resynchronization must not replace a newer pointer
    // sample. A different X/Y tuple is a newly delivered controller sample.
    if (motionSource_ == MotionSource::Mouse && !coordinatesChanged)
        return;
    cupid_gb_mbc7_set_motion(&emulator_->gb, controllerOutput_.motionX, controllerOutput_.motionY);
    motionSource_ = MotionSource::Controller;
}

bool MainWindow::handleMotionEvent(QObject *object, QEvent *event) {
    if (object != display_)
        return false;
    const bool moved = event->type() == QEvent::MouseMove;
    const bool pressed =
        event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick;
    const bool released = event->type() == QEvent::MouseButtonRelease;
    if (!moved && !pressed && !released)
        return false;
    // Synchronize device mode before a pointer event so an initial controller
    // resynchronization cannot immediately replace this newer mouse sample.
    refreshControllerGate();
    if (!motionInputAllowed(true) || !mouseMotionAction_->isChecked()) {
        if (released) {
            // A release received during an atomic operation updates host state;
            // the reopened gate reconciles it before another guest instruction.
            if (stateOperation_)
                mouseButtonA_ = false;
            else
                releaseMouseInput();
        }
        return false;
    }
    const auto *mouse = static_cast<QMouseEvent *>(event);
    if (moved) {
        const QSize source = display_->image.isNull() ? QSize(160, 144) : display_->image.size();
        const auto sample = pointerMotionSample(mouse->position(), source, display_->size(),
                                                display_->scaling, display_->devicePixelRatioF());
        if (sample) {
            cupid_gb_mbc7_set_motion(&emulator_->gb, int16_t(sample->x()), int16_t(sample->y()));
            motionSource_ = MotionSource::Mouse;
        }
    } else {
        mouseButtonA_ = pressed;
        if (pressed)
            display_->setFocus(Qt::MouseFocusReason);
        applyHeldButtons();
    }
    return true;
}

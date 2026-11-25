/**
 * @file    main_window_workboy.cpp
 * @brief   Qt serial keyboard attachment, key routing, and independent accessory clock.
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

#include <QDateTime>

extern "C" {
#include "cupid/gb/workboy.h"
}

namespace {
constexpr auto clockOffsetSetting = "workboy/clockOffsetSeconds";

uint8_t workBoyCharacter(QChar character) {
    const auto key = character.toLower().unicode();
    // Accessory key codes follow its physical rows independently of ASCII.
    constexpr char letters[] = "qwertyuiopasdfghjklzxcvbnm";
    constexpr uint8_t letterCodes[] = {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28, 29, 30,
                                       31, 32, 33, 34, 35, 36, 40, 41, 42, 43, 44, 45, 46};
    static_assert(std::size(letters) == std::size(letterCodes) + 1);
    const auto *letter = std::find(std::begin(letters), std::end(letters) - 1, key);
    if (letter != std::end(letters) - 1)
        return letterCodes[letter - letters];
    constexpr char shifted[] = "1234567890!~*#+-()%=<>?@";
    constexpr uint8_t shiftedCodes[] = {17, 18, 19, 28, 29, 30, 40, 41, 42, 51, 24, 25,
                                        26, 27, 31, 32, 35, 36, 44, 45, 47, 48, 49, 53};
    static_assert(std::size(shifted) == std::size(shiftedCodes) + 1);
    const auto *shiftedKey = std::find(std::begin(shifted), std::end(shifted) - 1, key);
    if (shiftedKey != std::end(shifted) - 1)
        return shiftedCodes[shiftedKey - shifted] | CUPID_GB_WORKBOY_REQUIRE_SHIFT;
    switch (key) {
        case '$':
            return 27 | CUPID_GB_WORKBOY_FORBID_SHIFT;
        case ';':
            return CUPID_GB_WORKBOY_KEY_COLON | CUPID_GB_WORKBOY_FORBID_SHIFT;
        case ':':
            return CUPID_GB_WORKBOY_KEY_COLON;
        case ',':
            return CUPID_GB_WORKBOY_KEY_COMMA;
        case '.':
            return CUPID_GB_WORKBOY_KEY_DOT;
        case '/':
            return CUPID_GB_WORKBOY_KEY_SLASH;
        case '\'':
            return CUPID_GB_WORKBOY_KEY_QUOTE;
        case '`':
            return CUPID_GB_WORKBOY_KEY_UMLAUT;
        case ' ':
            return CUPID_GB_WORKBOY_KEY_SPACE;
        default:
            return CUPID_GB_WORKBOY_KEY_NONE;
    }
}

uint8_t workBoyKey(const QKeyEvent *event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return CUPID_GB_WORKBOY_KEY_NONE;
    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F9)
        return uint8_t(CUPID_GB_WORKBOY_KEY_CLOCK + event->key() - Qt::Key_F1);
    switch (event->key()) {
        case Qt::Key_F10:
            return CUPID_GB_WORKBOY_KEY_UNKNOWN;
        case Qt::Key_Escape:
            return CUPID_GB_WORKBOY_KEY_ESCAPE;
        case Qt::Key_Backspace:
            return CUPID_GB_WORKBOY_KEY_BACKSPACE;
        case Qt::Key_Left:
            return CUPID_GB_WORKBOY_KEY_LEFT;
        case Qt::Key_Up:
            return CUPID_GB_WORKBOY_KEY_UP;
        case Qt::Key_Down:
            return CUPID_GB_WORKBOY_KEY_DOWN;
        case Qt::Key_Right:
            return CUPID_GB_WORKBOY_KEY_RIGHT;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return CUPID_GB_WORKBOY_KEY_ENTER;
        case Qt::Key_Shift:
            return CUPID_GB_WORKBOY_KEY_SHIFT_DOWN;
        case Qt::Key_Clear:
            return CUPID_GB_WORKBOY_KEY_M;
        case Qt::Key_Asterisk:
            if (event->modifiers() & Qt::KeypadModifier)
                return CUPID_GB_WORKBOY_KEY_H;
            break;
        case Qt::Key_Slash:
            if (event->modifiers() & Qt::KeypadModifier)
                return CUPID_GB_WORKBOY_KEY_J;
            break;
        case Qt::Key_Period:
            if (event->modifiers() & Qt::KeypadModifier)
                return CUPID_GB_WORKBOY_KEY_V | CUPID_GB_WORKBOY_REQUIRE_SHIFT;
            break;
        default:
            break;
    }
    if (!event->text().isEmpty())
        return workBoyCharacter(event->text().front());
    if (event->key() >= Qt::Key_Space && event->key() <= Qt::Key_AsciiTilde)
        return workBoyCharacter(QChar(ushort(event->key())));
    return CUPID_GB_WORKBOY_KEY_NONE;
}
} // namespace

int64_t MainWindow::readWorkBoyClock() const {
    bool valid = false;
    const qint64 stored = settings_.value(clockOffsetSetting, qint64(0)).toLongLong(&valid);
    // A corrupt preference must not overflow epoch arithmetic. The actual accessory
    // calendar spans a few centuries, comfortably inside this bound.
    constexpr qint64 limit = std::numeric_limits<qint64>::max() / 2;
    const qint64 offset = valid && stored >= -limit && stored <= limit ? stored : 0;
    return QDateTime::currentSecsSinceEpoch() - offset;
}

void MainWindow::writeWorkBoyClock(int64_t seconds) {
    if (suppressWorkBoyClockWrites_ || !QDateTime::fromSecsSinceEpoch(seconds).isValid())
        return;
    settings_.setValue(clockOffsetSetting, QDateTime::currentSecsSinceEpoch() - seconds);
    settings_.sync();
}

void MainWindow::setWorkBoyConnected(bool connected) {
    if (!connected) {
        if (workBoy_) {
            if (emulator_ && emulator_->gb.serial_link.user_data == workBoy_)
                cupid_gb_set_serial_link(&emulator_->gb, {});
            cupid_gb_workboy_destroy(workBoy_);
            workBoy_ = nullptr;
        }
        workBoyShiftHeld_ = false;
        workBoyAction_->setChecked(false);
        if (!printer_ && !linkSession_)
            disconnectAction_->setChecked(true);
        return;
    }
    if (!emulator_ || musicInfo_ || workBoy_)
        return;
    auto *device = cupid_gb_workboy_create(
        [](void *context) { return static_cast<MainWindow *>(context)->readWorkBoyClock(); },
        [](int64_t seconds, void *context) {
            static_cast<MainWindow *>(context)->writeWorkBoyClock(seconds);
        },
        this);
    if (!device) {
        workBoyAction_->setChecked(false);
        reportError("Could not connect the WorkBoy keyboard.");
        return;
    }
    const QPointer<MainWindow> self(this);
    disconnectLink();
    if (!self || !emulator_ || musicInfo_) {
        cupid_gb_workboy_destroy(device);
        if (self)
            workBoyAction_->setChecked(false);
        return;
    }
    setPrinterConnected(false);
    workBoy_ = device;
    workBoyShiftHeld_ = false;
    cupid_gb_set_serial_link(&emulator_->gb, cupid_gb_workboy_link(workBoy_));
    workBoyAction_->setChecked(true);
}

void MainWindow::resetWorkBoy() {
    if (!workBoy_)
        return;
    cupid_gb_workboy_reset(workBoy_);
    workBoyShiftHeld_ = false;
    cupid_gb_set_serial_link(&emulator_->gb, cupid_gb_workboy_link(workBoy_));
}

void MainWindow::releaseWorkBoyKeys() {
    if (!workBoy_)
        return;
    cupid_gb_workboy_set_key(workBoy_, workBoyShiftHeld_ ? CUPID_GB_WORKBOY_KEY_SHIFT_UP
                                                         : CUPID_GB_WORKBOY_KEY_NONE);
    workBoyShiftHeld_ = false;
}

bool MainWindow::handleWorkBoyKey(QKeyEvent *event, bool pressed, bool overrideShortcut) {
    if (!cupid_gb_workboy_enabled(workBoy_))
        return false;
    const uint8_t key = workBoyKey(event);
    if (key == CUPID_GB_WORKBOY_KEY_NONE) {
        if (!pressed && !event->isAutoRepeat())
            cupid_gb_workboy_set_key(workBoy_, CUPID_GB_WORKBOY_KEY_NONE);
        return false;
    }
    event->accept();
    if (overrideShortcut || event->isAutoRepeat())
        return true;
    if (event->key() == Qt::Key_Shift)
        workBoyShiftHeld_ = pressed;
    uint8_t pending = key;
    if (!pressed)
        pending = event->key() == Qt::Key_Shift ? CUPID_GB_WORKBOY_KEY_SHIFT_UP
                                                : CUPID_GB_WORKBOY_KEY_NONE;
    cupid_gb_workboy_set_key(workBoy_, pending);
    return true;
}

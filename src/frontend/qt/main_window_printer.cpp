/**
 * @file    main_window_printer.cpp
 * @brief   Qt printer attachment, host paper delivery, and device synchronization.
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

#include "main_window.h"
#include "printer_window.h"

#include <QAction>
#include <QMetaObject>
#include <QScopedValueRollback>

extern "C" {
#include "cupid/gb/printer.h"
}

void MainWindow::setPrinterConnected(bool connected) {
    if (connected) {
        disconnectLink();
        setWorkBoyConnected(false);
    }
    if (!connected) {
        if (printer_) {
            if (emulator_)
                cupid_gb_set_serial_link(&emulator_->gb, {});
            cupid_gb_printer_destroy(printer_);
            printer_ = nullptr;
            printerWindow_->setPrinting(false);
            printerWindow_->refresh(false);
        }
        disconnectAction_->setChecked(true);
        return;
    }
    if (!emulator_ || printer_)
        return;
    if (!printerWindow_) {
        printerWindow_ = new PrinterWindow(this, [this](const auto &action) {
            QScopedValueRollback<bool> operation(stateOperation_, true);
            const bool wasPaused = paused_;
            setPaused(true);
            action();
            setPaused(wasPaused);
        });
    }
    printer_ = cupid_gb_printer_create(
        [](const CupidGbPrinterImage *image, void *context) {
            auto *window = static_cast<MainWindow *>(context);
            // Paper already emitted by the guest belongs to the host. Copy it
            // now; deferred drawing must not lose it across a timeline change.
            const bool reveal = window->printerWindow_->append(
                image->pixels, image->height, image->top_margin, image->bottom_margin);
            const uint64_t session = window->sessionId_;
            QMetaObject::invokeMethod(
                window,
                [window, reveal, session] {
                    window->printerWindow_->refresh(reveal && window->emulator_ &&
                                                    window->sessionId_ == session);
                },
                Qt::QueuedConnection);
        },
        [](void *context) {
            auto *window = static_cast<MainWindow *>(context);
            window->printerWindow_->setPrinting(false);
            QMetaObject::invokeMethod(
                window, [window] { window->printerWindow_->refresh(false); }, Qt::QueuedConnection);
        },
        this);
    if (!printer_) {
        disconnectAction_->setChecked(true);
        reportError("Could not connect the Game Boy Printer.");
        return;
    }
    cupid_gb_set_serial_link(&emulator_->gb, cupid_gb_printer_link(printer_));
    printerAction_->setChecked(true);
}

void MainWindow::resetPrinter() {
    if (!printer_)
        return;
    cupid_gb_printer_reset(printer_);
    cupid_gb_set_serial_link(&emulator_->gb, cupid_gb_printer_link(printer_));
    printerWindow_->setPrinting(false);
    printerWindow_->refresh(false);
}

void MainWindow::updatePrinter() {
    if (printer_)
        cupid_gb_printer_update(printer_, &emulator_->gb);
}

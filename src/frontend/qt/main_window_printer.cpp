#include "main_window.h"
#include "printer_window.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QMenu>
#include <QPdfWriter>
#include <QScopedValueRollback>

extern "C" {
#include "cupid/gb/printer.h"
#include "cupid/gb/timer.h"
}

void MainWindow::setPrinterConnected(bool connected) {
    if (!connected) {
        if (printer_) {
            if (emulator_)
                cupid_gb_set_serial_link(&emulator_->gb, {});
            ++printerGeneration_;
            cupid_gb_printer_destroy(printer_);
            printer_ = nullptr;
            printerWindow_->setPrinting(false);
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
            const auto generation = window->printerGeneration_;
            const QByteArray pixels(reinterpret_cast<const char *>(image->pixels),
                                    qsizetype(image->height) * CUPID_GB_PRINTER_WIDTH);
            // The game is still executing inside the serial callback. Present after the slice.
            QMetaObject::invokeMethod(
                window,
                [window, generation, pixels, height = image->height, top = image->top_margin,
                 bottom = image->bottom_margin] {
                    if (window->printerGeneration_ == generation)
                        window->printerWindow_->append(
                            reinterpret_cast<const uint8_t *>(pixels.constData()), height, top,
                            bottom);
                },
                Qt::QueuedConnection);
        },
        [](void *context) {
            auto *window = static_cast<MainWindow *>(context);
            const auto generation = window->printerGeneration_;
            QMetaObject::invokeMethod(
                window,
                [window, generation] {
                    if (window->printerGeneration_ == generation)
                        window->printerWindow_->setPrinting(false);
                },
                Qt::QueuedConnection);
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
    ++printerGeneration_;
    cupid_gb_printer_reset(printer_);
    cupid_gb_set_serial_link(&emulator_->gb, cupid_gb_printer_link(printer_));
    printerWindow_->setPrinting(false);
}

void MainWindow::updatePrinter() {
    if (printer_)
        cupid_gb_printer_update(printer_, &emulator_->gb);
}

bool MainWindow::printerSmokeTest(const QString &path, const QString &directory) {
    MainWindow session(true, true);
    if (session.connectionsMenu_->isEnabled() || !session.openRom(path))
        return false;
    session.setPaused(true);
    session.printerAction_->trigger();
    if (!session.printer_ || !session.printerAction_->isChecked())
        return false;
    auto &gb = session.emulator_->gb;
    const auto send = [&gb](uint8_t value) {
        cupid_gb_write_u8(&gb, 0xff01, value);
        cupid_gb_write_u8(&gb, 0xff02, 0x81);
        cupid_gb_tick(&gb, 512 * 8);
        return cupid_gb_read_u8(&gb, 0xff01);
    };
    const auto packet = [&send](uint8_t command, const QByteArray &payload) {
        send(0x88);
        send(0x33);
        uint16_t checksum = 0;
        for (uint8_t byte :
             {command, uint8_t(0), uint8_t(payload.size()), uint8_t(payload.size() >> 8)}) {
            send(byte);
            checksum += byte;
        }
        for (unsigned char byte : payload) {
            send(byte);
            checksum += byte;
        }
        send(uint8_t(checksum));
        send(uint8_t(checksum >> 8));
        const uint8_t acknowledgment = send(0);
        send(0);
        return acknowledgment == 0x81;
    };
    QByteArray tiles(640, char(0xff));
    if (!packet(1, {}) || !packet(4, tiles) || !packet(2, QByteArray::fromHex("0123e440")))
        return false;
    QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
    const QString png = QDir(directory).filePath("printer-smoke.png");
    if (!session.printerWindow_->isVisible() || !session.printerWindow_->saveFeed(png))
        return false;
    const QImage image(png);
    if (image.size() != QSize(160, 21) || image.pixelColor(0, 0) != Qt::white ||
        image.pixelColor(159, 17) != Qt::black || image.pixelColor(159, 18) != Qt::white)
        return false;
    QFile pdf(QDir(directory).filePath("printer-smoke.pdf"));
    {
        QPdfWriter printer(pdf.fileName());
        printer.setResolution(144);
        if (!session.printerWindow_->printFeed(printer, printer.resolution()))
            return false;
    }
    if (!pdf.open(QIODevice::ReadOnly) || !pdf.read(5).startsWith("%PDF-"))
        return false;
    const QByteArray strip(160 * 8, char(0xaa));
    session.printerWindow_->append(reinterpret_cast<const uint8_t *>(strip.constData()), 8, 1, 0);
    if (!session.printerWindow_->saveFeed(png) || QImage(png).size() != QSize(160, 30) ||
        QImage(png).pixelColor(0, 22) != QColor(0xaa, 0xaa, 0xaa))
        return false;
    // Disconnect with a queued guest callback, then validate cancellation after reconnecting.
    if (!packet(1, {}) || !packet(4, tiles) || !packet(2, QByteArray::fromHex("0100e440")))
        return false;
    session.disconnectAction_->trigger();
    session.printerAction_->trigger();
    QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
    if (!session.printerWindow_->saveFeed(png) || QImage(png).height() != 30)
        return false;
    session.printerWindow_->hide();
    session.printerWindow_->append(reinterpret_cast<const uint8_t *>(strip.constData()), 8, 0, 0);
    if (!session.printerWindow_->saveFeed(png) || QImage(png).height() != 8)
        return false;
    session.disconnectAction_->trigger();
    return !session.printer_ && session.disconnectAction_->isChecked() && session.closeRom();
}

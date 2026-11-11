#pragma once

#include <QDialog>
#include <QImage>
#include <cstdint>
#include <functional>

class QLabel;
class QPagedPaintDevice;
class QPushButton;
class QScrollArea;

class PrinterWindow final : public QDialog {
  public:
    using ModalAction = std::function<void(const std::function<void()> &)>;

    PrinterWindow(QWidget *parent, ModalAction runWhilePaused);
    void append(const uint8_t *pixels, int height, int top, int bottom);
    void setPrinting(bool printing);
    bool saveFeed(const QString &path) const;
    bool printFeed(QPagedPaintDevice &printer, int resolution) const;

  private:
    void save();
    void print();

    ModalAction runWhilePaused_;
    QImage feed_;
    QLabel *image_;
    QLabel *status_;
    QScrollArea *scroll_;
    QPushButton *save_;
    QPushButton *print_;
    bool feedError_ = false;
};

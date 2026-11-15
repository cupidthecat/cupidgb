#pragma once

#include <QByteArrayView>
#include <QImage>
#include <QString>

QImage composeScreenMessage(const QImage &frame, const QImage &message);

// Messages follow guest presentation events. Host repaints never consume their
// lifetime, and the overlay is separate from screenshots and emulator state.
class ScreenMessages {
  public:
    void setEnabled(bool enabled);
    bool enabled() const;
    void show(const QString &message);
    void showCartridge(const QString &version, QByteArrayView rom);
    void advanceFrame(const QString &heldMode = {});
    void clear();
    QString text() const;
    unsigned remainingFrames() const;
    const QImage &image(const QSize &size) const;

  private:
    bool enabled_ = false;
    QString text_;
    unsigned remaining_ = 0;
    mutable QImage image_;
};

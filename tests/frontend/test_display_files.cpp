#include "display_files.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cassert>

namespace {
QByteArray readFile(const QString &path) {
    QFile input(path);
    assert(input.open(QIODevice::ReadOnly));
    return input.readAll();
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const DisplayPaletteFile original{
        {qRgb(223, 231, 191), qRgb(147, 169, 113), qRgb(72, 91, 57), qRgb(12, 20, 8)},
        qRgb(239, 244, 219)};
    const QByteArray bytes = encodeDisplayPalette(original);
    assert(bytes == QByteArray::fromHex("4c504253030c1408485b3993a971dfe7bf eff4db"));
    DisplayPaletteFile decoded;
    assert(decodeDisplayPalette(bytes, decoded));
    assert(decoded.shades == original.shades && decoded.off == original.off);

    // Automatic files carry their resolved RGB samples before optional editor metadata.
    auto automatic = bytes;
    automatic[4] = char(2);
    automatic.append(QByteArray::fromHex("ffffff7f00000080ffffffff"));
    assert(decodeDisplayPalette(automatic, decoded));
    assert(decoded.shades == original.shades && decoded.off == original.off);
    auto legacy = bytes.first(17);
    legacy[4] = char(1);
    assert(decodeDisplayPalette(legacy, decoded));
    assert(decoded.shades == original.shades && decoded.off == original.shades[0]);
    const DisplayPaletteFile before = decoded;
    for (qsizetype size = 0; size < bytes.size(); ++size) {
        assert(!decodeDisplayPalette(bytes.first(size), decoded));
        assert(decoded.shades == before.shades && decoded.off == before.off);
    }
    auto wrongSignature = bytes;
    wrongSignature[0] = 'X';
    assert(!decodeDisplayPalette(wrongSignature, decoded));
    assert(decoded.shades == before.shades && decoded.off == before.off);

    QTemporaryDir directory;
    assert(directory.isValid());
    assert(QDir(directory.path()).mkpath("folder.with.dots"));
    const QString cartridge =
        directory.filePath(QString::fromUtf8("folder.with.dots/caf\xc3\xa9.v2.gb"));
    const QDateTime timestamp(QDate(2026, 11, 15), QTime(13, 2, 3));
    QImage image(2, 2, QImage::Format_RGB32);
    image.setPixel(0, 0, qRgb(255, 0, 0));
    image.setPixel(1, 0, qRgb(0, 255, 0));
    image.setPixel(0, 1, qRgb(0, 0, 255));
    image.setPixel(1, 1, qRgb(3, 7, 11));
    QString error = "previous error";
    const QString first = saveTimestampedScreenshot(image, cartridge, timestamp, &error);
    assert(!first.isEmpty() && error.isEmpty());
    assert(QFileInfo(first).fileName() ==
           QString::fromUtf8("caf\xc3\xa9.v2 2026-11-15 13.02.03.png"));
    assert(QImage(first).convertToFormat(QImage::Format_RGB32) == image);
    const auto firstBytes = readFile(first);
    QImage later = image;
    later.fill(qRgb(11, 22, 33));
    const QString second = saveTimestampedScreenshot(later, cartridge, timestamp, &error);
    assert(QFileInfo(second).fileName() ==
           QString::fromUtf8("caf\xc3\xa9.v2 2026-11-15 13.02.03 2.png"));
    assert(first != second && readFile(first) == firstBytes);
    assert(QImage(second).convertToFormat(QImage::Format_RGB32) == later);
    const QString third = saveTimestampedScreenshot(image, cartridge, timestamp, &error);
    assert(QFileInfo(third).fileName() ==
           QString::fromUtf8("caf\xc3\xa9.v2 2026-11-15 13.02.03 3.png"));
    assert(saveTimestampedScreenshot({}, cartridge, timestamp, &error).isEmpty());
    assert(!error.isEmpty() && readFile(first) == firstBytes);
    assert(saveTimestampedScreenshot(image, {}, timestamp, &error).isEmpty());
    assert(saveTimestampedScreenshot(image, cartridge, {}, &error).isEmpty());
    assert(saveTimestampedScreenshot(image, directory.filePath("missing/cartridge.gb"), timestamp,
                                     &error)
               .isEmpty());
    assert(!error.isEmpty());
    assert(saveScreenshotFile(later, first, &error));
    assert(error.isEmpty() && QImage(first).convertToFormat(QImage::Format_RGB32) == later);
    const auto replacedBytes = readFile(first);
    assert(!saveScreenshotFile({}, first, &error));
    assert(!error.isEmpty() && readFile(first) == replacedBytes);
    assert(!saveScreenshotFile(image, directory.path(), &error));
    assert(!error.isEmpty());
}

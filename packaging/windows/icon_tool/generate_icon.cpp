#include <QBuffer>
#include <QDataStream>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QSvgRenderer>

#include <array>

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() != 4) {
        qCritical("Usage: cupidgb_generate_icon SOURCE.svg OUTPUT.ico OUTPUT.png");
        return 1;
    }
    QSvgRenderer svg(arguments[1]);
    if (!svg.isValid()) {
        qCritical("Invalid SVG input");
        return 1;
    }

    constexpr std::array<int, 7> sizes = {16, 24, 32, 48, 64, 128, 256};
    std::array<QByteArray, sizes.size()> pngs;
    for (size_t index = 0; index < sizes.size(); ++index) {
        QImage image(sizes[index], sizes[index], QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        svg.render(&painter, QRectF(0, 0, sizes[index], sizes[index]));
        painter.end();
        QBuffer buffer(&pngs[index]);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
            qCritical("Cannot encode icon image");
            return 1;
        }
    }

    QSaveFile icon(arguments[2]);
    QSaveFile png(arguments[3]);
    if (!icon.open(QIODevice::WriteOnly) || !png.open(QIODevice::WriteOnly)) {
        qCritical("Cannot open output files");
        return 1;
    }
    // PNG-backed ICO entries preserve alpha at each Windows shell icon size.
    QDataStream stream(&icon);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * quint32(sizes.size());
    for (size_t index = 0; index < sizes.size(); ++index) {
        const quint8 size = sizes[index] == 256 ? 0 : quint8(sizes[index]);
        stream << size << size << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(pngs[index].size()) << offset;
        offset += quint32(pngs[index].size());
    }
    for (const QByteArray &bytes : pngs) {
        if (icon.write(bytes) != bytes.size()) {
            qCritical("Cannot write icon images");
            return 1;
        }
    }
    if (stream.status() != QDataStream::Ok || png.write(pngs.back()) != pngs.back().size() ||
        !png.commit() || !icon.commit()) {
        qCritical("Cannot commit icon files");
        return 1;
    }
    return 0;
}

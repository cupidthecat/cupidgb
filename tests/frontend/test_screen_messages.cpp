#include "screen_messages.h"

#include <QByteArray>
#include <QColor>
#include <QGuiApplication>
#include <cassert>
#include <vector>

static void checkLifetime() {
    ScreenMessages messages;
    messages.show("Hidden");
    assert(!messages.enabled() && messages.text().isEmpty());
    messages.setEnabled(true);
    messages.show("State saved");
    for (unsigned frame = 0; frame < 29; ++frame) {
        messages.advanceFrame();
        assert(messages.text() == "State saved");
    }
    messages.advanceFrame();
    assert(messages.text().isEmpty() && messages.remainingFrames() == 0);
    messages.show("CupidGB\nCartridge");
    for (unsigned frame = 0; frame < 59; ++frame) {
        messages.advanceFrame();
        assert(!messages.text().isEmpty());
    }
    messages.advanceFrame();
    assert(messages.text().isEmpty());
    for (unsigned frame = 0; frame < 100; ++frame) {
        messages.advanceFrame("Rewinding...");
        assert(messages.text() == "Rewinding...");
    }
    for (unsigned frame = 0; frame < 29; ++frame)
        messages.advanceFrame();
    assert(messages.text().isEmpty());
    messages.show("Old session");
    messages.setEnabled(false);
    messages.advanceFrame("Fast forward...");
    messages.show("Hidden session");
    messages.setEnabled(true);
    assert(messages.text().isEmpty());
    messages.show("");
    assert(messages.remainingFrames() == 0);
}

static void checkRendering() {
    ScreenMessages messages;
    messages.setEnabled(true);
    messages.show("State saved");
    const unsigned lifetime = messages.remainingFrames();
    for (const QSize size : {QSize(160, 144), QSize(256, 224)}) {
        const QImage overlay = messages.image(size);
        assert(!overlay.isNull() && overlay.size() == size);
        bool hasWhite = false, hasBlack = false;
        for (int y = 0; y < size.height(); ++y) {
            for (int x = 0; x < size.width(); ++x) {
                const QRgb pixel = overlay.pixel(x, y);
                assert(pixel == qRgba(0, 0, 0, 0) || pixel == qRgba(0, 0, 0, 255) ||
                       pixel == qRgba(255, 255, 255, 255));
                if (y < size.height() - 24 || y >= size.height() - 4 || x < 4)
                    assert(qAlpha(pixel) == 0);
                if (qAlpha(pixel)) {
                    hasWhite |= qRed(pixel) == 255;
                    hasBlack |= qRed(pixel) == 0;
                }
            }
        }
        assert(hasWhite && hasBlack);
        // Resizing/repainting is host work and must not age a paused message.
        for (unsigned paint = 0; paint < 100; ++paint)
            assert(messages.image(size) == overlay);
        assert(messages.remainingFrames() == lifetime);
        QImage raw(size, QImage::Format_RGB32);
        raw.fill(QColor(128, 96, 64));
        const auto rawKey = raw.cacheKey();
        const QImage composed = composeScreenMessage(raw, overlay);
        assert(composed != raw && raw.pixel(8, size.height() - 12) == qRgb(128, 96, 64));
        assert(raw.cacheKey() == rawKey && composed.pixel(0, 0) == raw.pixel(0, 0));
        assert(composeScreenMessage(raw, {}).cacheKey() == rawKey);
        assert(composeScreenMessage(raw, QImage(1, 1, QImage::Format_RGB32)).cacheKey() == rawKey);
        assert(composeScreenMessage({}, overlay).isNull());
    }
    messages.show("Different text");
    const QImage changed = messages.image({160, 144});
    messages.show("State saved");
    assert(messages.image({160, 144}) != changed);
    messages.clear();
    assert(messages.image({160, 144}).isNull());
    messages.show(QString(4096, 'W'));
    assert(messages.text().size() == 512);
    assert(messages.image({}).isNull());
    assert(messages.image({160, 144}).size() == QSize(160, 144));
}

static void checkGlyphFaces() {
    ScreenMessages messages;
    messages.setEnabled(true);
    messages.show("OSD FIXTURE");
    for (const QSize size : {QSize(160, 144), QSize(256, 224)}) {
        QImage faces = messages.image(size);
        unsigned components = 0;
        unsigned totalPixels = 0;
        std::vector<QPoint> pending;
        for (int y = 0; y < faces.height(); ++y) {
            for (int x = 0; x < faces.width(); ++x) {
                if (faces.pixel(x, y) != qRgba(255, 255, 255, 255))
                    continue;
                ++components;
                unsigned pixels = 0;
                pending.push_back({x, y});
                faces.setPixel(x, y, 0);
                while (!pending.empty()) {
                    const QPoint point = pending.back();
                    pending.pop_back();
                    ++pixels;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const QPoint next = point + QPoint(dx, dy);
                            if (!faces.rect().contains(next) ||
                                faces.pixel(next) != qRgba(255, 255, 255, 255))
                                continue;
                            faces.setPixel(next, 0);
                            pending.push_back(next);
                        }
                    }
                }
                // These capital letters have continuous strokes and no dots.
                // Their faces must not fragment into isolated one-pixel pieces.
                assert(pixels >= 4);
                totalPixels += pixels;
            }
        }
        assert(components && components <= 10 && totalPixels >= 90);
    }
}

static void writeTitle(QByteArray &rom, qsizetype offset, const QByteArray &title) {
    rom.replace(offset + 0x134, 16, QByteArray(16, char(0)));
    rom.replace(offset + 0x134, title.size(), title);
}

static void checkCartridgeIdentity() {
    ScreenMessages messages;
    QByteArray ordinary(32768, char(0));
    writeTitle(ordinary, 0, "ORDINARY");
    messages.showCartridge("0.3.0", ordinary);
    assert(messages.text().isEmpty());
    messages.setEnabled(true);
    const auto check = [&](const QByteArray &rom, const char *title, const char *crc) {
        const QByteArray original = rom;
        messages.showCartridge("0.3.0", rom);
        assert(messages.text() == QString("CupidGB v0.3.0\n%1\n%2").arg(title, crc));
        assert(messages.remainingFrames() == 90 && rom == original);
    };
    // Expected CRCs were computed independently with the standard CRC-32 oracle
    // over explicit padded images. The UI must not allocate a padded ROM copy or
    // alter cartridge data to display this identity.
    check(ordinary, "ORDINARY", "C9D65D22");
    for (unsigned frame = 0; frame < 89; ++frame) {
        messages.advanceFrame();
        assert(!messages.text().isEmpty());
    }
    messages.advanceFrame();
    assert(messages.text().isEmpty());
    QByteArray partial(0x150, char(0));
    writeTitle(partial, 0, "PADDED");
    check(partial, "PADDED", "4C24D766");
    QByteArray irregular(0x8001, char(0x5a));
    writeTitle(irregular, 0, "ABCDEFGHIJKLMNO");
    irregular[0x143] = char(0x80);
    irregular[0x147] = char(0x1b);
    check(irregular, "ABCDEFGHIJKLMNO", "1C4C902A");
    QByteArray menu(32768, char(0x11));
    QByteArray game(32768, char(0x22));
    writeTitle(menu, 0, "MENU");
    writeTitle(game, 0, "FIRST GAME");
    menu[0x147] = char(0x0b);
    game[0x147] = char(0x01);
    check(menu + game, "FIRST GAME", "BFB39DAE");
    check(game + menu, "FIRST GAME", "BFB39DAE");
    check(menu + game.left(0x1000), "FIRST GAME", "36F07D6C");
    QByteArray control(32768, char(0));
    writeTitle(control, 0, "ABC\x01IGNORED");
    check(control, "ABC", "9B129CB1");
    messages.showCartridge("0.3.0", QByteArrayView("short"));
    assert(messages.text().isEmpty() && messages.remainingFrames() == 0);
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    checkLifetime();
    checkRendering();
    checkGlyphFaces();
    checkCartridgeIdentity();
}

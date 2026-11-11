#include "color_pipeline.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <cassert>
#include <cstring>
#include <fstream>

int main(int argc, char **argv) {
    assert(argc == 1 || (argc == 3 && std::strcmp(argv[1], "--write-tables") == 0));
    std::ofstream output;
    if (argc == 3) {
        output.open(argv[2], std::ios::binary);
        assert(output);
    }
    const auto raw = makeColorTable(ColorHardware::Cgb, {ColorCorrection::Disabled, 10});
    for (unsigned color = 0; color < raw.size(); ++color) {
        const unsigned r = color & 31u;
        const unsigned g = (color >> 5u) & 31u;
        const unsigned b = (color >> 10u) & 31u;
        assert(raw[color] ==
               qRgb((r << 3u) | (r >> 2u), (g << 3u) | (g >> 2u), (b << 3u) | (b >> 2u)));
    }
    assert(makeColorTable(ColorHardware::Cgb) ==
           makeColorTable(ColorHardware::Cgb, {ColorCorrection::Balanced, 10}));
    assert(makeColorTable(ColorHardware::Cgb, {ColorCorrection(-1), -10}) ==
           makeColorTable(ColorHardware::Cgb, {ColorCorrection::Balanced, 0}));
    assert(makeColorTable(ColorHardware::Sgb, {ColorCorrection::Curves, 100}) ==
           makeColorTable(ColorHardware::Sgb, {ColorCorrection::Accurate, 20}));
    assert(makeColorTable(ColorHardware::Cgb, {ColorCorrection::Disabled, 0})[32767] ==
           qRgb(183, 210, 255));
    assert(makeColorTable(ColorHardware::Cgb, {ColorCorrection::Disabled, 20})[32767] ==
           qRgb(255, 0, 0));

    // Hash explicit RGB bytes so the complete color domain is independent of host byte order.
    const std::array<const char *, 2> expected = {
        "b7779c6c42acfbf268b844dfcd30dac709c3acfdaaadfaad85b6b5920ed07d89",
        "30e471b34a732ace7228b5cc5d6fa3c0c0a903b321889ba51c564cdc154cbbaa"};
    for (size_t hardware = 0; hardware < expected.size(); ++hardware) {
        QCryptographicHash digest(QCryptographicHash::Sha256);
        for (int mode = 0; mode <= int(ColorCorrection::Accurate); ++mode) {
            for (int temperature = 0; temperature <= 20; ++temperature) {
                const auto colors =
                    makeColorTable(ColorHardware(hardware), {ColorCorrection(mode), temperature});
                QByteArray bytes(qsizetype(colors.size() * 3), Qt::Uninitialized);
                auto *destination = reinterpret_cast<unsigned char *>(bytes.data());
                for (QRgb color : colors) {
                    *destination++ = qRed(color);
                    *destination++ = qGreen(color);
                    *destination++ = qBlue(color);
                    assert(qAlpha(color) == 255);
                }
                digest.addData(bytes);
                if (output.is_open())
                    output.write(bytes.constData(), bytes.size());
            }
        }
        const auto actual = digest.result().toHex();
        assert(actual == expected[hardware]);
    }
    assert(!output.is_open() || output.good());
}

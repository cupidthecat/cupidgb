#include "inspector_renderer.h"
#include "inspectors_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>

extern "C" {
#include "cupid/gb/debugger.h"
#include "cupid/gb/state.h"
}

namespace {
std::unique_ptr<CupidGb> machine(bool color) {
    auto gb = std::make_unique<CupidGb>();
    cupid_gb_init(gb.get());
    cupid_gb_set_model(gb.get(), color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC);
    std::vector<uint8_t> rom(32768);
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    rom[0x143] = color ? 0x80 : 0;
    assert(cupid_gb_load_rom(gb.get(), rom.data(), rom.size()));
    gb->io_registers[0x47] = 0xe4;
    gb->io_registers[0x48] = 0xe4;
    gb->io_registers[0x49] = 0xe4;
    return gb;
}

std::vector<uint8_t> snapshot(const CupidGb &gb) {
    std::vector<uint8_t> bytes(cupid_gb_state_size(&gb));
    assert(cupid_gb_save_state(&gb, bytes.data(), bytes.size(), nullptr) == CUPID_GB_STATE_OK);
    return bytes;
}

void setPixel(CupidGb &gb, unsigned bank, unsigned tile, unsigned x, unsigned y, unsigned pixel) {
    const size_t offset = bank * 0x2000u + tile * 16u + y * 2u;
    const uint8_t bit = uint8_t(1u << (7u - x));
    gb.video_ram[offset] =
        uint8_t((gb.video_ram[offset] & uint8_t(~bit)) | (pixel & 1u ? bit : 0u));
    gb.video_ram[offset + 1] =
        uint8_t((gb.video_ram[offset + 1] & uint8_t(~bit)) | (pixel & 2u ? bit : 0u));
}

void setColor(CupidGb &gb, bool object, unsigned index, unsigned entry, uint16_t color) {
    auto *bytes = object ? gb.cgb_obj_palette_ram : gb.cgb_bg_palette_ram;
    const unsigned offset = index * 8u + entry * 2u;
    bytes[offset] = uint8_t(color);
    bytes[offset + 1] = uint8_t(color >> 8u);
}

void testTilePlanes() {
    auto gb = machine(true);
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned tile = 0; tile < 384; ++tile)
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x)
                    setPixel(*gb, bank, tile, x, y, (bank + tile + x + y) % 4u);
    const auto before = snapshot(*gb);
    const auto image = inspectorTiles(*gb, -1);
    assert(image.size() == QSize(256, 192));
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned tile = 0; tile < 384; ++tile)
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x) {
                    const int expected = 255 - int((bank + tile + x + y) % 4u) * 85;
                    assert(image.pixel(int(bank * 128u + tile % 16u * 8u + x),
                                       int(tile / 16u * 8u + y)) ==
                           qRgb(expected, expected, expected));
                }
    assert(inspectorTiles(*gb, 1) == image.copy(128, 0, 128, 192));
    assert(inspectorTiles(*gb, -2).isNull());
    assert(inspectorTiles(*gb, 2).isNull());
    assert(snapshot(*gb) == before);
    gb->model = CUPID_GB_MODEL_DMG_ABC;
    gb->cgb_mode = false;
    const auto monochrome = inspectorTiles(*gb, -1);
    assert(monochrome.pixel(128, 0) == qRgb(255, 255, 255));
    cupid_gb_cleanup(gb.get());
}

void testMapsAndAttributes() {
    auto gb = machine(true);
    const uint16_t red = 0x001f, green = 0x03e0, blue = 0x7c00;
    setColor(*gb, false, 3, 1, red);
    setColor(*gb, false, 3, 2, green);
    setColor(*gb, true, 2, 1, blue);
    gb->io_registers[0x40] = 0x91;
    gb->video_ram[0x1800] = 5;
    gb->video_ram[0x3800] = 3u | 8u | 0x20u | 0x40u | 0x80u;
    setPixel(*gb, 1, 5, 7, 7, 1);
    setPixel(*gb, 1, 5, 6, 7, 2);
    const auto before = snapshot(*gb);
    auto image = inspectorMap(*gb, InspectorMap::Background);
    assert(image.pixel(0, 0) == qRgb(255, 0, 0));
    assert(image.pixel(1, 0) == qRgb(0, 255, 0));
    const auto forced = inspectorMap(*gb, InspectorMap::Low, InspectorTileAddressing::Unsigned,
                                     {InspectorPalette::Object, 2});
    assert(forced.pixel(0, 0) == qRgb(0, 0, 255));
    const auto raw = inspectorMap(*gb, InspectorMap::Low, InspectorTileAddressing::Unsigned, {});
    assert(raw.pixel(0, 0) == qRgb(170, 170, 170));
    const auto info = inspectorMapCell(*gb, InspectorMap::Background, 0, 0);
    assert(info.contains("Bank 1") && info.contains("Palette 3") && info.contains("Priority 1"));
    assert(inspectorMapCell(*gb, InspectorMap::Background, -1, 0).isEmpty());
    assert(inspectorMapCell(*gb, InspectorMap::Background, 256, 0).isEmpty());
    assert(snapshot(*gb) == before);

    gb->video_ram[0x1c00] = 0xff;
    gb->video_ram[0x3c00] = 0;
    setPixel(*gb, 0, 255, 0, 0, 3);
    gb->io_registers[0x40] = 0xc1;
    const auto signedWindow =
        inspectorMap(*gb, InspectorMap::Window, InspectorTileAddressing::Automatic, {});
    assert(signedWindow.pixel(0, 0) == qRgb(0, 0, 0));
    const auto signedExplicit =
        inspectorMap(*gb, InspectorMap::High, InspectorTileAddressing::Signed, {});
    assert(signedExplicit == signedWindow);
    gb->cgb_mode = false;
    gb->model = CUPID_GB_MODEL_DMG_ABC;
    gb->io_registers[0x47] = 0x1b;
    setPixel(*gb, 0, 255, 0, 0, 0);
    const auto dmg = inspectorMap(*gb, InspectorMap::Window);
    assert(dmg.pixel(0, 0) == qRgb(0, 0, 0));
    gb->io_registers[0x49] = 0xff;
    setPixel(*gb, 0, 255, 0, 0, 1);
    const auto objectMap =
        inspectorMap(*gb, InspectorMap::Window, InspectorTileAddressing::Automatic,
                     {InspectorPalette::Object, 1});
    assert(objectMap.pixel(0, 0) == qRgb(170, 170, 170));
    gb->model = CUPID_GB_MODEL_CGB;
    setColor(*gb, false, 3, 2, 0x03e0);
    const auto compatibilityTiles = inspectorTiles(*gb, 0, {InspectorPalette::Background, 3});
    // BGP maps tile color one to shade two; the chosen hardware palette remains 3.
    assert(compatibilityTiles.pixel(15 * 8, 15 * 8) == qRgb(0, 255, 0));
    cupid_gb_cleanup(gb.get());
}

void testObjectsAndPalettes() {
    auto gb = machine(true);
    gb->io_registers[0x40] = 0x97;
    for (unsigned index = 0; index < 11; ++index) {
        auto *object = gb->object_attribute_memory + index * 4u;
        object[0] = 16;
        object[1] = 8;
        object[2] = 3;
        object[3] = 2u | 8u | 0x20u | 0x40u | 0x80u;
    }
    setColor(*gb, true, 2, 2, 0x03e0);
    setPixel(*gb, 1, 3, 7, 7, 2);
    const auto before = snapshot(*gb);
    const auto object = inspectorObject(*gb, 0);
    assert(object.address == 0xfe00 && object.x == 0 && object.y == 0);
    assert(object.height == 16 && object.tile == 2 && object.bank == 1 && object.palette == 2);
    assert(object.image.pixel(0, 0) == qRgb(0, 255, 0));
    assert(object.visibleLines && !object.lineLimited);
    assert(inspectorObject(*gb, 10).lineLimited);
    assert(!inspectorObject(*gb, 39).visibleLines);
    assert(inspectorObject(*gb, 40).image.isNull());
    assert(snapshot(*gb) == before);
    setColor(*gb, false, 7, 3, 0xffff);
    assert(inspectorPaletteValues(*gb, false, 7)[3] == 0xffff);
    assert(inspectorRgb555(0xffff) == qRgb(255, 255, 255));
    // A separate full hardware copy makes the nonmutating-renderer check independent
    // of the wire-format encoding used for snapshots.
    const auto copy = std::make_unique<CupidGb>(*gb);
    inspectorObject(*gb, 10);
    inspectorTiles(*gb, -1, {InspectorPalette::Object, 2});
    inspectorMap(*gb, InspectorMap::Low);
    assert(std::memcmp(gb.get(), copy.get(), sizeof(*gb)) == 0);
    cupid_gb_cleanup(gb.get());
}

void testWindowWorkflows() {
    auto gb = machine(true);
    CupidGb *session = gb.get();
    bool paused = false, invalidated = false;
    unsigned writes = 0;
    InspectorsWindow window;
    window.setSession([&] { return session; },
                      [&](uint16_t address, uint8_t value) {
                          ++writes;
                          paused = invalidated = true;
                          return cupid_gb_debug_poke(session, address, value);
                      });
    window.show();
    window.findChild<QCheckBox *>("inspectorLive")->setChecked(false);
    QApplication::processEvents();
    const auto initial = snapshot(*gb);
    for (int tab = 0; tab < 5; ++tab) {
        window.selectTab(tab);
        window.refresh();
        QApplication::processEvents();
    }
    assert(snapshot(*gb) == initial && writes == 0);
    assert(window.findChild<QTableWidget *>("inspectorPalettes")->rowCount() == 16);

    auto *address = window.findChild<QLineEdit *>("inspectorAddress");
    auto *go = window.findChild<QPushButton *>("inspectorGo");
    auto *memory = window.findChild<QTableWidget *>("inspectorMemory");
    address->setText("C000");
    go->click();
    memory->editItem(memory->item(0, 0));
    QApplication::processEvents();
    QPointer<QLineEdit> editor = memory->findChild<QLineEdit *>();
    assert(editor);
    editor->setText("7c");
    window.findChild<QCheckBox *>("inspectorLive")->setChecked(true);
    QEventLoop editing;
    QTimer::singleShot(350, &editing, &QEventLoop::quit);
    editing.exec();
    assert(editor && editor->text() == "7c" && writes == 0 && gb->work_ram[0] == 0);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(editor.data(), &enter);
    QApplication::processEvents();
    window.findChild<QCheckBox *>("inspectorLive")->setChecked(false);
    assert(writes == 1 && paused && invalidated && gb->work_ram[0] == 0x7c);
    memory->item(0, 0)->setText("GG");
    assert(writes == 1 && gb->work_ram[0] == 0x7c && memory->item(0, 0)->text() == "7C");
    assert(!window.writeByte(0x100, 0) && !window.writeByte(0xfea0, 0) && writes == 1);
    auto *writeAddress = window.findChild<QLineEdit *>("inspectorWriteAddress");
    auto *writeValue = window.findChild<QLineEdit *>("inspectorWriteValue");
    auto *write = window.findChild<QPushButton *>("inspectorWrite");
    writeAddress->setText("$C001");
    writeValue->setText("0xA5");
    write->click();
    assert(gb->work_ram[1] == 0xa5 && writes == 2);
    writeValue->setText("100");
    write->click();
    assert(gb->work_ram[1] == 0xa5 && writes == 2);
    address->setText("10000");
    go->click();
    assert(memory->verticalHeaderItem(0)->text() == "C000");
    address->setText("0000");
    go->click();
    assert(!(memory->item(0, 0)->flags() & Qt::ItemIsEditable));

    gb->io_registers[0x41] = 0x83;
    gb->io_registers[0x44] = 0;
    gb->io_registers[0x40] |= 0x80;
    gb->video_ram[0x2000] = 0x36;
    gb->cgb_vram_bank = 1;
    assert(cupid_gb_read_u8(gb.get(), 0x8000) == 0xff);
    address->setText("8000");
    go->click();
    assert(memory->item(0, 0)->text() == "36");
    memory->item(0, 0)->setText("81");
    assert(gb->video_ram[0x2000] == 0x81 && writes == 3);

    auto other = machine(false);
    memory->editItem(memory->item(0, 0));
    QApplication::processEvents();
    QPointer<QLineEdit> staleEditor = memory->findChild<QLineEdit *>();
    assert(staleEditor);
    staleEditor->setText("22");
    session = other.get();
    window.sessionChanged();
    QApplication::processEvents();
    assert(!staleEditor && writes == 3 && gb->video_ram[0x2000] == 0x81 &&
           other->video_ram[0] == 0);
    window.selectTab(4);
    window.refresh();
    assert(window.findChild<QTableWidget *>("inspectorPalettes")->rowCount() == 3);
    assert(window.findChild<QLabel *>("inspectorContext")->text().contains("VRAM bank 0"));
    session = nullptr;
    const auto retained = snapshot(*gb);
    for (int tab = 0; tab < 5; ++tab) {
        window.selectTab(tab);
        window.refresh();
    }
    assert(!window.writeByte(0xc000, 2) && writes == 3);
    assert(memory->item(0, 0) == nullptr);
    assert(window.findChild<QLabel *>("inspectorContext")->text() == "No cartridge loaded");
    assert(snapshot(*gb) == retained);
    window.setSession({}, {});
    window.close();
    QApplication::processEvents();
    cupid_gb_cleanup(other.get());
    cupid_gb_cleanup(gb.get());
}
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    testTilePlanes();
    testMapsAndAttributes();
    testObjectsAndPalettes();
    testWindowWorkflows();
    return 0;
}

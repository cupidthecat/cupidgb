/**
 * @file    test_inspectors.cpp
 * @brief   Qt tests for mapped/physical memory editing and graphics inspection.
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

#include "inspector_renderer.h"
#include "inspectors_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>

extern "C" {
#include "cupid/gb/cartridge.h"
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

void testColorRevisionCapabilities() {
    auto gb = machine(true);
    InspectorsWindow window;
    window.setSession([&] { return gb.get(); },
                      [&](uint16_t address, uint8_t value) {
                          return cupid_gb_debug_poke(gb.get(), address, value);
                      });
    for (const auto revision : {CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A, CUPID_GB_MODEL_CGB_B,
                                CUPID_GB_MODEL_CGB_C, CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB}) {
        cupid_gb_set_model(gb.get(), revision);
        assert(inspectorMemoryBankCount(*gb, InspectorMemoryRegion::VideoRam) == 2);
        assert(inspectorMemoryBankCount(*gb, InspectorMemoryRegion::WorkRam) == 8);
        setColor(*gb, false, 7, 3, 0x1234);
        assert(inspectorPaletteValues(*gb, false, 7)[3] == 0x1234);
        assert(inspectorTiles(*gb, 1).size() == QSize(128, 192));
        window.sessionChanged();
        window.selectTab(4);
        window.refresh();
        assert(window.findChild<QTableWidget *>("inspectorPalettes")->rowCount() == 16);
        window.selectTab(0);
        window.findChild<QLineEdit *>("inspectorAddress")->setText("FE00");
        window.findChild<QPushButton *>("inspectorGo")->click();
        const bool extra = revision != CUPID_GB_MODEL_CGB;
        const auto *cell = window.findChild<QTableWidget *>("inspectorMemory")->item(10, 0);
        assert(bool(cell->flags() & Qt::ItemIsEditable) == extra);
        assert(window.writeByte(0xfea0, 0x6c) == extra);
        if (extra)
            assert(cupid_gb_debug_peek(gb.get(), 0xfea0) == 0x6c);
    }
    cupid_gb_cleanup(gb.get());
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
    auto colors = std::make_unique<ColorTable>();
    colors->fill(qRgb(9, 17, 25));
    const auto corrected =
        inspectorMap(*gb, InspectorMap::Background, InspectorTileAddressing::Automatic,
                     {InspectorPalette::Automatic, 0}, colors.get());
    const auto correctedRaw =
        inspectorMap(*gb, InspectorMap::Low, InspectorTileAddressing::Unsigned, {}, colors.get());
    assert(corrected.pixel(0, 0) == qRgb(9, 17, 25));
    assert(correctedRaw == raw);
    assert(inspectorTiles(*gb, 1, {InspectorPalette::Background, 3}, colors.get()).pixel(0, 0) ==
           qRgb(9, 17, 25));
    assert(inspectorObject(*gb, 0, colors.get()).image.pixel(0, 0) == qRgb(9, 17, 25));
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

void testViewportAndGrids() {
    for (const QPoint scroll : {QPoint(0, 0), QPoint(96, 112), QPoint(97, 113), QPoint(240, 0),
                                QPoint(0, 240), QPoint(240, 240), QPoint(255, 255)}) {
        const QRegion visible = inspectorViewport(scroll);
        QImage image(256, 256, QImage::Format_RGB32);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            paintInspectorOverlays(painter, image.rect(), image.size(),
                                   {InspectorGrid::None, true, scroll});
        }
        unsigned count = 0;
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                // Membership follows a wrapped source coordinate, independently
                // of how the renderer splits the viewport into rectangles.
                const bool expected =
                    (x - scroll.x() + 256) % 256 < 160 && (y - scroll.y() + 256) % 256 < 144;
                count += expected;
                assert(visible.contains(QPoint(x, y)) == expected);
                assert((image.pixel(x, y) == qRgb(255, 255, 255)) == expected);
            }
        assert(count == 160 * 144);
    }

    const QColor margin(203, 217, 229);
    const QRect target(11, 7, 512, 384);
    QImage plain(540, 400, QImage::Format_RGB32);
    plain.fill(margin);
    {
        QPainter painter(&plain);
        painter.fillRect(target, Qt::white);
    }
    auto paintGrid = [&](InspectorGrid grid) {
        QImage image = plain;
        QPainter painter(&image);
        paintInspectorOverlays(painter, target, QSize(256, 192), {grid, false, {}});
        return image;
    };
    assert(paintGrid(InspectorGrid::None) == plain);
    const auto tiles = paintGrid(InspectorGrid::Tiles);
    const auto map = paintGrid(InspectorGrid::Map);
    const int small = qRed(tiles.pixel(target.left() + 15, target.top() + 4));
    const int bank = qRed(tiles.pixel(target.left() + 255, target.top() + 4));
    assert(bank < small && small < 255);
    assert(qRed(tiles.pixel(target.left() + 4, target.top() + 127)) == bank);
    assert(qRed(map.pixel(target.left() + 255, target.top() + 4)) == small);
    assert(qRed(map.pixel(target.left() + 4, target.top() + 127)) == small);
    assert(tiles.pixel(target.left() + 14, target.top() + 4) == qRgb(255, 255, 255));
    assert(tiles.pixel(target.left() + 16, target.top() + 4) == qRgb(255, 255, 255));
    unsigned verticalLines = 0;
    for (int x = target.left(); x <= target.right(); ++x)
        verticalLines += tiles.pixel(x, target.top() + 4) != qRgb(255, 255, 255);
    assert(verticalLines == 31);
    assert(tiles.pixel(target.left() - 1, target.top()) == margin.rgb());
    assert(tiles.pixel(target.right() + 1, target.bottom()) == margin.rgb());
    assert(plain.pixel(target.left() + 15, target.top() + 4) == qRgb(255, 255, 255));

    QImage scaled(540, 540, QImage::Format_RGB32);
    scaled.fill(margin);
    const QRect mapTarget(11, 7, 512, 512);
    {
        QPainter painter(&scaled);
        painter.fillRect(mapTarget, Qt::white);
        paintInspectorOverlays(painter, mapTarget, QSize(256, 256),
                               {InspectorGrid::None, true, QPoint(240, 240)});
    }
    for (const QPoint corner : {QPoint(8, 8), QPoint(248, 8), QPoint(8, 248), QPoint(248, 248)})
        assert(scaled.pixel(mapTarget.topLeft() + corner * 2) == qRgb(255, 255, 255));
    assert(scaled.pixel(mapTarget.topLeft() + QPoint(400, 400)) != qRgb(255, 255, 255));
    assert(scaled.pixel(mapTarget.left() - 1, mapTarget.top()) == margin.rgb());
    assert(scaled.pixel(mapTarget.right() + 1, mapTarget.bottom()) == margin.rgb());
}

QRgb imageViewPixel(QWidget *view, const QImage &capture, QSize nativeSize, QPoint point) {
    const int scale = std::max(
        1, std::min(view->width() / nativeSize.width(), view->height() / nativeSize.height()));
    const QPoint origin((view->width() - nativeSize.width() * scale) / 2,
                        (view->height() - nativeSize.height() * scale) / 2);
    const QPoint sample = origin + point * scale + QPoint(scale / 2, scale / 2);
    const qreal ratio = capture.devicePixelRatio();
    return capture.pixel(int((sample.x() + 0.5) * ratio), int((sample.y() + 0.5) * ratio));
}

void testOverlayWorkflows() {
    auto gb = machine(true);
    setColor(*gb, false, 0, 0, 0x7fff);
    gb->io_registers[0x43] = gb->io_registers[0x42] = 0;
    CupidGb *session = gb.get();
    unsigned writes = 0;
    InspectorsWindow window;
    window.setSession([&] { return session; },
                      [&](uint16_t, uint8_t) {
                          ++writes;
                          return false;
                      });
    window.show();
    auto *live = window.findChild<QCheckBox *>("inspectorLive");
    auto *grid = window.findChild<QCheckBox *>("inspectorGrid");
    auto *scrolling = window.findChild<QCheckBox *>("inspectorScrolling");
    auto *mapView = window.findChild<QWidget *>("inspectorMapImage");
    auto *tilesView = window.findChild<QWidget *>("inspectorTileImage");
    assert(live && grid && scrolling && mapView && tilesView);
    live->setChecked(false);
    assert(!grid->isChecked() && !scrolling->isChecked());
    assert(!grid->isVisible() && !scrolling->isVisible());
    const auto initial = snapshot(*gb);
    window.selectTab(2);
    QApplication::processEvents();
    assert(grid->isVisible() && scrolling->isVisible());
    const QImage plain = mapView->grab().toImage();
    scrolling->setChecked(true);
    const QImage viewport = mapView->grab().toImage();
    assert(viewport != plain);
    assert(imageViewPixel(mapView, viewport, {256, 256}, {20, 20}) == qRgb(255, 255, 255));
    assert(imageViewPixel(mapView, viewport, {256, 256}, {220, 220}) != qRgb(255, 255, 255));
    scrolling->setChecked(false);
    assert(mapView->grab().toImage() == plain);
    grid->setChecked(true);
    assert(mapView->grab().toImage() != plain);
    grid->setChecked(false);
    assert(mapView->grab().toImage() == plain);
    assert(snapshot(*gb) == initial && writes == 0);

    grid->setChecked(true);
    window.selectTab(1);
    QApplication::processEvents();
    assert(grid->isChecked() && grid->isVisible() && !scrolling->isVisible());
    const QImage tileGrid = tilesView->grab().toImage();
    grid->setChecked(false);
    const QImage tilePlain = tilesView->grab().toImage();
    assert(tileGrid != tilePlain);
    grid->setChecked(true);
    assert(tilesView->grab().toImage() == tileGrid);
    window.selectTab(3);
    assert(!grid->isVisible() && !scrolling->isVisible());
    grid->setChecked(false);
    window.selectTab(2);
    QApplication::processEvents();
    scrolling->setChecked(true);
    gb->io_registers[0x43] = 240;
    gb->io_registers[0x42] = 240;
    const auto scrolled = snapshot(*gb);
    const auto scrolledCopy = std::make_unique<CupidGb>(*gb);
    live->setChecked(true);
    QEventLoop refresh;
    QTimer::singleShot(350, &refresh, &QEventLoop::quit);
    refresh.exec();
    live->setChecked(false);
    const QImage wrapped = mapView->grab().toImage();
    for (const QPoint corner : {QPoint(8, 8), QPoint(248, 8), QPoint(8, 248), QPoint(248, 248)})
        assert(imageViewPixel(mapView, wrapped, {256, 256}, corner) == qRgb(255, 255, 255));
    assert(imageViewPixel(mapView, wrapped, {256, 256}, {200, 200}) != qRgb(255, 255, 255));
    assert(snapshot(*gb) == scrolled && writes == 0);
    assert(std::memcmp(gb.get(), scrolledCopy.get(), sizeof(*gb)) == 0);

    session = nullptr;
    window.sessionChanged();
    const QImage cleared = mapView->grab().toImage();
    assert(!grid->isEnabled() && !scrolling->isEnabled());
    grid->setChecked(true);
    scrolling->setChecked(false);
    assert(mapView->grab().toImage() == cleared);
    auto replacement = machine(false);
    replacement->io_registers[0x43] = 40;
    replacement->io_registers[0x42] = 60;
    const auto replacementState = snapshot(*replacement);
    session = replacement.get();
    window.sessionChanged();
    assert(grid->isEnabled() && scrolling->isEnabled());
    grid->setChecked(false);
    scrolling->setChecked(true);
    const QImage replaced = mapView->grab().toImage();
    assert(imageViewPixel(mapView, replaced, {256, 256}, {80, 80}) == qRgb(255, 255, 255));
    assert(imageViewPixel(mapView, replaced, {256, 256}, {8, 8}) != qRgb(255, 255, 255));
    assert(snapshot(*replacement) == replacementState && writes == 0);
    assert(snapshot(*gb) == scrolled);
    window.setSession({}, {});
    window.close();
    QApplication::processEvents();
    cupid_gb_cleanup(replacement.get());
    cupid_gb_cleanup(gb.get());
}

void testWindowWorkflows() {
    auto gb = machine(true);
    const uint8_t originalByte = gb->work_ram[0];
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
    assert(editor && editor->text() == "7c" && writes == 0 && gb->work_ram[0] == originalByte);
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

void testPhysicalMemoryWorkflows() {
    auto gb = machine(true);
    std::vector<uint8_t> rom(0x8000);
    rom[0x143] = 0x80;
    rom[0x147] = 0xbc;
    rom[0x149] = 0xc1;
    rom[0x14a] = 0x65;
    rom[0x152] = 9;
    rom[0x153] = 8;
    uint8_t checksum = 0;
    for (unsigned address = 0x134; address < 0x14d; ++address)
        checksum = uint8_t(checksum - rom[address] - 1);
    rom[0x14d] = checksum;
    assert(cupid_gb_load_rom(gb.get(), rom.data(), rom.size()));
    assert(gb->cartridge_ram_size == 0x200000);
    CupidGb *session = gb.get();
    unsigned writes = 0;
    InspectorsWindow window;
    window.setSession(
        [&] { return session; }, {},
        [&](InspectorMemoryRegion region, size_t bank, uint16_t offset, uint8_t value) {
            const auto bytes = inspectorMemoryBank(*session, region, bank);
            if (offset >= bytes.size || region == InspectorMemoryRegion::Rom)
                return false;
            const_cast<uint8_t *>(bytes.data)[offset] = value;
            ++writes;
            return true;
        });
    window.show();
    window.findChild<QCheckBox *>("inspectorLive")->setChecked(false);
    auto *region = window.findChild<QComboBox *>("inspectorMemoryRegion");
    auto *bank = window.findChild<QComboBox *>("inspectorMemoryBank");
    auto *address = window.findChild<QLineEdit *>("inspectorAddress");
    auto *go = window.findChild<QPushButton *>("inspectorGo");
    auto *memory = window.findChild<QTableWidget *>("inspectorMemory");
    auto *writeAddress = window.findChild<QLineEdit *>("inspectorWriteAddress");
    auto *writeValue = window.findChild<QLineEdit *>("inspectorWriteValue");
    auto *write = window.findChild<QPushButton *>("inspectorWrite");
    assert(region && bank && memory && !bank->isEnabled());
    gb->cgb_vram_bank = 0;
    gb->video_ram[0x2000] = 0x91;
    region->setCurrentIndex(int(InspectorMemoryRegion::VideoRam));
    bank->setCurrentIndex(1);
    assert(bank->count() == 2 && memory->item(0, 0)->text() == "91");
    memory->item(0, 0)->setText("25");
    assert(gb->video_ram[0x2000] == 0x25 && gb->cgb_vram_bank == 0 && writes == 1);
    region->setCurrentIndex(int(InspectorMemoryRegion::WorkRam));
    bank->setCurrentIndex(7);
    assert(bank->count() == 8 && memory->verticalHeaderItem(0)->text() == "D000");
    writeAddress->setText("DFFF");
    writeValue->setText("73");
    write->click();
    assert(gb->work_ram[0x7fff] == 0x73 && writes == 2);
    address->setText("C000");
    go->click();
    assert(memory->verticalHeaderItem(0)->text() == "D000");
    region->setCurrentIndex(int(InspectorMemoryRegion::CartridgeRam));
    bank->setCurrentIndex(255);
    assert(bank->count() == 256);
    writeAddress->setText("BFFF");
    writeValue->setText("A9");
    write->click();
    assert(cupid_gb_cartridge_ram(gb.get())[0x1fffff] == 0xa9 && writes == 3);
    assert(!inspectorMemoryBank(*gb, InspectorMemoryRegion::CartridgeRam, 256).data);
    region->setCurrentIndex(int(InspectorMemoryRegion::Rom));
    bank->setCurrentIndex(1);
    assert(memory->verticalHeaderItem(0)->text() == "4000");
    assert(!(memory->item(0, 0)->flags() & Qt::ItemIsEditable));
    writeAddress->setText("4000");
    write->click();
    assert(writes == 3);

    region->setCurrentIndex(int(InspectorMemoryRegion::VideoRam));
    bank->setCurrentIndex(1);
    memory->editItem(memory->item(0, 0));
    QApplication::processEvents();
    QPointer<QLineEdit> stale = memory->findChild<QLineEdit *>();
    assert(stale);
    stale->setText("FF");
    bank->setCurrentIndex(0);
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    assert(!stale && gb->video_ram[0x2000] == 0x25 && writes == 3);
    auto monochrome = machine(false);
    session = monochrome.get();
    window.sessionChanged();
    assert(bank->count() == 1 && bank->currentIndex() == 0);
    region->setCurrentIndex(int(InspectorMemoryRegion::WorkRam));
    assert(bank->count() == 2);
    region->setCurrentIndex(int(InspectorMemoryRegion::CartridgeRam));
    assert(bank->count() == 0 && !bank->isEnabled() && memory->item(0, 0)->text() == "--");
    writeAddress->setText("A000");
    write->click();
    assert(writes == 3);
    // A 2 KiB storage bank exposes only the populated address span.
    monochrome->cartridge_ram_size = 0x800;
    window.refresh();
    assert(bank->count() == 1);
    address->setText("A700");
    go->click();
    assert(memory->verticalHeaderItem(0)->text() == "A700");
    writeAddress->setText("A800");
    write->click();
    assert(writes == 3);
    session = nullptr;
    window.sessionChanged();
    write->click();
    assert(writes == 3);
    window.close();
    cupid_gb_cleanup(monochrome.get());
    cupid_gb_cleanup(gb.get());
}

void testWriteLifetime() {
    auto gb = machine(true);
    QPointer<InspectorsWindow> mapped = new InspectorsWindow;
    mapped->setSession([&] { return gb.get(); },
                       [&](uint16_t, uint8_t) {
                           delete mapped.data();
                           return true;
                       });
    assert(!mapped->writeByte(0xc000, 0x45) && !mapped);
    QPointer<InspectorsWindow> physical = new InspectorsWindow;
    physical->setSession([&] { return gb.get(); }, {},
                         [&](InspectorMemoryRegion, size_t, uint16_t, uint8_t) {
                             delete physical.data();
                             return true;
                         });
    physical->findChild<QComboBox *>("inspectorMemoryRegion")
        ->setCurrentIndex(int(InspectorMemoryRegion::VideoRam));
    physical->findChild<QLineEdit *>("inspectorWriteAddress")->setText("8000");
    physical->findChild<QPushButton *>("inspectorWrite")->click();
    assert(!physical);
    cupid_gb_cleanup(gb.get());
}

} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    testColorRevisionCapabilities();
    testTilePlanes();
    testMapsAndAttributes();
    testObjectsAndPalettes();
    testViewportAndGrids();
    testOverlayWorkflows();
    testWindowWorkflows();
    testPhysicalMemoryWorkflows();
    testWriteLifetime();
    return 0;
}

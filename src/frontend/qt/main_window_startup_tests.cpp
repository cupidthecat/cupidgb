#include "main_window_internal.h"

namespace {
QByteArray startupMemory(const CupidGb &gb) {
    QByteArray bytes;
    const auto append = [&](const uint8_t *data, size_t size) {
        bytes.append(reinterpret_cast<const char *>(data), qsizetype(size));
    };
    append(gb.work_ram, sizeof(gb.work_ram));
    append(gb.high_ram, sizeof(gb.high_ram));
    append(gb.object_attribute_memory, sizeof(gb.object_attribute_memory));
    append(gb.video_ram, sizeof(gb.video_ram));
    append(gb.io_registers + 0x30, 16);
    append(gb.cgb_bg_palette_ram, sizeof(gb.cgb_bg_palette_ram));
    append(gb.cgb_obj_palette_ram, sizeof(gb.cgb_obj_palette_ram));
    return bytes;
}

bool writeStartupFixture(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

bool MainWindow::startupSmokeTest(const QString &directory) {
    const QDir fixtures(QDir(directory).filePath("startup"));
    if (!QDir().mkpath(fixtures.path()))
        return smokeFailure(__FILE__, __LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom[0x147] = 0x09; // ROM + RAM + battery keeps the boot/load persistence path observable.
    rom[0x149] = 0x02;
    const auto cartridge = [&](const QString &name, uint8_t color) {
        rom[0x143] = char(color);
        uint8_t checksum = 0;
        for (int address = 0x134; address <= 0x14c; ++address)
            checksum = uint8_t(checksum - uint8_t(rom[address]) - 1);
        rom[0x14d] = char(checksum);
        const QString path = fixtures.filePath(name);
        return writeStartupFixture(path, rom) ? path : QString();
    };
    const QString monochrome = cartridge("monochrome.gb", 0);
    const QString color = cartridge("color.gbc", 0x80);
    const QString noBoot = QStringLiteral("");
    if (monochrome.isEmpty() || color.isEmpty())
        return smokeFailure(__FILE__, __LINE__);

    MainWindow fixed(true, false, false, noBoot, CUPID_GB_MODEL_DMG_ABC, uint64_t(0));
    fixed.timer_.stop();
    if (!fixed.openRom(monochrome) || !fixed.emulator_->gb.power_on.fixed_seed ||
        cupid_gb_get_power_on_seed(&fixed.emulator_->gb) != 0 ||
        fixed.emulator_->gb.cpu.pc != 0x100 || fixed.emulator_->gb.boot_rom_enabled)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray fixedMemory = startupMemory(fixed.emulator_->gb);
    // Zero selects a reproducible noise generation, rather than a zero-filled startup.
    if (std::all_of(std::begin(fixed.emulator_->gb.work_ram),
                    std::begin(fixed.emulator_->gb.work_ram) + 0x2000,
                    [](uint8_t value) { return value == 0; }))
        return smokeFailure(__FILE__, __LINE__);
    {
        MainWindow repeated(true, false, false, noBoot, CUPID_GB_MODEL_DMG_ABC, uint64_t(0));
        repeated.timer_.stop();
        if (!repeated.openRom(monochrome) || startupMemory(repeated.emulator_->gb) != fixedMemory)
            return smokeFailure(__FILE__, __LINE__);
    }
    fixed.emulator_->gb.work_ram[0x123] ^= 0xff;
    if (!fixed.restart() || startupMemory(fixed.emulator_->gb) != fixedMemory ||
        cupid_gb_get_power_on_seed(&fixed.emulator_->gb) != 0)
        return smokeFailure(__FILE__, __LINE__);
    fixed.setPaused(true);
    fixed.emulator_->gb.high_ram[0x21] ^= 0xff;
    if (!fixed.reloadRom() || !fixed.paused_ || startupMemory(fixed.emulator_->gb) != fixedMemory ||
        cupid_gb_get_power_on_seed(&fixed.emulator_->gb) != 0)
        return smokeFailure(__FILE__, __LINE__);
    if (!fixed.closeRom() || !fixed.openRom(monochrome) ||
        startupMemory(fixed.emulator_->gb) != fixedMemory)
        return smokeFailure(__FILE__, __LINE__);

    // Restored guest RAM belongs to the snapshot; the receiving session keeps its
    // cold-generation metadata and its policy for the next cartridge restart.
    fixed.emulator_->gb.work_ram[0x123] = 0x91;
    fixed.emulator_->gb.high_ram[0x21] = 0xe3;
    const QByteArray state = fixed.captureState();
    const QByteArray restoredMemory = startupMemory(fixed.emulator_->gb);
    constexpr uint64_t otherSeed = UINT64_C(0xfedcba9876543210);
    {
        MainWindow receiver(true, false, false, noBoot, CUPID_GB_MODEL_DMG_ABC, otherSeed);
        receiver.timer_.stop();
        if (!receiver.openRom(monochrome) ||
            cupid_gb_get_power_on_seed(&receiver.emulator_->gb) != otherSeed)
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray receiverMemory = startupMemory(receiver.emulator_->gb);
        if (state.isEmpty() || receiverMemory == fixedMemory ||
            !receiver.restoreState(state, false) ||
            startupMemory(receiver.emulator_->gb) != restoredMemory ||
            cupid_gb_get_power_on_seed(&receiver.emulator_->gb) != otherSeed ||
            !receiver.emulator_->gb.power_on.fixed_seed ||
            receiver.emulator_->gb.power_on.seed != otherSeed || !receiver.restart() ||
            startupMemory(receiver.emulator_->gb) != receiverMemory)
            return smokeFailure(__FILE__, __LINE__);
    }

    MainWindow automatic(true, false, false, noBoot, CUPID_GB_MODEL_DMG_ABC);
    automatic.timer_.stop();
    if (!automatic.openRom(monochrome) || automatic.powerOnSeedOverride_ ||
        automatic.emulator_->gb.power_on.fixed_seed)
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t automaticSeed = cupid_gb_get_power_on_seed(&automatic.emulator_->gb);
    const QByteArray automaticMemory = startupMemory(automatic.emulator_->gb);
    {
        // Replaying the realized automatic choice proves reproducibility without
        // assuming two entropy samples must differ or have a particular value.
        MainWindow replay(true, false, false, noBoot, CUPID_GB_MODEL_DMG_ABC, automaticSeed);
        replay.timer_.stop();
        if (!replay.openRom(monochrome) || startupMemory(replay.emulator_->gb) != automaticMemory)
            return smokeFailure(__FILE__, __LINE__);
    }
    const QByteArray beforeFailure = automatic.captureState();
    const CupidEmulator *beforeCore = automatic.emulator_.get();
    automatic.bootDirectoryOverride_ = fixtures.filePath("missing-firmware");
    if (automatic.openRom(monochrome) || automatic.emulator_.get() != beforeCore ||
        automatic.captureState() != beforeFailure ||
        cupid_gb_get_power_on_seed(&automatic.emulator_->gb) != automaticSeed ||
        automatic.emulator_->gb.power_on.fixed_seed)
        return smokeFailure(__FILE__, __LINE__);
    automatic.failed_ = false;
    automatic.bootDirectoryOverride_ = noBoot;
    if (!automatic.restart() || automatic.emulator_->gb.power_on.fixed_seed)
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t restartedSeed = cupid_gb_get_power_on_seed(&automatic.emulator_->gb);
    {
        MainWindow replay(true, false, false, noBoot, CUPID_GB_MODEL_DMG_ABC, restartedSeed);
        replay.timer_.stop();
        if (!replay.openRom(monochrome) ||
            startupMemory(replay.emulator_->gb) != startupMemory(automatic.emulator_->gb))
            return smokeFailure(__FILE__, __LINE__);
    }

    const QByteArray battery(8192, char(0x6d));
    if (!writeStartupFixture(fixtures.filePath("color.sav"), battery))
        return smokeFailure(__FILE__, __LINE__);
    MainWindow handoff(true, false, false, noBoot, CUPID_GB_MODEL_CGB, uint64_t(0));
    handoff.timer_.stop();
    if (!handoff.openRom(color) || !handoff.emulator_->gb.cgb_mode ||
        handoff.emulator_->gb.boot_rom_enabled || handoff.emulator_->gb.cpu.pc != 0x100 ||
        !std::all_of(std::begin(handoff.emulator_->gb.work_ram) + 0x2000,
                     std::begin(handoff.emulator_->gb.work_ram) + 0x3000,
                     [](uint8_t value) { return value == 0; }))
        return smokeFailure(__FILE__, __LINE__);
    const QDir firmware(fixtures.filePath("firmware"));
    QByteArray boot(0x900, char(0));
    boot[0] = char(0x18);
    boot[1] = char(0xfe);
    if (!QDir().mkpath(firmware.path()) ||
        !writeStartupFixture(firmware.filePath("cgb_boot.bin"), boot))
        return smokeFailure(__FILE__, __LINE__);
    MainWindow cold(true, false, false, firmware.path(), CUPID_GB_MODEL_CGB, uint64_t(0));
    cold.timer_.stop();
    if (!cold.openRom(color) || !cold.emulator_->gb.boot_rom_enabled ||
        cold.emulator_->gb.cpu.pc != 0 || cupid_gb_get_power_on_seed(&cold.emulator_->gb) != 0 ||
        std::all_of(std::begin(cold.emulator_->gb.work_ram) + 0x2000,
                    std::begin(cold.emulator_->gb.work_ram) + 0x3000,
                    [](uint8_t value) { return value == 0; }) ||
        std::memcmp(cold.emulator_->gb.work_ram, handoff.emulator_->gb.work_ram, 0x2000) != 0 ||
        std::memcmp(cold.emulator_->gb.work_ram + 0x3000, handoff.emulator_->gb.work_ram + 0x3000,
                    0x5000) != 0 ||
        cold.emulator_->gb.cartridge_ram_size != size_t(battery.size()) ||
        std::memcmp(cold.emulator_->gb.cartridge_ram, battery.constData(),
                    size_t(battery.size())) != 0)
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray coldMemory = startupMemory(cold.emulator_->gb);
    cold.emulator_->gb.work_ram[0x2345] ^= 0xff;
    cold.setPaused(true);
    if (!cold.reloadRom() || !cold.paused_ || cold.emulator_->gb.cpu.pc != 0 ||
        startupMemory(cold.emulator_->gb) != coldMemory ||
        cupid_gb_get_power_on_seed(&cold.emulator_->gb) != 0 ||
        std::memcmp(cold.emulator_->gb.cartridge_ram, battery.constData(),
                    size_t(battery.size())) != 0)
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB power-on seed, cold firmware and state lifecycle workflows passed");
    return true;
}

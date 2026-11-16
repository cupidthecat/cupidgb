#include "main_window_internal.h"

#include <QDateTime>
#include <QScopeGuard>

namespace {
struct RtcTestClock {
    int64_t now = INT64_C(1700000040);
    unsigned calls = 0;
};

int64_t rtcTestTime(void *context) {
    auto &clock = *static_cast<RtcTestClock *>(context);
    ++clock.calls;
    return clock.now;
}

std::array<uint8_t, 11> rtcRegisters(const CupidGb &gb) {
    return {gb.rtc.s,  gb.rtc.m,  gb.rtc.h,   gb.rtc.dl,  gb.rtc.dh,        gb.rtc.ls,
            gb.rtc.lm, gb.rtc.lh, gb.rtc.ldl, gb.rtc.ldh, gb.rtc.latch_prev};
}

bool writeRtcFixture(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readRtcFixture(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray rtcBatteryBytes(const CupidGb &gb, int64_t timestamp) {
    QByteArray bytes(qsizetype(cupid_gb_battery_size(&gb)), Qt::Uninitialized);
    size_t written = 0;
    if (cupid_gb_save_battery_at(&gb, reinterpret_cast<uint8_t *>(bytes.data()),
                                 size_t(bytes.size()), &written,
                                 timestamp) != CUPID_GB_BATTERY_OK ||
        written != size_t(bytes.size()))
        return {};
    return bytes;
}

void rtcFixtureChecksum(QByteArray &rom) {
    uint8_t checksum = 0;
    for (int offset = 0x134; offset <= 0x14c; ++offset)
        checksum = uint8_t(checksum - uint8_t(rom[offset]) - 1);
    rom[0x14d] = char(checksum);
}
} // namespace

bool MainWindow::rtcSmokeTest(const QString &directory) {
    std::vector<std::pair<QString, QVariant>> savedSettings;
    for (const auto &key : settings_.allKeys())
        savedSettings.emplace_back(key, settings_.value(key));
    const auto restoreSettings = qScopeGuard([&] {
        settings_.clear();
        for (const auto &[key, value] : savedSettings)
            settings_.setValue(key, value);
    });
    settings_.clear();
    settings_.setValue("emulation/rewindSeconds", 120);
    settings_.setValue("emulation/rewindMiB", 16);
    const QDir fixtures(QDir(directory).filePath("rtc"));
    if (!QDir().mkpath(fixtures.path()))
        return smokeFailure(__FILE__, __LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom[0x147] = 0x10;
    rom[0x149] = 2;
    rtcFixtureChecksum(rom);
    const QString cartridge = fixtures.filePath("clock.gb");
    if (!writeRtcFixture(cartridge, rom))
        return smokeFailure(__FILE__, __LINE__);

    const auto configureMode = [&](MainWindow &window, int expected, int selected, bool accept,
                                   bool invalidInput = false, bool capture = false) {
        bool inspected = false;
        bool submitted = false;
        bool warningSeen = false;
        bool timedOut = false;
        bool captured = !capture;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer command;
        command.setInterval(1);
        connect(&command, &QTimer::timeout, &window, [&] {
            auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (elapsed.elapsed() > 5000) {
                timedOut = true;
                if (active)
                    active->reject();
                if (auto *settings = window.findChild<QDialog *>("settings"))
                    settings->reject();
                return;
            }
            if (auto *warning = qobject_cast<QMessageBox *>(active)) {
                warningSeen = true;
                warning->accept();
                return;
            }
            if (!active || active->objectName() != "settings")
                return;
            if (submitted) {
                if (invalidInput && warningSeen)
                    active->reject();
                return;
            }
            submitted = true;
            auto *mode = active->findChild<QComboBox *>("rtcMode");
            auto *buttons = active->findChild<QDialogButtonBox *>();
            auto *form =
                mode ? qobject_cast<QFormLayout *>(mode->parentWidget()->layout()) : nullptr;
            auto *label = form ? qobject_cast<QLabel *>(form->labelForField(mode)) : nullptr;
            inspected = mode && buttons && label && label->text() == "Real Time Clock:" &&
                        mode->count() == 2 && mode->itemText(0) == "Sync to System Clock" &&
                        mode->itemText(1) == "Accurate" && mode->itemData(0).toInt() == 0 &&
                        mode->itemData(1).toInt() == 1 && mode->currentData().toInt() == expected;
            if (!inspected) {
                active->reject();
                return;
            }
            mode->setCurrentIndex(mode->findData(selected));
            if (invalidInput) {
                auto *color = active->findChild<QLineEdit *>("customPalette/0");
                if (!color) {
                    inspected = false;
                    active->reject();
                    return;
                }
                color->setText("bad");
            }
            if (capture) {
                const QString output = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
                captured = output.isEmpty() ||
                           (QDir().mkpath(output) &&
                            active->grab().save(QDir(output).filePath("rtc-settings.png")));
            }
            auto *button =
                buttons->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel);
            if (!button || !captured) {
                inspected = false;
                active->reject();
                return;
            }
            // Validation can open a nested modal dialog. Leave this timer's
            // callback first so it can dismiss the warning and enforce its deadline.
            QTimer::singleShot(0, button, [button] { button->click(); });
        });
        command.start();
        window.configure();
        command.stop();
        return inspected && !timedOut && captured && warningSeen == invalidInput;
    };

    if (configuredRtcMode(settings_) != CUPID_GB_RTC_SYNC_TO_HOST ||
        settings_.contains("emulation/rtcMode"))
        return smokeFailure(__FILE__, __LINE__);
    for (const QVariant &invalid :
         {QVariant(-1), QVariant(2), QVariant(1.5), QVariant(1.0), QVariant(true),
          QVariant(QStringLiteral("bad")), QVariant(QStringLiteral("1.0")),
          QVariant(QStringLiteral("+1")), QVariant(QStringLiteral(" 1")),
          QVariant(QStringLiteral("01")), QVariant(QStringLiteral("18446744073709551616"))}) {
        settings_.setValue("emulation/rtcMode", invalid);
        if (configuredRtcMode(settings_) != CUPID_GB_RTC_SYNC_TO_HOST ||
            settings_.value("emulation/rtcMode") != invalid)
            return smokeFailure(__FILE__, __LINE__);
    }
    for (const QVariant &valid : {QVariant(1), QVariant(1u), QVariant(qlonglong(1)),
                                  QVariant(qulonglong(1)), QVariant(QStringLiteral("1"))}) {
        settings_.setValue("emulation/rtcMode", valid);
        if (configuredRtcMode(settings_) != CUPID_GB_RTC_ACCURATE)
            return smokeFailure(__FILE__, __LINE__);
    }
    settings_.setValue("emulation/rtcMode", 2);
    RtcTestClock clock;
    MainWindow preferences(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
    preferences.timer_.stop();
    if (!configureMode(preferences, 0, 1, false) || preferences.emulator_ ||
        settings_.value("emulation/rtcMode").toInt() != 2)
        return smokeFailure(__FILE__, __LINE__);
    settings_.remove("emulation/rtcMode");
    if (!configureMode(preferences, 0, 0, false) || settings_.contains("emulation/rtcMode") ||
        !configureMode(preferences, 0, 1, true) ||
        settings_.value("emulation/rtcMode").toInt() != 1)
        return smokeFailure(__FILE__, __LINE__);
    {
        MainWindow reopened(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        reopened.timer_.stop();
        if (!configureMode(reopened, 1, 1, false) || !reopened.openRom(cartridge) ||
            cupid_gb_get_rtc_mode(&reopened.emulator_->gb) != CUPID_GB_RTC_ACCURATE)
            return smokeFailure(__FILE__, __LINE__);
    }

    MainWindow session(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
    session.timer_.stop();
    if (!session.openRom(cartridge) ||
        cupid_gb_get_rtc_mode(&session.emulator_->gb) != CUPID_GB_RTC_ACCURATE)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    {
        auto &gb = session.emulator_->gb;
        cupid_gb_set_rtc_time_source(&gb, rtcTestTime, &clock);
        if (!cupid_gb_set_rtc_multiplier(&gb, 2.0))
            return smokeFailure(__FILE__, __LINE__);
        gb.rtc = {12, 34, 5, 6, 0, 7, 8, 9, 10, 0, 1, clock.now - 63};
        gb.rtc_cycles = 1234;
        const auto registers = rtcRegisters(gb);
        const uint32_t period = gb.rtc_policy.second_length;
        const auto policyMatches = [&](CupidGbRtcMode mode) {
            return cupid_gb_get_rtc_mode(&gb) == mode && gb.rtc_policy.second_length == period &&
                   gb.rtc_policy.time_source == rtcTestTime &&
                   gb.rtc_policy.time_user_data == &clock && cupid_gb_get_power_on_seed(&gb) == 0;
        };
        session.resetRewind();
        session.rememberFrame();
        session.rememberFrame();
        if (session.rewindStates_.size() != 2)
            return smokeFailure(__FILE__, __LINE__);
        const auto history = session.rewindStates_;
        const auto historyBytes = session.rewindBytes_;
        const auto historyUnchanged = [&] {
            return session.rewindBytes_ == historyBytes &&
                   session.rewindStates_.size() == history.size() &&
                   std::equal(history.begin(), history.end(), session.rewindStates_.begin(),
                              [](const auto &left, const auto &right) {
                                  return left.cycles == right.cycles &&
                                         left.compressed == right.compressed;
                              });
        };
        const QByteArray before = session.captureState();
        const unsigned initialCalls = clock.calls;
        if (before.isEmpty() || !configureMode(session, 1, 0, false) ||
            session.captureState() != before || !policyMatches(CUPID_GB_RTC_ACCURATE) ||
            !historyUnchanged() || clock.calls != initialCalls ||
            settings_.value("emulation/rtcMode").toInt() != 1 ||
            !configureMode(session, 1, 0, true, true) || session.captureState() != before ||
            !policyMatches(CUPID_GB_RTC_ACCURATE) || !historyUnchanged() ||
            clock.calls != initialCalls || settings_.value("emulation/rtcMode").toInt() != 1)
            return smokeFailure(__FILE__, __LINE__);
        if (!configureMode(session, 1, 0, true) || !policyMatches(CUPID_GB_RTC_SYNC_TO_HOST) ||
            gb.rtc_cycles != 0 || gb.rtc.base_time != clock.now || rtcRegisters(gb) != registers ||
            clock.calls != initialCalls + 1 || !historyUnchanged() ||
            settings_.value("emulation/rtcMode").toInt() != 0)
            return smokeFailure(__FILE__, __LINE__);
        gb.rtc_cycles = 4321;
        gb.rtc.base_time = clock.now - 3;
        const QByteArray unchangedMode = session.captureState();
        const unsigned sameModeCalls = clock.calls;
        if (!configureMode(session, 0, 0, true) || session.captureState() != unchangedMode ||
            !policyMatches(CUPID_GB_RTC_SYNC_TO_HOST) || !historyUnchanged() ||
            clock.calls != sameModeCalls || !configureMode(session, 0, 1, true, false, true) ||
            !policyMatches(CUPID_GB_RTC_ACCURATE) || gb.rtc_cycles != 0 ||
            gb.rtc.base_time != clock.now || rtcRegisters(gb) != registers ||
            clock.calls != sameModeCalls + 1 || !historyUnchanged() || !session.paused_ ||
            !configureMode(session, 1, 1, false))
            return smokeFailure(__FILE__, __LINE__);

        const auto pausedCycles = gb.elapsed_cycles;
        const auto pausedPhase = gb.rtc_cycles;
        const auto pausedBaseline = gb.rtc.base_time;
        const auto pausedCalls = clock.calls;
        clock.now += 60;
        session.tick();
        session.setTurboCap(7);
        session.turbo_ = true;
        session.slowHeld_ = true;
        if (gb.elapsed_cycles != pausedCycles || gb.rtc_cycles != pausedPhase ||
            gb.rtc.base_time != pausedBaseline || clock.calls != pausedCalls ||
            !policyMatches(CUPID_GB_RTC_ACCURATE))
            return smokeFailure(__FILE__, __LINE__);
        session.turbo_ = false;
        session.slowHeld_ = false;

        gb.rtc.s = 21;
        gb.rtc_cycles = 54321;
        gb.rtc.base_time = clock.now - 9;
        const QByteArray saved = session.captureState();
        const auto savedRegisters = rtcRegisters(gb);
        if (saved.isEmpty() || !cupid_gb_set_rtc_mode(&gb, CUPID_GB_RTC_SYNC_TO_HOST))
            return smokeFailure(__FILE__, __LINE__);
        gb.rtc.s = 47;
        gb.rtc_cycles = 9988;
        const unsigned loadCalls = clock.calls;
        if (!session.restoreState(saved, false) || session.captureState() != saved ||
            !policyMatches(CUPID_GB_RTC_SYNC_TO_HOST) || rtcRegisters(gb) != savedRegisters ||
            gb.rtc_cycles != 54321 || gb.rtc.base_time != clock.now - 9 || clock.calls != loadCalls)
            return smokeFailure(__FILE__, __LINE__);
        QByteArray damaged = saved;
        damaged.back() = char(uint8_t(damaged.back()) ^ 1u);
        if (session.restoreState(damaged, false) || session.captureState() != saved ||
            !policyMatches(CUPID_GB_RTC_SYNC_TO_HOST) || clock.calls != loadCalls)
            return smokeFailure(__FILE__, __LINE__);

        const auto *beforeCore = session.emulator_.get();
        session.bootDirectoryOverride_ = fixtures.filePath("missing-firmware");
        if (session.openRom(cartridge) || session.emulator_.get() != beforeCore ||
            session.captureState() != saved || !policyMatches(CUPID_GB_RTC_SYNC_TO_HOST) ||
            clock.calls != loadCalls)
            return smokeFailure(__FILE__, __LINE__);
        session.failed_ = false;
        session.bootDirectoryOverride_ = QStringLiteral("");

        if (!cupid_gb_set_rtc_mode(&gb, CUPID_GB_RTC_ACCURATE))
            return smokeFailure(__FILE__, __LINE__);
        gb.rtc_cycles = 4096;
        gb.rtc.base_time = clock.now - 17;
        const auto replayRegisters = rtcRegisters(gb);
        session.resetRewind();
        session.rememberFrame();
        PlaybackResult forward;
        if (!session.runFrame(Playback::Forward, &forward) || !forward.cycles)
            return smokeFailure(__FILE__, __LINE__);
        if (session.rewindStates_.size() == 1)
            session.rememberFrame();
        if (session.rewindStates_.size() != 2)
            return smokeFailure(__FILE__, __LINE__);
        const unsigned replayCalls = clock.calls;
        PlaybackResult replay;
        if (!session.rewindFrame(&replay) || !replay.cycles ||
            gb.rtc_cycles != 4096 + replay.cycles * 2 || gb.rtc.base_time != clock.now - 17 ||
            rtcRegisters(gb) != replayRegisters || !policyMatches(CUPID_GB_RTC_ACCURATE) ||
            clock.calls != replayCalls)
            return smokeFailure(__FILE__, __LINE__);
        // Rewind replays guest cycles. Backstep restores the saved instruction boundary exactly.
        session.resetDebuggerTimeline();
        gb.rtc_cycles = 2468;
        gb.rtc.base_time = clock.now - 23;
        const auto backstepCycles = gb.elapsed_cycles;
        if (!session.rememberDebuggerStep() || !cupid_emulator_step(session.emulator_.get()) ||
            !cupid_gb_set_rtc_mode(&gb, CUPID_GB_RTC_SYNC_TO_HOST))
            return smokeFailure(__FILE__, __LINE__);
        const unsigned backstepCalls = clock.calls;
        if (!session.debuggerBackstep() || gb.elapsed_cycles != backstepCycles ||
            gb.rtc_cycles != 2468 || gb.rtc.base_time != clock.now - 23 ||
            rtcRegisters(gb) != replayRegisters || !policyMatches(CUPID_GB_RTC_SYNC_TO_HOST) ||
            clock.calls != backstepCalls)
            return smokeFailure(__FILE__, __LINE__);
    }
    // These frontend actions create a new cold session using the saved preference.
    if (!session.restart() ||
        cupid_gb_get_rtc_mode(&session.emulator_->gb) != CUPID_GB_RTC_ACCURATE)
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("emulation/rtcMode", 0);
    session.setPaused(true);
    if (!session.reloadRom() || !session.paused_ ||
        cupid_gb_get_rtc_mode(&session.emulator_->gb) != CUPID_GB_RTC_SYNC_TO_HOST)
        return smokeFailure(__FILE__, __LINE__);
    settings_.setValue("emulation/rtcMode", 1);
    if (!session.closeRom() || !session.openRom(cartridge) ||
        cupid_gb_get_rtc_mode(&session.emulator_->gb) != CUPID_GB_RTC_ACCURATE)
        return smokeFailure(__FILE__, __LINE__);

    QByteArray tppRom = rom;
    tppRom[0x147] = char(0xbc);
    tppRom[0x149] = char(0xc1);
    tppRom[0x14a] = char(0x65);
    tppRom[0x152] = 1;
    tppRom[0x153] = 8;
    rtcFixtureChecksum(tppRom);
    const QString tppPath = fixtures.filePath("ordered.gb");
    if (!writeRtcFixture(tppPath, tppRom))
        return smokeFailure(__FILE__, __LINE__);
    constexpr int64_t savedBaseline = INT64_C(1699999979);
    constexpr std::array<uint8_t, 4> savedClock = {3, 4, 5, 6};
    {
        auto source = std::make_unique<CupidGb>();
        const auto cleanup = qScopeGuard([&] { cupid_gb_cleanup(source.get()); });
        cupid_gb_set_power_on_seed(source.get(), 0);
        if (!cupid_gb_load_rom(source.get(), reinterpret_cast<const uint8_t *>(tppRom.constData()),
                               size_t(tppRom.size())) ||
            source->header.mbc_type != CUPID_GB_TPP1 || source->cartridge_ram_size != 8192)
            return smokeFailure(__FILE__, __LINE__);
        source->tpp1.status = 4;
        source->tpp1.rtc_base_time = savedBaseline;
        std::copy(savedClock.begin(), savedClock.end(), source->tpp1.live);
        std::memset(source->cartridge_ram, 0x6d, source->cartridge_ram_size);
        QByteArray battery(qsizetype(cupid_gb_battery_size(source.get())), Qt::Uninitialized);
        size_t written = 0;
        if (cupid_gb_save_battery_at(source.get(), reinterpret_cast<uint8_t *>(battery.data()),
                                     size_t(battery.size()), &written,
                                     savedBaseline) != CUPID_GB_BATTERY_OK ||
            written != size_t(battery.size()) ||
            !writeRtcFixture(fixtures.filePath("ordered.sav"), battery))
            return smokeFailure(__FILE__, __LINE__);
    }
    const QDir firmware(fixtures.filePath("firmware"));
    QByteArray boot(0x100, char(0));
    boot[0] = char(0x18);
    boot[1] = char(0xfe);
    if (!QDir().mkpath(firmware.path()) ||
        !writeRtcFixture(firmware.filePath("dmg_boot.bin"), boot))
        return smokeFailure(__FILE__, __LINE__);
    for (const bool cold : {false, true}) {
        // The export timestamp is part of this fixture. Keep its provider alive
        // through reload and destruction, which both commit battery history.
        RtcTestClock orderedClock{savedBaseline};
        MainWindow ordered(true, false, false, cold ? firmware.path() : QStringLiteral(""),
                           CUPID_GB_MODEL_DMG_ABC, 0);
        ordered.timer_.stop();
        if (!ordered.openRom(tppPath))
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_set_rtc_time_source(&ordered.emulator_->gb, rtcTestTime, &orderedClock);
        const auto imported = [&] {
            const auto &gb = ordered.emulator_->gb;
            return cupid_gb_get_rtc_mode(&gb) == CUPID_GB_RTC_ACCURATE &&
                   gb.tpp1.rtc_base_time == savedBaseline && gb.rtc_cycles == 0 &&
                   gb.tpp1.status == 4 &&
                   std::equal(savedClock.begin(), savedClock.end(), gb.tpp1.live) &&
                   gb.cartridge_ram_size == 8192 &&
                   std::all_of(gb.cartridge_ram, gb.cartridge_ram + gb.cartridge_ram_size,
                               [](uint8_t byte) { return byte == 0x6d; }) &&
                   gb.boot_rom_enabled == cold && gb.cpu.pc == (cold ? 0 : 0x100);
        };
        // A mode setter after import would erase this saved timestamp even with no guest ticks.
        if (!imported())
            return smokeFailure(__FILE__, __LINE__);
        ordered.setPaused(true);
        if (!ordered.reloadRom() || !ordered.paused_)
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_set_rtc_time_source(&ordered.emulator_->gb, rtcTestTime, &orderedClock);
        if (!imported())
            return smokeFailure(__FILE__, __LINE__);
    }
    if (!rtcBatterySmokeTest(fixtures.path()))
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB RTC settings, battery ordering and history workflows passed");
    return true;
}

bool MainWindow::rtcBatterySmokeTest(const QString &directory) {
    settings_.setValue("emulation/rtcMode", 1);
    const QDir fixtures(QDir(directory).filePath("battery-lifecycle"));
    if (!QDir().mkpath(fixtures.path()))
        return smokeFailure(__FILE__, __LINE__);
    const auto cartridge = [&](const QString &name, uint8_t type) {
        QByteArray bytes(32768, char(0));
        bytes[0x100] = char(0x18);
        bytes[0x101] = char(0xfe);
        bytes[0x147] = char(type);
        bytes[0x149] = type ? 2 : 0;
        if (type == 0xbc) {
            bytes[0x149] = char(0xc1);
            bytes[0x14a] = char(0x65);
            bytes[0x152] = 1;
            bytes[0x153] = 8;
        }
        rtcFixtureChecksum(bytes);
        const QString path = fixtures.filePath(name + ".gb");
        return writeRtcFixture(path, bytes) ? path : QString();
    };
    const QString replacement = cartridge("replacement", 0);
    if (replacement.isEmpty())
        return smokeFailure(__FILE__, __LINE__);
    const std::array<QString, 5> endings = {"close", "reset", "reload", "replace", "destroy"};
    for (const uint8_t type : {uint8_t(0x10), uint8_t(0xbc), uint8_t(0xfe)}) {
        for (size_t ending = 0; ending < endings.size(); ++ending) {
            const QString path =
                cartridge(QString("clock-%1-%2").arg(type).arg(endings[ending]), type);
            RtcTestClock clock;
            auto window = std::make_unique<MainWindow>(true, false, false, QStringLiteral(""),
                                                       CUPID_GB_MODEL_DMG_ABC, 0);
            window->timer_.stop();
            if (path.isEmpty() || !window->openRom(path))
                return smokeFailure(__FILE__, __LINE__);
            window->setPaused(true);
            auto &gb = window->emulator_->gb;
            cupid_gb_set_rtc_time_source(&gb, rtcTestTime, &clock);
            gb.rtc = {};
            gb.rtc.s = 10;
            gb.rtc.base_time = clock.now - 1;
            gb.tpp1.status = 4;
            gb.tpp1.live[3] = 10;
            gb.tpp1.rtc_base_time = clock.now - 1;
            gb.huc3.minutes = 10;
            gb.huc3.days = 3;
            // An Accurate minute update retains the old second remainder.
            gb.huc3.rtc_base_time = clock.now - 61;
            gb.rtc_cycles = cupid_gb_base_clock_hz(&gb) * 2u - 24u;
            const QByteArray initial = rtcBatteryBytes(gb, clock.now);
            const QString save = window->savePath_;
            if (initial.isEmpty() || !writeRtcFixture(save, initial))
                return smokeFailure(__FILE__, __LINE__);
            window->savedRam_ = initial;
            QFile battery(save);
            if (!battery.open(QIODevice::ReadWrite) ||
                !battery.setFileTime(QDateTime::fromSecsSinceEpoch(INT64_C(946684800)),
                                     QFileDevice::FileModificationTime))
                return smokeFailure(__FILE__, __LINE__);
            battery.close();
            const auto modified = QFileInfo(save).lastModified();
            const auto generation = gb.battery_write_generation;
            const auto cycles = gb.elapsed_cycles;
            if (!cupid_emulator_step(window->emulator_.get()) || gb.elapsed_cycles != cycles + 12 ||
                gb.rtc_cycles != 0 || gb.battery_write_generation != generation || clock.calls ||
                (type == 0x10 && gb.rtc.s != 11) || (type == 0xbc && gb.tpp1.live[3] != 11) ||
                (type == 0xfe && gb.huc3.minutes != 11))
                return smokeFailure(__FILE__, __LINE__);
            window->watchBattery();
            for (unsigned poll = 0; poll < 3; ++poll)
                if (!window->flushBattery())
                    return smokeFailure(__FILE__, __LINE__);
            window->setPaused(true);
            if (window->batteryDirty_ || window->saveTimer_.isActive() || clock.calls ||
                readRtcFixture(save) != initial || QFileInfo(save).lastModified() != modified)
                return smokeFailure(__FILE__, __LINE__);
            const QByteArray expected = rtcBatteryBytes(gb, clock.now);
            bool ended = true;
            switch (ending) {
                case 0:
                    ended = window->closeRom();
                    break;
                case 1:
                    ended = window->restart();
                    break;
                case 2:
                    ended = window->reloadRom();
                    break;
                case 3:
                    ended = window->openRom(replacement);
                    break;
                default:
                    window.reset();
                    break;
            }
            if (!ended || expected == initial || readRtcFixture(save) != expected ||
                clock.calls != (type == 0xfe ? 0u : 1u)) {
                qCritical("RTC lifecycle persistence failed for type 0x%02x during %s", type,
                          qPrintable(endings[ending]));
                return smokeFailure(__FILE__, __LINE__);
            }
        }
    }

    // Equal cached bytes do not prove a save exists. A new session may close
    // before either SRAM or the clock changes, and must still create its file.
    {
        RtcTestClock clock;
        MainWindow window(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        window.timer_.stop();
        const QString path = cartridge("unchanged-new-clock", 0x10);
        if (path.isEmpty() || !window.openRom(path))
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray cached = window.savedRam_;
        const QString save = window.savePath_;
        const size_t offset = window.emulator_->gb.cartridge_ram_size + 40;
        if (size_t(cached.size()) != offset + 8 || QFileInfo::exists(save))
            return smokeFailure(__FILE__, __LINE__);
        uint64_t timestamp = 0;
        for (unsigned i = 0; i < 8; ++i)
            timestamp |= uint64_t(uint8_t(cached[qsizetype(offset + i)])) << (8u * i);
        if (timestamp > uint64_t(INT64_MAX))
            return smokeFailure(__FILE__, __LINE__);
        clock.now = int64_t(timestamp);
        cupid_gb_set_rtc_time_source(&window.emulator_->gb, rtcTestTime, &clock);
        if (rtcBatteryBytes(window.emulator_->gb, clock.now) != cached || !window.flushBattery() ||
            QFileInfo::exists(save) || clock.calls || !window.closeRom() ||
            readRtcFixture(save) != cached || clock.calls != 1)
            return smokeFailure(__FILE__, __LINE__);
    }

    // Status-only mapper commands intentionally do not schedule debounced saves.
    {
        RtcTestClock clock;
        MainWindow window(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        window.timer_.stop();
        const QString path = cartridge("status-clock", 0xbc);
        if (path.isEmpty() || !window.openRom(path))
            return smokeFailure(__FILE__, __LINE__);
        auto &gb = window.emulator_->gb;
        cupid_gb_set_rtc_time_source(&gb, rtcTestTime, &clock);
        gb.tpp1.status = 12;
        gb.tpp1.rtc_base_time = clock.now;
        const auto generation = gb.battery_write_generation;
        const QByteArray initial = rtcBatteryBytes(gb, clock.now);
        const QString save = window.savePath_;
        if (initial.isEmpty() || !writeRtcFixture(save, initial))
            return smokeFailure(__FILE__, __LINE__);
        window.savedRam_ = initial;
        cupid_gb_write_u8(&gb, 0x0003, 0x14);
        cupid_gb_write_u8(&gb, 0x0003, 0x18);
        window.watchBattery();
        if (gb.tpp1.status || gb.battery_write_generation != generation || window.batteryDirty_ ||
            window.saveTimer_.isActive() || !window.flushBattery() ||
            readRtcFixture(save) != initial || clock.calls)
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray expected = rtcBatteryBytes(gb, clock.now);
        if (!window.closeRom() || expected == initial || readRtcFixture(save) != expected ||
            clock.calls != 1)
            return smokeFailure(__FILE__, __LINE__);
    }

    // A reset must replace stale disk bytes with the valid live battery before
    // importing the replacement session. Different candidates are validated first.
    {
        RtcTestClock clock;
        MainWindow window(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        window.timer_.stop();
        const QString path = cartridge("live-clock", 0xbc);
        const QString invalid = cartridge("invalid-clock", 0xbc);
        if (path.isEmpty() || invalid.isEmpty() || !window.openRom(path) ||
            !writeRtcFixture(fixtures.filePath("invalid-clock.sav"), "invalid battery"))
            return smokeFailure(__FILE__, __LINE__);
        auto &gb = window.emulator_->gb;
        cupid_gb_set_rtc_time_source(&gb, rtcTestTime, &clock);
        gb.tpp1.status = 4;
        gb.tpp1.live[3] = 23;
        gb.tpp1.rtc_base_time = clock.now;
        const QByteArray state = window.captureState();
        const auto *core = window.emulator_.get();
        if (state.isEmpty() || window.openRom(invalid) || window.emulator_.get() != core ||
            window.captureState() != state || clock.calls)
            return smokeFailure(__FILE__, __LINE__);
        window.failed_ = false;
        const QString save = window.savePath_;
        const QByteArray expected = rtcBatteryBytes(gb, clock.now);
        if (!writeRtcFixture(save, "stale invalid battery") || !window.restart() ||
            readRtcFixture(save) != expected || window.emulator_->gb.tpp1.live[3] != 23 ||
            window.emulator_->gb.tpp1.rtc_base_time != clock.now || clock.calls != 1)
            return smokeFailure(__FILE__, __LINE__);
        cupid_gb_set_rtc_time_source(&window.emulator_->gb, rtcTestTime, &clock);
    }

    // A failed lifecycle commit must retain the live cartridge and its retryable
    // data even when no generation change would have requested an autosave.
    {
        RtcTestClock clock;
        MainWindow window(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        window.timer_.stop();
        const QString path = cartridge("blocked-clock", 0x10);
        if (path.isEmpty() || !window.openRom(path))
            return smokeFailure(__FILE__, __LINE__);
        auto &gb = window.emulator_->gb;
        cupid_gb_set_rtc_time_source(&gb, rtcTestTime, &clock);
        gb.rtc.s = 41;
        if (!window.startRecording(fixtures.filePath("blocked-recording.wav")) ||
            !window.runFrame() || !window.recorder_->active() || !window.recorder_->framesWritten())
            return smokeFailure(__FILE__, __LINE__);
        const auto recordedFrames = window.recorder_->framesWritten();
        const QString save = window.savePath_;
        const QByteArray cached = window.savedRam_;
        const QByteArray state = window.captureState();
        const auto *core = window.emulator_.get();
        const auto sessionId = window.sessionId_;
        const QString blocked = fixtures.filePath("blocked-save");
        if (state.isEmpty() || !QDir().mkpath(blocked))
            return smokeFailure(__FILE__, __LINE__);
        const auto restorePath = qScopeGuard([&] { window.savePath_ = save; });
        window.savePath_ = blocked;
        for (unsigned ending = 0; ending < 4; ++ending) {
            const auto calls = clock.calls;
            const bool ended = ending == 0   ? window.closeRom()
                               : ending == 1 ? window.restart()
                               : ending == 2 ? window.reloadRom()
                                             : window.openRom(replacement);
            if (ended || window.emulator_.get() != core || window.sessionId_ != sessionId ||
                window.captureState() != state || window.savedRam_ != cached ||
                window.batteryDirty_ || window.saveTimer_.isActive() || !window.failed_ ||
                clock.calls != calls + 1 || !window.recorder_->active() ||
                window.recorder_->framesWritten() != recordedFrames ||
                window.recordingAction_->text() != "Stop Audio &Recording")
                return smokeFailure(__FILE__, __LINE__);
            window.failed_ = false;
        }
    }
    qInfo("CupidGB RTC lifecycle battery persistence and clean autosave checks passed");
    return true;
}

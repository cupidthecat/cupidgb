/**
 * @file    main_window_audio_policy_tests.cpp
 * @brief   Runtime checks for audio preferences, channel controls, and timeline changes.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "main_window_internal.h"

#include <QScopeGuard>
#include <cstddef>

namespace {
bool writeAudioFixture(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readAudioFixture(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

std::vector<std::pair<QString, QVariant>> audioSettingsSnapshot(const QSettings &settings) {
    std::vector<std::pair<QString, QVariant>> values;
    for (const auto &key : settings.allKeys())
        values.emplace_back(key, settings.value(key));
    return values;
}

QByteArray audioHardwareSnapshot(const CupidGb &gb) {
    auto snapshot = std::make_unique<CupidGb>(gb);
    // Output timing may defer digital work differently after a live load. Settle
    // only this copy before comparing the complete hardware at the guest boundary.
    cupid_gb_apu_run(snapshot.get(), true);
    const auto &settled = *snapshot;
    QByteArray bytes;
    const auto append = [&](const void *data, size_t size) {
        bytes.append(static_cast<const char *>(data), qsizetype(size));
    };
    append(&settled.cpu, sizeof(settled.cpu));
    append(settled.io_registers + 0x10, 0x30);
    // Keep hardware latches separate from output integration, queues and filters.
    append(&settled.apu, offsetof(CupidGbApu, output));
    append(&settled.elapsed_cycles, sizeof(settled.elapsed_cycles));
    append(&settled.div_counter, sizeof(settled.div_counter));
    append(&settled.divider_subcycle, sizeof(settled.divider_subcycle));
    return bytes;
}

QByteArray audioStreamHistory(const CupidGb &gb) {
    const auto &output = gb.apu.output;
    QByteArray bytes(reinterpret_cast<const char *>(&output), sizeof(output));
    // Live timeline changes rebase reconstruction phase while retaining all
    // receiving stream history. Compare the remaining representation exactly.
    std::fill_n(bytes.begin() + offsetof(CupidGbAudioOutput, sample_fraction),
                sizeof(output.sample_fraction), char(0));
    std::fill_n(bytes.begin() + offsetof(CupidGbAudioOutput, fraction_half),
                sizeof(output.fraction_half), char(0));
    return bytes;
}

bool sameAudioFilter(const CupidGbAudioFilterState &left, const CupidGbAudioFilterState &right) {
    return left.capacitor[0] == right.capacitor[0] && left.capacitor[1] == right.capacitor[1] &&
           left.interference == right.interference && left.retention == right.retention;
}

enum class AudioDialogEdit { None, InvalidColor, ShortcutConflict, RemapChannel, ClearChannel };
} // namespace

bool MainWindow::audioPolicySmokeTest(const QString &directory) {
    const auto savedSettings = audioSettingsSnapshot(settings_);
    const auto restoreSettings = qScopeGuard([&] {
        settings_.clear();
        for (const auto &[key, value] : savedSettings)
            settings_.setValue(key, value);
    });
    settings_.clear();
    settings_.setValue("emulation/rewindSeconds", 120);
    settings_.setValue("emulation/rewindMiB", 16);
    if (configuredAudioFilter(settings_) != CUPID_GB_AUDIO_FILTER_ACCURATE ||
        configuredAudioInterference(settings_) != 0 || settings_.contains("audio/highpassMode") ||
        settings_.contains("audio/interferenceVolume"))
        return smokeFailure(__FILE__, __LINE__);
    for (const QVariant &invalid :
         {QVariant(-1), QVariant(3), QVariant(2.0), QVariant(1.5), QVariant(true),
          QVariant(QStringLiteral("bad")), QVariant(QStringLiteral("1.0")),
          QVariant(QStringLiteral("+1")), QVariant(QStringLiteral(" 1")),
          QVariant(QStringLiteral("01")), QVariant(QStringLiteral("0x1")),
          QVariant(QStringLiteral("18446744073709551616"))}) {
        settings_.setValue("audio/highpassMode", invalid);
        if (configuredAudioFilter(settings_) != CUPID_GB_AUDIO_FILTER_ACCURATE ||
            settings_.value("audio/highpassMode") != invalid)
            return smokeFailure(__FILE__, __LINE__);
    }
    for (const int mode : {0, 1, 2})
        for (const QVariant &valid :
             {QVariant(mode), QVariant(unsigned(mode)), QVariant(qlonglong(mode)),
              QVariant(qulonglong(mode)), QVariant(QString::number(mode))}) {
            settings_.setValue("audio/highpassMode", valid);
            if (configuredAudioFilter(settings_) != CupidGbAudioFilter(mode) ||
                settings_.value("audio/highpassMode") != valid)
                return smokeFailure(__FILE__, __LINE__);
        }
    for (const QVariant &invalid :
         {QVariant(-1), QVariant(101), QVariant(5.0), QVariant(1.5), QVariant(true),
          QVariant(QStringLiteral("bad")), QVariant(QStringLiteral("5.0")),
          QVariant(QStringLiteral("+5")), QVariant(QStringLiteral(" 5")),
          QVariant(QStringLiteral("05")), QVariant(QStringLiteral("1e2")),
          QVariant(QStringLiteral("18446744073709551616"))}) {
        settings_.setValue("audio/interferenceVolume", invalid);
        if (configuredAudioInterference(settings_) != 0 ||
            settings_.value("audio/interferenceVolume") != invalid)
            return smokeFailure(__FILE__, __LINE__);
    }
    for (const int percent : {0, 5, 17, 35, 100})
        for (const QVariant &valid :
             {QVariant(percent), QVariant(unsigned(percent)), QVariant(qlonglong(percent)),
              QVariant(qulonglong(percent)), QVariant(QString::number(percent))}) {
            settings_.setValue("audio/interferenceVolume", valid);
            if (configuredAudioInterference(settings_) != percent ||
                settings_.value("audio/interferenceVolume") != valid)
                return smokeFailure(__FILE__, __LINE__);
        }
    settings_.remove("audio/highpassMode");
    settings_.remove("audio/interferenceVolume");

    const QDir fixtures(QDir(directory).filePath("audio-policy"));
    if (!QDir().mkpath(fixtures.path()))
        return smokeFailure(__FILE__, __LINE__);
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0xc3);
    rom[0x101] = char(0x50);
    rom[0x102] = char(0x01);
    rom[0x147] = char(0x09);
    rom[0x149] = char(0x02);
    const uint8_t tone[] = {0x3e, 0x80, 0xe0, 0x26, 0x3e, 0x77, 0xe0, 0x24, 0x3e, 0x11,
                            0xe0, 0x25, 0x3e, 0x80, 0xe0, 0x11, 0x3e, 0xf0, 0xe0, 0x12,
                            0x3e, 0x00, 0xe0, 0x13, 0x3e, 0x84, 0xe0, 0x14, 0x18, 0xfe};
    std::memcpy(rom.data() + 0x150, tone, sizeof(tone));
    const auto cartridge = [&](const QString &name, uint8_t color, uint8_t title) {
        rom[0x134] = char(title);
        rom[0x143] = char(color);
        uint8_t checksum = 0;
        for (int address = 0x134; address <= 0x14c; ++address)
            checksum = uint8_t(checksum - uint8_t(rom[address]) - 1);
        rom[0x14d] = char(checksum);
        const QString path = fixtures.filePath(name);
        return writeAudioFixture(path, rom) ? path : QString();
    };
    const QString monochrome = cartridge("tone.gb", 0, 'A');
    const QString replacement = cartridge("replacement.gb", 0, 'B');
    const QString invalidBattery = cartridge("invalid-battery.gb", 0, 'C');
    const QString color = cartridge("tone-color.gbc", 0x80, 'D');
    if (monochrome.isEmpty() || replacement.isEmpty() || invalidBattery.isEmpty() ||
        color.isEmpty() ||
        !writeAudioFixture(fixtures.filePath("invalid-battery.sav"), "invalid battery"))
        return smokeFailure(__FILE__, __LINE__);

    const auto configureAudio = [&](MainWindow &window, int expectedFilter, int expectedPercent,
                                    int selectedFilter, int selectedPercent, bool accept,
                                    AudioDialogEdit edit = AudioDialogEdit::None,
                                    bool capture = false) {
        bool inspected = false;
        bool submitted = false;
        bool warningSeen = false;
        bool timedOut = false;
        bool captured = !capture;
        const bool warningExpected =
            edit == AudioDialogEdit::InvalidColor || edit == AudioDialogEdit::ShortcutConflict;
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
                if (warningSeen)
                    active->reject();
                return;
            }
            submitted = true;
            auto *filter = active->findChild<QComboBox *>("audioFilterMode");
            auto *interference = active->findChild<QSlider *>("audioInterferenceVolume");
            auto *row = active->findChild<QWidget *>("audioInterferenceRow");
            auto *value = active->findChild<QLabel *>("audioInterferenceVolumeValue");
            auto *buttons = active->findChild<QDialogButtonBox *>();
            auto *form =
                filter ? qobject_cast<QFormLayout *>(filter->parentWidget()->layout()) : nullptr;
            auto *filterLabel =
                form ? qobject_cast<QLabel *>(form->labelForField(filter)) : nullptr;
            auto *interferenceLabel =
                form && row ? qobject_cast<QLabel *>(form->labelForField(row)) : nullptr;
            inspected =
                filter && interference && value && buttons && filterLabel && interferenceLabel &&
                filterLabel->text() == "Highpass Filter:" &&
                interferenceLabel->text() == "Interference Volume:" && filter->count() == 3 &&
                filter->itemText(0) == "None (Keep DC Offset)" &&
                filter->itemText(1) == "Accurate" && filter->itemText(2) == "Preserve Waveform" &&
                filter->itemData(0).toInt() == 0 && filter->itemData(1).toInt() == 1 &&
                filter->itemData(2).toInt() == 2 &&
                filter->currentData().toInt() == expectedFilter && interference->minimum() == 0 &&
                interference->maximum() == 100 && interference->singleStep() == 5 &&
                interference->pageStep() == 5 && interference->value() == expectedPercent &&
                value->text() == QString("%1%").arg(expectedPercent);
            if (!inspected) {
                active->reject();
                return;
            }
            filter->setCurrentIndex(filter->findData(selectedFilter));
            interference->setValue(selectedPercent);
            inspected = value->text() == QString("%1%").arg(selectedPercent);
            if (edit == AudioDialogEdit::InvalidColor) {
                auto *colorEdit = active->findChild<QLineEdit *>("customPalette/0");
                inspected = inspected && colorEdit;
                if (colorEdit)
                    colorEdit->setText("bad");
            } else if (edit != AudioDialogEdit::None) {
                const QString name = edit == AudioDialogEdit::ClearChannel
                                         ? "shortcut/audioChannel/3"
                                         : "shortcut/audioChannel/0";
                auto *channel = active->findChild<QKeySequenceEdit *>(name);
                inspected = inspected && channel;
                if (channel && edit == AudioDialogEdit::RemapChannel)
                    channel->setKeySequence(QKeySequence("Ctrl+Alt+1"));
                if (channel && edit == AudioDialogEdit::ClearChannel)
                    channel->setKeySequence(QKeySequence());
                if (channel && edit == AudioDialogEdit::ShortcutConflict) {
                    auto *pause = active->findChild<QKeySequenceEdit *>("shortcut/pause");
                    inspected = inspected && pause && !pause->keySequence().isEmpty();
                    if (pause)
                        channel->setKeySequence(pause->keySequence());
                }
            }
            if (!inspected) {
                active->reject();
                return;
            }
            if (capture) {
                const QString output = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
                if (auto *scroll = active->findChild<QScrollArea *>())
                    scroll->ensureWidgetVisible(interference);
                captured = output.isEmpty() ||
                           (QDir().mkpath(output) &&
                            active->grab().save(QDir(output).filePath("audio-settings.png")));
            }
            auto *button =
                buttons->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel);
            if (!button || !captured) {
                inspected = false;
                active->reject();
                return;
            }
            // Leave this callback before validation can open a nested warning.
            QTimer::singleShot(0, button, [button] { button->click(); });
        });
        command.start();
        window.configure();
        command.stop();
        return inspected && !timedOut && captured && warningSeen == warningExpected;
    };
    const auto policyMatches = [](const MainWindow &window, CupidGbAudioFilter mode, int percent,
                                  unsigned muted) {
        if (!window.emulator_ || cupid_gb_apu_get_filter_mode(&window.emulator_->gb) != mode ||
            cupid_gb_apu_get_interference_volume(&window.emulator_->gb) != double(percent) / 100)
            return false;
        for (size_t channel = 0; channel < window.audioChannelActions_.size(); ++channel) {
            const bool expected = (muted & (1u << channel)) != 0;
            if (!window.audioChannelActions_[channel] ||
                cupid_gb_apu_is_channel_muted(&window.emulator_->gb, unsigned(channel)) !=
                    expected ||
                window.audioChannelActions_[channel]->isChecked() == expected)
                return false;
        }
        return true;
    };

    MainWindow session(true, false, audioCheck_, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
    session.timer_.stop();
    for (size_t channel = 0; channel < session.audioChannelActions_.size(); ++channel) {
        auto *action = session.audioChannelActions_[channel];
        if (!action || !action->isCheckable() || !action->isChecked() || !action->isEnabled() ||
            action->text() != audioChannelNames[channel] ||
            action->shortcut() != QKeySequence(QString("Alt+%1").arg(channel + 1)) ||
            std::find(session.shortcutActions_.begin(), session.shortcutActions_.end(), action) ==
                session.shortcutActions_.end())
            return smokeFailure(__FILE__, __LINE__);
    }
    const auto initialSettings = audioSettingsSnapshot(settings_);
    if (!configureAudio(session, 1, 0, 0, 65, false) || session.emulator_ ||
        audioSettingsSnapshot(settings_) != initialSettings ||
        !configureAudio(session, 1, 0, 2, 35, true) ||
        settings_.value("audio/highpassMode").toInt() != 2 ||
        settings_.value("audio/interferenceVolume").toInt() != 35)
        return smokeFailure(__FILE__, __LINE__);
    const auto beforeChannel = audioSettingsSnapshot(settings_);
    session.audioChannelActions_[2]->trigger();
    if (session.audioChannelActions_[2]->isChecked() ||
        audioSettingsSnapshot(settings_) != beforeChannel || !session.openRom(monochrome) ||
        !policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 4) ||
        session.emulator_->gb.elapsed_cycles || session.emulator_->gb.cpu.pc != 0x100 ||
        (audioCheck_ && !session.audio_))
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    settings_.setValue("audio/volume", 40);
    session.setupAudio();
    session.muteAction_->setChecked(true);
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
    const auto originalSettings = audioSettingsSnapshot(settings_);
    const QByteArray state = session.captureState();
    const QByteArray hardware = audioHardwareSnapshot(session.emulator_->gb);
    if (state.isEmpty())
        return smokeFailure(__FILE__, __LINE__);
    for (const auto edit : {AudioDialogEdit::None, AudioDialogEdit::InvalidColor,
                            AudioDialogEdit::ShortcutConflict}) {
        if (!configureAudio(session, 2, 35, 0, 100, edit != AudioDialogEdit::None, edit) ||
            audioSettingsSnapshot(settings_) != originalSettings ||
            session.captureState() != state ||
            !policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 4) ||
            !historyUnchanged() || !session.paused_)
            return smokeFailure(__FILE__, __LINE__);
    }
    int previousFilter = 2;
    int previousPercent = 35;
    for (const auto &[mode, percent] : {std::pair{0, 0}, std::pair{1, 100}, std::pair{2, 35}}) {
        if (!configureAudio(session, previousFilter, previousPercent, mode, percent, true) ||
            !policyMatches(session, CupidGbAudioFilter(mode), percent, 4) ||
            settings_.value("audio/highpassMode").toInt() != mode ||
            settings_.value("audio/interferenceVolume").toInt() != percent ||
            audioHardwareSnapshot(session.emulator_->gb) != hardware || !historyUnchanged() ||
            !session.muteAction_->isChecked() || settings_.value("audio/volume").toInt() != 40 ||
            (session.audio_ && session.audio_->volume() != 0))
            return smokeFailure(__FILE__, __LINE__);
        previousFilter = mode;
        previousPercent = percent;
    }
    if (!configureAudio(session, 2, 35, 2, 35, false, AudioDialogEdit::None, true))
        return smokeFailure(__FILE__, __LINE__);
    session.muteAction_->setChecked(false);
    if (session.audio_ && (session.audio_->volume() < 0.399f || session.audio_->volume() > 0.401f))
        return smokeFailure(__FILE__, __LINE__);
    for (auto *action : session.audioChannelActions_)
        action->setChecked(true);
    const auto channelSettings = audioSettingsSnapshot(settings_);
    for (size_t channel = 0; channel < session.audioChannelActions_.size(); ++channel) {
        auto *action = session.audioChannelActions_[channel];
        action->trigger();
        if (!policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 1u << channel) ||
            audioHardwareSnapshot(session.emulator_->gb) != hardware || !historyUnchanged())
            return smokeFailure(__FILE__, __LINE__);
        action->trigger();
        if (!policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 0))
            return smokeFailure(__FILE__, __LINE__);
    }
    if (audioSettingsSnapshot(settings_) != channelSettings ||
        !configureAudio(session, 2, 35, 2, 35, true, AudioDialogEdit::RemapChannel) ||
        session.audioChannelActions_[0]->shortcut() != QKeySequence("Ctrl+Alt+1") ||
        settings_.value("shortcuts/audioChannel/0").toString() != "Ctrl+Alt+1" ||
        !configureAudio(session, 2, 35, 2, 35, true, AudioDialogEdit::ClearChannel) ||
        !session.audioChannelActions_[3]->shortcut().isEmpty() ||
        !settings_.value("shortcuts/audioChannel/3").toString().isEmpty() || !historyUnchanged())
        return smokeFailure(__FILE__, __LINE__);
    session.audioChannelActions_[0]->trigger();
    session.audioChannelActions_[2]->trigger();
    if (!policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 5))
        return smokeFailure(__FILE__, __LINE__);
    {
        MainWindow reopened(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_CGB, 0);
        reopened.timer_.stop();
        if (!configureAudio(reopened, 2, 35, 2, 35, false) || !reopened.openRom(color) ||
            !policyMatches(reopened, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 0) ||
            reopened.audioChannelActions_[0]->shortcut() != QKeySequence("Ctrl+Alt+1") ||
            !reopened.audioChannelActions_[3]->shortcut().isEmpty())
            return smokeFailure(__FILE__, __LINE__);
    }
    const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        auto *menu = session.findChild<QMenu *>("audioChannels");
        if (!menu || !QDir().mkpath(captureDirectory))
            return smokeFailure(__FILE__, __LINE__);
        session.show();
        menu->popup(session.mapToGlobal(QPoint(30, 40)));
        QApplication::processEvents();
        const bool captured =
            menu->grab().save(QDir(captureDirectory).filePath("audio-channels.png"));
        menu->close();
        QApplication::processEvents();
        if (!captured)
            return smokeFailure(__FILE__, __LINE__);
    }
    if (!session.restart() ||
        !policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 5) ||
        session.emulator_->gb.elapsed_cycles)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    if (!session.reloadRom() || !session.paused_ ||
        !policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 5) ||
        !session.closeRom() || !session.openRom(replacement) ||
        !policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 5) ||
        session.emulator_->gb.elapsed_cycles)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    if (!session.startRecording(fixtures.filePath("rejected-lifecycle.wav")) ||
        !session.runFrame() || !session.recorder_->framesWritten())
        return smokeFailure(__FILE__, __LINE__);
    const auto *live = session.emulator_.get();
    const auto liveId = session.sessionId_;
    const auto recordedFrames = session.recorder_->framesWritten();
    const QByteArray liveState = session.captureState();
    const auto rejected = [&](const auto &operation) {
        const bool accepted = operation();
        const bool preserved =
            !accepted && session.failed_ && session.emulator_.get() == live &&
            session.sessionId_ == liveId && session.captureState() == liveState &&
            policyMatches(session, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 5) &&
            session.recorder_->active() && session.recorder_->framesWritten() == recordedFrames &&
            session.recordingAction_->text() == "Stop Audio &Recording";
        session.failed_ = false;
        return preserved;
    };
    if (liveState.isEmpty())
        return smokeFailure(__FILE__, __LINE__);
    {
        QScopedValueRollback<QString> firmware(session.bootDirectoryOverride_,
                                               fixtures.filePath("missing-firmware"));
        if (!rejected([&] { return session.openRom(monochrome); }))
            return smokeFailure(__FILE__, __LINE__);
    }
    if (!rejected([&] { return session.openRom(invalidBattery); }))
        return smokeFailure(__FILE__, __LINE__);
    const QString blocked = fixtures.filePath("blocked-save");
    if (!QDir().mkpath(blocked))
        return smokeFailure(__FILE__, __LINE__);
    {
        QScopedValueRollback<QString> save(session.savePath_, blocked);
        for (unsigned ending = 0; ending < 4; ++ending)
            if (!rejected([&] {
                    return ending == 0   ? session.closeRom()
                           : ending == 1 ? session.restart()
                           : ending == 2 ? session.reloadRom()
                                         : session.openRom(monochrome);
                }))
                return smokeFailure(__FILE__, __LINE__);
    }
    if (!session.stopRecording())
        return smokeFailure(__FILE__, __LINE__);

    // Disk loads, rewind and Backstep restore guest execution without replacing
    // the receiving listening choices or its already-running analog history.
    const auto selectPolicy = [&](MainWindow &window, CupidGbAudioFilter mode, int percent,
                                  unsigned muted) {
        settings_.setValue("audio/highpassMode", int(mode));
        settings_.setValue("audio/interferenceVolume", percent);
        for (size_t channel = 0; channel < window.audioChannelActions_.size(); ++channel)
            window.audioChannelActions_[channel]->setChecked(!(muted & (1u << channel)));
        window.applyAudioPolicy(&window.emulator_->gb);
        return policyMatches(window, mode, percent, muted);
    };
    for (const auto model : {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB}) {
        settings_.setValue("audio/highpassMode", 1);
        settings_.setValue("audio/interferenceVolume", 0);
        MainWindow timeline(true, false, false, QStringLiteral(""), model, 0);
        timeline.timer_.stop();
        if (!timeline.openRom(model == CUPID_GB_MODEL_CGB ? color : monochrome))
            return smokeFailure(__FILE__, __LINE__);
        timeline.setPaused(true);
        if (!timeline.runFrame() || !timeline.runFrame() || !timeline.saveSlot(4))
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray checkpoint = timeline.captureState();
        const QByteArray checkpointHardware = audioHardwareSnapshot(timeline.emulator_->gb);
        if (checkpoint.isEmpty() || !selectPolicy(timeline, CUPID_GB_AUDIO_FILTER_OFF, 65, 5) ||
            !timeline.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray beforeRejectedState = timeline.captureState();
        if (timeline.restoreState(QByteArray("invalid", 7), false) ||
            timeline.captureState() != beforeRejectedState ||
            !policyMatches(timeline, CUPID_GB_AUDIO_FILTER_OFF, 65, 5))
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray receivingHistory = audioStreamHistory(timeline.emulator_->gb);
        if (!timeline.restoreState(checkpoint, false) ||
            audioHardwareSnapshot(timeline.emulator_->gb) != checkpointHardware ||
            audioStreamHistory(timeline.emulator_->gb) != receivingHistory ||
            timeline.emulator_->gb.apu.output.sample_fraction ||
            timeline.emulator_->gb.apu.output.fraction_half ||
            !policyMatches(timeline, CUPID_GB_AUDIO_FILTER_OFF, 65, 5))
            return smokeFailure(__FILE__, __LINE__);
        if (!selectPolicy(timeline, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 10) ||
            !timeline.runFrame())
            return smokeFailure(__FILE__, __LINE__);
        const auto filterBeforeSlot = timeline.emulator_->gb.apu.output.filter;
        if (!timeline.loadSlot(4) || !timeline.paused_ ||
            audioHardwareSnapshot(timeline.emulator_->gb) != checkpointHardware ||
            !sameAudioFilter(timeline.emulator_->gb.apu.output.filter, filterBeforeSlot) ||
            !policyMatches(timeline, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 35, 10))
            return smokeFailure(__FILE__, __LINE__);
        for (unsigned frame = 0; frame < 4; ++frame)
            if (!timeline.runFrame())
                return smokeFailure(__FILE__, __LINE__);
        const auto beforeRewind = timeline.emulator_->gb.elapsed_cycles;
        const auto generatedBeforeRewind = timeline.generatedAudioFrames_;
        if (!selectPolicy(timeline, CUPID_GB_AUDIO_FILTER_ACCURATE, 100, 3) ||
            !timeline.rewindFrame() ||
            !policyMatches(timeline, CUPID_GB_AUDIO_FILTER_ACCURATE, 100, 3) ||
            !timeline.rewindFrame() || timeline.emulator_->gb.elapsed_cycles >= beforeRewind ||
            timeline.generatedAudioFrames_ <= generatedBeforeRewind ||
            !policyMatches(timeline, CUPID_GB_AUDIO_FILTER_ACCURATE, 100, 3))
            return smokeFailure(__FILE__, __LINE__);
        timeline.ensureDebugger();
        timeline.pauseDebugger();
        const QByteArray beforeStep = audioHardwareSnapshot(timeline.emulator_->gb);
        const auto cyclesBeforeStep = timeline.emulator_->gb.elapsed_cycles;
        timeline.debuggerCommand("step");
        QElapsedTimer stepDeadline;
        stepDeadline.start();
        while (timeline.debuggerTimer_.isActive() && stepDeadline.elapsed() < 5000)
            timeline.debuggerSlice();
        if (timeline.debuggerTimer_.isActive() || timeline.debuggerHistory_.size() != 1 ||
            timeline.emulator_->gb.elapsed_cycles <= cyclesBeforeStep ||
            !selectPolicy(timeline, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 20, 12))
            return smokeFailure(__FILE__, __LINE__);
        const QByteArray beforeBackstep = audioStreamHistory(timeline.emulator_->gb);
        if (!timeline.debuggerBackstep() ||
            audioHardwareSnapshot(timeline.emulator_->gb) != beforeStep ||
            audioStreamHistory(timeline.emulator_->gb) != beforeBackstep ||
            timeline.emulator_->gb.apu.output.sample_fraction ||
            timeline.emulator_->gb.apu.output.fraction_half || !timeline.paused_ ||
            !policyMatches(timeline, CUPID_GB_AUDIO_FILTER_PRESERVE_WAVEFORM, 20, 12))
            return smokeFailure(__FILE__, __LINE__);
    }

    const QDir firmware(fixtures.filePath("firmware"));
    if (!QDir().mkpath(firmware.path()))
        return smokeFailure(__FILE__, __LINE__);
    for (const auto model : {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB}) {
        QByteArray boot(model == CUPID_GB_MODEL_CGB ? 0x900 : 0x100, char(0));
        boot[0] = char(0x18);
        boot[1] = char(0xfe);
        if (!writeAudioFixture(
                firmware.filePath(model == CUPID_GB_MODEL_CGB ? "cgb_boot.bin" : "dmg_boot.bin"),
                boot))
            return smokeFailure(__FILE__, __LINE__);
    }
    settings_.setValue("audio/highpassMode", 0);
    settings_.setValue("audio/interferenceVolume", 65);
    for (const auto model : {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB})
        for (const bool cold : {false, true}) {
            MainWindow booted(true, false, false, cold ? firmware.path() : QStringLiteral(""),
                              model, 0);
            booted.timer_.stop();
            booted.audioChannelActions_[3]->setChecked(false);
            if (!booted.openRom(model == CUPID_GB_MODEL_CGB ? color : monochrome) ||
                !policyMatches(booted, CUPID_GB_AUDIO_FILTER_OFF, 65, 8) ||
                booted.emulator_->gb.elapsed_cycles ||
                booted.emulator_->gb.boot_rom_enabled != cold ||
                booted.emulator_->gb.cpu.pc != (cold ? 0 : 0x100) || !booted.reloadRom() ||
                !policyMatches(booted, CUPID_GB_AUDIO_FILTER_OFF, 65, 8) ||
                booted.emulator_->gb.elapsed_cycles)
                return smokeFailure(__FILE__, __LINE__);
        }

    // Listening gain is a host-sink control. Keep the existing forward-PCM file
    // contract independent from both global mute and listening volume.
    const auto recordTone = [&](const QString &name, int volume, bool muted) {
        settings_.setValue("audio/highpassMode", 1);
        settings_.setValue("audio/interferenceVolume", 0);
        settings_.setValue("audio/volume", volume);
        settings_.setValue("audio/mute", muted);
        MainWindow recording(true, false, false, QStringLiteral(""), CUPID_GB_MODEL_DMG_ABC, 0);
        recording.timer_.stop();
        const QString path = fixtures.filePath(name);
        if (!recording.openRom(monochrome) ||
            !policyMatches(recording, CUPID_GB_AUDIO_FILTER_ACCURATE, 0, 0))
            return QByteArray();
        recording.setPaused(true);
        if (!recording.startRecording(path) || !recording.runFrame() || !recording.runFrame() ||
            !recording.stopRecording())
            return QByteArray();
        return readAudioFixture(path);
    };
    const QByteArray audible = recordTone("audible.wav", 100, false);
    const QByteArray muted = recordTone("muted.wav", 13, true);
    if (audible.size() <= 44 || audible != muted ||
        std::none_of(audible.begin() + 44, audible.end(), [](char value) { return value != 0; }))
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB audio preferences, channel controls and timeline workflows passed");
    return true;
}

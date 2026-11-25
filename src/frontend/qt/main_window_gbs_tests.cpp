/**
 * @file    main_window_gbs_tests.cpp
 * @brief   Music loading, transport, audio, state and compact-window workflow regressions.
 * @author  cupidthecat
 * @date    2026-11-18
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

#include "gbs_player.h"
#include "main_window_internal.h"

#include <QScopeGuard>
#include <QToolButton>

namespace {
QByteArray musicFixture(uint8_t count = 3, uint8_t initial = 2, uint16_t load = 0x200) {
    QByteArray bytes(CUPID_GB_GBS_HEADER_SIZE + 0x280 - load, char(0));
    bytes.replace(0, 4, QByteArray("GBS\1", 4));
    bytes[4] = char(count);
    bytes[5] = char(initial);
    const auto word = [&](int offset, uint16_t value) {
        bytes[offset] = char(value);
        bytes[offset + 1] = char(value >> 8);
    };
    word(6, load);
    word(8, 0x200);
    word(10, 0x240);
    word(12, 0xdff0);
    bytes.replace(16, 13, QByteArray("CupidGB tones"));
    bytes.replace(48, 11, QByteArray("Composer \xe9\xf1", 11));
    bytes.replace(80, 4, QByteArray("2026"));
    QByteArray init;
    const auto append = [&](std::initializer_list<uint8_t> code) {
        for (const uint8_t byte : code)
            init.append(char(byte));
    };
    // The selected track is observable in RAM. The play routine increments a
    // separate byte; its pulse tone exercises real synthesis without external assets.
    append({0xea, 0x00, 0xc0, 0xaf, 0xea, 0x01, 0xc0, 0x3e, 0xa5, 0xea, 0x02, 0xc0});
    for (const auto value : {std::pair<uint8_t, uint8_t>{0x26, 0x80},
                             {0x24, 0x77},
                             {0x25, 0x11},
                             {0x11, 0x80},
                             {0x12, 0xf0},
                             {0x13, 0x87},
                             {0x14, 0x83}})
        append({0x3e, value.second, 0xe0, value.first});
    append({0xc9});
    const QByteArray play = QByteArray::fromHex("fa01c03cea01c0c9");
    bytes.replace(CUPID_GB_GBS_HEADER_SIZE + 0x200 - load, init.size(), init);
    bytes.replace(CUPID_GB_GBS_HEADER_SIZE + 0x240 - load, play.size(), play);
    return bytes;
}

bool writeMusicFixture(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray musicZip(const QList<QPair<QByteArray, QByteArray>> &entries) {
    QByteArray result, directory;
    const auto number = [](QByteArray &bytes, uint32_t value, unsigned width) {
        for (unsigned byte = 0; byte < width; ++byte)
            bytes.append(char(value >> (8 * byte)));
    };
    for (const auto &entry : entries) {
        uint32_t crc = 0xffffffff;
        for (const unsigned char byte : entry.second) {
            crc ^= byte;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
        crc ^= 0xffffffff;
        const auto offset = uint32_t(result.size());
        number(result, 0x04034b50, 4);
        number(result, 20, 2);
        for (unsigned i = 0; i < 4; ++i)
            number(result, 0, 2);
        number(result, crc, 4);
        number(result, uint32_t(entry.second.size()), 4);
        number(result, uint32_t(entry.second.size()), 4);
        number(result, uint32_t(entry.first.size()), 2);
        number(result, 0, 2);
        result += entry.first;
        result += entry.second;
        number(directory, 0x02014b50, 4);
        number(directory, 20, 2);
        number(directory, 20, 2);
        for (unsigned i = 0; i < 4; ++i)
            number(directory, 0, 2);
        number(directory, crc, 4);
        number(directory, uint32_t(entry.second.size()), 4);
        number(directory, uint32_t(entry.second.size()), 4);
        number(directory, uint32_t(entry.first.size()), 2);
        for (unsigned i = 0; i < 4; ++i)
            number(directory, 0, 2);
        number(directory, 0, 4);
        number(directory, offset, 4);
        directory += entry.first;
    }
    const uint32_t offset = uint32_t(result.size());
    result += directory;
    number(result, 0x06054b50, 4);
    number(result, 0, 4);
    number(result, uint32_t(entries.size()), 2);
    number(result, uint32_t(entries.size()), 2);
    number(result, uint32_t(directory.size()), 4);
    number(result, offset, 4);
    number(result, 0, 2);
    return result;
}
} // namespace

bool MainWindow::gbsSmokeTest(const QString &directory) {
    QMap<QString, QVariant> previousPreferences;
    for (const auto &key : settings_.allKeys())
        previousPreferences[key] = settings_.value(key);
    const auto restorePreferences = qScopeGuard([&] {
        settings_.clear();
        for (auto it = previousPreferences.cbegin(); it != previousPreferences.cend(); ++it)
            settings_.setValue(it.key(), it.value());
    });
    settings_.setValue("audio/mute", true);
    settings_.setValue("audio/volume", 0);
    settings_.setValue("emulation/rewindSeconds", 2);
    settings_.setValue("emulation/rtcMode", int(CUPID_GB_RTC_SYNC_TO_HOST));
    const QDir fixtures(directory);
    const QString path = fixtures.filePath(QString::fromUtf8("music-\xc3\xa9.gbs"));
    const QByteArray original = musicFixture();
    if (original.size() >= 0x150 || !writeMusicFixture(path, original))
        return smokeFailure(__FILE__, __LINE__);
    QString error;
    const auto media = readGameMedia(path, error);
    if (media.kind != GameMediaKind::Gbs || media.data != original)
        return smokeFailure(__FILE__, __LINE__);

    const QString missingFirmware = fixtures.filePath("no-music-firmware");
    MainWindow session(true, true, audioCheck_, missingFirmware, CUPID_GB_MODEL_DMG_ABC,
                       uint64_t(1));
    session.timer_.stop();
    session.resize(712, 598);
    session.show();
    const QSize gameSize = session.size();
    const QSize gameMaximum = session.maximumSize();
    if (!session.openRom(path) || !session.musicInfo_ || session.musicTrack_ != 1 ||
        session.emulator_->gb.model != CUPID_GB_MODEL_DMG_ABC ||
        session.emulator_->gb.boot_rom_size || cupid_gb_battery_size(&session.emulator_->gb) ||
        session.centralWidget() != session.musicPlayer_ || !session.display_->isHidden() ||
        session.minimumSize() != session.maximumSize() || session.size() == gameSize ||
        session.reloadAction_->isEnabled() || session.hotSwapAction_->isEnabled() ||
        session.workBoyAction_->isEnabled() || session.screenshotAction_->isEnabled() ||
        session.copyScreenshotAction_->isEnabled() ||
        session.findChild<QAction *>("fullscreen")->isEnabled())
        return smokeFailure(__FILE__, __LINE__);
    session.setWorkBoyConnected(true);
    if (session.workBoy_)
        return smokeFailure(__FILE__, __LINE__);
    session.timer_.stop();
    for (auto *action : session.audioChannelActions_)
        action->setChecked(true);
    session.applyAudioPolicy(&session.emulator_->gb);
    auto *tracks = session.musicPlayer_->findChild<QComboBox *>("gbsTracks");
    auto *restartTrack = session.musicPlayer_->findChild<QToolButton *>("gbsRestart");
    auto *playPause = session.musicPlayer_->findChild<QToolButton *>("gbsPlayPause");
    auto *previous = session.musicPlayer_->findChild<QToolButton *>("gbsPrevious");
    auto *next = session.musicPlayer_->findChild<QToolButton *>("gbsNext");
    auto *waveform = session.musicPlayer_->findChild<QWidget *>("gbsWaveform");
    auto *author = session.musicPlayer_->findChild<QLabel *>("gbsAuthor");
    if (!tracks || !restartTrack || !playPause || !previous || !next || !waveform || !author ||
        tracks->count() != 3 || tracks->currentIndex() != 1 ||
        author->text() != QString::fromLatin1("Composer \xe9\xf1", 11) ||
        author->textFormat() != Qt::PlainText || session.paused_ ||
        !session.muteAction_->isChecked())
        return smokeFailure(__FILE__, __LINE__);
    const QImage initialWave = waveform->grab().toImage();
    const auto gameImages = session.presentedImages_;
    const auto playFrames = [&](unsigned frames) {
        for (unsigned frame = 0; frame < frames; ++frame)
            if (!session.runFrame())
                return false;
        return true;
    };
    if (!playFrames(8) || cupid_gb_debug_peek(&session.emulator_->gb, 0xc000) != 1 ||
        cupid_gb_debug_peek(&session.emulator_->gb, 0xc002) != 0xa5 ||
        !cupid_gb_debug_peek(&session.emulator_->gb, 0xc001) ||
        waveform->grab().toImage() == initialWave || !session.musicWaveformsPublished_ ||
        !session.generatedAudioFrames_ || session.presentedImages_ != gameImages ||
        !session.image_.isNull() ||
        (audioCheck_ && (!session.audio_ || session.audio_->volume() != 0)))
        return smokeFailure(__FILE__, __LINE__);
    const auto publications = session.musicWaveformsPublished_;
    session.present(Presentation::Redraw);
    session.present(Presentation::Reset);
    if (session.musicWaveformsPublished_ != publications)
        return smokeFailure(__FILE__, __LINE__);

    // Native paint requests cannot expose partial PCM, and the ring has exactly
    // 1024 stereo frames even when a single collection crosses its capacity.
    GbsPlayer ring;
    ring.resize(ring.sizeHint());
    ring.setInfo(*session.musicInfo_, 1);
    auto *ringWave = ring.findChild<QWidget *>("gbsWaveform");
    const QImage emptyRing = ringWave->grab().toImage();
    const std::array<int16_t, 2> pulse{16000, 16000};
    ring.appendSamples(pulse.data(), 1);
    if (ringWave->grab().toImage() != emptyRing)
        return smokeFailure(__FILE__, __LINE__);
    ring.publishWaveform();
    if (ringWave->grab().toImage() == emptyRing)
        return smokeFailure(__FILE__, __LINE__);
    std::array<int16_t, 2048> silence{};
    ring.appendSamples(silence.data(), 1024);
    ring.publishWaveform();
    if (ringWave->grab().toImage() != emptyRing)
        return smokeFailure(__FILE__, __LINE__);

    tracks->setCurrentIndex(2);
    if (!QMetaObject::invokeMethod(tracks, "activated", Qt::DirectConnection, Q_ARG(int, 2)) ||
        session.musicTrack_ != 2 || !playFrames(2) ||
        cupid_gb_debug_peek(&session.emulator_->gb, 0xc000) != 2)
        return smokeFailure(__FILE__, __LINE__);
    next->click();
    if (session.musicTrack_ != 0 || tracks->currentIndex() != 0)
        return smokeFailure(__FILE__, __LINE__);
    previous->click();
    if (session.musicTrack_ != 2 || tracks->currentIndex() != 2)
        return smokeFailure(__FILE__, __LINE__);
    playPause->click();
    if (!session.paused_ || playPause->accessibleName() != "Play")
        return smokeFailure(__FILE__, __LINE__);
    restartTrack->click();
    if (session.paused_ || session.musicTrack_ != 2 || !playFrames(2) ||
        cupid_gb_debug_peek(&session.emulator_->gb, 0xc000) != 2)
        return smokeFailure(__FILE__, __LINE__);
    if (!session.restart() || session.musicTrack_ != 1 || tracks->currentIndex() != 1 ||
        !session.startMusicTrack(2) || !session.quickReset() || session.musicTrack_ != 1 ||
        !session.startMusicTrack(2) || session.reloadRom())
        return smokeFailure(__FILE__, __LINE__);
    session.timer_.stop();

    // Recording stays attached through track changes, including already generated
    // samples that have not yet reached the frontend collector.
    const QString recordingPath = fixtures.filePath("music-recording.wav");
    if (!session.startRecording(recordingPath) || !playFrames(2))
        return smokeFailure(__FILE__, __LINE__);
    auto *recorder = session.recorder_.get();
    auto *sink = session.audio_.get();
    for (unsigned step = 0; step < 20000 && session.emulator_->gb.apu.output.buf_write < 64; ++step)
        if (!cupid_emulator_step(session.emulator_.get()))
            return smokeFailure(__FILE__, __LINE__);
    const auto queued = session.emulator_->gb.apu.output;
    QByteArray queuedPcm;
    for (uint32_t sample = 0; sample < queued.buf_write * 2; ++sample) {
        queuedPcm.append(char(uint16_t(queued.buf[sample])));
        queuedPcm.append(char(uint16_t(queued.buf[sample]) >> 8));
    }
    const auto recordedBefore = recorder->framesWritten();
    const QImage trackWave = waveform->grab().toImage();
    if (queued.buf_write < 64 || !session.startMusicTrack(0))
        return smokeFailure(__FILE__, __LINE__);
    const auto &afterTrack = session.emulator_->gb.apu.output;
    // Real sound-register reset writes alter reconstruction history. The queued
    // prefix, listening choices and fractional delivery credit remain owned by
    // the live stream and must survive that hardware transition unchanged.
    if (session.recorder_.get() != recorder || session.audio_.get() != sink ||
        recorder->framesWritten() != recordedBefore || afterTrack.buf_write != queued.buf_write ||
        std::memcmp(afterTrack.buf, queued.buf, size_t(queued.buf_write) * 2 * sizeof(int16_t)) ||
        afterTrack.sample_rate != queued.sample_rate ||
        afterTrack.sample_acc != queued.sample_acc ||
        afterTrack.sample_fraction != queued.sample_fraction ||
        afterTrack.fraction_step != queued.fraction_step ||
        afterTrack.fraction_half != queued.fraction_half ||
        afterTrack.max_span != queued.max_span ||
        afterTrack.policy.filter_disabled != queued.policy.filter_disabled ||
        afterTrack.policy.preserve_waveform != queued.policy.preserve_waveform ||
        afterTrack.policy.muted_channels != queued.policy.muted_channels ||
        afterTrack.policy.interference_volume != queued.policy.interference_volume ||
        waveform->grab().toImage() != trackWave || !playFrames(2) ||
        recorder->framesWritten() < recordedBefore + queued.buf_write)
        return smokeFailure(__FILE__, __LINE__);
    if (!session.saveSlot(4) || !session.startMusicTrack(2) || !playFrames(2) ||
        !session.loadSlot(4) || session.musicTrack_ != 2 || tracks->currentIndex() != 2 ||
        cupid_gb_debug_peek(&session.emulator_->gb, 0xc000) != 0 || !session.musicInfo_ ||
        session.musicInfo_->first_track != 1 || session.recorder_.get() != recorder ||
        !recorder->active())
        return smokeFailure(__FILE__, __LINE__);

    session.pauseDebugger();
    const auto beforeStep = session.emulator_->gb.elapsed_cycles;
    const auto beforeDebugWave = session.musicWaveformsPublished_;
    session.debuggerCommand("step");
    for (unsigned slice = 0; slice < 16 && session.debuggerTimer_.isActive(); ++slice)
        session.debuggerSlice();
    if (session.debuggerTimer_.isActive() || session.emulator_->gb.elapsed_cycles <= beforeStep ||
        session.musicWaveformsPublished_ != beforeDebugWave ||
        session.recorder_.get() != recorder || !session.debuggerBackstep() ||
        session.musicTrack_ != 2 || tracks->currentIndex() != 2)
        return smokeFailure(__FILE__, __LINE__);
    restartTrack->click();
    if (session.paused_ || !session.debuggerHistory_.empty() || !playFrames(2))
        return smokeFailure(__FILE__, __LINE__);

    session.setPrinterConnected(true);
    if (!session.printer_)
        return smokeFailure(__FILE__, __LINE__);
    const auto emptyPrinter = session.captureState();
    const auto printerLink = session.emulator_->gb.serial_link;
    printerLink.launch(&session.emulator_->gb, true, printerLink.user_data);
    const auto printerPrefix = session.captureState();
    if (printerPrefix == emptyPrinter)
        return smokeFailure(__FILE__, __LINE__);
    {
        uint64_t timePolls = 0;
        const auto previousPolicy = session.emulator_->gb.rtc_policy;
        cupid_gb_set_rtc_time_source(
            &session.emulator_->gb,
            [](void *context) -> int64_t {
                ++*static_cast<uint64_t *>(context);
                return 1600000000;
            },
            &timePolls);
        const auto restoreTimeSource = qScopeGuard([&] {
            cupid_gb_set_rtc_time_source(&session.emulator_->gb, previousPolicy.time_source,
                                         previousPolicy.time_user_data);
        });
        std::unique_ptr<CupidEmulator> candidate;
        CupidGbGbsInfo metadata{};
        int model = int(session.emulator_->gb.model);
        if (!session.prepareMusicCandidate(candidate, original, model, metadata))
            return smokeFailure(__FILE__, __LINE__);
        const auto discardCandidate = qScopeGuard([&] {
            cupid_gb_set_serial_link(&candidate->gb, {});
            cupid_emulator_shutdown(candidate.get());
        });
        const auto entry = candidate->gb.cpu.pc;
        // A fully prepared but abandoned Reset candidate must contain its music
        // entry state without resetting the live printer's partially received byte.
        if (!session.prepareResetCandidate(candidate.get(), ResetWorkflow::Normal, true) ||
            candidate->gb.cpu.pc != entry || candidate->gb.cpu.a != metadata.first_track ||
            session.captureState() != printerPrefix || timePolls)
            return smokeFailure(__FILE__, __LINE__);
    }

    // Rejected files and a rejected file Reset retain the active document,
    // selection, PCM owner, recorder, printer prefix, geometry, and exact guest state.
    const auto retained = session.captureState();
    const auto retainedSession = session.sessionId_;
    auto *retainedCore = session.emulator_.get();
    auto *retainedSink = session.audio_.get();
    const QSize retainedSize = session.size();
    const auto unchanged = [&] {
        return session.emulator_.get() == retainedCore && session.sessionId_ == retainedSession &&
               session.musicTrack_ == 2 && tracks->currentIndex() == 2 && session.musicInfo_ &&
               session.audio_.get() == retainedSink && session.recorder_.get() == recorder &&
               recorder->active() && session.size() == retainedSize &&
               session.captureState() == retained;
    };
    QByteArray rejected = original;
    rejected[3] = char(2);
    const QString badPath = fixtures.filePath("music-invalid.gbs");
    const QString shortPath = fixtures.filePath("music-short.gbs");
    const QString gbaPath = fixtures.filePath("music-unsupported.gba");
    if (!writeMusicFixture(badPath, rejected) ||
        !writeMusicFixture(shortPath, original.first(111)) || !writeMusicFixture(gbaPath, original))
        return smokeFailure(__FILE__, __LINE__);
    for (const auto &invalid : {badPath, shortPath, gbaPath}) {
        if (session.openRom(invalid) || !unchanged())
            return smokeFailure(__FILE__, __LINE__);
        session.failed_ = false;
    }
    if (!writeMusicFixture(path, rejected) || session.restart() || !unchanged() ||
        !writeMusicFixture(path, original))
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    const auto recordedFrames = recorder->framesWritten();
    if (!session.stopRecording() ||
        QFileInfo(recordingPath).size() != qint64(44 + recordedFrames * 4) ||
        !session.flushBattery(true) || QFileInfo::exists(session.savePath_))
        return smokeFailure(__FILE__, __LINE__);
    QFile recorded(recordingPath);
    if (!recorded.open(QIODevice::ReadOnly) || !recorded.seek(qint64(44 + recordedBefore * 4)) ||
        recorded.read(queuedPcm.size()) != queuedPcm)
        return smokeFailure(__FILE__, __LINE__);
    recorded.close();

    const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    if (!captureDirectory.isEmpty() &&
        (!QDir().mkpath(captureDirectory) ||
         !session.grab().save(QDir(captureDirectory).filePath("gbs-player.png"))))
        return smokeFailure(__FILE__, __LINE__);

    // Malformed counts remain exact. The UI has no invented track zero or
    // clamped initial selection; choosing a declared item resolves an invalid index.
    for (const auto metadata : {std::pair<uint8_t, uint8_t>{0, 0}, {3, 0}, {3, 5}}) {
        const QString malformed = fixtures.filePath(
            QString("music-count-%1-first-%2.gbs").arg(metadata.first).arg(metadata.second));
        if (!writeMusicFixture(malformed, musicFixture(metadata.first, metadata.second)) ||
            !session.openRom(malformed) || session.musicInfo_->track_count != metadata.first ||
            session.musicTrack_ != uint8_t(metadata.second - 1) ||
            tracks->count() != metadata.first || tracks->currentIndex() != -1 ||
            tracks->isEnabled() != bool(metadata.first) ||
            next->isEnabled() != bool(metadata.first) ||
            previous->isEnabled() != bool(metadata.first) || !playFrames(1))
            return smokeFailure(__FILE__, __LINE__);
        if (metadata.first) {
            next->click();
            if (session.musicTrack_ != 0 || tracks->currentIndex() != 0)
                return smokeFailure(__FILE__, __LINE__);
        }
    }

    const QString largePath = fixtures.filePath("music-capped.gbs");
    QByteArray large = original;
    large.resize(CUPID_GB_GBS_HEADER_SIZE + CUPID_GB_GBS_FILE_PAYLOAD_LIMIT + 1, char(0));
    if (!writeMusicFixture(largePath, large))
        return smokeFailure(__FILE__, __LINE__);
    const auto capped = readGameMedia(largePath, error);
    if (capped.kind != GameMediaKind::Gbs ||
        capped.data.size() != CUPID_GB_GBS_HEADER_SIZE + CUPID_GB_GBS_FILE_PAYLOAD_LIMIT ||
        !session.openRom(largePath) || session.romData_ != capped.data)
        return smokeFailure(__FILE__, __LINE__);
    const QString archivePath = fixtures.filePath("music-archive.zip");
    if (!writeMusicFixture(archivePath, musicZip({{"song.gbs", original}})) ||
        !session.openRom(archivePath) || !session.musicInfo_ || session.romData_ != original)
        return smokeFailure(__FILE__, __LINE__);
    const QString ambiguousPath = fixtures.filePath("music-ambiguous.zip");
    if (!writeMusicFixture(ambiguousPath,
                           musicZip({{"first.gbs", original}, {"second.gbs", original}})) ||
        session.openRom(ambiguousPath) ||
        session.romPath_ != QFileInfo(archivePath).absoluteFilePath())
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;

    // Automatic reads capability bytes from the mapped image, including its
    // padding, rather than indexing the 112-byte music header as a cartridge.
    MainWindow automatic(true, true, false, missingFirmware, -1, uint64_t(1));
    automatic.timer_.stop();
    for (const auto model : {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB}) {
        QByteArray bytes = musicFixture(3, 2, 0x100);
        if (model == CUPID_GB_MODEL_CGB)
            bytes[CUPID_GB_GBS_HEADER_SIZE + 0x43] = char(0x80);
        if (model == CUPID_GB_MODEL_SGB)
            bytes[CUPID_GB_GBS_HEADER_SIZE + 0x46] = char(3);
        const QString inferred = fixtures.filePath(QString("music-model-%1.gbs").arg(int(model)));
        if (!writeMusicFixture(inferred, bytes) || !automatic.openRom(inferred) ||
            automatic.emulator_->gb.model != model)
            return smokeFailure(__FILE__, __LINE__);
    }
    const QString paddedPath = fixtures.filePath("music-model-padding.gbs");
    if (!writeMusicFixture(paddedPath, original) || !automatic.openRom(paddedPath) ||
        automatic.emulator_->gb.model != CUPID_GB_MODEL_CGB)
        return smokeFailure(__FILE__, __LINE__);

    for (const auto revision : {CUPID_GB_MODEL_CGB_0, CUPID_GB_MODEL_CGB_A, CUPID_GB_MODEL_CGB_B,
                                CUPID_GB_MODEL_CGB_C, CUPID_GB_MODEL_CGB_D, CUPID_GB_MODEL_CGB}) {
        MainWindow revised(true, true, false, QStringLiteral(""), revision, uint64_t(1));
        if (!revised.openRom(path) || revised.emulator_->gb.model != revision ||
            revised.emulator_->target_system != CUPID_SYSTEM_GBC || !revised.musicInfo_ ||
            !revised.runFrame() || !revised.restart() || revised.emulator_->gb.model != revision)
            return smokeFailure(__FILE__, __LINE__);
    }

    // Music keeps the shared serial timeline and both windows' independent audio.
    if (!session.openRom(path) || !session.connectLink(&automatic))
        return smokeFailure(__FILE__, __LINE__);
    const auto cable = session.linkSession_;
    const auto peerCycles = automatic.emulator_->gb.elapsed_cycles;
    const auto peerImages = automatic.presentedImages_;
    if (!playFrames(6) || automatic.emulator_->gb.elapsed_cycles <= peerCycles ||
        automatic.presentedImages_ != peerImages || !automatic.musicWaveformsPublished_ ||
        !session.startMusicTrack(2) || session.linkSession_ != cable || !session.saveSlot(3) ||
        !session.loadSlot(3) || session.linkSession_ != cable || session.musicTrack_ != 2)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    if (!automatic.paused_)
        return smokeFailure(__FILE__, __LINE__);
    restartTrack->click();
    if (session.paused_ || automatic.paused_ || !playFrames(2))
        return smokeFailure(__FILE__, __LINE__);
    session.disconnectLink();
    if (session.linkSession_ || automatic.linkSession_ || !automatic.closeRom())
        return smokeFailure(__FILE__, __LINE__);

    // Returning to cartridges restores the previous game geometry and enables
    // its display controls. Closing music does the same before geometry is saved.
    QByteArray cartridge(32768, char(0));
    cartridge[0x100] = char(0x18);
    cartridge[0x101] = char(0xfe);
    const QString cartridgePath = fixtures.filePath("music-return.gb");
    session.bootDirectoryOverride_ = QStringLiteral("");
    if (!writeMusicFixture(cartridgePath, cartridge) || !session.openRom(cartridgePath) ||
        session.musicInfo_ || session.centralWidget() != session.display_ ||
        session.size() != gameSize || session.maximumSize() != gameMaximum ||
        !session.reloadAction_->isEnabled() || !session.hotSwapAction_->isEnabled() ||
        !session.workBoyAction_->isEnabled() || !session.screenshotAction_->isEnabled() ||
        !session.findChild<QAction *>("fullscreen")->isEnabled() || !playFrames(1) ||
        session.image_.isNull() || !session.openRom(path) || !session.closeRom() ||
        session.musicInfo_ || session.size() != gameSize || session.maximumSize() != gameMaximum ||
        session.centralWidget() != session.display_ || session.emulator_)
        return smokeFailure(__FILE__, __LINE__);

    // Transactional candidates retain prior mapper wiring through intervening
    // cartridge loads, then through music's Automatic-model preparation retry.
    automatic.bootDirectoryOverride_ = QStringLiteral("");
    QByteArray extended(0x400000, char(0));
    extended[0x147] = char(0x11);
    extended[0x148] = char(7);
    const QString extendedPath = fixtures.filePath("music-prior-mapper.gb");
    const QString historyMusicPath =
        fixtures.filePath(QString("music-model-%1.gbs").arg(int(CUPID_GB_MODEL_DMG_ABC)));
    if (!writeMusicFixture(extendedPath, extended) || !automatic.openRom(extendedPath) ||
        !automatic.emulator_->gb.mbc3_extended_history || !automatic.openRom(cartridgePath) ||
        !automatic.emulator_->gb.mbc3_extended_history ||
        cupid_gb_has_extended_mbc3(&automatic.emulator_->gb) ||
        !automatic.openRom(historyMusicPath) ||
        !cupid_gb_has_extended_mbc3(&automatic.emulator_->gb) || !automatic.closeRom())
        return smokeFailure(__FILE__, __LINE__);
    QPointer<MainWindow> child = session.newWindow();
    child->timer_.stop();
    if (!child->openRom(path) || !child->musicInfo_ || child->musicTrack_ != 1 || !child->close())
        return smokeFailure(__FILE__, __LINE__);
    QCoreApplication::sendPostedEvents(child, QEvent::DeferredDelete);
    if (child)
        return smokeFailure(__FILE__, __LINE__);

    // Sink retirement can dispatch deletion while a paused Restart is resuming.
    // Exercise the actual nested pause/audio path with a deterministic last-lease
    // callback, without requiring a usable native output device in offscreen tests.
    {
        QPointer<MainWindow> victim = new MainWindow(true, true, false, QStringLiteral(""),
                                                     CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
        const auto cleanup = qScopeGuard([&] {
            if (victim)
                delete victim.data();
        });
        victim->timer_.stop();
        if (!victim->openRom(path))
            return smokeFailure(__FILE__, __LINE__);
        victim->setPaused(true);
        bool deletedDuringResume = false;
        victim->audio_ = std::shared_ptr<QAudioSink>(
            new QAudioSink(QAudioDevice(), QAudioFormat()), [&](QAudioSink *sink) {
                delete sink;
                deletedDuringResume = victim && victim->stateOperation_ && !victim->paused_;
                delete victim.data();
            });
        auto *window = victim.data();
        if (!window->startMusicTrack(2) || victim || !deletedDuringResume)
            return smokeFailure(__FILE__, __LINE__);
    }
    // File replacement and both Reset commands retire that same stream after
    // committing the candidate. Every caller must survive a close at the boundary,
    // including nested reset guards that remain on the stack after Open returns.
    const QString replacementPath = fixtures.filePath("music-lifetime-replacement.gbs");
    if (!writeMusicFixture(replacementPath, musicFixture(2, 1)))
        return smokeFailure(__FILE__, __LINE__);
    const QString lifetimeCase = qEnvironmentVariable("CUPIDGB_GBS_LIFETIME_CASE");
    bool selectedLifetimeCase = false;
    for (const auto workflow : {ResetWorkflow::Open, ResetWorkflow::Normal, ResetWorkflow::Quick}) {
        const QString name = workflow == ResetWorkflow::Open    ? QStringLiteral("open")
                             : workflow == ResetWorkflow::Quick ? QStringLiteral("quick-reset")
                                                                : QStringLiteral("reset");
        if (!lifetimeCase.isEmpty() && lifetimeCase != name)
            continue;
        selectedLifetimeCase = true;
        QPointer<MainWindow> victim = new MainWindow(true, true, false, QStringLiteral(""),
                                                     CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
        const auto cleanup = qScopeGuard([&] {
            if (victim)
                delete victim.data();
        });
        victim->timer_.stop();
        if (!victim->openRom(path) || !victim->startMusicTrack(2))
            return smokeFailure(__FILE__, __LINE__);
        victim->setPaused(true);
        const auto previousSession = victim->sessionId_;
        const QString expectedPath =
            QFileInfo(workflow == ResetWorkflow::Open ? replacementPath : path).absoluteFilePath();
        const uint8_t expectedTrack = workflow == ResetWorkflow::Open ? 0 : 1;
        bool deletedAfterCommit = false;
        unsigned retirements = 0;
        victim->audio_ = std::shared_ptr<QAudioSink>(
            new QAudioSink(QAudioDevice(), QAudioFormat()), [&](QAudioSink *sink) {
                delete sink;
                ++retirements;
                deletedAfterCommit = victim && victim->stateOperation_ && !victim->paused_ &&
                                     victim->sessionId_ > previousSession && victim->musicInfo_ &&
                                     victim->romPath_ == expectedPath &&
                                     victim->musicTrack_ == expectedTrack &&
                                     victim->emulator_->gb.cpu.a == expectedTrack;
                delete victim.data();
            });
        auto *window = victim.data();
        qInfo("CupidGB GBS lifetime %s starting", qPrintable(name));
        const bool committed = workflow == ResetWorkflow::Open    ? window->openRom(replacementPath)
                               : workflow == ResetWorkflow::Quick ? window->quickReset()
                                                                  : window->restart();
        if (!committed || victim || !deletedAfterCommit || retirements != 1)
            return smokeFailure(__FILE__, __LINE__);
        qInfo("CupidGB GBS lifetime %s passed", qPrintable(name));
    }
    for (const QString name :
         {QStringLiteral("close"), QStringLiteral("close-window"), QStringLiteral("state-file"),
          QStringLiteral("state-slot"), QStringLiteral("console-reset")}) {
        if (!lifetimeCase.isEmpty() && lifetimeCase != name)
            continue;
        selectedLifetimeCase = true;
        const bool loading = name.startsWith("state-");
        QPointer<MainWindow> victim = new MainWindow(true, true, false, QStringLiteral(""),
                                                     CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
        const auto cleanup = qScopeGuard([&] {
            if (victim)
                delete victim.data();
        });
        victim->timer_.stop();
        if (!victim->openRom(path) || !victim->runFrame() || !victim->runFrame())
            return smokeFailure(__FILE__, __LINE__);
        if (loading &&
            (!victim->saveSlot(6) || cupid_gb_debug_peek(&victim->emulator_->gb, 0xc000) != 1))
            return smokeFailure(__FILE__, __LINE__);
        const QString nativeState = victim->slotPath(6);
        if (!victim->startMusicTrack(2) || !victim->runFrame() || !victim->runFrame() ||
            cupid_gb_debug_peek(&victim->emulator_->gb, 0xc000) != 2)
            return smokeFailure(__FILE__, __LINE__);
        victim->setPaused(true);
        const auto previousSession = victim->sessionId_;
        const auto previousTimeline = victim->timelineId_;
        bool deletedAtBoundary = false;
        unsigned retirements = 0;
        victim->audio_ = std::shared_ptr<QAudioSink>(
            new QAudioSink(QAudioDevice(), QAudioFormat()), [&](QAudioSink *sink) {
                delete sink;
                ++retirements;
                deletedAtBoundary =
                    victim && victim->stateOperation_ && victim->paused_ &&
                    victim->musicTrack_ == 2 && victim->musicInfo_ &&
                    (loading ? victim->sessionId_ == previousSession &&
                                   victim->timelineId_ > previousTimeline &&
                                   cupid_gb_debug_peek(&victim->emulator_->gb, 0xc000) == 1
                             : victim->sessionId_ > previousSession);
                delete victim.data();
            });
        auto *window = victim.data();
        qInfo("CupidGB GBS lifetime %s starting", qPrintable(name));
        bool completed = false;
        if (name == "close")
            completed = window->closeRom();
        else if (name == "close-window")
            completed = window->close();
        else if (name == "state-file")
            completed = window->loadStateFile(nativeState);
        else if (name == "state-slot")
            completed = window->loadSlot(6);
        else
            completed = window->resetConsole(CUPID_GB_RESET_HARD);
        if (!completed || victim || !deletedAtBoundary || retirements != 1)
            return smokeFailure(__FILE__, __LINE__);
        qInfo("CupidGB GBS lifetime %s passed", qPrintable(name));
    }

    // Playback can retire a stream before the next guest step, or while the
    // audio pump unwinds its deferred rate change. Neither path may use a window
    // deleted by the last sink lease, including from its scope-exit callbacks.
    for (const QString name :
         {QStringLiteral("tick-retirement"), QStringLiteral("pump-retirement")}) {
        if (!lifetimeCase.isEmpty() && lifetimeCase != name)
            continue;
        selectedLifetimeCase = true;
        QPointer<MainWindow> victim = new MainWindow(true, true, false, QStringLiteral(""),
                                                     CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
        const auto cleanup = qScopeGuard([&] {
            if (victim)
                delete victim.data();
        });
        victim->timer_.stop();
        if (!victim->openRom(path) || victim->paused_ || victim->stateOperation_ ||
            victim->linkSession_ || !victim->audioBuffer_)
            return smokeFailure(__FILE__, __LINE__);
        victim->audioBuffer_->clear();
        victim->audioPriming_ = true;
        victim->audioPrimeBytes_ = 4;
        victim->audioModeResetPending_ = true;
        const auto previousSession = victim->sessionId_;
        const auto previousCycles = victim->emulator_->gb.elapsed_cycles;
        bool deletedAtBoundary = false;
        unsigned retirements = 0;
        victim->audio_ = std::shared_ptr<QAudioSink>(
            new QAudioSink(QAudioDevice(), QAudioFormat()), [&](QAudioSink *sink) {
                delete sink;
                ++retirements;
                deletedAtBoundary = victim && victim->stateOperation_ && !victim->paused_ &&
                                    !victim->audioModeResetPending_ &&
                                    victim->sessionId_ == previousSession &&
                                    victim->emulator_->gb.elapsed_cycles == previousCycles;
                delete victim.data();
            });
        auto *window = victim.data();
        qInfo("CupidGB GBS lifetime %s starting", qPrintable(name));
        if (name == "tick-retirement")
            window->tick();
        else
            window->pumpAudio();
        if (victim || !deletedAtBoundary || retirements != 1)
            return smokeFailure(__FILE__, __LINE__);
        qInfo("CupidGB GBS lifetime %s passed", qPrintable(name));
    }

    // Error reporting owns a nested dialog loop. Deleting its parent must leave
    // neither the dialog nor a caller's operation guard touching freed storage.
    QByteArray batteryCartridge = cartridge;
    batteryCartridge[0x147] = char(3);
    batteryCartridge[0x149] = char(2);
    const QString batteryPath = fixtures.filePath("music-lifetime-battery.gb");
    if (!writeMusicFixture(batteryPath, batteryCartridge))
        return smokeFailure(__FILE__, __LINE__);
    for (const QString name :
         {QStringLiteral("error-dialog"), QStringLiteral("pause-save-error"),
          QStringLiteral("close-save-error"), QStringLiteral("slot-save-error"),
          QStringLiteral("state-load-error")}) {
        if (!lifetimeCase.isEmpty() && lifetimeCase != name)
            continue;
        selectedLifetimeCase = true;
        const bool batteryFailure = name == "pause-save-error" || name == "close-save-error";
        QPointer<MainWindow> victim = new MainWindow(true, true, false, QStringLiteral(""),
                                                     CUPID_GB_MODEL_DMG_ABC, uint64_t(1));
        const auto cleanup = qScopeGuard([&] {
            if (victim)
                delete victim.data();
        });
        victim->timer_.stop();
        if (!victim->openRom(batteryFailure ? batteryPath : path))
            return smokeFailure(__FILE__, __LINE__);
        const QString validSavePath = victim->savePath_;
        if (batteryFailure) {
            cupid_gb_write_u8(&victim->emulator_->gb, 0x0000, 0x0a);
            cupid_gb_write_u8(&victim->emulator_->gb, 0xa000,
                              uint8_t(cupid_gb_debug_peek(&victim->emulator_->gb, 0xa000) ^ 0xff));
            victim->watchBattery();
            if (!victim->batteryDirty_)
                return smokeFailure(__FILE__, __LINE__);
            victim->saveTimer_.stop();
            victim->savePath_ = fixtures.path();
        } else if (name == "slot-save-error") {
            victim->savePath_ = fixtures.filePath("missing-state-folder/music.sav");
        }
        victim->isolated_ = false;
        victim->automated_ = false;
        bool dialogSeen = false, guarded = false, timedOut = false;
        QElapsedTimer deadline;
        deadline.start();
        QTimer callback;
        callback.setInterval(1);
        connect(&callback, &QTimer::timeout, &callback, [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (deadline.elapsed() > 2000) {
                timedOut = true;
                callback.stop();
                if (dialog)
                    dialog->reject();
                return;
            }
            if (!dialog || dialog->parentWidget() != victim)
                return;
            const QString expected = batteryFailure              ? "Cannot save cartridge RAM:"
                                     : name == "slot-save-error" ? "Cannot save state:"
                                     : name == "state-load-error"
                                         ? "Cannot load emulator state:"
                                         : "The music file could not be loaded:";
            dialogSeen =
                dialog->text().startsWith(expected) && dialog->icon() == QMessageBox::Critical;
            guarded = victim->stateOperation_;
            // The destructor still exports a real battery. Restore its valid
            // destination before deletion so the intended error cannot recurse.
            victim->savePath_ = validSavePath;
            callback.stop();
            delete victim.data();
        });
        callback.start();
        auto *window = victim.data();
        qInfo("CupidGB GBS lifetime %s starting", qPrintable(name));
        bool rejected = true;
        if (name == "pause-save-error")
            window->setPaused(true);
        else if (name == "close-save-error")
            rejected = !window->closeRom();
        else if (name == "slot-save-error")
            rejected = !window->saveSlot(7);
        else if (name == "state-load-error")
            rejected = !window->loadStateFile(badPath);
        else
            rejected = !window->openRom(badPath);
        if (!rejected || victim || !dialogSeen || !guarded || timedOut)
            return smokeFailure(__FILE__, __LINE__);
        qInfo("CupidGB GBS lifetime %s passed", qPrintable(name));
    }
    if (!selectedLifetimeCase)
        return smokeFailure(__FILE__, __LINE__);
    qInfo("CupidGB GBS music workflows passed");
    return !session.failed_ && !automatic.failed_;
}

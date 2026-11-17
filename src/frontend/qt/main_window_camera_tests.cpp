/**
 * @file    main_window_camera_tests.cpp
 * @brief   Native Qt camera transactions tested with an injected frame source.
 * @author  cupidthecat
 * @date    2026-11-17
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

#include "host_camera.h"
#include "main_window_internal.h"

#include "cupid/gb/camera.h"

#include <QCoreApplication>
#include <QScopeGuard>

namespace {
QVideoFrame cameraTestFrame(uint8_t gray) {
    QVideoFrame frame(QVideoFrameFormat(QSize(130, 114), QVideoFrameFormat::Format_Y8));
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};
    for (int y = 0; y < frame.height(); ++y)
        std::memset(frame.bits(0) + y * frame.bytesPerLine(0), gray, size_t(frame.width()));
    frame.unmap();
    return frame;
}
} // namespace

bool MainWindow::cameraSmokeTest(const QString &directory) {
    const bool timerRunning = timer_.isActive();
    timer_.stop();
    const auto timerRestore = qScopeGuard([this, timerRunning] {
        if (timerRunning)
            timer_.start();
    });
    QByteArray rom(32768, char(0));
    rom[0x100] = char(0x18);
    rom[0x101] = char(0xfe);
    rom[0x147] = char(0xfc);
    rom[0x149] = 4;
    std::memcpy(rom.data() + 0x134, "CAMERA QT", 9);
    uint8_t checksum = 0;
    for (unsigned address = 0x134; address <= 0x14c; ++address)
        checksum = uint8_t(checksum - uint8_t(rom[address]) - 1u);
    rom[0x14d] = char(checksum);
    const QString path = QDir(directory).filePath("camera-workflow.gb");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(rom) != rom.size())
        return smokeFailure(__FILE__, __LINE__);
    file.close();
    if (!openRom(path))
        return smokeFailure(__FILE__, __LINE__);

    std::vector<HostCamera::FrameReady> replies;
    unsigned cancellations = 0, staleResults = 0;
    const auto cameraCleanup = qScopeGuard([this] {
        cancelCameraCapture();
        hostCamera_.reset();
    });
    const auto inject = [&] {
        hostCamera_ = std::make_unique<HostCamera>(
            this, [&replies](HostCamera::FrameReady reply) { replies.push_back(std::move(reply)); },
            [&cancellations] { ++cancellations; });
    };
    inject();
    const auto imageEquals = [this](uint8_t gray) {
        return emulator_ && std::all_of(std::begin(emulator_->gb.camera_host.image),
                                        std::end(emulator_->gb.camera_host.image),
                                        [gray](uint8_t value) { return value == gray; });
    };
    const auto begin = [this, &replies](bool dispatch = true) {
        const size_t count = replies.size();
        auto &gb = emulator_->gb;
        cupid_gb_write_u8(&gb, 0x4000, 0x10);
        cupid_gb_write_u8(&gb, 0xa000, 1);
        const uint64_t request = gb.camera_host.pending_request;
        if (!request || replies.size() != count)
            return uint64_t(0);
        if (!dispatch)
            return request;
        // Only the queued dispatch may enter the injected source.
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
        return replies.size() == count + 1 ? request : uint64_t(0);
    };
    const auto finish = [&](uint8_t gray) {
        replies.back()(cameraTestFrame(gray), {});
        return !emulator_->gb.camera_host.pending_request && imageEquals(gray);
    };

    if (!begin() || !finish(0x31))
        return smokeFailure(__FILE__, __LINE__);
    const uint64_t savedRequest = begin();
    if (!savedRequest)
        return smokeFailure(__FILE__, __LINE__);
    const auto saved = captureState();
    const auto savedHost = emulator_->gb.camera_host;
    if (saved.isEmpty() || uint8_t(saved[8]) != CUPID_GB_STATE_VERSION || !saveSlot(1) ||
        std::memcmp(&savedHost, &emulator_->gb.camera_host, sizeof(savedHost)))
        return smokeFailure(__FILE__, __LINE__);
    const auto oldStateReply = replies.back();
    auto invalid = saved;
    invalid[0] ^= 1;
    if (restoreState(invalid, false) ||
        std::memcmp(&savedHost, &emulator_->gb.camera_host, sizeof(savedHost)))
        return smokeFailure(__FILE__, __LINE__);
    if (!loadSlot(1) || emulator_->gb.camera_host.pending_request || !imageEquals(0x31) ||
        emulator_->gb.camera_host.request_sequence != savedRequest)
        return smokeFailure(__FILE__, __LINE__);
    oldStateReply(cameraTestFrame(0xe1), {});
    ++staleResults;
    if (!imageEquals(0x31) || !begin() || !finish(0x42))
        return smokeFailure(__FILE__, __LINE__);

    const uint64_t failedRequest = begin();
    const auto failedHost = emulator_->gb.camera_host;
    HostCamera *const sourceBeforeFailure = hostCamera_.get();
    if (!failedRequest || resetConsole(static_cast<CupidGbResetKind>(99)) ||
        std::memcmp(&failedHost, &emulator_->gb.camera_host, sizeof(failedHost)) ||
        hostCamera_.get() != sourceBeforeFailure)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false;
    if (openRom(QDir(directory).filePath("missing-camera-cartridge.gb")) ||
        std::memcmp(&failedHost, &emulator_->gb.camera_host, sizeof(failedHost)) ||
        hostCamera_.get() != sourceBeforeFailure)
        return smokeFailure(__FILE__, __LINE__);
    failed_ = false;
    if (!finish(0x53))
        return smokeFailure(__FILE__, __LINE__);

    for (unsigned workflow = 0; workflow < 3; ++workflow) {
        const uint64_t request = begin();
        const auto obsolete = replies.back();
        HostCamera *const previousSource = hostCamera_.get();
        CupidGb *const previousMachine = &emulator_->gb;
        const bool reset = workflow == 0 ? restart() : workflow == 1 ? quickReset() : reloadRom();
        if (!request || !reset || &emulator_->gb == previousMachine ||
            hostCamera_.get() != previousSource || emulator_->gb.camera_host.pending_request ||
            emulator_->gb.camera_host.request_sequence != request || !imageEquals(0x53))
            return smokeFailure(__FILE__, __LINE__);
        obsolete(cameraTestFrame(0xe2), {});
        ++staleResults;
        if (!imageEquals(0x53))
            return smokeFailure(__FILE__, __LINE__);
    }

    // The same receiving image survives an in-place reset and rewind. Rewind
    // snapshots never claim to carry a history of live camera frames.
    uint64_t request = begin();
    auto obsolete = replies.back();
    if (!request || !resetConsole(CUPID_GB_RESET_QUICK) ||
        emulator_->gb.camera_host.pending_request || !imageEquals(0x53))
        return smokeFailure(__FILE__, __LINE__);
    obsolete(cameraTestFrame(0xe3), {});
    ++staleResults;
    if (!imageEquals(0x53))
        return smokeFailure(__FILE__, __LINE__);
    setPaused(false);
    resetRewind();
    if (!runFrame() || !begin() || !finish(0x64) || !runFrame() || !runFrame())
        return smokeFailure(__FILE__, __LINE__);
    request = begin();
    obsolete = replies.back();
    if (!request || !rewindFrame() || emulator_->gb.camera_host.pending_request ||
        !imageEquals(0x64))
        return smokeFailure(__FILE__, __LINE__);
    obsolete(cameraTestFrame(0xe4), {});
    ++staleResults;
    if (!imageEquals(0x64))
        return smokeFailure(__FILE__, __LINE__);

    // A completed device error clears busy but preserves the accepted image.
    if (!begin())
        return smokeFailure(__FILE__, __LINE__);
    replies.back()({}, QStringLiteral("injected camera unavailable"));
    if (emulator_->gb.camera_host.pending_request || !imageEquals(0x64))
        return smokeFailure(__FILE__, __LINE__);

    // Rejected tool edits preserve the live request. Accepted edits invalidate
    // its timeline and release the busy latch even before queued dispatch.
    ensureDebugger();
    request = begin();
    const uint64_t rejectedTimeline = timelineId_;
    const unsigned beforeRejectedCancellations = cancellations;
    if (!request || debuggerWrite(0x0100, 0) || writeSearchMemory([](CupidGb &) { return false; }))
        return smokeFailure(__FILE__, __LINE__);
    debuggerCommand("set a $100");
    if (timelineId_ != rejectedTimeline || cancellations != beforeRejectedCancellations ||
        emulator_->gb.camera_host.pending_request != request || !finish(0x64))
        return smokeFailure(__FILE__, __LINE__);
    const std::function<bool()> mutations[] = {
        [this] { return debuggerWrite(0xc000, 0x69); },
        [this] {
            debuggerCommand("set a $57");
            return emulator_->gb.cpu.a == 0x57;
        },
        [this] {
            return writeSearchMemory(
                [](CupidGb &gb) { return cupid_gb_debug_poke(&gb, 0xc001, 0x7a); });
        },
        [this] { return rememberDebuggerStep() && debuggerBackstep(); }};
    unsigned timelineMutations = 0;
    for (const auto &mutate : mutations) {
        for (bool dispatch : {false, true}) {
            const size_t queuedBefore = replies.size();
            const uint64_t pending = begin(dispatch);
            const auto oldReply = dispatch ? replies.back() : HostCamera::FrameReady{};
            const uint64_t previousTimeline = timelineId_;
            const unsigned previousCancellations = cancellations;
            if (!pending || !mutate() || timelineId_ != previousTimeline + 1 ||
                cancellations <= previousCancellations ||
                emulator_->gb.camera_host.pending_request ||
                (emulator_->gb.camera.registers[0] & 1u) || emulator_->gb.camera.busy_tcycles ||
                emulator_->gb.camera_host.request_sequence != pending || !imageEquals(0x64))
                return smokeFailure(__FILE__, __LINE__);
            if (dispatch) {
                oldReply(cameraTestFrame(0xee), {});
                ++staleResults;
            } else {
                QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
                if (replies.size() != queuedBefore)
                    return smokeFailure(__FILE__, __LINE__);
            }
            if (!imageEquals(0x64) || begin() <= pending || !finish(0x64))
                return smokeFailure(__FILE__, __LINE__);
            ++timelineMutations;
        }
    }

    request = begin();
    obsolete = replies.back();
    if (!request || !openRom(path) || hostCamera_ || !imageEquals(0))
        return smokeFailure(__FILE__, __LINE__);
    obsolete(cameraTestFrame(0xe5), {});
    ++staleResults;
    if (!imageEquals(0) || emulator_->gb.camera_host.request_sequence != request)
        return smokeFailure(__FILE__, __LINE__);
    inject();
    if (!begin() || !finish(0x75))
        return smokeFailure(__FILE__, __LINE__);
    request = begin();
    obsolete = replies.back();
    if (!request || !closeRom() || emulator_ || hostCamera_)
        return smokeFailure(__FILE__, __LINE__);
    obsolete(cameraTestFrame(0xe6), {});
    ++staleResults;
    if (!openRom(path) || !imageEquals(0))
        return smokeFailure(__FILE__, __LINE__);
    inject();
    if (!begin() || !finish(0x86))
        return smokeFailure(__FILE__, __LINE__);
    // A response from the destroyed owner cannot overwrite a later instance,
    // even when its fresh core sequence starts again at the same token.
    obsolete(cameraTestFrame(0xe7), {});
    ++staleResults;
    if (!imageEquals(0x86))
        return smokeFailure(__FILE__, __LINE__);

    const QString evidenceDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
    if (!evidenceDirectory.isEmpty()) {
        const QJsonObject proof{{"injected_source", true},
                                {"physical_camera_opened", false},
                                {"requests", int(replies.size())},
                                {"stale_results_ignored", int(staleResults)},
                                {"cancellations", int(cancellations)},
                                {"queued_guest_dispatch", true},
                                {"pending_save_is_pure", true},
                                {"state_version", int(CUPID_GB_STATE_VERSION)},
                                {"failed_transactions_preserve_request", true},
                                {"tool_timeline_mutations", int(timelineMutations)},
                                {"failed_tool_edits_preserve_request", true},
                                {"reset_reload_rewind_retain_image", true},
                                {"open_starts_black", true},
                                {"close_rejects_old_owner", true}};
        QFile evidence(QDir(evidenceDirectory).filePath("camera-workflows.json"));
        const QByteArray bytes = QJsonDocument(proof).toJson();
        if (!evidence.open(QIODevice::WriteOnly) || evidence.write(bytes) != bytes.size())
            return smokeFailure(__FILE__, __LINE__);
    }
    return closeRom();
}

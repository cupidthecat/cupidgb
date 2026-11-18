/**
 * @file    main_window_link.cpp
 * @brief   Linked cartridge windows, shared guest scheduling, and cable lifecycle.
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

#include "main_window_internal.h"

#include <QScopeGuard>
#include <utility>

extern "C" {
#include "cupid/gb/link.h"
}

namespace {
QList<QPointer<MainWindow>> cartridgeWindows;

QString storageIdentity(const QString &path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty())
        return canonical;
    const QString directory = info.dir().canonicalPath();
    return directory.isEmpty() ? QDir::cleanPath(info.absoluteFilePath())
                               : QDir(directory).filePath(info.fileName());
}

bool sameStoragePath(const QString &left, const QString &right) {
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    return !left.isEmpty() && !right.isEmpty() &&
           storageIdentity(left).compare(storageIdentity(right), pathCase) == 0;
}
} // namespace

struct MainWindow::LinkSession {
    std::array<QPointer<MainWindow>, 2> windows;
    CupidGbLinkPair pair{};
    uint64_t generation = 0;
    bool ticking = false;
    bool executing = false;
    bool changingPause = false;
    bool desiredPause = false;

    struct RecordingFailure {
        QString message;
        WaveRecorder *recorder = nullptr;
    };

    std::array<RecordingFailure, 2> recordingFailures;

    bool connected() const {
        return windows[0] && windows[1] && windows[0]->linkSession_.get() == this &&
               windows[1]->linkSession_.get() == this && windows[0]->emulator_ &&
               windows[1]->emulator_;
    }

    bool blocked() const {
        if (!connected() || changingPause)
            return true;
        for (const auto &window : windows)
            if (window->paused_ || window->stateOperation_ || window->menuDepth_ ||
                window->menuInputSuspended_)
                return true;
        return false;
    }

    void retainHostBits() const {
        for (size_t side = 0; side < windows.size(); ++side)
            if (windows[side])
                windows[side]->outgoingLinkBit_ = pair.outgoing[side];
    }

    void reportRecordingFailures() {
        for (size_t side = 0; side < windows.size(); ++side) {
            const RecordingFailure failure = std::exchange(recordingFailures[side], {});
            const auto window = windows[side];
            if (!window || !failure.recorder)
                continue;
            // Finalizing may report its own write error and dispatch close or
            // reload. Both peers' generated prefixes have already been committed.
            if (window->recorder_.get() == failure.recorder && !window->stopRecording())
                continue;
            if (window)
                window->reportError("Audio recording stopped: " + failure.message);
        }
    }
};

void MainWindow::registerLinkWindow() {
    cartridgeWindows.append(this);
}

void MainWindow::unregisterLinkWindow() {
    linkWindowClosing_ = true;
    disconnectLink();
    cartridgeWindows.removeAll(this);
    cartridgeWindows.removeAll(nullptr);
}

void MainWindow::synchronizeInputBindings() {
    settings_.sync();
    for (const auto &window : cartridgeWindows) {
        if (!window)
            continue;
        window->settings_.sync();
        for (size_t button = 0; button < window->keys_.size(); ++button) {
            window->keys_[button] =
                window->settings_.value(QString("input/%1").arg(button), defaultKeys[button])
                    .toInt();
            window->linkedKeys_[button] =
                window->settings_.value(QString("input/linkPlayer2/%1").arg(button), 0).toInt();
        }
        for (size_t action = 0; action < window->specialKeys_.size(); ++action)
            window->specialKeys_[action] =
                window->settings_
                    .value(QString("input/special/%1").arg(action), defaultSpecialKeys[action])
                    .toInt();
        for (auto *action : window->shortcutActions_) {
            const auto stored = window->settings_.value("shortcuts/" + action->objectName());
            if (stored.isValid())
                action->setShortcut(
                    QKeySequence::fromString(stored.toString(), QKeySequence::PortableText));
        }
        window->releaseLinkedInput();
        window->releaseKeys(false);
    }
}

void MainWindow::applyControllerConfiguration(
    const cupid::input::ControllerConfiguration &configuration,
    const QMap<SDL_JoystickID, int> &players, SDL_JoystickID preferred) {
    settings_.setValue("controllers/configuration", configuration.save());
    settings_.sync();
    for (const auto &window : cartridgeWindows) {
        if (!window || !window->controllers_)
            continue;
        window->releaseLinkedInput();
        window->controllers_->configure(configuration, players);
        window->controllers_->setPreferredInstance(preferred);
        window->refreshControllerGate();
    }
}

MainWindow *MainWindow::newWindow() {
    auto *window = new MainWindow(isolated_, automated_, audioCheck_, bootDirectoryOverride_,
                                  modelOverride_, powerOnSeedOverride_);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->move(pos() + QPoint(32, 32));
    window->show();
    return window;
}

bool MainWindow::storageAvailable(const QString &rom, const QString &battery) {
    for (const auto &window : cartridgeWindows) {
        if (!window || window == this || !window->emulator_)
            continue;
        if (!sameStoragePath(rom, window->romPath_) &&
            !sameStoragePath(battery, window->savePath_) &&
            !sameStoragePath(rom, window->savePath_) && !sameStoragePath(battery, window->romPath_))
            continue;
        window->show();
        window->raise();
        window->activateWindow();
        reportError("This cartridge or its save storage is already open in another window. "
                    "Use a separate cartridge copy in another folder for another player.");
        return false;
    }
    return true;
}

void MainWindow::refreshLinkMenu() {
    if (!linkMenu_)
        return;
    linkMenu_->clear();
    for (const auto &window : cartridgeWindows) {
        if (!window || window == this || !window->emulator_ || window->linkWindowClosing_)
            continue;
        auto *action = linkMenu_->addAction(QFileInfo(window->romPath_).fileName());
        action->setToolTip(window->romPath_);
        action->setCheckable(true);
        action->setChecked(window == linkPartner());
        const QPointer<MainWindow> partner = window;
        connect(action, &QAction::triggered, this, [this, partner] {
            if (partner)
                connectLink(partner);
        });
    }
    if (linkMenu_->isEmpty()) {
        auto *empty = linkMenu_->addAction("Open a cartridge in another window");
        empty->setEnabled(false);
    }
}

MainWindow *MainWindow::linkPartner() const {
    if (!linkSession_)
        return nullptr;
    return linkSession_->windows[linkSession_->windows[0] == this ? 1 : 0];
}

MainWindow *MainWindow::linkOwner() const {
    return linkSession_ ? linkSession_->windows[0].data() : nullptr;
}

bool MainWindow::connectLink(MainWindow *partner) {
    if (!partner || partner == this || !emulator_ || !partner->emulator_ || linkWindowClosing_ ||
        partner->linkWindowClosing_)
        return false;
    if (partner == linkPartner())
        return true;
    if (!storageAvailable(romPath_, savePath_) ||
        !partner->storageAvailable(partner->romPath_, partner->savePath_))
        return false;
    const bool wasPaused = paused_;
    const QPointer<MainWindow> guardedPartner = partner;
    QScopedValueRollback<bool> ownOperation(stateOperation_, true);
    const bool peerOperation = partner->stateOperation_;
    partner->stateOperation_ = true;
    const auto restorePeerOperation = qScopeGuard([guardedPartner, peerOperation] {
        if (guardedPartner)
            guardedPartner->stateOperation_ = peerOperation;
    });
    disconnectLink();
    if (!guardedPartner || !emulator_ || !guardedPartner->emulator_)
        return false;
    partner->disconnectLink();
    if (!guardedPartner || !emulator_ || !guardedPartner->emulator_)
        return false;
    setPrinterConnected(false);
    partner->setPrinterConnected(false);
    auto session = std::make_shared<LinkSession>();
    session->windows = {this, partner};
    // A launched bit belongs to its window's cable endpoint. Resets, state loads
    // and reconnecting that window retain it independently of the guest SB byte.
    session->pair.outgoing[0] = outgoingLinkBit_;
    session->pair.outgoing[1] = partner->outgoingLinkBit_;
    if (!cupid_gb_link_attach(&session->pair, &emulator_->gb, &partner->emulator_->gb)) {
        reportError("The link cable could not connect. Disconnect other serial accessories first.");
        return false;
    }
    linkSession_ = partner->linkSession_ = session;
    for (const auto &window : session->windows) {
        window->linkedInput_.fill(0xff);
        window->controllerPlayerCount_ = 2;
        window->resetRewind();
        window->resetDebuggerTimeline();
        window->disconnectAction_->setChecked(false);
        window->printerAction_->setChecked(false);
        window->statusBar()->showMessage(
            "Link cable connected. Pause and playback speed apply to both games.", 5000);
    }
    setLinkPaused(wasPaused, false);
    for (const auto &window : session->windows)
        if (window && window->linkSession_ == session)
            window->applyHeldButtons();
    return true;
}

void MainWindow::disconnectLink() {
    const auto session = linkSession_;
    if (!session)
        return;
    const auto windows = session->windows;
    session->retainHostBits();
    for (const auto &window : windows)
        if (window)
            window->releaseLinkedInput();
    cupid_gb_link_detach(&session->pair);
    ++session->generation;
    for (const auto &window : windows)
        if (window && window->linkSession_ == session)
            window->linkSession_.reset();
    // Clear both associations before any native audio call can dispatch events.
    for (const auto &window : windows) {
        if (!window || window->linkWindowClosing_ || window->linkSession_)
            continue;
        window->disconnectAction_->setChecked(true);
        window->turbo_ = false;
        window->keyboardSpecialHeld_[0] = false;
        window->controllerOutput_.held[0] = false;
        window->linkedInput_.fill(0xff);
        window->controllerPlayerCount_ = window->inputPlayerCount();
        window->releaseKeys(false);
        window->resetRewind();
        window->rememberFrame();
        window->resetDebuggerTimeline();
        window->resetPacing();
        window->clearAudio();
        if (!window || window->linkSession_)
            continue;
        window->statusBar()->showMessage("Link cable disconnected", 3000);
        window->refreshActions();
    }
}

bool MainWindow::replaceLinkEndpoint(CupidGb *replacement) {
    const auto session = linkSession_;
    if (!session)
        return true;
    const unsigned side = session->windows[0] == this ? 0 : 1;
    if (!session->connected() || !cupid_gb_link_replace(&session->pair, side, replacement)) {
        reportError("The linked cartridge could not be replaced.");
        return false;
    }
    ++session->generation;
    session->retainHostBits();
    return true;
}

void MainWindow::rebaseLink() {
    const auto session = linkSession_;
    if (!session || !session->connected())
        return;
    cupid_gb_link_rebase(&session->pair);
    ++session->generation;
    session->retainHostBits();
    if (auto *owner = linkOwner())
        owner->resetPacing();
}

void MainWindow::setLinkPaused(bool paused, bool retainInput) {
    const auto session = linkSession_;
    if (!session || !session->connected())
        return;
    session->desiredPause = paused;
    if (session->changingPause)
        return;
    QScopedValueRollback<bool> changing(session->changingPause, true);
    // Native stream changes can deliver another pause request. Finish the
    // current pair transaction, then apply the most recent requested state.
    do {
        paused = session->desiredPause;
        for (const auto &window : session->windows) {
            if (!session->connected())
                return;
            window->setPausedLocal(paused, retainInput);
        }
    } while (session->connected() && session->desiredPause != paused);
}

bool MainWindow::playbackTurbo() const {
    const auto *partner = linkPartner();
    return turbo_ || (partner && partner->turbo_);
}

unsigned MainWindow::playbackSpeed() const {
    const auto *partner = linkPartner();
    return partner ? std::min(speedSteps_, partner->speedSteps_) : speedSteps_;
}

unsigned MainWindow::playbackTurboCap() const {
    const auto *owner = linkOwner();
    return owner ? owner->turboCapQuarters_ : turboCapQuarters_;
}

bool MainWindow::deferLinkRecordingError(const QString &message) {
    const auto session = linkSession_;
    if (!session || !session->executing)
        return false;
    const unsigned side = session->windows[0] == this ? 0 : 1;
    session->recordingFailures[side] = {message, recorder_.get()};
    return true;
}

void MainWindow::tickLink() {
    const auto session = linkSession_;
    if (!session || linkOwner() != this || session->ticking || session->executing)
        return;
    QScopedValueRollback<bool> ticking(session->ticking, true);
    const auto refreshIntervals = qScopeGuard([session] {
        for (const auto &window : session->windows)
            if (window)
                window->refreshTimerInterval();
    });
    for (const auto &window : session->windows)
        if (window)
            window->refreshControllerGate();
    if (session->blocked())
        return;
    const uint64_t generation = session->generation;
    const uint64_t pacing = pacingGeneration_;
    for (const auto &window : session->windows) {
        if (window->audioModeResetPending_)
            window->clearAudio();
        if (session->blocked() || session->generation != generation)
            return;
        window->pumpAudio();
        if (session->blocked() || session->generation != generation)
            return;
    }
    if (pacingGeneration_ != pacing)
        return;
    const bool turbo = playbackTurbo();
    const unsigned cap = turbo ? playbackTurboCap() : GuestClockPacer::normalTurboQuarters;
    const unsigned speed = playbackSpeed();
    const qint64 now = clock_.nsecsElapsed();
    if (!(turbo && !cap) && now < nextFrame_)
        return;
    for (const auto &window : session->windows)
        if (window->audio_ && !turbo &&
            window->audioBuffer_->bytesAvailable() > window->audioQueueLimit_)
            return;
    PlaybackResult result;
    if (!runLinkedFrame(&result) || session->blocked() || session->generation != generation ||
        pacingGeneration_ != pacing || playbackTurbo() != turbo ||
        (turbo && playbackTurboCap() != cap))
        return;
    if (turbo && !cap) {
        nextFrame_ = now;
        clockPacer_.reset();
        return;
    }
    const auto interval = clockPacer_.advance(result.presentationCycles, speed,
                                              cupid_gb_base_clock_hz(&emulator_->gb), cap);
    if (!interval || nextFrame_ > std::numeric_limits<qint64>::max() - *interval) {
        setPaused(true);
        reportError("Linked host pacing duration could not be computed.");
        return;
    }
    nextFrame_ += *interval;
    const qint64 nominalInterval = nominalFrameNanoseconds(&emulator_->gb, speed, cap);
    if (now - nextFrame_ > nominalInterval * 4) {
        nextFrame_ = now;
        clockPacer_.reset();
    }
}

bool MainWindow::runLinkedFrame(PlaybackResult *result) {
    const auto session = linkSession_;
    if (!session || !session->connected() || session->executing)
        return false;
    QScopedValueRollback<bool> executing(session->executing, true);
    const auto reportPendingErrors = qScopeGuard([session] { session->reportRecordingFailures(); });
    const auto windows = session->windows;
    const uint64_t generation = session->generation;
    const unsigned caller = windows[0] == this ? 0 : 1;
    std::array<PlaybackWork, 2> work;
    for (size_t side = 0; side < windows.size(); ++side)
        if (!windows[side]->beginPlayback(Playback::Forward, work[side]) || !session->connected() ||
            session->generation != generation)
            return false;
    for (unsigned count = 0; count < 160000; ++count) {
        const int side = cupid_gb_link_next(&session->pair);
        if (side < 0) {
            work[caller].executionFailed = true;
            break;
        }
        MainWindow *window = windows[size_t(side)];
        auto &current = work[size_t(side)];
        if (!window->preparePlaybackStep(current))
            break;
        const bool executed = cupid_emulator_step(window->emulator_.get());
        const bool accounted = cupid_gb_link_advance(&session->pair, unsigned(side));
        if (!window->completePlaybackStep(current, executed && accounted))
            break;
        if (window->emulator_->gb.frame_ready ||
            cupid_gb_presentation_cycles(&window->emulator_->gb) - current.startPresentation >=
                70224)
            break;
        if (count == 159999)
            current.executionFailed = true;
    }
    session->retainHostBits();
    // Commit both prefixes before widget publication or error dialogs can
    // dispatch a close, reset or load. Recording errors are deferred until then.
    for (size_t side = 0; side < windows.size(); ++side) {
        auto *window = windows[side].data();
        auto &current = work[side];
        if (!current.collectionFailed)
            current.collectionFailed = !window->collectAudio(current.collected);
        window->recordAudio(current.collected);
        const bool stopping =
            current.collectionFailed || current.executionFailed || !current.debuggerStop.isEmpty();
        if (!stopping && window->audio_ && window->audioConverter_ && !window->paused_) {
            const QByteArray converted = window->convertPlaybackAudio(
                current.collected.samples(), current.collected.frames(), window->audio_->format());
            if (!converted.isEmpty())
                window->audioBuffer_->append(converted.constData(), converted.size());
        }
    }
    bool complete = true;
    for (unsigned side = 0; side < windows.size(); ++side) {
        if (!session->connected() || session->generation != generation || !windows[side] ||
            windows[side]->sessionId_ != work[side].session ||
            windows[side]->timelineId_ != work[side].timeline)
            return false;
        complete = windows[side]->finishPlayback(work[side], side == caller ? result : nullptr,
                                                 false, false) &&
                   complete;
    }
    session->reportRecordingFailures();
    for (size_t side = 0; side < windows.size(); ++side) {
        if (!session->connected() || session->generation != generation)
            return complete;
        auto *window = windows[side].data();
        const auto &current = work[side];
        if (current.collectionFailed || current.executionFailed) {
            window->setPaused(true);
            window->reportError(
                current.collectionFailed
                    ? "Audio output could not be preserved. Emulation is paused."
                    : "CPU execution stopped. The cartridge remains open for inspection.");
            return false;
        }
        if (!current.debuggerStop.isEmpty()) {
            window->stopDebugger(current.debuggerStop);
            return complete;
        }
    }
    return complete;
}

void MainWindow::linkedDebuggerSlice() {
    const auto session = linkSession_;
    if (!session || !session->connected() || session->executing || !debugger_)
        return;
    for (const auto &window : session->windows)
        if (window->stateOperation_ || window->menuDepth_ || window->menuInputSuspended_)
            return;
    QScopedValueRollback<bool> executing(session->executing, true);
    const auto reportPendingErrors = qScopeGuard([session] { session->reportRecordingFailures(); });
    const auto windows = session->windows;
    const unsigned target = windows[0] == this ? 0 : 1;
    const uint64_t generation = session->generation;
    std::array<PlaybackWork, 2> work;
    for (size_t side = 0; side < windows.size(); ++side)
        if (!windows[side]->beginPlayback(Playback::Forward, work[side]) || !session->connected() ||
            session->generation != generation)
            return;
    QElapsedTimer slice;
    slice.start();
    for (unsigned count = 0; count < 256 && slice.elapsed() < 4; ++count) {
        const int chosen = cupid_gb_link_next(&session->pair);
        if (chosen < 0) {
            work[target].executionFailed = true;
            break;
        }
        const unsigned side = unsigned(chosen);
        auto *window = windows[side].data();
        auto &current = work[side];
        if (side != target) {
            if (!window->preparePlaybackStep(current))
                break;
            const bool executed = cupid_emulator_step(window->emulator_.get());
            const bool accounted = cupid_gb_link_advance(&session->pair, side);
            if (!window->completePlaybackStep(current, executed && accounted))
                break;
        } else {
            if (CoreAudioCollector::needsDrain(emulator_->gb) && !collectAudio(current.collected)) {
                current.collectionFailed = true;
                break;
            }
            const bool held = cupid_gb_sgb_startup_held(&emulator_->gb);
            const uint64_t previousSteps = debugger_->steps;
            const auto stopped = cupid_gb_debugger_step(debugger_.get(), &emulator_->gb);
            const bool conditionError =
                !held && stopped == CUPID_GB_DEBUG_STOP_ERROR && debugger_->steps == previousSteps;
            const bool attempted = stopped != CUPID_GB_DEBUG_STOP_BREAKPOINT &&
                                   stopped != CUPID_GB_DEBUG_STOP_NO_ROM && !conditionError;
            if (attempted) {
                current.attemptedHardware = true;
                current.attemptedGuest |= !held;
                if (!cupid_gb_link_advance(&session->pair, side)) {
                    current.executionFailed = true;
                    break;
                }
            }
            debuggerCommandSteps_ += debugger_->steps - previousSteps;
            if (inputPlayerCount() != controllerPlayerCount_)
                refreshControllerPlayerCount();
            if (stopped != CUPID_GB_DEBUG_STOP_NONE) {
                current.debuggerStop =
                    stopped == CUPID_GB_DEBUG_STOP_WATCHPOINT ||
                            (!held && stopped == CUPID_GB_DEBUG_STOP_ERROR && !conditionError &&
                             debugger_->last_watch_hit.id)
                        ? debuggerWatchMessage(stopped == CUPID_GB_DEBUG_STOP_ERROR)
                    : conditionError ? QStringLiteral("Breakpoint condition evaluation failed.")
                                     : QString::fromUtf8(cupid_gb_debugger_stop_name(stopped));
                break;
            }
            if (debuggerCommandSteps_ >= 1000000) {
                current.debuggerStop =
                    "Debugger instruction budget exhausted before the target was reached.";
                break;
            }
        }
    }
    session->retainHostBits();
    for (size_t side = 0; side < windows.size(); ++side) {
        if (!session->connected() || session->generation != generation)
            return;
        auto *window = windows[side].data();
        auto &current = work[side];
        if (!current.collectionFailed)
            current.collectionFailed = !window->collectAudio(current.collected);
        window->recordAudio(current.collected);
    }
    session->reportRecordingFailures();
    for (size_t side = 0; side < windows.size(); ++side) {
        if (!session->connected() || session->generation != generation)
            return;
        auto *window = windows[side].data();
        const auto &current = work[side];
        window->resetControllerRumble();
        window->present();
        window->watchBattery();
        window->refreshDebugger();
        if (current.collectionFailed || current.executionFailed) {
            window->stopDebugger("Linked execution stopped before the debugger target.");
            return;
        }
        if (!current.debuggerStop.isEmpty()) {
            window->stopDebugger(current.debuggerStop);
            return;
        }
    }
}

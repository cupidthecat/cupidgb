/**
 * @file    main_window.h
 * @brief   Qt main window interfaces and desktop session state.
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

#pragma once

#include "audio_clock_drift.h"
#include "audio_clock_trace.h"
#include "color_pipeline.h"
#include "controller_manager.h"
#include "display_pipeline.h"
#include "guest_clock_pacer.h"
#include "host_elapsed_timer.h"
#include "turbo_audio_window.h"

#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>
#include <QPointF>
#include <QSettings>
#include <QTimer>
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

extern "C" {
#include "cupid/core/emulator.h"
#include "cupid/gb/cartridge.h"
#include "cupid/gb/cheats.h"
#include "cupid/gb/debugger.h"
#include "cupid/gb/gbs.h"
#include "cupid/gb/reset.h"
#include "cupid/gb/state.h"
}

enum class InspectorMemoryRegion;

class QAction;
class QAudioSink;
class QAudioFormat;
class QCloseEvent;
class QMenu;
class QMediaDevices;
class QIODevice;
class QKeyEvent;
class QSlider;
class DisplayWidget;
class AudioBuffer;
class AudioConverter;
class WaveRecorder;
class DebuggerWindow;
class InspectorsWindow;
class PrinterWindow;
class CheatsWindow;
struct CupidGbPrinter;
struct CupidGbWorkBoy;
class PaletteLibrary;
class HostCamera;
class CoreAudioCollector;
class GbsPlayer;
struct CupidGbSgbBorder;

class MainWindow final : public QMainWindow {
  public:
    enum class CameraSource { System, Generated };
    explicit MainWindow(bool isolated = false, bool automated = false, bool audioCheck = false,
                        const QString &bootDirectoryOverride = QString(),
                        std::optional<int> modelOverride = std::nullopt,
                        std::optional<uint64_t> powerOnSeedOverride = std::nullopt,
                        std::optional<CameraSource> cameraSourceOverride = std::nullopt);
    ~MainWindow() override;
    bool openRom(const QString &path);
    MainWindow *newWindow();
    bool connectLink(MainWindow *partner);
    void disconnectLink();
    enum class SmokeWorkflow { All, Desktop, Storage, Sgb, Link, Gbs };
    bool smokeTest(SmokeWorkflow workflow = SmokeWorkflow::All);
    void beginBoundedRun(double seconds, const QString &reportPath);
    bool loadInputTrace(const QString &path);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    void buildMenus();
    void registerLinkWindow();
    void unregisterLinkWindow();
    bool storageAvailable(const QString &rom, const QString &battery);
    void refreshLinkMenu();
    bool replaceLinkEndpoint(CupidGb *replacement);
    void rebaseLink();
    MainWindow *linkPartner() const;
    MainWindow *linkOwner() const;
    void setLinkPaused(bool paused, bool retainInput);
    void synchronizeInputBindings();
    void applyControllerConfiguration(const cupid::input::ControllerConfiguration &configuration,
                                      const QMap<SDL_JoystickID, int> &players,
                                      SDL_JoystickID preferred);
    void releaseLinkedInput();
    void applyLinkedPlayerButtons(const std::array<uint8_t, 4> &buttons);
    unsigned inputPlayerCount() const;
    bool deferLinkRecordingError(const QString &message);
    bool playbackTurbo() const;
    unsigned playbackSpeed() const;
    unsigned playbackTurboCap() const;
    void tickLink();
    void linkedDebuggerSlice();
    bool linkSmokeTest(const QString &directory);
    bool gbsSmokeTest(const QString &directory);
    bool prepareMusicCandidate(std::unique_ptr<CupidEmulator> &candidate, const QByteArray &data,
                               int &model, CupidGbGbsInfo &info);
    void setMusicSession(const std::optional<CupidGbGbsInfo> &info, uint8_t track);
    bool startMusicTrack(uint8_t track);
    void moveMusicTrack(int direction);
    void refreshMusicControls();
    void appendMusicSamples(const int16_t *samples, size_t frames);
    void publishMusicFrame();
    bool presentMusic();
    void buildSgbBorderMenu(QMenu *view);
    bool showSgbBorder(const CupidGb &gb) const;
    void borrowSgbBorder();
    void invalidateSgbBorder();
    QImage handheldBorder(const CupidGb &gb);
    void buildColorMenus(QMenu *view);
    void buildPaletteFileMenu(QMenu *palette);
    void importPalette();
    void exportPalette();
    bool loadPaletteFile(const QString &path);
    void openPaletteEditor();
    void applyPaletteLibrary(const PaletteLibrary &library);
    void applyDisplaySettings();
    void applyDisplayLayers(CupidGb *gb) const;
    void setFrameBlending(FrameBlending mode);
    QSize logicalWindowSize(const QSize &nativeSize, int scale) const;
    void resizeWindowCentered(const QSize &windowSize, const QRect &previousGeometry);
    void resizeDisplay(int scale);
    void updateRecentMenu();
    void configure();
    void configureControllers();
    void receiveControllerOutput(const cupid::input::ControllerOutput &output);
    void reconcileControllerInput(bool resetAudio = true);
    void refreshControllerGate();
    static std::optional<QPoint> pointerMotionSample(QPointF position, QSize source,
                                                     QSizeF available, DisplayScaling scaling,
                                                     qreal devicePixelRatio);
    bool motionInputAllowed(bool mouse = false) const;
    void refreshMotionGate();
    void resetMotionInput();
    void releaseMouseInput();
    void controllerMotionChanged(const cupid::input::ControllerOutput &previous);
    bool handleMotionEvent(QObject *object, QEvent *event);
    bool motionSmokeTest(const QString &directory);
    void refreshControllerPlayerCount();
    std::array<uint8_t, 4> livePlayerButtons() const;
    void applyPlayerButtons(const std::array<uint8_t, 4> &buttons);
    void navigateControllerMenu(const QString &action, SDL_JoystickID instance);
    void resetControllerRumble();
    void finishControllerRumble();
    bool controllerSmokeTest(const QString &directory);
    QString bootDirectory() const;
    void setPaused(bool paused, bool retainInput = false);
    void setPausedLocal(bool paused, bool retainInput);
    void releaseKeys(bool resetAudio = true);
    void setJoypad(uint8_t mask);
    void applyHeldButtons();
    void advanceControls();
    void watchBattery();
    void registerShortcut(QAction *action, const QString &id);
    bool flushBattery(bool forceWrite = false);
    bool closeRom();
    enum class ResetWorkflow { Open, Normal, Quick, Reload, HotSwap };
    void queueCameraCapture(CupidGb *gb, uint64_t request);
    void cancelCameraCapture();
    void applyCameraSource(CupidGb *gb);
    void commitCameraSession(CupidGb *candidate, ResetWorkflow workflow);
    bool cameraSmokeTest(const QString &directory);
    bool cameraSourceSmokeTest(const QString &directory);
    bool restart();
    bool quickReset();
    bool resetCartridge(ResetWorkflow workflow);
    bool resetConsole(CupidGbResetKind kind);
    bool prepareResetCandidate(CupidEmulator *candidate, ResetWorkflow workflow, bool sameStorage);
    bool prepareCompanionSymbols(const QString &romPath, std::unique_ptr<CupidGbDebugger> &symbols);
    bool resetSmokeTest(const QString &directory);
    bool largeCartridgeRamSmokeTest(const QString &directory);
    bool resetInputSmokeTest(const QString &directory);
    bool resetMenuInputSmokeTest(const QString &directory);
    bool resetAudioSmokeTest(const QString &directory);
    bool reloadRom();
    void chooseCartridgeSwap();
    bool openRom(const QString &path, bool reload, ResetWorkflow workflow = ResetWorkflow::Open);
    bool reloadSmokeTest(const QString &directory);
    bool hotSwapSmokeTest(const QString &directory);
    void resetPacing();
    void refreshTimerInterval();
    void setTurboCap(unsigned quarters);
    void tick();
    enum class Playback { Forward, Rewind, RewindEnd };

    struct PlaybackResult {
        uint64_t presentationCycles = 0;
        uint64_t guestCycles = 0;
        uint64_t timeline = 0;
    };

    struct PlaybackWork;
    bool beginPlayback(Playback playback, PlaybackWork &work);
    bool preparePlaybackStep(PlaybackWork &work);
    bool completePlaybackStep(PlaybackWork &work, bool executed);
    bool finishPlayback(PlaybackWork &work, PlaybackResult *result, bool pumpOutput = true,
                        bool commitOutput = true);
    bool runFrame(Playback playback = Playback::Forward, PlaybackResult *result = nullptr);
    bool runLinkedFrame(PlaybackResult *result = nullptr);
    enum class Presentation { Redraw, Frame, Reset };
    void present(Presentation presentation = Presentation::Redraw);
    void notifyOnScreen(const QString &message);
    void advanceScreenMessages();
    bool screenMessagesSmokeTest(const QString &path, const QString &directory);
    void applyAudioPolicy(CupidGb *gb) const;
    void setupAudio();
    void stopAudio();
    void clearAudio();
    void pumpAudio();
    bool collectAudio(CoreAudioCollector &collected);
    bool discardCoreAudio();
    void recordAudio(const CoreAudioCollector &collected);
    QByteArray convertPlaybackAudio(const int16_t *samples, size_t frames,
                                    const QAudioFormat &format);
    bool audioLifecycleSmokeTest(const QString &path);
    bool audioPolicySmokeTest(const QString &directory);
    bool startRecording(const QString &path);
    bool stopRecording();
    void screenshot();
    void screenshotAs();
    void copyScreenshot();
    bool saveScreenshot(const QString &path);
    bool displayFilesSmokeTest(const QString &directory);
    QByteArray captureState(bool reportFailure = true, bool rewindSnapshot = false);
    bool restoreState(const QByteArray &state, bool reportFailure, bool rewindSnapshot = false);
    QString slotPath(int slot, bool legacy = false) const;
    bool saveSlot(int slot);
    bool loadSlot(int slot);
    bool loadStateFile(const QString &path);
    bool savePortableStateFile(const QString &path);
    bool recoverBattery(const QString &statePath, const QString &outputPath);
    void resetRewind();
    void rebaseRewindAfterReset(uint64_t previousPresentationCycles);
    void trackRewindTime(uint64_t presentationCycles);
    void rememberFrame();
    bool rewindFrame(PlaybackResult *result = nullptr);
    void reportError(const QString &message);
    void refreshActions();
    void ensureDebugger();
    void openDebugger();
    void openInspectors();
    void setPrinterConnected(bool connected);
    void resetPrinter();
    void updatePrinter();
    bool printerSmokeTest(const QString &romPath, const QString &directory);
    void setWorkBoyConnected(bool connected);
    void resetWorkBoy();
    void releaseWorkBoyKeys();
    bool handleWorkBoyKey(QKeyEvent *event, bool pressed, bool overrideShortcut);
    int64_t readWorkBoyClock() const;
    void writeWorkBoyClock(int64_t seconds);
    bool workBoySmokeTest(const QString &directory);
    void refreshDebugger();
    void debuggerCommand(const QString &command);
    bool debuggerWatchCommand(const QString &command);
    QString debuggerWatchpointListing() const;
    QString debuggerWatchMessage(bool conditionError = false) const;
    bool loadDebuggerSymbols(const QString &path);
    void resetDebuggerTimeline(bool clearSession = false);
    void pauseDebugger();
    void debuggerSlice();
    void stopDebugger(const QString &message);
    bool rememberDebuggerStep();
    bool debuggerBackstep();
    bool debuggerWrite(uint16_t address, uint8_t value);
    bool debuggerWritePhysical(InspectorMemoryRegion region, size_t bank, uint16_t offset,
                               uint8_t value);
    void finishDebuggerEdit();
    bool drainDebuggerAudio();
    bool debuggerSmokeTest(const QString &directory);
    bool watchpointSmokeTest(const QString &directory);
    bool pacingSmokeTest(const QString &path);
    bool idleTimerSmokeTest(const QString &path);
    bool turboCapSmokeTest(const QString &path);
    bool displayCadenceSmokeTest(const QString &path);
    bool playbackSmokeTest(const QString &directory);
    bool modelSmokeTest(const QString &directory);
    bool startupSmokeTest(const QString &directory);
    bool sgbSmokeTest(const QString &directory);
    bool sgbBorderModesSmokeTest(const QString &directory);
    bool externalSgbSmokeTest(const QString &directory);
    bool rtcSmokeTest(const QString &directory);
    bool rtcBatterySmokeTest(const QString &directory);
    void openCheats(bool search = false);
    void refreshCheats(bool newSession = false);
    bool mutateCheats(const std::function<bool(CupidGbCheats *)> &operation);
    bool loadCheatFile(const QString &path, bool replace = false);
    bool exportCheatFile(const QString &path);
    bool cheatDestinationProtected(const QString &path, const QString &rom,
                                   const QString &battery) const;
    bool writeSearchMemory(const std::function<bool(CupidGb &)> &operation);
    bool cheatSmokeTest(const QString &path, const QString &directory);

    QSettings settings_;
    QString bootDirectoryOverride_;
    const std::optional<int> modelOverride_;
    const std::optional<uint64_t> powerOnSeedOverride_;
    const std::optional<CameraSource> cameraSourceOverride_;
    CameraSource cameraSource_ = CameraSource::System;
    std::unique_ptr<CupidEmulator> emulator_;
    struct LinkSession;
    std::shared_ptr<LinkSession> linkSession_;
    bool outgoingLinkBit_ = false;
    bool linkWindowClosing_ = false;
    std::unique_ptr<HostCamera> hostCamera_;
    QString cameraError_;
    using CheatCollection = std::unique_ptr<CupidGbCheats, decltype(&cupid_gb_cheats_destroy)>;
    CheatCollection cheats_{nullptr, cupid_gb_cheats_destroy};
    CheatsWindow *cheatsWindow_ = nullptr;
    QString cheatPath_;
    QString cheatSaveWarning_;
    QAction *cheatsAction_ = nullptr;
    QAction *cheatSearchAction_ = nullptr;
    QAction *enableCheatsAction_ = nullptr;
    std::unique_ptr<CupidGbDebugger> debugger_;
    std::unique_ptr<CupidGbStateContext> debuggerContext_;
    DebuggerWindow *debuggerWindow_ = nullptr;
    InspectorsWindow *inspectorsWindow_ = nullptr;
    QTimer debuggerTimer_;
    std::deque<QByteArray> debuggerHistory_;
    qsizetype debuggerHistoryBytes_ = 0;
    uint64_t debuggerCommandSteps_ = 0;
    uint64_t debuggerSessionId_ = 0;
    QAction *debuggerAction_ = nullptr;
    QAction *inspectorsAction_ = nullptr;
    QMenu *connectionsMenu_ = nullptr;
    QMenu *linkMenu_ = nullptr;
    QAction *printerAction_ = nullptr;
    QAction *workBoyAction_ = nullptr;
    QAction *disconnectAction_ = nullptr;
    PrinterWindow *printerWindow_ = nullptr;
    CupidGbPrinter *printer_ = nullptr;
    CupidGbWorkBoy *workBoy_ = nullptr;
    bool workBoyShiftHeld_ = false;
    bool suppressWorkBoyClockWrites_ = false;
    DisplayWidget *display_ = nullptr;
    GbsPlayer *musicPlayer_ = nullptr;
    std::optional<CupidGbGbsInfo> musicInfo_;
    uint8_t musicTrack_ = 0;
    bool musicPresentation_ = false;
    bool musicFramePending_ = false;
    QByteArray gameGeometry_;
    QSize gameMinimumSize_;
    QSize gameMaximumSize_;
    std::shared_ptr<AudioBuffer> audioBuffer_;
    std::shared_ptr<QAudioSink> audio_;
    QIODevice *audioDevice_ = nullptr;
    std::unique_ptr<AudioConverter> audioConverter_;
    AudioClockDrift audioClockDrift_;
    AudioClockTrace audioClockTrace_;
    uint64_t hostAudioBytesWritten_ = 0;
    TurboAudioWindow turboAudioWindow_;
    std::unique_ptr<WaveRecorder> recorder_;
    QMediaDevices *mediaDevices_ = nullptr;
    QByteArray activeAudioDeviceId_;
    QString activeAudioDeviceName_;
    bool audioDeviceFallback_ = false;
    qint64 audioQueueLimit_ = 8192;
    qint64 audioPrimeBytes_ = 0;
    bool audioPriming_ = true;
    bool audioModeResetPending_ = false;
    uint64_t audioPrimeEvents_ = 0;
    QTimer timer_;
    QTimer saveTimer_;
    QTimer audioRefreshTimer_;
    HostElapsedTimer clock_;
    qint64 nextFrame_ = 0;
    uint64_t pacingGeneration_ = 0;
    GuestClockPacer clockPacer_;
    QString romPath_;
    QString savePath_;
    QByteArray romData_;
    QByteArray savedRam_;
    uint64_t observedBatteryGeneration_ = 0;
    bool batteryDirty_ = false;
    std::array<QRgb, 4> displayPalette_{};
    QRgb displayOffColor_ = 0;
    ColorSettings colorSettings_;
    std::unique_ptr<ColorTable> cgbColorTable_;
    std::unique_ptr<ColorTable> sgbColorTable_;
    FrameBlending frameBlending_ = FrameBlending::Accurate;
    SgbBorderMode sgbBorderMode_ = SgbBorderMode::SgbOnly;
    std::shared_ptr<const CupidGbSgbBorder> borrowedSgbBorder_;
    QImage sgbBorderImage_;
    CupidGbModel sgbBorderImageModel_ = CUPID_GB_MODEL_DMG_ABC;
    uint64_t sgbBorderCartridge_ = 0;
    bool sgbBorderAttempted_ = false;
    bool sgbBorderJobPending_ = false;
    QImage image_;
    QSize displayNativeSize_{160, 144};
    QImage previousDisplayFrame_;
    QImage currentDisplayFrame_;
    QImage previousMessageFrame_;
    QImage currentMessageFrame_;
    QMenu *recentMenu_ = nullptr;
    QAction *pauseAction_ = nullptr;
    QAction *muteAction_ = nullptr;
    std::array<QAction *, 4> audioChannelActions_{};
    QAction *recordingAction_ = nullptr;
    QAction *closeAction_ = nullptr;
    QAction *resetAction_ = nullptr;
    QAction *quickResetAction_ = nullptr;
    QAction *reloadAction_ = nullptr;
    QAction *hotSwapAction_ = nullptr;
    QAction *frameAction_ = nullptr;
    QAction *screenshotAction_ = nullptr;
    QAction *screenshotAsAction_ = nullptr;
    QAction *copyScreenshotAction_ = nullptr;
    QAction *loadStateFileAction_ = nullptr;
    QAction *savePortableStateAction_ = nullptr;
    QAction *showBackgroundAction_ = nullptr;
    QAction *showObjectsAction_ = nullptr;
    std::array<QAction *, 3> scalingActions_{};
    std::array<QAction *, 3> scalingFilterActions_{};
    std::array<QAction *, 12> turboCapActions_{};
    std::array<QAction *, 5> paletteActions_{};
    std::array<QAction *, 8> windowSizeActions_{};
    std::array<QAction *, 3> blendingActions_{};
    std::array<QAction *, 7> colorCorrectionActions_{};
    QSlider *colorTemperatureSlider_ = nullptr;
    std::array<QAction *, 3> sgbBorderActions_{};
    std::array<QAction *, 10> saveSlotActions_{};
    std::array<QAction *, 10> loadSlotActions_{};
    std::array<int, 8> keys_{};
    std::array<int, 8> linkedKeys_{};
    uint8_t linkedKeyboardJoypad_ = 0xff;
    std::array<uint8_t, 2> linkedInput_{0xff, 0xff};
    std::array<int, 5> specialKeys_{};
    std::unique_ptr<cupid::input::ControllerManager> controllers_;
    cupid::input::ControllerOutput controllerOutput_;
    bool controllerInputDeferred_ = false;
    enum class MotionSource { None, Mouse, Controller };
    MotionSource motionSource_ = MotionSource::None;
    int16_t lastControllerMotionX_ = 0, lastControllerMotionY_ = 0;
    bool mouseButtonA_ = false, motionGateOpen_ = false, motionFocusActive_ = true;
    QAction *mouseMotionAction_ = nullptr;
    std::array<bool, 5> keyboardSpecialHeld_{};
    std::array<std::array<bool, 2>, 4> controllerRapidHeld_{};
    std::array<std::array<unsigned, 2>, 4> controllerRapidPhase_{};
    std::array<uint8_t, 4> debuggerPlayerButtons_{0xff, 0xff, 0xff, 0xff};
    unsigned controllerPlayerCount_ = 1;
    uint64_t rumbleLastCycles_ = 0, rumbleSampleSum_ = 0, rumbleSampleCount_ = 0;
    std::vector<double> rumblePublishedAmplitudes_;
    unsigned menuDepth_ = 0;
    bool menuInputSuspended_ = false;
    bool controllerSmokeActive_ = false;
    std::vector<QAction *> shortcutActions_;
    std::array<bool, 2> rapidHeld_{};
    std::array<unsigned, 2> rapidPhase_{};
    bool slowHeld_ = false;
    unsigned speedSteps_ = 16;
    unsigned turboCapQuarters_ = 0;
    uint8_t physicalJoypad_ = 0xff;
    bool paused_ = false;
    bool debuggerInputPaused_ = false;
    bool turbo_ = false;
    bool rewind_ = false;
    bool stateOperation_ = false;
    bool rewindFailed_ = false;
    bool rewindExhausted_ = false;

    struct RewindSnapshot {
        QByteArray compressed;
        uint64_t cycles; /* Ordered presentation age; independent of reset epochs. */
    };

    std::deque<RewindSnapshot> rewindStates_;
    std::unique_ptr<CupidGbStateContext> rewindContext_;
    uint64_t rewindAgeCycles_ = 0;
    uint64_t rewindPresentationCycles_ = 0;
    qsizetype rewindBytes_ = 0;
    qint64 lastRewindBudgetNotice_ = 0;
    uint64_t rewindSnapshotsCaptured_ = 0;
    qint64 rewindCaptureNanoseconds_ = 0;
    qint64 rewindCaptureMaximumNanoseconds_ = 0;
    bool isolated_ = false;
    bool failed_ = false;
    bool automated_ = false;
    bool audioCheck_ = false;
    HostElapsedTimer telemetryClock_;
    uint64_t telemetryStartCycles_ = 0;
    uint64_t telemetryStartPresentationCycles_ = 0;
    uint64_t presentedImages_ = 0;
    uint64_t musicWaveformsPublished_ = 0;
    uint64_t generatedAudioFrames_ = 0;
    uint64_t handheldAudioFrames_ = 0;
    uint64_t startupAudioFrames_ = 0;
    uint64_t replayedGuestCycles_ = 0;
    uint64_t replayedFrames_ = 0;
    uint64_t turboWindowDroppedFrames_ = 0;
    uint64_t turboCapacityDroppedFrames_ = 0;
    uint64_t audioIdleEvents_ = 0;
    uint64_t audioErrorEvents_ = 0;
    std::vector<std::pair<uint64_t, uint8_t>> inputTrace_;
    size_t inputTraceIndex_ = 0;
    uint64_t sessionId_ = 0;
    uint64_t timelineId_ = 0;
    uint64_t boundedRunId_ = 0;
};

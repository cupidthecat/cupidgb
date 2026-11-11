#pragma once

#include "color_pipeline.h"
#include "controller_manager.h"
#include "display_pipeline.h"
#include "guest_clock_pacer.h"

#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>
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
#include "cupid/gb/cheats.h"
#include "cupid/gb/debugger.h"
#include "cupid/gb/state.h"
}

class QAction;
class QAudioSink;
class QCloseEvent;
class QMenu;
class QMediaDevices;
class QIODevice;
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

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(bool isolated = false, bool automated = false, bool audioCheck = false,
                        const QString &bootDirectoryOverride = QString(),
                        std::optional<int> modelOverride = std::nullopt);
    ~MainWindow() override;
    bool openRom(const QString &path);
    bool smokeTest();
    void beginBoundedRun(double seconds, const QString &reportPath);
    bool loadInputTrace(const QString &path);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    void buildMenus();
    void buildColorMenus(QMenu *view);
    void applyDisplaySettings();
    void setFrameBlending(FrameBlending mode);
    QSize logicalWindowSize(const QSize &nativeSize, int scale) const;
    void resizeWindowCentered(const QSize &windowSize, const QRect &previousGeometry);
    void resizeDisplay(int scale);
    void updateRecentMenu();
    void configure();
    void configureControllers();
    void reconcileControllerInput();
    void refreshControllerGate();
    void refreshControllerPlayerCount();
    std::array<uint8_t, 4> livePlayerButtons() const;
    void applyPlayerButtons(const std::array<uint8_t, 4> &buttons);
    void navigateControllerMenu(const QString &action, SDL_JoystickID instance);
    void resetControllerRumble();
    void finishControllerRumble();
    bool controllerSmokeTest(const QString &directory);
    QString bootDirectory() const;
    void setPaused(bool paused);
    void releaseKeys();
    void setJoypad(uint8_t mask);
    void applyHeldButtons();
    void advanceControls();
    void watchBattery();
    void registerShortcut(QAction *action, const QString &id);
    bool flushBattery();
    bool closeRom();
    bool restart();
    void resetPacing();
    void tick();
    bool runFrame();
    void present(bool completedFrame = false);
    void setupAudio();
    void stopAudio();
    void clearAudio();
    void pumpAudio();
    bool startRecording(const QString &path);
    bool stopRecording();
    void screenshot();
    QByteArray captureState(bool reportFailure = true, bool rewindSnapshot = false);
    bool restoreState(const QByteArray &state, bool reportFailure, bool rewindSnapshot = false);
    QString slotPath(int slot, bool legacy = false) const;
    bool saveSlot(int slot);
    bool loadSlot(int slot);
    bool loadStateFile(const QString &path);
    bool recoverBattery(const QString &statePath, const QString &outputPath);
    void resetRewind();
    void rememberFrame();
    bool rewindFrame();
    void reportError(const QString &message);
    void refreshActions();
    void ensureDebugger();
    void openDebugger();
    void openInspectors();
    void setPrinterConnected(bool connected);
    void resetPrinter();
    void updatePrinter();
    bool printerSmokeTest(const QString &romPath, const QString &directory);
    void refreshDebugger();
    void debuggerCommand(const QString &command);
    bool loadDebuggerSymbols(const QString &path);
    void resetDebuggerTimeline(bool clearSession = false);
    void pauseDebugger();
    void debuggerSlice();
    void stopDebugger(const QString &message);
    bool rememberDebuggerStep();
    bool debuggerBackstep();
    bool debuggerWrite(uint16_t address, uint8_t value);
    void drainDebuggerAudio();
    bool debuggerSmokeTest(const QString &directory);
    bool pacingSmokeTest(const QString &path);
    bool modelSmokeTest(const QString &directory);
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
    std::unique_ptr<CupidEmulator> emulator_;
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
    QAction *printerAction_ = nullptr;
    QAction *disconnectAction_ = nullptr;
    PrinterWindow *printerWindow_ = nullptr;
    CupidGbPrinter *printer_ = nullptr;
    uint64_t printerGeneration_ = 0;
    DisplayWidget *display_ = nullptr;
    std::unique_ptr<AudioBuffer> audioBuffer_;
    std::unique_ptr<QAudioSink> audio_;
    QIODevice *audioDevice_ = nullptr;
    std::unique_ptr<AudioConverter> audioConverter_;
    std::unique_ptr<WaveRecorder> recorder_;
    QMediaDevices *mediaDevices_ = nullptr;
    QByteArray activeAudioDeviceId_;
    QString activeAudioDeviceName_;
    bool audioDeviceFallback_ = false;
    qint64 audioQueueLimit_ = 8192;
    qint64 audioPrimeBytes_ = 0;
    bool audioPriming_ = true;
    uint64_t audioPrimeEvents_ = 0;
    QTimer timer_;
    QTimer saveTimer_;
    QTimer audioRefreshTimer_;
    QElapsedTimer clock_;
    qint64 nextFrame_ = 0;
    GuestClockPacer clockPacer_;
    QString romPath_;
    QString savePath_;
    QByteArray romData_;
    QByteArray savedRam_;
    uint64_t observedBatteryGeneration_ = 0;
    bool batteryDirty_ = false;
    std::array<QRgb, 4> displayPalette_{};
    ColorSettings colorSettings_;
    std::unique_ptr<ColorTable> cgbColorTable_;
    std::unique_ptr<ColorTable> sgbColorTable_;
    FrameBlending frameBlending_ = FrameBlending::Accurate;
    QImage image_;
    QSize displayNativeSize_{160, 144};
    QImage previousDisplayFrame_;
    QImage currentDisplayFrame_;
    QMenu *recentMenu_ = nullptr;
    QAction *pauseAction_ = nullptr;
    QAction *muteAction_ = nullptr;
    QAction *recordingAction_ = nullptr;
    QAction *closeAction_ = nullptr;
    QAction *resetAction_ = nullptr;
    QAction *frameAction_ = nullptr;
    QAction *screenshotAction_ = nullptr;
    QAction *loadStateFileAction_ = nullptr;
    std::array<QAction *, 3> scalingActions_{};
    std::array<QAction *, 5> paletteActions_{};
    std::array<QAction *, 8> windowSizeActions_{};
    std::array<QAction *, 3> blendingActions_{};
    std::array<QAction *, 7> colorCorrectionActions_{};
    QSlider *colorTemperatureSlider_ = nullptr;
    QAction *borderAction_ = nullptr;
    std::array<QAction *, 10> saveSlotActions_{};
    std::array<QAction *, 10> loadSlotActions_{};
    std::array<int, 8> keys_{};
    std::array<int, 5> specialKeys_{};
    std::unique_ptr<cupid::input::ControllerManager> controllers_;
    cupid::input::ControllerOutput controllerOutput_;
    std::array<bool, 5> keyboardSpecialHeld_{};
    std::array<std::array<bool, 2>, 4> controllerRapidHeld_{};
    std::array<std::array<unsigned, 2>, 4> controllerRapidPhase_{};
    std::array<uint8_t, 4> debuggerPlayerButtons_{0xff, 0xff, 0xff, 0xff};
    unsigned controllerPlayerCount_ = 1;
    uint64_t rumbleLastCycles_ = 0, rumbleWindowCycles_ = 0, rumbleOnCycles_ = 0;
    bool rumbleMotor_ = false;
    unsigned menuDepth_ = 0;
    bool controllerSmokeActive_ = false;
    std::vector<QAction *> shortcutActions_;
    std::array<bool, 2> rapidHeld_{};
    std::array<unsigned, 2> rapidPhase_{};
    bool slowHeld_ = false;
    unsigned speedSteps_ = 16;
    uint8_t physicalJoypad_ = 0xff;
    bool paused_ = false;
    bool debuggerInputPaused_ = false;
    bool turbo_ = false;
    bool rewind_ = false;
    bool stateOperation_ = false;
    bool rewindFailed_ = false;

    struct RewindSnapshot {
        QByteArray compressed;
        uint64_t cycles;
    };

    std::deque<RewindSnapshot> rewindStates_;
    std::unique_ptr<CupidGbStateContext> rewindContext_;
    qsizetype rewindBytes_ = 0;
    qint64 lastRewindBudgetNotice_ = 0;
    uint64_t rewindSnapshotsCaptured_ = 0;
    qint64 rewindCaptureNanoseconds_ = 0;
    qint64 rewindCaptureMaximumNanoseconds_ = 0;
    bool isolated_ = false;
    bool failed_ = false;
    bool automated_ = false;
    bool audioCheck_ = false;
    QElapsedTimer telemetryClock_;
    uint64_t telemetryStartCycles_ = 0;
    uint64_t presentedImages_ = 0;
    uint64_t generatedAudioFrames_ = 0;
    uint64_t audioIdleEvents_ = 0;
    uint64_t audioErrorEvents_ = 0;
    std::vector<std::pair<uint64_t, uint8_t>> inputTrace_;
    size_t inputTraceIndex_ = 0;
    uint64_t sessionId_ = 0;
    uint64_t timelineId_ = 0;
    uint64_t boundedRunId_ = 0;
};

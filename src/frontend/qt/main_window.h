#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>
#include <array>
#include <memory>
#include <vector>

extern "C" {
#include "cupid/core/emulator.h"
}

class QAction;
class QAudioSink;
class QCloseEvent;
class QMenu;
class QIODevice;
class DisplayWidget;
class AudioBuffer;
class AudioConverter;

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(bool isolated = false, bool automated = false);
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
    void updateRecentMenu();
    void configure();
    void setPaused(bool paused);
    void releaseKeys();
    void setJoypad(uint8_t mask);
    bool flushBattery();
    bool closeRom();
    bool restart();
    void tick();
    bool runFrame();
    void present();
    void setupAudio();
    void stopAudio();
    void clearAudio();
    void pumpAudio();
    void screenshot();
    void reportError(const QString &message);
    void refreshActions();

    QSettings settings_;
    std::unique_ptr<CupidEmulator> emulator_;
    DisplayWidget *display_ = nullptr;
    std::unique_ptr<AudioBuffer> audioBuffer_;
    std::unique_ptr<QAudioSink> audio_;
    QIODevice *audioDevice_ = nullptr;
    std::unique_ptr<AudioConverter> audioConverter_;
    qint64 audioQueueLimit_ = 8192;
    QTimer timer_;
    QTimer saveTimer_;
    QElapsedTimer clock_;
    qint64 nextFrame_ = 0;
    QString romPath_;
    QString savePath_;
    QByteArray romData_;
    QByteArray savedRam_;
    QImage image_;
    QMenu *recentMenu_ = nullptr;
    QAction *pauseAction_ = nullptr;
    QAction *muteAction_ = nullptr;
    QAction *closeAction_ = nullptr;
    QAction *resetAction_ = nullptr;
    QAction *frameAction_ = nullptr;
    QAction *screenshotAction_ = nullptr;
    std::array<int, 8> keys_{};
    bool paused_ = false;
    bool turbo_ = false;
    bool isolated_ = false;
    bool failed_ = false;
    bool automated_ = false;
    QElapsedTimer telemetryClock_;
    uint64_t telemetryStartCycles_ = 0;
    uint64_t presentedImages_ = 0;
    uint64_t generatedAudioFrames_ = 0;
    uint64_t audioIdleEvents_ = 0;
    uint64_t audioErrorEvents_ = 0;
    std::vector<std::pair<uint64_t, uint8_t>> inputTrace_;
    size_t inputTraceIndex_ = 0;
    uint64_t sessionId_ = 0;
};

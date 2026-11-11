#pragma once

#include "controller_manager.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;
class QTableWidget;

class ControllerDialog final : public QDialog {
  public:
    ControllerDialog(cupid::input::ControllerManager &manager,
                     const QList<QPair<QString, QString>> &shortcuts, QWidget *parent = nullptr);
    ~ControllerDialog() override;
    cupid::input::ControllerConfiguration configuration();
    QMap<SDL_JoystickID, int> playerOverrides();
    SDL_JoystickID preferredInstance() const;

  private:
    void refreshDevices();
    void selectDevice();
    void saveDevice();
    void refreshBindings();
    void refreshMotionAxes(const cupid::input::DeviceState &state);
    void capture(const cupid::input::InputEvent &event);
    void beginCapture();
    void cancelCapture();
    void refreshPreview();
    cupid::input::ControllerManager &manager_;
    cupid::input::ControllerConfiguration draft_;
    QMap<SDL_JoystickID, int> players_;
    SDL_JoystickID current_ = -1;
    QString currentKey_, captureAction_;
    cupid::input::DeviceInfo currentInfo_;
    QMap<QString, QString> motionErrors_;
    QStringList actions_;
    QComboBox *devices_ = nullptr, *player_ = nullptr, *preferred_ = nullptr;
    QComboBox *motionX_ = nullptr, *motionY_ = nullptr;
    QCheckBox *enabled_ = nullptr, *background_ = nullptr, *deviceEnabled_ = nullptr,
              *rumble_ = nullptr;
    QSpinBox *press_ = nullptr, *release_ = nullptr;
    QSlider *strength_ = nullptr;
    QTableWidget *bindings_ = nullptr;
    QLabel *identity_ = nullptr, *preview_ = nullptr, *status_ = nullptr;
    QTimer previewTimer_;
    bool selecting_ = false;
    bool preferredInitialized_ = false;
};

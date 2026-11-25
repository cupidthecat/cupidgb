/**
 * @file    controller_dialog.h
 * @brief   Qt controller configuration and binding-capture dialog interface.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

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

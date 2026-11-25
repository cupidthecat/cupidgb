/**
 * @file    host_camera.h
 * @brief   Queued Qt camera capture ownership and sensor-image conversion.
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

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVideoFrame>
#include <functional>
#include <memory>

class CameraDeviceSession;

// The common 8-bit YUV formats retain delivered luminance bytes. Other formats
// use Qt's color conversion followed by qGray. Both paths apply orientation,
// smooth scaling to cover 130 by 114, then the centered 128 by 112 sensor crop.
QByteArray cameraSensorPixels(const QVideoFrame &frame, bool *directLuminance = nullptr);

class HostCamera final : public QObject {
  public:
    using FrameReady = std::function<void(const QVideoFrame &, const QString &)>;
    using CaptureSource = std::function<void(FrameReady)>;
    using Completion = std::function<void(const QByteArray &, const QString &)>;

    // An injected source supplies frames without enumerating or opening devices.
    explicit HostCamera(QObject *parent = nullptr, CaptureSource source = {},
                        std::function<void()> cancelSource = {}, int timeoutMilliseconds = 10000);
    ~HostCamera() override;
    void request(Completion completion);
    void cancel();

  private:
    struct Request {
        Completion completion;
    };

    void delivered(const std::shared_ptr<Request> &request, const QVideoFrame &frame,
                   const QString &error);
    void finish(const std::shared_ptr<Request> &request, const QByteArray &pixels,
                const QString &error);
    std::shared_ptr<Request> pending_;
    QTimer timeout_;
    CaptureSource source_;
    std::function<void()> cancelSource_;
    std::unique_ptr<CameraDeviceSession> device_;
};

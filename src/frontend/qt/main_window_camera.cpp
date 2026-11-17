/**
 * @file    main_window_camera.cpp
 * @brief   Qt camera requests bound to committed cartridge and state timelines.
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

void MainWindow::cancelCameraCapture() {
    if (hostCamera_)
        hostCamera_->cancel();
    if (emulator_)
        cupid_gb_camera_cancel_request(&emulator_->gb);
}

void MainWindow::commitCameraSession(CupidGb *candidate, ResetWorkflow workflow) {
    // The caller has finished every fallible cartridge/reset operation. Copying
    // this host tail never publishes device work from a detached candidate.
    if (emulator_)
        candidate->camera_host = emulator_->gb.camera_host;
    cupid_gb_camera_cancel_request(candidate);
    cancelCameraCapture();
    if (workflow == ResetWorkflow::Open) {
        hostCamera_.reset();
        cupid_gb_set_camera_callback(candidate, nullptr, nullptr);
        cameraError_.clear();
    }
    if (candidate->header.mbc_type != CUPID_GB_CAMERA) {
        hostCamera_.reset();
        cupid_gb_set_camera_callback(candidate, nullptr, nullptr);
        return;
    }
    cupid_gb_set_camera_callback(
        candidate,
        [](CupidGb *gb, uint64_t request, void *user) {
            static_cast<MainWindow *>(user)->queueCameraCapture(gb, request);
        },
        this);
}

void MainWindow::queueCameraCapture(CupidGb *gb, uint64_t request) {
    const uint64_t session = sessionId_, timeline = timelineId_;
    // A guest bus write can only enqueue work. Device initialization, permission
    // UI and conversion cannot dispatch events inside instruction execution.
    QMetaObject::invokeMethod(
        this,
        [this, gb, request, session, timeline] {
            const auto current = [this, gb, request, session, timeline] {
                return emulator_ && &emulator_->gb == gb && sessionId_ == session &&
                       timelineId_ == timeline && gb->camera_host.pending_request == request &&
                       gb->camera_host.request_callback;
            };
            if (!current())
                return;
            if (!hostCamera_)
                hostCamera_ = std::make_unique<HostCamera>(this);
            hostCamera_->request([this, gb, request, current](const QByteArray &pixels,
                                                              const QString &error) {
                if (!current())
                    return;
                const auto *data =
                    pixels.isEmpty() ? nullptr : reinterpret_cast<const uint8_t *>(pixels.data());
                if (!cupid_gb_camera_complete(gb, request, data, size_t(pixels.size())))
                    return;
                if (error.isEmpty()) {
                    cameraError_.clear();
                } else if (cameraError_ != error) {
                    cameraError_ = error;
                    statusBar()->showMessage("Camera unavailable: " + error, 5000);
                }
            });
        },
        Qt::QueuedConnection);
}

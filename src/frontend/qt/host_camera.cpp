/**
 * @file    host_camera.cpp
 * @brief   Lazy default-device captures and bounded grayscale conversion for Camera cartridges.
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

#include "cupid/gb/camera.h"

#include <QBuffer>
#include <QCamera>
#include <QCameraDevice>
#include <QCoreApplication>
#include <QImage>
#include <QImageCapture>
#include <QImageReader>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPointer>
#include <QScopeGuard>
#include <QTransform>
#include <algorithm>
#include <cstring>
#include <utility>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif

namespace {
bool boundedSize(QSize size) {
    return size.width() > 0 && size.height() > 0 && size.width() <= 16384 &&
           size.height() <= 16384 && qint64(size.width()) * size.height() <= 64 * 1024 * 1024;
}

bool supportedGeometry(const QVideoFrame &frame) {
    using Format = QVideoFrameFormat;
    switch (frame.pixelFormat()) {
        case Format::Format_YUV420P:
        case Format::Format_YUV420P10:
        case Format::Format_YV12:
        case Format::Format_NV12:
        case Format::Format_NV21:
        case Format::Format_IMC1:
        case Format::Format_IMC2:
        case Format::Format_IMC3:
        case Format::Format_IMC4:
        case Format::Format_P010:
        case Format::Format_P016:
            // Qt mapping can divide by the subsampled height, and its 16-bit
            // color decoder writes complete 2x2 blocks only. Check before map().
            return frame.width() >= 2 && frame.height() >= 2 && !(frame.width() & 1) &&
                   !(frame.height() & 1);
        case Format::Format_YUV422P:
        case Format::Format_UYVY:
        case Format::Format_YUYV:
            return frame.width() >= 2 && !(frame.width() & 1);
        default:
            return true;
    }
}

QImage orient(QImage image, int rotation, bool mirrored) {
    if (rotation)
        image = image.transformed(QTransform().rotate(rotation));
    if (mirrored)
        image = image.mirrored(true, false);
    return image;
}

QImage rawLuminance(QVideoFrame frame, bool *supported) {
    using Format = QVideoFrameFormat;
    unsigned step = 1, offset = 0;
    *supported = true;
    switch (frame.pixelFormat()) {
        case Format::Format_YUV420P:
        case Format::Format_YUV422P:
        case Format::Format_YV12:
        case Format::Format_NV12:
        case Format::Format_NV21:
        case Format::Format_IMC1:
        case Format::Format_IMC2:
        case Format::Format_IMC3:
        case Format::Format_IMC4:
        case Format::Format_Y8:
            break;
        case Format::Format_UYVY:
            step = 2;
            offset = 1;
            break;
        case Format::Format_YUYV:
            step = 2;
            break;
        default:
            *supported = false;
            return {};
    }
    if (!frame.map(QVideoFrame::ReadOnly))
        return {};
    const int stride = frame.bytesPerLine(0), available = frame.mappedBytes(0);
    const auto *source = frame.bits(0);
    const qint64 rowBytes = qint64(frame.width() - 1) * step + offset + 1;
    const qint64 required = qint64(frame.height() - 1) * stride + rowBytes;
    if (!source || stride <= 0 || rowBytes > stride || required > available) {
        frame.unmap();
        return {};
    }
    QImage gray(frame.size(), QImage::Format_Grayscale8);
    if (gray.isNull()) {
        frame.unmap();
        return {};
    }
    for (int y = 0; y < frame.height(); ++y) {
        const auto *row = source + qint64(y) * stride;
        auto *output = gray.scanLine(y);
        if (step == 1)
            std::memcpy(output, row, size_t(frame.width()));
        else
            for (int x = 0; x < frame.width(); ++x)
                output[x] = row[size_t(x) * step + offset];
    }
    frame.unmap();
    return gray;
}

QImage grayscaleImage(const QImage &image) {
    if (image.isNull() || !boundedSize(image.size()))
        return {};
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    QImage gray(rgb.size(), QImage::Format_Grayscale8);
    if (rgb.isNull() || gray.isNull())
        return {};
    for (int y = 0; y < rgb.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        auto *output = gray.scanLine(y);
        for (int x = 0; x < rgb.width(); ++x)
            output[x] = uchar(qGray(row[x]));
    }
    return gray;
}

struct ColorPlane {
    int rowBytes;
    int rows;
};

bool planeFits(const QVideoFrame &frame, int plane, ColorPlane geometry) {
    const int stride = frame.bytesPerLine(plane);
    return geometry.rowBytes > 0 && geometry.rows > 0 && frame.bits(plane) &&
           stride >= geometry.rowBytes &&
           qint64(geometry.rows - 1) * stride + geometry.rowBytes <= frame.mappedBytes(plane);
}

QImage jpegColor(const QVideoFrame &frame) {
    constexpr int maximumEncodedBytes = 64 * 1024 * 1024;
    const int bytes = frame.mappedBytes(0);
    if (frame.planeCount() != 1 || bytes <= 0 || bytes > maximumEncodedBytes || !frame.bits(0))
        return {};
    // Compressed camera buffers may report zero bytesPerLine. Their usable
    // payload is the mapped byte span, not width times an uncompressed stride.
    QByteArray encoded =
        QByteArray::fromRawData(reinterpret_cast<const char *>(frame.bits(0)), bytes);
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer, "jpeg");
    reader.setAutoDetectImageFormat(false);
    reader.setDecideFormatFromContent(false);
    reader.setAutoTransform(false);
    const QSize reported = reader.size();
    if (!boundedSize(reported) || reported != frame.size())
        return {};
    const QImage decoded = reader.read();
    if (decoded.isNull() || decoded.size() != reported || !boundedSize(decoded.size()))
        return {};
    return grayscaleImage(decoded);
}

QImage mappedColor(QVideoFrame frame) {
    if (!frame.map(QVideoFrame::ReadOnly))
        return {};
    const auto unmap = qScopeGuard([&frame] { frame.unmap(); });
    if (frame.pixelFormat() == QVideoFrameFormat::Format_Jpeg)
        return jpegColor(frame);
    const auto imageFormat = QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat());
    if (imageFormat != QImage::Format_Invalid) {
        const int depth = QImage(1, 1, imageFormat).depth();
        if (!planeFits(frame, 0, {(frame.width() * depth + 7) / 8, frame.height()}))
            return {};
        // Read mapped RGB directly. Calling toImage() on an oriented frame can
        // use different transform order in Qt's CPU and GPU conversion paths.
        return grayscaleImage(QImage(static_cast<const uchar *>(frame.bits(0)), frame.width(),
                                     frame.height(), frame.bytesPerLine(0), imageFormat));
    }

    using Format = QVideoFrameFormat;
    ColorPlane planes[3]{};
    int count = 0;
    const int width = frame.width(), height = frame.height();
    const int chromaWidth = (width + 1) / 2, chromaHeight = (height + 1) / 2;
    switch (frame.pixelFormat()) {
        case Format::Format_ARGB8888:
        case Format::Format_ARGB8888_Premultiplied:
        case Format::Format_XRGB8888:
        case Format::Format_BGRA8888:
        case Format::Format_BGRA8888_Premultiplied:
        case Format::Format_BGRX8888:
        case Format::Format_ABGR8888:
        case Format::Format_XBGR8888:
        case Format::Format_AYUV:
        case Format::Format_AYUV_Premultiplied:
            planes[count++] = {width * 4, height};
            break;
        case Format::Format_P010:
        case Format::Format_P016:
            planes[count++] = {width * 2, height};
            planes[count++] = {chromaWidth * 4, chromaHeight};
            break;
        case Format::Format_YUV420P10:
            planes[count++] = {width * 2, height};
            planes[count++] = {chromaWidth * 2, chromaHeight};
            planes[count++] = {chromaWidth * 2, chromaHeight};
            break;
        default:
            return {};
    }
    // Keep the color space, range, transfer function and pixel layout. Only the
    // orientation is neutralized, so Qt decodes color before our common transform.
    auto format = frame.surfaceFormat();
    format.setScanLineDirection(Format::TopToBottom);
    format.setMirrored(false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    format.setRotation(QtVideo::Rotation::None);
#endif
    QVideoFrame neutral(format);
    if (!neutral.isValid() || !neutral.map(QVideoFrame::WriteOnly))
        return {};
    {
        const auto unmapNeutral = qScopeGuard([&neutral] { neutral.unmap(); });
        if (frame.planeCount() != count || neutral.planeCount() != count)
            return {};
        for (int plane = 0; plane < count; ++plane) {
            if (!planeFits(frame, plane, planes[plane]) ||
                !planeFits(neutral, plane, planes[plane]))
                return {};
            std::memset(neutral.bits(plane), 0, size_t(neutral.mappedBytes(plane)));
            for (int row = 0; row < planes[plane].rows; ++row)
                std::memcpy(neutral.bits(plane) + qint64(row) * neutral.bytesPerLine(plane),
                            frame.bits(plane) + qint64(row) * frame.bytesPerLine(plane),
                            size_t(planes[plane].rowBytes));
        }
    }
    return grayscaleImage(neutral.toImage());
}
} // namespace

QByteArray cameraSensorPixels(const QVideoFrame &frame, bool *directLuminance) {
    if (directLuminance)
        *directLuminance = false;
    if (!frame.isValid() || !boundedSize(frame.size()) || !supportedGeometry(frame))
        return {};
    bool direct = false;
    QImage gray = rawLuminance(frame, &direct);
    if (!direct)
        gray = mappedColor(frame);
    if (gray.isNull())
        return {};
    const auto format = frame.surfaceFormat();
    if (format.scanLineDirection() == QVideoFrameFormat::BottomToTop)
        gray = gray.mirrored(false, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Qt 6.8 separates stored surface orientation from presentation metadata.
    gray = orient(std::move(gray), int(format.rotation()), format.isMirrored());
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    gray = orient(std::move(gray), int(frame.rotation()), frame.mirrored());
#else
    // Older releases expose one effective frame transform. Some versions alias
    // its mirror flag into surfaceFormat(), so applying both mirrors is wrong.
    gray = orient(std::move(gray), int(frame.rotationAngle()), frame.mirrored());
#endif
    // A one-pixel margin on each side survives the central sensor crop. Qt's
    // interpolation is an explicit host conversion policy, not sensor arithmetic.
    const QSize scaledSize = gray.size().scaled(QSize(130, 114), Qt::KeepAspectRatioByExpanding);
    if (!boundedSize(scaledSize))
        return {};
    gray = gray.scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    gray = gray.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull() || gray.width() < 128 || gray.height() < 112)
        return {};
    const int left = (gray.width() - 128) / 2, top = (gray.height() - 112) / 2;
    QByteArray pixels(CUPID_GB_CAMERA_SENSOR_SIZE, Qt::Uninitialized);
    for (int y = 0; y < CUPID_GB_CAMERA_SENSOR_HEIGHT; ++y)
        std::memcpy(pixels.data() + y * CUPID_GB_CAMERA_SENSOR_WIDTH,
                    gray.constScanLine(top + y) + left, CUPID_GB_CAMERA_SENSOR_WIDTH);
    if (directLuminance)
        *directLuminance = direct;
    return pixels;
}

class CameraDeviceSession final : public QObject {
  public:
    explicit CameraDeviceSession(QObject *parent) : QObject(parent) {
    }

    ~CameraDeviceSession() override {
        cancel();
        if (camera_)
            camera_->stop();
        if (session_) {
            session_->setImageCapture(nullptr);
            session_->setCamera(nullptr);
        }
    }

    void capture(HostCamera::FrameReady completion) {
        cancel();
        completion_ = std::move(completion);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#if QT_CONFIG(permissions)
        auto *application = QCoreApplication::instance();
        const QCameraPermission permission;
        switch (application->checkPermission(permission)) {
            case Qt::PermissionStatus::Denied:
                finish({}, QStringLiteral("Camera permission was denied."));
                return;
            case Qt::PermissionStatus::Undetermined:
                if (!permissionPending_) {
                    permissionPending_ = true;
                    application->requestPermission(permission, this, [this](const QPermission &p) {
                        permissionPending_ = false;
                        if (!completion_)
                            return;
                        if (p.status() == Qt::PermissionStatus::Granted)
                            prepareDevice();
                        else
                            finish({}, QStringLiteral("Camera permission was denied."));
                    });
                }
                return;
            case Qt::PermissionStatus::Granted:
                break;
        }
#endif
#endif
        prepareDevice();
    }

    void cancel() {
        completion_ = {};
        captureId_ = -1;
    }

    void retryDevice() {
        retryDevice_ = true;
    }

  private:
    void finish(const QVideoFrame &frame, const QString &error) {
        auto completion = std::move(completion_);
        captureId_ = -1;
        if (completion)
            completion(frame, error);
    }

    void prepareDevice() {
        if (!completion_)
            return;
        if (retryDevice_ && camera_) {
            camera_->stop();
            session_->setImageCapture(nullptr);
            session_->setCamera(nullptr);
            capture_.reset();
            camera_.reset();
            session_.reset();
        }
        retryDevice_ = false;
        if (!camera_) {
            const QCameraDevice input = QMediaDevices::defaultVideoInput();
            if (input.isNull()) {
                finish({}, QStringLiteral("No camera is available."));
                return;
            }
            session_ = std::make_unique<QMediaCaptureSession>();
            camera_ = std::make_unique<QCamera>(input);
            capture_ = std::make_unique<QImageCapture>();
            session_->setCamera(camera_.get());
            session_->setImageCapture(capture_.get());
            const QPointer<QCamera> cameraOwner(camera_.get());
            const QPointer<QImageCapture> captureOwner(capture_.get());
            // Queued backend signals cannot complete a request inside start()
            // or capture() before its device request ID has been installed.
            connect(
                camera_.get(), &QCamera::errorOccurred, this,
                [this, cameraOwner](QCamera::Error error, const QString &message) {
                    if (cameraOwner && camera_.get() == cameraOwner && completion_ &&
                        error != QCamera::NoError)
                        finish({}, message);
                },
                Qt::QueuedConnection);
            connect(
                capture_.get(), &QImageCapture::readyForCaptureChanged, this,
                [this, captureOwner](bool ready) {
                    if (captureOwner && capture_.get() == captureOwner && ready)
                        takeImage();
                },
                Qt::QueuedConnection);
            connect(
                capture_.get(), &QImageCapture::errorOccurred, this,
                [this, captureOwner](int id, QImageCapture::Error error, const QString &message) {
                    if (captureOwner && capture_.get() == captureOwner && completion_ &&
                        error != QImageCapture::NoError && (id == captureId_ || id < 0))
                        finish({}, message);
                },
                Qt::QueuedConnection);
            connect(
                capture_.get(), &QImageCapture::imageAvailable, this,
                [this, captureOwner](int id, const QVideoFrame &frame) {
                    if (captureOwner && capture_.get() == captureOwner && completion_ &&
                        id == captureId_)
                        finish(frame, {});
                },
                Qt::QueuedConnection);
        }
        if (!capture_->isAvailable()) {
            finish({}, QStringLiteral("This camera cannot capture images."));
            return;
        }
        if (camera_->error() != QCamera::NoError) {
            finish({}, camera_->errorString());
            return;
        }
        if (!camera_->isActive())
            camera_->start();
        takeImage();
    }

    void takeImage() {
        if (!completion_ || captureId_ >= 0 || !capture_ || !capture_->isReadyForCapture())
            return;
        captureId_ = capture_->capture();
        if (captureId_ < 0)
            finish({}, capture_->errorString().isEmpty()
                           ? QStringLiteral("The camera could not capture an image.")
                           : capture_->errorString());
    }

    HostCamera::FrameReady completion_;
    std::unique_ptr<QMediaCaptureSession> session_;
    std::unique_ptr<QCamera> camera_;
    std::unique_ptr<QImageCapture> capture_;
    int captureId_ = -1;
    bool permissionPending_ = false;
    bool retryDevice_ = false;
};

HostCamera::HostCamera(QObject *parent, CaptureSource source, std::function<void()> cancelSource,
                       int timeoutMilliseconds)
    : QObject(parent), source_(std::move(source)), cancelSource_(std::move(cancelSource)) {
    timeout_.setSingleShot(true);
    timeout_.setInterval(std::clamp(timeoutMilliseconds, 1, 10000));
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (pending_)
            finish(pending_, {}, QStringLiteral("The camera did not return an image in time."));
    });
}

HostCamera::~HostCamera() {
    cancel();
}

void HostCamera::request(Completion completion) {
    cancel();
    pending_ = std::make_shared<Request>(Request{std::move(completion)});
    timeout_.start();
    const std::weak_ptr<Request> request = pending_;
    const QPointer<HostCamera> owner(this);
    FrameReady ready = [owner, request](const QVideoFrame &frame, const QString &error) {
        const auto active = request.lock();
        if (owner && active)
            owner->delivered(active, frame, error);
    };
    if (source_) {
        source_(std::move(ready));
        return;
    }
    if (!device_)
        device_ = std::make_unique<CameraDeviceSession>(this);
    device_->capture(std::move(ready));
}

void HostCamera::cancel() {
    timeout_.stop();
    pending_.reset();
    if (device_)
        device_->cancel();
    if (cancelSource_)
        cancelSource_();
}

void HostCamera::delivered(const std::shared_ptr<Request> &request, const QVideoFrame &frame,
                           const QString &error) {
    if (pending_ != request)
        return;
    if (!error.isEmpty()) {
        finish(request, {}, error);
        return;
    }
    const QByteArray pixels = cameraSensorPixels(frame);
    finish(request, pixels,
           pixels.isEmpty() ? QStringLiteral("The camera returned an invalid image.") : QString());
}

void HostCamera::finish(const std::shared_ptr<Request> &request, const QByteArray &pixels,
                        const QString &error) {
    if (pending_ != request)
        return;
    auto completion = std::move(request->completion);
    if (device_ && !error.isEmpty())
        device_->retryDevice();
    cancel();
    if (completion)
        completion(pixels, error);
}

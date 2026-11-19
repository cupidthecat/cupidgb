/**
 * @file    test_host_camera.cpp
 * @brief   Generated frame conversion and asynchronous camera ownership regressions.
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

#include <QBuffer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
QVideoFrame luminanceFrame(QVideoFrameFormat::PixelFormat pixelFormat, QSize size, int gray = -1,
                           bool bottomUp = false, bool formatMirror = false) {
    QVideoFrameFormat format(size, pixelFormat);
    format.setColorRange(QVideoFrameFormat::ColorRange_Video);
    format.setScanLineDirection(bottomUp ? QVideoFrameFormat::BottomToTop
                                         : QVideoFrameFormat::TopToBottom);
    format.setMirrored(formatMirror);
    QVideoFrame frame(format);
    assert(frame.isValid() && frame.map(QVideoFrame::WriteOnly));
    for (int plane = 0; plane < frame.planeCount(); ++plane)
        std::memset(frame.bits(plane), 128, size_t(frame.mappedBytes(plane)));
    const bool packed = pixelFormat == QVideoFrameFormat::Format_YUYV ||
                        pixelFormat == QVideoFrameFormat::Format_UYVY;
    const int step = packed ? 2 : 1;
    const int offset = pixelFormat == QVideoFrameFormat::Format_UYVY ? 1 : 0;
    for (int y = 0; y < size.height(); ++y) {
        auto *row = frame.bits(0) + y * frame.bytesPerLine(0);
        for (int x = 0; x < size.width(); ++x)
            row[x * step + offset] = uchar(gray >= 0 ? gray : (x + 3 * y) & 255);
    }
    frame.unmap();
    return frame;
}

QVideoFrame rgbFrame(QSize size, QRgb color) {
    const auto pixelFormat = QVideoFrameFormat::pixelFormatFromImageFormat(QImage::Format_ARGB32);
    QVideoFrame frame(QVideoFrameFormat(size, pixelFormat));
    assert(frame.isValid() && frame.map(QVideoFrame::WriteOnly));
    for (int y = 0; y < size.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(frame.bits(0) + y * frame.bytesPerLine(0));
        for (int x = 0; x < size.width(); ++x)
            row[x] = color;
    }
    frame.unmap();
    return frame;
}

QByteArray encodedImage(const QImage &image, const char *format) {
    QByteArray bytes;
    QBuffer output(&bytes);
    assert(output.open(QIODevice::WriteOnly) && image.save(&output, format, 100));
    return bytes;
}

QByteArray jpegImage(const QImage &image) {
    auto bytes = encodedImage(image, "JPEG");
    assert(bytes.startsWith(QByteArray::fromHex("ffd8")));
    // The generated EXIF entry requests a 90-degree rotation. The decoder must
    // ignore it: QVideoFrame's explicit surface/presentation transforms own that.
    bytes.insert(2,
                 QByteArray::fromHex(
                     "ffe1002245786966000049492a0008000000010012010300010000000600000000000000"));
    QBuffer input(&bytes);
    assert(input.open(QIODevice::ReadOnly));
    QImageReader reader(&input, "jpeg");
    reader.setAutoTransform(false);
    assert(reader.size() == image.size());
    assert(reader.transformation() == QImageIOHandler::TransformationRotate90);
    return bytes;
}

QVideoFrame jpegFrame(const QByteArray &bytes, const QVideoFrameFormat &format) {
    assert(format.pixelFormat() == QVideoFrameFormat::Format_Jpeg);
    QVideoFrame frame(format);
    assert(frame.isValid() && frame.map(QVideoFrame::WriteOnly));
    assert(frame.planeCount() == 1 && frame.bits(0) && bytes.size() <= frame.mappedBytes(0));
    // Public Qt frame storage is larger than this compressed payload. Trailing
    // zero padding is intentional; the reader must stop at the JPEG end marker.
    std::memset(frame.bits(0), 0, size_t(frame.mappedBytes(0)));
    std::memcpy(frame.bits(0), bytes.constData(), size_t(bytes.size()));
    frame.unmap();
    return frame;
}

void assertConstant(const QByteArray &pixels, int expected) {
    assert(pixels.size() == 128 * 112);
    for (unsigned char pixel : pixels)
        assert(pixel == expected);
}

void test_luminance_and_stride() {
    const QVideoFrameFormat::PixelFormat formats[] = {
        QVideoFrameFormat::Format_Y8,   QVideoFrameFormat::Format_NV12,
        QVideoFrameFormat::Format_NV21, QVideoFrameFormat::Format_YUV420P,
        QVideoFrameFormat::Format_YV12, QVideoFrameFormat::Format_YUV422P,
        QVideoFrameFormat::Format_UYVY, QVideoFrameFormat::Format_YUYV};
    for (const auto format : formats) {
        bool direct = false;
        const auto pixels = cameraSensorPixels(luminanceFrame(format, QSize(130, 114)), &direct);
        assert(direct && pixels.size() == 128 * 112);
        for (int y = 0; y < 112; ++y)
            for (int x = 0; x < 128; ++x)
                assert(uchar(pixels[y * 128 + x]) == ((x + 1 + 3 * (y + 1)) & 255));
        // Limited-range Y remains 16, rather than being expanded to RGB black.
        assertConstant(cameraSensorPixels(luminanceFrame(format, QSize(130, 114), 16)), 16);
        assertConstant(cameraSensorPixels(luminanceFrame(format, QSize(130, 114), 235)), 235);
    }
    const auto inverted =
        cameraSensorPixels(luminanceFrame(QVideoFrameFormat::Format_Y8, QSize(130, 114), -1, true));
    for (int y = 0; y < 112; ++y)
        for (int x = 0; x < 128; ++x)
            assert(uchar(inverted[y * 128 + x]) == ((x + 1 + 3 * (112 - y)) & 255));
    const auto mirroredFrame =
        luminanceFrame(QVideoFrameFormat::Format_Y8, QSize(130, 114), -1, false, true);
    const auto mirrored = cameraSensorPixels(mirroredFrame);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const bool formatMirrorApplied = true;
#else
    // Older releases differ in whether the format flag aliases the frame flag.
    // Their public frame orientation is the effective presentation contract.
    const bool formatMirrorApplied = mirroredFrame.mirrored();
#endif
    for (int y = 0; y < 112; ++y)
        for (int x = 0; x < 128; ++x)
            assert(uchar(mirrored[y * 128 + x]) ==
                   (((formatMirrorApplied ? 128 - x : x + 1) + 3 * (y + 1)) & 255));
}

void transformCorners(std::array<int, 4> &corners, int rotation, bool mirror) {
    const unsigned quadrants[4][4] = {{0, 1, 2, 3}, {2, 0, 3, 1}, {3, 2, 1, 0}, {1, 3, 0, 2}};
    const auto before = corners;
    for (unsigned i = 0; i < 4; ++i)
        corners[i] = before[quadrants[rotation][i ^ unsigned(mirror)]];
}

void checkOrientation(int colorPath, bool bottomUp, int rotation, bool mirror, int surfaceRotation,
                      bool surfaceMirror) {
    const bool rgb = colorPath == 1 || colorPath == 2;
    const int luminance[4] = {16, 64, 128, 235};
    const QRgb colors[4] = {qRgb(255, 0, 0), qRgb(0, 255, 0), qRgb(0, 0, 255), qRgb(231, 177, 98)};
    auto pixelFormat = rgb ? QVideoFrameFormat::pixelFormatFromImageFormat(QImage::Format_ARGB32)
                           : QVideoFrameFormat::Format_Y8;
    if (colorPath == 2) {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        pixelFormat = QVideoFrameFormat::Format_ARGB8888;
#else
        pixelFormat = QVideoFrameFormat::Format_BGRA8888;
#endif
        // This opposite-byte-order format has no direct QImage representation,
        // so it must exercise the metadata-neutral Qt color decoder instead.
        assert(QVideoFrameFormat::imageFormatFromPixelFormat(pixelFormat) ==
               QImage::Format_Invalid);
    }
    if (colorPath == 3)
        pixelFormat = QVideoFrameFormat::Format_Jpeg;
    QVideoFrameFormat format(QSize(130, 114), pixelFormat);
    format.setScanLineDirection(bottomUp ? QVideoFrameFormat::BottomToTop
                                         : QVideoFrameFormat::TopToBottom);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    format.setRotation(QtVideo::Rotation(surfaceRotation * 90));
    format.setMirrored(surfaceMirror);
#endif
    // Every case owns fresh storage: toImage caches its first conversion on
    // older Qt releases, and copied QVideoFrames share that cache and metadata.
    QVideoFrame frame;
    if (colorPath == 3) {
        QImage gray(format.frameSize(), QImage::Format_Grayscale8);
        assert(!gray.isNull());
        for (int y = 0; y < gray.height(); ++y)
            for (int x = 0; x < gray.width(); ++x)
                gray.scanLine(y)[x] = uchar(luminance[(y >= 57 ? 2 : 0) + (x >= 65 ? 1 : 0)]);
        frame = jpegFrame(jpegImage(gray), format);
    } else {
        frame = QVideoFrame(format);
        assert(frame.isValid() && frame.map(QVideoFrame::WriteOnly));
        for (int y = 0; y < 114; ++y) {
            auto *row = frame.bits(0) + y * frame.bytesPerLine(0);
            for (int x = 0; x < 130; ++x) {
                const int index = (y >= 57 ? 2 : 0) + (x >= 65 ? 1 : 0);
                if (colorPath == 2) {
                    auto *pixel = row + x * 4;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
                    pixel[0] = 255;
                    pixel[1] = uchar(qRed(colors[index]));
                    pixel[2] = uchar(qGreen(colors[index]));
                    pixel[3] = uchar(qBlue(colors[index]));
#else
                    pixel[0] = uchar(qBlue(colors[index]));
                    pixel[1] = uchar(qGreen(colors[index]));
                    pixel[2] = uchar(qRed(colors[index]));
                    pixel[3] = 255;
#endif
                } else if (rgb)
                    reinterpret_cast<QRgb *>(row)[x] = colors[index];
                else
                    row[x] = uchar(luminance[index]);
            }
        }
        frame.unmap();
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    frame.setRotation(QtVideo::Rotation(rotation * 90));
#else
    frame.setRotationAngle(QVideoFrame::RotationAngle(rotation * 90));
#endif
    frame.setMirrored(mirror);
    std::array<int, 4> corners;
    for (unsigned i = 0; i < 4; ++i)
        corners[i] =
            rgb ? qGray(colors[i ^ (bottomUp ? 2 : 0)]) : luminance[i ^ (bottomUp ? 2 : 0)];
    transformCorners(corners, surfaceRotation, surfaceMirror);
    transformCorners(corners, rotation, mirror);
    bool direct = colorPath == 0;
    const auto pixels = cameraSensorPixels(frame, &direct);
    assert(direct == (colorPath == 0) && pixels.size() == 128 * 112);
    const int x[2] = {20, 107}, y[2] = {20, 91};
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const int observed = uchar(pixels[y[row] * 128 + x[column]]);
            const int expected = corners[row * 2 + column];
            if (observed != expected)
                std::fprintf(stderr,
                             "Camera orientation mismatch: Qt=%s color_path=%d rgb=%d bottom_up=%d "
                             "frame_rotation=%d frame_mirror=%d surface_rotation=%d "
                             "surface_mirror=%d x=%d y=%d observed=%d expected=%d\n",
                             qVersion(), colorPath, int(rgb), int(bottomUp), rotation * 90,
                             int(mirror), surfaceRotation * 90, int(surfaceMirror), x[column],
                             y[row], observed, expected);
            assert(observed == expected);
        }
    }
}

void test_orientation() {
    for (int colorPath : {0, 1, 2, 3})
        for (bool bottomUp : {false, true})
            for (int rotation = 0; rotation < 4; ++rotation)
                for (bool mirror : {false, true}) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
                    for (int surfaceRotation = 0; surfaceRotation < 4; ++surfaceRotation)
                        for (bool surfaceMirror : {false, true})
                            checkOrientation(colorPath, bottomUp, rotation, mirror, surfaceRotation,
                                             surfaceMirror);
#else
                    checkOrientation(colorPath, bottomUp, rotation, mirror, 0, false);
#endif
                }
}

void test_subsampled_geometry() {
    const QVideoFrameFormat::PixelFormat planar420[] = {
        QVideoFrameFormat::Format_YUV420P, QVideoFrameFormat::Format_YUV420P10,
        QVideoFrameFormat::Format_YV12,    QVideoFrameFormat::Format_NV12,
        QVideoFrameFormat::Format_NV21,    QVideoFrameFormat::Format_IMC1,
        QVideoFrameFormat::Format_IMC2,    QVideoFrameFormat::Format_IMC3,
        QVideoFrameFormat::Format_IMC4,    QVideoFrameFormat::Format_P010,
        QVideoFrameFormat::Format_P016};
    for (const auto format : planar420)
        for (const QSize size : {QSize(1, 1), QSize(2, 1), QSize(1, 2), QSize(3, 2), QSize(2, 3),
                                 QSize(129, 114), QSize(130, 113)}) {
            // Do not map this fixture: tiny planar heights can fault inside Qt.
            // Conversion must reject it before asking Qt to derive UV planes.
            const QVideoFrame frame(QVideoFrameFormat(size, format));
            assert(frame.isValid());
            bool direct = true;
            assert(cameraSensorPixels(frame, &direct).isEmpty() && !direct);
        }
    for (const auto format : {QVideoFrameFormat::Format_YUV422P, QVideoFrameFormat::Format_UYVY,
                              QVideoFrameFormat::Format_YUYV}) {
        for (const QSize size : {QSize(1, 1), QSize(3, 1), QSize(3, 2)})
            assert(cameraSensorPixels(QVideoFrame(QVideoFrameFormat(size, format))).isEmpty());
        assertConstant(cameraSensorPixels(luminanceFrame(format, QSize(2, 1), 37)), 37);
    }
}

void test_jpeg_bounds() {
    QImage gray(QSize(129, 113), QImage::Format_Grayscale8);
    assert(!gray.isNull());
    gray.fill(37);
    const auto bytes = jpegImage(gray);
    const QVideoFrameFormat format(gray.size(), QVideoFrameFormat::Format_Jpeg);
    // JPEG's own chroma padding supports odd image dimensions; only raw
    // subsampled buffers require complete rows and columns before Qt mapping.
    assertConstant(cameraSensorPixels(jpegFrame(bytes, format)), 37);
    for (const auto &invalid :
         {QByteArray("not a JPEG"), bytes.first(16), encodedImage(gray, "PNG")})
        assert(cameraSensorPixels(jpegFrame(invalid, format)).isEmpty());
    assert(cameraSensorPixels(
               jpegFrame(bytes, QVideoFrameFormat(QSize(128, 113), QVideoFrameFormat::Format_Jpeg)))
               .isEmpty());

    for (const QSize reported : {QSize(16385, 113), QSize(8192, 8193)}) {
        auto oversized = bytes;
        const auto marker = oversized.indexOf(QByteArray::fromHex("ffc0"));
        assert(marker >= 0 && marker + 8 < oversized.size());
        oversized[marker + 5] = char(reported.height() >> 8);
        oversized[marker + 6] = char(reported.height() & 255);
        oversized[marker + 7] = char(reported.width() >> 8);
        oversized[marker + 8] = char(reported.width() & 255);
        assert(cameraSensorPixels(jpegFrame(oversized, format)).isEmpty());
    }
    // Bound both advertised geometry and the mapped compressed byte span.
    const QVideoFrame wide(QVideoFrameFormat(QSize(16385, 1), QVideoFrameFormat::Format_Jpeg));
    assert(cameraSensorPixels(wide).isEmpty());
    QVideoFrame large(QVideoFrameFormat(QSize(4097, 4096), QVideoFrameFormat::Format_Jpeg));
    assert(large.isValid() && large.map(QVideoFrame::ReadOnly));
    assert(large.mappedBytes(0) > 64 * 1024 * 1024);
    large.unmap();
    assert(cameraSensorPixels(large).isEmpty());
}

void test_resize_and_rgb() {
    for (const QSize size : {QSize(1, 1), QSize(271, 119), QSize(119, 271), QSize(1920, 1080)})
        assertConstant(cameraSensorPixels(luminanceFrame(QVideoFrameFormat::Format_Y8, size, 37)),
                       37);
    QVideoFrame tiny = luminanceFrame(QVideoFrameFormat::Format_Y8, QSize(2, 2), 0);
    assert(tiny.map(QVideoFrame::WriteOnly));
    tiny.bits(0)[1] = tiny.bits(0)[tiny.bytesPerLine(0) + 1] = 255;
    tiny.unmap();
    const auto interpolated = cameraSensorPixels(tiny);
    assert(uchar(interpolated[56 * 128 + 63]) > 96 && uchar(interpolated[56 * 128 + 63]) < 160);
    assert(uchar(interpolated[56 * 128 + 64]) > 96 && uchar(interpolated[56 * 128 + 64]) < 160);
    assert(interpolated[56 * 128 + 63] != interpolated[56 * 128 + 64]);
    for (const QRgb color :
         {qRgb(255, 0, 0), qRgb(0, 255, 0), qRgb(0, 0, 255), qRgb(17, 89, 201), qRgb(16, 16, 16)}) {
        bool direct = true;
        const auto pixels = cameraSensorPixels(rgbFrame(QSize(130, 114), color), &direct);
        assert(!direct);
        assertConstant(pixels, qGray(color));
    }
    bool direct = true;
    assert(cameraSensorPixels(QVideoFrame(), &direct).isEmpty() && !direct);
}

void test_request_lifetimes() {
    std::vector<HostCamera::FrameReady> replies;
    unsigned cancellations = 0, completions = 0;
    QByteArray accepted;
    QString lastError;
    auto camera = std::make_unique<HostCamera>(
        nullptr, [&replies](HostCamera::FrameReady reply) { replies.push_back(std::move(reply)); },
        [&cancellations] { ++cancellations; });
    assert(replies.empty());
    auto complete = [&](const QByteArray &pixels, const QString &error) {
        ++completions;
        accepted = pixels;
        lastError = error;
    };
    camera->request(complete);
    assert(replies.size() == 1 && !completions);
    camera->cancel();
    camera->request(complete);
    assert(replies.size() == 2);
    replies[0](luminanceFrame(QVideoFrameFormat::Format_Y8, QSize(130, 114), 17), {});
    assert(!completions);
    replies[1](luminanceFrame(QVideoFrameFormat::Format_Y8, QSize(130, 114), 91), {});
    assert(completions == 1 && lastError.isEmpty());
    assertConstant(accepted, 91);
    replies[1](luminanceFrame(QVideoFrameFormat::Format_Y8, QSize(130, 114), 42), {});
    assert(completions == 1);
    camera->request(complete);
    replies.back()({}, QStringLiteral("permission denied"));
    assert(completions == 2 && accepted.isEmpty() && lastError == "permission denied");
    camera->request(complete);
    replies.back()({}, {});
    assert(completions == 3 && accepted.isEmpty() && !lastError.isEmpty());
    camera->request(complete);
    const auto abandoned = replies.back();
    camera.reset();
    abandoned(luminanceFrame(QVideoFrameFormat::Format_Y8, QSize(130, 114), 201), {});
    assert(completions == 3 && cancellations >= 4);

    HostCamera immediate(
        nullptr, [](HostCamera::FrameReady reply) { reply({}, QStringLiteral("no camera")); });
    immediate.request(complete);
    assert(completions == 4 && lastError == "no camera");

    QEventLoop loop;
    bool timedOut = false;
    HostCamera timeout(nullptr, [](HostCamera::FrameReady) {}, {}, 1);
    timeout.request([&](const QByteArray &pixels, const QString &error) {
        timedOut = pixels.isEmpty() && !error.isEmpty();
        loop.quit();
    });
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();
    assert(timedOut);
}
} // namespace

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    test_luminance_and_stride();
    test_orientation();
    test_subsampled_geometry();
    test_jpeg_bounds();
    test_resize_and_rgb();
    test_request_lifetimes();
    return 0;
}

#include "wave_recorder.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstring>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "Cannot create recording test directory"))
        return 1;
    const auto path = directory.filePath(QString::fromUtf8("audio-\xc3\xa9.wav"));
    QFile previous(path);
    if (!previous.open(QIODevice::WriteOnly) || previous.write("keep") != 4)
        return 1;
    previous.close();
    const int16_t samples[] = {-32768, 32767, 1234, -5678, 0, 1};
    WaveRecorder recorder;
    if (!check(!recorder.start(path, 0), "Zero sample rate was accepted") ||
        !check(read(path) == "keep", "Invalid start modified the destination") ||
        !check(recorder.start(path, 48000), "Cannot start audio recording") ||
        !check(recorder.append(samples, 1) && recorder.append(samples + 2, 2),
               "Cannot append recording blocks") ||
        !check(read(path) == "keep", "Uncommitted recording replaced existing audio") ||
        !check(!recorder.start(path, 44100) && recorder.active(),
               "Repeated start discarded an active recording") ||
        !check(recorder.framesWritten() == 3 && recorder.sampleRate() == 48000,
               "Incorrect recording sample accounting") ||
        !check(recorder.finish() && !recorder.active(), "Cannot finalize recording"))
        return 1;
    const auto bytes = read(path);
    if (!check(bytes.size() == 56 && bytes.first(4) == "RIFF" && bytes.mid(8, 8) == "WAVEfmt " &&
                   bytes.mid(36, 4) == "data",
               "Invalid WAVE container") ||
        !check(qFromLittleEndian<quint32>(bytes.constData() + 4) == 48 &&
                   qFromLittleEndian<quint32>(bytes.constData() + 40) == 12 &&
                   qFromLittleEndian<quint32>(bytes.constData() + 24) == 48000 &&
                   qFromLittleEndian<quint32>(bytes.constData() + 28) == 192000 &&
                   qFromLittleEndian<quint16>(bytes.constData() + 20) == 1 &&
                   qFromLittleEndian<quint16>(bytes.constData() + 22) == 2 &&
                   qFromLittleEndian<quint16>(bytes.constData() + 32) == 4 &&
                   qFromLittleEndian<quint16>(bytes.constData() + 34) == 16,
               "Incorrect PCM format or finalized chunk sizes"))
        return 1;
    for (size_t i = 0; i < 6; ++i)
        if (!check(qFromLittleEndian<quint16>(bytes.constData() + 44 + i * 2) ==
                       quint16(samples[i]),
                   "Recorded PCM changed channel order, byte order, or amplitude"))
            return 1;
    if (!check(recorder.start(path, 44100), "Cannot restart recording") ||
        !check(!recorder.append(nullptr, size_t(WaveRecorder::maximumFrames + 1)),
               "RIFF overflow was accepted") ||
        !check(recorder.active() && recorder.framesWritten() == 0 && recorder.finish(),
               "Size limit discarded the valid prefix") ||
        !check(read(path).size() == 44, "Empty recording has an invalid size"))
        return 1;
    {
        WaveRecorder abandoned;
        if (!check(abandoned.start(path, 44100) && abandoned.append(samples, 3),
                   "Cannot start disposable recording"))
            return 1;
    }
    if (!check(read(path).size() == 44, "Unfinished recording damaged the prior destination") ||
        !check(!recorder.start(directory.filePath("missing/audio.wav"), 44100) &&
                   !recorder.active() && !recorder.errorString().isEmpty(),
               "Unwritable recording path was accepted"))
        return 1;
    std::cout << "CupidGB WAVE recording regression passed\n";
    return 0;
}

/**
 * @file    cartridge_file.cpp
 * @brief   Cartridge file loading and bounded archive extraction.
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

#include "cartridge_file.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <new>

extern "C" {
#include "cupid/gb/gb.h"
}

namespace {
constexpr qint64 minimumRomSize = 0x150;
constexpr qint64 maximumListingSize = 1024 * 1024;
constexpr int archiveTimeoutMs = 10000;

bool validRomSize(qint64 size) {
    return size >= minimumRomSize && size <= CUPID_GB_MAX_ROM_SIZE;
}

bool isCartridgeSuffix(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "gb" || suffix == "gbc" || suffix == "sgb";
}

bool isArchiveSuffix(const QString &suffix) {
    return suffix == "zip" || suffix == "7z" || suffix == "rar";
}

QString archiveTool() {
    for (const QString &name : {QStringLiteral("7zz"), QStringLiteral("7z")}) {
        const QString executable = QStandardPaths::findExecutable(name);
        if (!executable.isEmpty())
            return executable;
    }
#ifdef _WIN32
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    if (!programFiles.isEmpty()) {
        const QFileInfo installed(QDir(programFiles).filePath("7-Zip/7z.exe"));
        if (installed.isFile())
            return installed.absoluteFilePath();
    }
#endif
    return {};
}

bool runArchiveTool(const QString &executable, const QStringList &arguments, qint64 limit,
                    QByteArray &output, QString &error) {
    QProcess process;
    QElapsedTimer elapsed;
    elapsed.start();
    process.start(executable, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(archiveTimeoutMs)) {
        error = "Could not start the archive reader.";
        return false;
    }
    process.closeWriteChannel();
    while (true) {
        output += process.read(limit - output.size() + 1);
        if (output.size() > limit) {
            error = "Archive output exceeds the expected size.";
            break;
        }
        if (process.state() == QProcess::NotRunning) {
            if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
                return true;
            error =
                "Could not read the archive. Check its integrity, encryption, and codec support.";
            return false;
        }
        const qint64 remaining = archiveTimeoutMs - elapsed.elapsed();
        if (remaining <= 0) {
            error = "The archive reader timed out.";
            break;
        }
        process.waitForReadyRead(int(remaining));
    }
    process.kill();
    process.waitForFinished();
    return false;
}

QByteArray readArchive(const QFileInfo &file, QString &error) {
    const QString executable = archiveTool();
    if (executable.isEmpty()) {
        error = "Opening compressed cartridges requires 7-Zip (7z or 7zz).";
        return {};
    }

    const QString path = file.absoluteFilePath();
    QByteArray listing;
    if (!runArchiveTool(executable, {"l", "-slt", "-ba", "-sccUTF-8", "-bse0", "--", path},
                        maximumListingSize, listing, error))
        return {};

    QString member;
    qint64 size = 0;
    bool encrypted = false;
    QString description = QString::fromUtf8(listing);
    description.replace("\r\n", "\n");
    for (const QString &record : description.split("\n\n", Qt::SkipEmptyParts)) {
        QString entry;
        qint64 entrySize = -1;
        bool entryEncrypted = false;
        bool regular = true;
        for (const QString &line : record.split('\n')) {
            const qsizetype separator = line.indexOf(" = ");
            if (separator < 0)
                continue;
            const auto key = QStringView(line).first(separator);
            const auto value = QStringView(line).sliced(separator + 3);
            if (key == u"Path")
                entry = value.toString();
            else if (key == u"Size") {
                bool valid = false;
                const qint64 bytes = value.toLongLong(&valid);
                if (valid)
                    entrySize = bytes;
            } else if (key == u"Encrypted" && value == u"+")
                entryEncrypted = true;
            else if (((key == u"Folder" || key == u"Alternate Stream") && value == u"+") ||
                     ((key == u"Symbolic Link" || key == u"Hard Link" || key == u"Copy Link") &&
                      !value.isEmpty()))
                regular = false;
            else if (key == u"Attributes") {
                if (value.startsWith(u'D') || value.startsWith(u'l') || value.contains(u" l"))
                    regular = false;
            }
        }
        if (!regular || !isCartridgeSuffix(entry))
            continue;
        if (!member.isEmpty()) {
            error = "The archive contains more than one Game Boy cartridge.";
            return {};
        }
        member = entry;
        size = entrySize;
        encrypted = entryEncrypted;
    }
    if (member.isEmpty()) {
        error = "The archive contains no Game Boy cartridge (.gb, .gbc, or .sgb).";
        return {};
    }
    if (encrypted || !validRomSize(size)) {
        error = QStringLiteral("The archived cartridge must be unencrypted and between 336 bytes "
                               "and %1 MiB.")
                    .arg(CUPID_GB_MAX_ROM_SIZE / (1024u * 1024u));
        return {};
    }

    // Select the literal member name and stream its bytes. Nothing is extracted to disk.
    QByteArray rom;
    if (!runArchiveTool(executable, {"x", "-so", "-spd", "-bse0", "-i!" + member, "--", path}, size,
                        rom, error))
        return {};
    if (rom.size() != size) {
        error = "Could not read the complete cartridge from the archive.";
        return {};
    }
    return rom;
}

QByteArray readCartridge(const QString &path, QString &error) {
    const QFileInfo info(path);
    if (!info.isFile()) {
        error = "Cannot open ROM. Select a regular cartridge or archive file.";
        return {};
    }
    const QString suffix = info.suffix().toLower();
    if (suffix == "gba" || suffix == "agb") {
        error = "CupidGB accepts Game Boy and Game Boy Color cartridges.";
        return {};
    }
    if (isArchiveSuffix(suffix))
        return readArchive(info, error);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "Cannot open ROM: " + file.errorString();
        return {};
    }
    const qint64 size = file.size();
    if (!validRomSize(size)) {
        error = QStringLiteral("Cartridge size must be between 336 bytes and %1 MiB.")
                    .arg(CUPID_GB_MAX_ROM_SIZE / (1024u * 1024u));
        return {};
    }
    const QByteArray data = file.read(size + 1);
    if (data.size() != size) {
        error = "Could not read the complete cartridge: " + file.errorString();
        return {};
    }
    return data;
}
} // namespace

QByteArray readCartridgeFile(const QString &path, QString &error) {
    try {
        return readCartridge(path, error);
    } catch (const std::bad_alloc &) {
        error = QStringLiteral("Not enough memory to read the cartridge.");
        return {};
    }
}

QString cartridgeStorageName(const QFileInfo &file) {
    return isArchiveSuffix(file.suffix().toLower()) ? file.fileName() : file.completeBaseName();
}

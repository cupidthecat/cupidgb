/**
 * @file    huc3_alarm.cpp
 * @brief   Persistent HuC3 alarm deadlines and desktop notification delivery.
 * @author  cupidthecat
 * @date    2026-11-24
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "huc3_alarm.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

namespace huc3Alarm {

#ifdef Q_OS_WIN
bool armWindowsAlarm(const QByteArray &cartridgeId, const QByteArray &generation, qint64 due);
void disarmWindowsAlarm(const QByteArray &cartridgeId);
#elif defined(Q_OS_LINUX)
bool showLinuxAlarm(const QString &title, const std::function<void()> &open,
                    const std::function<void()> &dismiss, QObject *lifetime);
#endif

namespace {

struct Alarm {
    QString path;
    QByteArray generation;
    qint64 due;
    bool delivered = false;
};

constexpr int alarmLockTimeout = 4000;

class ActivationServer : public QLocalServer {
  public:
    ActivationServer(const QString &name, QWidget &window)
        : QLocalServer(&window), lock_(QDir(QDir::tempPath()).filePath(name + ".lock")) {
        lock_.setStaleLockTime(0);
        setSocketOptions(QLocalServer::UserAccessOption);
    }

    ~ActivationServer() override {
        close();
    }

    bool openEndpoint(const QString &name) {
        if (!lock_.tryLock(0))
            return false;
        QLocalServer::removeServer(name);
        return listen(name);
    }

  private:
    QLockFile lock_;
};

QByteArray cartridgeId(const QString &path) {
    QString key = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
}

bool validId(const QByteArray &id, qsizetype length) {
    return id.size() == length && std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

QDir alarmDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/alarms");
}

QString activationName(const QByteArray &id) {
    const auto key = alarmDirectory().absolutePath().toUtf8() + '\0' + id;
    return "cupidgb-alarm-" +
           QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex());
}

bool activateCartridge(const QByteArray &id) {
    QLocalSocket client;
    client.connectToServer(activationName(id), QIODevice::WriteOnly);
    if (client.state() != QLocalSocket::ConnectedState && !client.waitForConnected(400))
        return false;
    if (client.write("A", 1) != 1)
        return false;
    client.flush();
    return client.bytesToWrite() == 0 || client.waitForBytesWritten(400);
}

QString recordPath(const QDir &directory, const QByteArray &id) {
    return directory.filePath(QString::fromLatin1(id) + ".json");
}

QString lockPath(const QDir &directory, const QByteArray &id) {
    return directory.filePath(QString::fromLatin1(id) + ".lock");
}

std::optional<Alarm> readAlarm(const QString &file, const QByteArray &id) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 1024 * 1024)
        return {};
    const auto object = QJsonDocument::fromJson(input.readAll()).object();
    const Alarm alarm{object.value("path").toString(),
                      object.value("generation").toString().toLatin1(),
                      object.value("due").toInteger(-1), object.value("delivered").toBool()};
    if (object.value("version").toInt() != 1 || !QDir::isAbsolutePath(alarm.path) ||
        cartridgeId(alarm.path) != id || !validId(alarm.generation, 32) || alarm.due <= 0 ||
        !object.value("delivered").isBool())
        return {};
    return alarm;
}

bool writeAlarm(const QString &file, const Alarm &alarm) {
    QSaveFile output(file);
    const QJsonObject object{{"version", 1},
                             {"path", alarm.path},
                             {"generation", QString::fromLatin1(alarm.generation)},
                             {"due", alarm.due},
                             {"delivered", alarm.delivered}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() &&
           output.commit();
}

QStringList deliveryArguments(const QByteArray &id, const QByteArray &generation) {
    return {"--deliver-huc3-alarm", QString::fromLatin1(id), QString::fromLatin1(generation)};
}

#ifdef Q_OS_LINUX
QString timerUnit(const QByteArray &id, const QByteArray &generation) {
    return "cupidgb-huc3-" + QString::fromLatin1(id) + '-' + QString::fromLatin1(generation);
}

bool runUserServiceCommand(const QString &program, const QStringList &arguments) {
    QProcess command;
    command.setStandardOutputFile(QProcess::nullDevice());
    command.setStandardErrorFile(QProcess::nullDevice());
    command.start(program, arguments);
    if (!command.waitForStarted(500))
        return false;
    if (!command.waitForFinished(1500)) {
        command.kill();
        command.waitForFinished(500);
        return false;
    }
    return command.exitStatus() == QProcess::NormalExit && command.exitCode() == 0;
}
#endif

bool arm(const QByteArray &id, const Alarm &alarm) {
#ifdef Q_OS_WIN
    if (armWindowsAlarm(id, alarm.generation, alarm.due))
        return true;
#elif defined(Q_OS_LINUX)
    // Keep a reopened cartridge alive after the notification helper exits.
    QStringList args{"--user",
                     "--quiet",
                     "--collect",
                     "--unit=" + timerUnit(id, alarm.generation),
                     "--on-calendar=@" + QString::number(alarm.due),
                     "--property=ExitType=cgroup",
                     "--timer-property=AccuracySec=250ms",
                     "--timer-property=RemainAfterElapse=false"};
    // The desktop session can differ from the login environment of the user manager.
    for (const char *name :
         {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_DATA_HOME", "XDG_RUNTIME_DIR",
          "DBUS_SESSION_BUS_ADDRESS", "QT_QPA_PLATFORM", "TMPDIR"}) {
        if (qEnvironmentVariableIsSet(name))
            args.append("--setenv=" + QString::fromLatin1(name) + '=' + qEnvironmentVariable(name));
    }
    args << "--" << QCoreApplication::applicationFilePath()
         << deliveryArguments(id, alarm.generation);
    if (runUserServiceCommand("systemd-run", args))
        return true;
#endif
    return QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                   deliveryArguments(id, alarm.generation));
}

void disarm(const QByteArray &id, const QByteArray &generation) {
#ifdef Q_OS_WIN
    Q_UNUSED(generation);
    disarmWindowsAlarm(id);
#elif defined(Q_OS_LINUX)
    runUserServiceCommand("systemctl", {"--user", "stop", timerUnit(id, generation) + ".timer"});
#else
    Q_UNUSED(id);
    Q_UNUSED(generation);
#endif
}

QString alarmTitle(const QString &path) {
    QString title = QFileInfo(path).completeBaseName();
    static const QRegularExpression tags(R"(\([^()]+\)|\[[^\[\]]+\])");
    title.remove(tags);
    title = title.trimmed();
    return title.isEmpty() ? QFileInfo(path).fileName() : title;
}

} // namespace

void schedule(const QString &cartridgePath, qint64 dueUtcSeconds) {
    if (!QDir::isAbsolutePath(cartridgePath) || dueUtcSeconds <= 0)
        return;
    const auto directory = alarmDirectory();
    if (!directory.mkpath(".")) {
        qWarning("Cannot create the cartridge alarm directory.");
        return;
    }
    const auto id = cartridgeId(cartridgePath);
    const auto file = recordPath(directory, id);
    QLockFile lock(lockPath(directory, id));
    if (!lock.tryLock(alarmLockTimeout)) {
        qWarning("Cannot lock the cartridge alarm record.");
        return;
    }
    const auto previous = readAlarm(file, id);
    const Alarm alarm{cartridgePath, QUuid::createUuid().toByteArray(QUuid::Id128), dueUtcSeconds};
    if (!writeAlarm(file, alarm)) {
        qWarning("Cannot save the cartridge alarm deadline.");
        return;
    }
    if (previous)
        disarm(id, previous->generation);
    if (!arm(id, alarm)) {
        QFile::remove(file);
        qWarning("Cannot register the cartridge alarm deadline.");
    }
}

void cancel(const QString &cartridgePath) {
    if (!QDir::isAbsolutePath(cartridgePath))
        return;
    const auto directory = alarmDirectory();
    if (!directory.exists())
        return;
    const auto id = cartridgeId(cartridgePath);
    const auto file = recordPath(directory, id);
    QLockFile lock(lockPath(directory, id));
    if (!lock.tryLock(alarmLockTimeout)) {
        qWarning("Cannot lock the cartridge alarm record.");
        return;
    }
    const auto alarm = readAlarm(file, id);
    if (alarm) {
        if (!QFile::remove(file)) {
            qWarning("Cannot remove the cartridge alarm record.");
            return;
        }
        disarm(id, alarm->generation);
    }
}

QObject *listenForActivation(const QString &cartridgePath, QWidget &window) {
    const QString name = activationName(cartridgeId(cartridgePath));
    auto receiver = std::make_unique<ActivationServer>(name, window);
    if (!receiver->openEndpoint(name))
        return nullptr;
    QObject::connect(
        receiver.get(), &QLocalServer::newConnection, &window, [server = receiver.get(), &window] {
            while (auto *socket = server->nextPendingConnection()) {
                const auto request = [socket, &window] {
                    if (socket->read(1) == "A") {
                        window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
                        window.show();
                        window.raise();
                        window.activateWindow();
                    }
                    socket->deleteLater();
                };
                QObject::connect(socket, &QLocalSocket::readyRead, socket, request);
                if (socket->bytesAvailable())
                    request();
                QTimer::singleShot(1000, socket, &QObject::deleteLater);
            }
        });
    return receiver.release();
}

int deliver(QApplication &app, const QString &idArgument, const QString &generationArgument) {
    const auto id = idArgument.toLatin1();
    const auto generation = generationArgument.toLatin1();
    if (!validId(id, 64) || !validId(generation, 32))
        return 1;
    const auto directory = alarmDirectory();
    const auto file = recordPath(directory, id);
    const auto pending = readAlarm(file, id);
    if (!pending || pending->generation != generation || pending->delivered)
        return 0;

    app.setQuitOnLastWindowClosed(false);
    QTimer timer;
    timer.setSingleShot(true);
    timer.setTimerType(Qt::PreciseTimer);
    QFileSystemWatcher watcher({directory.absolutePath()});
    QMenu menu;
    QSystemTrayIcon tray(app.windowIcon());
    QMessageBox dialog;
    QObject lifetime;
    bool showing = false;
    bool finishing = false;

    const auto finish = [&](bool open) {
        if (finishing)
            return;
        finishing = true;
        timer.stop();
        QString path;
        {
            QLockFile lock(lockPath(directory, id));
            if (lock.tryLock(alarmLockTimeout)) {
                const auto alarm = readAlarm(file, id);
                // The notification can be clicked while a different process is resuming play.
                if (alarm && alarm->generation == generation && alarm->delivered &&
                    QFile::remove(file))
                    path = alarm->path;
            }
        }
        if (open && !path.isEmpty() && !activateCartridge(id))
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {path});
        app.quit();
    };
    const auto open = [&] { finish(true); };
    const auto dismiss = [&] { finish(false); };
    QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &lifetime, [&] {
        if (!finishing)
            timer.start(0);
    });
    QObject::connect(&timer, &QTimer::timeout, &lifetime, [&] {
        auto alarm = readAlarm(file, id);
        if (!alarm || alarm->generation != generation || (alarm->delivered && !showing)) {
            app.quit();
            return;
        }
        const qint64 remaining = alarm->due - QDateTime::currentSecsSinceEpoch();
        if (showing || remaining > 0) {
            timer.start(showing ? 1000 : int(std::min<qint64>(remaining, 60)) * 1000);
            return;
        }

        QLockFile lock(lockPath(directory, id));
        if (!lock.tryLock(0)) {
            timer.start(250);
            return;
        }
        alarm = readAlarm(file, id);
        if (!alarm || alarm->generation != generation || alarm->delivered)
            return;
        alarm->delivered = true;
        if (!writeAlarm(file, *alarm)) {
            qWarning("Cannot mark the cartridge alarm delivered.");
            app.exit(1);
            return;
        }

        // Publish under the same lock as cancellation. Afterward the helper monitors
        // the record and withdraws its notification when play resumes.
        showing = true;
        timer.start(1000);
        const QString title = alarmTitle(alarm->path);
#ifdef Q_OS_LINUX
        if (showLinuxAlarm(title, open, dismiss, &lifetime))
            return;
#endif
        const QString message = QObject::tr("%1 played its scheduled alarm.").arg(title);
        if (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages()) {
            menu.addAction(QObject::tr("Open Cartridge"), &lifetime, open);
            menu.addAction(QObject::tr("Dismiss"), &lifetime, dismiss);
            tray.setContextMenu(&menu);
            tray.setToolTip(message);
            QObject::connect(&tray, &QSystemTrayIcon::messageClicked, &lifetime, open);
            tray.show();
            tray.showMessage(QObject::tr("CupidGB Cartridge Alarm"), message, app.windowIcon());
            return;
        }

        dialog.setWindowTitle(QObject::tr("CupidGB Cartridge Alarm"));
        dialog.setTextFormat(Qt::PlainText);
        dialog.setText(message);
        auto *openButton = dialog.addButton(QObject::tr("Open Cartridge"), QMessageBox::AcceptRole);
        dialog.addButton(QMessageBox::Close);
        QObject::connect(&dialog, &QMessageBox::finished, &lifetime,
                         [&, openButton] { finish(dialog.clickedButton() == openButton); });
        QApplication::beep();
        dialog.show();
    });
    timer.start(0);
    return app.exec();
}

} // namespace huc3Alarm

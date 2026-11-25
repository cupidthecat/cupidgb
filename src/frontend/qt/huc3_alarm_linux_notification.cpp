/**
 * @file    huc3_alarm_linux_notification.cpp
 * @brief   Linux desktop notification delivery for stopped HuC3 cartridge alarms.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVariantMap>

#include <functional>
#include <utility>

namespace huc3Alarm {
namespace {

const QString service = QStringLiteral("org.freedesktop.Notifications");
const QString objectPath = QStringLiteral("/org/freedesktop/Notifications");
const QString appId = QStringLiteral("io.github.cupidthecat.CupidGB");
constexpr int replyTimeoutMs = 1500;

QDBusMessage request(const QString &owner, const QString &method) {
    return QDBusMessage::createMethodCall(owner, objectPath, service, method);
}

class LinuxAlarmNotification final : public QObject {
    Q_OBJECT

  public:
    LinuxAlarmNotification(const QDBusConnection &connection, const QString &owner,
                           std::function<void()> open, std::function<void()> dismiss,
                           QObject *lifetime)
        : QObject(lifetime), connection_(connection), owner_(owner), open_(std::move(open)),
          dismiss_(std::move(dismiss)) {
        auto *watcher = new QDBusServiceWatcher(service, connection_,
                                                QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
                [this](const QString &, const QString &, const QString &owner) {
                    if (owner != owner_)
                        finish(false);
                });
    }

    ~LinuxAlarmNotification() override {
        close();
    }

    bool show(const QString &title) {
        if (!connection_.connect(owner_, objectPath, service, QStringLiteral("ActionInvoked"), this,
                                 SLOT(actionInvoked(uint, QString))) ||
            !connection_.connect(owner_, objectPath, service, QStringLiteral("NotificationClosed"),
                                 this, SLOT(notificationClosed(uint, uint))))
            return false;

        auto message = request(owner_, QStringLiteral("Notify"));
        const QVariantMap hints = {{QStringLiteral("desktop-entry"), appId},
                                   {QStringLiteral("suppress-sound"), false}};
        message.setArguments(
            {QStringLiteral("CupidGB"), uint(0), appId, title + QStringLiteral(" played an alarm"),
             QStringLiteral("Open the cartridge to respond to its alarm."),
             QStringList{QStringLiteral("default"), QStringLiteral("Open cartridge")}, hints,
             int(-1)});
        const QDBusReply<uint> reply = connection_.call(message, QDBus::Block, replyTimeoutMs);
        if (!reply.isValid() || !reply.value())
            return false;
        id_ = reply.value();
        return true;
    }

  private slots:

    void actionInvoked(uint id, const QString &action) {
        if (id_ && id == id_ && action == QStringLiteral("default"))
            finish(true);
    }

    void notificationClosed(uint id, uint) {
        if (id_ && id == id_) {
            id_ = 0;
            finish(false);
        }
    }

  private:
    void close() {
        if (!id_)
            return;
        auto message = request(owner_, QStringLiteral("CloseNotification"));
        message.setArguments({id_});
        id_ = 0;
        connection_.send(message);
    }

    void finish(bool opened) {
        if (finished_)
            return;
        finished_ = true;
        auto callback = opened ? std::move(open_) : std::move(dismiss_);
        open_ = {};
        dismiss_ = {};
        close();
        // Keep the active receiver and its watcher alive until dispatch returns,
        // even if the callback immediately destroys their former parent.
        setParent(nullptr);
        deleteLater();
        // The callback may destroy this object's lifetime or exit the application.
        if (callback)
            callback();
    }

    QDBusConnection connection_;
    QString owner_;
    std::function<void()> open_;
    std::function<void()> dismiss_;
    uint id_ = 0;
    bool finished_ = false;
};

} // namespace

bool showLinuxAlarm(const QString &title, const std::function<void()> &open,
                    const std::function<void()> &dismiss, QObject *lifetime) {
    if (!lifetime || lifetime->thread() != QThread::currentThread())
        return false;
    const auto connection = QDBusConnection::sessionBus();
    if (!connection.isConnected() || !connection.interface())
        return false;
    const QDBusReply<QStringList> capabilities = connection.call(
        request(service, QStringLiteral("GetCapabilities")), QDBus::Block, replyTimeoutMs);
    if (!capabilities.isValid() || !capabilities.value().contains(QStringLiteral("actions")))
        return false;
    const QDBusReply<QString> owner = connection.interface()->serviceOwner(service);
    if (!owner.isValid() || owner.value().isEmpty())
        return false;

    auto *notification =
        new LinuxAlarmNotification(connection, owner.value(), open, dismiss, lifetime);
    if (!notification->show(title)) {
        delete notification;
        return false;
    }
    return true;
}

} // namespace huc3Alarm

#include "huc3_alarm_linux_notification.moc"

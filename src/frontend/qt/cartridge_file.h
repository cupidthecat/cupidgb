#pragma once

#include <QByteArray>
#include <QString>

class QFileInfo;

QByteArray readCartridgeFile(const QString &path, QString &error);
QString cartridgeStorageName(const QFileInfo &file);

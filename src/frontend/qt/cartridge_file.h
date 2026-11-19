/**
 * @file    cartridge_file.h
 * @brief   Typed cartridge and music-file loading with stable storage names.
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

#include <QByteArray>
#include <QString>

class QFileInfo;

enum class GameMediaKind { Cartridge, Gbs };

struct GameMedia {
    QByteArray data;
    GameMediaKind kind = GameMediaKind::Cartridge;
};

GameMedia readGameMedia(const QString &path, QString &error);
QString cartridgeStorageName(const QFileInfo &file);

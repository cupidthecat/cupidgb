/**
 * @file    huc3_alarm.h
 * @brief   Qt cartridge alarm scheduling and detached delivery.
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

#pragma once

#include <QString>

class QApplication;
class QObject;
class QWidget;

namespace huc3Alarm {

void schedule(const QString &cartridgePath, qint64 dueUtcSeconds);
void cancel(const QString &cartridgePath);
QObject *listenForActivation(const QString &cartridgePath, QWidget &window);
int deliver(QApplication &app, const QString &cartridgeId, const QString &generation);

} // namespace huc3Alarm

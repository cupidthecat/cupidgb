/**
 * @file    host_elapsed_timer.h
 * @brief   Host elapsed-time interface for guest pacing and runtime measurements.
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

#include <QElapsedTimer>

class HostElapsedTimer {
  public:
    // Start before querying elapsed time. Starting again establishes a new origin.
    void start();
    bool isValid() const;
    qint64 nsecsElapsed() const;
    qint64 elapsed() const;
    static const char *sourceName();

  private:
#ifdef Q_OS_LINUX
    qint64 origin_ = 0;
    bool valid_ = false;
#else
    QElapsedTimer timer_;
#endif
};

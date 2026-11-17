/**
 * @file    host_elapsed_timer.cpp
 * @brief   Stable host clock readings for emulation pacing and performance reports.
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

#include "host_elapsed_timer.h"

#ifdef Q_OS_LINUX
#include <time.h>

namespace {
qint64 hostNanoseconds() {
    timespec sample{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &sample) != 0)
        qFatal("Could not read the host performance clock.");
    return qint64(sample.tv_sec) * 1000000000 + qint64(sample.tv_nsec);
}
} // namespace
#endif

void HostElapsedTimer::start() {
#ifdef Q_OS_LINUX
    origin_ = hostNanoseconds();
    valid_ = true;
#else
    timer_.start();
#endif
}

bool HostElapsedTimer::isValid() const {
#ifdef Q_OS_LINUX
    return valid_;
#else
    return timer_.isValid();
#endif
}

qint64 HostElapsedTimer::nsecsElapsed() const {
#ifdef Q_OS_LINUX
    // Clock corrections can change the rate of CLOCK_MONOTONIC. Guest deadlines
    // and their measurements need the same unadjusted host duration.
    return hostNanoseconds() - origin_;
#else
    return timer_.nsecsElapsed();
#endif
}

qint64 HostElapsedTimer::elapsed() const {
    return nsecsElapsed() / 1000000;
}

const char *HostElapsedTimer::sourceName() {
#ifdef Q_OS_LINUX
    return "linux_monotonic_raw";
#else
    return "qt_elapsed_timer";
#endif
}

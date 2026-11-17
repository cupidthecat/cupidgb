/**
 * @file    test_host_elapsed_timer.cpp
 * @brief   Check elapsed-time readings against enclosing platform clock samples.
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

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

#ifdef Q_OS_LINUX
#include <time.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {
qint64 platformNanoseconds() {
#ifdef Q_OS_LINUX
    timespec sample{};
    assert(clock_gettime(CLOCK_MONOTONIC_RAW, &sample) == 0);
    return qint64(sample.tv_sec) * 1000000000 + qint64(sample.tv_nsec);
#elif defined(Q_OS_WIN)
    LARGE_INTEGER ticks{}, frequency{};
    assert(QueryPerformanceFrequency(&frequency));
    assert(QueryPerformanceCounter(&ticks));
    return qint64(static_cast<long double>(ticks.QuadPart) * 1000000000 / frequency.QuadPart);
#else
    static const QElapsedTimer clock = [] {
        QElapsedTimer value;
        value.start();
        return value;
    }();
    return clock.nsecsElapsed();
#endif
}
} // namespace

int main() {
    HostElapsedTimer clock;
    assert(!clock.isValid());
    for (unsigned restart = 0; restart < 2; ++restart) {
        const qint64 startBefore = platformNanoseconds();
        clock.start();
        assert(clock.isValid());
        const qint64 startAfter = platformNanoseconds();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        const qint64 before = platformNanoseconds();
        const qint64 elapsed = clock.nsecsElapsed();
        const qint64 after = platformNanoseconds();
        // The brackets include scheduling delay. Allow only clock-conversion rounding.
        assert(elapsed >= before - startAfter - 1000);
        assert(elapsed <= after - startBefore + 1000);
        assert(clock.elapsed() >= elapsed / 1000000);
    }
    std::printf("Host elapsed-time checks passed (%s).\n", HostElapsedTimer::sourceName());
    return 0;
}

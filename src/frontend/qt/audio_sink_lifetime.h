/**
 * @file    audio_sink_lifetime.h
 * @brief   Audio-sink ownership helpers for reentrant backend calls and retirement.
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

#include <QObject>
#include <memory>
#include <utility>

// Backend calls may dispatch events that retire the current stream. Keep a
// strong local lease until the call returns so its sink-owned device survives.
template <class Sink, class... Arguments>
std::shared_ptr<Sink> createAudioSink(Arguments &&...arguments) {
    return {new Sink(std::forward<Arguments>(arguments)...), [](Sink *sink) {
                // Reset discards an obsolete timeline without waiting for a drain.
                // The last lease releases only after any backend call has returned.
                sink->reset();
                delete sink;
            }};
}

template <class Sink> void retireAudioSink(std::shared_ptr<Sink> &owner, QObject *receiver) {
    const auto retired = std::move(owner);
    if (retired)
        retired->disconnect(receiver);
}

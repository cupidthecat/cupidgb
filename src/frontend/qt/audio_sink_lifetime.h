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

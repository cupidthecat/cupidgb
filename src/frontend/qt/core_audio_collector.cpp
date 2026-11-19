/**
 * @file    core_audio_collector.cpp
 * @brief   Lossless core audio draining with bounded storage and source spans.
 * @author  cupidthecat
 * @date    2026-11-17
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "core_audio_collector.h"

extern "C" {
#include "cupid/gb/gb.h"
}

bool CoreAudioCollector::collect(CupidGb &gb) {
    const auto pending = cupid_gb_apu_pending(&gb);
    const uint64_t available = uint64_t(pending.handheld_frames) + pending.startup_frames;
    // Reject before consuming anything if this interval cannot hold the pending
    // stream. A capacity failure must leave unread output in its core owner.
    if (available > maximumFrames - frames_)
        return false;
    uint64_t remaining = available;
    while (remaining) {
        if (spanCount_ == maximumSpans)
            return false;
        CupidGbAudioOrigin origin = CUPID_GB_AUDIO_ORIGIN_HANDHELD;
        const uint32_t count = cupid_gb_apu_drain_chunk(&gb, samples_.data() + frames_ * 2u,
                                                        uint32_t(remaining), &origin);
        if (!count || count > remaining)
            return false;
        if (spanCount_ && spans_[spanCount_ - 1].origin == origin) {
            spans_[spanCount_ - 1].frames += count;
        } else {
            spans_[spanCount_++] = {origin, frames_, count};
        }
        frames_ += count;
        if (origin == CUPID_GB_AUDIO_ORIGIN_HANDHELD)
            handheldFrames_ += count;
        else
            startupFrames_ += count;
        remaining -= count;
    }
    return true;
}

bool CoreAudioCollector::needsDrain(const CupidGb &gb) {
    const auto pending = cupid_gb_apu_pending(&gb);
    return pending.startup_frames || pending.handheld_frames >= CUPID_GB_AUDIO_DRAIN_WATERMARK;
}

bool CoreAudioCollector::discard(CupidGb &gb) {
    const auto pending = cupid_gb_apu_pending(&gb);
    uint64_t remaining = uint64_t(pending.handheld_frames) + pending.startup_frames;
    int16_t samples[CUPID_GB_APU_BUF_FRAMES * 2];
    while (remaining) {
        CupidGbAudioOrigin origin;
        const auto count = cupid_gb_apu_drain_chunk(&gb, samples, CUPID_GB_APU_BUF_FRAMES, &origin);
        if (!count || count > remaining)
            return false;
        remaining -= count;
    }
    return true;
}

const int16_t *CoreAudioCollector::samples() const {
    return samples_.data();
}

uint32_t CoreAudioCollector::frames() const {
    return frames_;
}

uint32_t CoreAudioCollector::frames(CupidGbAudioOrigin origin) const {
    return origin == CUPID_GB_AUDIO_ORIGIN_HANDHELD ? handheldFrames_ : startupFrames_;
}

const CoreAudioCollector::Span *CoreAudioCollector::spans() const {
    return spans_.data();
}

size_t CoreAudioCollector::spanCount() const {
    return spanCount_;
}

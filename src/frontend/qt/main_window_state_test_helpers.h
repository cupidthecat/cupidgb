/**
 * @file    main_window_state_test_helpers.h
 * @brief   Portable guest-state comparisons for live Qt timeline regression tests.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#ifndef CUPIDGB_QT_STATE_TEST_HELPERS_H
#define CUPIDGB_QT_STATE_TEST_HELPERS_H

#include <QByteArray>
#include <memory>

extern "C" {
#include "cupid/gb/apu.h"
#include "cupid/gb/state.h"
}

inline QByteArray hardwareSnapshotForTest(const CupidGb &gb) {
    auto snapshot = std::make_unique<CupidGb>(gb);
    // Live loads retain a different output clock. Settle deferred digital clocks
    // on this copy so comparisons describe the same guest boundary, regardless
    // of when either output stream last needed a sample. Keep every guest field.
    cupid_gb_apu_run(snapshot.get(), true);
    snapshot->apu.output = {};
    // Live restores retain both receiving output owners. Canonicalize only
    // output history; hardware startup phase and presentation time stay exact.
    cupid_gb_sgb_startup_audio_reset(&snapshot->sgb_startup.output, 1);
    if (!cupid_gb_apu_set_sample_rate(snapshot.get(), CUPID_GB_APU_SAMPLE_RATE))
        return {};
    QByteArray bytes(qsizetype(cupid_gb_state_size(snapshot.get())), Qt::Uninitialized);
    size_t written = 0;
    if (cupid_gb_save_state(snapshot.get(), reinterpret_cast<uint8_t *>(bytes.data()),
                            size_t(bytes.size()), &written) != CUPID_GB_STATE_OK ||
        written != size_t(bytes.size()))
        return {};
    return bytes;
}

inline bool matchesHardwareForTest(const CupidGb &gb, const QByteArray &expected) {
    return !expected.isEmpty() && hardwareSnapshotForTest(gb) == expected;
}

#endif

/**
 * @file    main_window_reset.cpp
 * @brief   Transactional menu and console resets with persistent recording and rewind history.
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

#include "main_window_internal.h"
#include "printer_window.h"

#include <QScopeGuard>

bool MainWindow::prepareResetCandidate(CupidEmulator *candidate, ResetWorkflow workflow,
                                       bool sameStorage) {
    if (!emulator_ || !candidate)
        return false;
    const auto &previous = emulator_->gb;
    auto &next = candidate->gb;
    const auto configuredMode = next.rtc_policy.mode;
    next.rtc_policy = previous.rtc_policy;
    next.rtc_policy.mode = configuredMode;
    next.apu.output = previous.apu.output;
    next.vblank_events = previous.vblank_events;
    next.battery_write_generation = previous.battery_write_generation;
    // These are current host controls, separate from the reset JOYP and sensor
    // latches. Supply them before native reset retains its host registrations.
    next.joypad = previous.joypad;
    std::memcpy(next.joypad_extra, previous.joypad_extra, sizeof(next.joypad_extra));
    next.joyp_sample_pending = previous.joyp_sample_pending;
    next.mbc7.motion_x = previous.mbc7.motion_x;
    next.mbc7.motion_y = previous.mbc7.motion_y;
    if (sameStorage) {
        if (next.cartridge_ram_size != previous.cartridge_ram_size) {
            reportError("The cartridge RAM population changed during reset.");
            return false;
        }
        next.rtc = previous.rtc;
        next.tpp1.status = previous.tpp1.status;
        std::memcpy(next.tpp1.live, previous.tpp1.live, sizeof(next.tpp1.live));
        std::memcpy(next.tpp1.latched, previous.tpp1.latched, sizeof(next.tpp1.latched));
        next.tpp1.rtc_base_time = previous.tpp1.rtc_base_time;
        next.huc3 = previous.huc3;
        next.rtc_cycles = configuredMode == previous.rtc_policy.mode ? previous.rtc_cycles : 0;
        // The native prefix retains the complete inline span, including bytes
        // beyond a smaller cartridge's connected RAM population.
        std::memcpy(next.cartridge_ram, previous.cartridge_ram, sizeof(next.cartridge_ram));
        if (next.cartridge_ram_heap)
            std::memcpy(cupid_gb_cartridge_ram(&next), cupid_gb_cartridge_ram_const(&previous),
                        next.cartridge_ram_size);
    }
    if (workflow == ResetWorkflow::Quick) {
        // Physical console RAM survives a quick reset even when cartridge assets
        // were refreshed. Cartridge storage follows its independently checked identity.
        next.power_on = previous.power_on;
        std::memcpy(next.work_ram, previous.work_ram, sizeof(next.work_ram));
        std::memcpy(next.high_ram, previous.high_ram, sizeof(next.high_ram));
        std::memcpy(next.object_attribute_memory, previous.object_attribute_memory,
                    sizeof(next.object_attribute_memory));
        std::memcpy(next.cgb_bg_palette_ram, previous.cgb_bg_palette_ram,
                    sizeof(next.cgb_bg_palette_ram));
        std::memcpy(next.cgb_obj_palette_ram, previous.cgb_obj_palette_ram,
                    sizeof(next.cgb_obj_palette_ram));
        for (const unsigned port : {0x46u, 0x48u, 0x49u})
            next.io_registers[port] = previous.io_registers[port];
    }
    // The native operation can reset an attached printer. Keep this candidate
    // detached until the outer cartridge transaction can no longer fail.
    if (!cupid_gb_reset(&next, workflow == ResetWorkflow::Quick ? CUPID_GB_RESET_QUICK
                                                                : CUPID_GB_RESET_HARD)) {
        reportError("The console could not be reset.");
        return false;
    }
    if (cupid_gb_base_clock_hz(&previous) != cupid_gb_base_clock_hz(&next)) {
        // An ordinary Reset may select a different oscillator. Keep the stream's
        // rate and history while updating coefficients derived from that oscillator.
        cupid_gb_apu_set_sample_rate(&next, cupid_gb_apu_get_sample_rate(&previous));
    }
    next.serial_output_callback = previous.serial_output_callback;
    next.serial_output_user_data = previous.serial_output_user_data;
    next.serial_link = previous.serial_link;
    next.rumble_callback = previous.rumble_callback;
    next.rumble_user_data = previous.rumble_user_data;
    next.rumble_strength_callback = previous.rumble_strength_callback;
    next.rumble_strength_user_data = previous.rumble_strength_user_data;
    next.infrared_callback = previous.infrared_callback;
    next.infrared_user_data = previous.infrared_user_data;
    return true;
}

bool MainWindow::resetCartridge(ResetWorkflow workflow) {
    if (!emulator_)
        return false;
    QScopedValueRollback<bool> operation(stateOperation_, true);
    std::unique_ptr<CupidGbDebugger> symbols;
    const auto cleanup = qScopeGuard([&symbols] {
        if (symbols)
            cupid_gb_debugger_cleanup(symbols.get());
    });
    if (!prepareResetSymbols(symbols) ||
        !openRom(romPath_, workflow != ResetWorkflow::Normal, workflow))
        return false;
    if (symbols) {
        std::swap(debugger_->symbols, symbols->symbols);
        std::swap(debugger_->symbol_count, symbols->symbol_count);
    }
    const QString message = workflow == ResetWorkflow::Quick    ? "Quick Reset"
                            : workflow == ResetWorkflow::Reload ? "Cartridge Reloaded"
                                                                : "Reset";
    statusBar()->showMessage(message, 3000);
    notifyOnScreen(message);
    refreshDebugger();
    refreshActions();
    return true;
}

bool MainWindow::restart() {
    return resetCartridge(ResetWorkflow::Normal);
}

bool MainWindow::quickReset() {
    return resetCartridge(ResetWorkflow::Quick);
}

bool MainWindow::resetConsole(CupidGbResetKind kind) {
    if (!emulator_)
        return false;
    QScopedValueRollback<bool> operation(stateOperation_, true);
    const uint64_t previousCycles = emulator_->gb.elapsed_cycles;
    if (!cupid_gb_reset(&emulator_->gb, kind)) {
        reportError("The console could not be reset.");
        return false;
    }
    cancelCameraCapture();
    resetControllerRumble();
    ++sessionId_;
    rebaseRewindAfterReset(previousCycles);
    resetDebuggerTimeline();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    if (printerWindow_) {
        printerWindow_->setPrinting(false);
        printerWindow_->refresh(false);
    }
    watchBattery();
    resetPacing();
    clearAudio();
    present(Presentation::Reset);
    rememberFrame();
    refreshDebugger();
    refreshActions();
    return true;
}

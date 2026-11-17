/**
 * @file    state.h
 * @brief   Versioned native snapshot capture, exact replay and live restoration.
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

#ifndef CUPID_GB_STATE_H
#define CUPID_GB_STATE_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CupidGbStateResult {
    CUPID_GB_STATE_OK = 0,
    CUPID_GB_STATE_INVALID,
    CUPID_GB_STATE_UNSUPPORTED_VERSION,
    CUPID_GB_STATE_WRONG_ROM,
    CUPID_GB_STATE_WRONG_MODEL,
    CUPID_GB_STATE_WRONG_BOOT_ROM,
    CUPID_GB_STATE_BUFFER_TOO_SMALL,
    CUPID_GB_STATE_OUT_OF_MEMORY,
    CUPID_GB_STATE_NO_ROM
} CupidGbStateResult;

#define CUPID_GB_STATE_VERSION 17u

/* Trusted in-memory snapshots may cache the immutable cartridge identity. Initialize
 * once for the current instance and discard the context on cartridge replacement,
 * model changes, or any mutation of ROM bytes, including future cheat/patch features.
 * Binding checks detect successful cartridge loads even when allocation is reused,
 * but cannot detect manually patched ROM bytes without a new context.
 * Do not use this context to validate untrusted disk files; the ordinary APIs compute
 * the full cartridge digest on every call. Context objects are never serialized.
 */
typedef struct CupidGbStateContext {
    const CupidGb *owner;
    const uint8_t *rom;
    size_t rom_size;
    uint64_t rom_generation;
    CupidGbModel model;
    uint8_t rom_digest[32];
} CupidGbStateContext;

CupidGbStateResult cupid_gb_state_context_init(CupidGbStateContext *context, const CupidGb *gb);
CupidGbStateResult cupid_gb_save_state_with_context(const CupidGb *gb, uint8_t *data,
                                                    size_t capacity, size_t *written,
                                                    const CupidGbStateContext *context);
CupidGbStateResult cupid_gb_load_state_with_context(CupidGb *gb, const uint8_t *data, size_t length,
                                                    const CupidGbStateContext *context);

/* Calls require an initialized, loaded instance at an instruction boundary with no concurrent
 * core access. The native format is versioned and independent of host structure layout.
 * ROMs, boot ROMs, and save paths are session resources and are never included in state data.
 * Restoring an active boot overlay requires the same boot ROM to be available in the target.
 * The target's current boot-overlay enabled flag need not match the saved flag.
 * Version 16 gives TPP1 an independent mapped ROM-bank cache. Control readback is
 * zero immediately after native reset while bank 1 remains mapped until a control
 * write. Older imports reconstruct the mapping from their TPP1 control register.
 * It also appends one pending-input byte after the complete version 15 timing tail.
 * JOYP retains its sampled contacts separately from host masks. Older imports
 * reconstruct their formerly computed contact value without requesting an interrupt.
 * Version 15 appends retained display-clock phase and pending batches, then logical
 * pixel position and window timing latches, and VRAM address/read latches, after
 * version 14 audio history and before the accessory descriptor. It also retains a
 * pending monochrome object-fetch abort.
 * Committed and pending VRAM fetch addresses remain separate across quiet display
 * batches, later synchronization, and LCD power transitions on every hardware model.
 * Capture and restoration do not force display synchronization or advance receiver charge.
 * Versions 2 through 14 receive deterministic pixel, VRAM bus and synchronized display-clock
 * projections from their fully decoded hardware state. Their missing timing/access
 * history cannot reproduce exact pixel or infrared continuation. Live host input and
 * callbacks remain attached.
 * Version 14 retains the older hardware and accessory layout and adds explicit
 * reconstruction rings, DAC delays, binary64 filter history, audio policy,
 * deterministic interference state and pending synthesis clocks before the accessory
 * descriptor. Exact loads restore both the guest hardware and this output history.
 * Live loads first finish the saved APU's deferred clocks on the candidate, then retain
 * the receiving output history and policy. They rebase its sample fraction without
 * changing the receiving sample rate or emitting the discarded timeline's queued audio.
 * Version 13 retains the version 11 hardware payload and version 12 receiver history, then adds
 * uint32 fractional RTC phase, one TPP1 motor byte and one divider subcycle byte before the
 * built-in accessory descriptor. Divider subcycles retain zero through three unheld CPU T-cycles.
 * Shared receiver history is present on non-HuC3 cartridges; HuC3 keeps its existing fields.
 * RTC phase is restored literally, even above the live clock period. Host clock mode, period,
 * time-provider registration and startup policy remain those of the receiving instance.
 * A factory-tagged printer includes its parser, bits, image buffer, clock debt and job timer.
 * Loading preserves the live attachment and callbacks, emits no image/completion callback,
 * and validates the saved device even when that kind is not currently attached.
 * An absent device or version 2-11 file initializes an attached printer at the loaded guest time.
 * Version 11 retains monochrome pulse clock gates, held duty output and pulse DAC codes alongside
 * LCD/cartridge/OAM timing, retained PPU bus history, all three
 * STOP access gates, the palette-port release edge, object fetch stage, pending
 * JOYP row release, and presentation events. Applicable SGB, MMM01, MBC7, Camera,
 * TPP1, and HuC3 sections are included.
 * Versions 2 through 16 remain readable. Versions 2 through 13 receive deterministic
 * settled-channel defaults for missing reconstruction history; queued PCM, output rate, rational
 * credit and the disabled-filter flag survive import. Retired integer mixer/filter history is
 * validated but cannot reproduce the new reconstruction waveform exactly. Versions 2 through 12 use
 * defaults for absent history. Their RTC phase and divider subcycle start at zero; a saved TPP1
 * section supplies motor strength from its status low bits. The older timer_counter field retains
 * its literal saved value. Missing TPP1 sections and other controllers start with zero motor
 * strength. Older monochrome pulse latches are derived from saved channel state; unknown duty
 * sampling, trigger suppression and expired-clock history cannot be recovered.
 * Absent disconnected-DAC codes default to zero on every hardware model.
 * Versions 2 and 3 import additional pads released and the motor off because their
 * writers discarded motor history. Version 2 also initializes audio and DMA latches.
 * Missing bus history and OAM alignment receive deterministic defaults. Exact
 * continuation comparisons require the same qualified engine build and inputs.
 */
size_t cupid_gb_state_size(const CupidGb *gb);
/* Output and written-result storage must be separate from each other, the core,
 * ROM, recognized printer allocation and any supplied trusted context. Capture
 * does not advance the printer or consume its pending elapsed-clock interval. */
CupidGbStateResult cupid_gb_save_state(const CupidGb *gb, uint8_t *data, size_t capacity,
                                       size_t *written);
/* Failure leaves the entire target instance unchanged, including pending audio and ownership.
 * On success, pending core audio is restored; callers must discard queued host audio and reset
 * their host pacing baseline before resuming. These buffers use the CupidGB native format.
 */
CupidGbStateResult cupid_gb_load_state(CupidGb *gb, const uint8_t *data, size_t length);
/* Live desktop timeline changes restore guest state while retaining the receiving
 * output queue, reconstruction/DAC/filter history, sample rate and listening policy.
 * Only the reconstruction fraction is rebased. Clear queued host/core playback
 * before resuming. Rejected loads leave the entire receiving instance unchanged. */
CupidGbStateResult cupid_gb_load_state_live(CupidGb *gb, const uint8_t *data, size_t length);
CupidGbStateResult cupid_gb_load_state_live_with_context(CupidGb *gb, const uint8_t *data,
                                                         size_t length,
                                                         const CupidGbStateContext *context);
const char *cupid_gb_state_result_name(CupidGbStateResult result);

#ifdef __cplusplus
}
#endif
#endif

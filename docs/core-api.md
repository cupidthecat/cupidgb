# Core API

`cupidgb_core` is a C11 static library with no Qt dependency. Public declarations live under `include/cupid`. Link through the CMake target to retain its platform dependencies: Windows system cryptography or optional OpenSSL 3 Crypto on Linux when native hashing is enabled. No external Crypto library is required for a portable-hash build. The core owns emulated hardware; callers own host files, windows, device queues, pacing, and lifecycle coordination.

## Instance lifetime

Use a zero-initialized instance. `CupidGb` is large, so heap allocation avoids small platform stack limits. Its ROM pointer and allocation capacity belong to the core. Initialization preserves those fields and the selected model, so an uninitialized stack object is not a valid input. Cleanup releases the owned ROM allocation.

`CupidEmulator` provides a thin GB/GBC lifecycle wrapper:

```c
#include "cupid/core/emulator.h"

#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }

    CupidEmulator *emulator = calloc(1, sizeof(*emulator));
    if (!emulator) {
        return 1;
    }
    cupid_emulator_init(emulator, CUPID_SYSTEM_GB);
    if (!cupid_emulator_load_rom_file(emulator, argv[1])) {
        cupid_emulator_shutdown(emulator);
        free(emulator);
        return 1;
    }

    int result = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        if (!cupid_emulator_step(emulator)) {
            result = 1;
            break;
        }
    }

    cupid_emulator_shutdown(emulator);
    free(emulator);
    return result;
}
```

This example checks lifecycle and bounded execution only. A complete host must consume output and provide pacing, input, persistence, and error handling. Link the target through CMake with `target_link_libraries(your_host PRIVATE cupidgb_core)`.

For explicit hardware selection, call `cupid_gb_set_model` before loading the cartridge. The direct API in `cupid/gb/gb.h` exposes initialization, cleanup, header parsing, ROM loading, stepping, and memory access. `cupid_gb_load_rom` copies supplied cartridge bytes into its own storage. The wrapper selects CGB when initialized with `CUPID_SYSTEM_GBC`; `CUPID_SYSTEM_GB` uses the monochrome default unless a more specific model is selected.

When calling the C core from C++, wrap its C header includes in an `extern "C"` block, as the Qt frontend does. Some codec headers provide their own linkage guards, while the general lifecycle and hardware headers rely on the caller's wrapper:

```cpp
extern "C" {
#include "cupid/core/emulator.h"
#include "cupid/gb/input.h"
}
```

Loading a cartridge resets hardware on success. The loader validates size, header and supported storage before replacing the current ROM, and a failed allocation leaves the live core unchanged. Input may alias the current ROM allocation when the complete source range is valid. Storage follows the actual input length, with a 32 KiB minimum and a 1 GiB image limit. ROM size and allocation capacity are separate: missing bytes in the physical bank population read as `0xff`, and hashes cover only the supplied bytes.

A complete frontend replacement also validates battery and boot inputs. Build a separate candidate and swap it in only after all those inputs succeed. The Qt frontend uses that pattern. Native resets retain the ROM pointer and capacity; native states retain the receiving allocation and do not serialize its capacity.

## Cold memory and firmware startup

Initialization creates model-specific WRAM, HRAM, OAM, and wave-memory contents. The default chooses an automatic seed. `cupid/gb/startup.h` provides `cupid_gb_set_power_on_seed(gb, seed)` for a reproducible generation and `cupid_gb_clear_power_on_seed(gb)` to return to automatic choices. Set the policy before initialization or cartridge loading. Zero is a valid seed that still produces modeled memory variation.

Both setters affect the next cold initialization; they leave current memory and the last realized seed intact. `cupid_gb_get_power_on_seed` returns that realized seed, including an automatic choice. Policy survives initialization and native-state loading, while cleanup clears it. A loaded state restores its own memory without replacing the receiving instance's seed metadata. The getter therefore describes the host's last cold generation, not the provenance of restored RAM. Native states exclude this metadata; the realized guest memory is already serialized.

Ordinary initialization and cartridge loading apply the approximate post-boot profile. On physical CGB, it clears WRAM bank 2 and supplies the post-boot wave pattern. Initial firmware execution needs the original cold memory instead:

```c
cupid_gb_set_model(gb, CUPID_GB_MODEL_CGB);
cupid_gb_set_power_on_seed(gb, UINT64_C(0x1234));
if (cupid_gb_load_rom_file(gb, cartridge_path) &&
    cupid_gb_load_boot_rom_file(gb, boot_path)) {
    cupid_gb_start_boot_rom(gb);
}
```

Call `cupid_gb_start_boot_rom` after loading the cartridge, optional battery data, and firmware, before guest execution. It reconstructs WRAM, HRAM, OAM, and VRAM from the same realized generation, then initializes boot-entry registers and wave/palette memory. It draws no new seed and preserves cartridge storage, RTC data, seed policy, and host registrations. A null instance or missing firmware changes nothing. This operation deliberately replaces volatile memory and is not a native hard or quick reset.

The lower-level `cupid_gb_enter_boot_rom` retains live WRAM, HRAM, OAM, and VRAM while resetting its boot-entry registers and reconstructing wave/palette memory from the realized generation. Use it only when that memory-preserving re-entry contract is intended. The Qt frontend and ROM runner use the explicit cold operation for initial firmware startup.

The startup header supplies its own C++ linkage guard. General hardware headers still use the caller's `extern "C"` wrapper described above. Fixed seeds reproduce the current generator's bytes for the same profile; byte sequences are not a stable cross-version file format. Record the build, model, seed, firmware hash, and input trace when comparing runs. See [hardware profiles](hardware.md#cold-start-memory) for the qualified distributions and remaining model limits.

## Native console reset

`cupid/gb/reset.h` provides `cupid_gb_reset(gb, CUPID_GB_RESET_HARD)` and `cupid_gb_reset(gb, CUPID_GB_RESET_QUICK)` for an already loaded instance. Both restart execution and controller access latches while retaining ROM ownership, battery storage, host callbacks, input registration, cheats and audio output history. Quick Reset also retains physical WRAM, HRAM, OAM, color palettes and the DMA/object-palette I/O bytes. Both clear VRAM. Hard reset uses the configured power-on seed policy and erases active volatile cartridge storage to `FF`.

Installed firmware starts at PC `0000`; no instructions execute inside the reset call. Without firmware, the approximate handoff starts at PC `0100`. For color hardware that approximation includes clearing WRAM bank 2 and OAM, so those post-boot effects differ from the immediate native retention boundary. The complete SGB introduction and its timing remain outside the qualified scope.

MBC3 and TPP1 clock data and common fractional history survive. HuC3 clock and alarm digits belong to its controller reset domain and clear. Reset refreshes the host observation baseline without advancing the retained clock digits. A change to persistent HuC3 digits increments the battery generation for autosave tracking. The host time callback follows the existing non-reentrant observer contract.

The operation prepares a detached candidate. An invalid request, invalid firmware size or allocation failure leaves the complete live core and attached printer unchanged. On success it commits the candidate, then clears the attached printer's protocol and pending job while retaining printer callbacks and emitted paper. Reset does not emit sound, serial, rumble, infrared or printer notifications. Hosts must reset presentation pacing and resynchronize feedback after success.

## Execution and clocks

`cupid_gb_step` and `cupid_emulator_step` execute one hardware step: an instruction, interrupt entry, or bounded HALT/STOP work. A true result means that step completed. It does not establish instruction progress or successful gameplay. False reports an invalid instance, an absent cartridge, or a decoder failure. These functions do not sleep or synchronize with a display.

The eleven illegal primary opcodes complete their fetch, clear IE, and halt the CPU. Their step returns true so the host can continue clocks and consume subsequent graphics and sound. Interrupt requests alone cannot wake that halt because no interrupts remain enabled. A debugger can deliberately restore IE; reset or an earlier snapshot can also replace the halted state. Native states retain it through the existing CPU and IE fields, without a new format version.

An IME-clear HALT wake can return successfully with an unchanged PC and no fetched opcode. A fully frozen CGB speed-switch poll can update CPU-domain sequencing without advancing `elapsed_cycles`. Use observed step-event information when tracking executed instructions, rather than assuming every successful call fetched the byte currently at PC.

`CupidGb.elapsed_cycles` counts base-speed T-cycles. Query `cupid_gb_base_clock_hz(gb)` to convert those cycles to seconds: the SGB NTSC profile uses 4,295,454 Hz, and the other current profiles use 4,194,304 Hz. A null instance returns the default 4,194,304 Hz. SGB PAL is not implemented. A regular LCD frame spans 70,224 base-speed T-cycles for every current profile. Do not infer guest time from CPU instruction count or host paint count. CGB double speed changes CPU-domain work while the base-speed clock remains the timekeeping domain.

Use both a cycle bound and an execution-step bound for unattended runs. The LCD can be disabled, so waiting only for `frame_ready` can leave a host loop without a useful presentation bound. Host stalls must not be corrected by silently dropping guest hardware work.

## Cartridge clock policy

`cupid/gb/rtc.h` exposes host synchronization and Accurate emulated-clock policies, an optional Unix-time provider, and a core-only period multiplier. Configure them before cartridge and battery loading. Policy/provider registration survives initialization and native loads; cleanup clears it. A native state restores guest clock history without changing the current host policy or calling its provider.

Mode changes preserve counter and latch values while clearing fractional progress and rebasing the active timestamp. A same-mode call is an exact no-op. Period changes preserve phase and baseline. Providers must not mutate or reenter the core. The [cartridge clock guide](rtc.md) describes validation, scheduling, controller behavior and the bounded exception for raw timestamp histories without a reachable stopping value.

## Pixels and sound

| Output | Representation |
| --- | --- |
| `frame_buffer` | 160 by 144 monochrome shade indices |
| `frame_buffer_color` | 160 by 144 RGB555 values for color or compatibility palettes |
| SGB render API | 256 by 224 ARGB image with the game viewport and border |
| `cupid_gb_apu_drain` | Interleaved signed 16-bit stereo, left then right; returns stereo frame count |

Check and clear `frame_ready` when consuming completed images. Keep color/model selection consistent with the core's compatibility behavior; a monochrome cartridge can still use a color hardware palette. The Qt frontend's display pipeline is a host component and is not part of the C core.

Drain audio regularly, including while the LCD is disabled. The core's sample buffer is finite. A host that drains only on VBlank can lose output during prolonged LCD-off execution. `tools/rom_runner.c` drains per hardware step; another host can use a bounded interval small enough to preserve every generated sample. Draining never executes pending audio clocks. The [audio guide](audio.md) separates channel timing, reconstruction and host delivery.

`cupid/gb/apu.h` exposes sample-rate, filter-mode, interference and channel-mute controls. The rate is stereo frames per physical model-second. Changing it finishes pending digital work at the old rate and updates output coefficients while retaining queued PCM and reconstruction/filter history. Filter and interference setters validate their domains; channel muting leaves guest registers and digital taps unchanged. Clear or recreate host conversion state when changing formats or timelines. Global listening volume/mute, resampling and playback-device control remain outside the core.

## Input and memory access

Buttons use an active-low byte, ordered Right, Left, Up, Down, A, B, Select, Start. All released is `0xff`; pressing A alone is `0xef`. Call `cupid_gb_set_buttons(gb, released_mask)` from `cupid/gb/input.h` to supply host button state. The setter records the mask and schedules a sample, including when the same mask is submitted again. It does not immediately change sampled JOYP lines or request an interrupt. The next delivered hardware span samples the selected contacts and requests an interrupt only for a falling selected line.

A write that changes JOYP row selection samples the new active row immediately. Selecting the same row leaves the sample unchanged. Row-write sampling does not consume a pending host update, which still runs on the next hardware delivery. Both reset kinds retain held masks and any pending update while restarting the released-contact latch. Firmware entry uses `CF`; the approximate SGB/SGB2 handoff uses `FF`. A settled held key is therefore not sampled again solely because the console resets.

`cupid_gb_set_buttons_for_player(gb, player, released_mask)` accepts indexes 0–3 and returns false for an invalid instance/player. The old setter still targets player 0. The existing `joypad` field is player 0; `joypad_extra[0..2]` stores players 1–3. SGB selects the active mask through its multiplayer protocol. A nonselected mask change creates no selected-line edge. Device assignments and single-player controller folding belong to the host, which should keep distinct masks available as the guest changes player count.

`cupid_gb_rumble_enabled(gb)` queries the raw MBC5 or TPP1 motor latch. `cupid_gb_set_rumble_callback(gb, callback, user_data)` retains Boolean on/off notifications; changing between positive TPP1 levels does not notify this observer.

`cupid_gb_rumble_strength(gb)` returns the effective cartridge motor level from 0 through 3. MBC5 uses 0 or 3. TPP1 retains levels 1, 2 and 3 when its extended header declares a motor; otherwise the effective level is zero while guest register readback remains intact. `cupid_gb_set_rumble_strength_callback(gb, callback, user_data)` reports every effective level change, including positive-to-positive changes, with the const instance, new level, base-speed cycle timestamp and user pointer.

The two transition observers run synchronously on supported bus writes. `cupid_gb_set_rumble_sample_callback(gb, callback, user_data)` instead observes the effective level at the first nonfrozen portion of each CPU clock delivery, before its display work. The callback receives the const instance, level and user pointer. A longer delivery still contributes one sample. Fully frozen oscillator spans contribute none; a speed change can split a delivery into two samples. Bulk `cupid_gb_tick` calls retain one delivery per machine cycle. Internal pixel and DMA subdivisions do not add samples on their own.

`cupid_gb_set_presentation_callback(gb, callback, user_data)` observes each presentation after its handheld pixel buffers are finalized. It receives the const instance, a `CUPID_GB_FRAME_*` type and the user pointer. Normal, first-frame blank, repeat, LCD-off and artificial events are included. An immediate LCD-enable presentation can occur without advancing time or adding a motor sample. This is an event inside hardware execution: the surrounding instruction, interrupt and SGB host-frame bookkeeping may still be in progress. Collect observations cheaply, avoid mutating or reentering the core, and defer device I/O until the core call returns. Registration survives initialization, reset and state loading, is excluded from snapshots, and is cleared by cleanup.

Qt registers these observers only for a cartridge with motor capability. At a presentation event it averages the collected levels and divides by three, queues that completed interval, and starts collecting the next. Samples delivered later in the same instruction remain in the next interval. After execution returns, Qt applies the controller Strength response described under [cartridge feedback](controllers.md#cartridge-feedback) and sends the queued requests. During playback, a host step without a presentation does not flush a partial average. Paused debugger execution discards partial and completed feedback at the end of each execution slice.

All motor callbacks must avoid mutating or reentering hardware. Registration survives initialization, cartridge loading, reset and state loading, and cleanup clears it. Registrations and accumulated host samples are not serialized. Reset and state decoding emit no motor notification: hosts must stop output and clear pending sample history after successful reset, loading, rewind and session changes. The hardware core does not open a controller.

`cupid_gb_mbc7_set_motion(gb, x, y)` supplies signed motion samples relative to neutral. About 112 counts correspond to one g. The cartridge copies them into its axis registers when the guest requests a sample. Native states store the sampled registers. The current host sample remains with the live instance.

The direction-only scan resolves opposing direction contacts consistently; when both rows are selected, it preserves their wired-AND result. Monochrome row selection models immediate pull-down and delayed release, including MGB timing adjustments. Physical contact bouncing and broader electrical behavior remain unqualified; the input API does not model every property of real switches.

`cupid_gb_read_u8` passively observes mapped device state while respecting banking and access restrictions. For JOYP it returns the stored active row and sampled contacts. `cupid_gb_read_bus_u8` is the guest access path; during monochrome row settling it exposes the requested select bits with the stored contact sample. `cupid_gb_write_u8` performs a device write at the current phase. These calls do not execute a CPU instruction's bus schedule. Timing checks should execute instructions or use the relevant subsystem interface deliberately.

Do not mutate internal fields from another thread. The public structures expose implementation state but do not make concurrent access safe. Pause and serialize access at an instruction boundary before saving, loading, inspecting, or changing host integration settings.

## Serial observation

Register `cupid_gb_set_serial_output_callback(gb, callback, user_data)` to observe completed internal-clock transfers. The callback receives a const instance, the SB byte latched when SC starts the transfer, and the supplied user pointer. It runs synchronously after the eighth shift, the clearing of SC's start bit, and the serial interrupt request. Other work in the enclosing machine cycle can still be pending. The callback must neither mutate nor reenter the emulator. Queue any later host work outside the callback.

The core writes no serial bytes to stdout and performs no host-stream flushing. A host can buffer, display, or ignore observed bytes. This API observes completed bytes. Separate bit-level link APIs support internal- and external-clock transfers; desktop coordination of linked sessions remains pending.

Registration survives initialization, cartridge loading, boot reset, and native-state loading. Cleanup or registration with a null callback clears both callback and user pointer. These host fields are excluded from snapshots: restoring guest state preserves the current registration rather than restoring an old pointer. A state load does not replay previously observed bytes.

## Battery data

`cupid/gb/battery.h` provides size, encode, decode, result-name, and clock helpers. Ordinary save/load functions use the registered time provider when their controller's format requires host time; `_at` variants accept an explicit timestamp for reproducible checks. HuC3 export stores its internal baseline without polling the provider. Battery operations preserve host clock policy and fractional phase.

`cupid_gb_battery_size` returns zero when the loaded cartridge has no supported battery payload. Allocate sufficient independent storage and check both the result and returned length. Rejected loads preserve the instance. The caller handles file I/O and atomic replacement; the codec does not choose paths. Prefer this explicit result-bearing API for new hosts over the older convenience file-save helpers.

The optional byte-count pointer must not overlap the live core, any part of its owned ROM allocation, or the supplied output capacity. Both save variants reject those aliases before clearing the count; the ordinary variant also rejects them before polling its time provider. A rejected count alias leaves the core, ROM and destination unchanged.

Battery data differs from execution state. See [battery saves](battery-saves.md) for encodings and clock semantics.

## Snapshots and rewind

Use `cupid_gb_state_size`, `cupid_gb_save_state`, and `cupid_gb_load_state` at an instruction boundary with no concurrent access. States exclude ROM/boot binaries and host resources. A restored active boot overlay needs the matching binary already available to the target session.

Normal disk-state APIs recompute cartridge identity. A rejected load leaves the complete target unchanged. The ordinary load APIs restore recorded hardware and audio history exactly. Use `cupid_gb_load_state_live` for desktop timeline changes, or `cupid_gb_load_state_live_with_context` for trusted history. Those variants retain receiving output reconstruction, DAC/filter history, rate and listening policy while rebasing its fractional phase. They validate the combined pending-clock credit before committing. After success, discard stale queued playback, reset host conversion/pacing, reconcile input and redraw.

`CupidGbStateContext` caches cartridge identity for trusted in-memory rewind. It binds to one owner, ROM pointer, ROM size, load generation, and model. Every successful cartridge load advances the generation, so a context becomes stale even when the allocation and length stay the same. Failed loads and native resets preserve the generation. Discard and recreate the context after cartridge/model changes or any in-place ROM patch. Direct external writes cannot be detected by the binding checks. Never use the cached context to validate an untrusted disk file.

The codec checks a complete word-array range before reading or writing its little-endian elements and accepts unaligned buffers. New snapshots use version 18, which adds the serial divider tap selected by an SC write. Reset selects the slow tap independently of SC readback. Later compatibility-mode changes retain that selection. Older imports derive the missing selector from the loaded operating mode and SC bit 1, preserving the raw register byte and recorded clock phase. They never borrow the receiving session's selector or issue a control write. See [serial state migration](save-states.md#serial-clock-selection-in-version-18) for the resulting readback and continuation rules.

Version 17's retained CGB tile row, pending input sampling and TPP1 mapping remain independent of later control writes. The codec preserves version 15's display wait and bus-synchronization history used by infrared sensing, interrupted pixel-fetch work and the VRAM read circuit's retained addresses and byte. Version 14's reconstruction rings, DAC delays, filter and policy history, deterministic interference and pending synthesis clocks remain intact. Versions 2 through 17 remain loadable with explicit defaults for missing history; an old file cannot recreate input events, waveform edges, scroll latches, serial selections or display synchronization it never stored. Version 13's RTC phase, motor strength and divider remainder remain independent of host clock policy. Query the size for every capture because model, cartridge and printer sections change the length. Identity, integrity, final-offset and hardware-domain checks apply to every accepted version.

`cupid_gb_set_serial_link` registers bit-launch and sample callbacks for an internal-clock transfer. A host can read an external-clock peer with `cupid_gb_serial_external_bit`, then deliver the sender's bit with `cupid_gb_serial_external_clock`. Both cores need a shared host schedule. Serial registrations survive reset and state loading. External cable state remains the host's responsibility.

`cupid/gb/printer.h` provides the built-in printer endpoint. Pass the complete link returned by `cupid_gb_printer_link`; it includes a device tag alongside its callbacks and context. Ordinary cable callbacks leave the tag at `CUPID_GB_SERIAL_DEVICE_NONE`. Recognition checks both the printer tag and its factory callbacks before using its context. Detach the link before destroying the printer, and call `cupid_gb_printer_update` after execution slices to deliver completion while the wire is idle.

Native version 12 captures a recognized printer's protocol, partial serial bits, image buffer and pending job without advancing its clock or invoking callbacks. Loading into another attached printer restores that history and keeps the target's host registrations. A saved empty attachment or version 2–11 file initializes a fresh printer at the loaded guest time. A saved printer never changes the current attachment choice. The complete saved record is validated even if it will be discarded. Console reset clears an attached printer's protocol and job. See the [state format](save-states.md#printer-state-in-version-12) for the descriptor and transaction rules.

Reattaching a retained printer to an earlier guest timeline rebases its clock without consuming its pending job or invoking a callback. The next capture can therefore include it immediately. Attaching a newly created printer instead starts an empty device at the current guest time.

`cupid/gb/cheats.h` supplies host-owned rule collections, Game Genie/GameShark imports, duplicate-priority-preserving cloning, and a portable `.cht` codec. Attach with `cupid_gb_set_cheats`, mutate only at instruction boundaries, and detach before destruction. Rules intercept reads after bank selection and leave storage bytes intact. Collections remain outside native snapshots. [Cheat integration](cheats.md) describes the matching and file contracts.

The core supplies snapshot bytes. The host supplies compression, history limits, storage, and UI. Convert rewind age from base T-cycles using the selected model clock, not a fixed rate. The complete wire-format and continuation contract is in [save states and rewind](save-states.md).

## Infrared connections

`cupid/gb/infrared.h` exposes the shared CGB, HuC1, and HuC3 sensor. Use `cupid_gb_set_infrared_input` to supply external light and `cupid_gb_set_infrared_callback` to observe output changes. The callback receives the changed latch's level and current base-cycle timestamp. Cartridge callbacks see the newly committed latch; CGB port callbacks run before the port register changes. `cupid_gb_infrared_output` reports the combined current LED state, so querying it inside a color-port callback can still return the preceding value.

Sensor charge and decay follow guest execution. Native version 12 preserves their history on every cartridge while retaining the current host input and callback. The existing HuC3 functions delegate to this API. No transport thread, device handle, or physical link is created by these calls. Register/callback and stable-sensor comparisons are covered; noisy transition behavior still differs and requires further qualification.

## Debugger integration

`cupid/gb/debugger.h` exposes host-only `CupidGbDebugger` state. Initialize it once, clean up its owned symbol table, and reset execution after a state change or edit. Its breakpoints, watchpoints, and symbols are not guest hardware and are not serialized. A host can preserve that metadata while invalidating a running command and its history.

Begin RUN, STEP, NEXT, or FINISH with `cupid_gb_debugger_begin`, then call `cupid_gb_debugger_step` or the bounded `cupid_gb_debugger_run`. Budgets count hardware-step attempts. A budget result leaves the command active for a later slice; an actual stop pauses the debugger. Ordinary hosts can use the before-instruction `cupid_gb_debugger_check` hook for execution breakpoints. A breakpoint resumes past its stopped occurrence once, without disabling later matches.

`cupid_gb_step_observed` fills a caller-owned `CupidGbStepTrace` for an instruction fetch, interrupt dispatch, idle step, or illegal primary opcode. `CUPID_GB_STEP_ILLEGAL_OPCODE` occurs once for the completed illegal fetch and records its actual address and byte, including HALT-bug PC suppression. Subsequent halted steps report `CUPID_GB_STEP_IDLE`. All CB-prefixed sub-opcodes are defined; their primary trace byte remains `CB`. This API uses the same hardware execution path as `cupid_gb_step`, so consumers do not infer fetched opcodes from an unrestricted memory view. No trace or debugger resource is added to a native snapshot.

An explicit debugger execution command stops with `CUPID_GB_DEBUG_STOP_ILLEGAL_OPCODE` after the illegal fetch. This is distinct from `CUPID_GB_DEBUG_STOP_ERROR`. The resulting hardware state remains available for inspection, saving, and instruction Backstep. Continuing playback advances the halted hardware under the ordinary execution contract.

`cupid_gb_debug_peek` reads underlying mapped memory without ticking hardware or applying CPU VRAM/OAM/wave restrictions. `cupid_gb_debug_poke` rejects ROM/unusable ranges, edits writable mapped storage, and applies device semantics for I/O. It requires the same exclusive instruction-boundary access as state operations. Raw observation and intentional edits are different contracts; an I/O edit can have hardware side effects.

`cupid_gb_debugger_evaluate` evaluates bounded, pure 16-bit expressions over registers, symbols, and side-effect-free memory views. Conditional execution breakpoints store their expression as host metadata and evaluate it only at a matching instruction boundary. An evaluation failure pauses execution with a debugger error. Invalid new definitions leave existing breakpoints intact. The Qt console exposes these operations through `print` and `break ... if ...`. See [debugging](debugging.md) for syntax and limits.

Memory watchpoints use inclusive virtual-address ranges, optional bank qualification, read/write masks, stable IDs, and optional conditions. `cupid_gb_debugger_step` observes guest bus accesses and returns `CUPID_GB_DEBUG_STOP_WATCHPOINT` after a matching hardware step completes. A host using the ordinary execution API instead surrounds exactly one step with `cupid_gb_debugger_watch_begin` and `cupid_gb_debugger_watch_end`; it must call the end hook even when execution fails. Scope these hooks to forward guest execution, excluding passive inspection, editor writes, and rewind replay. A hit records the first matching original bus address and the attempted write value, including blocked accesses.

The `old` and `new` expression tokens exist only during watch conditions. Both are zero for reads. For writes, `new` is the attempted value; each evaluated occurrence of `old` performs a recursion-guarded guest read before the store, so it follows bus locks and device read semantics. Other expression reads remain passive. Conditions that do not use `old` perform no extra guest read. An evaluation failure returns `ERROR` after the completed hardware step. Host history code must distinguish that result from a condition error reported before an execution breakpoint: only the latter has no executed instruction to restore.

Disassembly, symbol loading/lookups, bank identification, and breakpoint management have separate APIs. Symbol-buffer loading is transactional and bounded. The Qt host implements Backstep with trusted native snapshots; C hosts must provide their own instruction history and reconcile audio/pacing after restoration. See [debugging](debugging.md) for command behavior, memory scopes, and remaining capabilities.

`cupid_gb_rom_file_bank` translates a controller-selected ROM bank to its original-file storage bank, including MMM01 menu placement and disconnected address lines. `cupid_gb_effective_rom_bank` applies that translation to the currently selected mapping. `cupid_gb_debugger_bank` supplies the identity used by symbol and breakpoint matching; for MMM01 this is the controller bank, including the unlocked `FFFE`/`FFFF` identities. Expression values and Qt `break` commands retain nine-bit qualifiers. Keep storage offsets and debugger identities distinct when embedding these APIs.

## Errors and compatibility

Lifecycle/step functions use booleans; snapshot and battery operations return typed result codes with readable name helpers. Logging reports additional loading and execution details. File loading through narrow C paths can depend on host filesystem encoding; the Qt frontend reads local cartridge and boot files through Qt before handing bytes to the core.

With GCC mixed C/C++ Release IPO, compile the C++ host with `-fno-strict-aliasing`, as the in-tree build does. Link through the core target to retain cryptography dependencies; external C++ consumers must also preserve the IPO aliasing flag in their own compilation settings.

The exposed structures are an internal integration surface, not a promised stable binary ABI. Preserve persistent model tags and version serialized formats deliberately when hardware state changes. API source presence, unit tests, and a bounded run do not establish complete hardware compatibility.

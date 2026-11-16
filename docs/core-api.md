# Core API

`cupidgb_core` is a C11 static library with no Qt dependency. Public declarations live under `include/cupid`. Link through the CMake target to retain its platform dependencies: Windows system cryptography or optional OpenSSL 3 Crypto on Linux when native hashing is enabled. No external Crypto library is required for a portable-hash build. The core owns emulated hardware; callers own host files, windows, device queues, pacing, and lifecycle coordination.

## Instance lifetime

Use a zero-initialized instance. `CupidGb` is large, so heap allocation avoids small platform stack limits. Its ROM pointer is owned by the core after initialization. Do not initialize an uninitialized stack object: initialization preserves selected fields, including the ROM pointer and model.

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

Loading a cartridge resets hardware. The direct loader is not a transactional session-replacement API. To preserve a running session after a failed load, build a separate candidate and swap it in only after cartridge, battery, and boot inputs validate. The Qt frontend uses that pattern.

## Cold memory and firmware startup

Initialization creates model-specific WRAM, HRAM, OAM, and wave-memory contents. The default chooses an automatic seed. `cupid/gb/startup.h` provides `cupid_gb_set_power_on_seed(gb, seed)` for a reproducible generation and `cupid_gb_clear_power_on_seed(gb)` to return to automatic choices. Set the policy before initialization or cartridge loading. Zero is a valid seed that still produces modeled memory variation.

Both setters affect the next cold initialization; they leave current memory and the last realized seed intact. `cupid_gb_get_power_on_seed` returns that realized seed, including an automatic choice. Policy survives initialization and native-state loading, while cleanup clears it. A loaded state restores its own memory without replacing the receiving instance's seed metadata. The getter therefore describes the host's last cold generation, not the provenance of restored RAM. This metadata is excluded from native V12; the realized guest memory is already serialized.

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

## Execution and clocks

`cupid_gb_step` and `cupid_emulator_step` execute one hardware step: an instruction, interrupt entry, or bounded HALT/STOP work. A true result means that step completed. It does not establish instruction progress or successful gameplay. False reports an invalid instance, an absent cartridge, or a decoder failure. These functions do not sleep or synchronize with a display.

The eleven illegal primary opcodes complete their fetch, clear IE, and halt the CPU. Their step returns true so the host can continue clocks and consume subsequent graphics and sound. Interrupt requests alone cannot wake that halt because no interrupts remain enabled. A debugger can deliberately restore IE; reset or an earlier snapshot can also replace the halted state. Native states retain it through the existing CPU and IE fields, without a new format version.

An IME-clear HALT wake can return successfully with an unchanged PC and no fetched opcode. A fully frozen CGB speed-switch poll can update CPU-domain sequencing without advancing `elapsed_cycles`. Use observed step-event information when tracking executed instructions, rather than assuming every successful call fetched the byte currently at PC.

`CupidGb.elapsed_cycles` counts base-speed T-cycles. Query `cupid_gb_base_clock_hz(gb)` to convert those cycles to seconds: the SGB NTSC profile uses 4,295,454 Hz, and the other current profiles use 4,194,304 Hz. A null instance returns the default 4,194,304 Hz. SGB PAL is not implemented. A regular LCD frame spans 70,224 base-speed T-cycles for every current profile. Do not infer guest time from CPU instruction count or host paint count. CGB double speed changes CPU-domain work while the base-speed clock remains the timekeeping domain.

Use both a cycle bound and an execution-step bound for unattended runs. The LCD can be disabled, so waiting only for `frame_ready` can leave a host loop without a useful presentation bound. Host stalls must not be corrected by silently dropping guest hardware work.

## Pixels and sound

| Output | Representation |
| --- | --- |
| `frame_buffer` | 160 by 144 monochrome shade indices |
| `frame_buffer_color` | 160 by 144 RGB555 values for color or compatibility palettes |
| SGB render API | 256 by 224 ARGB image with the game viewport and border |
| `cupid_gb_apu_drain` | Interleaved signed 16-bit stereo, left then right; returns stereo frame count |

Check and clear `frame_ready` when consuming completed images. Keep color/model selection consistent with the core's compatibility behavior; a monochrome cartridge can still use a color hardware palette. The Qt frontend's display pipeline is a host component and is not part of the C core.

Drain audio regularly, including while the LCD is disabled. The core's sample buffer is finite. A host that drains only on VBlank can lose output during prolonged LCD-off execution. `tools/rom_runner.c` drains per hardware step; another host can use a bounded interval small enough to preserve every generated sample. The [audio guide](audio.md) separates channel timing, integration and host delivery.

`cupid/gb/apu.h` exposes output sample-rate and high-pass controls. The configured rate is samples per physical model-second, so sample scheduling and the high-pass coefficient use the model clock. Changing the output rate discards queued and partial output frames while preserving oscillators. Clear or recreate corresponding host conversion state when changing formats or loading a state. Host volume, mute, resampling, and playback-device control belong outside the core.

## Input and memory access

Buttons use an active-low byte, ordered Right, Left, Up, Down, A, B, Select, Start. All released is `0xff`; pressing A alone is `0xef`. Call `cupid_gb_set_buttons(gb, released_mask)` from `cupid/gb/input.h` when supplying physical button state so selected-line interrupt behavior remains in the core. Selecting a row containing an already-held key can also request an interrupt. Only falling selected-line levels request an interrupt. Resolving opposing directions can expose a new falling level even when a physical contact is released.

`cupid_gb_set_buttons_for_player(gb, player, released_mask)` accepts indexes 0–3 and returns false for an invalid instance/player. The old setter still targets player 0. The existing `joypad` field is player 0; `joypad_extra[0..2]` stores players 1–3. SGB selects the active mask through its multiplayer protocol. A nonselected mask change creates no selected-line edge. Device assignments and single-player controller folding belong to the host, which should keep distinct masks available as the guest changes player count.

`cupid_gb_rumble_enabled(gb)` queries the MBC5 motor latch. `cupid_gb_set_rumble_callback(gb, callback, user_data)` registers a synchronous observer receiving the const instance, new motor state, base-speed cycle timestamp and user pointer. It fires only on a motor transition caused by a supported bus write. Do not mutate or reenter hardware from the callback. Registration survives guest reset/load and is not serialized. State decoding emits no callback: hosts must explicitly stop and resynchronize feedback after reset, loading, rewind and session changes. The hardware core does not open a controller or impose host feedback policy.

`cupid_gb_mbc7_set_motion(gb, x, y)` supplies signed motion samples relative to neutral. About 112 counts correspond to one g. The cartridge copies them into its axis registers when the guest requests a sample. Native states store the sampled registers. The current host sample remains with the live instance.

The direction-only scan resolves opposing direction contacts consistently; when both rows are selected, it preserves their wired-AND result. Monochrome row selection models immediate pull-down and delayed release, including MGB timing adjustments. Physical contact bouncing and broader electrical behavior remain unqualified; the input API does not model every property of real switches.

`cupid_gb_read_u8` and `cupid_gb_write_u8` perform normal device accesses at the current hardware phase. They apply banking and access restrictions. They are not unrestricted debugger peek/poke operations and do not execute a CPU instruction's bus schedule by themselves. Tests that need bus phases should drive CPU execution or the relevant subsystem interface deliberately.

Do not mutate internal fields from another thread. The public structures expose implementation state but do not make concurrent access safe. Pause and serialize access at an instruction boundary before saving, loading, inspecting, or changing host integration settings.

## Serial observation

Register `cupid_gb_set_serial_output_callback(gb, callback, user_data)` to observe completed internal-clock transfers. The callback receives a const instance, the SB byte latched when SC starts the transfer, and the supplied user pointer. It runs synchronously after the eighth shift, the clearing of SC's start bit, and the serial interrupt request. Other work in the enclosing machine cycle can still be pending. The callback must neither mutate nor reenter the emulator. Queue any later host work outside the callback.

The core writes no serial bytes to stdout and performs no host-stream flushing. A host can buffer, display, or ignore observed bytes. This API observes completed bytes. Separate bit-level link APIs support internal- and external-clock transfers; desktop coordination of linked sessions remains pending.

Registration survives initialization, cartridge loading, boot reset, and native-state loading. Cleanup or registration with a null callback clears both callback and user pointer. These host fields are excluded from snapshots: restoring guest state preserves the current registration rather than restoring an old pointer. A state load does not replay previously observed bytes.

## Battery data

`cupid/gb/battery.h` provides size, encode, decode, result-name, and clock helpers. The ordinary save/load functions use host time; `_at` variants accept an explicit timestamp for reproducible checks.

`cupid_gb_battery_size` returns zero when the loaded cartridge has no supported battery payload. Allocate sufficient independent storage and check both the result and returned length. Rejected loads preserve the instance. The caller handles file I/O and atomic replacement; the codec does not choose paths. Prefer this explicit result-bearing API for new hosts over the older convenience file-save helpers.

Battery data differs from execution state. See [battery saves](battery-saves.md) for encodings and clock semantics.

## Snapshots and rewind

Use `cupid_gb_state_size`, `cupid_gb_save_state`, and `cupid_gb_load_state` at an instruction boundary with no concurrent access. States exclude ROM/boot binaries and host resources. A restored active boot overlay needs the matching binary already available to the target session.

Normal disk-state APIs recompute cartridge identity. A rejected load leaves the complete target unchanged. After a successful load, discard queued host audio, reset conversion/pacing baselines, reconcile physical input, and redraw. The core's pending samples are part of the restored state; the host playback queue is not.

`CupidGbStateContext` caches cartridge identity for trusted in-memory rewind. It binds to one owner, ROM pointer, ROM size, and model. Discard it after cartridge/model changes or any in-place ROM patch. Binding checks cannot detect a patch that preserves the pointer and size. Never use the cached context to validate an untrusted disk file.

The codec checks a complete word-array range before reading or writing its little-endian elements. It accepts unaligned state buffers. New snapshots use version 12. Versions 2 through 11 remain loadable with deterministic defaults for absent history, including monochrome pulse gates that older builds did not maintain. Query the size for every capture: a printer attachment also changes the required length. Identity, integrity, final-offset, and hardware-domain checks apply to every accepted version.

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

`cupid/gb/debugger.h` exposes host-only `CupidGbDebugger` state. Initialize it once, clean up its owned symbol table, and reset execution after a state change or edit. Its breakpoints and symbols are not guest hardware and are not serialized. A host can preserve that metadata while invalidating a running command and its history.

Begin RUN, STEP, NEXT, or FINISH with `cupid_gb_debugger_begin`, then call `cupid_gb_debugger_step` or the bounded `cupid_gb_debugger_run`. Budgets count hardware-step attempts. A budget result leaves the command active for a later slice; an actual stop pauses the debugger. Ordinary hosts can use the before-instruction `cupid_gb_debugger_check` hook for execution breakpoints. A breakpoint resumes past its stopped occurrence once, without disabling later matches.

`cupid_gb_step_observed` fills a caller-owned `CupidGbStepTrace` for an instruction fetch, interrupt dispatch, idle step, or illegal primary opcode. `CUPID_GB_STEP_ILLEGAL_OPCODE` occurs once for the completed illegal fetch and records its actual address and byte, including HALT-bug PC suppression. Subsequent halted steps report `CUPID_GB_STEP_IDLE`. All CB-prefixed sub-opcodes are defined; their primary trace byte remains `CB`. This API uses the same hardware execution path as `cupid_gb_step`, so consumers do not infer fetched opcodes from an unrestricted memory view. No trace or debugger resource is added to a native snapshot.

An explicit debugger execution command stops with `CUPID_GB_DEBUG_STOP_ILLEGAL_OPCODE` after the illegal fetch. This is distinct from `CUPID_GB_DEBUG_STOP_ERROR`. The resulting hardware state remains available for inspection, saving, and instruction Backstep. Continuing playback advances the halted hardware under the ordinary execution contract.

`cupid_gb_debug_peek` reads underlying mapped memory without ticking hardware or applying CPU VRAM/OAM/wave restrictions. `cupid_gb_debug_poke` rejects ROM/unusable ranges, edits writable mapped storage, and applies device semantics for I/O. It requires the same exclusive instruction-boundary access as state operations. Raw observation and intentional edits are different contracts; an I/O edit can have hardware side effects.

`cupid_gb_debugger_evaluate` evaluates bounded, pure 16-bit expressions over registers, symbols, and side-effect-free memory views. Conditional execution breakpoints store their expression as host metadata and evaluate it only at a matching instruction boundary. An evaluation failure pauses execution with a debugger error. Invalid new definitions leave existing breakpoints intact. The Qt console exposes these operations through `print` and `break ... if ...`. See [debugging](debugging.md) for syntax and limits.

Disassembly, symbol loading/lookups, bank identification, and breakpoint management have separate APIs. Symbol-buffer loading is transactional and bounded. The Qt host implements Backstep with trusted native snapshots; C hosts must provide their own instruction history and reconcile audio/pacing after restoration. See [debugging](debugging.md) for command behavior, memory scopes, and remaining capabilities.

`cupid_gb_rom_file_bank` translates a controller-selected ROM bank to its original-file storage bank, including MMM01 menu placement and disconnected address lines. `cupid_gb_effective_rom_bank` applies that translation to the currently selected mapping. `cupid_gb_debugger_bank` supplies the identity used by symbol and breakpoint matching; for MMM01 this is the controller bank, including the unlocked `FFFE`/`FFFF` identities. Expression values and Qt `break` commands retain nine-bit qualifiers. Keep storage offsets and debugger identities distinct when embedding these APIs.

## Errors and compatibility

Lifecycle/step functions use booleans; snapshot and battery operations return typed result codes with readable name helpers. Logging reports additional loading and execution details. File loading through narrow C paths can depend on host filesystem encoding; the Qt frontend reads local cartridge and boot files through Qt before handing bytes to the core.

With GCC mixed C/C++ Release IPO, compile the C++ host with `-fno-strict-aliasing`, as the in-tree build does. Link through the core target to retain cryptography dependencies; external C++ consumers must also preserve the IPO aliasing flag in their own compilation settings.

The exposed structures are an internal integration surface, not a promised stable binary ABI. Preserve persistent model tags and version serialized formats deliberately when hardware state changes. API source presence, unit tests, and a bounded run do not establish complete hardware compatibility.

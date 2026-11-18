# Core API

`cupidgb_core` is a C11 static library with no Qt dependency. Public declarations live under `include/cupid`. Link through the CMake target to retain its platform dependencies: Windows system cryptography or optional OpenSSL 3 Crypto on Linux when native hashing is enabled. No external Crypto library is required for a portable-hash build. The core owns emulated hardware; callers own host files, windows, device queues, pacing, and lifecycle coordination.

## Instance lifetime

Use a zero-initialized instance. `CupidGb` is large, so heap allocation avoids small platform stack limits. Its ROM allocation and optional cartridge-SRAM allocation belong to the core. Initialization retains their capacity and the selected model while clearing guest state; an uninitialized stack object is not a valid input. Cleanup releases both allocations. A shallow structure copy shares their pointers and is not an independent owner.

Rebuild the library and its hosts against matching headers when public ownership fields change. Native-state compatibility follows the explicit wire format rather than `sizeof(CupidGb)`.

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

## Cartridge storage

Loading a cartridge resets hardware on success. The loader validates size, header and supported storage, then prepares every required ROM and SRAM allocation before changing the live core. Failure preserves the current session. Input may alias owned ROM or cartridge RAM when the complete source range is valid; the loader copies it before erasing reused storage or releasing an old allocation. ROM storage follows the actual input length, with a 32 KiB minimum and a 1 GiB image limit. ROM size and allocation capacity are separate: missing bytes in the physical bank population read as `0xff`, and hashes cover only the supplied bytes.

A complete frontend replacement also validates battery and boot inputs. Build a separate candidate and swap it in only after all those inputs succeed. The Qt frontend uses that pattern. Native resets and state imports retain the receiving ROM pointer, capacity and load generation. They can replace the SRAM allocation; neither allocation's pointer or capacity is serialized.

`CUPID_GB_INLINE_RAM_SIZE` describes the fixed 128 KiB array and historical state prefix. `CUPID_GB_MAX_RAM_SIZE` is the 2 MiB active-storage ceiling. Loaded TPP1 cartridges above 128 KiB use `cartridge_ram_heap` with an exact `cartridge_ram_capacity`; smaller populations use the inline array with zero heap capacity. Other controllers retain their existing physical limits. Use the inline constant when working with the fixed array, and `cartridge_ram_size` for the active span.

The accessors in `cupid/gb/cartridge.h` select contiguous active storage:

```c
uint8_t *cupid_gb_cartridge_ram(CupidGb *gb);
const uint8_t *cupid_gb_cartridge_ram_const(const CupidGb *gb);
```

Their pointers are borrowed. Stay within `cartridge_ram_size`, never free the returned pointer, and reacquire it after loading a cartridge, resetting or importing a state. Only a null instance returns null; a non-null pointer does not establish that a cartridge has any SRAM.

## GBS music programs

`cupid/gb/gbs.h` provides a C/C++ interface for loading GBS version 1 buffers, reading metadata and starting a track. Initialize and select the hardware model as for a cartridge, then call `cupid_gb_load_gbs`. Check its `CupidGbGbsResult`; `cupid_gb_gbs_result_name` supplies a diagnostic. The existing step, audio and state APIs execute the loaded program.

The loader copies the 112-byte header and payload into owned storage. It prepares a power-of-two ROM image of at least 32 KiB, installs the format's entry code and vectors where required, and starts the header's initial track. It uses MBC3 program banking without cartridge RAM, battery storage or a cartridge clock. Metadata is returned as terminated 33-byte fields containing the original 32 Latin-1 bytes. `track_count` is literal; `first_track` is the zero-based byte obtained from the header's one-based initial value. A zero count or out-of-range initial value is preserved.

`cupid_gb_gbs_start_track` accepts a raw zero-based byte, including values outside the declared count. It initializes the guest track transactionally while keeping the program allocation, generation, callbacks, listening policy and queued output. It does not clamp the track. `cupid_gb_gbs_info` reports whether the current program is GBS and optionally returns its metadata. Successful ordinary cartridge loading clears the music descriptor.

An unloaded instance begins music with silent, uncharged audio reconstruction instead of the approximate post-boot waveform prepared by ordinary initialization. Its configured sample rate, listening policy and any queued PCM are retained. An already loaded session retains its live reconstruction history. In both cases, the track's initial sound-register writes then update that history normally; preserving a stream does not mean discarding new voltage transitions or leaving every reconstruction byte unchanged.

Use `cupid_gb_gbs_start_track` to restart music. The generic `cupid_gb_reset` retains the music descriptor and banking history but applies an ordinary hardware reset; it does not choose or initialize a track. A host that needs that separate reset boundary must call the track API afterward before resuming guest execution.

Failed loading or track initialization leaves the live instance and optional output metadata unchanged. Input may alias an owned allocation only when the complete range is valid. The metadata output must not overlap the core, source input, ROM or heap SRAM. The buffer API accepts the complete supplied payload when its length plus load address fits `CUPID_GB_MAX_ROM_SIZE`, currently 1 GiB; hosts must apply their own file policy. Qt's file and archive loader reads at most the 112-byte header plus 4 MiB of payload.

Music entry initializes CPU, timers, graphics and audio directly; it does not execute installed firmware. SGB/SGB2 start after their introduction with command reception disabled. Timer-control bit 7 requests double speed even on a non-CGB model. A prior successful extended MBC3 load can leave wider bank wiring attached to later music; fresh music loads retain that host history rather than inferring it from their own image size. Cleanup clears the history. A host that prepares replacement instances must transfer `mbc3_extended_history` from the receiving session after initializing the candidate and before loading its program.

Native version 23 binds states to the immutable music descriptor, effective bank wiring and prepared image. The current UI track selection stays with the host. The [music guide](gbs-music.md) describes desktop Reset and Restart, and [the state format](save-states.md#gbs-program-identity-in-version-23) specifies the digest and import restrictions.

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

The lower-level `cupid_gb_enter_boot_rom` retains live WRAM, HRAM, OAM, and VRAM while resetting its boot-entry registers and reconstructing wave/palette memory from the realized generation. Use it only when that memory-preserving re-entry contract is intended. Both firmware entry paths restart physical SGB/SGB2 introduction state and its independent audio owner. The Qt frontend and ROM runner use the explicit cold operation for initial firmware startup.

The startup header supplies its own C++ linkage guard. General hardware headers still use the caller's `extern "C"` wrapper described above. Fixed seeds reproduce the current generator's bytes for the same profile; byte sequences are not a stable cross-version file format. Record the build, model, seed, firmware hash, and input trace when comparing runs. See [hardware profiles](hardware.md#cold-start-memory) for the qualified distributions and remaining model limits.

## Native console reset

`cupid/gb/reset.h` provides `cupid_gb_reset(gb, CUPID_GB_RESET_HARD)` and `cupid_gb_reset(gb, CUPID_GB_RESET_QUICK)` for an already loaded instance. Both restart execution and controller access latches while retaining ROM ownership, battery storage, host callbacks, input registration, cheats and ordinary audio output history. Startup synthesis, queued batches and drop diagnostics clear. Quick Reset also retains physical WRAM, HRAM, OAM, color palettes and the DMA/object-palette I/O bytes. Both clear VRAM. Hard reset uses the configured power-on seed policy and erases active volatile cartridge storage to `FF`.

Installed firmware starts at PC `0000`; no instructions execute inside the reset call. On physical SGB/SGB2, both reset kinds start introduction phase -10 and hold CPU execution until phase 96. Without firmware, the approximate handoff starts at PC `0100` with startup completed at phase 200. For color hardware that approximation includes clearing WRAM bank 2 and OAM, so those post-boot effects differ from the immediate native retention boundary. [SGB startup](sgb-startup.md) describes the contract; the [startup checkpoint](validation-sgb-startup.md) records its paired timing, platform and playback evidence and remaining limits.

MBC3 and TPP1 clock data and common fractional history survive. HuC3 clock and alarm digits belong to its controller reset domain and clear. Reset refreshes the host observation baseline without advancing the retained clock digits. A change to persistent HuC3 digits increments the battery generation for autosave tracking. The host time callback follows the existing non-reentrant observer contract.

The operation prepares a detached candidate with independent heap SRAM. An invalid request, invalid firmware size or allocation failure leaves the complete live core and attached printer unchanged. On success it commits the candidate, releases the previous SRAM allocation, then clears the attached printer's protocol and pending job while retaining printer callbacks and emitted paper. Reacquire borrowed SRAM pointers after success. Reset does not emit sound, serial, rumble, infrared or printer notifications. Hosts must reset presentation pacing and resynchronize feedback after success.

## Execution and clocks

`cupid_gb_step` and `cupid_emulator_step` execute one hardware step: an instruction, interrupt entry, bounded HALT/STOP work, or a held adapter-startup interval. A true result means that step completed. It does not establish instruction progress or successful gameplay. False reports an invalid instance, an absent cartridge, a decoder failure, or a held startup clock that cannot advance without overflowing its 64-bit time. These functions do not sleep or synchronize with a display.

The eleven illegal primary opcodes complete their fetch, clear IE, and halt the CPU. Their step returns true so the host can continue clocks and consume subsequent graphics and sound. Interrupt requests alone cannot wake that halt because no interrupts remain enabled. A debugger can deliberately restore IE; reset or an earlier snapshot can also replace the halted state. Native states retain it through the existing CPU and IE fields, without a new format version.

An IME-clear HALT wake can return successfully with an unchanged PC and no fetched opcode. A fully frozen CGB speed-switch poll can update CPU-domain sequencing without advancing `elapsed_cycles`. Use observed step-event information when tracking executed instructions, rather than assuming every successful call fetched the byte currently at PC.

`CupidGb.elapsed_cycles` counts base-speed T-cycles. Query `cupid_gb_base_clock_hz(gb)` to convert those cycles to seconds: the SGB NTSC profile uses 4,295,454 Hz, SGB PAL uses 4,256,274 Hz, and the other current profiles use 4,194,304 Hz. A null instance returns the default 4,194,304 Hz. `cupid_gb_model_is_sgb(model)` identifies all three adapter profiles. SGB PAL has model value 6, preserving values 0 through 5 in existing native states. A regular LCD frame spans 70,224 base-speed T-cycles for every current profile. Do not infer guest time from CPU instruction count or host paint count. CGB double speed changes CPU-domain work while the base-speed clock remains the timekeeping domain.

Use `cupid_gb_presentation_cycles(gb)` for host pacing and display-history intervals. It adds the adapter's host-only startup time to guest `elapsed_cycles`, in the same base-clock units. A held startup call advances that query by 114 while leaving guest time unchanged. Input traces, serial schedules and guest performance counters continue to use guest time. After loading or resetting, rebase host deadlines before subtracting timestamps from different timelines.

Use both a cycle bound and an execution-step bound for unattended runs. The LCD can be disabled, so waiting only for `frame_ready` can leave a host loop without a useful presentation bound. Host stalls must not be corrected by silently dropping guest hardware work.

## Adapter startup

`cupid/gb/sgb_startup.h` exposes three pure queries. `cupid_gb_sgb_host_present` selects physical SGB/SGB2 hardware independently of cartridge enhancement. `cupid_gb_sgb_startup_active` covers phases -10 through 199, and `cupid_gb_sgb_startup_held` covers the CPU hold before phase 96. The existing `cupid_gb_sgb_active` reports cartridge declaration metadata. `cupid_gb_sgb_commands_enabled` separately reports physical receiver permission, which can remain enabled for an ordinary cartridge during firmware execution. Header rejection disables packet reception and pad rotation while retaining the selected pad for reads. See [boot headers](sgb-startup.md#boot-headers-and-command-permission).

Firmware entry starts at phase -10 with zero additional host time. Before phase 96, stepping advances display delivery while CPU, timer, DIV, APU, DMA, serial, input sampling and cartridge clocks stay held. The observed step reports `CUPID_GB_STEP_IDLE`. Phases 96 through 199 permit guest execution while ordinary handheld PCM remains suppressed. Phase 200 completes startup. Approximate post-boot initialization skips the introduction.

Each actual presentation generates startup audio for the old phase, advances the phase, updates adapter transfer/frozen-screen state, then sets `frame_ready` and calls the observer. Incomplete presentations also advance startup. Paused redraws and pure image composition do not. The original CupidGB artwork depends on retained phase and supports the normal border overlay. Its presentation and the original synthesis still require the qualification described in the [startup guide](sgb-startup.md).

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
| `cupid_gb_apu_drain_chunk` | One ordered chunk with handheld or startup origin; returns stereo frame count |
| `cupid_gb_apu_pending` | Separate counts of queued handheld and startup frames |

Check and clear `frame_ready` when consuming completed images. Keep color/model selection consistent with the core's compatibility behavior; a monochrome cartridge can still use a color hardware palette. The Qt frontend's display pipeline is a host component and is not part of the C core.

Drain audio regularly, including while the LCD is disabled. The ordinary queue holds 2,048 frames and startup retains at most two 3,214-frame mono batches. A prefix count orders older ordinary output before the startup batches and later ordinary output after them. Before another step, drain whenever startup output remains or ordinary occupancy reaches `CUPID_GB_AUDIO_DRAIN_WATERMARK` (1,536). Drain fully on presentations and all stop or budget exits. `tools/rom_runner.c` drains after each hardware step, including its terminal output. Draining never advances clocks or executes pending hardware.

`cupid_gb_apu_drain_chunk` returns a single `CupidGbAudioOrigin` per nonempty call. Repeat until zero to consume the complete stream. Playback accepts both origins; WAVE recording accepts `CUPID_GB_AUDIO_ORIGIN_HANDHELD` only. The legacy drain API combines chunks up to capacity for hosts that do not need origin metadata. A host that falls behind can lose newly generated output without stopping emulation; startup's saturating drop count records that loss. The [audio guide](audio.md) describes queue, overflow and partial-drain rules.

`cupid/gb/apu.h` exposes sample-rate, filter-mode, interference and channel-mute controls. The rate is stereo frames per physical model-second. A different rate is rejected without mutation while a startup batch is pending; drain the stream and retry. The current rate remains selectable. An accepted change finishes pending digital work at the old rate and updates output coefficients while retaining ordinary queued PCM, reconstruction/filter history and normalized startup synthesis history. Filter and interference setters validate their domains; channel muting leaves guest registers and digital taps unchanged. These handheld controls do not alter intro PCM. Clear or recreate host conversion state when changing formats or timelines. Global listening volume/mute, resampling and playback-device control remain outside the core.

## Input and memory access

Buttons use an active-low byte, ordered Right, Left, Up, Down, A, B, Select, Start. All released is `0xff`; pressing A alone is `0xef`. Call `cupid_gb_set_buttons(gb, released_mask)` from `cupid/gb/input.h` to supply host button state. The setter records the mask and schedules a sample, including when the same mask is submitted again. It does not immediately change sampled JOYP lines or request an interrupt. The next delivered hardware span samples the selected contacts and requests an interrupt only for a falling selected line.

A write that changes JOYP row selection samples the new active row immediately. Selecting the same row leaves the sample unchanged. Row-write sampling does not consume a pending host update, which still runs on the next hardware delivery. Both reset kinds retain held masks and any pending update while restarting the released-contact latch. Firmware entry uses `CF`; the approximate SGB/SGB2 handoff uses `FF`. A settled held key is therefore not sampled again solely because the console resets.

`cupid_gb_set_buttons_for_player(gb, player, released_mask)` accepts indexes 0–3 and returns false for an invalid instance/player. The old setter still targets player 0. The existing `joypad` field is player 0; `joypad_extra[0..2]` stores players 1–3. SGB selects the active mask through its multiplayer protocol. A nonselected mask change creates no selected-line edge. Device assignments and single-player controller folding belong to the host, which should keep distinct masks available as the guest changes player count.

`cupid_gb_rumble_enabled(gb)` queries the raw MBC5 or TPP1 motor latch. `cupid_gb_set_rumble_callback(gb, callback, user_data)` retains Boolean on/off notifications; changing between positive TPP1 levels does not notify this observer.

`cupid_gb_rumble_strength(gb)` returns the effective cartridge motor level from 0 through 3. MBC5 uses 0 or 3. TPP1 retains levels 1, 2 and 3 when its extended header declares a motor; otherwise the effective level is zero while guest register readback remains intact. `cupid_gb_set_rumble_strength_callback(gb, callback, user_data)` reports every effective level change, including positive-to-positive changes, with the const instance, new level, base-speed cycle timestamp and user pointer.

The two transition observers run synchronously on supported bus writes. `cupid_gb_set_rumble_sample_callback(gb, callback, user_data)` instead observes the effective level at the first nonfrozen portion of each CPU clock delivery, before its display work. The callback receives the const instance, level and user pointer. A longer delivery still contributes one sample. Fully frozen oscillator spans contribute none; a speed change can split a delivery into two samples. Bulk `cupid_gb_tick` calls retain one delivery per machine cycle. Internal pixel and DMA subdivisions do not add samples on their own.

`cupid_gb_set_presentation_callback(gb, callback, user_data)` observes each presentation after its handheld pixel buffers are finalized. It receives the const instance, a `CUPID_GB_FRAME_*` type and the user pointer. Normal, first-frame blank, repeat, LCD-off and artificial events are included. An immediate LCD-enable presentation can occur without advancing time or adding a motor sample. Startup audio and phase are updated before this callback. The surrounding CPU, interrupt and display delivery may still be in progress. Collect observations cheaply, avoid mutating or reentering the core, and defer device I/O until the core call returns. Registration survives initialization, reset and state loading, is excluded from snapshots, and is cleared by cleanup.

Qt registers these observers only for a cartridge with motor capability. At a presentation event it averages the collected levels and divides by three, queues that completed interval, and starts collecting the next. Samples delivered later in the same instruction remain in the next interval. After execution returns, Qt applies the controller Strength response described under [cartridge feedback](controllers.md#cartridge-feedback) and sends the queued requests. During playback, a host step without a presentation does not flush a partial average. Paused debugger execution discards partial and completed feedback at the end of each execution slice.

All motor callbacks must avoid mutating or reentering hardware. Registration survives initialization, cartridge loading, reset and state loading, and cleanup clears it. Registrations and accumulated host samples are not serialized. Reset and state decoding emit no motor notification: hosts must stop output and clear pending sample history after successful reset, loading, rewind and session changes. The hardware core does not open a controller.

`cupid_gb_mbc7_set_motion(gb, x, y)` supplies signed motion samples relative to neutral. About 112 counts correspond to one g. The cartridge copies them into its axis registers when the guest requests a sample. Native states store the sampled registers. The current host sample remains with the live instance.

The direction-only scan resolves opposing direction contacts consistently; when both rows are selected, it preserves their wired-AND result. Monochrome row selection models immediate pull-down and delayed release, including MGB timing adjustments. Physical contact bouncing and broader electrical behavior remain unqualified; the input API does not model every property of real switches.

`cupid_gb_read_u8` passively observes mapped device state while respecting banking and access restrictions. For JOYP it returns the stored active row and sampled contacts. `cupid_gb_read_bus_u8` is the guest access path; during monochrome row settling it exposes the requested select bits with the stored contact sample. `cupid_gb_write_u8` performs a device write at the current phase. These calls do not execute a CPU instruction's bus schedule. Timing checks should execute instructions or use the relevant subsystem interface deliberately.

Do not mutate internal fields from another thread. The public structures expose implementation state but do not make concurrent access safe. Pause and serialize access at an instruction boundary before saving, loading, inspecting, or changing host integration settings.

## Serial observation

Register `cupid_gb_set_serial_output_callback(gb, callback, user_data)` to observe completed internal-clock transfers. The callback receives a const instance, the SB byte latched when SC starts the transfer, and the supplied user pointer. It runs synchronously after the eighth shift, the clearing of SC's start bit, and the serial interrupt request. Other work in the enclosing machine cycle can still be pending. The callback must neither mutate nor reenter the emulator. Queue any later host work outside the callback.

The core writes no serial bytes to stdout and performs no host-stream flushing. A host can buffer, display, or ignore observed bytes. This API observes completed bytes. Separate bit-level link APIs support internal- and external-clock transfers, and `cupid/gb/link.h` coordinates two consoles on a shared physical timeline.

Registration survives initialization, cartridge loading, boot reset, and native-state loading. Cleanup or registration with a null callback clears both callback and user pointer. These host fields are excluded from snapshots: restoring guest state preserves the current registration rather than restoring an old pointer. A state load does not replay previously observed bytes.

## Paired console execution

Zero-initialize a `CupidGbLinkPair`, then call `cupid_gb_link_attach` with two distinct loaded consoles. Both consoles must have free serial and infrared callback slots. A rejected attachment leaves their registrations and the pair unchanged. Keep the pair and both consoles at stable addresses, use one host thread, and detach before destroying them.

`cupid_gb_link_step` chooses and executes one complete hardware step. Hosts with debugger or input instrumentation use `cupid_gb_link_next`, execute exactly one step on that endpoint, then call `cupid_gb_link_advance` once. Do not call `next` between execution and accounting. Check each result; an invalid connection, changed ownership, unaccounted peer step, arithmetic overflow or unchanged no-progress step is a failure. A CGB speed switch can validly consume a step without advancing presentation time when its finite oscillator-freeze countdown decreases.

The scheduler compares elapsed presentation time at each model's physical frequency. It includes SGB startup holds, preserves fractional differences through integer cross multiplication, and selects endpoint zero when their relative times are equal. CGB double speed changes instruction duration through the existing core clock path. It does not change the physical base frequency used to compare the consoles.

Serial callbacks retain each sender's launched bit and sample the peer before applying its external clock. A fresh host latch starts low; attaching during a transfer does not reconstruct an earlier launch from SB. Infrared callbacks forward actual emitted levels. Callback ownership, launched bits and the scheduling offset belong to the live host connection and add no native payload fields.

After a successful in-place reset or state load, call `cupid_gb_link_rebase`. It refreshes observed clocks, cartridge generations and model frequencies while preserving the signed scheduling offset, launched bits and incoming infrared levels. Ordinary pause/resume needs no rebase. Use `cupid_gb_link_replace` to commit a prepared console replacement before freeing the previous console; rejection leaves the connection intact. Replacement preserves that side's live host history.

Detach removes only registrations still owned by the pair. It preserves launched bits for reconnecting the same host endpoints and emits no synthetic infrared transition. Hosts that create a new pair when reconnecting existing windows must carry each window's launched bit into its new endpoint. A new attachment begins with a zero scheduling offset. Single-console state files do not capture an entire pair or the other console's timeline.

## Battery data

`cupid/gb/battery.h` provides size, encode, decode, result-name, and clock helpers. Ordinary save/load functions use the registered time provider when their controller's format requires host time; `_at` variants accept an explicit timestamp for reproducible checks. HuC3 export stores its internal baseline without polling the provider. Battery operations preserve host clock policy and fractional phase.

`cupid_gb_battery_size` returns zero when the loaded cartridge has no supported battery payload. Allocate sufficient independent storage and check both the result and returned length. Rejected loads preserve the instance. The caller handles file I/O and atomic replacement; the codec does not choose paths. Prefer this explicit result-bearing API for new hosts over the older convenience file-save helpers.

The optional byte-count pointer must not overlap the live core, any part of its owned ROM or heap-SRAM allocation, or the supplied output capacity. Both save variants reject those aliases before clearing the count; the ordinary variant also rejects them before polling its time provider. A rejected count alias leaves the core, ROM, SRAM and destination unchanged.

Battery data differs from execution state. See [battery saves](battery-saves.md) for encodings and clock semantics.

## Snapshots and rewind

Use `cupid_gb_state_size`, `cupid_gb_save_state`, and `cupid_gb_load_state` after a completed hardware step with no concurrent access. A held startup step is also a valid capture boundary. States exclude ROM/boot binaries and host resources. A restored active boot overlay needs the matching binary already available to the target session.

Normal disk-state APIs recompute cartridge identity. A rejected load leaves the complete target unchanged. The ordinary load APIs restore recorded hardware and audio history exactly. Use `cupid_gb_load_state_live` for desktop timeline changes, or `cupid_gb_load_state_live_with_context` for trusted history. Those variants retain ordinary and startup output owners together, including reconstruction, DAC/filter history, rate, listening policy, synthesis history and queued batches. They rebase the ordinary fractional phase and validate the combined state before committing. The saved hardware startup phase and host time still restore. After success, discard the entire stale stream, reset host conversion/pacing, reconcile input and redraw.

`CupidGbStateContext` caches cartridge identity for trusted in-memory rewind. It binds to one owner, ROM pointer, ROM size, load generation, and model. Every successful cartridge load advances the generation, so a context becomes stale even when the allocation and length stay the same. Failed loads and native resets preserve the generation. Discard and recreate the context after cartridge/model changes or any in-place ROM patch. Direct external writes cannot be detected by the binding checks. Never use the cached context to validate an untrusted disk file.

The codec checks a complete word-array range before reading or writing its little-endian elements and accepts unaligned buffers. New snapshots use version 26. They retain GBS program identity and the firmware-entry cartridge bus. MBC6 adds a fixed-size flash and controller record before the serial accessory descriptor. TAMA5 uses that position for its clock and command record. The version 26 payload for other cartridges is unchanged from version 25. The version 20 payload remains intact: after the version 18 serial selector it stores the full byte last fetched by CH3, active SRAM length, and bytes beyond the fixed 128 KiB SRAM prefix. The length must match the receiving cartridge; a state cannot resize it. Physical SGB/SGB2 then append their variable startup extension. Every model follows with the 85-byte header-protocol record, before the accessory descriptor; non-adapter protocol bytes must all be zero. Loading prepares independent writable storage and commits only after the complete console and accessory record validate. Failed loads preserve bytes and ownership.

The two accepted version 19 development layouts separately stored wave history and extended SRAM. The loader distinguishes them by receiving geometry, exact length and accessory framing. Versions without a wave byte duplicate their validated saved nibble into both halves of the missing byte. Live loads establish imported hardware history before executing saved pending audio clocks, then restore the receiving output history. See [the version 20 layout](save-states.md#complete-cartridge-sram-in-version-20) for compatibility and migration limits.

Version 21 records signed startup phase, host-only elapsed clocks, fixed-point synthesis history, nonzero private noise state, drop diagnostics, queue ordering and unread mono samples. Each queued rate matches the decoded effective ordinary rate, but its generation phase need not match current hardware phase. Exact loads preserve partial drains; live loads retain receiving output while restoring the saved hardware phase/time. Versions 2 through 20 import completed phase 200 and zero additional host time, with canonical empty startup output for exact loads. They do not restart the introduction. See [the startup extension](save-states.md#adapter-startup-in-version-21) for its explicit bounds.

Version 18 added the serial divider tap selected by an SC write. Reset selects the slow tap independently of SC readback. Later compatibility-mode changes retain that selection. Versions 2 through 17 derive the missing selector from the loaded operating mode and SC bit 1, preserving the raw register byte and recorded clock phase. They never borrow the receiving session's selector or issue a control write. See [serial state migration](save-states.md#serial-clock-selection-in-version-18) for the resulting readback and continuation rules.

Version 17's retained CGB tile row, pending input sampling and TPP1 mapping remain independent of later control writes. The codec preserves version 15's display wait and bus-synchronization history used by infrared sensing, interrupted pixel-fetch work and the VRAM read circuit's retained addresses and byte. Version 14's reconstruction rings, DAC delays, filter and policy history, deterministic interference and pending synthesis clocks remain intact. Versions 2 through 18 remain loadable within their 128 KiB SRAM limit, with explicit defaults for missing history. An old file cannot recreate input events, waveform edges, fetched wave bytes, scroll latches, serial selections or display synchronization it never stored. Version 13's RTC phase, motor strength and divider remainder remain independent of host clock policy.

Query the size for every capture because model, cartridge geometry, printer sections and unread startup samples change the length. Identity, integrity, final-offset and hardware-domain checks apply to every accepted version.

`cupid_gb_set_serial_link` registers bit-launch and sample callbacks for an internal-clock transfer. A host can read an external-clock peer with `cupid_gb_serial_external_bit`, then deliver the sender's bit with `cupid_gb_serial_external_clock`. Both cores need a shared host schedule. Serial registrations survive reset and state loading. External cable state remains the host's responsibility.

`cupid/gb/printer.h` provides the built-in printer endpoint. Pass the complete link returned by `cupid_gb_printer_link`; it includes a device tag alongside its callbacks and context. Ordinary cable callbacks leave the tag at `CUPID_GB_SERIAL_DEVICE_NONE`. Recognition checks both the printer tag and its factory callbacks before using its context. Detach the link before destroying the printer, and call `cupid_gb_printer_update` after execution slices to deliver completion while the wire is idle.

Native version 12 captures a recognized printer's protocol, partial serial bits, image buffer and pending job without advancing its clock or invoking callbacks. Loading into another attached printer restores that history and keeps the target's host registrations. A saved empty attachment or version 2–11 file initializes a fresh printer at the loaded guest time. A saved printer never changes the current attachment choice. The complete saved record is validated even if it will be discarded. Console reset clears an attached printer's protocol and job. See the [state format](save-states.md#printer-state-in-version-12) for the descriptor and transaction rules.

Reattaching a retained printer to an earlier guest timeline rebases its clock without consuming its pending job or invoking a callback. The next capture can therefore include it immediately. Attaching a newly created printer instead starts an empty device at the current guest time.

`cupid/gb/cheats.h` supplies host-owned rule collections, Game Genie/GameShark imports, duplicate-priority-preserving cloning, and a portable `.cht` codec. Attach with `cupid_gb_set_cheats`, mutate only at instruction boundaries, and detach before destruction. Rules intercept reads after bank selection and leave storage bytes intact. Collections remain outside native snapshots. [Cheat integration](cheats.md) describes the matching and file contracts.

The core supplies snapshot bytes. The host supplies compression, history limits, storage, and UI. Accumulate rewind age from positive presentation-time progress using the selected model clock, including startup holds. Rebase after a reset or restored timeline. Guest execution counters keep their separate meaning. The complete wire-format and continuation contract is in [save states and rewind](save-states.md).

## Infrared connections

`cupid/gb/infrared.h` exposes the shared CGB, HuC1, and HuC3 sensor. Use `cupid_gb_set_infrared_input` to supply external light and `cupid_gb_set_infrared_callback` to observe output changes. The callback receives the changed latch's level and current base-cycle timestamp. Cartridge callbacks see the newly committed latch; CGB port callbacks run before the port register changes. `cupid_gb_infrared_output` reports the combined current LED state, so querying it inside a color-port callback can still return the preceding value.

Sensor charge and decay follow guest execution. Native version 12 preserves their history on every cartridge while retaining the current host input and callback. The existing HuC3 functions delegate to this API. No transport thread, device handle, or physical link is created by these calls. Register/callback and stable-sensor comparisons are covered; noisy transition behavior still differs and requires further qualification.

## Debugger integration

`cupid/gb/debugger.h` exposes host-only `CupidGbDebugger` state. Initialize it once, clean up its owned symbol table, and reset execution after a state change or edit. Its breakpoints, watchpoints, and symbols are not guest hardware and are not serialized. A host can preserve that metadata while invalidating a running command and its history.

Begin RUN, STEP, NEXT, or FINISH with `cupid_gb_debugger_begin`, then call `cupid_gb_debugger_step` or the bounded `cupid_gb_debugger_run`. Budgets count hardware-step attempts. A budget result leaves the command active for a later slice; an actual stop pauses the debugger. Ordinary hosts can use the before-instruction `cupid_gb_debugger_check` hook for execution breakpoints. A breakpoint resumes past its stopped occurrence once, without disabling later matches.

While `cupid_gb_sgb_startup_held` is true, debugger stepping advances presentation without testing an execution breakpoint, consuming its one-fetch exemption, or incrementing instruction steps/history. STEP, NEXT and FINISH wait for guest execution to resume. Hardware-call and host time-slice budgets still bound the work. Hosts must drain generated startup output during these slices and before returning at a stop or budget boundary.

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

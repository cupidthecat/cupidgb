# Architecture

CupidGB keeps hardware emulation separate from host services.

- `src/gb` owns the SM83 CPU, memory map, timer, serial clock, DMA, graphics, audio, cartridge controllers, and SGB behavior.
- `src/gbc` owns CGB banking, palettes, DMA extensions, double-speed behavior, and color rendering.
- `src/core` owns the thin emulator lifecycle and GB/GBC system selection.
- `src/frontend/qt` owns windows, input mapping, host audio, settings, ROM workflows, and presentation.
- `tools/rom_runner.c` runs bounded ROM checks without a GUI. `scripts/run_rom_suite.py` records explicit test verdicts and private evidence.

The `cupidgb_core` static library depends on neither Qt nor SDL2. Configure with `-DCUPIDGB_BUILD_QT=OFF` for core tools and tests. The desktop executable links Qt Widgets/Multimedia and the separate SDL2 controller library.

The core uses M-cycles for CPU/timer work. `CupidGb.elapsed_cycles` counts guest base-speed T-cycles, including execution with the LCD disabled. `cupid_gb_presentation_cycles` adds the SGB host clocks spent holding guest execution during startup. Pacing and visual deadlines use that combined time; guest telemetry and scripted inputs retain guest time. `cupid_gb_base_clock_hz` supplies the conversion to physical seconds: 4,295,454 Hz for SGB NTSC and 4,194,304 Hz for the other current profiles. SGB PAL remains pending. CGB double speed advances CPU and timer work twice as often as the PPU/audio domain.

Each machine cycle advances four LCD/APU dots at normal speed and two at double speed. The interrupt-entry tail schedules stack-register effects, IE/IF selection, and the CPU T18 acknowledgment on one local timeline. Requests arriving on either side of that edge remain distinguishable. Snapshot fields retain their explicit wire encoding; timing repairs can change generated field values without changing that encoding.

[Timing and clock domains](timing.md) describes CPU/base-clock units, bus phases, interrupt sampling, the shared STAT line, audio integration, and host deadline invariants. Use it when changing a clock boundary; subsystem totals and register ordering need separate checks.

`src/gb/pixels.c` maintains background and object queues across LCD dots. Fetched bytes remain stable across later CPU writes; palette lookup happens as a pixel leaves the queue. `src/gb/ppu.c` owns scanline state, bus access modes, STAT edges, and DMA scheduling. These boundaries allow mid-scanline changes without rendering already emitted pixels again. FIFO presence alone does not establish correct fetch or interrupt timing.

The initial background FIFO contains discarded pixels. Object alignment advances the background fetcher before sampling the shared OAM word; tile data reads follow that sample. The retained OAM word survives LCD resets and can affect a later scan while DMA owns the port. Separate stages record pending object alignment, word sampling and a monochrome abort waiting for its final pixel step. The data countdown retains its own phase. Logical horizontal position, window-read history and pending background insertion survive interrupted fetch work.

`src/gb/display_clock.c` tracks the LCD waits and forced synchronization that affect VRAM read history and infrared sensing. CPU delivery boundaries and guest accesses advance that history alongside rendering; passive inspection leaves it untouched. `src/gb/vram_bus.c` resolves the first-HBlank address selection and held read byte. It retains committed bus addresses separately from pending pixel-fetch addresses and uses the CPU's physical VRAM bank even when a retained PPU tile address names the other bank. CPU and OAM DMA reads share the circuit while keeping their distinct access gates.

When OAM DMA reads VRAM, LCD fetches share its source port and can replace a previously copied OAM byte. The port retains a row until the next DMA slot. Clock callers interleave those transactions without adding oscillator, divider, audio or DMA credit. HALT, STOP, speed-switch freezes and interrupt acknowledgment retain their own clock boundaries. Transfers without shared VRAM activity keep the ordinary batched path.

The APU clocks digital channels separately from the audio output subsystem. Channel transitions feed fractional-phase reconstruction rings; DAC charge/delay, filtering and optional interference follow in smaller modules. Rational delivery credit remains separate from reconstruction phase. Sample production uses the physical model clock. Native snapshots include the complete output history for exact replay; live desktop restoration retains the receiving audio stream and listening policy. Qt converts signed stereo samples into the negotiated host format and bounds its queue. WAVE recording captures core output before global host mute and volume.

Adapter startup has its own timing, sound and artwork modules. Held calls advance 114 display dots without delivering CPU, timer, DMA, APU or cartridge clocks. A common presentation event queues the old phase's sound, advances the phase and border transfers, then notifies the host. The artwork renderer only reads retained state. Tagged audio drains preserve ordinary PCM before and after at most two startup batches; playback consumes both origins, while handheld WAVE recording excludes startup sound. [SGB startup](sgb-startup.md) describes these boundaries and the remaining qualification.

Cartridge ROM storage follows the supplied image length, with a 32 KiB minimum and a 1 GiB ceiling. Cartridge RAM keeps a fixed 128 KiB inline array; larger TPP1 populations use a separate contiguous allocation through 2 MiB. Generic storage callers use the cartridge-RAM accessors and active length. Loading prepares all allocations before modifying the live instance, including when its input aliases current ROM or SRAM. Reset prepares independent mutable SRAM while retaining ROM ownership.

`src/gb/battery.c` encodes cartridge RAM, EEPROM and controller-specific clock data without host structure dumps. Battery write generations exclude natural clock advancement; Qt uses them to schedule atomic saves after guest writes settle. `src/compat/native_v1.c` reads the frozen older snapshot layout solely for explicit battery recovery, without restoring execution state.

The public C API exposes hardware state through `CupidGb`. `src/gb/state.c` encodes a versioned native snapshot. The loader checks identity and integrity, decodes into a separate candidate with independent heap SRAM, and commits only after validation. ROM ownership and host registrations stay with the current session; successful loading may replace the borrowed SRAM pointer. `src/common/sha256.c` supplies SHA-256 for file identity and integrity. Large one-shot hashes can use Windows system cryptography or OpenSSL 3 Crypto on Linux, with a portable retry if a native operation fails. Incremental hashing remains portable. The size query accounts for the selected hardware and cartridge. Word arrays reserve one checked range and encode little-endian elements. This path accepts unaligned buffers.

Version 3 adds timing latches to the frozen version 2 payload. Version 4 adds three SGB input masks and the MBC5 motor latch. Version 5 appends sections for hardware that requires additional continuation state. Version 6 adds LCD and cartridge-bus timing fields; version 7 adds the OAM DMA phase. Version 8 adds pending JOYP row release and presentation-event timing. Version 9 adds the shared VRAM row, retained OAM word, sampled STOP VRAM gate and object-fetch stage. Version 10 adds sampled STOP OAM/palette gates and the final-pixel dot used for palette ownership. Version 11 retains monochrome pulse gates and sampled duty, and appends two disconnected-DAC codes. Version 12 appends shared infrared receiver history on non-HuC3 cartridges, then an accessory descriptor and optional printer history. HuC3 keeps its existing receiver fields. Older files receive deterministic defaults for history their writers did not maintain.

Version 13 retains fractional cartridge-clock progress, the independent divider remainder and TPP1 motor strength. Version 14 adds complete audio reconstruction/filter history, pending digital clocks and monochrome noise feedback/trigger latches. Version 15 retains LCD wait phases and bus synchronization needed by the infrared receiver, interrupted pixel-fetch work, and VRAM read history. Version 16 adds pending input sampling and independent TPP1 mapping; version 17 retains the selected CGB tile row; version 18 adds serial clock selection. Version 20 combines the retained wave byte and full cartridge SRAM, accepting both supported version 19 development layouts with strict framing. Version 21 adds SGB startup phase, host-only time and bounded output history after that prefix. The [state format guide](save-states.md) specifies exact and live restoration and the limits of older imports.

The printer protocol lives in `src/gb/printer.c`; `printer_state.c` owns its explicit pointer-free wire codec. The native loader stages console and printer state separately and commits only after both validate. Host callback registrations and already emitted paper remain outside the codec. The Qt image callback copies paper synchronously, then queues a widget refresh; it never queues borrowed core image memory.

`src/gb/infrared.c` supplies the shared receiver and output callbacks for CGB, HuC1, and HuC3. Charge and decay advance on the guest base clock. Host light input and callback registrations remain live across native-state loads. Register decoding stays with the relevant memory-map handler; the older HuC3 API delegates to the shared sensor.

Qt advances playback deadlines from the presentation cycles actually consumed, including adapter startup holds. `guest_clock_pacer.cpp` converts cycles with the selected model clock, carries fractional nanoseconds at a stable playback rate, and resets that host fraction after a timeline rebase or rate change. Slow motion and a selected turbo cap form one exact rational rate. A cached cap avoids settings reads in the frame loop. A pacing generation prevents reentrant audio callbacks from charging an old-rate interval to a newly selected deadline. Rewind uses positive presentation progress after restoring a snapshot, with the same rate and fractional-carry rules; replayed guest-cycle telemetry remains separate. LCD transitions can produce partial images without changing the guest clock. Uncapped turbo and long host-stall recovery remain separate host policies.

The main Qt timer requests a 1 ms interval during playback and 16 ms when there is no cartridge, playback is paused, a menu is open, or held rewind has exhausted its history. State transitions update that interval immediately without starting a deliberately stopped timer. Short state-operation guards retain the active interval so a nested audio callback cannot strand playback at the idle cadence. The independent controller timer keeps its 8 ms interval and refreshes delivery gates before polling. Battery saves, debugger slices, inspector refresh, and native audio retain their separate schedules; printer jobs advance with guest execution.

`screen_messages.cpp` owns transient notification text, guest-event lifetime, startup cartridge identity and a cached transparent raster. Startup CRC computation streams the padded logical ROM layout, including MMM01 menu rotation. Text uses hinted monochrome glyphs, preferring Consolas, then DejaVu Sans Mono, then the platform's fixed font. Keeping the glyphs in the text rasterizer avoids fragmented curves at native pixel sizes. The display pairs message rasters with clean frame history and composes them before blending. Repeat events age the logical message while preserving the submitted pixels for host repaints. A notification-setting change retains those pixels and the previous blend contribution until guest presentation advances. Native screenshots read the clean image. The default-disabled path uses the existing display blend without another image allocation.

Presentation callers distinguish a host redraw, a completed guest frame and a history reset. Redraws retain published message rasters even before the first guest frame; queued text waits for guest publication. Cartridge/state transitions and display conversions that invalidate history request a reset explicitly. This keeps menu and Settings actions from accidentally publishing or erasing text.

Qt owns atomic state-file writes and compressed, bounded rewind history. Large snapshots use bounded sampling to select lossless compression; larger encoded states shorten the duration that fits within the configured budget. Trusted rewind contexts cache immutable ROM identity and load generation. Replacing SRAM during a successful native reset or restore preserves that binding. Cartridge/model replacement and future ROM patching invalidate it. Disk operations retain complete hashing. [The state format and workflows](save-states.md) describe the contract.

Controller transport, binding policy and native UI are separate modules. `controller_backend.cpp` owns SDL handles, physical/effective input snapshots, event draining, device identity and bounded rumble requests. It initializes no SDL video or audio. `controller_manager.cpp` evaluates typed bindings and hysteresis, combines device contributions and stages host configuration. `controller_dialog.cpp` owns the draft settings and capture workflow. MainWindow combines those contributions with keyboard input, maps single-player/SGB assignments and dispatches existing actions. A cached player-count check updates routing at hardware-step boundaries, preserving held masks through SGB mode changes. Menus temporarily block execution without rewriting the user's pause state; modal settings explicitly preserve it.

MBC5 bus writes update a guest motor latch and can notify a const host observer. The Qt observer only accumulates scalar base-cycle duty information; it does not reenter hardware. The next forward interval sends feedback through the controller manager. Timeline, pause, focus and target changes stop/reset host requests. Callback pointers, SDK handles and duty counters remain outside snapshots. [Controller details and limits](controllers.md) include shared SDL event ownership and disconnected-device feedback behavior.

`src/gb/debugger.c` owns host debugger state, bank-aware breakpoints, symbols, disassembly, and bounded execution commands. `cupid_gb_step_observed` reports the actual fetch/interrupt/idle event through a caller-owned output value; it shares the ordinary execution path and stores no trace resource in hardware state. Raw inspection is separate from CPU-visible memory access, which matters when a bus lock changes the opcode the CPU fetches.

Qt's debugger window presents commands and views through callbacks. MainWindow owns instruction history, pause/audio coordination, and session invalidation. Inspector rendering is a separate module that reads hardware buffers without advancing them. Both tool windows use current-session access and discard pending edits or run targets when that session changes. [The debugger guide](debugging.md) describes their user-facing contracts and limits.

## Source map

| Files | Responsibility |
| --- | --- |
| `src/gb/gb.c` | Instance lifecycle, boot overlay, memory access dispatch, top-level instruction stepping |
| `src/gb/cpu.c` | Instruction decode, arithmetic/flags, bus phases, interrupts, HALT/STOP |
| `src/gb/timer.c` | Divider consumers, TIMA reload windows, serial edges, CPU/base-clock coordination |
| `src/gb/cartridge.c`, `cartridge_internal.h` | Header decode, controller identification, shared post-boot and controller defaults |
| `src/gb/reset.c` | Detached native reset, retained storage/output and committed accessory reset |
| `src/gb/ppu.c`, `src/gb/pixels.c` | LCD timing and persistent pixel fetch/merge queues |
| `src/gb/display_clock.c`, `src/gb/vram_bus.c` | LCD synchronization history and shared VRAM read ownership |
| `src/gb/apu.c`, `src/gb/apu_noise.c` | Digital channel state, sequencer, noise feedback and pending-clock delivery |
| `src/gb/audio_synthesis.c`, `audio_output.c`, `audio_filter.c` | Fractional transition reconstruction, output scheduling, DAC response and filtering |
| `src/gb/audio_state.c`, `state_io.h` | Portable audio history, bounded fields and snapshot timing validation |
| `src/gb/input.c` | Physical button masks, selected JOYP lines, input interrupts |
| `src/gb/infrared.c` | Shared infrared sensing, LED latches, and host connection callbacks |
| `src/gb/printer.c`, `src/gb/printer_state.c` | Printer protocol, image jobs, and native device-state codec |
| `src/gb/sgb.c` | SGB command packets, palette/attribute state, masking, border rendering |
| `src/gb/sgb_startup.c`, `sgb_startup_audio.c`, `sgb_startup_render.c` | Host startup timing, bounded sound delivery and pure CupidGB artwork |
| `src/gb/state_startup.c` | Validated portable startup continuation and output ownership |
| `src/gbc/cgb.c` | Color hardware reset, memory-bank addressing and register dispatch |
| `src/gbc/cgb_palette.c`, `cgb_dma.c`, `cgb_render.c` | Compatibility colors and palette ports, color DMA, and the direct scanline API |
| `src/gb/battery.c`, `src/gb/state.c` | Explicit battery and execution-state codecs |
| `src/compat/native_v1.c` | Frozen older-state battery recovery |
| `src/frontend/qt/main_window.cpp`, `main_window_reset.cpp` | Desktop session lifecycle, reset transactions and menu actions |
| `src/frontend/qt/main_window_audio.cpp`, `main_window_input.cpp`, `main_window_motion.cpp`, `main_window_session.cpp` | Host audio, input/controller routing, motion, pacing, rewind and state files |
| `src/frontend/qt/main_window_display.cpp`, `main_window_debugger.cpp`, `main_window_telemetry.cpp` | Display settings/presentation, debugger integration and bounded runtime reports |
| `src/frontend/qt/main_window_*tests.cpp` | Generated native Qt workflow checks |
| `src/gb/cheats.c`, `src/frontend/qt/cheats_window.cpp`, `cheat_search.cpp` | Read interception and file codec, native cheat controls, physical RAM searches |
| `src/frontend/qt/controller_backend.cpp`, `controller_manager.cpp`, `controller_dialog.cpp` | SDL device transport, bindings/preferences, native controller settings/capture |
| `src/frontend/qt/audio_converter.cpp`, `wave_recorder.cpp`, `display_pipeline.cpp` | Host sample conversion, WAVE encoding, display viewport/palette/blending helpers |
| `src/frontend/qt/color_pipeline.cpp` | Cached CGB/SGB RGB555 correction and ambient temperature tables |
| `src/frontend/qt/display_files.cpp`, `main_window_display_files.cpp` | Palette file codec, timestamped screenshots, clipboard and native file dialogs |
| `src/frontend/qt/audio_buffer.h` | Generated-audio queue and reentrant host-write transactions |

Serial completion calls an optional host observer with the byte latched at transfer start. The core owns transfer timing, SB/SC updates, and interrupt requests; the caller owns diagnostics and output buffering. `cupidgb_rom_runner` installs its own observer for verdict text and counters. Ordinary desktop execution performs no serial writes or flushes on stdout. Observer pointers are host resources preserved across guest resets and state loads, and cleared at cleanup.

## Host lifecycle and ownership

Rewind age advances independently of the guest cycle counter. A retained-history reset takes guest time back to zero without making existing snapshots appear older by an unsigned wraparound. Restoring a snapshot restores its history age and guest-clock baseline together. Identical replacement cartridge bytes can retain snapshots, but their trusted codec context must be rebound to the new allocation.

The frontend reads Unicode paths through Qt and constructs a candidate emulator before replacing the live session. Cartridge header/model checks, battery decoding, and optional boot loading must succeed before replacement. It flushes the previous cartridge's battery data first and preserves the current session if validation fails.

Shared cartridge defaults serve initial loading and native reset. The display observer projection runs after the cartridge header is available. Native reset prepares a detached core, preserves host output and registrations, and clears the attached printer only after commit. The cartridge loader resets hardware only after input validation and allocation succeed. A failed cartridge load preserves the live core. Hosts that also load battery data or firmware during replacement should prepare a separate candidate and commit only after every step succeeds. See [the core API](core-api.md) for allocation and ownership contracts.

Core execution and UI operations are serialized through the frontend event loop. Audio playback uses a separate host backend with a protected output queue. It must not access or mutate emulated hardware concurrently. Snapshot APIs require an instruction boundary without concurrent core access.

The host tracks emulated battery write generations separately from natural clock progression. It applies a quiet-period save timer and flushes at lifecycle boundaries. Host dirty generations and file paths are not emulated hardware and are excluded from guest snapshots.

## Presentation and pacing

Frontend pacing uses elapsed host time and bounded guest intervals. LCD-off execution still needs a host interval even when no VBlank arrives. Each event-loop turn performs bounded work so input, dialogs, and close remain responsive. Audio backpressure pauses additional guest work when generated output exceeds the queue limit; it does not discard guest cycles to catch up.

The display pipeline maps monochrome shades, keeps hardware color palettes separate, chooses the host viewport, and optionally mixes consecutive completed images. These operations do not modify core framebuffers or hardware timing. Native screenshots and blended window presentation are distinct outputs.

After a successful state load, the frontend reapplies physical input, clears queued host audio, and resets pacing/presentation history. Failed loads preserve those resources. Rewind validates its target before consuming history, restores current input and guest hardware, and retains the receiving audio output history and listening policy. It clears stale playback, then executes a forward interval to produce the replayed image and sound. The previously submitted image stays visible until that interval publishes. Exhaustion has an explicit stopped-replay state, released by a mode change or new forward history.

Completed display and rewind bookkeeping precede any host audio call that can dispatch Qt events. After that call, the frame path returns without touching guest or presentation state. The scheduler rejects stale session, timeline, mode or pacing results. A focus or mode change during guarded backend dispatch defers its audio reset until the guard and sink leases have been released.

`turbo_audio_window.cpp` selects whole frames after host-format conversion using a retained periodic phase. Its bounded software queue is separate from the native sink's buffer. Normal playback keeps its existing backpressure. Replay sound and Turbo selection do not change the forward-only recording input.

## Build and test boundaries

CMake builds one Qt-free static core and links the desktop/tool targets against it. Release IPO is enabled after a C/C++ toolchain support check, with a normal-compilation fallback and no IPO in sanitizer builds. GCC C++ Release targets use `-fno-strict-aliasing` with IPO because hardware records cross the C/C++ boundary. A mixed-language regression checks those interactions under the active build configuration. Tests include generated hardware programs, explicit serialization contracts, isolated host conversion/display helpers, and an integrated Qt workflow check. External ROM corpora and commercial captures remain local inputs.

Treat core tests, Qt workflow tests, native backend checks, paired behavioral comparisons, and sustained commercial sessions as separate evidence. [Testing and measurements](testing.md) describes how to record and interpret them.

Remaining hardware and desktop requirements are tracked in [the parity checklist](parity.md). Source presence alone does not establish runtime compatibility.

Display preferences resolve when settings are applied. Frame conversion selects color or monochrome mode once for the image. The cached host palette does not alter guest palette memory or temporal blending history.

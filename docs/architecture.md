# Architecture

CupidGB keeps hardware emulation separate from host services.

- `src/gb` owns the SM83 CPU, memory map, timer, serial clock, DMA, graphics, audio, cartridge controllers, and SGB behavior.
- `src/gbc` owns CGB banking, palettes, DMA extensions, double-speed behavior, and color rendering.
- `src/core` owns the thin emulator lifecycle and GB/GBC system selection.
- `src/frontend/qt` owns windows, input mapping, host audio, settings, ROM workflows, and presentation.
- `tools/rom_runner.c` runs bounded ROM checks without a GUI. `scripts/run_rom_suite.py` records explicit test verdicts and private evidence.

The `cupidgb_core` static library has no Qt dependency. Configure with `-DCUPIDGB_BUILD_QT=OFF` for core tools and tests. The desktop executable links Qt Widgets and Multimedia.

The core uses M-cycles for CPU/timer work. `CupidGb.elapsed_cycles` counts base-speed T-cycles, including execution with the LCD disabled. `cupid_gb_base_clock_hz` supplies the conversion to physical seconds: 4,295,454 Hz for SGB NTSC and 4,194,304 Hz for the other current profiles. SGB PAL remains pending. CGB double speed advances CPU and timer work twice as often as the PPU/audio domain. Frontend pacing must use guest time rather than assuming one completed image per instruction budget.

Each machine cycle advances four LCD/APU dots at normal speed and two at double speed. The interrupt-entry tail schedules stack-register effects, IE/IF selection, and the CPU T18 acknowledgment on one local timeline. Requests arriving on either side of that edge remain distinguishable. Snapshot fields retain their explicit wire encoding; timing repairs can change generated field values without changing that encoding.

[Timing and clock domains](timing.md) describes CPU/base-clock units, bus phases, interrupt sampling, the shared STAT line, audio integration, and host deadline invariants. Use it when changing a clock boundary; subsystem totals and register ordering need separate checks.

`src/gb/pixels.c` maintains background and object queues across LCD dots. Fetched bytes remain stable across later CPU writes; palette lookup happens as a pixel leaves the queue. `src/gb/ppu.c` owns scanline state, bus access modes, STAT edges, and DMA scheduling. These boundaries allow mid-scanline changes without rendering already emitted pixels again. FIFO presence alone does not establish correct fetch or interrupt timing.

The APU integrates channel output over each sample interval and keeps coupling-capacitor state separate from channel power. Host sample-rate selection changes sample production rather than the guest audio clock. Sample intervals and filtering use the physical model clock; an SGB second and a CGB second both produce the configured number of sample frames. Qt consumes signed stereo samples, applies host format conversion, and bounds its output queue. WAVE recording captures those guest samples before host mute and volume.

`src/gb/battery.c` encodes cartridge RAM and MBC3 clock data without host structure dumps. Battery write generations exclude natural clock advancement; Qt uses them to schedule atomic saves after guest writes settle. `src/compat/native_v1.c` reads the frozen older snapshot layout solely for explicit battery recovery, without restoring execution state.

The public C API exposes hardware state through `CupidGb`. `src/gb/state.c` encodes an explicit, versioned native snapshot format rather than serializing that structure. The loader checks identity and integrity, decodes into a separate candidate, and commits only after complete validation. Host resources and input binaries stay with the current session. `src/common/sha256.c` supplies SHA-256 for file identity and integrity. Large one-shot hashes can use Windows system cryptography or optional OpenSSL 3 Crypto on Linux, with a complete portable retry if a native operation fails. Incremental hashing remains portable. The state-size query returns the current format's fixed wire length; encoding and loading still verify their exact final offsets. Version 3 appends hardware timing latches to the frozen version 2 payload, and older files receive deterministic defaults for missing history. Word arrays reserve one complete checked range and encode each element in little-endian order. This path supports unaligned buffers.

Qt advances playback deadlines from the base-speed T-cycles actually consumed, rather than charging every completed image as a full frame. `guest_clock_pacer.cpp` converts cycles with the selected model clock, carries fractional nanoseconds at a stable playback rate, and resets that host fraction after a timeline rebase or rate change. LCD transitions can produce partial images without changing the guest clock. Turbo, frame rewind, and long host-stall recovery remain separate host policies.

Qt owns atomic state-file writes and compressed, bounded rewind history. Trusted rewind contexts cache immutable cartridge identity; disk operations retain complete hashing. Cartridge/model replacement and future ROM patching must invalidate the context. [The state format and workflows](save-states.md) describe the contract.

`src/gb/debugger.c` owns host debugger state, bank-aware breakpoints, symbols, disassembly, and bounded execution commands. `cupid_gb_step_observed` reports the actual fetch/interrupt/idle event through a caller-owned output value; it shares the ordinary execution path and stores no trace resource in hardware state. Raw inspection is separate from CPU-visible memory access, which matters when a bus lock changes the opcode the CPU fetches.

Qt's debugger window presents commands and views through callbacks. MainWindow owns instruction history, pause/audio coordination, and session invalidation. Inspector rendering is a separate module that reads hardware buffers without advancing them. Both tool windows use current-session access and discard pending edits or run targets when that session changes. [The debugger guide](debugging.md) describes their user-facing contracts and limits.

## Source map

| Files | Responsibility |
| --- | --- |
| `src/gb/gb.c` | Instance lifecycle, boot overlay, memory access dispatch, top-level instruction stepping |
| `src/gb/cpu.c` | Instruction decode, arithmetic/flags, bus phases, interrupts, HALT/STOP |
| `src/gb/timer.c` | Divider consumers, TIMA reload windows, serial edges, CPU/base-clock coordination |
| `src/gb/cartridge.c` | Header decode, controller identification, load-time banking/storage metadata |
| `src/gb/ppu.c`, `src/gb/pixels.c` | LCD timing and persistent pixel fetch/merge queues |
| `src/gb/apu.c` | Channel state, sequencer, sample integration, filtering, PCM output |
| `src/gb/input.c` | Physical button masks, selected JOYP lines, input interrupts |
| `src/gb/sgb.c` | SGB command packets, palette/attribute state, masking, border rendering |
| `src/gbc/cgb.c` | Color memory/register extensions, compatibility palettes, HDMA and color helpers |
| `src/gb/battery.c`, `src/gb/state.c` | Explicit battery and execution-state codecs |
| `src/compat/native_v1.c` | Frozen older-state battery recovery |
| `src/frontend/qt/main_window.cpp` | Desktop session lifecycle, actions/settings, input, audio queues, rewind, capture, reports |
| `src/frontend/qt/audio_converter.cpp`, `wave_recorder.cpp`, `display_pipeline.cpp` | Host sample conversion, WAVE encoding, display viewport/palette/blending helpers |

Serial completion calls an optional host observer with the byte latched at transfer start. The core owns transfer timing, SB/SC updates, and interrupt requests; the caller owns diagnostics and output buffering. `cupidgb_rom_runner` installs its own observer for verdict text and counters. Ordinary desktop execution performs no serial writes or flushes on stdout. Observer pointers are host resources preserved across guest resets and state loads, and cleared at cleanup.

## Host lifecycle and ownership

The frontend reads Unicode paths through Qt and constructs a candidate emulator before replacing the live session. Cartridge header/model checks, battery decoding, and optional boot loading must succeed before replacement. It flushes the previous cartridge's battery data first and preserves the current session if validation fails.

The direct core loader resets its instance and is not a transactional session replacement API. Other hosts need the same candidate/swap pattern when failed loads must preserve execution. See [the core API](core-api.md) for allocation and ownership contracts.

Core execution and UI operations are serialized through the frontend event loop. Audio playback uses a separate host backend with a protected output queue. It must not access or mutate emulated hardware concurrently. Snapshot APIs require an instruction boundary without concurrent core access.

The host tracks emulated battery write generations separately from natural clock progression. It applies a quiet-period save timer and flushes at lifecycle boundaries. Host dirty generations and file paths are not emulated hardware and are excluded from guest snapshots.

## Presentation and pacing

Frontend pacing uses elapsed host time and bounded guest intervals. LCD-off execution still needs a host interval even when no VBlank arrives. Each event-loop turn performs bounded work so input, dialogs, and close remain responsive. Audio backpressure pauses additional guest work when generated output exceeds the queue limit; it does not discard guest cycles to catch up.

The display pipeline maps monochrome shades, keeps hardware color palettes separate, chooses the host viewport, and optionally mixes consecutive completed images. These operations do not modify core framebuffers or hardware timing. Native screenshots and blended window presentation are distinct outputs.

After a successful state load, the frontend reapplies physical input, clears queued host audio, and resets pacing/presentation history. Failed loads preserve those resources. Rewind intentionally replaces snapshots and mutes backward playback; forward audio generation resumes on the restored timeline.

## Build and test boundaries

CMake builds one Qt-free static core and links the desktop/tool targets against it. Release IPO is enabled after a C/C++ toolchain support check, with a normal-compilation fallback and no IPO in sanitizer builds. GCC C++ Release targets use `-fno-strict-aliasing` with IPO because hardware records cross the C/C++ boundary. A mixed-language regression checks those interactions under the active build configuration. Tests include generated hardware programs, explicit serialization contracts, isolated host conversion/display helpers, and an integrated Qt workflow check. External ROM corpora and commercial captures remain local inputs.

Treat core tests, Qt workflow tests, native backend checks, paired behavioral comparisons, and sustained commercial sessions as separate evidence. [Testing and measurements](testing.md) describes how to record and interpret them.

Remaining hardware and desktop requirements are tracked in [the parity checklist](parity.md). Source presence alone does not establish runtime compatibility.

Display preferences resolve when settings are applied. Frame conversion selects color or monochrome mode once for the image. The cached host palette does not alter guest palette memory or temporal blending history.

# Validation record

This record starts with the desktop and timing checks on 2026-11-04 and includes later work through 2026-11-07. Results apply to each stated build, input set, and scope. They do not establish complete hardware or frontend parity. The comparison source revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`.

## Builds and automated checks

| Environment | Configuration | Result |
| --- | --- | --- |
| Windows x64, MSVC 19.43, Qt 6.8.3 | Release desktop/core | Nine CTest checks, visible frontend workflow smoke, local deployed executable |
| Ubuntu 24.04 under WSLg, GCC 13.3, Qt 6.4.2 | Release desktop/core | Nine CTest checks, native Xcb frontend workflow smoke |
| Ubuntu 24.04, GCC 13.3 | Debug core with ASan/UBSan | Seven CTest checks, leak detection enabled |

The checks cover core lifecycle, CPU execution, serial timing, sprite priority, DIV-driven audio sequencing, CPU bus phases, generated-ROM runner protocols, host audio conversion, and Qt workflows. The Qt workflow check uses an isolated settings directory and generated cartridge; it covers Unicode paths, pause, input release, failed-open preservation, screenshots, battery reset/load, settings persistence, and boot-overlay selection. It does not open an audio device.

The native Windows audio run exposed and verified a fix for a shutdown callback that accessed a destroyed audio sink. Deployed smoke checks require no Qt installation paths in the application environment. This is local folder-deployment evidence, not an installer qualification.

## External ROM checks

- Eleven CPU instruction cases produced explicit serial success results.
- Sixty-four of the same sixty-six selected DMG acceptance cases produced explicit register success results. Two remain inconclusive: the interrupt transition case and the boot-divider case.
- Twelve DMG and twelve CGB sound cases produced explicit memory-signature success results. Paired runs used identical ROM hashes and boot binaries and also passed in the pinned comparison build.
- Both acid2 final images matched the pinned comparison build with zero differing pixels out of 23,040, using matching hardware models, shared boot binaries, grayscale, and disabled color correction. These are stable final-image comparisons, not timing coverage.

The remaining interrupt-transition case passes in the comparison build with either tested boot binary. The boot-divider case passes there with the local boot binary and remains inconclusive with the generated boot binary. CupidGB still has startup and interrupt/PPU timing gaps.

## Commercial runtime checks

Private input traces reached active Tetris and Kirby gameplay, the first Super Mario Land stage, and Pokemon dialogue. Five cartridges were checked in bounded headless comparisons using shared boot files, models, and inputs. Final-image matches varied by case and source revision; these runs do not establish sustained image/audio parity.

Native desktop samples ran with normal audio and no turbo. They recorded guest base-clock progress separately from images submitted to the widget and widget paint events. A paint event does not prove host display presentation.

Windows Release samples ran Tetris and a CGB cartridge sequence for sixty seconds each. Their guest speed measurements were approximately 99.9% of the nominal clock. Both processes exited successfully with zero audio backend errors, but reported idle events. Tetris showed active gameplay; the CGB sequence remained in intro dialogue. The host used 48 kHz stereo float output converted from the core's 44.1 kHz signed samples.

Linux native runs also used the actual audio device. Audio-backend readiness matters under WSLg: a run with no usable sink is excluded from audio evidence. Backend idle events occurred in successful samples, so glitch-free playback and audible quality remain unverified.

The private receipts retain source manifests, executable and ROM hashes, models, boot hashes, traces, screenshots, host audio formats, queue sizes, paint intervals, process resource samples, elapsed time, and exit codes. Runtime samples preceding final input/save guards or operand-bus fixes remain identified by their own source manifests. CI checks the exact pull-request head separately.

At the desktop milestone, save-state loading, RTC persistence, controllers, debugger tools, link devices, uncommon cartridges, all hardware revisions, full PPU timing, and long-term resource stability remained open. The state milestone below adds native state loading and rewind; the other gaps remain. See [the parity checklist](parity.md).


## Native states and rewind milestone

Windows MSVC Release and Linux GCC Release passed ten CTest checks each. Native Qt workflow checks cover ten slots, atomic writes, missing-only filename fallback, failed-load preservation while paused and running, Unicode state drops, physical-input reconciliation, replay, rewind history eviction, and disabled capture. An additional local `--smoke-test --audio-check` requires an actual sink and checks forward playback after loading and rewind, followed by clean shutdown.

Linux ASan/UBSan passed eight core checks with leak detection enabled. State coverage includes 112 generated continuation scenarios across the current six hardware profiles, pending hardware events, emitted PCM, framebuffers, every truncation boundary, independently valid but out-of-domain payloads, wrong input identity, and trusted-context binding. Additional cases cover the ten recognized cartridge controller types and extreme restored RTC timestamps. The RTC regression reproduces signed overflow in the previous arithmetic; the corrected path preserves exact modular counter results without signed overflow or narrowed day counts.

Windows and Linux generated byte-identical native-v1 fixtures: 300,349 bytes, SHA-256 `ac3df28d89204f9a5d91c74570cdae32c9ee7a8e5cb7d54e321ef7f20ae9ab5c`. The fixture is now an explicit wire-format regression check. Native snapshots qualify exact continuation within the tested scenarios; BESS imports and other state formats remain unsupported.

Caching immutable cartridge identity reduced snapshot cost while retaining the same capture cadence, guest cycles, and stored history. In comparable ten-second Linux runs of the same 1 MiB cartridge, 599 captures took about 2.747 seconds with repeated full-ROM hashing and 1.019 seconds with the trusted rewind context. Disk operations retain full-ROM hashing. The cache contract requires invalidation after cartridge replacement, model changes, or any future in-place ROM patch.

Cached, audio-enabled sixty-second CGB sequences completed on both platforms with approximately 99.98% nominal guest-clock progress and 3,585 snapshots. Both retained about 59.99 seconds in 28.06 MB. Snapshot capture took approximately 6.47 seconds total on Linux and 6.48 seconds on Windows. These were forward introduction/dialogue sequences with rewind capture enabled, not a sixty-second rewind playback or complete commercial-game qualification. Local workflow tests separately exercised backward restoration and audio resumption.

The Windows sequence reported zero audio errors and two backend idle events; the Linux sequence reported neither in that sample. This does not establish glitch-free audio across workloads. Windows process CPU counters failed an independent single-thread busy-loop calibration and are excluded from CPU-utilization claims. Private receipts retain elapsed timing, memory, audio, queue, capture, paint, and source identity measurements.


Expanded local GBC inputs were then checked with private copies of Crystal, Mario Tennis, and Tomb Raider. After a bounded input sequence, each snapshot/restore replay matched the complete encoded end state, both framebuffers, PCM bytes and sample counts, and guest cycles. Original ROM hashes remained unchanged and existing user saves were untouched. The captured scenes were intro dialogue, court play, and a main menu respectively; this is bounded replay evidence, not full-game compatibility.

A separate native Tomb Raider menu run completed sixty seconds with normal audio and rewind capture: approximately 99.987% nominal guest-clock progress, 3,585 snapshots, about 97.27 MB of retained history, zero reported audio errors or idle events, and a clean process exit. This qualifies sustained menu operation on that input, not commercial gameplay or universal full-speed operation.

## Independent pixel pipeline and desktop save/audio milestone

The 0.3.0 implementation replaces whole-line drawing with persistent dot-driven fetch queues. It also replaces the identified adapted timing and SGB routines with independently written implementations. The obsolete hardware notice file and its directory are removed; the root project license is unchanged. The comparison revision remains `ea667ad79aa28409f034b1fd6da7304e1844750d`.

Windows MSVC Release with Qt 6.8.3 and Ubuntu 24.04 under WSLg with GCC and Qt 6.4.2 each passed all 14 CTest checks. Native Windows and Xcb workflow runs also passed with a real audio sink. Linux GCC Debug passed all 11 core/tool checks with AddressSanitizer, UndefinedBehaviorSanitizer, and leak detection enabled. The supplied formatting file matches the installed repository configuration byte for byte; changed project sources pass its formatting check.

New coverage includes timer edges and reload windows, HALT polling before and during its first idle cycle, held-key STOP behavior, serial control writes, physical model selection with color-only headers, MBC3 bank aliases, CGB object-priority latching, LCD-off STAT behavior, SGB packet commit timing, and malformed snapshot rejection. Normal-store LCDC write regressions cover 13 store forms across six profiles and both SP-byte positions. A paired object-timing probe matched all 210 selected configurations. These scopes do not cover every CPU bus conflict or object combination.

Native version 2 stores pending pixel fetches, audio integration/filter state, object priority, and the first HALT polling phase. Continuation tests include the earlier generated scenarios, 64 additional queue/window scenarios, and a first-HALT boundary restoration. Rehashed malformed map addresses are rejected before a CGB attribute fetch can cross VRAM's boundary. Windows and Linux check the same 300,518-byte portable fixture with SHA-256 `bceb48a1fadaad1dcf305f6f7bdf27dcefbb291e853eb788b61bdc4bdcf6978b`. Version 1 battery export is tested separately; older execution states cannot continue in this core.

Qt runtime checks exercise actual Settings accept/cancel and conflict dialogs, button/mode/shortcut persistence, standalone modifiers, Shift+Tab, quiet-period battery saves, explicit older-state battery export, WAVE recording, and a session-only boot-directory override. Recording checks verify PCM channel order, amplitude, byte order, RIFF limits, atomic finalization, and preservation of previous files after failures. Real-audio workflows cover fresh stream setup, state loading, rewind, and forward resumption. Host audio starts after a short reserve of generated samples; this introduces host latency without inserting silence or changing guest cycles. Backend setup blocks guest timer reentry while Qt processes native audio events.

### Equivalent-input comparisons

The final shared-boot comparison used identical ROM hashes, model choices, boot binaries, and protocol limits in both builds:

| Corpus | CupidGB | Comparison build |
| --- | --- | --- |
| Eleven CPU instruction cases | 11 explicit serial passes | 11 explicit serial passes |
| Twelve DMG sound cases | 12 explicit memory passes | 12 explicit memory passes |
| Twelve CGB sound cases | 12 explicit memory passes | 12 explicit memory passes |
| Sixty-six selected DMG acceptance cases | 61 explicit register passes, 5 bounded | 63 explicit register passes, 3 bounded |

The two CupidGB-only acceptance gaps are `intr_2_mode0_timing_sprites` and `lcdon_timing-GS`. The boot-divider, boot-I/O, and boot-serial-alignment cases are bounded in both builds with the selected generated boot binary. A separate CupidGB run without boot firmware reaches 64 explicit passes and two bounded cases. Those startup conditions are different and their counts must not be combined. This remains incomplete timing coverage.

Both acid2 final images and six bounded commercial scenes matched the comparison build with zero differing pixels out of 23,040 each. The commercial inputs were Tetris, Kirby, Super Mario Land, Crystal, Mario Tennis, and Tomb Raider. Shared boot inputs, equivalent model choices, grayscale where applicable, disabled color correction, and identical input traces were used. Captured scenes include active gameplay, dialogue, character selection, and menus. Stable final-image matches do not establish timing or audio parity throughout a game.

Separate native snapshot/replay checks ran Crystal, Mario Tennis, and Tomb Raider for 1,800 guest-clock intervals, then replayed 180 intervals from a checkpoint. All three reproduced the complete encoded end state, both framebuffers, emitted PCM bytes and sample counts, and guest cycles. These are bounded continuation checks. Existing ROMs and user saves were preserved.

### Desktop runtime samples

Final Windows and WSLg/Xcb sessions used fresh private cartridge copies, the same CGB boot binary, equivalent settings, normal audio, rewind capture, and matching per-game input traces. Each measurement lasted 60 seconds and included boot, menus, and audio-device initialization. All four ended in active Tetris or Super Mario Bros. Deluxe gameplay and exited successfully. Source manifests matched the final code throughout the runs.

| Environment and game | Nominal guest-clock progress | Audio error / idle events | Widget paint interval p95 / maximum | Peak process memory |
| --- | --- | --- | --- | --- |
| Windows, Tetris | 99.6319% | 0 / 1 | 17.98 / 190.42 ms | 73.5 MiB working set |
| Windows, Mario Deluxe | 99.6310% | 0 / 1 | 17.95 / 188.02 ms | 84.0 MiB working set |
| WSLg/Xcb, Tetris | 99.9342% | 0 / 1 | 17.57 / 23.65 ms | 86.7 MiB resident |
| WSLg/Xcb, Mario Deluxe | 99.8799% | 0 / 3 | 17.52 / 20.70 ms | 97.8 MiB resident |

Guest-clock progress is computed from base-speed T-cycles and elapsed host time. Widget paint events do not establish physical display presentation. The sessions include stream initialization and are not steady-state benchmarks. Backend idle events remain; these samples do not establish glitch-free or audibly correct playback.

Linux process user-plus-system CPU times were 12.38 and 12.28 seconds over complete process lifetimes of 61.05 and 60.95 seconds. Those lifetimes include work outside the 60-second telemetry windows. Windows sampled CPU counters are retained privately and are not used for utilization claims. Private receipts retain source and binary hashes, ROM/boot/trace identities, queue and priming metrics, paint intervals, memory samples, input counts, screenshots, and exit codes.

Remaining work includes the two measured timing mismatches, finer CPU bus conflicts, decaying open bus, extended and uncommon cartridge controllers, individual hardware revisions, wider frontend tools and devices, and long-term gameplay/audio qualification. Current release packaging and installation have not been qualified by these checks.

## Bus timing, cartridge mapping, and desktop workflows

The 2026-11-05 work corrects the two measured acceptance mismatches above. OAM reads are blocked at the scanline boundary before the visible mode changes, and STAT bit 7 reads high after real boot/reset. LCDC write phases now cover memory read-modify-write instructions, PUSH, CALL, RST, and the first interrupt stack byte. The second interrupt stack byte targeting LCDC remains unqualified; its existing interrupt-request sampling order is preserved.

Cartridge changes cover physical ROM address lines and erased tails independently of header size metadata, multicart detection and RAM selection, 2 KiB SRAM mirrors, extended MBC3 banks, exact MBC5 RAM enable decoding, and independently implemented HuC1 banking/startup behavior. Native state layout remains version 2. Core input now owns selected JOYP falling-edge interrupts and the direction-only opposing-contact policy. Physical contact bounce and monochrome row-switch settling remain incomplete.

Qt adds integer/aspect/fill scaling, logical 1x-8x window sizes with physical-pixel integer rendering, monochrome presets/custom colors, simple gamma-correct blending, SGB border visibility, and audio output-device selection with default fallback. Workflow checks cover settings acceptance/cancellation, palette output, blending history across frame advance/load/rewind, central SGB border overlays, missing audio-device choices, queued device-list notifications, and recording continuity. A simulated device-list notification does not qualify physical hot-unplug behavior.

### Builds and comparisons

Windows MSVC Release with Qt 6.8.3 and Ubuntu 24.04 under WSLg with GCC and Qt 6.4.2 each passed all 17 CTest checks. Linux Debug passed all 13 core/tool checks with AddressSanitizer, UndefinedBehaviorSanitizer, and leak detection. Native audio-enabled workflow checks passed on Windows and Xcb. Windows capture commands need `QT_FORCE_STDERR_LOGGING=1` to retain Qt diagnostics from the GUI executable. Smoke failures now identify their source line.

All comparisons retain revision `ea667ad79aa28409f034b1fd6da7304e1844750d` and equivalent inputs:

| Corpus | CupidGB | Comparison build |
| --- | --- | --- |
| Sixty-six selected DMG acceptance cases, shared generated boot | 63 explicit register passes, 3 bounded | 63 explicit register passes, 3 bounded |
| Eleven CPU instruction cases | 11 explicit serial passes | 11 explicit serial passes |
| Twelve DMG and twelve CGB sound cases | 24 explicit memory passes | 24 explicit memory passes |
| Twenty-eight MBC1/MBC2/MBC5 cases, shared generated boot | 28 explicit register passes | 28 explicit register passes |

The remaining bounded acceptance cases concern boot-divider state, boot I/O, and serial alignment. They are inconclusive in both builds under the selected generated firmware. The sprite/HBlank and LCD-startup cases that previously differed now pass under the same shared boot conditions. These counts do not cover the full available hardware corpus.

Separate generated probes matched 266,240 CGB input-row/interrupt observations with contact bounce disabled and 31,025 active cartridge-bus observations. The cartridge probe excludes disabled-RAM open bus and the unresolved unbanked-RAM discrepancy. Both acid2 final images and six bounded commercial scenes again matched with zero differing pixels out of 23,040 each. Final images do not establish complete frame-by-frame, timing, or audio parity.

### Native gameplay samples

Fresh private Tetris and Super Mario Bros. Deluxe copies ran for 60 seconds each on Windows and WSLg/Xcb with normal audio, rewind capture, matching input traces, and the same CGB boot binary. All four processes exited successfully and ended in active gameplay. Existing ROMs and user saves were preserved.

| Environment and game | Nominal guest-clock progress | Audio error / idle events | Widget paint interval p95 / maximum | Peak process memory |
| --- | --- | --- | --- | --- |
| Windows, Tetris | 99.6310% | 0 / 2 | 17.72 / 191.98 ms | 73.63 MiB working set |
| Windows, Mario Deluxe | 99.6034% | 0 / 3 | 17.72 / 201.55 ms | 83.48 MiB working set |
| WSLg/Xcb, Tetris | 99.9087% | 0 / 4 | 17.59 / 19.93 ms | 85.14 MiB resident |
| WSLg/Xcb, Mario Deluxe | 99.8777% | 0 / 3 | 17.62 / 20.91 ms | 96.37 MiB resident |

The telemetry windows include startup. The idle-event counter excludes the first 250 ms. Guest-clock progress uses base-speed T-cycles; widget paint intervals are not physical presentation FPS. Windows converted 44.1 kHz guest PCM to 48 kHz stereo float output, while Linux used 44.1 kHz stereo signed output. Backend idle events remain, so these runs do not establish glitch-free or audibly equivalent playback. Linux process user-plus-system times were 12.61 and 12.66 seconds over lifetimes of 60.17 and 60.08 seconds. Windows CPU counters are retained privately and excluded from utilization claims.

Source and binary receipts identify each run. Linux gameplay measurements preceded the smoke-only failure-diagnostic helper; final desktop regressions were rerun after that change. The input-comment clarification produced a byte-identical Linux executable. No guest timing, rendering, audio, or pacing changes followed the gameplay measurements.

### Installation and documentation

A staged Linux installation includes the executable, project license, desktop launcher, GB/GBC MIME declarations, and icon. An isolated MIME database identified `.gb` and `.gbc` inputs correctly; native installed workflow checks passed after the WSLg audio service was ready. A staged Windows folder passed native audio-enabled workflows with deployed Qt dependencies. These checks do not qualify an installer, every Linux desktop, or a machine without the tested runtime environment.

The README and thirteen guides now cover building, installation, use, hardware limits, battery files, states/rewind, troubleshooting, architecture, core integration, testing, contribution requirements, parity, and validation. All 56 local documentation links resolved, and the C11/C++17 integration examples passed compiler syntax checks. Changed project sources pass the supplied formatting configuration, and the configuration matches the supplied file byte for byte.

Remaining work includes individual hardware revisions, full fetch/bus timing, contact settling, uncommon controllers and peripherals, controller devices, debugger tools, cheats, interoperable state imports, advanced color/filter behavior, and long-term gameplay/audio qualification. The parity checklist remains open.

## Debugger tools and clock-phase repairs

The 2026-11-07 work adds an independently written debugger and hardware inspectors. CPU execution, mapped-memory inspection, and host presentation remain separate. The debugger has bank-aware execution breakpoints, symbols, disassembly of all primary/prefixed encodings with invalid opcodes marked, Step/Next/Finish, and bounded instruction Backstep. Memory, tiles, maps, OAM, and palette views operate on the current session. [The debugger guide](debugging.md) documents commands, limits, edits, and session behavior.

Desktop pacing now charges actual guest base-clock progress for partial LCD intervals and carries fractional host time across ordinary playback. A Qt regression drives the real LCD to a scanline tail and checks the resulting deadline, then checks a full LCD-off interval. Standalone pacing tests cover one-/two-/four-T-cycle chunks, all playback rates from half through normal speed, 100,000 variable intervals, reset behavior, and overflow rejection. Ordinary play no longer accumulates an unbounded paint-timing history. Bounded reports retain whole-run results and add a separate five-second post-warmup window.

Clock repairs move interrupt acknowledgment to CPU T18 and advance every double-speed machine cycle by two LCD/APU dots. Double-speed HBlank readback changes at its corrected edge. These changes do not add snapshot fields. The second interrupt stack write targeting LCDC remains unqualified.

### Equivalent-input checks

Comparisons retain revision `ea667ad79aa28409f034b1fd6da7304e1844750d`. Generated interrupt-entry probes match all 76 selected phases across normal-speed DMG/Pocket/CGB and double-speed CGB, compared with 51 matches before the repair. Observations cover base-cycle duration, destination PC, final interrupt flags, and visible LCD mode. Separate regressions check timer requests on either side of acknowledgment, equal guest-time audio generation, DMA duration, and pending LCD handoff.

Nine debugger command checkpoints match on each of DMG and CGB, for 18 matched observations. They cover stepping, nested calls, Finish, interrupt entry/return, and Next with a pending interrupt. Direct-step/observed-step regressions preserve complete native state, including a HALT wake that crosses a bus-lock edge. Another regression compares 8,192 ordinary and debugger-run steps. Read-only peeks across all 65,536 addresses preserve hardware state in the generated fixture.

Inspector rendering matches 6,114,816 pixels and 4,356 metadata values across three fixtures each for DMG-B, CGB-E, and CGB monochrome compatibility. These are direct rendering comparisons with grayscale palettes and color correction disabled, not complete gameplay rendering coverage.

| Corpus | CupidGB | Comparison build |
| --- | --- | --- |
| Sixty-six selected DMG acceptance cases, shared generated boot | 63 explicit register passes, 3 bounded | 63 explicit register passes, 3 bounded |
| Eleven CPU instruction cases | 11 explicit serial passes | 11 explicit serial passes |
| Twelve DMG and twelve CGB sound cases | 24 explicit memory passes | 24 explicit memory passes |
| Twenty-eight MBC1/MBC2/MBC5 cases, shared generated boot | 28 explicit register passes | 28 explicit register passes |

The three bounded cases still concern boot-divider state, boot I/O, and serial alignment under the selected generated firmware. Both acid2 final images and six bounded commercial scenes match with zero differing pixels out of 23,040 each. The six commercial inputs are Tetris, Kirby, Super Mario Land, Crystal, Mario Tennis, and Tomb Raider. These outcomes do not establish complete frame-by-frame timing or audible equivalence.

Crystal, Mario Tennis, and Tomb Raider each ran 1,800 guest-clock intervals before a checkpoint and replayed another 180 intervals twice. The encoded end state, both framebuffers, generated PCM/sample counts, and cycles match on replay. Mario Tennis and Tomb Raider exercised double speed. Existing cartridges and saves were preserved; these checks do not establish in-game save interoperability or long-term gameplay correctness.

### Snapshot compatibility

Native version 2 remains 300,518 bytes with the same field encoding. Windows and Linux produce the same updated fixture, SHA-256 `6106c038f80d9ef4ca4417f31305dc05bb714d9252e6caff3eec0205ea4ec0eb`. A decoded comparison isolates the changed fixture fields to the corrected two-dot clock position and dependent audio timing/integration. The preceding version 2 fixture remains loadable. Format compatibility does not preserve the earlier engine's timing bug when that state continues in the repaired core.

### Desktop and regression checks

Windows MSVC Release with Qt 6.8.3 and Ubuntu 24.04 under WSLg with GCC and Qt 6.4.2 each pass all 21 CTest checks. Linux Debug passes all 14 core/tool checks with AddressSanitizer, UndefinedBehaviorSanitizer, and leak detection enabled. The expanded Qt workflow smoke also passes under those sanitizers.

New frontend regressions cover command dispatch/history, symbol loading failures, register and memory edits, breakpoint resumption, Next/Finish cancellation, instruction Backstep, frame-advance history invalidation, preserved paused input including late key events, ROM/reset/load/rewind boundaries, and stale memory-editor cancellation. Escape, explicit rejection, and window close all cancel a running debugger command before hiding its window.

Installed Windows and Linux/Xcb builds pass native audio-enabled workflows. All six tool panes were captured and visually reviewed on each platform. The Windows run uses deployed Qt dependencies without development paths; the Linux run uses the ready WSLg audio service. These checks qualify the tested installations and workflows, not every desktop or a standalone installer.

### Clock-pacing gameplay samples

Two fresh private cartridges ran for 60 seconds each in the installed Linux/Xcb build with normal audio, rewind capture, the same CGB boot input, and the existing per-game input traces. Both ended in active gameplay and exited successfully. The source manifest remained unchanged throughout. Tetris used monochrome compatibility; Mario Deluxe entered double speed.

| Game | Whole-run guest-clock progress | Post-warmup guest-clock progress | Audio error / idle events | Post-warmup paint interval p95 / maximum | Peak resident memory |
| --- | --- | --- | --- | --- | --- |
| Tetris | 100.0225% | 99.9939% | 0 / 0 | 17.67 / 20.06 ms | 84.85 MiB |
| Mario Deluxe | 100.0222% | 100.0129% | 0 / 0 | 17.41 / 19.99 ms | 96.43 MiB |

The post-warmup window starts at an actual sample taken near five seconds. Frame-sized execution quanta can put a bounded measurement slightly above or below 100%. Paint intervals describe widget work, not physical display presentation. Zero reported backend events does not establish audible equivalence or qualify every game. Windows commercial speed measurements of this pacing change remain pending at this checkpoint; native Windows audio-enabled workflows pass.

Complete process user-plus-system CPU times were 12.66 and 13.05 seconds over lifetimes of 60.22 and 60.83 seconds. Rewind capture consumed 5.96 and 6.17 seconds inside the telemetry windows. These figures describe this host and these runs; earlier samples under different host conditions are not a controlled optimization comparison.

The README and fourteen guides cover the application and core, including debugger controls, memory scopes, state compatibility, build/install steps, testing, and remaining parity work. Local links and the C11/C++17 embedding examples are checked separately. Individual hardware revisions, finer fetch/bus timing, the second interrupt stack write targeting LCDC, contact settling, uncommon controllers/peripherals, device controls, advanced debugger commands, cheats, state interchange, advanced filters, and long-term gameplay/audio/save qualification remain open.

## Snapshot and display performance follow-up

The next change reduces host work while retaining every captured frame and all state validation. Large one-shot hashes use Windows system SHA-256 or optional OpenSSL 3 Crypto on Linux. Small hashes and incremental hashing retain the portable path; a failed native operation retries the complete portable calculation. Native output is staged before it can overwrite an input. The fixed version 2 size query no longer walks the schema repeatedly, while encoding/loading still require the exact final byte offset.

### Component measurements

A Linux GCC Release comparison used the same two captured commercial states, compiler flags, 1,000 iterations per variant, and Qt 6.4.2 compression at level 1. The baseline is commit `fe303db7b829f140574884b3ee0e70397797d7b7`. Candidate and baseline produced identical complete encoded states and compressed bytes.

| Captured state | Cached capture before / after | Capture plus compression before / after | Combined reduction |
| --- | --- | --- | --- |
| Tetris | 1,010.2 / 283.6 microseconds | 1,577.8 / 840.4 microseconds | 46.7% |
| Mario Deluxe | 1,015.7 / 233.9 microseconds | 1,663.6 / 833.4 microseconds | 49.9% |

These are component costs, not gameplay FPS. Separate native digest probes measured about 119 versus 806 microseconds on Linux and 128 versus 817 microseconds on Windows for the sampled Tetris state. Provider initialization and full application lifetimes are different measurements.

Display conversion now resolves the four host palette colors when preferences change and chooses the hardware color path once per frame. Thirty synthetic comparisons across DMG, CGB compatibility, CGB, five palettes, and blending on/off produced identical images. The prototype saved about 2-37 microseconds per converted frame, depending on mode. This small conversion saving does not establish a whole-game speedup.

### Correctness checks

Windows and Linux each pass all 22 desktop/core CTest checks. Linux passes all 15 core/tool checks with ASan/UBSan and leak detection, and the expanded Qt workflow smoke also passes under sanitizers. Forced portable hashing passes the snapshot and hash checks on Windows and sanitized Linux. Private fault-injection checks force native allocation/provider, initialization/create, update, and finalization failures on both backends, including a finalization that writes incorrect bytes before failing; digest/input alias tests still pass.

All four Windows/Linux and accelerated/portable fixture files are 300,518 bytes with SHA-256 `6106c038f80d9ef4ca4417f31305dc05bb714d9252e6caff3eec0205ea4ec0eb`. Installed native audio-enabled workflows pass on Windows and Linux/Xcb. The selected equivalent-input corpora still produce 126 explicit passes and three bounded results in each build. Both acid2 and six commercial final images still match exactly. The three commercial checkpoint replays still reproduce their state, framebuffer, PCM/sample-count, and cycle observations.

Timed native gameplay qualification for this optimization is recorded separately from these component results. The remaining hardware/frontend checklist stays open; these changes do not establish compatibility or correct speed for every cartridge.

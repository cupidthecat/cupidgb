# Validation record

This record starts with the desktop and timing checks on 2026-11-04 and includes later work through 2026-11-08. Results apply to each stated build, input set, and scope. They do not establish complete hardware or frontend parity. The comparison source revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`.

Earlier sections retain the results and assumptions of their original builds. A result without a recorded model clock does not qualify the corrected SGB clock. Use the current model-clock reports when comparing physical speed across profiles.

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

### Expanded native runtime samples

The optimized installed build ran seven private titles on each platform, for fourteen successful processes. Tetris and Mario Deluxe ran for 60 seconds each; the other five ran for 30 seconds each. Normal audio and every-frame rewind capture remained enabled. Each platform used fresh cartridge copies, matching per-game traces and boot input, and an unchanged source manifest. All reports confirmed a real audio sink, successful screenshot/save flushing, and no execution or rewind failure.

Automatic selection chose color hardware for these inputs. Monochrome titles used compatibility mode; these commercial samples do not separately qualify the physical DMG profile. Captures show active Tetris, Mario Deluxe, and Kirby gameplay; paused Super Mario Land gameplay; Crystal introduction text; Mario Tennis character selection; and a Tomb Raider load menu. The shorter runs qualify those scenes rather than complete gameplay.

| Platform | Title | Seconds | Post-warmup guest-clock progress | Post-warmup paint interval p95 / maximum | Peak process memory |
| --- | --- | --- | --- | --- | --- |
| Windows | Tetris | 60 | 100.0190% | 17.65 / 33.94 ms | 73.80 MiB working set |
| Windows | Mario Deluxe | 60 | 100.0120% | 17.67 / 40.23 ms | 84.34 MiB working set |
| Windows | Kirby | 30 | 99.9732% | 17.62 / 27.66 ms | 62.77 MiB working set |
| Windows | Super Mario Land | 30 | 100.0098% | 17.66 / 20.96 ms | 51.66 MiB working set |
| Windows | Crystal | 30 | 99.9821% | 17.70 / 25.72 ms | 51.76 MiB working set |
| Windows | Mario Tennis | 30 | 99.9755% | 17.69 / 22.23 ms | 67.66 MiB working set |
| Windows | Tomb Raider | 30 | 99.9712% | 17.66 / 38.56 ms | 81.06 MiB working set |
| WSLg/Xcb | Tetris | 60 | 99.9943% | 17.28 / 20.01 ms | 90.88 MiB resident |
| WSLg/Xcb | Mario Deluxe | 60 | 100.0136% | 17.47 / 21.72 ms | 98.63 MiB resident |
| WSLg/Xcb | Kirby | 30 | 99.9798% | 17.51 / 19.48 ms | 75.38 MiB resident |
| WSLg/Xcb | Super Mario Land | 30 | 100.0023% | 17.25 / 18.13 ms | 68.02 MiB resident |
| WSLg/Xcb | Crystal | 30 | 99.9853% | 17.37 / 19.30 ms | 65.95 MiB resident |
| WSLg/Xcb | Mario Tennis | 30 | 99.9717% | 17.63 / 24.03 ms | 85.13 MiB resident |
| WSLg/Xcb | Tomb Raider | 30 | 99.9759% | 17.51 / 18.89 ms | 93.42 MiB resident |

All fourteen whole-run and post-warmup windows report zero backend audio idle/error events. Windows whole-run guest-clock progress ranges from 99.4289% to 99.7944%, including startup; Linux ranges from 100.0189% to 100.0566%. The post-warmup values above use actual sample times near five seconds, so frame-sized endpoint quantization remains visible. They do not imply overclocking or establish physical presentation FPS. Windows output uses 48 kHz stereo float and Linux uses 44.1 kHz stereo signed samples. Zero reported events is not an audible comparison.

Complete Linux process user-plus-system times for the two 60-second titles were 9.82 and 10.33 seconds. Their rewind capture totals were 3.16 and 3.50 seconds, compared with 5.96 and 6.17 seconds in the preceding paced samples. Those separate process runs are observations; the component comparison above isolates the capture/compression change more directly. Windows sampled CPU counters remain private and excluded from utilization claims. Working set and resident memory are different host metrics and should not be treated as equivalent cross-platform measurements.

The README and fourteen documentation files resolve all 67 local links, and the C11/C++17 embedding examples pass syntax checks. These results cover the stated scenes, hardware choices, builds, and durations. Full-game correctness, audible equivalence, long-term saving, uncommon cartridges, and complete hardware/frontend parity remain unqualified.


## Stream, snapshot, and build performance

The 2026-11-08 component comparisons preserve commit `a6d6474c9026ddcd0034b9cde217188137fb3c3c` as their baseline. They isolate serial host output, word-array serialization, and Release IPO. They do not measure the combined application, and their percentage changes must not be added together.

### Serial host output

A generated Linux GCC Release stress case drives 131,072 CGB fast-clock transfers through 16,777,216 base T-cycles. Each variant ran three times under WSL with stdout redirected to a file on the Windows-mounted workspace filesystem. The baseline flushes every completed byte; the candidate either has no observer, counts bytes in an observer, or buffers identical bytes through a host callback.

| Variant | Median host elapsed time | Median process CPU time |
| --- | --- | --- |
| Baseline per-byte flush | 7.858 seconds | 0.432 seconds |
| No observer | 0.101 seconds | 0.110 seconds |
| Counting observer | 0.099 seconds | 0.108 seconds |
| Buffered host output | 0.106 seconds | 0.110 seconds |

Buffered output reproduces the complete baseline byte stream. Observers receive all 131,072 bytes, and every variant advances the same guest cycles. The large elapsed-time difference is specific to this artificial transfer-heavy case and its host output path. It is not a commercial gameplay speedup, a Windows timing result, or a link-cable compatibility claim. The runner continues to detect serial verdicts without printing raw bytes; `--serial-output` enables buffered diagnostics explicitly.

### Word-array codec

A separate A/B uses generated DMG, CGB, and SGB2 states, five alternating repetitions, and 500 operations per sample. Both variants link the same preserved core library on each platform. Linux uses GCC Release and native hashing; Windows uses MSVC Release and system hashing. Capture includes identity-cache checks, hardware validation, encoding, and integrity hashing, but excludes compression and Qt history management.

| Platform | State profile | Median cached capture before / after | Reduction |
| --- | --- | --- | --- |
| Linux | DMG | 232.0 / 163.8 microseconds | 29.4% |
| Linux | CGB | 233.8 / 164.9 microseconds | 29.4% |
| Linux | SGB2 | 241.2 / 158.3 microseconds | 34.4% |
| Windows | DMG | 261.1 / 164.1 microseconds | 37.2% |
| Windows | CGB | 261.5 / 163.0 microseconds | 37.7% |
| Windows | SGB2 | 263.5 / 167.0 microseconds | 36.6% |

Isolated field encoding costs fall by about 81-82% on Linux and 78% on Windows. Linux cached-load medians improve by 21-25%; Windows load samples vary too much across profiles and repetitions to support a load-speed claim. These are component measurements on this host, not frame-rate or sustained-gameplay results.

Every 300,518-byte baseline and candidate state matches across Windows and Linux for all three sampled profiles. The codec retains explicit little-endian elements, one complete checked array range, overflow rejection, unaligned buffers, full integrity checks, and hardware validation. Targeted tests check fixed array boundaries and construct the expected bytes independently. Continuation and rejection checks pass, and the CGB fixture remains SHA-256 `6106c038f80d9ef4ca4417f31305dc05bb714d9252e6caff3eec0205ea4ec0eb`.

### Release IPO

A preserved-source comparison uses 2,400 guest-clock intervals per run, normal graphics and generated stereo PCM, and two runs per variant in ABBA order. Linux uses GCC 13.3.0 and Windows uses MSVC 19.43.34809.0. These headless process measurements exclude Qt presentation, real audio-device playback, and frontend rewind capture.

| Platform | Input and model | Median no-IPO / IPO elapsed time | Throughput ratio |
| --- | --- | --- | --- |
| Linux | Tetris, DMG | 3.458 / 3.276 seconds | 1.055x |
| Linux | Mario Deluxe, CGB | 3.881 / 3.866 seconds | 1.004x |
| Windows | Tetris, DMG | 4.157 / 3.930 seconds | 1.058x |
| Windows | Mario Deluxe, CGB | 4.724 / 4.225 seconds | 1.118x |

Variants advance identical base cycles and instruction counts and reproduce image and PCM hashes for each input. The small Linux CGB difference is close enough to host variation that it does not establish a general CGB gain. The preserved baseline predates the serial and codec changes above, so this table isolates the build setting rather than the combined branch.

The active CMake configuration probes C and C++ IPO support, enables it only in Release, falls back to ordinary compilation when unavailable or disabled, and suppresses it with sanitizers. GCC C++ Release IPO targets use `-fno-strict-aliasing` for shared C hardware records. A mixed-language regression covers aggregate replacement, reset, memory access, native-state loading, and callback observation; the sanitizer build tests a different optimizer path.

### Current checks and model-clock scope

The final model-clock build passes all 24 desktop/core CTest checks on Windows and Linux. Linux also passes all 16 core/tool checks under ASan/UBSan with leak detection. These checks include the C/C++ integration boundary, serial callback/runner behavior, Qt hardware overrides, model-clock pacing, and generated SGB/SGB2 sample-count checks. They do not establish native gameplay or a real audio-device result for this build.

The current SGB NTSC profile uses 4,295,454 Hz; SGB2 and the other current profiles use 4,194,304 Hz. Audio sample scheduling/filtering, Qt deadlines, rewind seconds, and guest-speed percentages use that model clock. LCD geometry and the 70,224-cycle input-trace interval remain unchanged. Reports include `base_clock_hz`, and Qt reports distinguish `requested_model` from the actual `hardware_model`. SGB PAL, complete SGB host behavior, and sustained gameplay/audio qualification remain open.

Forced portable hashing with IPO disabled passes the snapshot, hash, and mixed C/C++ checks on Windows and sanitized Linux. The expanded Qt workflow smoke passes under ASan/UBSan. A Linux runner check directs serial output to a failing stream and verifies exit status 2, including a failure delayed until the final buffered flush.

The combined build repeats the 129 equivalent-input external cases: both builds produce 126 explicit passes and three bounded results. The bounded results remain inconclusive. Both acid2 images and six commercial final images match with zero differing pixels out of 23,040 each. The comparison revision remains pinned to the revision recorded above.

Three commercial CGB checkpoint replays also pass. Each advances 1,800 guest intervals, captures a native state, advances 180 more intervals, restores, and repeats the same input. Complete final state bytes and the combined image/PCM/sample-count observations match, including the two cases using CGB double speed. This is bounded native continuation, not in-game save-file or full-game qualification.

### Combined headless throughput

A separate comparison measures the final combined implementation against the preserved baseline without IPO. Each variant runs twice in ABBA order for 2,400 guest intervals with normal graphics and stereo sample generation. This includes the clock and serial changes, but excludes Qt playback and rewind. It is distinct from the isolated IPO comparison above.

| Platform | Input and model | Median baseline / combined elapsed time | Throughput ratio |
| --- | --- | --- | --- |
| Linux | Tetris, DMG | 3.152 / 3.143 seconds | 1.003x |
| Linux | Mario Deluxe, CGB | 3.677 / 3.397 seconds | 1.082x |
| Windows | Tetris, DMG | 4.476 / 3.975 seconds | 1.126x |
| Windows | Mario Deluxe, CGB | 4.525 / 4.070 seconds | 1.112x |

Complete PCM and image files, sample counts, instruction counts, and guest-cycle counts match across variants and platforms for both inputs. The Linux Tetris difference is inconclusive. Two runs per variant do not characterize every host or cartridge; the table supports these bounded comparisons rather than a general gameplay speedup.

### Native runtime samples across model profiles

The installed combined build ran nine title/profile cases on each platform with normal audio and rewind capture enabled. Tetris on DMG and Mario Deluxe ran for 60 seconds, Mario Tennis for 65 seconds, and the remaining cases for 30 seconds. Inputs, requested hardware, boot binaries, and traces match across platforms. Reports confirm a real audio sink, screenshot and battery-save flushing, and no execution or rewind-capture failure.

| Platform | Title | Model | Seconds | Post-warmup guest progress | Post-warmup paint p95 / maximum | Peak process memory |
| --- | --- | --- | --- | --- | --- | --- |
| Windows | Tetris | DMG | 60 | 100.0073% | 17.86 / 33.70 ms | 63.15 MiB working set |
| Windows | Mario Deluxe | CGB | 60 | 99.9821% | 17.84 / 36.63 ms | 84.55 MiB working set |
| Windows | Tetris | SGB NTSC | 30 | 99.9702% | 17.45 / 35.46 ms | 49.63 MiB working set |
| Windows | Tetris | SGB2 | 30 | 99.9723% | 17.83 / 37.57 ms | 49.06 MiB working set |
| Windows | Kirby | DMG | 30 | 99.9961% | 17.81 / 35.69 ms | 52.85 MiB working set |
| Windows | Super Mario Land | DMG | 30 | 100.0295% | 17.81 / 33.47 ms | 49.09 MiB working set |
| Windows | Crystal | CGB | 30 | 99.9844% | 17.77 / 33.88 ms | 52.27 MiB working set |
| Windows | Mario Tennis | CGB | 65 | 99.9986% | 17.84 / 35.71 ms | 104.66 MiB working set |
| Windows | Tomb Raider | CGB | 30 | 99.9694% | 17.85 / 38.71 ms | 81.55 MiB working set |
| WSLg/Xcb | Tetris | DMG | 60 | 99.9783% | 17.49 / 19.50 ms | 80.00 MiB resident |
| WSLg/Xcb | Mario Deluxe | CGB | 60 | 100.0136% | 17.39 / 20.37 ms | 101.39 MiB resident |
| WSLg/Xcb | Tetris | SGB NTSC | 30 | 100.0357% | 17.09 / 17.88 ms | 63.61 MiB resident |
| WSLg/Xcb | Tetris | SGB2 | 30 | 100.0364% | 17.21 / 20.74 ms | 64.92 MiB resident |
| WSLg/Xcb | Kirby | DMG | 30 | 99.9962% | 17.43 / 20.03 ms | 70.84 MiB resident |
| WSLg/Xcb | Super Mario Land | DMG | 30 | 99.9609% | 17.24 / 18.50 ms | 63.23 MiB resident |
| WSLg/Xcb | Crystal | CGB | 30 | 99.9832% | 17.38 / 19.22 ms | 68.12 MiB resident |
| WSLg/Xcb | Mario Tennis | CGB | 65 | 99.9997% | 17.58 / 19.31 ms | 123.94 MiB resident |
| WSLg/Xcb | Tomb Raider | CGB | 30 | 99.9671% | 17.55 / 19.19 ms | 96.18 MiB resident |

Windows memory is the per-process PeakWorkingSet64 sampled every 500 ms; Linux memory is the maximum resident set from the process resource report. These metrics are different and are not a cross-platform memory comparison. Percentages use the physical model clock: 4,295,454 Hz for SGB NTSC and 4,194,304 Hz for the other current profiles. Paint intervals describe widget activity rather than physical display presentation.

Audio event exceptions are retained: Windows Tomb Raider: whole-run idle/errors 1/0, post-warmup idle/errors 0/0. All other sampled windows report zero backend idle/error events.

A separate 30-second Windows Tomb Raider repeat reports zero whole-run and post-warmup idle/error events. It does not replace or erase the event in the original run. Zero events alone does not establish audible correctness.

Installed Windows and Linux/Xcb workflow smoke checks also pass with `--audio-check`. An initial WSLg attempt could not connect to PulseAudio. The retry passes after checking server readiness and selecting the WSLg audio socket explicitly; audio-service availability is separate from guest timing.

Captures on both platforms show active Tetris under DMG, SGB, and SGB2; Mario Deluxe; Kirby; and Super Mario Land gameplay. Crystal shows introduction text, Mario Tennis character selection, and Tomb Raider a load menu. These scene qualifications do not establish complete gameplay for those titles, long-term saving, or audible equivalence.

## Interrupt register phases and LCD edges

The next timing comparison uses the same pinned revision and preserves `2dddf47b15ec77d0269cecc3e37f8548297eeaa0` as its CupidGB baseline. Interrupt entry previously committed only the final STAT value. With SP at `ff42`, return address `8091`, and a particular LCD startup phase, that omitted transient selected vector `0050` and cleared the timer request. The corrected sequence selects vector `0048` and retains the timer request in IF. Stack writes remain within 20 CPU T-cycles, with acknowledgment at T18.

The register sequence also needs the correct LCD-start comparison edge. LCD enable preserves the frozen comparison and combined STAT latches until the model-specific refresh; an early synchronous refresh would change interrupt selection. A separate coincidence edge now preserves a pending HBlank transition rather than canceling it. The [timing guide](timing.md) explains these contracts and their remaining limits.

| Paired comparison | Cases | Matches | Observations |
| --- | --- | --- | --- |
| STAT/LCDC stack targets | 720 | 720 | Elapsed clocks, PC, SP, IF, STAT, LCDC; staged writes observed |
| Ordinary RAM stack controls | 720 | 720 | The same reported clocks and register fields |
| IF/IE stack aliases and timer phases | 720 | 720 | Duration, PC, SP, IF, IE, TIMA, DIV |
| Earlier acknowledgment phase sweep | 76 | 76 | Duration, vector, IF, visible LCD mode |
| LCD-start comparison and explicit writes | 144 | 144 | Byte-identical observation logs across automatic refresh, LYC writes, and STAT writes |

The paired profiles are DMG-B, Pocket, SGB NTSC and SGB2 without the host processor, and CGB-E at normal and double speed. The early local DMG profile is covered by continuation tests but has no enabled equivalent profile in this comparison build. Final-field equality does not establish complete register-conflict coverage or general half-cycle accuracy. Odd CGB CPU phases are local to the atomic interrupt scheduler; the startup comparison helper advances no pixel, oscillator, or sample clock.

Windows and Linux pass all 24 desktop/core CTest checks. Linux passes all 16 core/tool checks under ASan/UBSan with leak detection, plus the Qt workflow smoke under sanitizers. The Qt run disables leak detection for host-library allocations. Forced portable hashing with IPO disabled passes snapshot, hash, and mixed C/C++ checks on Windows and sanitized Linux.

Fourteen startup continuation captures and two pending-HBlank captures preserve their resumed IF/mode behavior and complete encoded states. No serialized fields were added. Version 2 remains 300,518 bytes, and the CGB fixture remains SHA-256 `6106c038f80d9ef4ca4417f31305dc05bb714d9252e6caff3eec0205ea4ec0eb`.

The repeated external corpus produces 126 explicit passes and three bounded results in each build. All eight final images match with zero differing pixels. Three commercial CGB checkpoint replays again reproduce complete final state bytes and image/PCM/sample-count observations, including two double-speed cases. Native runtime measurements for this timing build are recorded separately from these component and continuation checks.

### Expanded test coverage and remaining failures

An inventory found 236 distinct compiled local GB/GBC test ROMs. An additional 80-case batch uses verified explicit-verdict protocols and corresponding DMG or CGB-E profiles. The comparison build produces 79 passes and one bounded result. CupidGB produces 15 passes, 62 failures, and three bounded results. The preserved `2dddf47` build produces exactly the same CupidGB observations, including verdicts, instruction/cycle counts, images, generated audio, serial counts, and memory result text. Host time is excluded from that equality check.

The 62 explicit failures comprise 59 APU sample-timing cases and three DMA cases covering address masking, LCD-off behavior, and mode-0 transfers. Two hardware-I/O cases pass in the comparison build but remain bounded in both CupidGB builds. One divider-startup case remains bounded in all builds. These results identify substantial remaining accuracy work. They do not invalidate the narrower passing scopes above, and neither a bounded native run nor this interrupt repair resolves them.

The full inventory also includes unsupported hardware revisions, boot-dependent cases, tests needing completion adapters, manual/experimental tests, and a clock procedure. They require their own model and validation setup; they are not counted as passing or dismissed as unnecessary. Private receipts retain every selected row, input hash, executable hash, outcome, and baseline comparison. The current parity goal remains open.

### Native playback after the timing repair

The installed timing build ran nine title/profile cases on each platform with normal audio and rewind capture enabled. Tetris on DMG and Mario Deluxe ran for 60 seconds, Mario Tennis for 65 seconds, and the remaining cases for 30 seconds. Inputs, requested hardware, boot binaries, and traces match across platforms. Reports confirm a real audio sink, screenshot and battery-save flushing, and no execution or rewind-capture failure.

| Platform | Title | Model | Seconds | Post-warmup guest progress | Post-warmup paint p95 / maximum | Peak process memory |
| --- | --- | --- | --- | --- | --- | --- |
| Windows | Tetris | DMG | 60 | 100.0065% | 17.80 / 35.02 ms | 63.58 MiB working set |
| Windows | Mario Deluxe | CGB | 60 | 100.0090% | 17.81 / 39.45 ms | 84.19 MiB working set |
| Windows | Tetris | SGB NTSC | 30 | 100.0353% | 17.31 / 34.53 ms | 49.39 MiB working set |
| Windows | Tetris | SGB2 | 30 | 100.0332% | 17.85 / 28.78 ms | 49.24 MiB working set |
| Windows | Kirby | DMG | 30 | 99.9970% | 17.87 / 38.33 ms | 52.96 MiB working set |
| Windows | Super Mario Land | DMG | 30 | 100.0288% | 17.92 / 37.15 ms | 48.87 MiB working set |
| Windows | Crystal | CGB | 30 | 99.9884% | 17.72 / 41.72 ms | 51.85 MiB working set |
| Windows | Mario Tennis | CGB | 65 | 99.9980% | 17.85 / 37.66 ms | 103.95 MiB working set |
| Windows | Tomb Raider | CGB | 30 | 99.9650% | 17.85 / 36.92 ms | 81.62 MiB working set |
| WSLg/Xcb | Tetris | DMG | 60 | 99.9772% | 17.56 / 20.08 ms | 79.88 MiB resident |
| WSLg/Xcb | Mario Deluxe | CGB | 60 | 100.0134% | 17.36 / 20.06 ms | 99.59 MiB resident |
| WSLg/Xcb | Tetris | SGB NTSC | 30 | 100.0331% | 17.07 / 20.70 ms | 66.05 MiB resident |
| WSLg/Xcb | Tetris | SGB2 | 30 | 100.0356% | 17.44 / 19.11 ms | 67.10 MiB resident |
| WSLg/Xcb | Kirby | DMG | 30 | 99.9988% | 17.47 / 18.85 ms | 69.18 MiB resident |
| WSLg/Xcb | Super Mario Land | DMG | 30 | 99.9654% | 17.28 / 18.22 ms | 60.69 MiB resident |
| WSLg/Xcb | Crystal | CGB | 30 | 99.9808% | 17.34 / 19.87 ms | 67.71 MiB resident |
| WSLg/Xcb | Mario Tennis | CGB | 65 | 99.9997% | 17.60 / 20.61 ms | 122.64 MiB resident |
| WSLg/Xcb | Tomb Raider | CGB | 30 | 99.9770% | 17.59 / 19.75 ms | 97.46 MiB resident |

Windows memory is the per-process PeakWorkingSet64 sampled every 500 ms; Linux memory is the maximum resident set from the process resource report. These metrics are different and are not a cross-platform memory comparison. Percentages use the physical model clock: 4,295,454 Hz for SGB NTSC and 4,194,304 Hz for the other current profiles. Paint intervals describe widget activity rather than physical display presentation.

All whole-run and post-warmup windows report zero backend idle/error events. Installed Windows and Linux/Xcb workflow smoke checks also pass with real audio required.

Captures on both platforms show active Tetris under DMG, SGB, and SGB2; Mario Deluxe; Kirby; and Super Mario Land gameplay. Crystal shows introduction text, Mario Tennis character selection, and Tomb Raider a load menu. These scene qualifications do not establish complete gameplay for those titles, long-term saving, or audible equivalence.

## CGB acceptance, STOP timing and native-v3 component checks

The 2026-11-08 component run repairs the expanded-batch failures recorded above. Comparisons use the same pinned source revision, corresponding hardware profiles, and verified ROM and boot inputs. Private receipts retain every outcome, input and executable hash, and the preserved CupidGB baseline.

| Environment | Current component result |
| --- | --- |
| Windows Release desktop/core | 25/25 CTest checks pass |
| Linux Release desktop/core | 25/25 CTest checks pass |
| Linux ASan/UBSan core/tool build | 17/17 CTest checks pass |

The desktop runs include the Qt workflow and command-line checks. These are automated component results; native playback measurements belong to their separately identified builds and sessions.

### External cases and explicit verdicts

| Batch | CupidGB | Pinned comparison |
| --- | --- | --- |
| Selected 129 cases: 101 main cases and 28 cartridge cases | 126 pass, 3 bounded | 126 pass, 3 bounded |
| Expanded 80 cases | 79 pass, 1 bounded | 79 pass, 1 bounded |

The expanded batch improves 64 cases against the preserved `2dddf47` build, which produced 15 passes, 62 failures, and three bounded results under the same adapters. The improvements cover 59 APU cases, three color-DMA cases, and two hardware-I/O cases. There are no verdict regressions in that batch.

The remaining `boot_div-cgbABCDE` row is bounded under the original register adapter. A separate completed-binary-serial check reports FAIL in both CupidGB and the pinned comparison with the same CGB boot binary. CupidGB completes six serial bytes and exits with failure. This is a shared boot-dependent test failure, not a CupidGB-only regression or an implicit pass. The two hardware-I/O cases now pass; their current success marker can appear before serial transfer completes.

All eight bounded final-image comparisons have zero differing pixels. All 59 previously failing APU ROMs also pass, with every byte of their authored RAM result tables matching the expected data and pinned comparison. Those tables cover mapped digital audio taps, pulse alignment and held duty output, live envelope writes, delayed sweep calculation, and noise-divider and retrigger behavior. Final-image equality alone does not qualify complete gameplay or audible output.

### Timing, boot and continuation regressions

The three color-DMA acceptance ROMs reproduce their expected RAM tables and tested clock counts. Sixteen start-boundary traces match, including fetched-opcode retention when execution begins in VRAM. Passing regressions cover deferred HBlank blocks, LCD-off requests, cancellation status, and HALT/STOP wake permissions. Boot checks cover physical-CGB register visibility, read masks, palette access during boot, and compatibility-mode selection.

Review then found that compatibility selection retained the bank registers but incorrectly routed data through default VRAM/WRAM banks. A separate 48-profile probe now matches all 192 observations; 27 differed before the correction. The generated regression checks actual bank data and WRAM echo aliases before and after KEY0/FF50, alongside native-mode, no-overlay and monochrome controls. The initial native matrix was intentionally interrupted for this correction and retained as incomplete evidence; final qualification uses a fresh build.

STOP tests cover the timed speed-switch wait, toggle and freeze phases, natural expiry versus an IME0 interrupt wake, DIV-reset hold, effective IME after EI, selected held input, and padding-byte execution. The paired EI probe matches all 28 records. The debugger regression verifies that Next and Finish continue through a wake-only idle event before tracking the following call or return.

Native version 3 stores the added audio, DMA and speed-switch history. The current CGB fixture is 300,568 bytes with SHA-256 `e4fdd3d7ebfdd04aec94ce7b85f0f2d7d83de1416242fa89f26ccf5dfaf71eb2`, matching the Windows, Linux and sanitized core runs. Continuation checks include 128 audio phases, ten combined DMA/audio captures at normal and double speed, and pending speed-switch waits. They compare complete encoded states after restoration and subsequent execution. Noise counter carry and wrap, delayed sweep overflow, queued PCM, partial integration and capacitor history have targeted regressions.

Version 2 remains readable. Import checks exercise DMG, CGB and SGB with and without a boot overlay, through ordinary and trusted-context loads. Existing PCM, partial integration and filter history survive; fields missing from the old format receive deterministic defaults derived from known legacy state. The loader does not borrow the target instance's unrelated timing history. Exact pre-import oscillator, DMA or STOP phase cannot be recovered from fields that version 2 never stored. See the [save-state guide](save-states.md) for the format and migration contract.

### Isolated audio integration measurements

A controlled comparison changes only integration of a locally constant mixer value within one APU call. Oscillator events retain their base-clock order. Each run covers 2,400 nominal frames, about 40.18 guest seconds, with normal graphics, audio generation and audio hashing. The table gives medians from two timed runs per variant in ABBA order; separate runs compare complete PCM and image files.

| Platform and input | Before | Batched integration | Interpretation |
| --- | --- | --- | --- |
| Linux, Tetris/DMG | 3.4151 s | 3.1691 s | 7.20% lower host time in this comparison |
| Linux, Mario Deluxe/CGB | 4.3362 s | 4.3065 s | Difference is inconclusive |
| Windows, Tetris/DMG | 4.4589 s | 4.4100 s | Difference is inconclusive |
| Windows, Mario Deluxe/CGB | 5.2352 s | 5.2224 s | Difference is inconclusive |

Complete PCM and image files match across variants and platforms. Another 2,400 encoded-state/PCM checkpoints match across chunk splits, six models, five sample rates, and varied channel, power, filter and queue states. These timings isolate the integration change; they do not measure the whole milestone or Qt presentation speed.

A separate STAT shortcut passed 1,376,256 whole-state comparisons and the paired output checks, but increased CGB host time by 6.08% while reducing DMG host time by 3.56%. It was rejected and remains outside the product.

### Remaining accuracy work

The wider 192-case DMA memory matrix still has 160 full-VRAM mismatches. All 192 reported clock, PC and status records match, while only 32 complete VRAM results match. Shared main-bus data and decay, undriven-source behavior, and mode-3 PPU destination-address conflicts remain unresolved.

The STOP probes retain JOYP propagation and interrupt differences. Some interrupt-abort rows also differ in elapsed clocks and DIV phase. The API-started OAM-DMA fixture retains its startup byte offset and STAT differences; it does not establish the behavior of an ordinary CPU-bus start. At 44,100 and 192,000 Hz, a fully frozen comparison poll can defer a host audio callback that CupidGB has already delivered. The following PCM read brings the tested frame totals and digital tap value into agreement, but that delivery-order difference remains open.

Phantom pulse-trigger cases, high-selector noise corruption, wider non-CGB audio behavior, model revisions, PPU bus conflicts, link/accessory workflows, and long-term commercial audio/input/save qualification remain in the [parity checklist](parity.md). The passing scopes above do not establish complete hardware or frontend parity.

## Mapper, startup, SGB, and controller checks

The comparison uses revision `ea667ad79aa28409f034b1fd6da7304e1844750d`. Paired runs use the same cartridge bytes, model, firmware, and input. Private receipts keep the build identities, hashes, full results, and captures.

Windows MSVC Release, Linux GCC Release with Qt 6.8.3, and Linux ASan/UBSan with portable hashing each passed 31 of 31 CTest checks. The combined shared-boot ROM suite returned 177 explicit passes and four bounded results out of 181 on both builds. The prior CupidGB build returned the same verdicts.

- With local firmware, CupidGB improved from four to seven explicit boot test passes, matching the comparison. Three tests pass with shared generated firmware and four reach the execution limit in both builds. The firmware profiles are separate results.
- Five STAT suites match 100,008 generated cases and 200,016 controls on the observed endpoints and write phases. A separate speed-switch probe matches 60 control/clock observations and 60 sample-phase observations. These tests exercise the covered write routes and LCD boundaries.
- The SGB compositor matches 96 generated frames pixel for pixel. In a render-only Linux test with dense valid tiles, the median fell from 373 to 136 microseconds per frame. This benchmark measures border composition only.
- Shared-boot SGB2 captures match at the Donkey Kong title, file select, and stage opening, and at the Pokémon Yellow Professor Oak dialogue. Donkey Kong active play retains 149 viewport differences while all 34,304 border pixels match. Yellow's intro viewport also differs at the tested checkpoint. A Donkey Kong snapshot taken during a border transfer reproduced the complete state, framebuffer, and PCM on replay.
- Controller probes match 16,912 input and rumble observations and 72 multiplayer command transitions. Qt tests use virtual controller devices to exercise mapping, hotplug, menus, focus, multiple players, and motor requests. Physical controllers and motion sensors were unavailable.

### Mapper coverage

Generated cartridges exercise mapper modes that the local commercial library lacks. The observations below cover specific reads, writes, capture steps, and persistence paths. The [hardware guide](hardware.md) records the remaining device behavior.

The local scan ran all 880 available cartridge inputs with a limit of 420 guest-clock intervals per input. Of those, 876 reached the limit and four CGB inputs hit illegal opcodes. The Camera input now reaches the limit. A private trace for one failing input matched all 357,821 post-boot instruction addresses and opcodes, including the illegal instruction, when both builds used deterministic power-on RAM. The scan covers startup and early execution. Longer gameplay, audio, and persistence use separate checks.

| Mapper | Observations and remaining gaps |
| --- | --- |
| ROM-only and ROM+RAM | Fixed banks and local RAM persistence exercised. Unbanked RAM reads differ from the comparison's held bus behavior |
| MBC1 and multicart | Bank and mask probes match on the tested populated regions. Disabled SRAM bus readback differs |
| MBC2 | Address-dependent control and all tested four-bit RAM mirrors match |
| MBC3 and MBC30 | Normal and extended ROM/RAM banks, RTC state and battery reload exercised. Repeat latch writes still differ |
| MBC5 and rumble | All 512 ROM selections, RAM enable decoding, battery reload and motor transitions exercised |
| MMM01 | All 17,056 bank and RAM observations match across DMG/CGB, both dump layouts, lock, masks, and multiplex modes. Native continuation has a separate regression |
| MBC6 | Generated probes exercise the partial generic bank path. Flash and the full split-window mapper remain pending |
| MBC7 | All 552 observations match, including both access gates, motion latches, EEPROM commands, and EEPROM bytes. The tested tilt game reached active play with matching pixels under neutral motion |
| HuC1 | Banking, SRAM, battery reload and disconnected infrared reads exercised. Infrared transport remains pending |
| HuC3 | All 26 banking and SRAM observations match across DMG/CGB. RTC and infrared probes still differ |
| Camera | Both models match all 7,168 image tile bytes at unit gain with equivalent input pixels. The local game reached its title and Shoot menu, captured a synthetic photo, changed 3,788 SRAM bytes on Save, and reloaded all 128 KiB byte for byte |
| TPP1 | All 4,736 generated bank, SRAM, RTC, and motor observations in the installed bank range match. A probe with an unpopulated high ROM bit set has different SRAM readback. No local commercial cartridge was available |
| TAMA5 | No working mapper in either build |

Commercial snapshot replays for Pokémon Crystal, Mario Tennis, and Tomb Raider reproduced their complete encoded end states and PCM after restore and 180 additional guest intervals. Existing user ROMs and saves were preserved.

TPP1 stopped-clock exports match byte for byte at the same timestamp on DMG and CGB, and six cross-imports restore the expected SRAM and clock. CupidGB preserves a nonzero motor status in the file. The comparison normally writes zero for an active motor held in its separate host state.

An isolated APU shortcut preserved PCM, images, guest cycles, and instruction counts in both tested traces. It reduced the median Windows Tetris time from 4.876 to 4.630 seconds and increased Mario Deluxe from 4.697 to 5.010 seconds. The shortcut was removed because the CGB regression repeated across the paired Windows runs.

### Desktop playback

The final Windows and WSLg Xcb runs used Qt 6.8.3, audio output, normal rewind capture, and matching per-title firmware and inputs. All four 60-second captures ended in active gameplay. The table uses the last 55 seconds for guest-clock progress and widget paint timing. Memory measures the Windows working set or Linux resident set over the process lifetime.

| Environment and input | Nominal guest progress | Widget paint p95 / maximum | Peak memory | Audio errors / idle events |
| --- | ---: | ---: | ---: | ---: |
| Windows, Tetris | 100.006% | 19.29 / 22.41 ms | 66.13 MiB | 0 / 0 |
| Windows, Mario Deluxe | 99.981% | 17.69 / 22.81 ms | 87.21 MiB | 0 / 0 |
| WSLg Xcb, Tetris | 99.976% | 17.41 / 24.65 ms | 94.96 MiB | 0 / 0 |
| WSLg Xcb, Mario Deluxe | 100.013% | 17.64 / 20.01 ms | 113.78 MiB | 0 / 0 |

The WSLg processes used 10.39 and 11.74 seconds of user-plus-system CPU time. A separate 36-second SGB2 Donkey Kong Windows sample reached 100.029% of nominal guest time after warmup, with zero audio errors or idle events. The sink processed 35.877 seconds of audio across the full run. Its widget paint count was zero, so this sample covers guest execution and audio delivery. The other paint timings describe widget events. Physical display refresh was unmeasured.

Native workflow smokes passed on Windows and WSLg Xcb, including state load, rewind, and audio resumption. Earlier WSLg playback stalled with its RDP audio sink while the audio queue was full. The earlier 60-second runs reached 77.90% for Tetris and 0.18% for Mario Deluxe after warmup. The previous build and an intermediate Qt 6.8.3 build also stalled in 20-second Mario Deluxe runs and advanced the same 30,830,526 guest cycles. An earlier 36-second SGB2 sample reached 45.24% overall. The final reruns above completed at nominal speed. The intermittent RDP audio stalls remain open.

## Cartridge bus, peripherals, and desktop workflows

The Windows MSVC and Linux GCC Release builds with Qt 6.8.3 passed all 33 CTests. Linux also passed all 23 core checks with ASan/UBSan. A Linux Qt 6.4.2 build passed the focused state, MBC7, display and desktop workflow tests.

The CGB DMA matrix covers eight source regions, three destination regions, both speed modes, both VRAM banks, and LCD-off and active-fetch timing. The previous build matched 32 of 192 full VRAM dumps. The shared VRAM destination change increased that to 76. With the CGB bus hold and decay change, all 192 matched, including CPU position, DMA status, and timing. Three booted DMA diagnostics also matched their checkpoint bytes, CPU position, step counts, and clocks. The 181-case external suite produced 177 explicit passes and four bounded cases on both builds.

The 19 register-immediate and ALU-immediate opcode forms now fetch at their actual machine-cycle boundaries. All 38 normal/double-speed probe records matched registers, flags, PC, elapsed clocks, and remaining bus hold. The 181-case diagnostics retained the same 177 passes and four bounded cases after this change. Other store and prefix fetch phases require further work.

A separate matrix started OAM DMA from `C100` before the color transfer. Timing, CPU position, and status matched on all 192 cases. Full VRAM matched on 80 cases and full OAM matched on none. Concurrent arbitration remains open.

The 880-input local library scan again reached its 420-interval limit on 876 inputs. The same four CGB inputs hit illegal opcodes. With deterministic power-on RAM and equivalent firmware, all four inputs now reach the same illegal instruction in both builds. The fourth formerly diverged when it read disabled MBC5 RAM. The preceding high address operand holds the bus at `A8` through that read. A 1,417-row trace now matches the CPU address, opcode, accumulator, and stack pointer through the final instruction. The scan qualifies bounded execution and leaves full gameplay open.

HuC3 DMG and CGB probes matched SRAM modes, clock and command readback, day carry, battery bytes, and six stable infrared checkpoints. Robopon Sun reached in-game dialogue after 3,120 guest intervals on Windows and Linux with matching final image and PCM checksums. A private run wrote 1,298 nonzero SRAM bytes, and its 32,785-byte battery file round-tripped byte for byte. A restored native state reproduced the next 180 intervals and 132,904 PCM frames. The in-game Continue menu, exact infrared transition noise, and physical infrared remain unqualified.

The HuC3 writer timestamps an export at the supplied host time. Internal minute-boundary arithmetic can temporarily put its baseline ahead of that time. A regression checks immediate reload at every second of the following minute and verifies that the clock, alarm, and SRAM survive.

Paired serial traces matched all 155 lines, including DMG/CGB clock directions and CGB fast and double-speed transfers. Printer traces matched all 816 lines, including checksum failures, compressed data spanning packets, image output, and completion timing. The Qt workflow checks a generated printer job, its grayscale PNG, and output through Qt's PDF print backend. Physical print and commercial multiplayer remain unverified.

The printer UI checks also append a second strip, start a fresh roll after closing the feed, and disconnect and reconnect with an old callback queued. PDF testing uses `QPdfWriter` with the same paged renderer as system printing. This keeps synthetic output independent of native printer discovery, which blocks in CUPS on this Linux host.

The archive loader returned matching cartridge SHA-256 hashes for all 386 local ZIP inputs on Windows and Linux. Generated ZIP and 7z fixtures exercised the desktop launch path. Stored and compressed RAR fixtures produced the expected cartridge bytes on both hosts, with the RAR codec installed on Linux for the compressed case. The automated checks cover nested Unicode names, multiple cartridges, links, invalid sizes, corrupt data, and separate archive battery paths. Additional regressions reject native Game Boy Advance headers in the core and `.gba` inputs in Qt.

The Accurate blend fast path skips color lookups on rows that have identical source pixels. A Linux Qt 6.4.2 microbenchmark replayed 476 Tetris frames and 458 Mario Deluxe frames and compared the output pixels in both LCD phases. Median time per frame fell from 49.04 to 7.06 microseconds for Tetris and from 48.52 to 17.18 microseconds for Mario Deluxe. The measurement covers blending only.

A separate pinned-CPU Linux test used six permutations of 600-interval runs. Median process CPU time for baseline, initial bus implementation, and final ROM fast path was 1.014, 1.133, and 0.991 seconds for Tetris, and 1.127, 1.178, and 1.127 seconds for Mario Deluxe. Guest cycles, instruction counts, PCM frame counts, and image/audio checksums matched in those samples.

### Audio-enabled desktop runs

Four 60-second runs used Qt 6.8.3, local boot firmware, recorded inputs, default rewind, and Accurate blending. The final images showed active play. Each run had zero audio errors and zero audio idle events. The table records the 55 seconds after warmup.

| Environment and input | Nominal guest progress | Widget paint p95 / maximum | Peak memory |
| --- | ---: | ---: | ---: |
| Windows, Tetris | 100.006% | 17.84 / 25.07 ms | 66.75 MiB |
| Windows, Mario Deluxe | 100.007% | 17.70 / 35.03 ms | 87.98 MiB |
| WSLg Xcb, Tetris | 100.003% | 17.30 / 19.28 ms | 93.38 MiB |
| WSLg Xcb, Mario Deluxe | 100.012% | 17.61 / 19.12 ms | 115.58 MiB |

Windows process CPU samples reached 4.30 and 6.42 seconds, and Linux user-plus-system CPU totals were 9.59 and 11.65 seconds. Native audio-required workflow smokes passed on Windows and WSLg Xcb. Guest progress uses the emulated clock. Widget paint events measure host UI delivery. Physical display refresh and longer gameplay remain unmeasured.

## Memory tools, display color, and bus phases

This checkpoint uses comparison revision `ea667ad79aa28409f034b1fd6da7304e1844750d`. CupidGB implements the behavior independently. Private fixtures and receipts remain outside the repository.

The CPU comparison matched all 3,448 records for the remaining immediate load/store and relative branch phases, all 256 CB instructions, four flag combinations, and DMG, CGB, and double-speed CGB. Records include read/write timestamps and architectural state. The previous build differed on every timestamp record and 40 architectural results. Two independent 181-case test-suite sweeps each reported 177 passes and four bounded cases.

Standalone OAM DMA matched 13,608 observations across models, source regions, short clock segments, and CPU access locks. Primed concurrent DMA probes matched all timing records. The 6,912-case overlap grid matched all OAM records and 6,848 VRAM records; 64 mode-3 boundary cases remain. The 5,376-case restart grid matched both complete memories. A further 5,376-case VRAM/high-source grid matched all VRAM records and 4,660 OAM records. Its remaining 716 OAM differences require PPU fetch/address conflict modeling. The historical unprimed 192-case overlap grid still matches 80 VRAM and 80 OAM results because the initial held cartridge bus differs. These results leave concurrent DMA parity incomplete. A separate 66-record final-tail restart probe matches the source cursor, phase, OAM read lock, and copied bytes on DMG and normal/double-speed CGB. Restarting after byte 160 begins a fresh request; restarting before that byte keeps the initial destination locked.

Native states now write V7 with one OAM phase byte after the unchanged V6 payload. V2 through V6 import remains supported. Historical V4 size and digest checks pass. Host cheats, memory search, and display settings remain outside the state wire format.

Cheat comparisons matched 823 traces covering all 256 bank prefixes, 512 generated conditional codes, controller bank selection, duplicates, global enable, and RAM regions. Canonical cheat-file bytes matched. Search comparisons matched 56 candidate-count and address/bank/value hashes. Component tests cover all eleven conditions and all three value widths, changing types, physical banks, writes, and region ends. Two-byte freeze rules deliberately map each byte separately across a physical bank boundary; this corrects an invalid second-byte address in the comparison frontend.

Qt workflow checks cover editing, import/export, per-rule and global enable, automatic saving, failed writes, protected cartridge/battery/state paths, reset retention, and native state restore. A failed automatic write leaves the active collection available for export. Custom expressions and decimal arithmetic search remain pending.

The color pipeline matched all 32,768 input colors, seven modes, 21 temperature positions, and both CGB and SGB profiles: 9,633,792 colors, or 28,901,376 output bytes. Windows and Linux output bytes matched. Inspector numeric values remain raw; palette previews use the selected correction. This does not qualify every display filter or hardware variant.

The complete builds passed 36 of 36 checks on Windows/MSVC and Linux/GCC with Qt 6.8.3. Linux ASan/UBSan passed all 24 core checks. An earlier private state-sanitizer run hit a clock-baseline assertion once without a sanitizer diagnostic; a focused rerun and the complete sanitizer suite passed. Its cause remains unconfirmed.

### Bounded core throughput

Each sample executes 3,600 guest intervals, approximately 60.274 guest seconds. The core measurement at `8106f93`, before the later pulse-trigger correction, compares with `3b896f3`. Hosts ran sequentially, discarded one warmup pair, then measured five alternating pairs. Tetris guest counters and complete PCM/PPM captures match on both hosts. Mario Deluxe retains the same guest cycles, VBlank count, audio frame count, and final image, but the corrected fresh SRAM produces 63 additional instructions and different startup PCM.

| Host and input | Previous median wall time | Current median wall time | Change |
| --- | ---: | ---: | ---: |
| Linux, Tetris | 7.511 s | 7.702 s | +2.5% |
| Linux, Mario Deluxe | 8.506 s | 8.462 s | -0.5% |
| Windows, Tetris | 7.713 s | 7.693 s | -0.3% |
| Windows, Mario Deluxe | 9.553 s | 9.443 s | -1.2% |

Linux process CPU medians changed from 6.934 to 7.111 seconds and 7.855 to 7.814 seconds. Windows process CPU counters varied widely between repeats, so their median changes do not establish an improvement. Current bounded core rates span 6.38 to 7.83 times nominal guest speed. These uncapped core samples do not measure desktop frame pacing or qualify other games.

A separate correctness control imports an explicit 8 KiB zero-filled battery before boot. This restores every baseline Mario signature and byte-identical PCM/PPM, isolating the startup difference to the fresh-storage correction. It was not used for performance claims. An isolated inactive-DMA call guard preserved output but increased Linux runtime by 6.4% and 2.5% in its two screening inputs; it was excluded from the implementation.

### Native desktop playback

Final Windows Qt 6.8.3 sessions used local firmware, recorded inputs, fresh storage, normal audio, default rewind, Balanced correction, and neutral temperature. Tetris and Mario Deluxe ran from the installed folder with only system directories on PATH; every loaded Qt DLL came from that folder. The SGB2 result below uses the final centered border-window sizing. Each session lasted 60 seconds; the table records the last 55 seconds after warmup. All had zero audio errors and zero audio idle events.

| Input | Nominal guest progress | Widget paint p95 / maximum | Peak working set |
| --- | ---: | ---: | ---: |
| Tetris, DMG | 100.006% | 17.68 / 36.83 ms | 67.46 MiB |
| Mario Deluxe, CGB | 99.983% | 17.65 / 20.63 ms | 88.71 MiB |
| Donkey Kong, SGB2 | 100.006% | 17.82 / 36.61 ms | 121.04 MiB |

The final GB/GBC images show active play. The 60-second SGB2 image shows the stage introduction after its recorded input sequence. A separate 36-second capture shows active first-stage gameplay with the full border at the corrected default size. All 34,304 corrected border pixels match the pinned color oracle. The viewport is at a different gameplay time and is excluded from that pixel comparison.

Default-size workflow checks exercise handheld/border changes, ROM/model transitions, visible centering at 1x/2x/3x, never-shown positioning, custom/fullscreen preservation, and explicit 1x shrinking after close. Layout activation occurs only on a resize operation, so an old minimum cannot clamp the new size and steady frames do not repeat layout work. Close/restore position policies remain unchanged.

### Linux host timing limit

The Linux Xcb frontend rendered the SGB2 border and processed inputs, but sustained native audio qualification remains blocked on this host. Qt 6.8.3 and system Qt 6.4.2 both stalled; a preserved preceding build reproduced the problem. A shutdown sample found a PulseAudio drain wait. Sink teardown now resets obsolete timeline audio rather than requesting that drain. This repairs the close/pause behavior without qualifying sustained playback.

Read-only probes found clock-rate settings of tick 10833 microseconds plus 33.333 ppm, with monotonic time advancing 8.333% faster than the raw clock. An independent 10-second Pulse playback also developed reported latency from 37 to 842 ms. A fresh boot briefly returned to tick 10000/frequency zero. The unchanged production binary then completed a 20-second SGB2 menu/input capture at 99.974% post-warmup guest progress, zero idle/errors, and paint p95/maximum 17.84/18.94 ms. Its border pixels also match the oracle.

The rate changed again automatically, including settings near 10% above raw time. A user-authorized standard-rate trial was overwritten during playback and failed; its immediate pre-trial tick/frequency values were restored and independently verified. Bounded syscall traces found wall-clock steps from the time service but did not identify a tick/frequency writer. No time service was stopped or reconfigured. The cause of the recurring rate changes remains unverified.

A private 125 ms backend-buffer probe did not recover playback. A separately rebuilt raw-pacing prototype also failed, with 24 post-warmup idle events. Neither was admitted. Production retains the 50 ms managed queue and 30 ms priming policy, with guest execution waiting on audio backpressure. The short standard-clock capture establishes only that bounded workflow; it does not establish sustained Linux gameplay or universal full-speed support.

The fresh 880-input library scan used 420 guest intervals per input. All 350 DMG inputs and 526 of 530 CGB inputs reached the limit and emitted VBlank and PCM output. The same four MBC5 inputs stopped with core errors; no new failing input appeared. All input hashes remained unchanged. Bounded counts were MBC5 483, MBC1 271, MBC3 81, MBC5 rumble 13, ROM-only 12, MBC2 11, and one each for MBC7, HuC3, Camera, MBC1 multicart, and an unknown-header fallback. HuC1, MMM01, MBC30, and TPP1 had no commercial entries in this library; their generated and paired tests remain separate evidence. A startup limit does not qualify complete gameplay, every bank transition, or every mapper peripheral.

Fresh traces for the four failing library inputs matched the complete instruction-address/opcode sequence and terminal illegal opcode in the pinned build: 94, 357,821, 197,367, and 1,417 records. The initial comparison isolated 800 accumulator differences in the fourth input to fresh SRAM initialized at zero. Fresh cartridge storage now starts at erased `FF` after mapper-specific sizes are resolved. All four complete address/opcode/accumulator/stack-pointer traces are now byte-identical. Twenty-two mapper/header regressions cover the cold bytes and explicit battery/state restoration. All 24 core checks pass again on Windows, Linux, and Linux ASan/UBSan; frozen legacy state fixtures remain unchanged. These traces cover their recorded registers and paths, rather than every hardware state field.

### Color pulse retriggers

The pulse repair matches 50,420 paired records: 30,912 trigger-boundary observations, 19,200 register-only timeline rows, 220 power-cycle samples, 48 inactive reload/write rows, and 40 repeat-write rows. The initial trigger grids matched only 10,744 and 10,616 rows before correction. Scope is the pinned CGB-E profile, both pulse channels and CPU speeds, with aligned two-base-dot observations. Cold starts, trigger bit 2, retained inactive reloads, suppression, and maximum-frequency extra delay have regressions. Windows, Linux, and Linux ASan/UBSan pass all 24 core checks; no state field or format changed.

A deterministic 3,600-interval Tetris replay retains identical PCM and pixels. Mario Deluxe retains guest cycles, instruction count, VBlank count, audio frame count, and final pixels, while its PCM changes after the corrected pulse edges. These are bounded comparisons, not a complete commercial audio or hardware-revision qualification.

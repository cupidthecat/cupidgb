# Validation record

This record starts with the desktop and timing checks on 2026-11-04 and includes later work through 2026-11-16. Results apply to each stated build, input set, and scope. They do not establish complete hardware or frontend parity. The comparison source revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`.

Earlier sections retain the results and assumptions of their original builds. A result without a recorded model clock does not qualify the corrected SGB clock. Use the current model-clock reports when comparing physical speed across profiles.

The [audio and native-state checkpoint](validation-audio.md) records reconstruction, live-load, Camera and continuation results separately from these earlier builds. The subsequent [display and infrared checkpoint](validation-timing.md) records display-delivery, pixel and VRAM comparisons and distinguishes development probes from final integration gates.

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
| Donkey Kong, SGB2 | 100.000% | 17.80 / 34.41 ms | 119.61 MiB |

The final GB/GBC images show active play. The 60-second SGB2 image shows the stage introduction after its recorded input sequence. A separate 36-second capture shows active first-stage gameplay with the full border at the corrected default size. That run reached 99.973% post-warmup guest progress, zero audio idle/errors, and paint p95/maximum 17.81/37.38 ms. Both SGB2 captures use the refreshed installed binary after the native window-chrome correction. All 34,304 corrected border pixels match the pinned color oracle. The viewport is at a different gameplay time and is excluded from that pixel comparison.

Default-size workflow checks exercise handheld/border changes, ROM/model transitions, visible centering at 1x/2x/3x, never-shown positioning, custom/fullscreen preservation, and explicit 1x shrinking after close. Layout activation occurs only on a resize operation, so an old minimum cannot clamp the new size and steady frames do not repeat layout work. Close/restore position policies remain unchanged. A packaged native Windows audio workflow caught a one-pixel chrome discrepancy that offscreen tests missed. Window chrome now comes from the complete main-window minimum-size hint, retaining exact size and center assertions. The refreshed Windows bundle passes the native audio workflow with no SDK on PATH. The installed Linux system-Qt build passes the native Xcb UI workflow; that UI-only check does not require host audio.

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


## Shared bus, input, presentation, and conditional debugging

Windows/MSVC with Qt 6.8.3 and Linux/GCC with both system Qt 6.4.2 and SDK Qt 6.8.3 each pass 37/37 checks. The SDK build verifies Qt 6.8.3 in the CMake cache, link metadata, and resolved runtime library paths before testing, with archive checks required. Its complete test run passes in 40.67 seconds. Linux ASan/UBSan passes 25/25 core and runner checks. The Qt workflows cover custom LCD-off color persistence and Cancel, repeat-frame history, and a conditional breakpoint that loses its symbol.

A condition error before an instruction pauses execution, retains the pending frame and session, and leaves inspection available. Native snapshot equality is verified in a fixture with an empty core audio output queue. The existing pause policy can drain or discard queued audio and is unchanged. Explicit stepping adds no fictitious Backstep entry for a condition failure before execution, while a failed hardware-step attempt retains its history entry.

JOYP comparisons match 4,761,600 readback, interrupt, electrical-row, and countdown records across 92,160 cases using DMG-B, Pocket, CGB-E, SGB, and SGB2 profiles. Another 31,104 real-instruction cases cover three store forms, three read forms, and intervening instructions on DMG/Pocket/CGB. Contact bounce is disabled in those comparisons. Monochrome select-bit readback changes immediately while the electrical row releases later; pending rewrites can retain or extend that deadline. CGB and physical SGB profiles bypass settling.

The noise-selector repair matches 1,592,640 controlled observations, including 49,152 register-only observations and 200,000 deterministic mixed-state cases. High-selector transitions can generate an intermediate pulse and partial shift rather than a complete LFSR clock. Existing width and pre-shift history suffice; no oscillator state was added. These are deterministic CGB-E observations, rather than every silicon instance or audio revision.

Shared-bus and OAM-DMA comparisons match 89,910 normalized records: 67,230 primary memory/access records, 3,240 real CPU outcomes, and 19,440 additional bank-line/mirror records. They compare exact OAM bytes and fingerprints of complete WRAM, VRAM, and 8 KiB cartridge RAM, alongside registers, mapper latches, held data, and decay. Component probes use controlled clock adapters; CPU cases execute instructions. LCD is off. Mode 3 and other CGB revisions remain separate requirements.

Monochrome WRAM/OAM now share the main-bus hold, while CGB retains its separate buses. Contended reads resolve their effective address before drive; writes drive the original address/value before redirection or rejection. Tests cover next-source aliases, the final active tail, CGB high-source suppression, and the initially readable OAM destination slot that still rejects writes. The unused OAM tail returns zero on the current monochrome profiles and the repeated address nibble on the current color profile when accessible. Continuation harnesses execute from HRAM during OAM DMA so the checks do not assume uncontended ROM access.

The RTC probe matches all 4,096 snapshot/readback cases, including stored shadow bytes after driven reads. Every MBC3 latch-window write refreshes the snapshot. Clock reads clear disconnected shadow-register bits together; passive inspection remains unchanged. The clock is halted in this probe to separate mapper behavior from host calendar advancement.

Presentation comparisons match all 1,130 events across 248 DMG-B/CGB-E scenarios: timestamp, normalized event kind, polarity, and two pixel samples per callback. Cases include LCD-off periods, STOP, first-frame blanking, CGB repeats, repeat cutoffs, and VBlank-line refreshes. They do not establish full-image equality or SGB host integration. The frontend retains image/blend history on repeats and provides a separate fifth off color, with a lightest-shade fallback for older custom palettes.

Pure debugger expressions match 7,294 paired records; conditional execution hooks match 8,192 decisions. Coverage includes register/symbol arithmetic, the audited precedence, bank-qualified ROM/VRAM/WRAM views, byte/word reads, and bounded parse failures. Assignment, unary expressions, watchpoints, and range breakpoints remain pending.

Native V8 appends pending JOYP release and presentation history without moving older payloads. V2–V7 imports receive explicit missing-history defaults. The frozen V4 fixture remains 300,572 bytes with SHA-256 `652af4f40504c86dd59eff09f0997f5b60b654816a0908d7802704f981b43be5`. Actual VBlank observation counts stay outside guest snapshots. Runner reports separate `vblank_frames` and `presentation_events`; synthetic LCD-off/STOP events cannot inflate VBlank counts.

ROM+RAM headers `08`/`09` retain direct RAM support as an explicit comparison exception. Their eighteen differing observations remain visible rather than being counted as matches. Two MBC6 diagnostics remain outside the pinned mapper comparison scope. Physical mapper peripherals and complete commercial gameplay still require their own qualification.

### Final regression and library checks

The final hardware implementation at `2638ae4` completes all 181 external suite cases with 177 passes and four bounded cases, matching the preceding build and pinned comparison statuses. The expanded 80-case subset contributes 79 passes and one bounded case to those totals; it is not an additional suite. The four bounded cases cover boot divider, boot register, and serial-clock alignment diagnostics. The final candidate was rerun against hash-verified cached baseline and comparison receipts. Neither cached execution was repeated, and host elapsed time is excluded from the diagnostic comparison.

The generated mapper matrix matches 42,886 of 42,906 observations. Its twenty raw differences are the eighteen retained ROM+RAM observations and two MBC6 diagnostics described above, with no unexpected difference. Separate probes match 17,056 MMM01, 552 MBC7, 7,208 Camera, and 4,736 TPP1 observations, plus two HuC3 and six classic-mapper trace summaries. TPP1 checks also reproduce two paused battery exports and two motor exports byte for byte, and all six candidate import cases pass. The final refresh uses hash-verified cached comparison outputs; the unchanged comparison import cases were not rerun. These generated checks do not qualify physical peripherals.

The final 880-input library refresh uses 420 nominal 70,224-cycle guest intervals per input, without gameplay input automation. All 350 DMG inputs and 524 of 530 CGB inputs reach the bound and emit VBlank and PCM output. Bounded mapper counts are MBC5 481, MBC1 271, MBC3 81, MBC5 rumble 13, ROM-only 12, MBC2 11, and one each for MBC7, HuC3, Camera, MBC1 multicart, and an unknown-header fallback. HuC1, MMM01, MBC30, and TPP1 have no commercial entries in this library. ROM and firmware hashes remain unchanged.

Six MBC5 inputs stop on illegal opcodes, including two additional stops after the shared-bus correction. Their complete recorded PC/opcode/accumulator/stack-pointer traces match the cached pinned execution through the stop: 94, 357,821, 197,367, 1,417, 215,861, and 3,139,898 records. The final refresh reproduces the preceding bus-corrected library statuses and deterministic report fields without changes. This accounts for the additional stops; it does not establish full gameplay compatibility for those inputs or complete hardware-state equality.

### Final bounded core throughput

The final `2638ae4` runners were compared with preserved runners built at `02f69c4`. The core, headers, runner, and build description are byte-identical between that baseline revision and merged `d8496e8`. Both hosts used Release builds with interprocedural optimization and native hashing enabled: GCC 13.3.0 on Linux and MSVC 19.43.34809.0 on Windows. Each run executes 3,600 guest intervals, approximately 60.274 guest seconds, with the same local firmware and recorded inputs. Hosts ran sequentially, discarded one warmup pair per profile, and measured five alternating baseline/final pairs. The values below are median elapsed times for the complete child process, excluding clock-rate queries. Positive changes mean longer execution time.

| Host and input | Baseline median | Final median | Change |
| --- | ---: | ---: | ---: |
| Linux, Tetris | 7.133 s | 7.031 s | -1.44% |
| Linux, Mario Deluxe | 7.969 s | 8.255 s | +3.59% |
| Windows, Tetris | 7.453 s | 7.687 s | +3.13% |
| Windows, Mario Deluxe | 9.196 s | 8.964 s | -2.53% |

Linux elapsed times use `CLOCK_MONOTONIC_RAW`; Windows uses QueryPerformanceCounter. Read-only clock queries bracket every run and pair. Linux tick settings changed between 9,801 and 10,833 microseconds during its sweep, so the runner's adjusted monotonic times remain separate from the primary measurements. No clock setting was changed for these checks. Linux child CPU medians changed from 7.060 to 7.018 seconds for Tetris and 7.956 to 8.168 seconds for Mario Deluxe. Windows CPU counters varied widely between repeats and do not establish an improvement. Private receipts retain every elapsed and CPU sample, adjusted runner time, clock snapshot, command, input hash, and artifact hash.

All comparable guest counters and complete PCM/PPM captures match across the two builds and hosts. Each build repeats the same signatures in all six runs. The added `presentation_events` field reports 3,603 events for Tetris and 3,604 for Mario Deluxe; the baseline has no separate field. Its legacy `vblank_frames` counted presentations, whereas the final field counts physical VBlank events. Their values happen to agree in these traces. These uncapped core measurements do not establish Qt frame pacing, sustained interactive gameplay, or performance for other games.

A private candidate restored a direct return for resolved non-main-bus reads without cheats, preserving guest read side effects. It passed 25 core and runner tests on each host and retained all counters and full media output. Screening used 1,800 intervals, one discarded warmup pair, and two alternating measured pairs per profile. Linux elapsed time fell 3.22% for Tetris and 1.60% for Mario Deluxe, but Windows time rose 4.67% and 3.52%. The candidate was rejected. No bank-wrapping change or other performance modification was admitted.

Private evidence for this final refresh is retained under `input-audio-20261111` in `external/post-rtc/paired-suites.json`, `mappers/final-2638ae4/final-verdict.json`, and `performance/final-receipt.json`. The receipts identify the source revision, artifacts, cached comparison outputs, and their hashes. ROMs, firmware, saves, and captured media remain private.

### Final native desktop checks

The refreshed Windows installation passes the native audio-required workflow with no SDK on PATH. The installed Linux system-Qt build and the separately verified Qt 6.8.3 SDK build pass native Xcb UI workflows. Those Linux UI checks do not require audio playback.

Installed Windows Qt 6.8.3 sessions used local firmware, recorded inputs, normal audio, and default rewind. Every loaded Qt DLL came from the installed folder, with only system directories on PATH. The following samples exclude the first five seconds of each session. The first four sessions lasted 60 seconds; the Silver session lasted 90 seconds. All five report zero audio idle events and zero audio errors.

| Input and model | Nominal guest progress | Widget paint p95 / maximum | Peak working set |
| --- | ---: | ---: | ---: |
| Tetris, DMG | 99.976% | 17.84 / 26.93 ms | 67.03 MiB |
| Mario Deluxe, CGB | 99.982% | 17.79 / 20.78 ms | 88.23 MiB |
| Tetris, SGB | 100.011% | 17.36 / 23.43 ms | 71.24 MiB |
| Donkey Kong, SGB2 | 99.980% | 17.78 / 24.08 ms | 120.62 MiB |
| Silver, CGB | 99.986% | 17.75 / 24.11 ms | 123.22 MiB |

The Silver session applies 33 recorded input events and reaches New Bark Town. Its in-game save has a valid primary checksum and completion markers. A fresh 30-second process selects Continue and returns to the overworld, with 99.992% post-warmup guest progress and zero audio idle/errors. The primary saved block and checksum remain unchanged after reopening; scratch and clock regions account for 336 changed SRAM bytes, so the complete SRAM is not asserted equal. The native captures were inspected. This covers a bounded overworld save/reopen workflow; starter selection, battles, and complete gameplay remain unverified.

A separate 36-second SGB2 capture shows active first-stage gameplay with the full border. Its final 31 seconds reach 100.028% nominal guest progress with zero audio idle/errors and paint p95/maximum 17.69/20.06 ms. All 34,304 corrected border pixels match the pinned oracle. The viewport has advanced to another gameplay time and is excluded from that pixel comparison.

A 20-second installed Linux Qt 6.4.2 Tetris pilot reports 99.912% post-warmup guest progress, zero audio idle/errors, and paint p95/maximum 17.49/18.35 ms. Read-only clock queries show tick 10,000 microseconds/frequency zero before it and tick 9,997 microseconds/frequency +49.284 ppm afterward. No clock setting was changed.

Subsequent 60-second Linux sessions give mixed results. Tetris reaches 100.005% post-warmup guest progress with zero audio idle/errors. Mario Deluxe reaches only 7.194%, with seven audio idle events and a 13,366.92 ms maximum paint interval. Donkey Kong/SGB2 reaches 28.908%, with six idle events and a 17,285.15 ms maximum paint interval. All three report zero audio errors. After this series, the clock query shows tick 10,833 microseconds/frequency +32.977 ppm.

A preserved preceding installed build completes a shorter 20-second Mario Deluxe control at 100.057% post-warmup guest progress with zero audio idle/errors. Its matched 60-second control then reaches only 18.044%, with seven idle events, zero audio errors, and a 12,183.02 ms maximum paint interval. The final clock query shows tick 10,833 microseconds/frequency +33.130 ppm. Both builds reproduce long stalls at the matched duration, with different measured rates. Their cause remains unverified, and sustained Linux playback qualification remains blocked. Private receipts are retained in `windows-final-native`, `windows-silver-native`, `windows-silver-reopen-continued`, `windows-sgb2-active`, `linux-final-audio-pilot`, `linux-final-native`, `linux-preserved-control`, and `linux-preserved-control60` under the same validation directory.

## Motion cartridge controls

Windows/MSVC with Qt 6.8.3 and Linux/GCC with system Qt 6.4.2 pass 38/38 checks after the motion-classification repair. The refreshed Windows installation passes its native audio-required workflow with only system directories on PATH. The Linux installation passes its native Xcb UI workflow; that check does not require audio playback.

The new checks cover raw and mapped tilt axes, unavailable/duplicate physical-axis rejection, legacy settings, Cancel and persistence, deterministic controller ownership, single-player routing, and focus/background gates. Mouse checks cover rendering viewport geometry, four display pixel ratios, integer/aspect/fill scaling, borders, A-button merging, source ownership, and ordinary-cartridge isolation. Native state loads retain current host tilt and held input while restoring guest sensor latches. Nonmodal loader failure retains host input; a real modal dialog can independently invoke the normal focus-release policy.

Motion classification follows the loaded cartridge, independently of temporary delivery gates. Real virtual-device regressions hold tilt axes, Turbo/Rewind and old keyboard/mouse A through cartridge changes and pause/resume. They check JOYP interrupts before the first guest instruction and verify that atomic reconciliation cannot reset audio against old session metadata. New-session host samples reset to neutral; state loading retains live samples. The hardware core and V8 wire format are unchanged.

A private paired probe matches all 308,834 latched X/Y pairs across DMG and CGB: every signed 16-bit axis code on each axis, plus every inclusive 161-by-145 pointer coordinate. Mapping uses the positive sensor center before integer conversion. Truncating a negative fractional offset toward zero instead of rounding down differs on 161,984 pairs. The probe drives the real mapper enable/reset/latch/read protocol; it does not compare only a standalone arithmetic helper.

The only local MBC7 commercial input is marked as a modified dump. A bounded private headless replay reaches the first stage and verifies actual CPU sensor reads and visible changes for four tilt directions from the same initial state. The game also writes its own EEPROM when changing a position option. A fresh instance imports those exact 256 bytes and displays the saved option, retaining identical EEPROM on export. These results qualify that modified input and bounded paths; an original cartridge dump, physical accelerometer streams, original-hardware timing, complete gameplay and sustained Linux audio remain unverified.

Seven Windows native sessions use a private event driver compiled with the same frontend/core sources, using public MainWindow APIs and real Qt pointer events. This is not the production CLI executable. Normal audio and default rewind remain enabled. Five 32.5-second sessions cover neutral and four directions. A 35-second session changes the in-game option, and a fresh 32-second process reopens the actual native-created EEPROM. All seven retain 97 source-file hashes through building and runtime; original ROM/boot inputs remain unchanged.

After a five-second warmup, guest progress ranges from 99.965% to 100.025%. Widget paint p99 ranges from 19.41 to 20.62 ms. All sessions report zero audio idle, error and sink-error events. Captures show actual character/scene movement and the retained option. The 256-byte EEPROM export after reopen is byte-identical to the native-created file and independent guest-generated headless save. Button traces finish released before a public empty-trace transition enables pointer control. The product trace counter then resets to zero; injected input evidence comes from the private driver logs, rather than that final counter.

A separate 60-second run uses the refreshed installed production executable, local firmware, normal audio and default rewind. Tetris reaches 99.978% post-warmup guest progress with zero audio idle/errors, paint p95/maximum 17.91/37.85 ms, and a 66.68 MiB peak working set. This control covers ordinary-cartridge routing; it does not establish every game's performance. Sustained Linux audio remains blocked by the previously recorded host stalls.

Private probe and fixture receipts are retained under `input-audio-20261111/motion`, including `oracle-results.json`, `audit-and-headless-verdict.json`, and `native/post-classifier/final-native-verdict.json`. ROMs, boot binaries, EEPROM saves and captures remain outside public commits.


## Shared video bus and object-fetch timing

The 2026-11-11 integration passes 39 CTest checks on Windows with Qt 6.8.3, 39 on Linux with Qt 6.4.2, and 26 Linux core checks with ASan/UBSan and leak detection. These include the boot-entry repair found during review: entering firmware aborts the old object countdown as well as its stage. The regression reproduces the retained countdown before the repair and checks saving/loading after boot entry across all four stages. All production C/C++ files remain below 2,000 lines.

The first parallel Linux desktop run aborted inside PulseAudio during a short command-line workflow. The preserved preceding build and current build both passed standalone CLI controls. Full serial desktop runs then passed, including the final build. The failed log remains retained. This result does not resolve the sustained Linux audio stalls recorded above.

Private paired probes compare equivalent hardware, deterministic synthetic cartridges and the pinned revision. All 12,288 retained contention cases match complete VRAM, all 160 OAM bytes, guest time and transfer status. The set combines 6,912 primary cases with 5,376 cases using additional source pages. It covers both CGB speeds, VRAM banks, LCD state, source classes, destination wrapping, warmup and overlapping transfers. This closes those reproduced cases, not every possible bus collision.

Another 18,816 observations after actual CPU steps and 6,400 observations through interrupt acknowledgment match the compared CPU, timer, audio phase/count, DMA, LCD, VRAM and valid OAM fields. An expanded 125,440-observation sweep preserves all valid OAM, DMA, timer, audio and LCD matches without introducing a new field mismatch. It retains 128 CPU and 1,152 VRAM differences that already existed. Palette-tail differences remain separate; no full-state equality claim includes them.

A per-dot screen checks 4,032 cases across matched monochrome, Pocket, color and SGB2 profiles. It covers clipped sprites, eight scroll offsets, overlapping objects and both heights. Background, OAM-word and sprite-data transactions and pixel emission match; color cases also compare emitted 15-bit pixels. Controlled tile/flag writes on either alignment edge establish when the word is sampled. A later held scan verifies that the sprite word replaces both retained OAM bus bytes. Raw unused-OAM readback matches all 1,536 profile/address cases, and all 2,457,600 reachable redirected reads match. No out-of-bounds OAM storage access is reproduced.

The composed V9 implementation passes 292 snapshot continuations spanning initial discarded pixels, pending alignment/word reads, data countdowns, mid-fetch window writes and older busy imports. Comparisons use an un-serialized continuation branch. Ordinary and trusted state paths preserve the retained row; changing that row in a naturally captured normal-speed state changes future OAM. Fast-profile checks establish wire preservation and continuation, without claiming the same whole-machine-cycle mutation witness. Frozen V3/V4 fixture assertions still pass, and malformed row, stage, selected-object and boolean fields leave the target unchanged.

The external screen remains 177 explicit passes and four bounded cases out of 181. Failed intermediate grid and external results remain preserved alongside the corrected results. The paired artifact receipts precede the separately tested boot-entry lifecycle repair. Private receipts retain source/artifact hashes, full outputs and commands outside version control.

Complete FIFO/window timing remains open: the checked CGB window restart is still one dot early. Sampled STOP OAM/palette gates, broader hardware revisions, remaining bus conflicts, all-game compatibility and sustained playback qualification also remain open. Current-writer continuation checks do not reconstruct missing timing history in older files.


Uncapped throughput controls compare the preceding merged build with accuracy checkpoint `9531139`. Each run advances 3,600 guest intervals, approximately 60.274 guest seconds. One warmup pair is discarded before five alternating measured pairs per game and host. Complete PCM, final images and guest counters match across builds and hosts for both inputs. Windows QPC elapsed medians increase from 7.620 to 8.160 seconds for Tetris and 8.906 to 9.589 for Mario Deluxe. Linux `CLOCK_MONOTONIC_RAW` medians increase from 6.786 to 7.725 and 8.034 to 8.458 seconds. Adjusted Linux clock measurements remain separate because the host rate changes. These are headless throughput costs, not presentation FPS or sustained playback results.

A counts-only profile finds no selected or pending object in more than 96% of enabled lookup calls for those sequences. The lookup now returns immediately for zero selected slots or a fully fetched selected mask, ignoring unused higher bits from older snapshots. A private 46,137,344-case check preserves return values and mask mutations. Three measured alternating pilot pairs after warmup retain complete outputs and lower elapsed medians relative to `9531139`: Windows by 2.06%/1.02% and Linux by 1.46%/0.62% for Tetris/Mario Deluxe. These small pilot changes qualify the lookup shortcut only; the added accuracy work still has a net throughput cost. Both desktop suites and the sanitizer suite pass again after the shortcut.

## Native ports and desktop workflows, 2026-11-15

The current hardware work samples STOP access independently for VRAM, OAM and the
color-palette port, retains the palette release edge in native version 10, and
keeps versions 2 through 9 readable with defaults for missing history. CGB reset,
register dispatch, palette access, DMA and rendering now occupy separate modules.
The hardware tests also cover monochrome OAM-conflict phases and pixel-queue lane
shifting. These checks do not establish complete fetcher timing or every hardware
revision.

A PPU-only comparison covers 64 physical-color profiles: color and compatibility
mode, normal and double speed, eight scroll offsets, and zero or ten objects. It
samples 1,400 dots per profile, then repeats with nine directly latched STOP
positions. All 896,000 access masks and STAT mode samples match. A first run finds
160 differing sprite pixels: compatibility mode had discarded physical attribute
reads, losing background priority when VRAM was blocked. The targeted regression
fails before the repair and passes afterward. The current production core matches
all 276,480 compared RGB555 words. Internal line and LCD-x bookkeeping still differs
at some boundaries. This fixture does not execute CPU STOP or cover DMA, audio,
every pixel pattern or complete pipeline equivalence.

The desktop work adds palette-file import/export, a persistent named palette
library with automatic color controls, timestamped captures, Save As and clipboard
capture. Restoring built-ins at the library limit now rejects the whole operation
and preserves custom entries. The automatic theme collection remains incomplete.
Windows resources and optional Open With scripts are included; registration tests
use a private registry hive and preserve current user associations.

Audio queue writes release the queue lock before calling the backend. In-flight
operations retain the old queue and sink through reentrant session changes. Queue
replacement and telemetry resets cannot consume replacement samples or charge the
new reporting window. Stream-capacity checks validate ownership again before
writing. Native callback tests close the cartridge during a write and open a menu
during timer pumping; the menu case must retain the guest cycle count.

Windows MSVC/Qt 6.8.3 Release passes 45/45 checks, including both private-hive tests.
An earlier Linux GCC/Qt 6.4.2 Release suite passes 43/43. After the attribute repair,
42/43 pass; the CLI check hits the intermittent backend abort described below.
The separate Linux ASan/UBSan core build passes 26/26 after the repair with leak
detection enabled. Native Windows and Linux audio-enabled
Qt workflow checks pass. The palette editor was captured and visually reviewed
under both native window systems.

The first Linux desktop suite aborted one short CLI launch inside PulseAudio with
`mainloop_io_free` asserting `!e->dead`. The standalone CLI check and one complete
43-case rerun pass, but later launches reproduce PulseAudio dispatch assertions.
A clean `bd41bf4` baseline also reproduces the abort on its first CLI test run.
A debugger capture places the current failure in the PulseAudio thread during
Qt's global audio-engine destruction, after the core has shut down. This does not
prove the exact cause. A later
60-second Linux CGB run completes, but the WSLg audio socket is missing and stream
creation fails. Its clock and image measurements do not qualify sustained audio.

A Windows CGB input sequence reaches commercial course selection over 60 seconds.
After five seconds of warmup, guest progress is 99.995% with zero audio idle or
error events. This run starts with a hidden window and has no widget paint events;
it qualifies the bounded guest/audio counters only. It does not establish visible
presentation, physical display refresh, complete gameplay or all-game speed.

A separate visible Windows DMG input run reaches an active falling-block board.
The 55-second window after warmup records 99.994% guest progress, 3,287 widget
paints, zero audio idle events and zero audio errors. Widget paint intervals have
a 16.732 ms median, 18.081 ms 95th percentile and 20.408 ms maximum. An earlier
menu-only trace records one startup audio idle event; it is retained separately.
These are bounded application observations, without a listening assessment or
physical display-refresh measurement. They do not qualify every game or saving.

The matching Linux Qt 6.8.3 SDK passes 43/43 desktop checks, three additional
complete CLI workflow runs, and a native Xcb audio-enabled smoke check. Desktop CI
now selects that SDK on Linux as well as Windows. The earlier Qt 6.4.2 failures
remain recorded; changing the validated toolkit does not establish long-term
Linux audio stability or resolve changes to the host clock and audio service.

A corrected Windows CGB trace enters level 1-1 and exercises running, scrolling
and jumping. The 60-second run ends in the active level with terrain and an enemy
visible. Its post-warmup window records 99.994% guest progress, 3,290 widget paints,
zero audio idle events and zero audio errors. Paint intervals have a 16.719 ms
median, 17.987 ms 95th percentile and 24.123 ms maximum. A separate 35-second
capture caught a guest-controlled LCD-off transition: private per-interval checks
show the display returning within five intervals. That white capture is retained
without treating it as a persistent renderer failure.

Active commercial DMG and CGB states also pass a bounded Linux V10 continuation
check. Independent instances preserve 17 queued PCM frames at restore, then match
443,012 drained stereo frames, 600 presentation buffers, final framebuffers and
the complete final native state bytes over roughly ten further guest seconds.
This checks the core codec and continuation; frontend disk workflows and
cross-platform state interchange have separate requirements.

The refreshed external corpus is pinned at `31510e12eea6286d36eea060a6adde755e1067aa`.
Source-qualified shared-boot cases pass on both Windows and Linux: 66/66 DMG and
53/53 CGB acceptance cases, plus 28 MBC1/MBC2/MBC5 banking cases. Two banking
exhaustion cases remain bounded at the initial 600-interval limit and pass their
separate 3,600-interval runs. All 149 paired observations, including those initial
bounded results, agree on verdicts, guest counters, image/PCM checksums and exit
codes. These counts cover the selected models and protocols. Source-inapplicable
startup revisions remain excluded, and these three mapper families do not stand
in for every cartridge peripheral or all-game qualification.

The Qt 6.8.3 native Linux CGB run also reaches active level 1-1 over 60 requested
seconds with normal 44,100 Hz output. It exits normally with zero audio errors
and zero startup or post-warmup idle events. The 55-second post-warmup window
reports 99.997% guest progress against the adjusted host clock. Read-only clock
sampling records 122 successful observations, changing ticks from 9,644 to
9,685 microseconds and changing frequency correction. Adjusted monotonic time
advances at roughly 0.9644 to 0.9685 of the RAW clock's rate. Consequently, the
near-nominal adjusted-clock result does not establish stable real-time speed.
Peak sampled resident memory is 114,680 KiB. The sampler makes no clock writes.

The refreshed local library inventory contains 880 inputs and 878 distinct ROM
hashes. A cold shared-boot scan gives each input 420 guest intervals with no input
or battery loading: 874 reach that bound and six stop on illegal opcodes. All
894 original files, including adjacent user data, retain their hashes and
modification times. A bounded startup is not a compatibility verdict. The library
contains ROM-only, MBC1, MBC2, MBC3, MBC5, MBC7, HuC3 and Camera inputs; it has no
commercial HuC1, MBC6, MMM01 or TPP1 input. One nonstandard header is retained in
the inventory and excluded from verified mapper coverage.

All six stopped inputs also reach illegal instructions in the comparison build.
Default startup matches five terminal register sets and four complete preceding
64-instruction windows. Source inspection and repeated controls identify a
startup-memory difference: the comparison's color profile varies work/high RAM,
while CupidGB initializes it to zero. A separate reference-only zero-randomness
control matches all six terminal sets and preceding windows. These diagnostic
controls do not establish default startup equivalence or justify changing an
input. A clean `bd41bf4` baseline matches all six current terminal sets, preceding
windows and base-cycle counts. Power-on memory variation remains a parity
requirement.

One hosted portable-hash sanitizer state check exceeds its 120-second limit;
the identical check passes in 111.53 seconds in the other event's job. A local
complete run takes 132.21 seconds. The instrumented portable-hash configuration
now gives that state check 240 seconds, retaining the entire matrix. Other
configurations keep the 120-second state limit. The local portable state, SHA-256
and C/C++ integration checks all pass with leak detection enabled.

## Cartridge storage, pulse clocks and Qt playback, 2026-11-15

This milestone uses the reference revision recorded in the parity checklist.
Private receipts retain the source snapshots, executable hashes, test inputs,
boot hashes and commands. Existing dirty checkouts were preserved; implementation
and validation took place in a separate worktree.

The cartridge header matrix covers all 256 type bytes, eight RAM-size codes and
both DMG and CGB. Of 4,096 paired cases, 4,066 match exactly. The remaining 30 are
the documented direct-RAM types `08` and `09` and partial MBC6 behavior, ten each.
No additional discrepancy remains in that matrix. Another 2,244 paired MMM01
cases match storage capacity, controller selection, battery/timer/motor flags,
ROM reads and RAM/open-bus observations. These cover both dump layouts,
conflicting menu/game metadata, truncated headers and padded physical tails.

Address-line tests cover 15 cartridge variants, nine physical ROM capacities and
512 register sequences per image. They verify erased tails, both ROM windows and
RAM aliases against the loader's power-of-two geometry. The address tests reuse
the logical bank-selection API; dedicated controller tests and the paired
runtime observations provide the separate decoder evidence.

Pulse checks cover both channels, four duties, two divider offsets, first
triggers, live duty writes, active retriggers, length expiry, inactive retriggers,
DAC disconnect/reconnect and subsequent triggers. All 61,440 recorded
observations match in phase, channel status and raw sample: 49,152 across four
monochrome models and 12,288 on the modeled color revision. The first timing
repair still differed in held DAC samples; that discrepancy was retained and
resolved before the final comparison. These checks do not establish analog DAC
response, complete wave/noise/envelope behavior or audible quality.

Independent review found a combined trigger/length-enable write that temporarily
expired a one-tick counter before choosing trigger timing. The fix samples
activity at entry to the write, preserving active retrigger delay and held
output. A regression covers both pulse channels, all four power phases, five
monochrome model tags and color hardware at both CPU speeds: 56 configurations.
The separate paired probe covers 96 scenarios and 393,600 observations, including
expiry without trigger and active/inactive controls. All observations match.
The frozen preceding build differs in all 98,400 observations from the 24
combined-trigger scenarios; its other 72 scenarios match.

Native version 11 adds two validated pulse DAC codes and preserves monochrome
pulse timing that older writers did not maintain. Continuation checks include
active, suppressed, length-expired and disconnected channels. Older-format
fixtures retain their existing hashes. Versions 2 through 10 load with explicit
defaults for missing pulse history, without borrowing a destination session's
state.

The final source passes 46/46 Windows MSVC/Qt 6.8.3 Release checks in 42.37 seconds
and 44/44 Linux GCC/Qt 6.8.3 Release checks in 45.90 seconds. Sanitizer validation
includes the 26-check pulse/storage core run and focused ASan/UBSan rebuilds for
the later debugger repair and expanded TPP1 regression.
An earlier portable-hash sanitizer run passed the state, SHA-256 and C/C++
integration checks, with the state matrix taking 163.48 seconds. Its final
rebuild passes the latter two checks but reaches the unchanged 240-second state
limit under concurrent validation. A repeat after the library scans finish
passes all three checks; the complete state matrix takes 108.43 seconds within
the same 240-second limit. The original failed receipt remains intact, and no
cases or assertions were removed.
Native capture review found that tiny unhinted glyph paths fragmented letters
even after drawing the white face over its outline. The renderer now uses hinted
monochrome text at integer baselines, with a one-pixel axial outline and
12-pixel line spacing. Native Windows and Xcb previews resolve readable system
monospace faces. Exact bitmap typography remains unqualified.

The new Windows glyph regression initially fails because the offscreen plugin
finds zero font families and draws identical missing-glyph boxes, including for
a space. Native rendering resolves Consolas and has intact letter faces. The
same test binary passes unchanged when its process-local font directory points
to the installed Windows fonts. CTest now supplies that directory from the
Windows environment. No glyph assertion was weakened; the failed test and
private diagnostic attempts remain preserved.

Earlier native Windows and Linux Xcb workflow checks passed with real audio.
Final installed-package checks are recorded separately below. A new callback
case exposed a test fixture that queued four bytes despite a negotiated
eight-byte Windows host frame. The fixture now uses the negotiated frame size;
the production audio path was unchanged by that correction. A Linux attempt
failed before those cases because the existing WSLg audio socket was absent.
The failed receipt remains recorded. A later read-only readiness check found the
socket present, and the audio-enabled workflow passed. No audio service or global
host setting was changed.

Later checks explain the intermittent missing socket during fresh WSL boots.
In one recorded boot, both `stat` and the Python socket check report `ENOENT`
through uptime 11.50 seconds. The socket appears at 12.52 seconds, and a bounded
protocol probe connects successfully with daemon autospawn disabled. The private
native-validation harness now waits up to 15 seconds for that existing-server
connection and launches the application from the same process. This addresses
test startup readiness without restarting or reconfiguring the audio service.
Earlier attempts that stopped before launching CupidGB remain recorded as such.

Native captures on both platforms were visually reviewed for tile grids,
viewport wrapping at SCX 200/SCY 224 and message placement. Overlay workflow
checks preserve paused native-state bytes and clean screenshot pixels. Eighteen
combinations cover three blend modes, both row phases, the DMG viewport, the SGB
viewport and the full SGB border image. Messages compose before blending; repeat
events age logical text while retaining the submitted raster. Expiry and setting
changes preserve the previous frame's blend contribution until guest
presentation advances. Repainting or pausing does not consume message lifetime.
Startup checks cover version, header title and logical CRC across ordinary,
truncated and MMM01 layouts.

Further review reproduced a paused redraw defect: disabling messages retained
their pixels for the first paint, but changing the blend mode or applying
unchanged Settings again erased them. Presentation now distinguishes host
redraws, completed guest frames and explicit history resets. The regression
fails before that repair and passes afterward. It exercises the actual actions
and repeated settings application in all 18 configurations, plus paused startup
before the first guest frame, queued notifications, Repeat and subsequent
publication. Open/load/rewind/Backstep/edit callers were audited for explicit
reset intent; the message matrix does not separately qualify notification pixels
through every real restore workflow.

Turbo cap checks exercise every menu choice, settings persistence, pacing gates,
combined slow/turbo rates and callback reentrancy. A separate external held-key
driver could not retain X11 focus on this WSLg session, and external window
readback returned black. Those attempts remain recorded and do not qualify
physical held-key timing. The application's native workflow checks and captures
pass; no production change was made to work around the external driver.

The expanded Windows and Linux external runs use the acceptance/banking revision
`31510e12eea6286d36eea060a6adde755e1067aa` and the additional hardware-test revision
`c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15`. They cover 242 explicit passes across
243 cases: 119 source-qualified acceptance cases, 56 DMG/CGB banking cases,
24 audio cases, 22 CPU cases, 14 memory/instruction timing cases and seven OAM
cases. One OAM timing ROM remains bounded, including at an extended limit.

The corrected Windows run initially has 241 passes, one bounded case and one
missing report. The missing case exits before emulation because its ROM buffer
allocation fails. Windows commit availability was about 507 MiB when inspected;
the private resource receipt retains the host counters. An isolated rerun with
the identical executable, input, firmware, model and limit passes. With that
separately recorded observation, all 243 Windows/Linux guest results agree,
including cycle and instruction counts, final image and PCM checksums, and
protocol output. The original failed record remains unchanged.

The remaining ROM's text logger crosses the end of its 8 KiB RAM window and
overwrites copied executable code at `C000`. Both cores execute the first
overflowing instruction at cycle 77,746,804, overwrite the byte at 77,746,816 and
disable cartridge RAM at 79,465,692. Their terminal registers, memory and the
preceding instruction trace agree. A private diagnostic copy suppresses only
the memory-text append call while retaining all 116 timing cases, their expected
CRC, console output and result protocol. It passes on both cores; the integrated
V11 runner also passes at cycle 114,321,652 after 12,735,631 instructions. The
original ROM remains unchanged and its corpus verdict remains bounded.

The final V11 replay checks nine cases from eight local cartridge images across
ROM-only, MBC1, MBC2, MBC3, MBC5, MBC7, HuC3 and Camera. Every case runs a local
restore on each platform and imports the other platform's saved file: 36/36
paths pass. Windows and Linux saved/final bytes, images, PCM and continuation
reports match. The comparisons cover 319,566,188 CPU instructions, 21,608
presentation/memory observations and 432 complete native states. All 24
protected source files retain their bytes, size and modification times.

These cases use HLE post-boot profiles, fresh battery memory and a private RTC
clock derived from guest time. Each save occurs during visible mode 3 after
3,000 guest intervals, with 17–19 queued PCM frames, and continues for another
600 intervals after loading into a separately advanced instance. The observed
phases include active GB/GBC play, an additional MBC1 life-loss sequence, MBC2
name entry, MBC3 introduction dialogue, MBC7 play/tutorial screens, HuC3 movement
and synthetic Camera captures. The Camera capture counter advances from 174 to
182. The local MBC7 image is modified and remains qualified by its exact private
hash. This covers bounded continuation and file portability; it does not qualify
physical peripherals, host audio delivery or complete commercial gameplay.

A frozen Windows throughput comparison isolates the cartridge storage/address
changes from the other work. Five alternating repetitions per input run 1,800
guest intervals with normal PCM generation and a fixed processor affinity.
Every guest report, final image checksum and PCM checksum matches. Median host
times change from 4.863 to 4.910 seconds for ROM-only, 4.411 to 4.420 for MBC1,
4.379 to 4.247 for MBC2, 6.187 to 6.164 for MBC3 and 7.649 to 7.581 for MBC5.
The results are mixed, and other validation was active. They do not establish a
general speedup or sustained presentation performance.

### Debugger ROM reads and supported cartridge ranges

Bank-qualified debugger expressions previously wrapped an address within the
original file length. A read in the erased tail of a truncated physical bank
could therefore return bytes from the start of the image. The debugger now uses
the same physical bank population as mapped reads and returns `FF` beyond the
available file bytes. This change affects debugger inspection only.

Twenty-eight generated images cover 420 explicit expressions and 28 conditional
breakpoint checks. Native-state bytes remain identical before and after each
inspection. All 4,928 paired reference reads match; the preserved preceding
debugger differs on 2,136. The debugger tests pass on Windows and Linux.

Expanded TPP1 regressions cover 12 DMG/CGB capacity cases from no SRAM through
128 KiB. They check 2,560 read-only aliases, 2,560 writable aliases and 47,616
independent reads for unintended changes to other physical banks. Another 1,024
mode/latch cases verify absent-SRAM bus values at zero, five and six T-cycles,
including expiry without hold renewal. An 8 MiB fixture distinguishes all 512
physical ROM banks with two-byte markers. It exercises 131,072 complete 16-bit
register selections across both models, each intermediate byte-write state,
register mirrors, raw latch readback and a fixed lower ROM window. Both platform
tests pass. ROM above 8 MiB and SRAM above 128 KiB remain unsupported.

### Extended local startup scan

The corrected hardware build ran all 880 local input files, representing 878
distinct images, for up to 3,600 nominal 70,224-cycle guest intervals each. Models were
selected consistently with the private inventory: 556 CGB, 198 DMG and 126 SGB.
Runs use shared local boot binaries, fresh storage, no button input and no
success protocol. A bounded result means only that the run reached its limit.

| Cartridge family | Inputs | Bounded | Core error |
| --- | ---: | ---: | ---: |
| MBC5 | 500 | 493 | 7 |
| MBC1 | 272 | 272 | 0 |
| MBC3 | 81 | 81 | 0 |
| MBC2 | 11 | 11 | 0 |
| ROM-only | 12 | 12 | 0 |
| MBC7 | 1 | 1 | 0 |
| HuC3 | 1 | 1 | 0 |
| Camera | 1 | 1 | 0 |
| Unclassified header | 1 | 1 | 0 |
| Total | 880 | 873 | 7 |

Both the initial V11 scan and the corrected-trigger scan produce those counts.
All input identities match between them. Status, instruction and cycle counts,
presentation counts, final image checksums and protocol fields agree for all
880 inputs. Nine PCM checksums change after the pulse correction; their sample
counts remain equal. This is a comparison between CupidGB builds, not a complete
reference-audio comparison. All 894 protected source files retain their bytes,
size and modification times.

Six errors retain the termination fields from the earlier, shorter scan. The
additional stop occurs after 2,648 presentations on a 384 KiB image whose header
declares 1 MiB. It also occurs in the frozen preceding build. Investigation
follows a bank switch into a sequence of `FF` bytes, repeated restart
instructions, a descending stack and eventual execution from work RAM. A pinned
reference run and selected controlled power-on RAM fills reproduce the terminal
instruction history; other fills change the later path. Power-on RAM variation
therefore remains a qualification gap. No per-game exception was added.

There are no local commercial HuC1, MMM01, TPP1 or MBC6 inputs in this inventory.
Their generated register tests must remain separate from commercial evidence.
The scan does not establish that every game boots, accepts input, saves correctly
or runs at nominal speed.

### Native gameplay measurements

Two Windows runs used the corrected pulse core, normal audio, fresh private
cartridge copies and recorded input. Each ran for 120 seconds with rewind
capture enabled. They preceded the final debugger and message-rendering review;
those later changes do not alter the emulation path, and messages were disabled
in these fresh settings.

| Input and model | Post-warmup guest progress | Paint interval p95 / maximum | Audio error / idle events | Peak resident memory |
| --- | ---: | ---: | ---: | ---: |
| Tetris, DMG | 100.0016% | 18.34 / 35.47 ms | 0 / 0 | 102.22 MiB |
| Mario Deluxe, CGB | 97.4513% | 19.32 / 1,640.02 ms | 0 / 0 | 141.73 MiB |

Both processes exit successfully, apply the recorded controls and retain the
original inputs and existing saves. The CGB run writes and flushes a private
8 KiB battery file. Its final capture shows active first-stage gameplay; the DMG
capture shows a game-over screen. The latter is not evidence of 120 seconds of
continuous active play.

Other validation and host commit pressure were present during these runs. The
CGB stall and below-nominal guest progress remain part of the record. Paint
events measure widget activity, not physical refresh, and zero backend events
do not establish audible equivalence. A Linux attempt stops at its prelaunch
readiness guard because the WSLg audio socket is absent; no application playback
occurred in that attempt.

A later 120-second CGB repeat uses the final installed Windows executable,
`e58c272dcf83d4f8f04a9ae3ce3f55a38c8234834c095dee3bdc184c0270d50a`,
with no concurrent builds or validation runs. Guest progress after warmup is
100.0048% of the nominal clock. Widget paint intervals have a 16.74 ms median,
17.66 ms p95, 18.26 ms p99 and 27.46 ms maximum. Normal audio remains enabled,
with zero sink errors or idle events after startup. All 22 recorded inputs apply,
the private 8 KiB battery file flushes, and the final capture shows the player,
enemy, pipe and HUD in the first stage.

Peak process memory is 142.69 MiB, with 7.0 seconds of sampled application CPU
time. Sampled host CPU activity has a 1.39% median and 6.40% maximum. The earlier
stalled sample, original inputs and installed executable remain unchanged.
This repeat qualifies one cartridge over two minutes under a quieter host load;
it does not establish a code speedup, universal full speed, physical refresh
timing or audible quality.

### Installed desktop checks

The final Windows folder deployment contains 295 files and uses the executable
hash recorded above. Its native workflow smoke passes in 11.46 seconds with an
actual audio sink. The process search path contains only the staged executable
directory and Windows system directories; `QT_PLUGIN_PATH` is unset. Module
receipts place Qt, SDL2 and the required desktop/media plugins in that stage.
All ten workflow captures are retained, including readable startup and action
messages. The included runtime redistributable was not executed, and this
existing development machine does not qualify a clean installation.

The Linux installation initially reaches its 180-second native-workflow guard.
Two bounded debugger traces find the main thread probing missing theme icons
under the private prefix on the Windows-mounted filesystem: first during
Settings construction, then during screenshot-dialog construction. Neither
trace shows a file dialog waiting in its modal loop. Reordering the system icon
paths while retaining that prefix still stalls. Removing the prefix narrows the
cause, but the final check retains it.

Generating and validating the prefix's hicolor icon cache resolves the measured
delay. A fresh 22-file stage differs from the original only by that cache; every
original installed file and the application icon retain their hashes. Its native
Xcb workflow passes with real audio in 17.10 seconds under the unchanged
180-second guard. All ten captures are retained and the message captures are
readable. No application code, dialog fixture, audio service or global desktop
setting changed for this repair. The original timeout and diagnostic attempts
remain intact.

The installed Linux executable is
`4dc66c1ed916be43411bbbffe7b262ba01c744d0e408019b9e011e160395ed7a`.
The source executable is
`18e78d561f108791ff817aebc702ccf1929dc47346d9d2705848adf167c21e06`;
installation removes its build runtime-search path. The process uses the
explicit matching Qt 6.8.3 runtime and system SDL2. Metadata hashes, XML and
runtime dependencies pass the available checks. Desktop-file validation and
desktop database registration utilities were unavailable. This stage does not
qualify a portable Linux bundle or every desktop environment.

The fresh Linux stage also completes the 120-second CGB input sequence with
normal audio, 22 applied controls, a flushed 8 KiB battery file and a final
active-play capture. After warmup, the application's adjusted monotonic clock
reports 100.0012% nominal guest progress, with widget paint intervals of
17.42 ms at p95 and 19.08 ms maximum. Audio error and post-startup idle counters
remain zero. Process CPU time is 26.89 seconds and peak resident memory is
161.81 MiB. Original inputs, previous samples, and the build and installed Linux
executables remain unchanged.

This WSL host still changes clock rate during the run. Across sampled intervals,
adjusted monotonic time advances by 0.9167 to 1.0003 seconds per raw-clock second.
Complete process durations are 120.93 adjusted seconds and
127.30 raw seconds, including startup and shutdown. These measurements do not
qualify stable physical-time Linux speed. No global clock setting was changed.

## MMM01 debugger and playback replay

This follow-up uses comparison revision
`ea667ad79aa28409f034b1fd6da7304e1844750d` and the preceding
`9b1058cd17695758dd935bc9fa9cbac5aa94bfeb` checkpoint as its baseline. The work
changes debugger bank identity, Turbo host-audio selection and rewind replay.
It does not revise the V11 state encoding or the guest cartridge mapping.

### Debugger bank identity

MMM01 debugger identities now follow the controller's logical bank numbers in
both supported dump layouts. Bank-qualified memory queries translate those
numbers into the original file's storage without changing mapper latches or
rotating input bytes. Unlocked menu banks retain the raw `FFFE` and `FFFF`
identities used by the debugger API. Expressions and the console's explicit
breakpoint banks still narrow to nine bits; unqualified breakpoints can target
the startup menu.

The paired matrix contains 28 generated inputs on DMG and CGB, each in six
controller states. It covers both menu placements, two-bank and 512-bank inputs,
truncated and odd lengths, erased padding, lock and multiplex modes, and the
type-11 detection heuristic. The complete matrix has 102,816 observations:

| Observation | Cases | Baseline differences | Fixed differences |
| --- | ---: | ---: | ---: |
| Qualified reads | 86,016 | 26,928 | 0 |
| Mapped banks, PC and labels | 672 | 494 | 0 |
| Numeric breakpoints | 11,424 | 976 | 0 |
| Symbol values | 2,352 | 0 | 0 |
| Symbol breakpoints | 2,352 | 256 | 0 |
| Total | 102,816 | 28,654 | 0 |

The new public debugger regression fails on the original implementation and
passes after the fix. It also checks two logical bank numbers that alias the
same storage while retaining distinct breakpoint identities. The Qt workflow
covers both models and layouts, inspector labels, wide raw API identities,
console narrowing and wildcard breakpoints. A separate 336-instance comparison
preserves complete ROM hashes, V11 snapshot bytes and battery bytes before and
after the change. Debugger queries themselves leave serialized state unchanged.

### Turbo audio selection

The independently written host-frame helper selects contiguous spans in place
without allocating. Its generated matrix covers 13,673,366 input frames in
12,130 blocks, including 44.1, 48 and 96 kHz, all supported caps, complete frame
sizes from one to 32 bytes, partition boundaries, phase wrap, normal bypass,
queue capacity, host-rate changes, invalid input and guard bytes. Windows MSVC
Release and Linux AddressSanitizer/UndefinedBehaviorSanitizer checks pass.

A separate private probe runs the pinned source's actual playback callback at
fixed rates and volume. Across 3,031,096 input frames in 408 blocks, all 736,938
retained frame identifiers match byte for byte. Its artificial queue is empty
until the selected capacity is exhausted and then reports above the threshold.
This establishes the window arithmetic under that queue model. It does not
establish native buffering, resampling, waveform quality, rate-change behavior
or backend drop-trace equivalence.

The integrated queue checks seed `floor(rate / 8)` pending frames, accept one
more, then reject the first excess frame for 44.1 kHz integer stereo and 48 kHz
float stereo. The software queue's inclusive limit is separate from the native
sink's requested 50 ms buffer. Normal playback retains its existing conversion
and execution-backpressure policy.

### Rewind execution and callback safety

Before implementation, paired probes of the actual history capture/pop API and
CupidGB's restore/remove/replay sequence match all 32 numeric trace rows. They
cover history lengths 0, 1, 2, 3, 5 and 8 under normal and Turbo rewind. Normal
rewind first replays the current frame position and then moves back one frame
per interval. Turbo moves back one frame initially, then two. Empty or
insufficient history executes its terminal instruction and waits for release.

The new regression first fails on the old frontend with displayed tag 16 instead
of 17 and no newly generated replay PCM. The corrected frontend consumes the
proper history, restores current input and runs forward to produce each replay
image and sound. Positive executed cycles drive combined slow/Turbo pacing;
backward movement through guest time never becomes a negative host deadline.

Integrated workflows cover history byte accounting and exhaustion, release and
forward resumption, current input, slow and rapid controls, fractional capped
and uncapped pacing, invalid or wrong-cartridge snapshots, battery preservation,
recording isolation, breakpoint stops and whole-frame host conversion. They
also check focus loss during guarded dispatch and retained controller modes.
Review caught a keyboard-release ordering defect that could leave old-mode
audio queued; the final code retains the previous flags until reconciliation
and applies or defers the reset correctly.

Presentation and history are committed before backend audio calls can dispatch
session changes. The frame path returns without accessing guest state after
those calls. Session, timeline and pacing-generation checks prevent a callback
from charging an old interval to a new deadline or executing a newly loaded
timeline through the previous tick.

### Complete local builds

The initial complete follow-up passes all 47 Windows Release checks in 37.54
seconds and all 45 Linux Release checks in 44.19 seconds. Both include the
expanded Qt workflows and the standalone Turbo window target. Source hashes
remain unchanged through each build and test run.

The Windows executable is
`302d3e6d67986548e026e48fef56022c717f754941bcba0de5ef1ef242bc1a92`.
The Linux build executable is
`3ba45a7da9fde121b620e97a839b1231528ae29dea9c8c7723b70b2d35fda617`.

### Native audio fixture diagnosis

Both fresh installed native checks initially fail the new readiness assertion,
before reaching its injected focus-loss callback. That fixture generates four
intervals, stops producing audio and waits 75 ms, then incorrectly requires the
short stream to remain primed. The failures take 5.06 seconds on Windows and
5.55 seconds on Linux; neither reaches the overall time guard.

Instrumented runs show active playback with no error before the wait. Windows
accepts 2,879 complete 48 kHz float-stereo frames, and Linux accepts 1,654 complete
44.1 kHz integer-stereo frames. After the forced wait, each backend reports an
idle stream and an underrun, as expected when its producer stops. Linux's
processed duration advances from 6.22 to 37.51 ms. These observations identify a
fixture assumption, not a change needed in production audio buffering.

The repaired fixture keeps the 75 ms readiness deadline and stops polling when
native capacity increases. It requires active playback, no backend error, a new
prime, positive processed time and accepted complete frames. A carried Turbo
phase can legitimately discard an entire interval, so bounded warmup and
callback loops advance through that phase and stop immediately when the
callback fires. The original failed stages and both diagnostic runs remain
preserved.

### Final installed workflows

Fresh canonical installations pass the complete native audio workflow in
11.43 seconds on Windows and 19.75 seconds on Linux/Xcb. Both run under the
unchanged 180-second guard and retain all ten UI captures. The repaired
fixture is the only change to the eight frozen playback integration files
since the initial complete local suites; production playback behavior is
unchanged during the fixture repair.

| Platform and mode | Host format | Accepted frames | Writable capacity | Readiness |
| --- | --- | ---: | ---: | ---: |
| Windows Turbo | 48 kHz float stereo | 2,399 | 0 to 3,840 bytes | 2 ms |
| Windows rewind | 48 kHz float stereo | 2,879 | 0 to 3,840 bytes | 8 ms |
| Linux Turbo | 44.1 kHz integer stereo | 1,654 | 0 to 4,408 bytes | 1 ms |
| Linux rewind | 44.1 kHz integer stereo | 2,756 | 0 to 4,412 bytes | 31 ms |

All four observations retain active playback, no backend error, a live device,
a new prime and positive processed time. The later focus-loss, nested-tick,
state-load, phase/queue and cartridge-close assertions all complete. Startup
text and debugger controls remain readable on both platforms.

The final Windows build and installed executable have SHA-256
`6dab6a8c8def8db19037bb87d1c2d32bf8213982d979894e39c55e7b622eb29c`.
All loaded Qt and SDL modules come from its 295-file stage, with SDK Qt absent
from the process search path. The Linux build is
`0b4596870c8e28ecbd4ae72b8d454a7e89255ec97909259cafc97c9e0d3736f2`;
the installed executable is
`2022f7220db3343a4e13be74d73cd1b8bf7f99afde4e94b369b642865535ff6e`.
The 22-file Linux stage uses matching Qt 6.8.3 and system SDL2, has a validated
private icon cache and contains no executable RPATH or RUNPATH. Its existing
audio server passes the protocol readiness gate without a service change.

Source and executable identities remain unchanged throughout the final native
runs. Formatting and whitespace checks pass, all 114 local documentation links
resolve, and all 162 project source/test files remain below 2,000 lines. These
installed workflow checks do not establish audible quality, sustained gameplay
speed, clean-machine deployment or a portable Linux bundle.

Matching history order and selected host-frame identifiers does not qualify
identical rewind waveforms. V11 retains deterministic APU/filter history, and
the host's slow-motion conversion has its own interpolation policy. WAVE
recording still captures forward PCM and excludes replay; controller rumble
remains suppressed during rewind. Printer protocol, image-buffer and job state
are not yet restored with native snapshots. These remain explicit parity gaps.

## Peripheral continuation and desktop lifecycle, 2026-11-15

Checkpoint `40c299b0e92fe6e94c5bd84e3cb9db205ad195ac` adds native V12 printer and
infrared history, shared CGB/HuC1/HuC3
infrared connections, explicit cartridge/symbol reload, and reduced Qt polling
while idle. The comparison revision remains
`ea667ad79aa28409f034b1fd6da7304e1844750d`. Generated probes and commercial replay
use frozen libraries, inputs, and source manifests; their scope is recorded below.

### Regression gates

The final Windows Release build passes all 49 checks; Linux Release passes all
47. Both include the Qt reload, printer, controller, and idle-timer workflows.
The Linux core sanitizer build passes all 28 checks with address/undefined
behavior instrumentation and leak detection. The first full Release runs found
two stale test expectations: a supposedly unsupported version was now V12, and
an MBC7 size assertion omitted six receiver bytes. The repaired checks still
require rejection of the next version and the exact current wire length.

Printer reattachment also exposed a production defect: after loading an earlier
console timeline while the printer was detached, immediate capture rejected its
future clock origin. Attachment now rebases that origin without consuming or
clearing the pending job. The regression fails before the repair and passes
through both ordinary and trusted snapshot APIs afterward.

### Printer continuation

All 16 paired cases and 80 observations pass. The matrix covers buffered DATA,
partially received DATA and reply bytes at one, four, and seven sampled bits,
and an active print job on DMG and CGB. Each engine agrees with its independently
continued control for image bytes, replies, callback order, and remaining work.
Paired callback brackets and tail-cycle counts also agree in every case. The
brackets are 12 base cycles wide; these are instruction/host-operation bounds,
not exact internal callback edges.

Both engines produce 30 image/completion callbacks on the uninterrupted paths
and the same 30 after native restoration. Saving and loading emit none. An
explicit-reset negative control still loses the pending history in all 16 cases.
Four additional origin cases make 320 step comparisons per engine and agree on
fresh-device load and reset clocks. V12 states with a printer occupy 333,301 bytes
for these ordinary cartridges.

The independent core regressions also cover the attachment matrix, malformed
records with recomputed integrity, overlapping storage, older imports, reset,
and retained callbacks. Qt workflows cover disk load, rewind, Backstep, already
emitted paper, and queued work crossing a cartridge replacement. These checks
do not qualify physical printing or commercial printer workflows.

### Mapper and infrared comparisons

The refreshed header/storage matrix has 4,096 rows: 4,066 match, with the same
30 recorded exceptions for ROM+RAM types `08` and `09` and partial MBC6 type `20`.
The exception check retains exact expected values rather than excluding those
types from comparison. All 2,244 MMM01 menu/header rows match. Generated core
tests cover ROM-only, MBC1, multicart MBC1, MBC2, MBC3, extended MBC3, MBC5/rumble,
MMM01, MBC7, HuC1, HuC3, Camera, and TPP1. This is controller coverage, not a claim
that every commercial cartridge or peripheral works.

All 17,664 infrared register/callback observations and 10,240 stable sensor rows
match. The frozen transition matrix retains 550 differences among 8,192 rows.
A subsequent phase audit reproduces those rows and finds no separate charge-rate
or clamping discrepancy: the implementations use different deterministic noise
phases, and the original fixtures also began with different divider/display
phases. Those 550 rows therefore describe that recorded setup, not a controlled
measurement of one isolated defect. Exact transition behavior remains pending;
stable receiver agreement does not establish it.

A controlled follow-up normalizes divider and presentation origins. Resetting
DIV changes detection in 254 of 512 comparison pairs, while CupidGB changes in
none. A VRAM read that synchronizes deferred display work changes detection in
107 of another 512 pairs; CupidGB again changes in none. One pair has identical
CupidGB native bytes despite distinct required outputs. Exact transition parity
therefore needs additional retained phase/history or a broader event mapping;
substituting an existing elapsed, line, or frame counter is insufficient.

### Commercial native-state continuation

Nine traces cover eight commercial cartridges across ROM-only, MBC1, MBC2,
MBC3, MBC5, MBC7, HuC3, and Camera, including a separate active MBC1 gameplay
trace. Each local run warms for 3,000 intervals, saves during mode 3 with 17–19
queued PCM frames, and compares 600 continuation intervals. All 36 runs pass:
nine local save/restore runs per platform and nine cross-platform imports in
each direction.

The aggregate contains 319,566,188 instruction comparisons, 21,608 presentation
and memory comparisons, and 432 complete native-byte comparisons. Every drained
PCM chunk matches. Saved/final states and captured start/end image bytes agree
between hosts. All 341 protected original files retain their bytes and
modification times.

These runs use frozen Windows library
`c60f336d6488c7a19c467eb121b7547bcf4cb346ecb7abcc0432b43888c578d1`
and Linux library
`f2b3bcc5c1842bb38394e7c8d110c4d31fd5609ebef17ec890153d047f4c0520`.
A later frontend rebuild changed the live Linux archive after a documented
header-comment edit. The receipt retains that archive and the exact comment
diff and does not claim byte identity with the frozen library. No continuation
assertion failed. Startup uses the approximate post-boot profile and a private
deterministic clock; PCM is checked before host playback. No printer or cable
is attached during these commercial replay runs.

### External suites and illegal execution

The current frozen Windows runner produces 242 explicit passes in the 243-case
external gate. The remaining OAM timing logger reaches its 3,600-interval limit
with result code 128 and no success verdict. It remains bounded, not passed.

Fresh diagnosis of the seven previously failing library inputs finds matching
terminal opcodes, instruction counts, and final 256 instruction/register records
under controlled startup. Their CPU fetches still expose a separate bug:
CupidGB consumes no time for an invalid opcode and leaves IE enabled. The
comparison consumes one machine cycle and clears IE while peripheral clocks
continue. A 44-case synthetic matrix covers all eleven invalid opcodes, both
CGB speeds, and both IME values; an injected pending interrupt incorrectly wakes
CupidGB in every case. This is a remaining CPU/API/frontend gap. The seven
library outcomes stay errors, and matching terminal paths do not justify labeling
every input a bad dump or claiming successful gameplay.

### Installed native workflows

Fresh Windows and Linux installations pass the complete native Qt workflow with
real audio, using the unchanged 180-second guard. Both verify the expected Qt/SDL
runtime paths and retain ten UI captures. Debugger controls, startup messages,
map overlays, and palette panels remain readable on both hosts. The Windows
stage has 295 files; the Linux stage has 22 and uses Qt 6.8.3 with system SDL2.

The Windows installed executable is
`5d1e243ec621e57714ae38b8a86d35bac4a9479758547eeabdc05d4e2aed9396`.
The Linux build executable is
`d82c8c7880cc2c09760b6e3de7ce9202b8f694b5d44c28891dfe13ffc3727377`;
its installed executable is
`43e700942db1dcf1e721f40d8593bbab4dc0cd188c591ce4e18576fdcd3a14c8`.
These integration checks run while other validation is active. Their duration
is not a performance benchmark, and passing them does not qualify audible
quality, physical refresh, a clean machine, or a portable Linux bundle.

Formatting and whitespace checks pass. All 170 project C/C++ source and header
files remain below 2,000 lines; the largest is the 1,957-line state regression.
No obsolete root `licenses` directory is present. The project license and
required notices for retained dependencies remain intact.

## Illegal-opcode halt and bounded library scan, 2026-11-16

The follow-up corrects the CPU defect recorded above. All eleven illegal primary
bytes now complete their fetch cycle, clear IE, and enter the existing halted
state. Timer, serial, graphics, and sound continue under the normal hardware
gates. OAM DMA remains suspended until wakeup. Pending IF bits alone cannot wake
the CPU with IE clear; a debugger can deliberately restore IE and release it.
Native V12 already represents this state, so no format or legacy-load change is
needed.

Observed stepping reports the actual illegal byte and address. Explicit debugger
commands stop with a distinct diagnostic and retain Backstep. Ordinary playback
continues the halted hardware. The runner ends with `illegal_opcode` and exit 1,
including the final fetch's clocks, presentation event, and PCM. A simultaneous
protocol success or duration limit cannot turn that outcome into a pass.

### Core and paired lockup checks

The targeted Linux Release run passes GB, CPU timing, CPU lockup, native state,
and debugger checks. The new target covers 198 illegal-opcode cases: eleven
bytes, six model/speed profiles, and IME off, on, or enabled by a preceding EI.
It also checks all 1,536 CB-subopcode/profile combinations, blocked bus fetches,
HALT-bug addresses, interrupt preemption, IF-only non-wake, explicit IE release,
plain/observed equivalence, peripheral output, and snapshot continuation.

The first new peripheral fixture incorrectly expected OAM DMA to finish during
HALT. The inspected memory path requires suspension; the corrected fixture
checks unchanged pending DMA/OAM and completion after explicit wakeup. No DMA
production behavior was changed to satisfy that test.

Fresh paired probes use comparison revision
`ea667ad79aa28409f034b1fd6da7304e1844750d` and frozen candidate library
`4f8d9ba46870ec9a867c76249c5ab39238e5cefff648a25e6158e22c20ebfaa9`.
All 66 cases and 396 observations agree across fetch, idle, an IF request,
further idle, explicit IE release, and the following instruction. They cover
all eleven bytes, both CGB speeds, and all three IME conditions. Separate
event, cycle, PC, IE, and IME expectations also pass. The retained pre-change
negative control differs in all 396 observations, including every initial
fetch. Protected inputs and the 55 frozen core source/header files remain
unchanged.

The seven affected library inputs still reach illegal instructions in both
engines. Their final 256 instruction/register records, total instruction counts,
and total base clocks now agree in every pair. Six cases gain the missing two
base cycles of a double-speed fetch; one gains four. These comparisons use the
same color boot binary, released buttons, no battery load, and a controlled
startup state. Matching the terminal path and halt behavior does not establish
successful gameplay or justify classifying every input as a bad dump.

### Completed pre-fix library scan

The retained runner with SHA-256
`865b517e19d9a168c2540b1f0b45f8cce836ae10fa1a42b4d4b8c4e18dd072ec`
completes the 880-input scan: 873 reach the 3,600-interval guest-time bound and
seven report the original core error. This executable was frozen before the CPU
repair; these totals do not describe a rescan of the new executable.

The inventory contains 878 distinct ROM hashes. Model selection is 198 DMG,
556 CGB, and 126 SGB runs, using the corresponding recorded boot inputs. The
mapper inventory is 500 MBC5, 272 MBC1, 81 MBC3, 12 ROM-only, 11 MBC2, and one
each for MBC7, HuC3, Camera, and an unknown header type. All seven errors occur
among the MBC5 inputs. No HuC1, MMM01, TPP1, or MBC6 commercial input is present.
Generated controller probes supply separate coverage where applicable.

Every protected original retains its bytes and modification time; the frozen
runner is unchanged. A bounded result records execution through the chosen
guest-clock limit. It does not establish active play, correct pixels or sound,
successful persistence, every mapper feature, or sustained native performance.

### Final builds, CLI outcomes, and native workflows

The final Release builds pass all 50 Windows checks and all 48 Linux checks,
including the expanded Qt workflow and required archive tests. The Linux
address/undefined-behavior sanitizer build passes all 29 checks with leak
detection. The new CPU lockup target and the complete runner regression run in
each configuration. The 194 recorded build/test inputs remain unchanged through
these final gates and native validation.

The first Windows suite passed 49 of 50 checks. Its new debugger fixture invoked
bare `reset`, which is not implemented by the current console. The diagnostic
run located the failed post-reset assertion. Changing that fixture to the
existing `reset reload` command made the focused check and subsequent full
suite pass. Bare and quick-reset console commands remain explicit checklist
gaps; no production reset behavior was changed to satisfy this fixture.

The actual runner also rechecks the seven library failures on both hosts. All
fourteen executions return `illegal_opcode` with exit 1, the expected fetched
byte/address, and one additional completed step. Six gain two base cycles and
one gains four. All seven parsed result records agree across platforms except
host elapsed time, including audio and image checksums. Their other output
counters remain equal to the retained pre-fix scan. These checks use frozen
Windows runner
`8abe3f9a7633ee93ed6bccb53a188cee24c4bc4752056b8e9a05b8ec72f98e4b`
and Linux runner
`db6c70b52d16914e44fd985baefa4c8513994984c63e54b9924c7d64ad389cab`.
The private guard records the exact one-line Qt fixture correction during the
Windows run; the runner/core inputs, binaries, ROMs, boot input, and saves remain
unchanged. No successful case was repeated to replace its evidence.

The new Windows runner also repeats the 243-case external gate: 242 explicit
passes and the same bounded OAM timing logger. The passing groups contain 119
model-qualified acceptance cases, 56 mapper cases, 24 audio cases, 22 CPU cases,
14 timing cases, and seven OAM cases. The remaining logger reports code 128
after 3,600 intervals without a success verdict. It remains inconclusive; the
runner hash is unchanged throughout the gate.

Fresh installations pass the complete native Qt workflow with real audio on
Windows and Linux/Xcb. Both verify the expected runtime paths and retain ten
captures. The debugger, map overlay, startup message, and palette views remain
readable. Native save/load, rewind, and reentrant audio-write cartridge close
checks also pass. These runs establish the stated integration scope under
concurrent validation load, without a performance or audible-quality claim.

The Windows build and installed executable hash is
`6addb20d9aaff6ddb892b26f68eea1233b268ec206bce040ec06c48848c0eb30`.
The Linux build hash is
`b65f7b2cd9f79381323ccbab419cfc3edd2096426ddfa8985e4f8db6d483843f`;
the installed ELF hash is
`dc3b9867ddee0840bab8e47cdb063fed9e2f81535b82323c73f64983918dca47`.
The installation receipt retains the normal Linux RPATH change and runtime
dependencies. All 171 project C/C++ source and header files remain below 2,000
lines, with formatting and whitespace checks passing.

## Native pacing and idle measurements, 2026-11-16

The CPU implementation is committed as
`07fb75b7df5b303899e4b11df2360c9a2ffb469c`. Its 194 frozen build/test inputs
match the committed Git blobs. All six hosted Windows, Linux, and sanitizer
checks pass on that exact head in workflow runs `43780966953` and `43780963140`.

### Idle work

The idle-timer change delivered in `40c299b` was measured separately with frozen
frontend sources, identical core libraries within each platform, identical
instrumentation, and a generated DMG loop cartridge. Eight sequential runs
followed baseline/candidate/candidate/baseline order on Windows and Ubuntu under
WSLg/Xcb. All 48 phases completed. Each phase measured five Qt elapsed seconds
after settling. Mean main-timer deliveries per Qt second were:

| Idle state | Windows before | Windows after | Linux before | Linux after |
| --- | ---: | ---: | ---: | ---: |
| Paused | 929.7 | 62.6 | 909.8 | 62.6 |
| Menu open | 932.2 | 62.6 | 917.4 | 62.6 |
| Rewind exhausted | 924.5 | 62.4 | 899.5 | 62.4 |
| No cartridge | 930.5 | 62.6 | 902.3 | 62.6 |

Main-timer deliveries fell by about 93%. The independent controller timer
continued near 125 deliveries per second. All 32 idle phases advanced no guest
cycles or generated audio frames; all 16 active/recovery phases advanced both.
Both variants retained the 1 ms active timer, and no measured phase added an
audio error or idle event. Linux process CPU time averaged 44.8–50.3 ms per idle
phase before the change and 19.6–28.6 ms afterward. Sources, executables, and
deployment files retained their recorded hashes.

This comparison used no physical controller. Application timer and dispatcher
events do not measure physical wakeups or energy use. Windows thread-cycle
readings remain raw observations without a CPU-utilization conversion. The
private idle receipt has SHA-256
`e2754b39dad6af6ac102e864af10d4c0d6c8ed3c1bc14011db01df086481c5b5`.

### Bounded commercial sessions

The installed CPU-checkpoint applications ran two local commercial cartridges
on each platform with explicit post-boot startup, normal audio, scripted inputs,
and rewind recording. Each of the four sessions completed its 120-second
application-time limit with exit 0. Statistics below exclude the first five
seconds, leaving approximately 115 application seconds per session.

| Session | Guest speed against app clock | Paint p95 | Paint p99 | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| Windows DMG | 100.0023% | 17.68 ms | 18.24 ms | 103.5 MiB |
| Windows CGB | 100.0047% | 17.72 ms | 18.41 ms | 143.9 MiB |
| Linux/Xcb DMG | 100.0021% | 17.68 ms | 18.08 ms | 126.4 MiB |
| Linux/Xcb CGB | 100.0042% | 17.52 ms | 18.24 ms | 159.0 MiB |

Guest speed divides measured base-speed cycles by application elapsed time and
the selected model clock. Paint intervals count Qt widget paint events
separately from guest execution; they do not measure physical display refresh.
The largest post-warmup paint interval was 25.64 ms. Peak RSS includes the
interface, audio, and accumulating rewind history. Retained rewind data reached
about 61.0 MiB for DMG and 97.8 MiB for CGB.

All four sessions recorded zero audio errors and zero audio-idle events after
startup, with active device processing and no illegal-opcode diagnostic. The
traces applied 14 input events for DMG and 22 for CGB. Both final DMG captures
show Game Over; both CGB captures show an active course. CGB sessions flushed
fresh private 8 KiB battery files, without a battery reload round trip in this
measurement. Original ROM, trace, existing-save bytes and modification times,
and installed executable hashes remained unchanged.

Linux guest percentages and paint intervals use adjusted monotonic time. The
harness also retained `CLOCK_MONOTONIC_RAW`: whole-process durations were
122.496 seconds for DMG and 121.988 seconds for CGB, including startup and
shutdown. Sampled adjusted-to-RAW interval ratios ranged from 0.917 to 1.023.
Those process durations do not share the application's measurement window and
cannot replace it in a guest-speed calculation. Both clocks are retained for
the idle comparison as well; its callback table uses Qt elapsed time.

The combined private receipt binds each result, image, trace, source manifest,
and installation manifest to the recorded binaries. Coverage is limited to
these snapshots, cartridges, traces, and one Windows/WSLg host. Audible quality,
physical refresh, longer active play, and wider cartridge coverage remain
unmeasured by this set. The idle result establishes reduced idle work; it does
not establish an active-game throughput gain or universal full speed.

## Cold memory and firmware startup, 2026-11-16

The [startup validation record](validation-startup.md) covers model-specific cold memory, seed controls, SGB handoff and older-state recovery, mapped register comparisons, executed firmware, and the final Windows/Linux regression gates. It retains the raw display-phase and PCM differences. The later [clock validation record](validation-rtc.md) qualifies the four-dot display offset as a coordinate difference for the tested normal-speed CGB startup edges; PCM equality remains open.

## Cartridge clocks and divider continuation, 2026-11-16

The [cartridge-clock validation record](validation-rtc.md) covers host and emulated clock policies, controller-specific battery behavior, bounded raw-counter arithmetic, logical clock delivery, the STOP/HBlank DMA divider repair, and native version 13 continuation. It distinguishes frozen comparisons, negative controls, current integration results and remaining qualification.

## Initial display sizing, 2026-11-16

The [display validation record](validation-display.md) covers the initial-window status-bar correction, an old-constructor negative control, focused Windows/Linux frontend suites, installed native UI/audio checks, and before/after viewport captures. Earlier timed gameplay samples retain their original executables and render sizes.

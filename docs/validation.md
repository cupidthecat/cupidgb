# Validation record

This record covers the Qt desktop and GB/GBC timing milestone on 2026-11-04. It does not establish complete hardware or frontend parity. The comparison source revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`.

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

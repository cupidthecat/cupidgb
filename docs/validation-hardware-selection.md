# Hardware selection integration

This 2026-11-17 checkpoint integrates CH3 wave-memory and serial-clock-selection
corrections. It records completed hardware comparisons, native-state interchange,
external suites, native desktop workflows and four bounded Windows gameplay
runs. A controlled Linux clock experiment completed with verified service
restoration. A subsequent clock-readiness attempt timed out before gameplay
could launch, so sustained Linux playback remains unqualified.

The behavioral comparison revision is
`ea667ad79aa28409f034b1fd6da7304e1844750d`.
Results apply to the stated models, fixtures and artifact identities. They do
not establish complete hardware parity or compatibility with every cartridge.

This page retains the earlier checkpoint's results. The later
[storage, wave and camera integration](validation-storage-wave.md) records
native version 20, SRAM through 2 MiB and the fresh validation for that build.

## Build gates and evidence continuity

| Gate | Windows Release | Linux Release | Linux ASan/UBSan |
| --- | --- | --- | --- |
| `hardware-selection-01` | 78/78 full-suite checks | 76/76 full-suite checks | 56/56 core checks |
| `hardware-selection-02` | 78/78 full-suite checks | 76/76 full-suite checks | 56/56 core checks |
| `hardware-selection-03` | 2/2 fresh targeted checks | 2/2 fresh targeted checks | Earlier 56/56 evidence reused; no fresh run |

The first two gates completed their full configured suites with one build job.
Gate 02 changes the visible motion-input fixture to wait for native activation
after cartridge and audio setup. It preserves the existing input guards.

An offline continuity audit compares Gate 01 and Gate 02 by size and SHA-256.
Both core libraries, both ROM runners and all 29 public headers are identical.
That supports reuse of the wave, serial, state-exchange and external-corpus
results against those artifacts. It does not represent another execution of
those comparisons. The Qt executables changed and require their own native
workflow evidence.

Gate 03 changes only diagnostic output in the reset-audio fixture. It reruns
`cupidgb_qt_workflows` and `cupidgb_source_headers` on Windows and Linux.
The registered Qt check uses the offscreen platform. The four core/runner
artifacts and all 29 public headers remain identical, so the bound 78/76/56
full-suite results remain applicable to the unchanged core. Gate 03 does not
claim a fresh full-suite or sanitizer run.

This checkpoint's build inventory contains 268 inputs, with manifest SHA-256
`ffd9e13ae235c9e14272c3ba9950aa25a99f8310e956e849c971be833bb1047c`.
Its before/after guards pass. Repository documentation is outside that build
inventory.

## Wave-memory comparison

The integrated probe completes 25,600 generated cases and 102,400 observations
with zero differing rows, differing cases or invalid fixtures. It covers DMG,
Pocket, CGB, SGB and SGB2, with 3,840 component cases and 1,280 executed guest
cases per model. Complete wave-memory observations include the source bytes
and destination bytes, both nibble phases, all sixteen offsets, timing
boundaries and inactive controls.

The retained negative control contains 7,680 original cases. The correction
resolves all 324 failing cases and their 648 differing observations. The other
7,356 cases retain their observations, and recorded non-wave fields stay
unchanged. The original baseline remains preserved.

Permanent regressions also cover safe instruction-boundary capture and
continuation through ordinary, trusted-context, live and live-context state
loads. The wave correction adds no serialized field. Its model controls,
timing window and 7,808 regression checks are described in
[wave-memory validation](validation-wave-retrigger.md).

These comparisons qualify the generated memory-mutation behavior. SGB and
SGB2 fixtures exercise handheld APU behavior at their selected base clocks;
host-console startup and transport are outside the fixture. The results do
not establish physical-unit behavior, complete audio accuracy or gameplay
speed.

## Serial selection and version 18

The serial correction retains the divider tap selected by an SC write.
Changing KEY0 or removing the boot overlay does not silently select a new tap.
CGB register readback also retains the stored speed bit in monochrome
compatibility mode.

The final paired probe reuses all nine original guest programs and their
observer. Its 18 executions verify 19,948 instructions, 144 guest reads and
their 144 corresponding stores. All 72 paired guest values agree, and all
nine scenario pairs agree at the compared clock-phase checkpoints. The
retained baseline still records its five differing guest values.

The paired probe covers the current CGB model at normal CPU speed.
Its observations occur at completed instructions, not at an independently
measured subinstruction bus edge. External-clock peers, physical link devices,
other silicon revisions and gameplay are outside this probe. Permanent
regressions separately exercise selector changes, reset, speed switches and
state migration. See [serial-selection validation](validation-serial-selection.md).

Native version 18 stores the selector independently. Versions 2 through 17
derive the missing selection from their saved mode and raw SC bit while
preserving their saved phase. An older file cannot recover an unrecorded
sequence of SC and KEY0 writes.

A separate file-exchange check captures two generated CGB states on each host:
reset with the slow selector despite SC readback, and an active fast transfer
retained after entering compatibility mode. Real files continue through
Windows-to-Windows, Windows-to-Linux, Linux-to-Windows and Linux-to-Linux paths.
Each receiver starts with the opposite selector before loading.

Four captures and eight restores produce 70 exact comparisons with no
mismatches. Each continuation matches an uninterrupted control for 512
instructions, including the final encoded state, executed guest observations,
serial history and nonzero 44,100 Hz PCM. The exchange uses the ordinary native
load API; the permanent regression suite covers all four load APIs. LCD output
is disabled in this synthetic fixture, so matching framebuffer bytes do not
qualify rendering.

## External corpus

The final bounded corpus contains 260 cases. Every compared guest field
matches the earlier storage/clock checkpoint, including verdicts, guest clocks,
graphics and PCM checksums, audio counts, serial observations and memory
results. Host durations, path spelling and raw stdout are excluded from that
comparison.

| Group | Pass | Fail | Bounded | Total |
| --- | ---: | ---: | ---: | ---: |
| Acceptance | 125 | 11 | 0 | 136 |
| Audio | 24 | 0 | 0 | 24 |
| CPU | 22 | 0 | 0 | 22 |
| Mapper | 56 | 0 | 0 | 56 |
| OAM | 7 | 0 | 1 | 8 |
| Timing | 14 | 0 | 0 | 14 |
| Total | 248 | 11 | 1 | 260 |

The eleven failures remain startup-divider, startup-I/O or serial-alignment
cases on the tested DMG, Pocket, SGB, SGB2 and CGB profiles. One DMG OAM timing
case remains bounded. Three DMG0 startup cases remain excluded under the
existing corpus policy. None of these results is counted as a pass.

All 161 protected inputs retain their hashes and modification times. The
journal contains exactly 260 records and agrees with the complete result.
The core identity audit carries this evidence through the later frontend-only
changes; it does not convert the unresolved cases into successes or provide
performance evidence.

## Mapper coverage and limits

All eleven working controller families have explicit generated DMG and CGB
regressions. The dedicated external mapper corpus covers three of those
families:

| Controller family | Generated coverage on DMG and CGB | Dedicated external cases |
| --- | --- | ---: |
| ROM and direct RAM | Storage rules, aliases and load lifetime | None |
| MBC1 and multicart | Bank wiring, RAM modes and small-RAM aliases | 26 |
| MBC2 | Address decoding and internal RAM | 14 |
| MBC3 and extended wiring | Banking, aliases and clock snapshots | None |
| MBC5 and motor hardware | ROM/RAM selection, enable decoding and motor bits | 16 |
| MMM01 | Menu layouts, lock transitions and bank identities | None |
| MBC7 | EEPROM, motion latches, battery files and continuation | None |
| HuC1 | Banking and infrared ports | None |
| HuC3 | Banking, commands, clock and battery data | None |
| Camera | Bank control and generated sensor captures | None |
| TPP1 | Extended ROM, RAM, clock, battery and graded motor control | None |

ROM+RAM headers retain an explicit comparison exception. Partial MBC6 support
has generic storage, alias and state coverage, but no dedicated split-window
or flash qualification; it is outside the working-mapper comparison scope.
TAMA5 has no implemented controller. An accepted input or fallback mapping
does not establish support for its hardware.

The earlier [storage checkpoint](validation-cartridge-storage.md) records
1,048,576 matching generated bank reads across DMG and CGB and four populations
through 16 MiB. The configured 1 GiB image ceiling is broader than that paired
qualification. At this checkpoint, cartridge RAM, including TPP1 SRAM, was
limited to 128 KiB; larger declarations were rejected before replacing the
active cartridge. The later integration extends that storage through 2 MiB.

Physical MBC7 input, motor feedback, infrared transport, host camera input and
physical sensor behavior remain unqualified. HuC3 alarm delivery, physical
qualification of its Accurate minute policy, and flash persistence also remain
open. The generated cases and 56 external passes do not cover every mapper
combination, undocumented board or commercial game. Current details remain in
[hardware support](hardware.md), [cartridge clocks](rtc.md),
[battery saves](battery-saves.md) and the [parity checklist](parity.md).

## Native desktop workflows

The Gate 02 Windows native workflow passed with real audio and 16 captures.
Its Linux run failed with four captures. The Linux log records a successful
normal-reset audio proof, then an idle/underrun state before the next manually
fed proof begins. The fixture was emitting diagnostic JSON between proofs
while the guest timer was stopped.

Gate 03 defers that diagnostic output until the consecutive proofs finish.
It preserves the playback assertions and observations. The subsequent native
runs pass on both exact Gate 03 Qt executables:

| Host | Audio requested | Exit and success marker | Captures |
| --- | --- | --- | ---: |
| Windows | Yes | Exit 0; marker present | 16 |
| Linux under WSLg | Yes | Exit 0; marker present | 16 |

Neither run forces the offscreen Qt platform. Executable hashes remain
unchanged during each run. The Windows check takes about 23.02 seconds and
the Linux check about 19.43 seconds; these are workflow durations, not
sustained-playback measurements.

| Native executable | SHA-256 |
| --- | --- |
| Windows Gate 03 | `79d2a6f1556841c392570815236bbe2bbe22f806c62a09cbebc338a827e64867` |
| Linux Gate 03 | `dc479f305ab92cc2790b22448ee580754ebda84197a36a54d2afe22fc4f150f1` |

These results establish the exercised native workflows and device readiness.
Audible quality, physical device-disconnect behavior, sustained pacing and
complete interface parity require separate evidence.

## Reset, recording and audio consumption

The native reset evidence contains five playback-consumption checks per host.
An independent review recomputed all ten intervals. Host frames written,
application queue bytes, guest cycles, generated frames and recorded frames
remain unchanged while writable native capacity increases by at least one
complete host frame. Both endpoints remain active with no audio error, and
their prime-event counts are unchanged. The guest timer is stopped during
these observations.

The transition from the normal-reset proof to the quick-reset proof retains
the same prime-event count on both hosts; the quick proof needs no new priming
interval. This covers the point where the earlier Linux fixture stopped.
Normal and quick reset release the old sink and replace its queue while
preserving the stated reset and history contracts.

Recording stays active at 48,000 Hz, with its immediate frame count unchanged
across each reset. Rewind crosses the quick-reset boundary without adding
recorded frames, and forward playback resumes recording progress. The retained
WAVE files contain 19,988 frames on Windows and 19,184 on Linux, with byte sizes
equal to the 44-byte header plus four bytes per stereo frame.

These checks establish the exercised reset, queue, recording and continuation
contracts. Their readiness durations use the test's Qt clock; they do not
qualify sustained host rate or audible quality.

## Current Windows runtime samples

All four current Windows gameplay samples complete 120 seconds with normal
audio, fixed seed zero, approximate post-boot startup and rewind capture.
Guest speeds below exclude a five-second warmup. Every final native window was
inspected and shows active gameplay.

| Windows workload | Guest speed after warmup | Paint interval p95 | Peak resident memory | Inputs applied |
| --- | ---: | ---: | ---: | ---: |
| DMG ROM-only | 99.9993% | 18.6051 ms | 111.9 MiB | 173/173 |
| CGB MBC5 | 99.9996% | 18.8246 ms | 214.2 MiB | 153/153 |
| SGB MBC1 | 99.9996% | 18.4186 ms | 218.2 MiB | 111/111 |
| CGB MBC5 motor hardware | 99.9964% | 19.2923 ms | 254.5 MiB | 101/101 |

All four receipts report zero audio errors and zero idle events after startup,
successful exits, saved handheld and native-window captures, successful rewind
capture and a completed shutdown battery-flush check. Every requested input
is applied. Cartridge, trace and executable identities remain unchanged.

The completed native/workload queue also retains all 268 build inputs and all
six qualified executables/core archives without changes. Both platforms'
debugger and audio-settings captures were inspected and are legible, with
clipping limited to the intended scrolling viewport.

Guest speed measures emulated base-clock progress. Paint intervals describe
Qt widget events and do not establish physical display presentation. A battery
flush does not by itself demonstrate a later reload of saved game progress.
These four bounded samples do not establish every-game performance, all mapper
behavior, long-term resource stability or audible quality.

## Linux runtime qualification

| Work | Current status | Evidence still required |
| --- | --- | --- |
| Sustained Linux gameplay | Unqualified; the current readiness gate timed out before launching a game | Stable host-clock and audio-rate measurements followed by equivalent normal-audio runtime checks |

With the time service restored to its normal configuration, a new readiness
attempt waited 300 raw seconds for four consecutive ten-second samples within
0.1% of raw time. It collected 29 complete samples. Three qualified individually,
but none were consecutive. Monotonic/raw ratios ranged from 0.992465463 to
1.009633276; PulseAudio/raw ratios followed the same variation. All sample
brackets were valid and the boot identity was unchanged. The four requested
gameplay cases were recorded as skipped; no gameplay helper or Qt process ran.

The private observer wrote its failed readiness receipt, then exited with code
134 because a daemon thread still held buffered stdin during interpreter
shutdown. Its replacement uses a stoppable raw-descriptor watcher and joins it
before exit. Six isolated Linux process-lifecycle checks pass, including
shutdown with the stdin pipe still open. That repair does not change the
recorded clock failure, and the full readiness attempt was not repeated.
The failed receipts and exact original helper files remain preserved.

A controlled clock experiment completed with two active-service baseline
samples, four samples during a temporary stop, and two samples after restoring
the service. The baseline was already stable, and all four stopped samples
remained within 0.1% of raw time. The restored samples ran about 0.50% and 0.13%
fast against raw time. The service returned to active/running with no queued
job, and its enable state, dependencies, configuration, boot identity and
protected inputs remained unchanged.

This result does not show that stopping the service corrected an unstable
baseline or identify the writer of the earlier coarse-tick changes. It also
does not measure audio playback. The [host-clock record](validation-host-clock.md)
contains the observations and restoration evidence.

The earlier [host-clock record](validation-host-clock.md) retains the failed
Linux sustained run and the independent audio-rate discrepancy. Passing the
new short native workflow does not resolve that rate mismatch. The process
responsible for historical coarse-tick changes remains unproven.

No uniform performance-improvement or every-game claim is made at this
checkpoint. Sustained Linux qualification requires completed receipts tied
to the current source and binary identities.

## Evidence identities

| Record | SHA-256 |
| --- | --- |
| Full integration Gate 01 | `9ee730531db425e8dca4ea1dd487474907bfd0abde6a93023754e02bd83e6faa` |
| Full integration Gate 02 | `3e9f6fc464e4206f56640bbb635baffe4e371f576a5489fc8dbc178c5413f303` |
| Targeted frontend Gate 03 | `c66bc03f96fb50697e278987854f330a37f739377196d1becbcbf8b26956d312` |
| Gate 01 to Gate 02 core continuity | `21a7e4bbd2f0571fac078d82ff01410e6585c1b1df6e5e4f5760230729b16986` |
| Wave comparison | `64077f9b077171a6a830b12576f09724d59de043352aba7efe07313cbaec3823` |
| Serial comparison | `38f78848664103584e650e367683194e14d76fb30ca3eceb4b64c2bd2d422688` |
| Version 18 file interchange | `594297e05209f5888e99c1e04772b2c6e6e140ac365a491b2472060ff3b07449` |
| External corpus and comparison receipt | `e9e87ecd5ffe7d637455fa468ff3bf4b1cf05b2a54746d0153ea60fd018de892` |
| Native workflows and four Windows gameplay runs | `e7eab0bef907f3a5e9637f7b00ae3536156bfb55d431a95bbb6d490193eb96c3` |
| Controlled Linux clock experiment | `0a69962bfe438fbe6a23893ad34fe201eb16880daaa5b22d1f2b64363355974c` |
| Normal-configuration Linux readiness attempt | `eb75a53ae1ce29359904089a6a2131fade369597981871af97b109417548bc4d` |
| Linux readiness observations | `f8e0b8ca95a63b314961deff6dbcfab0b3800fabc7ae65b3116fcb0b91c2ea2f` |
| Isolated observer lifecycle checks | `8de7782ea6b9775a4792c4e9c6d7e17de93577bbc29512735da711effa14c857` |

Full logs, generated input identities, native receipts and captured content
remain with the private evidence. ROMs, boot binaries, saves and private input
paths are not part of this document.

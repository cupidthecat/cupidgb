# SGB startup validation

CupidGB now keeps physical SGB/SGB2 startup time separate from guest execution,
preserves startup audio in native state version 21, and uses presentation time
for Qt playback and pacing. This record identifies the verified behavior and
the checks that remain open. Implementation details are in [SGB startup](sgb-startup.md).

The checkpoint has passed its complete Windows, Linux and sanitizer gates,
paired startup timing checks, four Windows commercial sessions and a separate
synthetic startup pacing run. Cross-platform state exchange and normal native
Linux audio qualification remain open. The results below do not establish
complete hardware, frontend or all-game parity.

## Source and build identity

The tested inventory contains 301 build inputs. Its SHA-256 is
`502f700fe3cbecbba0e33d425b34f24888bdb299d86ff10780a4da62b9e4d134`.
The source inventories before and after the gate match. Runtime checks bind
their inputs and executables to that inventory; documentation changes are not
part of the build-input digest.

| Evidence | SHA-256 |
| --- | --- |
| Complete gate receipt | `cac64ceeebbac882d5bcf2eed70a12fedff055d0d17bcc8ca94ad231a8697632` |
| Final source freeze | `c578784bdb15015f7baf25eccbd20066ff93dc071428f86bd214eacfc8a6ea86` |
| Windows Qt executable | `3f545f09a65f208eaa7525453f97788d48707e4e064dd1ca231831f6fa05b2f1` |
| Linux Qt executable | `d7393f4296367914a195b8b8ec8001e75e10347decc81fda0e9c619c2ba70e70` |

Private inputs and runtime evidence stay outside the repository. These digests
identify the retained records without publishing games, firmware, saves or
screenshots. The earlier [storage, wave audio and camera checkpoint](validation-storage-wave.md)
retains its own source identity, results and limitations.

## Complete build and test gate

Gate `sgb-startup-02` completed all registered tests without skips. Both Release
frontends use Qt 6.8.3. The Linux sanitizer configuration is Debug with Qt disabled.

| Configuration | Passed | CTest elapsed time |
| --- | ---: | ---: |
| Windows Release with Qt | 92/92 | 101.83 s |
| Linux Release with Qt | 90/90 | 137.60 s |
| Linux Debug with ASan/UBSan | 65/65 | 192.17 s |

All five gate stages exited successfully, including the two source freezes.
The receipt records no timeout, cancellation, unresolved process cleanup or
source change. Test elapsed time does not measure emulation speed.

The three Qt workflow groups retain separate 30-second deadlines:

| Workflow | Windows | Linux |
| --- | ---: | ---: |
| Desktop | 14.43 s | 11.98 s |
| Storage | 1.68 s | 1.98 s |
| SGB startup | 1.43 s | 1.79 s |

These gate workflows use the offscreen backend. The SGB group covers both
physical models and both cartridge header classes, the complete held interval,
guest resumption, redraws, cropping, pause/menu behavior, WAVE recording,
reset/load rebasing, explicit held-state rewind and debugger stepping. Its
192 kHz collector checks retain sample ordering and capacity assertions.
Native device playback requires the separate observations below.

Core regressions cover the hold/release boundary, reset and presentation order,
rendering without mutation, per-model audio counts, partial drains, queue limits,
sample-rate rejection and exact/live state ownership. The gate also retains the
existing CPU, bus, mapper, timer, DMA, graphics, audio, input, accessory, debugger
and storage tests. Test counts are not counts of games or hardware revisions.

## Startup timing comparison

Thirty observations formed 15 pairs. Twelve adapter pairs cover SGB/SGB2,
ordinary/enhanced headers, and cold, hard and quick reset paths. Three DMG pairs
provide controls for those reset paths.

Every adapter pair recorded 65,296 held calls and 106 presentations before the
first guest instruction on call 65,297. The host-only interval was 14,887,488
half-cycles, equivalent to 7,443,744 base cycles. Guest CPU, timer, APU, DMA,
serial, input and cartridge clocks remained unchanged during the hold and
resumed afterward. DMG controls had no hold and executed their first instruction
on call 1.

The comparison completed with unchanged inputs, no errors and all 31 owned
children waited. Its receipt is
`cfe5d83cde8d777f32ab829fbaec929ada388fe19d0f89fea1752959d3a84921`.
The inputs use original synthetic firmware and cartridges. This qualifies the
listed timing and reset behavior, without establishing commercial firmware
compatibility, artwork identity or acoustic fidelity.

## Portable state exchange

The version 21 exchange attempt completed 16 captures and 32 resume processes
across Windows and Linux. It compared both models, both header classes and two
partial-output positions through all four exact/live and ordinary/trusted load
APIs. Independent no-load continuations provide its controls.

The attempt failed 32 of 1,024 byte comparisons. All 32 differences are in final
state files for the API route using one-frame drains, including same-host
routes. The other 992 file comparisons matched. Byte inspection localized every
payload difference to consumed ordinary PCM slots; the queue counts were zero.
The format preserves that complete array, and the fixture's control used a
different drain capacity. Replaying the array compaction on captured bytes
accounts for every difference. Immediate states, guest/timing traces, emitted
PCM, origin streams and summaries matched for all 32 pairs.

A corrected exchange is being prepared with independent no-load controls for
each drain history. It will retain full byte comparisons and separate
cross-capacity output checks. The original failed verdict remains unchanged;
neither this diagnosis nor local codec tests constitute a completed exchange.

Inputs remained unchanged and process cleanup completed. The failed receipt is
`303daa0bceaced9f1e0734de2f4313240197bfb60c9e6a4c9e489c3b13226935`.

## Native Qt and audio

The first native attempt completed all four Windows model/header cases. Its
held-output observation used a 48 kHz stereo float sink. Across three generated
intervals and a 225 ms observation, the no-write interval returned 3,840 bytes
of capacity after 2,411 host frames had been written. Guest and recorded-handheld
progress stayed zero. Four reviewed captures showed the startup, unchanged
redraw, cropped viewport and completed display with intact Qt controls.

Linux passed its 3.68-second endpoint readiness check, then failed the
post-event-loop stream guard after 2.5477 seconds. The retained log does not
identify which joined condition failed: stream ownership, re-priming, state
or backend error. All owned children were waited and cleanup completed. The
paired attempt remains failed, with receipt
`9d612dd4a993c2fe168dda30fd96b0629d0ce87fc383f0a208d1a54c29fa4f9a`.

A later logged diagnostic passed on both hosts. Linux consumed startup output
at 44.1 kHz stereo Int16: 4,412 bytes of returned capacity, 2,205 written host
frames, four generated intervals and 67 ms, with guest/recorded progress still
zero. Its four captures showed the expected layout. Both logging and Linux
boot/audio-server identity changed between attempts, so this does not isolate
the original cause or qualify normal Linux native operation. The diagnostic
receipt is `ddc6e3193fb39d9b733e3cf5454f1ecc93d1f180e0cfe8734542005fb725b974`.

## External emulation corpus

The fresh 260-case run retained 248 passes, 11 failures and one bounded case,
plus the three existing exclusions. All guest verdicts, guest cycles and VBlank
counts matched the earlier checkpoint. All compared fields matched in 252 cases.

Eight SGB/SGB2 cases added 65,296 calls and 106 presentations. Their audio
counts/hashes and image hashes changed while their guest results stayed the
same. They ended at presentation 145, startup phase 135, with 104,400 startup
PCM frames on SGB or 107,010 on SGB2. Four of these cases retain earlier firmware
expectation failures; four retain passing guest verdicts.

The original comparator returned 1 and reported a nonidentical result. That
failure remains in the record. Separate per-case analysis connects the changed
output to the new startup interval; it does not waive any compared field or
qualify the artwork and sound. The receipt is
`16ef83d4521326538a0767523d735031e621d0acbd0449483688db3b0f8dc8e2`,
and the comparison identity is
`1e0e46d936b11ace2eff417ceba84cd64f2645e5309e74f7d4f96d460d5e6c36`.

## Windows gameplay and pacing

Four post-boot commercial sessions completed 120 seconds each with normal
native audio. Five seconds of warmup left 114.9993–115.0107 measured seconds.
Guest and presentation progress matched; startup-origin PCM stayed zero.

| Cartridge profile | Measured guest/presentation speed | Host paint FPS |
| --- | ---: | ---: |
| DMG, ROM | 100.0008% | 59.7569 |
| CGB, MBC5 with battery RAM | 100.0001% | 59.7915 |
| SGB, MBC1 | 100.0023% | 61.2117 |
| CGB, MBC5 rumble | 100.0084% | 59.7390 |

All retained checks passed, including input, battery flush, rewind capture and
zero measured audio idle/error events. Whole-run speed was 99.8508–99.8685%,
including host initialization. Peak resident memory ranged from 109.81 to
252.61 MiB. Four reviewed captures show active gameplay, including the SGB
border. These sessions do not establish game completion, acoustic quality,
physical rumble or a battery save/reload round trip. The parent receipt is
`3b283f7254f5f778fff67d7a3e0d778c6dc0048a15cbd56501663cd3f84df3a9`.

A separate 12.0101-second SGB2 run executed original synthetic firmware. It
produced exactly 7,443,744 additional presentation cycles and 154,980 startup
PCM frames. After five seconds of warmup, the 7.0100687-second window measured
equal guest/presentation speed of 100.0731%, zero startup PCM and zero audio
idle/error events. Whole-run presentation speed was 98.6988% and guest speed
83.9219%; the initial host/setup deficit remains visible.

Both synthetic captures showed the expected display and executed-firmware
status. This run qualifies aggregate startup pacing and guest resumption under
the recorded conditions. It does not measure every presentation deadline or
replace the commercial sessions. Its receipt is
`2b931dcdee1cb31e7eb6825bc90d91623d5d7538bc067eff03e32c85401c9fce`.

## Header protocol baseline and remaining work

A separate baseline completed 320 observations across 30 header, multiplayer,
palette, reset and partial-packet scenarios on both physical models and header
classes. Its controls passed, but 258 compared public fields differed: 60
baseline colors, 58 button results, 82 palette results and 58 controller IDs.
All 80 same-engine restored continuations matched their independent controls.
These differences confirm incomplete boot-header validation, command disabling
and physical receive eligibility. They are a failed parity result that guides
the next implementation, not a qualification of current header handling.

The baseline completed in 17.0931 seconds with unchanged protected inputs and
all children waited. Its receipt is
`b8e79e9c299e191ae5b389871c5d016f31c490999a78159b223b1923845421fd`.
The earlier incomplete observer attempt is retained separately; it omitted a
required PCM callback and produced no complete comparison.

Linux sustained gameplay remains unqualified after the earlier 300-second
clock-readiness check found host-clock disagreement. A build, short native
workflow or core timing comparison cannot establish sustained Linux speed.
No new sustained-readiness result is claimed here.

PAL and other host-integration variants, title-specific adapter palettes,
physical device behavior and acoustic fidelity remain open. MBC6 has a generic
bank placeholder without its split windows or flash implementation; TAMA5 is
unsupported. The [hardware guide](hardware.md) and [parity checklist](parity.md)
track the broader remaining work.

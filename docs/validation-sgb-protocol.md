# SGB boot-header protocol validation

The version 22 checkpoint passed the complete Windows, Linux and sanitizer gates,
followed by a bounded paired comparison of SGB/SGB2 command reception. The comparison
resolved all 258 public-field differences retained by the earlier baseline. All 80
restored continuations matched their independent controls.

This record covers boot-header reception, command permission, controller visibility,
native-state history and four bounded Windows gameplay sessions. Dedicated startup
playback workflows, version 22 state exchange and Linux gameplay remain pending.
The earlier [startup checkpoint](validation-sgb-startup.md) retains its own tested
source, results and limitations.

## Source and evidence identity

The tested inventory contains 305 build inputs. Its SHA-256 is
`f0b22114b2849252d91df40670652a104b793f2a71aa0e8a71d96838ee0e97ee`.
The complete gate's before/after inventories match. The paired comparison used the
Linux Release core from that gate and checked the same product inputs before and
after execution. The behavior reference remains revision
`ea667ad79aa28409f034b1fd6da7304e1844750d`.

| Evidence | SHA-256 |
| --- | --- |
| Complete gate receipt | `fd0511cba108b5c66b796801624343b97ddab1ce7e2fb5b407d1050bead884c3` |
| Final source freeze | `c742d264cf40dcdb58aa261b8b777ae8144128bf0162913ba5a0ffdb1292b2aa` |
| Linux Release core archive | `d0eef0eaa2f88ee432eeb50ce2250bab9ab10434f949ba6a6a1831e481356dec` |
| Paired comparison receipt | `a8e05745bc10c0cf16e412e91226f1a8f70227f12970f0d0b0954afe2515210a` |
| Paired Linux observation receipt | `5c57e9f05ec3acf7464d97bd2dc65ecb4316a684eac0f6033954f23b84907762` |
| Retained negative comparison receipt | `b8e79e9c299e191ae5b389871c5d016f31c490999a78159b223b1923845421fd` |
| Windows gameplay receipt | `9e2edf4e53c95a99fd182a116963afb8a944e9fa00fc2f70ac604f6337064cc9` |

These digests identify retained verification records. Games, firmware, saves,
captures and private evidence paths are excluded from public commits.

## Complete build and test gate

Gate `sgb-protocol-02` completed every registered test without failures, skips or
disabled cases. Both Release builds include Qt. The Linux sanitizer build uses
Debug with Qt disabled.

| Configuration | Passed | CTest elapsed time |
| --- | ---: | ---: |
| Windows Release with Qt | 94/94 | 110.67 s |
| Linux Release with Qt | 92/92 | 158.09 s |
| Linux Debug with ASan/UBSan | 67/67 | 211.43 s |

All five gate stages exited successfully. The receipt records no timeout,
cancellation, unresolved process cleanup or change to the protected inputs.
These durations measure the test suite, not emulation throughput.

The three Qt workflow groups ran offscreen with their existing 30-second deadlines:

| Workflow | Windows | Linux |
| --- | ---: | ---: |
| Desktop | 15.95 s | 13.66 s |
| Storage | 1.89 s | 2.36 s |
| SGB startup | 1.60 s | 2.24 s |

The groups exercise their registered workflow assertions. These runs do not require
a physical audio sink and do not qualify native window presentation or device playback.

The first gate remains a failed attempt: Windows passed 92 of 94 tests before the
gate stopped. Linux and sanitizer stages were not reached. The single-player input
fixture retained the previous multiplayer count, selected controller and disabled
receiver; its setup now explicitly restores one player. The MBC7 current-size
assertion omitted the new 85-byte protocol record; it now uses the shared current-tail
calculation. Only `tests/test_input.c` and `tests/test_mbc7.c` changed between the two
305-input gate inventories. Historical state lengths, digest expectations and the
failed gate's evidence remain intact.

## Regression coverage

`cupidgb_sgb_protocol` checks physical SGB/SGB2 reception with ordinary and enhanced
cartridge declarations, per-section checksums, repeated and reordered cache writes,
stop-pulse commit, post-handoff header handling, controller reads after rejection,
palette/mask effects and reset behavior. The Qt desktop workflow also checks input
delivery when physical controller selection differs from cartridge metadata.

`cupidgb_sgb_protocol_state` checks the fixed 85-byte version 22 extension: one
canonical disable byte and 84 cached header bytes. It covers partial reception,
exact and live loads through ordinary and trusted APIs, invalid records rejected
without changing the receiver, and the explicit migration for versions 2 through 21.
Non-adapter models require a zero protocol record. Older imports start with an empty
cache and retain their former receiver eligibility; they cannot recover unrecorded
header packets. The complete gate also reran the existing storage, startup, audio
and historical-format regressions.

See the [boot-header contract](sgb-startup.md#boot-headers-and-command-permission)
and [version 22 layout](save-states.md#adapter-header-protocol-in-version-22) for the
implemented rules and migration limits.

## Paired public-bus comparison

The unchanged matrix contains 30 scenarios on SGB and SGB2 with both ordinary and
enhanced cartridge declarations, using CupidGB and the pinned behavior reference.
Eight observer processes produced 240 independent control rows and 80 restored rows,
for 320 observations. The product supplied 64 restorations through its four load
APIs; the reference supplied 16 native restorations. All 32 snapshot files were
retained and checked. Product snapshots use native version 22 with valid lengths
and complete integrity digests.

The inputs are original generated cartridges and a short generated startup image.
Packet edges use public JOYP bus writes without CPU time between those edges.
JOYP observations use active reads, and guest instructions run to the next display
presentation. Startup phase is set to its completed value to isolate command
reception. This setup does not execute commercial firmware's header-transfer routine.

Each control observes four player IDs and four selected-button rows, plus three
viewport samples before and after the diagnostic palette/mask sequence. The frozen
expected controls distinguish enabled and disabled receivers. Checks include invalid
license/flag bytes, checksum failure, missing or reordered sections, ignored section
indices, incomplete packets, missing stop, header writes after handoff and reset.

The four saved boundaries are a cached header section, a partial packet awaiting its
next low pulse, a complete packet awaiting stop, and the disabled receiver latch.
The source continues without loading its own snapshot. Separate receivers then load
and execute the same suffix. Every restored public result matched that uninterrupted
control, including exact/live output-policy variants.

| Comparison | Earlier baseline | Version 22 |
| --- | ---: | ---: |
| Differing public-field groups | 258 | 0 |
| Differing same-engine continuations | 0 | 0 |
| Process exit code | 1 | 0 |

The 258 differences count array-valued public fields, not 258 separate scenarios.
They comprised 58 player-ID fields, 58 button fields, 60 baseline-color fields and
82 palette/mask fields. The new result changes exactly those product fields; the
reference's public fields are unchanged. No exclusion, expected-value rewrite or
subtraction from the old baseline was used. The earlier baseline retains its failed
verdict and original observations.

The paired run rechecked 305 product inputs, 51 reference inputs and 207 protected
files. Its one compiler and eight observers all exited successfully and were reaped
with no residual process groups. Compilation retained its 120-second limit; each
observer retained five seconds within a 60-second matrix budget. Fixed guest limits
remained 20,000 steps per presentation, 256 presentations and 500,000 JOYP writes per
process, and 32 calls per drain. The final receipt records no cancellation, deadline,
identity or cleanup failure.

## Windows gameplay

Four fresh commercial sessions passed on the exact Windows Release executable from
the version 22 gate. They retained the preceding checkpoint's local inputs, models,
scripted controls and seed, normal native audio, a 120-second rewind window and a
256 MiB rewind budget. Each session ran for 120 seconds total, including five seconds
of warmup; the measured windows were approximately 115 seconds. Entry was post-boot.

| Session | Measured guest speed | Measured presentation speed | Host paint rate |
| --- | ---: | ---: | ---: |
| DMG | 100.000338% | 100.000338% | 59.756607 FPS |
| CGB, first case | 99.999366% | 99.999366% | 59.791040 FPS |
| SGB | 100.003477% | 100.003477% | 61.212422 FPS |
| CGB, second case | 99.993467% | 99.993467% | 59.730030 FPS |

The unchanged post-warmup acceptance is 99.5% to 100.5% of each model's guest clock.
Presentation and guest clocks match in these post-boot sessions. Host paint counts
measure widget activity and are not a substitute for either clock. Whole-session
guest/presentation speed, including initial host setup, ranged from 99.864090% to
99.875503%; the warmup interval is not hidden in those totals.

All four sessions recorded zero audio idle/error events in the measured interval.
Generated handheld output ran at 44.1 kHz and reached the real 48 kHz stereo host sink.
Startup-origin PCM remained zero, as expected for post-boot entry. These observations
exercise ordinary gameplay playback, not the executed adapter introduction.

Scripted input, battery flush, rewind capture, visible-window telemetry and saved
capture checks passed. Each session's executable, cartridge and input trace retained
its expected identity. All child processes exited successfully and were reaped;
the final receipt records no timeout, cancellation, unresolved cleanup or protected
input change. No acceptance threshold, input sequence or workload deadline changed.

All four retained gameplay captures were reviewed and show active gameplay; the
SGB case displays its adapter border. An independent evidence audit verified 49
output hashes, 108 protected files, all 305 source inputs and all eight gate
artifacts, and recomputed all 128 existing validation checks successfully.

These checks do not establish game completion, acoustic fidelity, physical motor
operation, a battery save/reload
round trip or successful rewind replay. Version 22 file exchange between Windows
and Linux and sustained Linux gameplay/audio remain separate pending qualifications.

## Host clock readiness

A separate Linux readiness check, bound to the retained version 21 inputs, passed
four consecutive ten-second samples under the unchanged 0.1% full-bracket clock
tolerance. It retained the five-millisecond snapshot-bracket limit and the
300-second readiness deadline. The Linux receipt is
`3124567123ea2526faae0dedcdc21b1a87298f9fcfaebcb72d3235eded438dd3`.

That check started no emulator or gameplay child process. It establishes clock
readiness during those samples only. It does not qualify Linux gameplay, audio
playback or sustained speed, and it does not replace the earlier failed clock
evidence. Those results remain separate from this version 22 gate and comparison.

## Remaining qualification

Dedicated version 22 startup workflows with real audio playback and sustained
Linux gameplay/audio remain pending. Four bounded Windows gameplay sessions do
not replace those qualifications; reviewed captures retain the same bounded scope.
Windows/Linux exchange of version 22 snapshots also requires separate evidence;
the earlier completed startup-state exchange belongs to its version 21 checkpoint.

This matrix does not qualify instruction-timed packet edges, actual commercial
firmware startup, title-selected fallback palettes, full image identity, acoustic
fidelity, physical controllers or every remaining SGB command family. It samples
defined viewport positions and keeps normal audio generation active without comparing
complete images or waveforms. The [parity checklist](parity.md) tracks the wider work.

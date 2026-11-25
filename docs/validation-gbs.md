# GBS player validation

This checkpoint adds GBS version 1 execution and a Qt music player. The comparison revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`. Private receipts identify the source files, build inputs, executables, original generated programs and commands used for each comparison. The results below qualify those inputs and workflows; they do not establish compatibility with every distributed music file.

## Loading and execution

The core owns the prepared program image and its immutable 112-byte music header. Loading validates the header, addresses and allocation limits before replacing a session. Track initialization prepares an independent hardware state before committing, while retaining the existing program allocation and host registrations. Allocation failures and malformed input preserve the current program and the caller's metadata output.

Generated programs exercise raw initial and selected track bytes, normal and timer-driven play calls, interrupt vectors, stack placement, bank selection and the format's double-speed flag. The original checkpoint covered six hardware profiles. The integrated unit tests also cover PAL. The desktop accepts up to 4 MiB of payload plus the header. The buffer API checks its own allocation limit. Metadata decoding preserves Latin-1 bytes, empty fields, a zero declared track count and an out-of-range initial track.

Extended MBC3 wiring can remain attached to a later music program after an earlier cartridge load. The implementation retains that host history across intervening loads and resets. A fresh instance and a previously extended instance can therefore execute the same large music file with different bank wiring. This is tested separately from ordinary cartridge bank selection.

The paired 145-case execution matrix uses these specific profiles:

| Fixture label | CupidGB profile | Comparison profile | Base clock |
| --- | --- | --- | ---: |
| `dmg` | Grouped DMG profile | DMG-B | 4,194,304 Hz |
| `cgb` | CGB | CGB-E | 4,194,304 Hz |
| `sgb` | SGB | First NTSC SGB, with PAL and host-integration flags clear | 4,295,454 Hz |

Both engines select their model before initialization, use seed `0x26154` and load no firmware. These comparisons do not qualify every revision grouped under CupidGB's DMG or CGB tags. The separate cross-host state exchange includes SGB2 at 4,194,304 Hz; it is not an additional profile in the paired execution matrix.

## Audio and timing corrections

Fresh music initialization clears fabricated post-boot reconstruction history before applying its real sound-register writes. Track changes retain queued PCM and the current reconstruction history, then let the new register writes affect that history. Restoring the old reconstruction object after those writes would lose their transitions; the comparison probes reproduced that failure before the fix.

Monochrome double-speed HALT still delivers two CPU T-cycles per half of its interrupt poll. Each half advances one base dot at double speed, or two at normal speed. Timer, DMA, display, cartridge and audio clocks retain their separate units. The regression checks the first and subsequent HALT polls, divider and timer progress, presentation clocks, phase and interrupt wakeup.

The line-153 probes distinguish readable LY, coincidence readback and the STAT interrupt edge. Fast monochrome execution retains the preceding comparison through the transition, clears readable LY at its later boundary and then compares against zero. Tests retain the ordinary-speed and physical-CGB cases as controls.

Pulse frequency writes now replace the countdown during the reload window on monochrome as well as color hardware. The noise trigger retains separate rules for fast monochrome execution, including its phase-dependent initial delay, an active counter carry and retained background-clock history. The earlier probes reproduced a four-base-clock pulse error and an eight-base-clock noise error before these corrections.

Audio reconstruction updates follow actual channel events. An idle divider edge or a configuration write cannot prematurely apply saved routing after a live state load. Envelope, length, sweep and noise-selector events retain their order, including multiple transitions on the same clock. Targeted regressions cover a pulse rise followed by a delayed envelope fall and a noise-selector write whose intermediate output differs from its final output.

Audio phase probes keep raw engine fields in their receipts. One engine's reconstruction counter uses half-rate audio cycles and CupidGB's uses base T-cycles; their numerical values differ by two even when elapsed time agrees. The normalized comparison records that source-derived conversion explicitly. It does not replace or discard the raw observations.

A separate ordinary-cartridge preload probe starts with different listening history: one loader retains cold audio history while CupidGB's approximate cartridge startup seeds its post-boot output. That PCM difference remains identified in the receipts. Cold music and retained real-audio histories are separate comparison cases.

## Native states

Native version 23 retains the version 22 hardware layout and adds a GBS program-kind flag. Music identity hashes a domain separator, effective bank wiring, accepted payload length, the original header and the entire prepared image. The receiving session must already own the same program. A state cannot grant music semantics or non-CGB double-speed permission to an ordinary cartridge.

Exact restore reproduces saved guest execution and reconstruction history. Live desktop restore retains the receiving listening history. Both paths are tested under their own contracts. The host's selected track is deliberately outside the state: loading restores the saved guest, while Restart uses the displayed selection and Reset reloads the file's initial selection.

The legacy regression now constructs version 22 explicitly in addition to the current version. Older cartridge versions retain their flag restrictions and migration checks; unknown flag bits remain invalid.

## Desktop workflows

The dedicated Qt check covers file and archive loading, metadata, track transport, malformed counts, Reset versus Restart, waveform publication, audio recording, state restoration, debugger stepping, failed replacement and return to a cartridge window. It also closes a music window from an audio callback to check reentrant lifetime handling. A failed reset candidate cannot modify the live printer or invoke a host clock observer while still preparing.

The waveform uses the last 1,024 stereo frames before volume and mute. It publishes only when the owning program completes a presentation frame. Unrelated widget repaints cannot reveal a partial frame. Runtime reports count music waveform publications and paints separately from game images, guest clocks and audio frames, and record the actual sink volume and mute state.

Native Windows and Linux GBS workflow checks require an actual audio sink. Their retained captures were reviewed for metadata, waveform and transport layout. Generated music inputs contain original instructions rather than music extracted from a commercial title.

## Core comparison results

The corrected core was tested in Release builds with IPO on Windows and Linux. Each host ran the 145-case execution matrix under exact and live restore, a separate 25-case identity matrix under both contracts, and two controls with equal initialization history. All 688 candidate processes completed successfully. The comparison retains literal PCM bytes and explicit mismatches.

| Observation | Result |
| --- | --- |
| Guest execution, registers, interrupts and clock totals | 145/145 match per host and restore contract |
| Complete forward observations, including raw PCM | 143/145 match per host and restore contract |
| Exact same-engine guest and PCM continuation | 344/344 match across the execution, identity and history matrices |
| Live continuation with receiving output history | 344/344 match across those matrices |
| Wrong-program and bank-wiring identity rejection | 100 cases; all 200 ordinary/context API rejections preserve the receiving state |
| Equal-initialization-history controls | 2/2 match per host and restore contract |
| Line-153 boundary observations | 480/480 rows match across 12 runs |
| Cross-platform native-state exchange | 12/12 exchanges match guest/PCM suffixes and byte-identical re-encoding; 36 processes |
| Available cartridge regression corpus | 99/99 explicit passes on each host, with no deterministic differences from the preceding checkpoint |

The two raw forward differences are the DMG and CGB ordinary-cartridge preload cases described above. The identity matrix likewise retains 23/25 raw forward matches per host and contract because two cases start from that different preload history. Every live continuation in those matrices matches. Equal-history controls pass independently; they do not change the raw totals.

Audio comparisons disable high-pass filtering and interference and leave all channels enabled. The VBlank-driven and double-speed fixtures use 44,100 Hz; the normal-speed timer-driven fixture uses 48,000 Hz. The nine generated audio cases now match in all 36 host/restore combinations. Native Qt playback uses its own recorded settings and is measured separately.

The cartridge replay uses 49 distinct test ROMs, producing 96 mapper/timer/interrupt/HALT cases, two serial post-boot cases and one serial case with the same external firmware input per host. Its seed is decimal `26154`. Both hosts retain all result fields, including guest cycles, steps, image CRC and audio CRC; only host elapsed time is excluded from equality. All 89 protected input and result files remain unchanged.

The immutable core aggregate is `0612540642f6deb245bb5fbd2687108fdc4a780449f27ea85b2c6375b516f279`. The focused audio receipt is `04c6329ebca31137d71686c999d9e762f2e8d1019bf22f2e91a5e7f735a5dc56`; the cartridge replay receipt is `41b8858bdf9aa045a6e3f65b07b8666f3650e5b6994c6ad09fd8489548b81a0d`. These core observations precede the final frontend lifetime repair and remain bound to their own source and library identities.

## Preserved development failures

The private evidence keeps unsuccessful attempts with their original sources and binaries. Early PCM comparisons exposed both fresh-history initialization and the restoration ordering bug. Double-speed traces isolated the monochrome HALT clock error and the line-153 readback and interrupt boundaries. Correcting a counter's unit interpretation is recorded separately from those behavioral changes.

The v5 freeze passed all 98 Windows, 96 Linux and 69 sanitizer tests, with no skipped or disabled cases and unchanged build inputs. Its wider comparison still matched only 139 of 145 complete forward observations per host, and 138 of 145 live continuations. Four fresh timer-driven audio cases differed, alongside the two already identified ordinary-preload histories; live audio also differed after restoring an otherwise matching program. Guest traces, registers, interrupts, clock totals and sample counts agreed. The PCM differences exceeded one-unit rounding and remained at the unshifted comparison point. These failures prompted separate pulse-reload and channel-publication investigations; the passing local gates did not qualify that audio comparison.

Two older state tests treated the newly assigned program-kind flag as an unknown bit. They now check an actually unknown bit, the current program-kind mismatch and the legacy flag rule separately. The record retains the failing assertions and corrected focused sanitizer run.

The first Windows full-gate command omitted the documented Qt runtime path. Desktop launches timed out without output; that attempt was stopped and retained. The corrected environment exposed an obsolete archive-diagnostic assertion on both platforms. The archive remained rejected with the expected failing exit; its test needed to use the current typed-media diagnostic. A concurrent sanitizer storage test also reached its deadline. Final gate results identify their own invocation and source manifest rather than replacing these attempts.

The final lifecycle audit also reproduced three Linux crashes when an audio-sink retirement deleted the window during Open, Reset or Quick Reset. Each original process reached its named operation and exited with signal status 139. The failing executable, frontend source and process logs are retained separately from the repaired build.

## Qt lifetime completion

The final frontend repair makes audio-pump cleanup conditional on the window still existing. Retiring a backend can delete its owning window before the pump's local leases and scope guards finish. The pacing tick now checks the same lifetime boundary before continuing audio work, executing a guest frame or refreshing its timer. Deferred mode cleanup checks ownership again after resetting the audio stream.

A preserved Linux Release control exits with status 139 in the `tick-retirement` case. The repaired build passes that case and the complete GBS workflow on Windows and Linux with Qt 6.8.3. The `pump-retirement` control returns successfully on both hosts, so its return code alone does not detect its dangling cleanup guards. The repair removes those references explicitly.

Both hosts pass seven focused Qt CTests covering command-line model selection, cartridge dispatch, desktop controls, larger storage, SGB, linked windows and GBS. The GBS workflow checks fifteen named deletion cases across Open, both Reset commands, Close, window closure, state loading, console reset, pacing/audio retirement and error dialogs. Its paused track-restart check also deletes the owning window during audio retirement. The test uses the public byte-write API when preparing its battery-error cases.

These are offscreen integration checks. Native output-device playback, visible presentation and acoustic quality retain their separate qualification requirements. The core comparison results above remain bound to their recorded version 6 inputs.

## Qualification limits

Generated programs establish loading and selected execution contracts, not compatibility with a distributed GBS library. Native waveform activity and a healthy audio backend do not establish audible quality or the absence of glitches. The [Linux host-clock limitation](validation-host-clock.md) remains relevant to sustained playback measurements. Cartridge regression results, actual gameplay scenes and music measurements retain separate input sets and durations.

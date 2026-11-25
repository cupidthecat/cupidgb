# Audio and native-state validation

This record covers the 2026-11-16 audio, native-state and Camera checkpoint. It distinguishes digital hardware observations, generated samples, native playback and commercial continuation. None of those checks establishes complete hardware parity or compatibility with every game. The pinned comparison revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`.

The final product-input manifest contains 230 files and has SHA-256 `e9d8b546c429f58a67774bf13856833728457f09a4829f7897a80c10bead4c4d`. It covers source, headers, tests, build inputs and supporting product files; documentation and private evidence are separate. Each private receipt records the exact inputs, executable or core archive, harness and comparison outputs used by that check. Original cartridges, boot binaries, saves and earlier evidence remain outside the repository changes.

## Changes under test

The core now keeps digital sound hardware separate from output reconstruction. Each channel schedules fractional transitions through an independently generated 256-phase, 64-tap coefficient bank. Smaller modules own noise timing, reconstruction, DAC response, filtering, output delivery and state encoding. The Qt frontend exposes three high-pass modes, optional interference and four remappable channel toggles. Host volume and global mute retain their separate listening role.

Native version 14 stores pending digital clocks, reconstruction rings, DAC delays, filter history, channel policy and the independent interference generator. Exact core loads restore that recorded output history. Live loads used by Qt settle the saved digital work on a candidate, discard its output and retain the receiver's listening history. Disk loads, rewind and debugger Backstep use this live path. Invalid loads still leave the receiving core and its attached device state unchanged.

Camera capture now uses calibrated integer gain, truncates that stage before exposure, and preserves each edge term's signed truncation before threshold conversion. The generated sensor remains a substitute for a physical camera. Matching digital output does not qualify a real sensor or host camera integration.

## Build and desktop gates

| Environment | Gate | Result |
| --- | --- | --- |
| Windows x64, MSVC Release with IPO, Qt 6.8.3 | Complete CTest run | 60 of 60 passed; none skipped |
| Windows installed folder | Native Qt workflow smoke with an actual audio sink | Passed all seven required workflow markers; clean exit |
| Windows installed folder | Runtime dependency and visual checks | All 295 installed files retained their hashes; loaded Qt and SDL paths resolved inside the staged application |
| Linux under WSLg, GCC Release with IPO, Qt 6.8.3 | Complete CTest run | 58 of 58 passed; none skipped |
| Linux installed prefix, Xcb and the existing PulseAudio server | Native Qt workflow smoke with an actual audio sink | Passed all seven required workflow markers; clean exit |
| Linux installed prefix | Runtime dependency and visual checks | All 22 installed files retained their hashes; actual Qt and SDL module paths matched the selected runtime |
| Linux GCC Debug, ASan/UBSan, Qt disabled | Complete core/runner CTest run | 39 of 39 passed; leak detection enabled and undefined-behavior failures configured to stop the run |

The Windows desktop executable has SHA-256 `10d4eefef0644a6cc18ed0f2b0666f56ed13140a1489a97bab658f3845a7b3c3`. The ROM runner has SHA-256 `b8be61248c1f0b2e9a34a60b309df4245923f6ee2a8d341ab173066a5b41cda8`. These hashes bind the tested binaries to the product-input manifest above. Hosted checks validate the committed pull-request head separately.

The Linux build executable has SHA-256 `6fe70ade7371bb0d429b0e56dad418cfbbeed347438cd0bcd7d80562892626ec`; installation removes the build runtime path, producing SHA-256 `00496cebc1e29d388eb1b3c09f91586a2653f27e480a2ddb24a3ebbdf71a907d`. Its runner has SHA-256 `aaa5fa972f3b5d181cc4c8c770e2e9558b5115b39ac6f28998bb42eb5c6fb1e1`. The Linux test count is lower because two file-association tests are Windows-only.

Each platform's native workflow produced thirteen private captures, with five inspected. Inspection covered audio settings, channel controls, remapped shortcuts, the debugger, memory inspection and startup messages. Labels, values, checkmarks and shortcuts were readable. Windows negotiated 48 kHz with eight-byte host frames; Linux used 44.1 kHz with four-byte host frames. Both reported sink error zero and capacity progress during Turbo and rewind. A successful native audio smoke establishes that the backend opened and the exercised workflows completed; it does not establish audible quality or long-term stream stability.

Linux compilation initially spent most of its sampled elapsed time waiting on mounted-filesystem reads. A private native-filesystem cache preserved all 10,732 Qt entries, including 9,808 regular files, with identical bytes, link targets and modes. The temporary cache disappeared after a WSL restart; a version guard rejected a fallback to Qt 6.4.2 before any build used it. The final build uses a verified persistent Qt 6.8.3 cache. Its complete one-job rebuild finished in 153.2 seconds, and the core archive retained its hash. No global Qt or WSL setting changed. The cache and its runtime identity were checked before and after native validation.

The build retains the previously reviewed GCC HRAM range diagnostic described in [cartridge-clock validation](validation-rtc.md), along with conversion diagnostics in several modules. The four noise-module diagnostics were reviewed against Boolean feedback, 16-bit counter bounds and selector shifts from zero through fifteen; those conversions preserve the values. Noise transition, selector and carry regressions also passed under ASan/UBSan. Diagnostics remain in the receipts; this is not a warning-free build.

## Bounded native playback

Six installed-application runs exercised DMG, CGB and SGB for 120 seconds each, one at a time after builds and cartridge scans had finished. They used normal audio, power-on seed zero, explicit post-boot profiles, isolated settings, copied private cartridges and fixed input traces. The table reports guest base-clock progress and widget paint intervals after the five-second warmup. Resident memory is the maximum sampled process value over each run.

| Platform and model | Guest speed, app clock | Paint interval p95 | Longest paint interval | Peak resident memory |
| --- | ---: | ---: | ---: | ---: |
| Windows DMG | 100.0024% | 17.634 ms | 21.559 ms | 108.0 MiB |
| Windows CGB | 100.0020% | 17.693 ms | 32.749 ms | 226.3 MiB |
| Windows SGB | 100.0057% | 17.285 ms | 23.769 ms | 108.6 MiB |
| Linux DMG | 100.0022% | 17.627 ms | 20.915 ms | 128.2 MiB |
| Linux CGB | 99.9902% | 17.513 ms | 20.186 ms | 243.1 MiB |
| Linux SGB | 100.0072% | 17.103 ms | 19.613 ms | 129.5 MiB |

All six exited cleanly with normal output, zero reported audio errors and zero post-startup or post-warmup sink-idle events. DMG and SGB applied fourteen scripted inputs each; CGB applied twenty-two. The core produced 44.1 kHz audio, with a 48 kHz Windows sink and a 44.1 kHz Linux sink. Both CGB sessions flushed identical 8,192-byte save files. This verifies the exercised flush, not an in-game save and reload workflow.

The Linux resource sampler recorded adjusted monotonic and RAW clocks separately. DMG, CGB and SGB process durations were respectively 120.233, 120.161 and 120.122 adjusted seconds, versus 125.825, 124.913 and 125.968 RAW seconds. Across individual sampling intervals, adjusted/RAW ratios ranged from 0.916659 to 1.083337; the largest timestamp bracket was 14,625 ns. Process durations include startup and shutdown. The guest percentages therefore qualify pacing against the application's adjusted clock, not independently measured real-time speed. Widget paints do not establish physical display refresh, and these runs do not establish audible quality. Windows process CPU-utilization counters remain unqualified.

All six raw captures are 160 by 144 pixels, with a 320 by 288 game image in the inspected application windows. No blank or corrupt rendering was visible in those ending captures. DMG and SGB end at a game-over screen; CGB ends inside a level beside a pipe and enemy. These scenes do not establish uninterrupted active play for the whole run. The SGB samples use an ordinary cartridge image without an enhanced border.

Every run checked the 230 product inputs, installed payload, runtime manifest, helper identity and protected originals before and after execution. No input changed. Linux also retained the exact persistent Qt cache. The runs exercised two commercial titles and the SGB model clock; they do not establish compatibility with every game or complete enhanced SGB behavior.

## State restoration regressions

A low-rate donor can legitimately retain more deferred audio work than a high-rate receiver's output credit can fund. The previous live-load path replaced the donor's output history before settling that work and rejected valid snapshots. The corrected path validates the donor, finishes its deferred digital clocks on the candidate, retains the receiver's output and then validates the combined state before committing it.

The regression matrix covers 96 mixed-rate cases: three hardware models, four donor rates, four receiver rates and both live-load APIs. Each case compares against an explicitly settled donor and then checks continued PCM and encoded guest state. Separate active-channel coverage retains 2,048 credited base clocks before restoration. Exact-load checks retain their byte-exact contract, and malformed-load checks retain full receiving-state comparisons.

Qt workflow checks cover receiving sample rate, filter/interference/channel policy, recording continuity, rewind and Backstep. A load cannot replace an active recording's sample rate with a slot's older listening rate. Tests inspect appended PCM and the resulting WAVE file. Failed loads preserve the recording and session. Turbo-cap comparisons restore identical output history before execution and check complete PCM and guest state.

Legacy fixtures retain their prior portable hashes. Imports from versions 2 through 13 validate their retired fields and supply explicit defaults for history those versions did not store. Those defaults cannot reproduce missing waveform history.

## Paired audio observations

The final Linux core archive was compared against frozen generated programs and the pinned implementation. These checks use identical models, rates, startup inputs and scripted writes. PCM is compared at its producing step, without alignment, trimming or resampling.

| Comparison | Cases | Stereo frames | Result |
| --- | --- | --- | --- |
| Guest execution and audio boundaries | 80 | 43,526 | Exact PCM, delivery, frame and step counts; no CPU-visible register or total-clock differences |
| Oscillator/filter programs | 144 | 65,310 | Exact PCM, delivery and frame counts in all three filter modes |
| External audio test ROMs on Linux | 24 | Protocol verdicts | Twelve DMG and twelve CGB explicit memory-protocol passes |

Each filter mode covers 48 programs and 21,770 stereo frames: DMG, normal-speed CGB and double-speed CGB, at 44,100 and 48,000 Hz, with four channel programs and two variants. Off, Accurate and Preserve Waveform each retain their own comparison output. This is generated-program coverage, not broad commercial audio equivalence.

The guest trace check distinguishes CPU-visible registers from internal storage. Two DMG HALT cases initially differed for one raw TIMA row: the comparison stored `0xA7` early while masking the CPU read to zero, whereas CupidGB retained zero until the reload completed. Fresh live and cloned reads both returned zero at that boundary, then `0xA7` with the same interrupt on the next step. The reads did not mutate either machine. The original raw mismatch remains recorded; a fresh 80-case run samples the CPU-visible timer and passes. One internal audio batching difference also remains recorded separately from guest clocks and delivered PCM.

## Executed firmware audio

Twenty paired firmware runs refresh the earlier startup matrix against the final Linux core. Five firmware/cartridge profiles each use four retained seeds, 44,100 Hz output, Accurate filtering, zero interference, released buttons and no battery import. The final drain settles already credited APU work in both implementations; it does not add guest clocks.

| Firmware profile | Cartridge mode | Base cycles per run | Stereo frames per run |
| --- | --- | ---: | ---: |
| Supplied monochrome firmware | DMG | 23,440,324 | 246,457 |
| Supplied color firmware | CGB compatibility | 13,053,116 | 137,243 |
| Supplied color firmware | CGB color | 13,054,568 | 137,259 |
| Pinned built color firmware | CGB compatibility | 13,156,372 | 138,329 |
| Pinned built color firmware | CGB color | 13,085,724 | 137,586 |

All twenty pairs reach PC `0x0100` with matching CPU registers, final instruction, instruction and step counts, base cycles and audio-frame counts. All 3,187,496 stereo frames, or 6,374,992 channel samples, are byte-identical. This closes the previously recorded boot PCM difference within this matrix. Every reference output reproduces the retained startup evidence; PCM is the only captured candidate output that changed.

Raw register and memory differences remain recorded. Mapped differences include independently initialized CGB palette bytes and DMG wave RAM. The CGB internal dot coordinate still differs by four dots at handoff. The harness captures VRAM and palettes but has no framebuffer or image gate, so these runs do not establish complete boot graphics or register parity. All 230 product inputs, 1,153 earlier evidence files and 57 original protected inputs remained unchanged.

## Sustained SGB output phase

Nearest rounding of the cached reconstruction increment could advance fractional phase slightly faster than rational sample delivery. The discrepancy accumulated at uncommon SGB rates. With the old implementation, naturally scheduled histories first became invalid after approximately 407.4312 guest seconds at 8 kHz and 344.9059 guest seconds at 192 kHz. Retained boundary histories reproduced the failure after one additional base clock.

The corrected increment rounds down. Sample delivery remains rational, and a render boundary clamps the small negative remainder. The 4,194,304 Hz model increments are unchanged; SGB's 44.1 and 48 kHz increments are also unchanged. This is an explicit comparison exception for affected SGB output rates, not a claim of identical samples at every rate.

The `audio_clock` regression checks 368,002 supported rate/clock combinations, the full-span bound, both one-clock boundary histories, current-state recapture, recognized older caches and malformed-cache rejection. Separate native scheduler probes at the earlier corrected-audio checkpoint ran 430 guest seconds at each affected rate with queue draining and no invalid state. Their final credit, phase and pending clocks were zero. The permanent regression and final comparison suites ran on the final product freeze. The long probes use inactive channels and establish scheduling and snapshot bounds; they are not gameplay, audio-device or speed measurements.

Early version 14 snapshots can contain the exact earlier nearest-rounded cache. The decoder accepts only that derived value with its matching render span, then canonicalizes the cache. It continues to reject unrelated increments and spans. Current version 14 snapshots retain exact recorded output continuation.

## Camera output

Paired probes supply identical deterministic grayscale pixels to each implementation. Calibration covers all 32 gain selections and all 256 input values, or 8,192 observations. Full-image checks cover each gain with eight edge strengths and the disabled-edge case. Focused cases add exposure boundaries, capture-counter extremes and threshold patterns.

| Check | Scope | Result |
| --- | --- | --- |
| Full images | 288 images; 1,032,192 encoded bytes | Exact on Windows and Linux |
| Focused exposure/edge cases | 27,648 cases; 884,736 selected bytes | Exact on Windows and Linux |
| Combined output | 1,916,928 bytes per platform | Zero differing bytes |
| Previous Windows implementation | Same combined input set | 95,109 differing bytes |

The final combined output has SHA-256 `9fe06bce9706d8d9afdec4553bfa08787798c142d8b18d725a1bc96a82810e18`. Permanent regressions cover complete encoded images, border pixels, gain, exposure, capture-counter wrap and both tile bitplanes. Physical capture, sensor noise and complete Camera cartridge behavior remain separate requirements.

## Mapper and infrared comparisons

Fresh probes linked against the final Windows and Linux core archives. Each platform produced the following results against the pinned comparison:

| Probe | Observations | Result |
| --- | --- | --- |
| Header and storage resolution | 4,096 rows | 4,066 exact; thirty retained exceptions |
| MMM01 menus and metadata | 2,244 rows | All exact |
| Infrared register behavior | 17,664 rows | All exact |
| Stable infrared input | 10,240 rows | All exact |
| Infrared transition timing | 8,192 rows | 550 retained differences |

All 42,436 candidate rows were identical between Windows and Linux. The thirty header exceptions are ten cases each for direct-RAM types `08`, `09` and partial MBC6 type `20`. CupidGB deliberately retains direct RAM for the first two types. The pinned implementation does not support MBC6, while CupidGB support remains partial.

The retained infrared transition matrix starts from different divider origins: cold hardware state on one side and CupidGB's post-boot profile on the other. Its 550 differing rows therefore do not isolate an emulation defect. The original observations remain in the totals and private receipts. Separate controlled probes reset the divider and display origins before testing. They still show that divider resets and guest display-memory reads can change receiver detection in ways CupidGB does not yet retain. Those controlled cases, followed by a matrix with equivalent initial clocks, define the next correction.

## Commercial native continuation

Nine scenarios use eight private commercial ROM images spanning ROM-only, MBC1, MBC2, MBC3, MBC5, MBC7, HuC3 and Camera. Five scenarios use the DMG profile and four use CGB. Each local run warms for at least 3,000 nominal frame intervals, captures a version 14 state during LCD mode 3 with 17 to 19 queued PCM frames, then compares two instances over 600 further intervals and eight scripted input changes. Imports begin from those saved states. One scenario explicitly reaches active MBC1 gameplay; the others have their own recorded scene limits.

All 36 runs passed: nine local replays per platform, nine Windows-to-Linux imports and nine Linux-to-Windows imports. The checks made 319,582,692 CPU-state comparisons after core steps, 21,608 presentation comparisons, 432 complete encoded-state comparisons and 15,949,064 PCM-frame comparisons. Core steps include idle and interrupt work; that count is not an instruction count. Saved and final state bytes and both framebuffer formats also matched across platforms. The Camera case continued from capture 174 through capture 182.

The harness uses a fixed power-on seed of `0x4355504944474231`, an explicit post-boot profile and a deterministic virtual cartridge clock. It preserves 658 prior evidence, ROM and save files by hash and modification time. This establishes bounded native continuation and portability, not equivalent reference gameplay, full-game completion or frontend performance. No local commercial HuC1, MMM01, TPP1 or MBC6 input was available.

## External test ROMs

The final Windows runner used frozen ROM inputs, recorded boot binaries, explicit models and power-on seed zero. The same source manifest and runner hash were checked before and after execution.

| Group | Explicit passes | Other outcome |
| --- | --- | --- |
| Acceptance | 119 | None |
| Audio | 24 | None |
| CPU | 22 | None |
| Mapper | 56 | None |
| OAM | 7 | One bounded case |
| Timing | 14 | None |
| Total | 242 | One bounded case out of 243 |

The remaining OAM case, `7-timing_effect.gb`, did not produce an explicit success result within its bound. Its logger output and run receipt remain available privately. It is not counted as a pass. The suite, runner and 147 protected original files remained unchanged, with no source or power-on-seed mismatches.

## Local cartridge scan

The final Windows runner checked 880 local inputs representing 878 distinct ROM hashes. Each used its recorded hardware model and boot binary, power-on seed zero, a 3,600-interval guest-clock budget and a 90-second host timeout. The scan supplied no scripted gameplay input and had no guest success protocol.

| Mapper family | Reached run bound | Illegal opcode |
| --- | --- | --- |
| ROM-only | 12 | 0 |
| MBC1 | 272 | 0 |
| MBC2 | 11 | 0 |
| MBC3 | 81 | 0 |
| MBC5 | 494 | 6 |
| MBC7 | 1 | 0 |
| HuC3 | 1 | 0 |
| Camera | 1 | 0 |
| Unrecognized header family | 1 | 0 |
| Total | 874 | 6 |

All six illegal-opcode stops were reproduced in fresh paired runs with equivalent initial memory, ROMs, models, boot inputs and seeds. Terminal addresses/opcodes, step counts and base cycles matched. A seventh historically investigated input reached the bound in both implementations and ended in STOP. Its classification depends on startup memory; an earlier zero-memory run cannot qualify the current seeded run. Neither the matching failures nor the bounded outcomes establish successful gameplay.

The scan retained its first 67 completed rows when it moved from two workers to eight, then executed only the remaining 813 inputs. The final record contains all 880 unique input IDs and preserves the earlier partial log and manifest. Concurrent wall times are not speed measurements. All 1,284 protected originals retained their bytes and modification times; the suite, runner, source inputs and seed checks were unchanged.

Generated tests separately cover ROM-only storage, MBC1 and multicart wiring, MBC2, ordinary and extended MBC3, MBC5/rumble, MMM01, MBC7, HuC1, HuC3, Camera and TPP1. No local commercial HuC1, MMM01, TPP1 or MBC6 input was available. Partial MBC6 has header/storage observations but no dedicated flash-protocol qualification. These gaps remain open.

## Source organization and remaining scope

All 206 first-party C and C++ source/header files have descriptive GPL headers with the requested author and date. No such file exceeds 2,000 lines; the largest is the 1,965-line native-state regression. The CGB subsystem is already split into control, DMA, palette and rendering files. The audio coefficient include is generated by the checked-in generator. Third-party source and notices remain unchanged.

All 206 files pass the formatting check, use LF endings and pass the header audit. The repository formatting file matches the supplied file byte for byte, with SHA-256 `0081591e56fb3455123fcaa3a97e0b1fcf1c8f01e41e6b0806da11434ddcc364`. The whitespace check reports no errors, and the product-input manifest remains unchanged.

The build has no GBA execution target or entry point. File rejection and its regression still recognize unsupported extensions so they can report a useful error.

The [parity checklist](parity.md) retains unresolved model revisions, boot behavior, finer bus and display timing, audio revision differences, SGB host/audio features, infrared transitions, physical accessories, mapper limits, desktop workflows and broader commercial qualification. The [audio guide](audio.md) defines the implemented ownership and timing contracts. Passing this checkpoint does not close those requirements.

## Monochrome envelope edges

A paired DMG multicart replay first differed at output frame 18,963.
Both builds scheduled the pulse transitions at the same phase. CupidGB
lowered the pulse volume one 64 Hz tick early after a retrigger. The fix
counts at sequencer phase 6, arms the envelope on the rising DIV edge,
and applies the volume step on the falling edge. A trigger clears an
already armed step.

The first 40 nominal guest intervals now match all 29,534 stereo frames
with Accurate filtering and with the filter disabled. The paired revision
is `ea667ad79aa28409f034b1fd6da7304e1844750d`. A generated comparison
checked 116,640 observations in 1,296 cases across DMG, Game Boy Pocket,
SGB, and CGB-E. It matched the four-bit volume, countdown, armed latch,
endpoint lock, and pending lock. The probe masks internal volume to
the four-bit counter width. The raw comparison value was wider in 3,600
observations after live control writes.

The four full multicart routes retained the sample counts. Remaining
paired differences are counted per signed 16-bit channel sample.

| Route | Before | After | Largest remaining difference |
| --- | ---: | ---: | ---: |
| Mortal Kombat I, active | 682,609 | 133,292 | 9,248 |
| Mortal Kombat I, idle | 636,120 | 89,169 | 5,900 |
| Mortal Kombat II, active | 3,295,012 | 3,229,216 | 9,904 |
| Mortal Kombat II, idle | 3,295,068 | 3,229,218 | 9,904 |

The generated regression checks both pulse channels and noise after a
retrigger between divider edges. State tests cover exact pending-latch
restore, version 30 import before and after the rising edge, and PCM
publication after a live restore. [Commercial mapper validation](validation-cartridge-storage.md#commercial-mbc1-multicart-fights)
records the matching cross-platform replays and remaining timing gaps.

Release passed 111 tests on Windows and 109 on Linux. The Qt desktop
workflow also passed at 125% device scale on both. The APU, audio publication,
audio state, and native-state tests passed ASan and UBSan on Linux.

## Monochrome pulse frequency writes

In the Mortal Kombat I replay, a write to NR24 changed pulse 2 from
frequency `0x7f3` to `0x6f3` on the reload tick. CupidGB loaded the new
1,076-clock period and retained a duty step that should have been
canceled. The oscillator then changed the output one step early. The
fix cancels the step at that edge while keeping the digital sample
already held by the channel.

A generated cartridge reproduces the edge without commercial inputs.
The two neighboring write placements, a write that stays in the high
frequency range, and a trigger write keep their previous behavior.
Across 361 write placements on DMG ABC, two of the three mismatches are
fixed. The remaining case is well past the reload tick, when the pinned
comparison backs up duty after the countdown happens to equal the newly
written low-byte period.
The core regression checks both pulse channels on DMG 0, DMG ABC, Pocket,
SGB, and SGB2. It verifies the new countdown, the held sample and the
next duty step on either side of the reload boundary.

The active Mortal Kombat I route matches its first 2,414,746 stereo
frames, about 54.76 seconds at 44.1 kHz. Differing 16-bit channel samples
fell from 133,292 to 21,707 out of 4,873,132. The peak difference fell
from 9,248 to 5,894. The active Mortal Kombat II PCM comparison retains
its 3,229,216 differing channel samples. Both replays still match the
pinned guest execution, final clocks, registers and terminal images.

The Windows and Linux builds produced identical PCM and exact state
continuations for both active routes. The DMG and CGB-E sound ROMs each
reported twelve passing cases on both hosts, with matching PCM and image
checksums across hosts.

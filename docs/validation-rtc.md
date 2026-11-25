# RTC validation

This record covers frozen cartridge-clock work compared with behavioral reference
revision `ea667ad79aa28409f034b1fd6da7304e1844750d`. Counter, policy and battery
results precede the divider repair; V5 and V6 record its focused comparisons
and regression gates. The integrated Windows, Linux, sanitizer, commercial
continuation and sustained desktop results appear below with their limits.

The audit labels V3 through V6 identify implementation snapshots, independently of
native state format version 13. SHA-256 prefixes identify the retained archives:

| Frozen build | Core archive SHA-256 prefix | Evidence covered here |
| --- | --- | --- |
| Initial RTC implementation | `d7a11ebd06d0747e7a458b68a61b6067` | Counter replay and four focused backend executables |
| Timing V3 | `e1858bc783cc678c8bc1342dbabf3e804` | 8,250 RTC observations after delivery corrections |
| Cleanup V4 | `4e3847613b850a764585579935490ae0e` | The same timing replay and six focused executables |
| Divider V5 | `6561e5fe9961f5edecc8cd0b842787f4` | Guest and peripheral comparisons, six-byte states and targeted regressions |
| Cleaned divider V6 | `a17deb4deea151b66e68e702c10d39d` | Final focused divider/HALT replay and fifteen regression executables |
| Pinned reference | `dd7ab1cec0bd2c4836ba9fa195a425048` | Source-matched reference observations |

The retained receipts include full hashes, matching headers, commands, failures
and input guards. Generated cartridges and injected time isolate clock behavior;
the probes did not change the operating-system clock or modify user saves.

## Policy and battery behavior

The initial frozen RTC archive passed the Linux `rtc`, `battery`, `huc3` and
`tpp1` executables. Policy fixtures exercise the default host mode, invalid and
same-mode calls, provider registration, negative provider observations, lifecycle
preservation and multiplier bounds. A real mode change retains counter and latch
values, clears fractional progress and rebases the active clock. Registering a
provider does not poll it. Multiplier changes retain accumulated phase; zero,
one and values truncating to a zero period select the default model period.
These results precede the later delivery corrections and desktop integration.

A separate reference characterization used 320 battery imports and 120 pure
exports per engine. It covered both supported MBC3 trailers, HuC3 and TPP1,
both policies, stopped clocks, timestamp validation and near-threshold phase.
The old target was a negative control without an Accurate scheduler. Its
requested-mode labels did not establish Accurate-mode support.

| Controller | Established export and import behavior |
| --- | --- |
| MBC3 | Export current live and latched bytes with host time, without advancing a copy. A valid import catches up immediately in either policy, subject to the stop bit, while preserving latch and phase. |
| HuC3 | Export current counters, alarms and the internal timestamp. Import valid fields without immediate catch-up. A future timestamp invokes invalid-clock recovery and disables the alarm. |
| TPP1 | Export current live bytes and MR4 with host time, without catch-up. Valid import preserves phase and defers host-mode catch-up until a scheduled poll; Accurate mode excludes offline time. Independent motor history and the retained fifth clock byte survive import. |

All 320 pinned imports retained their existing fractional phase. This establishes
why the frontend must select its policy before battery import. Native timestamp
zero is retained history, independently of battery-age validation. Three
generated prior-writer V12 snapshots verified that zero survives their native
save/load path; this is not coverage of historical user files. Nine malformed
old-target battery imports rejected transactionally. The earlier paired audit
retained 454 differing state rows, including 54 differing exports; those are
diagnostic rows from the old implementation, not current compatibility results.

HuC3 Accurate behavior was checked across all 60 timestamp second remainders,
four initial counter patterns and 61 shortened-period updates per case. Full
nominal periods covered remainders 0, 58 and 59. A remainder from 0 through 58
does not advance under repeated baseline-plus-one requests. Remainder 59 advances
one minute on every scheduled request and retains that remainder. This matches
the pinned implementation; physical-hardware qualification remains open.

The earlier HuC3 export-repair policy recorded in historical validation is
superseded. The current contract preserves the internal timestamp without
repair. In the retained example, baseline 60030 advances to 60090 at host time
60060. Exporting 60090 and immediately importing at 60060 triggers future-stamp
recovery and disables the alarm. Importing at 60090 accepts it. The old exporter
instead wrote 60060 and preserved the clock on immediate reload. Valid existing
files remain accepted as stored; their bytes cannot identify which policy wrote
them. See [battery saves](battery-saves.md) and [cartridge clocks](rtc.md).

## Bounded counter arithmetic

The counter matrix contains 1,550 cases: 31 raw counter patterns and 50 timestamp
pairs. It includes invalid writable digits, strict day boundaries, rollback,
epoch zero, signed storage boundaries and unsigned wrap. The product replay
agreed with every retained expectation, with three distinct kinds of evidence:

| Cases | Basis for the expected result |
| --- | --- |
| 1,280 | Actual calls to the pinned counter implementation, compared with the independent implementation |
| 162 | Finite loops beyond the execution budget, checked with independent arbitrary-precision arithmetic |
| 108 | Proven nonterminating timestamp histories; the pinned loop was deliberately not called |

The finite algorithm preserves strict whole-day updates before residual seconds,
storage-width wrapping and delayed carry for out-of-range digits. Reduced-domain
exhaustive checks covered 69,632 timestamp pairs. A separate 128-by-128 residue
classification identified 8,128 nonterminating pairs near the unsigned limit.
For the exceptional class, CupidGB leaves the complete clock record unchanged
while consuming a due scheduler interval. This is an explicit bounded runtime
exception, not agreement with a terminating reference result.

The old-core negative control differed in 680 executed cases. A conventional
total-seconds control differed in 155. These controls detect changes to update
ordering and raw-digit behavior. The product replay reused the retained reference
observations and mathematical results; it did not rerun enormous or nonterminating
loops. Undefined-behavior sanitizer covered the private arithmetic prototype and
wrapper, not the frozen Release archives. Counter-only checks do not qualify
clock delivery, native decoding, gameplay or performance.

## RTC delivery and retained differences

Both timing V3 and cleanup V4 matched the RTC observations in all 8,250 generated
rows. The matrix uses Accurate-mode MBC3 clocks and covers direct spans,
instructions, register writes, interrupts, HALT, delayed speed changes, color DMA
and memory-mapped access profiles. It executes no firmware. Common CPU, WRAM and
HRAM controls are explicit; raw display, audio and JOYP startup state remains
visible. Synthetic countdown and freeze combinations need not all be reachable
from a guest STOP instruction.

The earlier delivery negative control differed in 727 RTC rows. Focused DMA and
interrupt fixtures also failed on the frozen pre-correction core with the
expected assertions. Those failures remain negative evidence, separate from
passing corrected-build tests.

RTC agreement does not imply complete timing or machine-state agreement:

| Retained V3/V4 difference | Scope |
| --- | --- |
| 963 elapsed half-unit rows | 547 differences of -1, 170 of -2 and 246 of +1. CupidGB's reported value is elapsed base T-cycles multiplied by two, with whole-dot rounding; it does not measure raw oscillator halves. |
| 12 CPU fixture rows | The CPU differences remain recorded separately from the matching RTC history. |
| 3,546 raw-detail rows | Register and startup-state differences remain visible. These are differing rows, not individual field counts. |

The twelve CPU rows differ only in A. Eight read model-specific wave RAM that
the fixture initialized differently; four read the SGB/SGB2 joypad value from
different cold and post-boot profiles. The inputs and differences remain in the
receipt. They are not normalized away or counted as complete CPU comparisons.

The matrix did not sample DIV or its internal subcycle. A separate guest sequence
exposed a divider scheduling and continuation defect, so these results cannot
qualify the subsequent divider repair. The matrix also does not establish
host-sync polling, other controller counters, commercial gameplay or host speed.

## Cleanup V4 focused checks

Six Linux executables passed against the frozen cleanup V4 archive: `rtc`,
`rtc_state`, `state`, `rtc_timing`, `rtc_dma` and `cpu_timing`. The first private
`state` compilation failed because its local test header had not been copied;
the failure was retained and the remaining tests ran after correcting the
harness. Compiler diagnostics were retained, including conversion warnings in
two test sources. This was not a warning-free or full sanitizer qualification.

V4 removed the unused pending-RTC accumulator and unreachable deferred-finish
plumbing. Its 8,250 observation rows and raw-detail rows are byte-identical to
V3's outputs. The adapted wrapper reports the removed pending field as constant
zero; that column is no longer a measurement. The six-unit result qualifies that
cleanup before the divider repair.

The V4 native-state executable reported 120 round trips, 88 legacy imports,
64 transactional rejections and 48 clock continuations. Those checks used the
then-five-byte V13 extension. They do not qualify the later six-byte extension.

## Current V13 continuation contract

The current format appends six bytes after the unchanged V11 payload and V12
receiver history, before the accessory descriptor: a little-endian 32-bit RTC
phase, one TPP1 motor byte and one divider-subcycle byte. HuC3 keeps its existing
receiver section without a duplicate shared tail. Divider values are restricted
to 0 through 3; motor values to 0 through 3, with zero required for other
controllers. RTC phase remains literal even above the receiving clock period.

Versions 2 through 12 default RTC phase and divider subcycle to zero. A present
legacy TPP1 section supplies motor strength from its stored low status bits;
missing sections and other controllers use zero. The older `timer_counter`
retains its literal value. The fixed SGB V12 fixture digests remain unchanged.

Ordinary and trusted loads validate transactionally while preserving the live
clock policy, period, provider, startup policy, attachments and callbacks.
Restoration does not poll host time. The V5 state executable passed 120 round
trips, 88 legacy imports, 96 transactional rejections and 56 continuations.
Eight continuations begin at divider remainder two, covering both CGB speeds,
both state APIs and printer attached or detached. Invalid values 4 and 255
preserve the live objects and save output. The startup executable also passed
with the fixed historical SGB fixture digests. See [native states](save-states.md).

## Divider repair: V5

A generated CGB cartridge reaches STOP with an interrupt pending, then overlaps
the delayed speed switch with one HBlank DMA block. It uses guest writes to arm
the transfer and switch. A scan of 97 NOP offsets finds the same overlapping
case at offset 32 in the pinned build and both CupidGB builds. The generated
firmware only transfers control to the cartridge; no commercial content is used.

STOP holds the reset divider for four CPU T-cycles. The subsequent 34-T-cycle
transfer therefore emits divider events at T8, T12 through T32 and retains a
two-T-cycle remainder. The previous local bus schedule emitted an extra event
at T34. A following instruction could then shift serial data or reload the
timer too early, despite matching elapsed and RTC clocks.

| Observation after STOP | Before repair: hidden DIV | Repaired and pinned DIV |
| --- | --- | --- |
| Transfer and following fetch complete | 36 | 32 |
| Next NOP completes | 40 | 36 |
| Third following NOP completes | 48 | 44 |

The paired V5 run matches all retained guest divider and peripheral observations.
It also matches all 126 focused domain rows for partial spans, reset, held reset,
camera PHI, delayed envelope and interrupt ordering. With initial divider phases
0, 1, 2 and 3, the timer request after interrupt entry is respectively 4, 4, 0
and 0: an event at T18 completes before acknowledgment, while a later request
survives. The old build differs in 89 domain rows, twelve guest phase rows and
six guest peripheral rows.

All nine focused unit groups pass on V5. Eight fail on the old archive using
its matching headers; the audio-budget control passes on both. The comparison
checks accumulated sample-clock credit, not waveform equality or identical host
queue visibility. Fully frozen calls can still expose output at a different
flush boundary. The run preserved 154 frozen inputs and 93 comparison controls.

The V5 RTC replay remains byte-identical to V4 across all 8,250 observations and
3,546 detail rows, including the recorded differences above. Its separate core
build retained all 62 guarded inputs. Fourteen of fifteen targeted executables
pass, including RTC, native states, serial, APU, camera, DMA, STAT, video bus,
TPP1 and startup. The CPU timing executable stopped at a raw normal-speed versus
double-speed APU-state equality assertion. Its diagnostic is retained separately
from the later corrected fixture.

## Final focused gate: V6

The failed V5 assertion expected every APU byte to match across normal and
double speed. At 8,192 base T-cycles, only the double-speed envelope latch differs:
it holds two while the normal-speed latch is zero. Both volumes and every other
byte match, and complete equality resumes at 8,196 base T-cycles. The pinned
CGB-D/E path defers this event until the following divider period. An independent
source review predicted the exact boundary before inspecting the diagnostic.
Running all thirty original CPU test functions separately found no other failure.

The corrected fixture asserts that exact temporary latch difference, compares
every other byte without changing the live states, checks clearing on the next
double-speed M-cycle, and retains complete equality at every other iteration.
V6 also removes the obsolete deferred-edge descriptor and unused PPU HALT
wrapper. A due timer event now completes inside its span before the RTC provider
is called. No extra pending history was introduced.

All fifteen selected Linux executables pass on the V6 archive: `divider_phase`,
`rtc`, `rtc_timing`, `rtc_dma`, `rtc_state`, `cpu_timing`, `state`, `serial`, `apu`,
`camera`, `dma`, `stat`, `video_bus`, `tpp1` and `startup`. Their live core,
header and test inputs remained unchanged. The extra-warning harness retains
address-conversion diagnostics from the APU test source; this is not a
warning-free qualification.

Fresh V6 paired runs again match all 126 divider-domain rows and the complete
retained guest sequence. The nine selected focused groups pass and retain the
same meaningful V4 negative failures. A separate HALT matrix covers 96 cases:
four compared monochrome profiles, CGB at both speeds, first and steady polling,
four divider remainders and both IME values. All 288 observations match, with
zero polling-contract failures. The regression now covers that matrix in the
ordinary `divider_phase` executable as well.

The final RTC replay still matches all 8,250 RTC rows and is byte-identical to
V4 for every recorded observation and detail. The 963 elapsed, twelve CPU-fixture
and 3,546 raw-detail differences remain unchanged. V6 retains the six-byte state
contract and all its continuation checks. These are source-bound core results;
they do not replace native desktop or commercial measurements.

## LCD startup coordinate qualification

A separate probe on the frozen V6 core qualifies the four-dot raw phase
difference retained by the earlier startup comparison. Five generated normal-speed
CGB cases produced 3,652 observations per engine, with zero mapped LY/STAT/IF
difference intervals and equal elapsed clocks. Four cases enable the LCD through
a mapped host write with different STAT sources. The fifth executes a guest
`LDH (FF40),A` instruction.

| Edge | Direct enable, both engines | Guest instruction, both engines |
| --- | ---: | ---: |
| LY becomes 1 | T452 | T460 |
| STAT reports mode 2 | T453 | T461 |
| LY becomes 144 | T65659 | T65667 |
| STAT reports mode 1 and VBlank IF is requested | T65662 | T65670 |

The guest write occurs eight T-cycles into its instruction. That offset is not
subtracted from either observation stream. After direct enable, both engines
report LY144, STAT81 and IF1 at T65800, while their internal dot coordinates are
144 and 140. The pinned scheduler includes a four-dot line prefix before visible
mode 2; CupidGB uses a later line origin. Its 452-dot startup boundary therefore
produces the matched observable edges and remains unchanged.

Both compiles and probes succeeded, with all 95 protected and 95 frozen inputs
unchanged. An independent review checked the event-interval comparison and the
guest offset handling. The initial twelve-T-cycle instruction is observed only
before and after execution; subsequent spans are sampled every T-cycle. These
fixtures qualify the listed mapped edges with zeroed rendering memory. They do
not establish pixel, PCM, other-model, double-speed or commercial-game equality.
The earlier raw phase observations and remaining PCM differences are retained.

## Qt lifecycle persistence

Integrated review found that the frontend's dirty-generation shortcut also
skipped battery commits at session boundaries. Natural clock progress does not
change that generation, so an Accurate MBC3 clock could advance during play and
lose that history on close. A focused Linux Qt negative control built successfully
and failed at the generated MBC3 close fixture. The same fixture passed after the
production correction.

Reset, reload, cartridge replacement, close and destruction now commit current
battery history regardless of the dirty generation or cached-byte equality.
Ordinary autosave polls keep their existing gate. Firmware and a different
candidate's battery are validated before the current clock is exported. Resetting
the same cartridge instead commits live history before importing it, replacing
stale disk bytes. A rejected commit also keeps an active audio recording open.

The final focused workflow passed in 9.29 seconds. It covers fifteen natural
progress cases across MBC3, HuC3 and TPP1 and the five session endings, clean
autosave polls, creation of a missing save whose bytes match the cache, TPP1
status-only changes, stale-file replacement and rejected writes. Rejected close,
reset, reload and replacement retain the current core, native state, cache,
session identity, recorded-frame count and active recorder. Existing RTC settings
and policy-ordering workflows also pass. All 208 product inputs and the core
archive remained unchanged during each focused run.

The first attempted negative control timed out before reaching the battery
fixtures. Its test timer clicked a button synchronously, then blocked inside the
resulting validation warning. Queuing the click lets the timer dismiss that
warning and enforce its deadline. This fixture failure is retained separately
from the later intended persistence failure. These generated offscreen workflows
do not establish native audio quality or commercial gameplay.

## Integrated builds and regression gates

The first integrated Windows, Linux and sanitizer builds stopped because three
PPU regression calls still used a removed HALT helper. All 208 guarded product
inputs remained unchanged during those failed builds. The fixtures now call the
current hardware HALT scheduler, keep the before/after-midpoint VBlank and delayed
HBlank assertions, and check four elapsed T-cycles. Their complete-state control
also advances the hardware clocks. This repair changes test code only.

After that repair and the reviewed Qt lifecycle correction, the complete builds
and configured suites pass on the same 208-input source freeze. Its manifest
SHA-256 is `9854e7017cfe1eed43fc51cf4a4a8cf950d80cf969b4df934c5addcf692dcc7a`.
No tests were skipped and no guarded input changed during these gates.

| Gate | Result | Configuration |
| --- | --- | --- |
| Windows desktop | 56/56 | Visual Studio 2022 x64 Release, Qt 6.8.3 |
| Linux desktop | 54/54 | GCC/Ninja Release, Qt 6.8.3 under WSL |
| Linux core sanitizers | 35/35 | Debug, ASan leak detection and UBSan halt-on-error |

The desktop inventories include all five new RTC/divider executables, expanded
CLI checks and the Qt workflows. The latter pass in 10.94 seconds on Windows
and 9.65 seconds on Linux. Separate installed native checks qualify deployment
and real audio; the CTest results alone do not establish those workflows.

The Linux Release link retains a GCC `-Wstringop-overflow` diagnostic for an
inlined HRAM write. Its reported unsigned offsets correspond to addresses
`6000` through `7FFF`, which return unconditionally in the earlier cartridge
branch. The write also checks `FF80` through `FFFE`, restricting its actual index
to 0 through 126 in a 127-byte array. Independent source review found no reachable
overflow. The warning is retained, and this build is not described as warning-free.

Formatting and whitespace checks pass for the integrated changes. All 185 project
C/C++ source and header files remain below 2,000 lines; the largest is the
1,976-line native-state regression file. Third-party and generated files were
not reformatted.

## External ROM verdicts

The final Windows runner repeats the retained 243-case corpus with explicit seed
zero and the same local firmware, model selection and verdict protocols. It
reports 242 explicit passes and one bounded DMG OAM timing logger. The logger
reaches its frame limit with result byte `80`; it remains inconclusive.

| Group | Explicit passes | Bounded |
| --- | ---: | ---: |
| Hardware acceptance | 119 | 0 |
| Audio | 24 | 0 |
| CPU instructions | 22 | 0 |
| Mapper tests | 56 | 0 |
| OAM | 7 | 1 |
| Timing | 14 | 0 |

The runner SHA-256 is
`30f5578975c8ddb3e2f2e6f01a502cedc4240bcbb5adfad752a3db3e259ceb16`.
All 151 protected original files, the frozen runner and suite, and the 208 source
inputs remained unchanged. Every reported seed matches zero. These verdicts do
not establish compatibility with every cartridge or resolve the bounded case.

## Commercial native and battery continuation

Sixteen runs pass using private MBC3 and HuC3 CGB cartridges, both clock policies,
both Windows and Linux builds, and native import in both cross-host directions.
Each run starts from seed zero and a controlled provider whose timestamp follows
elapsed guest cycles from a fixed epoch. This supplies equivalent host-time
observations without changing the operating-system clock.

After 3,000 nominal guest intervals, capture waits for mode 3 and at least seventeen
queued stereo frames. Continuation compares CPU and clock/peripheral records at
every completed step, emitted PCM, presentation buffers and twelve full native
encodings over another 600 intervals. Each run compares 443,029 stereo frames and
600 presentation buffers. The MBC3 runs execute 7,048,923 continuation steps;
HuC3 executes 6,921,295. All four controller/policy groups agree across the local
and cross-host paths. These commercial captures use divider remainder zero;
the generated state tests separately cover remainder two.

Separate battery exports are imported into fresh instances after a controlled
75-second offline gap. SRAM is byte-identical, policy and provider ownership are
retained, and export leaves native state and dirty generation unchanged.

| Controller and policy | Before export | Immediately after import | After one scheduled interval |
| --- | --- | --- | --- |
| MBC3, host | 00:01:00 | 00:02:15 | 00:02:15 |
| MBC3, Accurate | 00:01:00 | 00:02:15 | 00:02:16 |
| HuC3, host | Minute 1, day 4096 | Minute 1, day 4096 | Minute 2, day 4096 |
| HuC3, Accurate | Minute 0, day 4096 | Minute 0, day 4096 | Minute 0, day 4096 |

MBC3 imports the offline time in either policy while preserving its latch. HuC3
defers updates; the Accurate row retains second remainder zero and exhibits the
separately characterized minute-counter behavior. None of the eight commercial
HuC3 observations invokes future-stamp recovery, so that branch has generated
coverage only. The stored HuC3 Accurate baseline predates export by sixty seconds;
that retained calendar history is separate from the subsequent offline gap.

All 534 protected originals, 229 frozen files, four absent adjacent-save paths
and both harness executables retain their recorded identities. The final summary
SHA-256 is `2382b6aea1fb5fde5696028f9fb8fd1f047c6c385b3affb5cc513feb03a69590`.
Visual review of eight start/end captures shows the MBC3 introduction advancing
through dialogue and the HuC3 interior scene progressing. They establish neither
complete gameplay nor a game-driven save/Continue workflow. These core codec
checks are separate from Qt lifecycle persistence and host performance.

## Installed native workflows

Fresh Windows folder deployment and Linux installation pass the native Qt
workflow with a real audio device required. The runs finish with exit zero in
18.47 and 21.28 seconds respectively. Loaded Qt and SDL paths match the expected
runtime. All 295 Windows installed files, 22 Linux installed files, 208 source
inputs and protected private inputs remain unchanged.

The workflow covers RTC settings, policy-before-battery ordering, session
transitions, native loads, rewind and debugger Backstep. It separately requires
real sink progress while checking playback after save/load and rewind, and
reentrant cartridge close during host audio writes. Controlled cold-memory
fixtures stop their timers and suppress audio for exact memory comparisons;
they are not the real-device portion of this check.

Each platform retains eleven captures, including the new RTC settings view.
Visual review of both RTC dialogs and representative debugger/memory views
found readable controls without overlap or clipping. The combined native receipt
SHA-256 is `84db380e331b5be81564e903b1a9feab7a9edf6197d0496adf7047faacd46f56`.
This establishes the tested local deployments, not clean-machine installation,
a portable Linux bundle, audible quality or sustained speed.

## Sustained desktop samples

Four sequential native samples exercised the final RTC/divider build on Windows
and Linux/Xcb. Each ran for 120 application seconds with normal audio, a fixed
power-on seed of zero, post-boot startup, isolated default settings, a preserved
input trace and a fresh private save path. The measurements below exclude the
first five application seconds. No other builds, emulator probes or corpus scans
ran during the coordinated window.

| Platform and model | Guest speed after warmup | Paint median | Paint p95 | Paint p99 | Longest paint interval | Largest sampled resident set |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows DMG | 100.0015% | 16.7357 ms | 17.6831 ms | 18.2144 ms | 27.0744 ms | 104.10 MiB |
| Windows CGB | 99.9895% | 16.7438 ms | 17.7371 ms | 18.4839 ms | 28.7165 ms | 221.60 MiB |
| Linux DMG | 100.0020% | 16.6193 ms | 24.2175 ms | 25.2387 ms | 28.0820 ms | 122.75 MiB |
| Linux CGB | 100.0020% | 16.5855 ms | 17.5496 ms | 17.8683 ms | 20.1577 ms | 239.08 MiB |

All four processes exited successfully, applied fourteen DMG events or twenty-two
CGB events, and reported no execution failure, illegal-opcode diagnostic or audio
error. Windows DMG recorded one sink-idle event before the five-second warmup
cutoff; no run recorded one after warmup. Real audio sinks processed samples in
all four runs. These observations do not establish audible quality.

The final DMG captures show Game Over. The final CGB captures show the course
scene with the player, pipe and enemy visible. The runs do not establish continuous
active play or compatibility across other games and mappers. Both CGB runs wrote
a fresh 8,192-byte private save with SHA-256
`575c35ca15de7e01202dded3174fa633dd121a491d22261f33d19a073f90ae17`.
Commercial save reload was not exercised by these timed samples. The separate
native workflow verifies generated save/load and rewind transitions, and the
RTC lifecycle tests cover clock/status persistence across session boundaries.

Guest percentages and paint intervals use the application's monotonic clock.
Linux also retained `CLOCK_MONOTONIC_RAW`: DMG took 126.5208 raw process seconds
versus 121.9348 adjusted process seconds, and CGB took 126.7896 versus 121.2712.
Sampled adjusted/raw interval ratios ranged from 0.916658 to 1.001163 for DMG
and 0.916666 to 1.000000 for CGB. These process intervals include startup and
shutdown and differ from the application's post-warmup interval. The near-100%
application results therefore do not establish independently measured real-time
full speed. Widget paints do not measure physical display refresh. Windows CPU
counters remain unqualified, and resident memory is the largest sampled set
rather than a guaranteed operating-system peak.

The Windows captures contain a 320 by 288 guest image in a 483 by 401 window.
Linux contains a 160 by 144 guest image in a 320 by 307 window despite the isolated
default scale of two. This startup sizing observation remains a separate UI
follow-up; the frozen samples were preserved. These are not equal-render-size
platform comparisons and do not establish a performance improvement over another
build.

All 208 frozen product inputs, installed runtime files, protected original ROMs,
traces and saves, and measured executables remained unchanged. The performance
summary SHA-256 is `14da6accb5be6eb7c04dd9f8042cd6aa2c65067cdd7c1f02137bfd4861ab4823`.
It binds the four host summaries, application reports, raw resource samples,
source/installation guards, input hashes and visual review to the source manifest.

## Remaining scope

These results do not establish full RTC feature parity, every hardware model,
physical clock behavior, audible PCM equivalence or compatibility with every game.
The Linux startup-size defect and host-clock qualification remain explicit, along
with the hardware and desktop requirements in the [parity checklist](parity.md).

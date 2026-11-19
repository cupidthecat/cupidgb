# Link play and LCD line validation

CupidGB now connects two local cartridge windows through a shared serial and infrared
session. The core schedules whole instructions against the consoles' physical clocks.
The Qt frontend owns the windows, input roles, playback rate and separate output and
storage paths. [Link play](link-play.md) describes the controls and connection limits.

This checkpoint also repairs writes to the read-only LCD line register. The repair
was found while investigating four cartridge inputs that stop on illegal instructions.
It is committed separately as `a3787c3a8ff60fad3de6ee8745c7bc5e398da1cb`.

The behavior reference remains revision
`ea667ad79aa28409f034b1fd6da7304e1844750d`. The preserved product baseline is
`508776c21b4a022eb9dd8f8286ffc2ecd70993c5`. Comparison programs and fixtures are
private, independently built tools. Reference code, game images, firmware, saves and
captured commercial content are excluded from the product and public commits.

## Complete local gate

| Configuration | Result |
| --- | ---: |
| Windows Release, MSVC 19.43 and Qt 6.8.3 | 96/96 passed |
| Linux Release, GCC 13.3 and Qt 6.8.3 | 94/94 passed |
| Linux Debug, ASan/UBSan and leak detection, Qt disabled | 68/68 passed |

No registered test was skipped or disabled. The desktop gates run the Qt workflow
groups offscreen; those results alone do not establish native audio or presentation.
Linux development builds run under Ubuntu in WSL. The sanitizer gate uses the same
core as the final desktop builds; later corrections affect only Qt code and tests.

The four Qt groups cover desktop, storage, SGB startup and link workflows. Link checks
exercise connect/disconnect, independent storage, duplicate-path rejection, shared
pause and rate controls, frame advance, debugger stops, reset/reload/state replacement,
printer exclusivity, keyboard settings, two controller roles, transient edges,
background ownership, menus, modal capture, rumble and recording failures.

The controller backend test also checks shared SDL subsystem and event ownership.
An inactive window can poll first without consuming another window's brief button
press. A window reopening delivery discards only its own stale button edges. Device
metadata and unrelated SDL events survive. A motor request from an older client cannot
cancel a later owner's request when the older client expires or closes.

Integrated checks exposed overlapping comparison fixtures that used one cartridge's
save path in two windows. Those fixtures now use byte-identical private copies with
separate storage; the product's duplicate-storage guard remains active. Further checks
caught duplicated controller menu navigation and a held-stick regression during an
MBC7-to-ordinary-cartridge change. Navigation now belongs to the menu or dialog's
window, including after queued delivery. A new cartridge explicitly receives its
classified held controls after setup and before its first rewind snapshot. Reset and
reload retain their existing contact history. The original failed runs and diagnostic
values remain in the private evidence.

The final native `all` workflow also passes with real audio on both hosts: Windows
completes in 25.611 seconds and Linux in 28.183 seconds, measured by the Windows
process wrapper. The Linux run uses the X11 Qt backend through WSLg and waits for the
real desktop audio service before launching. These are workflow durations, not
emulation-speed measurements. The controller fixture waits for actual window
activation and settled dialog teardown before checking exclusive input ownership.

The final build-input inventory covers 303 files. Its content digest is
`8ec47353da13c36ffde8783f78aa13acb4a5a1f26eb1f6e61ef4545ce837bc15`.
Documentation is excluded from that digest. The only source change after the earlier
complete desktop gate is the link workflow's focus setup and failure diagnostics;
both complete desktop gates were rerun after that correction.

## Native linked gameplay and performance

Four serial runs each request 60 seconds of paired commercial gameplay with normal
audio: a GB two-player puzzle game and a GBC VS platform game on Windows and Linux.
They use fixed seed `0x26154`, post-boot entry, separate cartridge/save copies and the
same input traces on both platforms. Builds and other test workloads finish before
measurement. Each process uses the product core and Qt sources with a private driver
that opens two real windows, connects them through the public workflow and records
both players independently.

All four processes finish without execution failure or timeout. Both audio sinks
remain active, every scheduled input event is applied, and final battery flushes
succeed. Original ROMs, saves, trace files, driver sources and executables retain
their hashes and modification times. Visual review confirms distinct two-player
playfields with advancing pieces in the GB game, and a shared race with distinct
player positions, actions and coin counts in the GBC game on both hosts.

Guest speed and widget measurements below exclude the first five seconds. Ranges
span the two players. CPU use covers the complete measured driver interval and is
expressed as a percentage of one logical CPU; memory is the pair's peak resident set.

| Host and pair | Guest speed | Widget paints/s | Paint interval p95 | Mean CPU | Peak memory |
| --- | ---: | ---: | ---: | ---: | ---: |
| Windows GB | 99.988–100.009% | 59.934–59.940 | 17.830–18.623 ms | 7.94% | 51.55 MiB |
| Windows GBC | 99.833–99.882% | 59.894–59.905 | 18.159–24.944 ms | 12.50% | 55.53 MiB |
| Linux GB | 93.928–93.956% | 56.319–56.336 | 27.116–27.527 ms | 36.68% | 73.80 MiB |
| Linux GBC | 93.624–93.681% | 56.168–56.184 | 27.276–27.641 ms | 53.25% | 75.97 MiB |

No audio error is recorded. After warmup, each endpoint records one idle event on
Windows, two for Linux GB and three for Linux GBC. These runs therefore do not
establish uninterrupted audio. Widget paints include capture-triggered repaints;
they do not measure physical display refresh. Captures are requested at ten-second
intervals, and their actual timestamps are retained. A short successful match does
not establish every game's compatibility, long-term saving or perceptual audio quality.

Linux remains below the required speed against independent host time. During its
GB run, the driver's adjusted clock advances 56.399 seconds while its raw clock
advances 60.045 seconds. The GBC ratio is similar. A fresh START/STOP handshake
brackets 20.000574–20.001831 Windows performance-counter seconds; Linux raw time
reports 20.001208–20.001210 seconds, while adjusted monotonic time reports 18.315272
seconds. The two IPC round trips total about 1.26 ms, far below the discrepancy.

The existing [host-clock investigation](validation-host-clock.md#independent-linux-audio-diagnosis)
also reproduces slow drain in a standalone audio client on this WSLg endpoint and
identifies its adjusted-clock scheduling. Current native audio processing follows
that slower clock. This checkpoint leaves host services and clock configuration
unchanged. Correct sustained Linux speed still needs a corrected host audio/clock
path or qualification on another Linux environment. The measured limitation is not
hidden by disabling audio, skipping guest work or relabeling the elapsed clock.

The private measurement summary is SHA-256
`fae92ece791b0d74541d146717c93f95a908cbc628cda4996e24ddd89d9acafa`.
The current cross-host clock receipt is
`201c75badfab8d94fef5e64dbb642829666efde62be4b26817270237f4d83496`.
Visual confirmations are separate hashed reviews of the captured images; a successful
process exit alone does not mark commercial gameplay as observed.

## Paired core comparison

All 38 scenarios matched every recorded field on Windows, and all 38 matched on Linux.
The frozen input set contains 79 hashed files. Scenarios include normal and fast
serial clocks, DMG/CGB/SGB/SGB2 combinations, internal and external clock choices,
state loads, resets, endpoint replacement, reconnection, infrared changes and an
executed CGB speed switch.

The observations include exchanged bytes, serial control and interrupt registers,
CPU position, selected RAM, elapsed half-dots, instruction counts, scheduler choices,
zero-time progress and infrared state. Expected transfer outcomes are checked
separately from equality between engines.

The cable samples the peer before shifting it. An endpoint's launched output bit
survives reset, state loading and reconnection independently of its current serial
data register. Resetting or replacing one console retains the pair's relative lead.
A finite speed-switch countdown can advance through a step with no oscillator time;
other non-progress and overflow conditions are rejected.

Mixed model clocks are compared by cross multiplication, preserving fractional
physical time without a floating-point accumulator. SGB startup can hold guest
instructions while its presentation clock advances. The connection keeps those
clock domains distinct.

Native state format 22 is unchanged. A single-console state does not capture the
partner's timeline. Connected rewind and Backstep remain unavailable; loading a
state during a game transfer can still disrupt that game's protocol.

## Read-only LY behavior

Writes to `FF44` now leave LY, STAT mode and coincidence state unchanged apart from
normal bus-time progress. Existing memory observation and deferred display
synchronization still occur before the byte is ignored.

The tracked STAT regression covers 1,536 host writes and 1,001 guest instruction,
model and phase combinations. It checks all six current model tags, color double
speed, visible LCD phases, VBlank, the shortened LY=153 interval and disabled LCD
operation. Guest routes include immediate stores, register-indirect stores,
increment/decrement and CB read/modify/write instructions, followed by 128
continuation instructions. The new regression failed against the preceding core.

The generated reference matrix matched all 858 combinations. Each combination also
checks every host-written byte, totaling 219,648 ignored host writes per engine, and
256 continuation instructions after the guest write. The reference revision does
not expose an enabled DMG0 enum, so that profile has tracked regression coverage
without an equivalent reference execution in this matrix.

## External mapper, timer and serial tests

The external fixture revision is `31510e12eea6286d36eea060a6adde755e1067aa`.
The same frozen corpus runs against the preserved baseline and this checkpoint.

| Group | Windows | Linux |
| --- | ---: | ---: |
| MBC1: 13 fixtures on DMG and CGB | 26/26 | 26/26 |
| MBC2: 7 fixtures on DMG and CGB | 14/14 | 14/14 |
| MBC5: 8 fixtures on DMG and CGB | 16/16 | 16/16 |
| Timer, interrupt and HALT: 20 fixtures on both models | 40/40 | 40/40 |
| Serial: two post-boot cases and one shared-firmware case | 3/3 | 3/3 |
| Total explicit success results | 99/99 | 99/99 |

All 198 executions report their explicit success protocol. Every deterministic
observation matches the baseline; host elapsed time is excluded from that equality
check. The CGB rows execute monochrome-compatible cartridge programs. They do not
constitute native-color instruction coverage. The external MBC5 fixtures do not
establish ninth ROM-bank-bit coverage; the tracked cartridge regression has a
separate bank-511 marker check.

The serial firmware comparison holds the private boot bytes identical. It does not
establish the provenance of those bytes. These results cover selected controller and
timing behavior, with no throughput or every-mapper claim. Broader generated mapper
coverage and limits are described in [hardware support](hardware.md#cartridge-controllers)
and [cartridge validation](validation-cartridge-storage.md).

## Original MBC5 guest corpus

An independently written guest exercises all 512 MBC5 ROM-bank selections on DMG
and normal-speed CGB. It executes mapper writes and marker reads from WRAM. Four-byte
markers distinguish every physical bank, including banks 256 through 511. Images with
512, 256 and 32 physical banks also check address-line aliasing on smaller populations.

Each run executes 55,937 commands. The cases cover both register-write orders,
intermediate mappings, all 256 raw high-register values, every address in both bank
register ranges and every remaining address from `0000` through `7FFF`. Each run
also restores a native state captured with the ninth bank bit set and repeats a
64-command suffix.
The suffix includes single-register writes that depend on the retained bank latch.

| Build | Model/image combinations | Commands | Result |
| --- | ---: | ---: | --- |
| CupidGB Windows Release | 6 | 335,622 | All guest assertions passed |
| CupidGB Linux Release | 6 | 335,622 | All guest assertions passed |
| Behavior revision `ea667ad79aa28409f034b1fd6da7304e1844750d` | 6 | 335,622 | All guest assertions passed |

The 18 runs execute 1,006,866 commands in total. All 12 comparisons between CupidGB
and the behavior reference match every recorded field, including observation hashes,
instruction counts, elapsed half-dots and native-state continuation. The 18 state
replays compare 1,152 command pairs. SHA-256, byte length and modification-time guards
agree before and after execution for all 63 protected inputs, libraries, programs
and source files.
The reference sources also match the retained library-build manifest.

All emulator processes exit successfully. The collector initially rejects CupidGB's
output because its ROM-load log precedes the JSON result. A separate strict analysis
accepts exactly that known log line followed by one JSON object, then checks process
status, configuration, case counts and every comparison field. The original failed
collection and raw output are retained. No emulator execution was repeated for this
parsing correction.

| Private record | SHA-256 |
| --- | --- |
| Original execution receipt | `fdda373e7601ffe816eca4213ce5acf77048ddc75016278783fbf4c7abc75f7e` |
| Strict retained-output analysis | `0aa3f49fd191c6537ab424ee73c025c77f1d13f3f268e239785a4122f7d6506b` |
| Generated ROM manifest | `d0be5ced6527fad27358299f41b5e7ab4de16ff06def6999390a2c5945c419ce` |

This corpus uses MBC5 without cartridge RAM and the original minimal comparison
bootstraps. Its coverage is ROM banking and native-state continuation. Cartridge RAM,
rumble, commercial firmware startup and commercial gameplay require their own evidence.

## Local cartridge scan and trap investigation

The final scan covers 880 library entries, or 878 distinct cartridge inputs: 528 on
the CGB profile and 350 on DMG. It uses fixed seed `0x26154`, post-boot entry and a
420-frame bound. Of the unique cases, 874 reach that bound and four stop on an illegal
instruction. All recorded deterministic observations match the preserved baseline.
The original inputs retain their bytes and modification times.

The library includes ROM-only, MBC1 and multicart, MBC2, MBC3, MBC5 and rumble, MBC7,
HuC3 and Camera inputs, plus one unknown header. It has no identified HuC1, MMM01,
extended-MBC3 or TPP1 commercial input. A short startup scan cannot establish active
gameplay, successful saving or complete compatibility for these cartridges.

Twelve diagnostic probes investigate the four trapped inputs. Controlled executions
with identical startup memory and firmware match the reference's terminal CPU
observations and complete final 256-instruction histories. The original default-entry
halt PCs and opcodes are unchanged. One stack write into `FF44` previously left LY at
`00`; the corrected result is `92`, matching the controlled reference execution.

Several trapped inputs have size or checksum irregularities, but one has valid logo,
header and global checksums. The four cases are not classified collectively as damaged
files. Their matched stops are diagnostic evidence, not successful-gameplay results.

## Retained core evidence

| Record | SHA-256 |
| --- | --- |
| Consolidated final core comparison | `f3824bbd1ec10449b8eb20788569e5694f6ed928cb1107aa2d6937a35675b03f` |
| Generated LY comparison | `00ec990a8fc437e98f8b51137d53cb825aae80eb2062e98170cdbeb5720b4590` |
| Windows final external corpus | `4a2a4ce7fc99dcea0a4bffe53af1a5a3d103409c11a9a1cb8c5642d931ca617f` |
| Linux final external corpus | `72152b577dcd359d2da871a314d00cb43415efb12bd266901931269b3b6b1512` |
| Four controlled trap comparisons | `db88f2844e4aa5c6568810dd48471df4f0b5292518c1c1bc890d947ddc2b983d` |

The private records retain commands, input identities, compiler output, observations
and failed attempts. Hashes identify those records without publishing protected
cartridge or firmware content.

## Remaining scope

Local two-console cable play does not establish every multiplayer title, every
hardware revision or physical infrared transport. Network connections, additional
cable participants and keyboard accessories remain pending. Physical controllers,
motors, cameras and motion sensors need separate qualification.

MBC6 still lacks its split windows and flash protocol; TAMA5 has no implemented
controller. Neither has a complete implementation in the pinned behavior source.
Independent startup firmware, remaining model variants, wider graphics/audio
qualification and other desktop workflows remain on the [parity checklist](parity.md).
Issue 5 remains open and PR 16 remains draft until the complete acceptance criteria
are met.

# Startup validation, 2026-11-16

This checkpoint covers cold memory, firmware entry, startup controls, and
continuation from the modeled SGB handoff. It does not establish complete
hardware, firmware, audio, or game compatibility. The comparison revision and
remaining requirements are recorded in the [parity checklist](parity.md).

Cold initialization now generates model-specific WRAM, HRAM, OAM, wave memory,
and color palette contents. The core API, Qt frontend, and runner can select a
fixed 64-bit seed or report an automatically chosen seed for replay. Zero is a
valid seed. Seed policy and the last realized generation belong to the host
session; native loading restores actual guest memory while retaining that host
metadata. The V12 wire layout is unchanged.

Initial firmware startup reconstructs the same cold generation after cartridge
and battery loading. It preserves cartridge storage and host registrations.
The approximate CGB post-boot profile clears physical WRAM bank 2 in both color
and monochrome compatibility modes. Executed firmware receives the original
cold bytes and performs its own writes. Color output masks the unused RGB555
high bit while raw palette memory and state files retain all eight stored bits.

## Cold-memory comparison

The final core was compared with the pinned revision across five qualified
profiles: grouped DMG-B, Pocket, SGB NTSC, SGB2, and CGB-E. Each engine contributes
256 seeds per profile, or 1,280 cold generations and 29,780,736 observed bytes.
The probe observes memory at boot-overlay entry before executing firmware.

All declared criteria pass across 52 populated model/domain classes, including
304 nonzero per-bit class comparisons, 342 byte-popcount histogram-bin
comparisons, 14 exact-zero classes, and eight full-byte-domain uniform classes.
The largest corresponding bit-mean difference is 0.030692, within the declared
finite-sample margin. The retained pre-change control fails 848 individual
conditions under the same criteria. It has no seed API and repeats its old
initialization; its repetitions are not independent random generations.

The generators are independent. These results compare modeled distributions,
not identical random sequences. They do not establish default entropy quality,
all random correlations, physical-hardware measurements, DMG0 behavior, or
other CGB revisions. The audited SGB startup profile itself remains unverified
on physical hardware. All 115 protected original control files retained their
hashes and modification times.

The final core archive has SHA-256
`4475967a317f04b301cae90765d8542a8dd690beb03df20276a2cda4fba19a99`.
The distribution receipt has SHA-256
`1e72f8024d1359af70e07f980024b19c3bf849a98e65378fde2afa348e5db9ee`.

## SGB startup and older states

The continuation checks exposed an existing SGB/SGB2 post-boot defect:
LY started at 255, and the first pixel update copied that value into an internal
scanline field that must remain below 154. A subsequent native save correctly
rejected the invalid state. Measured firmware handoffs establish VBlank on line
144 at dot 200 for the tested profiles. The HLE profiles now start there.

The regression covers immediate and first-step saves, six checkpoints through
12,000 steps, the return to visible lines, and two new VBlanks over 144,000 base
cycles. Restored execution produces byte-identical states while retaining fixed
or automatic host seed policy. The frozen old core fails the new startup
assertion. State validation remains strict.

Older untouched SGB and SGB2 files need the same correction when loaded. The
loader matches the initial CPU/LCD signature at guest cycle zero after strict
validation, then changes only LY, STAT, and the display counter. The divider's
subphase depends on cartridge header packets and remains as saved. It also
preserves memory, host callbacks, attached printer ownership, seed policy,
battery generation, and host observation counters.

Two generated fixtures match every byte written by the frozen prior writer.
Eight combinations cover both models, ordinary and trusted loads, and fixed
or automatic host seed policy. They check immediate saves, the first step,
16 steps, and two frames of continued execution. Eighteen nearby signatures
remain byte-identical through loading. These are generated prior-writer
fixtures; they do not establish coverage of historical user saves.

## Cold registers and palette access

Firmware comparisons exposed a WRAM selector readback defect. Writing FF70
with zero selected physical bank 1 but changed the readable selector to one.
The corrected register keeps those values separate: zero reads as `F8`, while
one reads as `F9`. Both select the same physical bank. At cold firmware entry,
the register reads `00` until a write sets its unused upper bits.

The selector regression covers all 256 writes in both HLE and boot contexts,
fixed and selected banks, echo reads and writes, a zero-selector native round
trip, and compatibility-mode write gating. The existing I/O array and effective
bank field already preserve both values in V12.

Monochrome boot entry clears both palette arrays. During boot, data ports FF69
and FF6B address byte zero; inaccessible index ports cannot enable auto-increment.
Removing the boot overlay hides the data ports and ignores subsequent writes.
The regression checks both cold entry and memory-preserving re-entry on DMG,
Pocket, SGB, and SGB2. The unchanged isolated cold-register tests fail on the
preceding core and pass on the final archive. Their final receipt has SHA-256
`eaa18094fc9160cc9fa99b48543aad6ac66c5ab621969f64538e1671658bf02e`.

A separate paired matrix covers six model/header combinations and four seeds:
24 pairs, 48 engine observations, and 216 paired phases with 129 mapped
registers per phase. It includes cold entry, repeated reads, initial writes,
mode selection, and direct boot-overlay release. All 256 previously recorded
deterministic mismatch observations now agree. No deterministic mapped
differences remain in this matrix.

Random wave and palette values remain separate observations. Stable repeated
reads and unchanged raw I/O bytes do not prove the absence of internal joypad
observation or PPU/APU synchronization effects. This matrix executes no guest
instructions; its direct FF50 write is distinct from an executed boot handoff.

## Executed firmware

The final firmware matrix uses one local DMG image and two distinct local CGB
images, with monochrome and color cartridges and four seeds per profile. All
20 paired cases, or 40 fresh engine observations, reach PC `0100`. CPU register
files, mapped interrupt observations, instruction counts, base-cycle counts,
and generated audio-frame counts agree in every pair.

Physical CGB WRAM bank 2 is byte-identical between engines for each image and
cartridge mode. Both images first clear all 4,096 bytes. One image later writes
scratch data, leaving 714 nonzero bytes in compatibility mode and 697 in color
mode; every final nonzero byte has a verified firmware write. The other image
leaves the bank zero. Write provenance was measured in an earlier frozen probe;
the final archive reproduces those complete handoff bytes in all four seeds.

The remaining CGB display-phase difference is consistent across the matrix:

| Image and mode | Reference PPU dot | CupidGB PPU dot |
| --- | ---: | ---: |
| Local image A, compatibility | 144 | 140 |
| Local image A, color | 172 | 168 |
| Local image B, compatibility | 148 | 144 |
| Local image B, color | 180 | 176 |

Equal CPU cycle totals do not resolve this four-dot difference. The approximate
SGB handoff and its historical boot controls are separate from this final
DMG/CGB firmware matrix; SGB host introduction behavior remains incomplete.

Both engines use 44,100 Hz audio and accurate/enabled high-pass filtering, with
reference interference disabled. PCM still differs: peak absolute deltas range
from 9,111 to 9,500, and RMS deltas from 1,815.924471 to 2,441.082255 across the
five firmware/cartridge profiles. Equal sample-frame counts establish scheduling
agreement for these runs, not matching audio reconstruction. Each engine exactly
reproduces its own earlier frozen PCM.

The compact firmware receipt has SHA-256
`1eff1b179d8f96f992a01224bc1cf02b9748ba53c9b3bfff2754b84428ccb592`.
Protected source inputs, archives, original ROMs, boot images, saves, and earlier
observations retained their recorded hashes and modification times.

## Frozen startup regression gates

The V7 source manifest covers 199 build/test inputs and has SHA-256
`7029ba8702be298d65c9fa53ec8182afc91e18ecc3f61b2100bf29dc42d92715`.
Every input retained its hash throughout the following builds and tests.

| Configuration | Result |
| --- | ---: |
| Windows MSVC Release with Qt | 51/51 pass |
| Linux GCC Release with Qt | 49/49 pass |
| Linux AddressSanitizer and UndefinedBehaviorSanitizer | 30/30 pass |

The desktop runs require archive checks. The sanitizer run enables leak
detection and stops on undefined behavior. All 176 project C/C++ source and
header files follow the supplied formatting configuration; the largest is
1,976 lines. A provenance check confirms that the paired-probe core and both
desktop binaries are byte-identical to the V7 artifacts. The V7 test
freeze also removes an obsolete monochrome palette-preservation exception
while retaining the full-memory comparison.

Other fixture changes initialize bytes required by a test or choose the same
seed when comparing complete snapshots. The memory editor still proves that
pending edits do not write until committed, and the cheat search still requires
its unique banked result. These gates cover the tested startup, state, runner,
and Qt contracts; they do not establish complete firmware, model, or game parity.

## Installed desktop workflows

Fresh V7 Windows and Linux installations pass the native Qt workflow checks with
real audio. The runs verify fixed and captured automatic seed replay, restart
and reload behavior, failed-open preservation, native-state host metadata,
initial firmware memory, and loaded cartridge SRAM. Separate assertions in
each run require a real audio sink during save/load, rewind, resume, and closure
of a cartridge during an audio write. Exact-memory fixtures stop their timers
and suppress their own audio while comparing bytes.

Each platform also passes 32 seed command-line cases: 12 valid inputs and
20 rejected inputs. These cover zero, leading decimal zeros, hexadecimal,
the maximum 64-bit value, overflow, malformed input, and repeated valid options.
The later repeated-option correction and its expanded checks are qualified
separately below.
Both installations load the expected Qt and SDL libraries. Linux uses Xcb and
the existing WSLg audio server.

The native runs retain ten captures per platform. Visual inspection of the
memory editor, debugger, startup overlay, and map overlay on both platforms
found legible controls without clipping or overlap. All 199 product inputs
and 317 installed files retained their hashes. The combined desktop receipt
has SHA-256
`f07c58f6c46627ced98eba530d15c9b35f481751df3a0576dcbacb5916ce4810`.

These are integration checks, not speed measurements. They do not establish
audible quality, coverage of every display scale or desktop environment,
commercial firmware completion, or a portable Linux bundle.

## External test programs

The final Windows runner executes 243 external cases with power-on seed zero:

| Group | Pass | Frame limit without a verdict |
| --- | ---: | ---: |
| Hardware acceptance | 119 | 0 |
| Audio | 24 | 0 |
| CPU | 22 | 0 |
| Cartridge controllers | 56 | 0 |
| OAM | 7 | 1 |
| Timing | 14 | 0 |

The unresolved OAM timing logger remains at memory result `80` after 3,600
frame budgets, or 252,806,400 base cycles. That bounded observation is not a
pass. The runner, frozen suite, 199 product inputs, and 147 protected original
files remain unchanged; every result records the requested zero seed.

## Local cartridge scan

The frozen Windows runner processes 880 local inputs, representing 878 unique
ROM images. Each uses its recorded model and firmware with power-on seed zero,
a 3,600-frame budget, and a 90-second host timeout. The inventory includes
556 CGB, 198 DMG, and 126 SGB model selections.

| Controller family | Reached frame limit | Illegal-opcode stop |
| --- | ---: | ---: |
| ROM | 12 | 0 |
| MBC1 | 272 | 0 |
| MBC2 | 11 | 0 |
| MBC3 | 81 | 0 |
| MBC5 | 494 | 6 |
| MBC7 | 1 | 0 |
| HuC3 | 1 | 0 |
| Camera | 1 | 0 |
| Unidentified header | 1 | 0 |
| Total | 874 | 6 |

These are bounded execution results without scripted controls or guest test
verdicts. They do not establish successful gameplay, saving, real-time speed,
or universal cartridge compatibility. No local commercial inputs cover HuC1,
MBC6, MMM01, or TPP1; generated controller checks have a separate scope.

The runner and suite, all 199 source inputs, and 1,284 protected original files
retain their hashes and modification times. Every result reports the requested
seed. The scan summary has SHA-256
`3144f3e0e8d611746c13983ead6c017c71aa1a05fd30e9e86ee52312deb6bc8d`.

The six illegal stops receive a separate 18-observation investigation. All six
target probes reproduce the scan's steps, cycles, terminal address, and opcode.
With independent seed-zero cold generations, both engines reach illegal opcodes
in all six cases, but only three pairs have identical final 256 fetched
instruction/register records. One other pair has earlier register differences,
one takes a different instruction count, and one reaches a different terminal.

For those three cases, a second control gives the reference exactly the target's
49,583 cold volatile bytes before executing the first instruction. All three then
match the complete final 256 records, instruction counts, cycles, and illegal
terminal; the target observations do not change. This isolates sensitivity to
startup memory in these controls. Equal seed values do not imply equal random
streams, and matching illegal stops are neither compatibility passes nor proof
that an input is malformed. The checks do not compare every earlier instruction
or qualify post-opcode peripheral behavior. All 135 protected inputs and prior
receipts remain unchanged. The combined qualification has SHA-256
`027ce00f341adaf9d3c959f630a717178410b2c912888484338e6b42ad6e6f7a`.

## Repeated seed options and final desktop gates

The last review found that Qt validated only the last repeated seed argument.
An earlier invalid value followed by a valid value could therefore be accepted.
Qt now validates every occurrence in order. Any malformed value rejects the
command; otherwise the last value selects the policy. A final `auto` clears
fixed seeding, and fixed zero remains valid.

The V8 freeze changes only `main.cpp` and the Qt command-line test script from
V7. Its 199-input manifest has SHA-256
`1982baf95ea766d7e3cc7e2316ca05361cec967999bfb20f09c022d09a385d75`.
Fresh Release builds pass all 51 Windows and 49 Linux desktop tests. Fresh
installed native Qt workflows also pass on both hosts with real audio, the
expected runtime libraries, and ten retained captures per platform. Inspection
of the memory editor, debugger, startup overlay, and map overlay finds no
clipping or overlap in those captures.

Each installed application passes 42 seed command-line cases. These include
all twelve cases paired with its V7 control: five formerly accepted malformed
earlier or middle arguments now exit with status 1 and produce no report.
Valid repeated choices and the already rejected malformed endings retain
their expected behavior.

All 199 V8 inputs and 317 installed files remain unchanged during these checks.
Both core libraries and both ROM runners are byte-identical to V7. The earlier
sanitizer, paired-core, external-suite, and cartridge-scan results retain their
V7 execution scope and apply to those identical artifacts; they were not
rerun for this parser-only correction. The final desktop receipt has SHA-256
`c1ce054c58296c5fdda1b3d120b31801abcd456591a9214bc4ffba0e21e95c72`.

Native workflow duration is not a speed benchmark. Audio readiness and state
transitions are asserted, while audible quality and broader desktop coverage
remain unqualified by these integration checks.

## Later firmware-sensitive controls

The [cartridge and clock checkpoint](validation-cartridge-storage.md) retains
11 external startup failures. A separate comparison investigates four selected
cases using the same cartridge bytes with two local firmware profiles. Each
profile runs on both cores, for 16 complete observations.

| Test | Local firmware, both cores | Generated firmware, both cores |
| --- | --- | --- |
| DMG divider startup | Pass | Fail |
| DMG initial hardware registers | Pass | Fail |
| DMG serial startup | Pass | Fail |
| CGB divider startup | Pass | Fail |

Executed assertion values match between cores in every pair. The generated DMG
register test first differs from its expected value at DIV: it expects `AD` and
reads `BE`. The DMG serial test has matching eight-shift histories and executed
interrupt entry within each firmware pair. The firmware profiles reach different
divider phases, so their distinct verdicts must remain attached to their exact
firmware inputs. This four-case matrix does not reclassify the other seven
retained model/test failures.

Observations use stored fields, verified ROM bytes and guest-written assertion
storage. The observer performs no mapped memory reads that could synchronize
display or audio work. Raw IF/LY/STAT and I/O arrays are diagnostic only and are
excluded from equality claims. Serial shift observations are instruction-boundary
upper bounds, not exact internal edge timestamps. Normal audio generation stays
enabled; waveform equality is outside this probe's scope.

Seven pairs have no compared differences. The generated-firmware CGB case has
nine differences in the retained idle serial-clock phase. Its CPU registers,
executed divider values and elapsed cycles agree. That test starts no CGB serial
transfer, so this result does not demonstrate a wrong transferred byte or
interrupt. Source inspection identifies a clock-selector lifetime difference;
an active-transfer comparison is required before qualifying its guest effect.

All 29 protected inputs and the 264 build inputs remain unchanged. The retained
receipt has SHA-256
`4980c616fcbfe365042d26f7ec0be532eaff86f383a357f32fde063693736234`.
These later observations retain the storage/clock source manifest and executable
identities; they do not replace the older startup records above.

## CGB work RAM by revision

On revisions 0 through C, alternating eight-byte lanes start at zero. The lane
order reverses halfway through each 4 KiB bank. Bits in the remaining lanes are
set about 31 times out of 32. Revision D uses a 75% probability in each bank's
lower half and 25% in the upper half. Revision E uses a uniform distribution.

The cold-entry probe compared 12,582,912 WRAM bytes per engine across 64 seeds
for each of the six revisions. Every zero lane matched exactly. The largest
per-bit rate difference was 0.189 percentage points. The probe also used guest
instructions to read 12,288 samples across physical banks 0 through 7 and
checked those bytes against the corresponding cold image. Each generator
retains its own random sequence.

The startup regression checks that the approximate handoff clears physical
bank 2, firmware receives the raw generation, and firmware re-entry preserves
live memory. Extra WRAM draws use a separate stream to preserve the seeded
HRAM and palette sequences. Physical cold-start measurements remain open.

## CGB cartridge entry registers

With built-in startup, monochrome cartridges now start with DE `0008` and HL
`007C`. Register B contains the sum of the 16 title bytes modulo 256 when the
header uses the Nintendo license code. Other monochrome cartridges start with
B `00`. Color-capable and color-only cartridges retain BC `0000`, DE `FF56`,
and HL `000D`.

The regression saves registers to HRAM with the cartridge's first instructions.
It covers six header cases on each of the six CGB revisions after cartridge load,
quick reset, and hard reset. The four monochrome cases exercise both license
formats and two rejected license combinations. The color cases use flags `80`
and `C0`.

The 36 entry register comparisons agreed with executed firmware. Revisions A
through E used the same local firmware image, and revision 0 used a generated
image. The earlier built-in profile differed in all 24 monochrome cases.
Divider and LCD phase at handoff still depend on the firmware and cartridge
header. Firmware-specific bank 2 work data remains open.

### I/O ports at cartridge entry

Built-in startup now sets the CGB DMA source page and DMG sprite palette
registers to zero. It restores the palette index and auto-increment latches,
the monochrome sprite priority and joypad row selection, and the CGB scratch
registers. The color-mode WRAM selector reads `F8` and still maps bank 1.

The shared firmware for revisions A through E agreed with all 12 selected
ports in each of 30 cases. The generated revision 0 firmware agreed on 68
selected readbacks across six more cases. Authentic revision 0 color JOYP
and OBJ index values remain open. The startup regression also captures the
ports through cartridge `LDH` instructions after loading and both reset kinds.

Firmware re-entry resets the VRAM and WRAM selectors and sprite priority
before executing the first instruction. The test changes those selectors
first, enters the overlay, and verifies the initial mappings with the same
backing memory. A quick reset carries forward the live DMA page and the two
DMG sprite palette bytes.

### CGB color LCD phase at cartridge entry

Built-in startup on CGB A through E now enters native-color cartridges at
VBlank line 144. The first cartridge reads return `LY=90`, `STAT=81` and
`IF=E1` in hexadecimal. The pixel clock and the display scheduler keep
their measured offsets through the following scanline and frame. Rapidly
turning the LCD off and back on during this interval delivers a repeat-frame
event at the same cycle as supplied firmware.

Thirty color cases used both color header flags and three title and
license combinations on each revision. The comparisons checked the first
guest reads, the following dot-level LY and STAT transitions, and the first
presentation after an immediate LCD toggle. Reset tests also check the
initial state, later IRQs, and continuation through saved states.

Starting supplied firmware resets the LCD pipeline, blank-frame clock and
repeat countdown. In 45 executed-firmware comparisons, the initial
snapshot, presentation sequence, cartridge-entry snapshot and snapshot
after ten more frames matched the previous build byte for byte.

Windows and Linux each passed 177 acceptance and mapper cases. Both
retained the previous guest instruction counts, clock totals, framebuffer
checksums and audio checksums. The 121 paired firmware cases still matched
the comparison run's terminal guest clocks.

Native Windows and WSLg each ran Super Mario Bros. Deluxe for 65 seconds
on CGB-C with built-in startup and normal audio. Both sessions applied all
105 scripted inputs and reached World 1-1. They ended at the same guest
cycle with identical framebuffer captures and matching 8 KiB saves.
Both audio sinks reported zero idle or error events.

CGB revision 0 display handoff remains open. See
[VBlank comparison timing](validation-timing.md#cgb-stat-coincidence-across-scanlines)
for the early color coincidence bit transition.

### CGB native-color divider at cartridge entry

Built-in startup for CGB A through E sets the divider and APU sequencer
phase from the cartridge's license bytes and the timed compatibility
palette search. This covers the ordinary checksum match, fourth-byte
selection, and fallback paths.

The full divider and APU sequencer step matched 384 native-color CGB-C
handoffs with the supplied firmware. The cases cover all 256 title
checksums, conditional keys, both native-color flag values and the license
checks. Another 64 held-out cases across CGB A, B, D and E matched the
entry state and the first three APU falling edges.

Six commercial cartridges also matched the divider and sequencer phase
on both CGB-C and CGB-E. A separate set of 45 supplied-firmware runs
reproduced the prior build's entry and cartridge snapshots, frame
callbacks and ten-frame continuation.

The startup regression checks the first guest DIV read, TIMA advance,
and pulse length behavior after load, quick reset and hard reset. A guest
probe also confirms that the sequencer step affects the first length edge
when the divider phase is identical. CGB revision 0 and wider APU handoff
state remain open.

### Cartridge logo VRAM at entry

Built-in startup expands the 48-byte cartridge header logo into the low
bitplane of tiles at `8010–818F` in VRAM bank 0. Each source pixel becomes a
2 by 2 block. The registration mark follows at `8190–819F`. DMG0 leaves
that tile and its map cell clear. [Original monochrome firmware disassembly](https://codeberg.org/ISSOtm/gb-bootroms/src/commit/443d7f057ae06e8d1d76fa8083650cf0be2cd0ae/src/dmg.asm#L102-L140)

Monochrome and adapter profiles place the logo at `9904–990F` and
`9924–992F`, with the mark at `9910` where applicable. Color profiles
retain the expanded tiles. They install the map only for the licensed
monochrome compatibility titles selected by checksums `43` and `58`.
Those handoffs also leave `HL` at `991A`. [Adapter firmware disassembly](https://codeberg.org/ISSOtm/gb-bootroms/src/commit/443d7f057ae06e8d1d76fa8083650cf0be2cd0ae/src/sgb.asm#L110-L132), [Color firmware disassembly](https://codeberg.org/ISSOtm/gb-bootroms/src/commit/443d7f057ae06e8d1d76fa8083650cf0be2cd0ae/src/cgb.asm#L1130-L1164)

The regression checks both VRAM banks after cartridge load, Quick Reset, and
Hard Reset for all twelve modeled profiles. Cold firmware entry clears VRAM
before execution. Memory-preserving firmware re-entry retains live VRAM.

Windows and Linux each matched 96 full VRAM images. The comparison used the
installed DMG and Color firmware, plus replacement firmware for Pocket and
adapter profiles. Two DMG0 expectations follow the original disassembly.
The CGB0 comparison ran the installed Color firmware on the CGB0 model, and
its legacy map rules also match the revision-specific disassembly. Original
DMG0 and CGB0 binary verification remains open.

### CGB monochrome compatibility handoff

Built-in startup on CGB A through E enters monochrome cartridges in
VBlank. The setup time follows the selected palette slot and accounts
for the legacy tilemap transfer on two keys. DIV, the APU sequencer,
the pixel clock, the retained display scheduler and the LCD repeat
window all start at the corresponding point.

The compiled handoff matched 522 supplied-firmware cases. These include
324 CGB-C cases, 148 held-out cases on A, B, D and E, and 50 license
controls for the two legacy keys. They cover all 26 measured setup
durations and the first guest DIV, LY, STAT and NR52 reads.

The legacy paths leave HL at `991A` and write 25 tile indices to VRAM
bank 0. A separate nine-case comparison checked those cells in both
banks for the two keys and an ordinary key with each of the three
cartridge flags. All nine cases retained the bytes through a snapshot
round trip. The startup regression checks the tilemap, divider and
display coordinates after load, quick reset and hard reset.

The 448 earlier native-color timing cases still match. Executed firmware
reproduced the prior build's entry and cartridge snapshots, frame
callbacks and ten-frame continuation in 55 runs, including both legacy
keys on each revision. CGB revision 0 and wider firmware VRAM and APU
state remain open.

Windows passed 105 local tests, Linux passed 103, and the Linux sanitizer
build passed 76. All 177 acceptance and mapper cases passed on both
hosts, with identical guest outcomes across hosts. The 121 paired
firmware cases retained their terminal clocks. In the 28 built-in CGB
mapper runs, terminal time increased by 2,340 or 2,392 guest cycles.
Instruction counts, audio frame counts and audio CRCs changed in those
runs. Framebuffer CRCs and test verdicts stayed the same.

### CGB-0 monochrome divider at cartridge entry

On CGB-0, built-in monochrome startup sets the divider phase to `2884`
when the extended license check rejects its first byte. The hardware
test reads `29 2A 2A 2B 2C 2E` at six points after cartridge entry. The
regression checks the same reads after load, quick reset and hard reset,
with controls for other license headers and native color mode.

The test distinguishes four-clock-aligned starting phases. The low two
oscillator bits, authentic CGB-0 firmware execution, other license
branches, and the display and audio handoff still need qualification.

### CGB-0 wave RAM at cartridge entry

Built-in startup leaves all 16 wave RAM bytes at zero on CGB-0. A
guest probe reads `FF30` through `FF3F`, enables channel 3 without
writing a waveform, and samples its PCM output 256 times. The CGB-0
samples stay zero in both native color and monochrome mode, matching
the generated CGB-0 firmware. CGB-A and CGB-E retain the alternating
`00 FF` bytes and produce 128 nonzero samples in the same probe.

All 61 local cartridges that reached the corrected divider branch ran
for 420 guest intervals on both the baseline and corrected builds.
Final image checksums, frame counts and audio frame counts matched.
Cumulative audio checksums differed in 56 runs.

### CGB pulse 1 at cartridge entry

Built-in startup on CGB A through E restores pulse 1's oscillator phase,
length, sweep, envelope lock and register latches at cartridge entry.
Monochrome startup gives the oscillator more time to run after the last
trigger. On A and B, the length depends on the sequencer phase at both
trigger writes. The first PCM12 read now returns `00`, with pulse 1
still active in NR52.

The compiled state matched 300 supplied-firmware handoffs across both
cartridge modes, all 26 measured monochrome setup durations and the
license branches. In 95 guest runs, the PCM reads and pulse state matched
before and after writes to envelope, frequency and trigger registers.
The startup regression covers the first guest PCM read and
oscillator and length state after load, quick reset and hard reset.

The 55 supplied-firmware runs retained their snapshots and frame traces.
All 177 acceptance and mapper cases passed on both hosts. Only the
audio CRC changed in the 28 built-in CGB mapper cases. Windows passed
105 tests, Linux passed 103 and the Linux sanitizer build passed 76.
Revision 0 pulse handoff remains open.

### CGB startup audio filter

The first built-in audio frame on CGB A through E starts with settled
capacitor history for the selected filter mode. Accurate uses the
cartridge-entry mix from pulse 1. Preserve Waveform uses the DC estimate
from the entry routing and volume registers. A guest write before the
first sample is still delivered through the normal reconstruction path.

In 300 comparisons with supplied firmware, the maximum first-sample
difference was 1 PCM unit with Accurate and 3 with Preserve Waveform.
The earlier implementation differed by up to 4,080 and 4,082 units,
respectively. With the filter off, the maximum difference stayed at 3.
The matrix covers five revisions, four cartridge cases and five rates
from 8 to 192 kHz. Another 90 comparisons in a separate runtime
confirmed the same first-sample bounds.

Version 30 records the pending initialization for exact snapshot
restoration. The regression covers all three filter modes, a routing
write before the first sample, version 29 import, reset and live restore
into firmware. In 55 supplied-firmware runs, snapshot payloads and
frame callbacks reproduced the previous results after accounting for
the version header. Sample-clock phase and reconstruction remainders
still differ from executed firmware.

Windows passed 105 tests, Linux passed 103 and the Linux sanitizer build
passed 76. All 177 acceptance and mapper cases passed per host with
matching guest results across hosts. Only the audio CRC changed in the
28 built-in CGB mapper runs. The 121 paired firmware terminal clocks
still matched.

### Monochrome cartridge in Qt

Windows and WSLg each ran Donkey Kong for 60 seconds at `0d898fa` on CGB-C
using built-in startup, normal audio, seed zero, and separate copies of the same
save. Both applied all 88 scripted inputs and reached the first stage.
The final guest images and written 8 KiB saves matched byte for byte. Reopening
each saved copy and applying eight more inputs produced matching first-stage
intro images, and both saves retained their contents.

After warmup, Qt measured guest clock progress at 100.008% of target on Windows
and 100.020% on WSLg. The WSLg monotonic clock advanced at 96.68% of its raw clock
over the full run. The 95th-percentile widget paint intervals were 18.29 ms and
18.12 ms respectively. Both audio sinks reported zero idle or error events.

The 60-second CGB-C run repeated after the I/O latch changes at `7f8d442`.
Both hosts again applied all 88 inputs, reached the first stage and wrote
the same 8 KiB save. The final images also matched the earlier run byte for
byte. Both audio sinks reported zero idle or error events.

## DMA at firmware re-entry

Direct overlay entry cancels pending, active, and restarted OAM DMA while
keeping the bytes already written to OAM. The first firmware instruction
can fetch from the overlay immediately. A new write to `FF46` starts a
fresh copy. The guest regression exercises all three transfer states on
each of the 12 supported profiles.

Across 72 re-entry and hard-reset cases, the previous build left DMA pending
or active in 24 direct re-entry cases. With an active transfer, the first boot
instruction read the cartridge's `HALT` opcode instead of the overlay byte.
All 72 cases pass with the reset. The paired cold and quick-reset controls
also passed 66 cases across 11 profiles.

CGB entry also resets the VRAM DMA address latches and releases a pending
HBlank transfer. On all six revisions, a firmware transfer started without
an address write copies from the start of the overlay to VRAM bank zero.
The old destination and all unrelated VRAM bytes retain their prior values.

### Timer, serial and joypad transactions

Overlay entry now clears a pending TIMA reload and its write-blocking window,
a partial serial transfer, and the monochrome joypad row-release delay. Held
buttons and pending host input carry forward to the new session.

Before the fix, an old reload could raise IF bit 2 during the first firmware
read. A pending row change could also make JOYP read `FF` in place of `CF`.
The updated regression covers these reads, the first TIMA write, and the
serial control register in 60 cases across all 12 profiles. An independent
352-observation comparison covered cold, hard-reset, quick-reset, and
overlay-entry paths. A new serial transfer after re-entry completed with
the expected data and interrupt.

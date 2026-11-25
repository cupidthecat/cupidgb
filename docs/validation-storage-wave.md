# Storage, wave history and camera integration

This checkpoint combines full cartridge SRAM, retained CH3 fetch history and
asynchronous camera input in the core and Qt frontend. Native version 20 carries
the complete storage and wave history together. Desktop reset, state, rewind,
debugger and memory-search workflows use that same ownership contract.

The behavioral comparison revision is
`ea667ad79aa28409f034b1fd6da7304e1844750d`.
Results below apply to their stated build, models, inputs and duration.
Complete hardware parity and compatibility with every cartridge remain open.

## Cartridge storage and ownership

TPP1 declarations now cover all ten supported SRAM codes, from no SRAM through
2 MiB. Small cartridges retain the 128 KiB inline storage area. Larger
cartridges own a contiguous allocation, accessed through
`cupid_gb_cartridge_ram` or `cupid_gb_cartridge_ram_const`. Callers must reacquire
a borrowed pointer after a successful cartridge replacement, reset or state
load. An existing core must be cleaned up before its allocation is released.

Cartridge validation and allocation complete before replacing the running
machine. A failed cartridge load preserves its storage, execution and host
registrations. Battery imports validate the complete geometry before changing
RAM or clock state. Native state loads also commit only after the payload,
descriptor, digest and replacement storage are valid. The regressions cover
allocation failure and aliased arguments as well as malformed files.

Ordinary MBC3 timer-bank selections 4 through 7 return a driven FF when the
cartridge has physical SRAM. A cartridge without that SRAM leaves the bus
undriven. The dedicated bus regression distinguishes those cases instead of
inferring the electrical behavior from an unused bank number.

The Qt frontend reads the actual battery size, applies reset retention and
erasure rules across the complete SRAM span, and searches all physical cartridge RAM. Byte,
little-endian word and big-endian word searches cross bank boundaries and reach
the end of a 2 MiB allocation. The debugger's mapped-memory editor does not
provide arbitrary physical SRAM-bank editing; that remains a separate parity gap.

Rewind remains lossless. For states of at least 512 KiB, the frontend samples
eight distributed 32 KiB windows before choosing ordinary compression or stored
blocks. Smaller states retain their existing compression path. Every encoded
result must decompress to the original snapshot, and the existing history
budget still limits retained bytes. This selection does not omit guest memory
or reduce hardware work.

## Native version 20

The existing hardware prefix is followed by the retained CH3 byte, a four-byte
SRAM length, any SRAM above the inline 128 KiB area, and the accessory
descriptor. Printer data follows when present. Earlier hardware offsets remain
fixed.

| Input format | Retained CH3 byte | Extended SRAM |
| --- | --- | --- |
| Version 20 | Recorded explicitly | Recorded length and complete tail |
| Version 19 wave-history layout | Recorded explicitly | Not present; receiving SRAM must fit the inline area |
| Version 19 SRAM layout | Projected from the saved sample nibble | Recorded length and complete tail |
| Supported versions 2 through 18 | Projected from the saved sample nibble | Not present; receiving SRAM must fit the inline area |

The two development layouts numbered 19 have different exact shapes. The
reader accepts only a layout whose declared geometry, accessory descriptor and
complete file length agree. A missing extended tail cannot inherit bytes from
the receiving machine. An older file without the fetched byte duplicates the
saved nibble; it cannot reconstruct the other half of a byte that was never
recorded.

Ordinary, trusted-context, live and live-context loads share the same structural
validation. Tests exercise incompatible lengths, wrong SRAM declarations,
truncated tails, damaged descriptors, late failures, printer attachments and
receivers with different prior contents. Frozen older-format lengths and
digests retain their historical meaning. Version 1 remains a battery-recovery
format rather than an execution-state import.

## Wave-channel behavior

The core retains the entire fetched wave byte independently of the selected
sample nibble and current wave RAM. A retrigger can expose its high nibble
before a later fetch. Fetch-boundary retriggers preserve the ordering between
RAM mutation, byte selection and an immediately expiring length counter.

An active NR30 disconnect can also replace the retained byte. In the final two
base clocks before a fetch, the executing program counter selects the source
byte through its low four bits. On monochrome hardware, a disconnect just
after a fetch uses NR30's low address bits. Neither operation rewrites wave RAM
or replaces the already selected raw sample. Repeating the disconnect after
the channel is inactive must not replace the retained byte again.

The retained negative evidence includes the original four CGB cases and a
separate 36-case extension. Their eight differing boundary cases expose sixteen
differing guest values across completed PCM reads and actual HRAM stores.
The corrected comparison keeps observable guest values separate from internal
byte and sample diagnostics. Monochrome PCM34 reads return FF and cannot
establish channel amplitude.

Permanent regressions also cover length-expiry ordering, mode and speed
controls, different receiving histories, pending clock credit and all four
native load APIs. The earlier
[fetch-history record](validation-wave-history.md) retains its isolated
comparison results and development-format scope.

## Camera timeline ownership

The raw host sensor image and pending device request belong to the receiving
session. State files retain ordinary guest RAM, including image tiles already
written by the cartridge. They do not save the raw host frame or replay an old
device request.

Reset, reload, state loading and rewind cancel obsolete captures after a
successful transaction. Debugger Backstep, memory writes, register edits and
memory-search edits now do the same. Cancellation clears the busy request while
keeping the accepted sensor image. Rejected operations retain the active
request. Late results from an abandoned timeline cannot overwrite a new one.

The Qt camera fixture tests queued and submitted requests around each debugger
operation. It records request, cancellation, stale-result and timeline counters.
These checks use injected frames; they do not open a physical camera.
The [camera guide](host-camera.md) describes conversion, error handling and
the remaining device-workflow qualification.

## Build validation

The build inventory contains 287 inputs, with manifest SHA-256
`cec52ae0e794c432649338544dc6589caa53f2e55bf69128013f52abd9d354ea`.
Documentation and private evidence are outside that inventory. The source
audit covers 275 owned files; all owned C-family files remain within 2,000 lines
and pass the supplied formatting rules.

The first attempt compiled the Windows targets, then stopped before CTest
because the private cache parser included blank lines in CMake key names.
That failed receipt remains preserved. The second attempt uses a corrected
parser with the same build inputs.

| Fresh build check | Status |
| --- | --- |
| Windows Release, Qt 6.8.3 | 88 of 88 tests passed |
| Linux Release, Qt 6.8.3 | 86 of 86 tests passed |
| Linux ASan/UBSan | 62 of 62 tests passed |

All five build and source-check stages finish successfully, with unchanged
inputs and no unresolved process cleanup. The completed gate receipt has SHA-256
`5ef7ff2c78387ad47124a9bb59dcc0bd3a87f74e06fc04c17ed1a7a1f86c7a8e`;
the final source and artifact inventory has SHA-256
`0b89d4653016b4e55be65fcd7cd2d8d283812e40314aef3f0d16c711a666d0eb`.

CTest separates desktop and large-storage workflows into two offscreen checks,
each with the existing 30-second deadline. They complete in 13.67 and 1.97 seconds
on Windows, and 11.47 and 1.74 seconds on Linux. CLI selection and cartridge-loading
checks run as separate groups with 90-second deadlines.
These are test durations, not gameplay or physical-presentation measurements.

## Runtime qualification

The paired NR30 comparison passes all 40 cases with no differing guest values
or retained-byte/sample diagnostics. Its 20 CGB cases supply 40 meaningful
PCM read/store values; the 20 monochrome cases supply 40 unavailable-port FF
values. All 80 same-engine native continuations pass. This resolves the sixteen
earlier guest-value differences in the retained negative cases. It does not
qualify output waveforms, physical devices or inactive-channel fetching.
The completed comparison receipt has SHA-256
`dcb7e7f41941f5c9d844e2b76b83c01c7690135f6cdcf039b1e42ecff1696fbc`.

The version 20 file exchange passes six captures, twelve restores and ninety
exact comparisons across Windows and Linux, with no mismatches. Two cases
capture wave-channel boundaries; the third uses a CGB TPP1 cartridge with
2 MiB SRAM. Ordinary state loads into poisoned receivers reproduce complete
SRAM before and after 512 guest instructions, immediate state re-encoding and
48 kHz PCM continuation. The LCD is disabled for these fixtures, so the exchange
does not qualify rendering. The receipt has SHA-256
`65e59b1b48eef69e9132710d672e84f54cb7963ffeb25d53629d998725296f56`.

The first offline SRAM comparison failed because the JUnit files truncate
successful test output at 1,024 bytes. Five complete identities remain in each
file and match across all three builds; the other fifteen are absent from that
output. The failed attempt remains preserved and does not establish the required
twenty-identity result.

Offline recovery from the retained full CTest logs passes all twenty model and
RAM-geometry combinations: DMG and CGB at each of the ten TPP1 RAM codes, from
no SRAM through 2 MiB. State sizes and SHA-256 hashes match across Windows
Release, Linux Release and Linux ASan/UBSan, covering sixty retained records.
No tests were rerun. The full logs were collected and hashed after the build
gate, then cross-checked against its recorded 88, 86 and 62 test inventories,
commands, working directories and results, including the five SRAM records
retained in each JUnit file. The gate itself did not record those full-log hashes.
The recovery receipt has SHA-256
`81ef1b49f8239c95f450b4fa4ad61c3f7f8af72c754473b23c03bf309cdaa83e`.
This is an offline comparison of generated state bytes; the separate file
exchange above supplies the foreign-file restore evidence.

The fresh external corpus records 248 passes, eleven failures and one bounded
case, with zero changed guest cases against the retained comparison. The eleven
failures and bounded case remain part of the result. Inputs remain unchanged,
and the receipt has SHA-256
`c2d717f9f23c2ee4e629d481d3051c8668b1604bffda2bea34150d71601e32fb`.

Native Qt workflows pass on Windows and Linux with audio enabled, unchanged
inputs and no offscreen platform override. Each run retains sixteen screenshots;
all thirty-two were reviewed without an identified layout, clipping or branding
defect in those captures. Each host's version 20 camera record covers 28 requests,
62 cancellations, thirteen ignored stale results and eight debugger or memory
edit timeline cases. Both runs use injected images and leave the physical camera
closed. The native receipt has SHA-256
`0ab5158319ab1bb176b5f2b61bbdc148a3e5fee0018ace0da14d3c4373e9e03b`.

All eight reset-audio observations pass on each host, including real sink
consumption, queued PCM, recording continuity and reset/rewind history. Each run
keeps its 48 kHz recording across the checked operations. Windows records 19,183
frames with a 48 kHz float stereo sink; Linux records 16,772 frames with a
44.1 kHz signed 16-bit stereo sink. These short workflows do not qualify acoustic
output, physical camera behavior or sustained emulation performance.

Four fresh Windows gameplay cases each complete 120 seconds with normal audio,
rewind capture, fixed startup seed zero and scripted input. They start from the
approximate post-boot state. The table reports the interval after the first five
seconds; widget paint intervals describe Qt delivery, not physical monitor scans.

| Game | Model and mapper | Guest speed | Median / 99th-percentile paint interval | Peak resident memory |
| --- | --- | ---: | ---: | ---: |
| Tetris | DMG, ROM-only | 99.9917% | 16.737 / 18.369 ms | 110.0 MiB |
| Super Mario Bros. Deluxe | CGB, MBC5 | 100.0001% | 16.735 / 18.513 ms | 213.3 MiB |
| Donkey Kong | SGB, MBC1 | 99.9971% | 16.336 / 18.267 ms | 213.6 MiB |
| Vigilante 8 | CGB, MBC5 rumble | 99.9943% | 16.753 / 18.807 ms | 253.8 MiB |

Each run reports zero audio idle and error events after warm-up, an active native
sink, complete scripted input, successful battery flushing and rewind capture,
and a clean process exit. The retained final captures show game scenes in all
four cases. Guest output runs at 44.1 kHz and the native sink at 48 kHz. Peak
memory includes the retained rewind history; it is not a core-only allocation
measurement. The unchanged-input Windows batch receipt has SHA-256
`448a07b6da84797240e69d94994a8f9053dea236de8b46fda86fc8d081da32c4`.
These runs do not qualify every game, physical rumble, or arbitrary saved-game
progress. State loading and reset/rewind transitions have their separate checks
above.

The fresh Linux attempt does not reach gameplay. The available Ubuntu/WSLg host
fails its 300-second clock-readiness budget: it never supplies four consecutive
ten-second samples with both adjusted monotonic and audio clocks within 0.1% of
the raw monotonic clock. The largest observed rate disagreement is 8.33%.
All four requested games remain unlaunched. Inputs stay unchanged, no service or
clock settings change, and process cleanup completes. The Linux owner receipt
has SHA-256
`929d503aa3a1c78821120f7e5f0eb122e160ac690fed0028591ee9273edaa2bb`.
The successful Linux builds and short native workflows remain valid within their
scope; sustained Linux playback speed and audio stability remain unqualified.

A separate thirty-observation startup probe confirms a remaining SGB difference.
Across SGB and SGB2, ordinary and enhanced cartridge headers, and cold, hard and
quick reset, the comparison holds guest execution for 106 host presentations
before the first instruction. That interval totals 14,887,488 half-cycles while
CPU, divider, timer, serial and RTC state stay unchanged. CupidGB version 20
executes immediately in all twelve cases. Three paired DMG controls execute
immediately in both implementations, and subsequent clocks advance in every
case. The probe uses original synthetic cartridge and startup instructions; it
does not qualify firmware authentication, startup artwork or sound. Its receipt
has SHA-256
`3b9a8de05c2a620a7a02b862ef533d521f54f80eb2aff56cbda509ae4d18d200`.
This is completed evidence of a missing behavior, not a parity pass.

The retained mapper baseline has 56 external passes across MBC1, MBC2 and MBC5.
Generated DMG/CGB regressions cover eleven implemented controller families.
Those scopes do not establish every mapper combination or commercial game.
Partial MBC6 behavior and the absent TAMA5 controller remain explicit.

SGB host startup, inactive wave-channel fetch behavior, other hardware
revisions, independently written startup firmware, physical devices and
transport, wider desktop workflows and broader sustained gameplay remain
requirements in the [parity checklist](parity.md). Original ROMs, boot
binaries, saves, captures and private receipt paths stay outside public commits.

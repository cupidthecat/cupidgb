# Cartridge storage and load lifetime

This checkpoint extends cartridge image storage beyond the former 8 MiB limit and makes core loading preserve the current session when validation or allocation fails. The behavioral comparison revision remains `ea667ad79aa28409f034b1fd6da7304e1844750d`.

## Storage and banking

The image limit is 1 GiB, corresponding to 65,536 banks of 16 KiB. Each cartridge retains its controller's address wiring; a larger input does not widen an MBC5 bank register. The core allocates the actual input length, with a 32 KiB minimum, instead of reserving the full image limit for every instance. Missing bytes in a rounded physical bank population still read as `0xff`, and identity hashes cover the supplied bytes.

Loading validates the candidate before changing the live core. A rejected size, unsupported storage declaration or failed allocation preserves the complete core and its ROM bytes. Valid input can share the current ROM allocation, including a shorter subrange. A replacement that changes allocation size copies its input before releasing the old allocation.

Successful loads advance a host generation counter. Trusted rewind contexts include that generation, so reloading different bytes into the same allocation and length invalidates an old context. Reset and native-state restoration preserve the receiving allocation, capacity and generation. Direct external ROM patches still require explicit context invalidation. The native wire format remains version 17.

## Regression coverage

Generated DMG and CGB cases exercise an 8 MiB input, the first byte beyond that boundary, a partial extended bank, a 16 MiB population, erased tails, bank aliases, reset and native-state continuation. Lifecycle checks cover same-capacity reloads, shrinking self-alias loads, invalid interior ranges, deterministic allocation failure, stale context save/load rejection and state or battery output overlapping unused allocation capacity. The C file-loading boundary has a separate generated extended input.

Twenty existing mapper cases now select DMG and CGB explicitly, with matching cartridge color flags and valid header checksums. They cover ordinary and extended MBC3 wiring, MBC1/multicart, MBC2 address decoding, MBC5 RAM and motor bits, HuC1 infrared/banking, HuC3 banking/clock commands, MMM01 menu/lock behavior, physical-bank aliases and small RAM. Camera, MBC7 and TPP1 already have explicit cases for both models. These generated cases supplement the external mapper corpus; they do not establish every-game compatibility.

Review also found that a battery save could clear its optional byte-count pointer before checking whether it overlapped live storage. Both save APIs now reject that alias before writing or polling host time. Sixteen rejection cases cover core metadata, ROM bytes, unused ROM capacity, output bytes and the unused output tail, including a partial overlap. Each compares the complete core, ROM allocation and destination with their original bytes. A separate adjacent-storage case remains valid.

The desktop check opens a generated extended TPP1 cartridge both directly and through ZIP extraction. Its guest program reads bank 512 and writes the marker to battery RAM. The test verifies that saved marker and separate raw/archive save paths. A small archive with an oversized declared member checks rejection before decompression; it does not allocate a 1 GiB test image.

| Controller family | Generated DMG and CGB coverage | External mapper cases |
| --- | --- | ---: |
| ROM and direct RAM | Header/storage rules, aliases and reload lifetime | No dedicated cases |
| MBC1 and multicart | Bank wiring, RAM modes and small RAM aliases | 26 |
| MBC2 | Address decoding and internal RAM | 14 |
| MBC3 and extended wiring | ROM/RAM selection, aliases and clock snapshots | No dedicated cases |
| MBC5 | ROM/RAM banking, enable rules and motor bits | 16 |
| MMM01 | Menu layouts, lock transitions and bank identities | No dedicated cases |
| MBC7 | EEPROM protocol, motion latches, battery and continuation | No dedicated cases |
| HuC1 | Banking and infrared ports | No dedicated cases |
| HuC3 | Banking, command bus, clock and battery | No dedicated cases |
| Camera | Bank control and generated sensor captures | No dedicated cases |
| TPP1 | Extended ROM, RAM, clock, battery and graded motor control | No dedicated cases |

All listed generated regressions pass in the Windows and Linux gates. The 56 external mapper cases also pass. Direct RAM headers retain the documented comparison exception, and the partial MBC6 implementation remains outside the pinned working-mapper scope. These results do not establish every mapper's complete behavior or physical accessory support.

## Infrared model comparison

A separate paired guest probe covers HuC1 and HuC3 on CGB at normal and double speed. It combines four receiver-gate values, input light states, two warmup lengths, two boundary counter seeds and eight actions involving LCD control, video reads/writes, divider reset/read and work RAM. Guest instructions read both infrared ports and store the observed bytes in RAM.

All 1,024 cases and 11,264 paired observations match, with no invalid fixtures. The probe selects stable speed states directly; it does not qualify an actual KEY1/STOP transition or physical infrared transport. Its retained receipt has SHA-256 `e278a8fe79787dbdab93ca49596e8b5c65d15af561d77f14dff8a7ca7786fa59`. Independent parsing verifies every row, expected instruction position, guest clock count and stored port result. The execution logs and all protected inputs remain unchanged.

## Validation status and limits

The integrated builds pass 75/75 tests on Windows, 73/73 on Linux and 53/53 in the Linux AddressSanitizer/UndefinedBehaviorSanitizer core build. The 260-case external run retains every prior guest result, including graphics and PCM checksums: 248 explicit passes, 11 retained firmware-sensitive failures and one bounded OAM observation. All 56 dedicated mapper cases pass. The unresolved results remain separate from passing tests.

The paired bank probe checks every one of the 65,536 ROM-latch values at two byte positions across four input sizes in both DMG and CGB modes. All 1,048,576 paired checks match:

| Input size | Physical bank population | Paired checks across both models | Differences |
| --- | ---: | ---: | ---: |
| 8 MiB | 512 | 262,144 | 0 |
| 8 MiB + 1 byte | 1,024 | 262,144 | 0 |
| 8 MiB + 16 KiB | 1,024 | 262,144 | 0 |
| 16 MiB | 1,024 | 262,144 | 0 |

The probe, libraries, headers, compiler and all 264 build inputs retain their recorded hashes. An offline companion receipt corrects the original report parser's rejection of eight ordinary cartridge-load messages; it retains all comparison rows and leaves the original logs and receipt unchanged. The companion has SHA-256 `fcc61a21524410218d18b0412231a7b115a26bd8fcdb9654b5883415570841f2`.

The configured ceiling does not establish tested gameplay at every capacity. This checkpoint limited cartridge RAM to 128 KiB, including TPP1. The later [storage, wave and camera checkpoint](validation-storage-wave.md) extends declared TPP1 SRAM through 2 MiB and records its separate validation. Physical peripherals and every-game compatibility remain open in the [parity checklist](parity.md).

## HuC3, MBC7 and Camera comparisons

At `92cd698`, all 131,072 HuC3 cases matched. The test exercised every command byte and register index from a fixed initial clock state on DMG and CGB. Each case compared eight state fields and both command-port readbacks.

Four Linux headless runs matched every pixel after 1,800 frame-equivalents. They reached the file menu with a modified MBC7 image on CGB-E, the new-game and date menu with HuC3 on CGB-E, and the camera capture and contrast controls on DMG and CGB-E. Both builds used the same per-model firmware and ten-event input trace. CupidGB used startup seed 0, and the comparison used seed 1. The camera cases used the same grayscale input and immediate capture completion.

The local library scan found no commercial HuC1, MMM01, MBC6, TAMA5 or TPP1 inputs. Physical camera capture, motion and infrared still need separate tests.

## Commercial battery saves and cold Continue

Windows and Linux headless builds at `bf6ef13` used each game's Save command
and restored the game-written battery in a new process. The runs used ordinary
guest inputs and opened no native snapshots.

Final Fantasy Adventure on DMG saved the opening arena into slot 1 on MBC2.
The 512-byte battery matched on both platforms. A fresh Continue displayed
the saved character, while the same inputs with erased storage entered the
New Game introduction. After loading the slot, Right moved the character 56
pixels on both platforms. The test covers the initial arena and save slot.

Native Qt repeated that route on Windows and WSLg at `053d1ce`. Both
83-second Save sessions produced the same 512-byte battery, and separate
30-second Continue runs loaded their own desktop-written batteries and
returned to controllable movement. Both platforms finished each route with
identical 160 × 144 pixels. After warmup, guest speed during Save was 100.005%
on Windows and 99.999% on WSLg. All four sessions recorded zero audio idle or
error events after warmup.

Robopon Sun Version on CGB-E confirmed its own Save command after the player
reached the kitchen. Both builds wrote the same 32,785-byte HuC3 battery with
a fixed clock. Cold Continue restored the named player, one Robopon, and
1,000 gold. Left movement changed 13,179 pixels compared with an idle run at
the same frame. Separate loads using the system clock also restored progress
and advanced the HuC3 minute counter.

## TPP1 mapper write range

Addresses `0000–3FFF` select the mapper registers using the low two address
bits. The upper ROM window delegates writes to the mapped cartridge memory.
CupidGB treats that window as ordinary ROM for TPP1. [TPP1 address blocks](https://github.com/aaaaaa123456789/tpp1/blob/1932f7be2ebe19503bb684c9a82d79f9b8e4af6c/specification.md#address-blocks)

The DMG and CGB regression executes a guest store at every address from
`4000` through `7FFF`, for 32,768 stores. It checks register readback, selected
banks, SRAM, the RTC, motor state and battery generation after each store.
The previous build changed the low ROM bank on the first store at `4000`.
Physical flash behavior and commercial TPP1 carts remain unverified.

## Local cartridge startup sample

The 2026-11-21 inventory found 880 ROM images or ZIP members. They covered
eight families with valid headers, including an MBC1 multicart and 13
MBC5 rumble images. The library had no recognized MMM01, TPP1, HuC1, MBC6,
TAMA5, ROM+RAM, or extended MBC3 image.

The Release runner completed 600 guest intervals on each platform for
one representative of each available family, plus the multicart, one
rumble image, and a bad-dump control. Windows and Linux produced the same
guest reports and full final images for all 11 inputs. The bad-dump
control logged an unknown type and invalid checksum and stayed at the
Nintendo logo. The MBC7, Camera, and multicart captures showed their
opening screens or menus. This run measures startup and bounded execution.
The generated mapper tests cover banking and state continuation separately.

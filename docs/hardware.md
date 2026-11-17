# Hardware support

CupidGB implements Game Boy and Game Boy Color execution in the same C core, with color-specific banking and registers in `src/gbc`. A recognized model or cartridge header does not establish complete hardware accuracy. The [parity checklist](parity.md) tracks the remaining requirements, and the [validation record](validation.md) identifies tested scopes.

## Hardware profiles

| Desktop choice | Core profile | Scope |
| --- | --- | --- |
| Game Boy | `CUPID_GB_MODEL_DMG_ABC` | Grouped monochrome profile |
| Game Boy (DMG0) | `CUPID_GB_MODEL_DMG0` | Early monochrome startup profile |
| Game Boy Pocket / Light | `CUPID_GB_MODEL_MGB` | Pocket-family profile |
| Game Boy Color | `CUPID_GB_MODEL_CGB` | One color profile with monochrome compatibility mode |
| Super Game Boy | `CUPID_GB_MODEL_SGB` | SGB NTSC clock, command, palette, attribute, mask, and border behavior |
| Super Game Boy 2 | `CUPID_GB_MODEL_SGB2` | Separate SGB2 startup profile |

These are the six current profile tags. Individual CGB hardware revisions, separate SGB regional/host variants, and every model-specific startup or bus difference have not been qualified. Keep the physical hardware model separate from the cartridge's color capability: a header must not turn selected monochrome hardware into color hardware.

The SGB profile uses a 4,295,454 Hz base clock. The other five profiles, including SGB2, use 4,194,304 Hz. A regular LCD frame still spans 70,224 base T-cycles; converting those cycles to wall time must use the selected clock. CPU, display, audio sample scheduling, and host pacing share that model choice. SGB PAL and separate SGB host-integration profiles remain unimplemented.

Automatic desktop selection is a convenience policy, not a new hardware profile. Reproducible comparisons should select the model explicitly and record whether startup used a boot binary or the approximate post-boot profile.

## Cold-start memory

Cold memory varies by model. An independent per-instance generator supplies the following distributions; the percentages describe each bit's probability of being set across seeds.

| Profile | WRAM | HRAM | OAM | Wave RAM before firmware |
| --- | --- | --- | --- | --- |
| Grouped DMG and SGB | 75% where address bit 8 is clear, 25% where set | Even bytes 12.5%, odd bytes 87.5% | 87.5% where address bit 1 is clear, 12.5% where set | Even bytes 87.5%, odd bytes 12.5% |
| Pocket | Uniform bytes | Same monochrome bias | Same monochrome bias | Even bytes 75%, odd bytes 25% |
| SGB2 | `0x55` baseline, each bit flipped with 12.5% probability | Same monochrome bias | Same monochrome bias | Same grouped monochrome bias |
| CGB | Uniform bytes | Uniform bytes | Zero | Zero |

The wave biases cover FF30–FF3E; FF3F starts at zero. Cold VRAM is zero. Physical CGB palette bytes vary across their full eight-bit storage range, including the unused high color bit. RGB555 output masks that bit while palette reads and native states retain it. Unmapped monochrome backing banks remain zero.

The grouped monochrome distribution corresponds to the audited DMG-B behavior, and the current uniform CGB distribution corresponds to CGB-E. DMG0 uses the grouped fallback without separate qualification. Other CGB revision distributions remain pending. The SGB comparison profile is itself unverified on physical hardware; matching its modeled distribution does not resolve that limit.

Without external firmware, startup applies an approximate post-boot state: physical CGB WRAM bank 2 is cleared and wave RAM has the expected alternating `00`/`FF` pattern. This bank clear applies to both color and monochrome cartridges on CGB. Initial firmware startup reconstructs the original cold generation so firmware performs its own writes, including any scratch data it leaves in bank 2. Other post-boot memory effects and independently written startup firmware remain incomplete. [The core API](core-api.md#cold-memory-and-firmware-startup) documents the separate cold-start and memory-preserving re-entry operations.

SGB and SGB2 post-boot profiles enter VBlank on line 144 at dot 200. This gives the pixel pipeline a valid starting scanline and permits immediate native save/load followed by continued rendering. The profile does not reproduce the host introduction sequence or every firmware timing effect.

The CGB WRAM selector retains its raw register value separately from the effective memory bank. At cold firmware entry FF70 reads `00` while bank 1 is selected. A write sets the unused upper bits: writing zero reads back `F8`, and writing one reads back `F9`; both select bank 1. Echo RAM uses that same physical bank. Native states already store the raw I/O byte and effective bank independently, so this correction does not change the V12 layout.

Palette data ports FF69 and FF6B are accessible while the boot overlay is mapped, including on the current monochrome profiles. Monochrome palette storage starts at zero, but its index ports remain unavailable: index writes are ignored, so data writes address byte zero without auto-increment. Removing the overlay hides the data ports and ignores further writes in monochrome mode. Physical CGB retains its separate index, compatibility-mode, and PPU access rules.

## Cartridge controllers

The core decodes ROM-only, MBC1, MBC2, MBC3, MBC5, MMM01, MBC6, MBC7, HuC1, HuC3, Camera, and TPP1 cartridges. Coverage differs by controller.

| Family | Current scope and limits |
| --- | --- |
| ROM-only | Fixed ROM; explicit ROM+RAM types `08` and `09` retain direct RAM, with battery storage for `09` |
| MBC1 | ROM/RAM bank selection, wired bank aliases, and logo-detected multicart layouts; multicart RAM remains in bank 0 |
| MBC2 | Address-dependent control and built-in 512 by 4-bit RAM |
| MBC3 | Banked ROM/RAM, optional clock registers, live/latched clock persistence, halt and carry handling; extended MBC30-sized inputs use eight-bit ROM selection and up to eight RAM banks |
| MBC5 | Nine-bit ROM selection, RAM banking, exact RAM-enable decoding and a motor latch/host observer for rumble cartridges; physical feedback remains unqualified |
| MMM01 | Menu-first and menu-last dump layouts, menu-selected RAM capacity and battery metadata, one-way lock, ROM/RAM masks, multiplex mode, and battery SRAM |
| MBC6 | Partial mapping; flash behavior remains pending |
| MBC7 | Dual access gates, motion latches, 256-byte serial EEPROM, battery files, and mid-command continuation. Physical motion input remains pending |
| HuC1 | Independent six-bit ROM and three-bit RAM selection, RAM access outside infrared mode, and a shared timed infrared receiver with host input and output callbacks. Physical transport and startup mapping qualification remain pending |
| HuC3 | ROM bank zero after selection, 16 SRAM banks, read-only and read/write modes, nibble commands, clock and alarm persistence, and the shared timed infrared receiver. Physical infrared transport and alarm notification remain pending |
| Camera | ROM/SRAM banking, shutter timing, generated sensor pixels, calibrated gain, exposure and edge processing, tile conversion, and capture continuation. Paired digital-image checks cover all gain and edge selections. Qt captures from the default camera and keeps host images and requests outside native files. Generated-frame checks cover Windows Qt 6.8.3 and Linux Qt 6.4.2; the combined build and physical-device workflows remain to be qualified. See [Camera cartridges](host-camera.md) |
| TPP1 | Extended header, mirrored bank registers, up to 2 MiB of read-only or writable SRAM, RTC commands and persistence, and four guest rumble levels. Motor feedback averages hardware deliveries at presentation boundaries; the frontend applies the configured strength response |

MBC6 exposes partial mapping without flash support. The cartridge image limit is 1 GiB, covering the 65,536 bank addresses available through TPP1's two ROM registers. Allocation follows the actual input length with a 32 KiB minimum. Individual controllers retain their own bank-selection wiring. Cartridge RAM uses the 128 KiB inline array for ordinary populations; larger TPP1 populations allocate their declared SRAM length, through 2 MiB. These limits do not establish tested gameplay at every capacity. See [storage validation](validation-cartridge-storage.md) for the measured scope.

Physical ROM bank population follows the actual file length, rounded to the next power of two with a minimum of two 16 KiB banks. Bytes in an unpopulated tail read as `0xff`. Header ROM-size metadata does not override the loaded bank population, and an unknown size code does not reject an otherwise loadable image. Cartridge identity and native-state hashes retain the original, unpadded input bytes. A ROM-only type-0 header on an image larger than 32 KiB selects an MBC3 banking fallback. An invalid header checksum is reported without necessarily rejecting the image.

RAM capacity follows the resolved cartridge hardware. A nonzero RAM-size byte alone does not add storage to an ordinary ROM-only or unsupported cartridge. Recognized bank controllers can still expose RAM described by that byte even when their type byte says no RAM. MBC2 and MBC7 use their fixed internal storage; TPP1 uses its extended header. ROM and RAM address aliases use the power-of-two chip geometry established by the loader.

| TPP1 SRAM code | Banks of 8 KiB | Active bytes |
| ---: | ---: | ---: |
| 0 | 0 | 0 |
| 1 | 1 | 8,192 |
| 2 | 2 | 16,384 |
| 3 | 4 | 32,768 |
| 4 | 8 | 65,536 |
| 5 | 16 | 131,072 |
| 6 | 32 | 262,144 |
| 7 | 64 | 524,288 |
| 8 | 128 | 1,048,576 |
| 9 | 256 | 2,097,152 |

Other TPP1 SRAM codes declare no storage. Its eight-bit latch selects all 256 banks, with physical aliases for smaller populations. Mode 2 permits reads and mode 3 permits reads and writes. This geometry does not widen other controllers' RAM wiring.

MMM01 menu detection uses the final 32 KiB of the padded physical ROM geometry, not the last 32 KiB of an incomplete file. Both menu-first and menu-last dumps retain their original bytes. The menu supplies RAM capacity and battery capability; a no-RAM menu's promotion also depends on the first game's RAM declaration. Menus declaring type `11` do not inherit a game's battery, timer or motor flags. Paired generated cases cover conflicting headers, both layouts, erased tails and truncated menu metadata.

Two-KiB SRAM mirrors across the eight-KiB RAM window. On ordinary timer-equipped MBC3 cartridges, banks 4 through 7 return a driven `FF` only when SRAM is physically present; without SRAM, the bus retains its undriven behavior. Wider RAM wiring applies to extended inputs larger than 2 MiB ROM or 32 KiB RAM. These rules have targeted regression coverage, but do not qualify every undocumented board or decaying open-bus behavior.

The frontend saves battery SRAM, MBC3, TPP1 and HuC3 clock data, and MBC7 EEPROM to `.sav` files. Clock cartridges support host-time synchronization or Accurate emulated-time scheduling. Counter and latch rules remain controller-specific, including HuC3 minute behavior and separate TPP1 clock status and motor strength. See [cartridge clocks](rtc.md) and [battery saves](battery-saves.md) for the policy, counter and persistence contracts.

## CPU, memory, and timing

The CPU executes SM83 instructions with instruction-specific bus phases, interrupt dispatch, delayed interrupt enable, HALT, and STOP handling. CPU and timer work use machine cycles; elapsed guest time is counted in base-speed T-cycles. CGB double speed increases CPU-domain work without doubling LCD or sound generator clocks.

The eleven illegal primary opcodes consume their fetch cycle, clear IE, and halt the CPU. Graphics, sound, timer, and serial clocks continue under the ordinary HALT rules; OAM DMA remains suspended until wakeup. Pending interrupt requests cannot wake the CPU while IE stays clear. Debugger execution reports the illegal fetch separately from a core failure. Every CB-prefixed sub-opcode remains valid.

Interrupt entry samples enable and request state separately because either stack byte can target an I/O register. STAT transients and LCDC control phases can change which interrupt wins before the T18 acknowledgment. The entry still consumes 20 CPU T-cycles, including cancellation. [Timing and clock domains](timing.md) describes the tested stack-register phases and the limits of that coverage.

The memory map includes cartridge windows, VRAM, banked WRAM, echo RAM, OAM, I/O, high RAM, and interrupt enable. Device access restrictions depend on LCD and DMA state. Open-bus decay, fine bus conflicts, interrupt races, and revision differences remain incomplete. Passing ordinary instruction tests cannot establish those behaviors.

The timer derives edges from divider phase and models delayed TIMA reloads and register-write windows. Serial supports an internal slow clock, the color fast clock, external-clock stepping, and host endpoints. Completed transfers can notify a host callback without changing hardware execution or writing to a host stream. The built-in printer implements packet transfer, image output, and timed jobs. The Qt frontend exposes the printer; paired cable sessions and physical transport remain pending.

OAM DMA and CGB general/HBlank DMA are implemented. CGB transfers retain the fetched opcode, schedule sixteen-byte blocks through two-dot slots, and preserve pending HBlank work across state capture. Shared-bus decay for undriven source ranges and PPU fetch-address conflicts with mode-3 destinations remain incomplete. See [DMA timing](timing.md#dma-and-serial-consequences) for scheduling and sleep behavior. Bus contention, OAM restarts, collisions, and revision combinations still need broader coverage.

## Graphics

The pixel pipeline keeps background and object fetch queues across LCD dots. It handles scroll, window activation, object selection and priority, color attributes, palettes, and a latched color object-priority choice. The PPU owns scanline progression, access modes, LCD startup, STAT signaling, and DMA coordination.

Window start handling retains the initial fractional position, early-window hold and pending zero-pixel insertion. Physical CGB still fetches hidden objects when a monochrome cartridge disables their display. Monochrome object-disable writes can interrupt a fetch while retaining its final pending step. These states survive native snapshots.

VRAM reads at the first HBlank edge use retained fetch addresses and a separate read latch. The CPU-selected bank is independent of the PPU fetch bank. Quiet display rows can leave the earlier committed address history intact, so matching the completed image alone does not establish the correct guest read. See [display delivery and VRAM read history](timing.md#display-delivery-and-vram-read-history) for the access and ownership rules.

Coincidence and mode sources feed one combined STAT interrupt line. A rising coincidence source must preserve a pending HBlank transition; suppressing a second interrupt edge does not cancel the display's mode change. LCD power cycling preserves the comparison and combined-line latches until startup refreshes them. Automatic refresh follows the measured model-specific startup edge, while explicit STAT and LYC writes can update the comparison earlier.

SGB behavior includes packet reception, palette and attribute commands, screen masking, and four player input masks. Transfers capture rendered shades on the third host frame. Character and map uploads stage the next border while the active border transitions. Host frames also advance during modeled LCD-off and STOP intervals. SGB audio extensions, PAL timing, and remaining display phase differences are still open.

Complete fetcher timing, mid-scanline conflicts, broad pixel comparisons, model-specific startup, and hardware color response remain active accuracy work. Host scaling, optional blending, and palette presentation do not change those requirements.

## Audio

The APU has two square channels, a wave channel, and a noise channel with routing, gain, envelopes, sweep, a divider-driven sequencer, and sample integration. Signed stereo output defaults to 44,100 Hz. The core accepts output rates from 8,000 through 192,000 Hz without changing oscillator timing and includes a configurable DC-blocking filter API.

Sweep, envelope, wave, interference, and revision-specific quirks need wider testing. Output samples, host playback, and recordings are different observations. Exact sample reproduction after a state restore establishes continuation for that scenario; it does not prove that the sound matches hardware.

## Input and startup

Each player's eight buttons use an active-low mask: Right, Left, Up, Down, A, B, Select, Start. JOYP selects the direction/action rows and the SGB protocol selects the player. Input interrupts and STOP wake behavior follow selected register lines rather than host events alone. Nonselected players do not create an interrupt merely by changing their physical mask. The Qt frontend supplies keyboard and controller input; [controller workflows](controllers.md) cover mappings and assignments. Contact bouncing, row-switch settling, motion and physical-device qualification remain incomplete.

An optional local boot binary overlays the startup address range. The divider starts eight CPU T-cycles into its period. FF50 reads `FE` while the overlay is active and `FF` after a write with bit 0 set disables it. At power-on, DMA, OBP0, and OBP1 start at `FF` on the current monochrome profiles and `00` on CGB. Without firmware, CupidGB initializes an approximate post-boot profile. Match the firmware and model in comparative tests. The accepted filenames and sizes are listed in the [user guide](user-guide.md#boot-behavior).

Physical CGB hardware enters an actual boot overlay with color registers available even for a monochrome cartridge. Boot code can select compatibility through KEY0 before removing the overlay through FF50. Palette initialization and WRAM selection remain available during the boot sequence; post-boot compatibility gates apply after overlay removal. The no-boot convenience profile selects its initial mode from the cartridge header instead. These two startup methods are distinct comparison inputs.

Changing KEY0 does not reset the physical VRAM or WRAM bank latches. CPU data accesses and WRAM echo aliases continue through the retained bank, even when compatibility mode blocks a later register write. A visible bank-register value alone is insufficient to validate this mapping; check which bank received the data.

CGB register dispatch includes DMA status, palette indexes, the infrared port, undocumented storage/masks, and read-only PCM amplitude registers. The CGB, HuC1, and HuC3 receivers share charge, decay, detection, and host input. Each output latch reports changed levels through a host callback. Physical CGB hardware accepts output writes in monochrome compatibility mode while hiding port reads; the receiver still follows its model and cartridge gates. Native snapshots preserve receiver and display-delivery history while keeping the current host connection. Generated comparisons cover register/callback behavior, stable sensing and bounded transition histories. Wider transition sequences, hardware revisions and physical infrared transport remain unqualified. PCM register access does not establish complete audio accuracy.

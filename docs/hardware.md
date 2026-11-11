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

## Cartridge controllers

The core decodes ROM-only, MBC1, MBC2, MBC3, MBC5, MMM01, MBC6, MBC7, HuC1, HuC3, Camera, and TPP1 cartridges. Coverage differs by controller.

| Family | Current scope and limits |
| --- | --- |
| ROM-only | Fixed ROM and optional RAM, with battery files when the header identifies battery storage |
| MBC1 | ROM/RAM bank selection, wired bank aliases, and logo-detected multicart layouts; multicart RAM remains in bank 0 |
| MBC2 | Address-dependent control and built-in 512 by 4-bit RAM |
| MBC3 | Banked ROM/RAM, optional clock registers, live/latched clock persistence, halt and carry handling; extended MBC30-sized inputs use eight-bit ROM selection and up to eight RAM banks |
| MBC5 | Nine-bit ROM selection, RAM banking, exact RAM-enable decoding and a motor latch/host observer for rumble cartridges; physical feedback remains unqualified |
| MMM01 | Menu-first and menu-last dump layouts, one-way lock, ROM/RAM masks, multiplex mode, and battery SRAM |
| MBC6 | Partial mapping; flash behavior remains pending |
| MBC7 | Dual access gates, motion latches, 256-byte serial EEPROM, battery files, and mid-command continuation. Physical motion input remains pending |
| HuC1 | Independent six-bit ROM and three-bit RAM selection, RAM access outside infrared mode, and disconnected infrared register behavior; transport and startup mapping qualification remain pending |
| HuC3 | ROM bank zero after selection, 16 SRAM banks, read-only and read/write modes, nibble commands, clock and alarm persistence, and timed infrared register input and output. Host infrared transport and alarm notification remain pending |
| Camera | ROM/SRAM banking, shutter timing, generated sensor pixels, tile conversion, and capture continuation. Analog gain is approximate. Host camera input remains pending |
| TPP1 | Extended header, mirrored bank registers, read-only and writable SRAM, RTC commands and persistence, and four guest rumble levels. Host feedback uses an on/off latch |

MBC6 exposes partial mapping without flash support. The maximum cartridge image size is 8 MiB. The core supports up to 128 KiB of cartridge storage, including TPP1. Larger TPP1 SRAM configurations are rejected before cartridge replacement.

Physical ROM bank population follows the actual file length, rounded to the next power of two with a minimum of two 16 KiB banks. Bytes in an unpopulated tail read as `0xff`. Header ROM-size metadata does not override the loaded bank population, and an unknown size code does not reject an otherwise loadable image. Cartridge identity and native-state hashes retain the original, unpadded input bytes. A ROM-only type-0 header on an image larger than 32 KiB selects an MBC3 banking fallback. An invalid header checksum is reported without necessarily rejecting the image.

Two-KiB SRAM mirrors across the eight-KiB RAM window. Ordinary timer-equipped MBC3 RAM banks 4 through 7 are disconnected; the wider RAM wiring applies to extended inputs larger than 2 MiB ROM or 32 KiB RAM. These rules have targeted regression coverage, but do not qualify every undocumented board or decaying open-bus behavior.

The frontend saves battery SRAM, MBC3, TPP1 and HuC3 clock data, and MBC7 EEPROM to `.sav` files. See [battery saves](battery-saves.md) for the supported encodings.

## CPU, memory, and timing

The CPU executes SM83 instructions with instruction-specific bus phases, interrupt dispatch, delayed interrupt enable, HALT, and STOP handling. CPU and timer work use machine cycles; elapsed guest time is counted in base-speed T-cycles. CGB double speed increases CPU-domain work without doubling LCD or sound generator clocks.

Interrupt entry samples enable and request state separately because either stack byte can target an I/O register. STAT transients and LCDC control phases can change which interrupt wins before the T18 acknowledgment. The entry still consumes 20 CPU T-cycles, including cancellation. [Timing and clock domains](timing.md) describes the tested stack-register phases and the limits of that coverage.

The memory map includes cartridge windows, VRAM, banked WRAM, echo RAM, OAM, I/O, high RAM, and interrupt enable. Device access restrictions depend on LCD and DMA state. Open-bus decay, fine bus conflicts, interrupt races, and revision differences remain incomplete. Passing ordinary instruction tests cannot establish those behaviors.

The timer derives edges from divider phase and models delayed TIMA reloads and register-write windows. Serial supports an internal slow clock and the color fast clock with disconnected incoming data. Completed transfers can notify a host callback without changing hardware execution or writing to a host stream. External serial transport, infrared, and attached devices remain pending.

OAM DMA and CGB general/HBlank DMA are implemented. CGB transfers retain the fetched opcode, schedule sixteen-byte blocks through two-dot slots, and preserve pending HBlank work across state capture. Shared-bus decay for undriven source ranges and PPU fetch-address conflicts with mode-3 destinations remain incomplete. See [DMA timing](timing.md#dma-and-serial-consequences) for scheduling and sleep behavior. Bus contention, OAM restarts, collisions, and revision combinations still need broader coverage.

## Graphics

The pixel pipeline keeps background and object fetch queues across LCD dots. It handles scroll, window activation, object selection and priority, color attributes, palettes, and a latched color object-priority choice. The PPU owns scanline progression, access modes, LCD startup, STAT signaling, and DMA coordination.

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

CGB register dispatch includes DMA status, palette indexes, infrared storage, undocumented storage/masks, and read-only PCM amplitude registers. A physical color model and color execution mode are separate gates. Disconnected infrared register behavior does not establish external infrared transport, and PCM register access does not establish complete audio accuracy.

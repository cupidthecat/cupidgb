# Save states and rewind

CupidGB native states preserve CPU, timer, DMA, serial, graphics, cartridge, SGB, and audio state after a completed hardware step. A held adapter-startup step is also a valid capture boundary. States include pending handheld PCM, startup synthesis history and unread startup batches. They exclude ROM binaries, boot binaries, host paths, Qt resources, serial observer pointers, and queued host-device audio. Loading a state preserves the current serial observer registration and does not replay earlier host notifications.

## Desktop workflows

Use the state menus or Ctrl+1 through Ctrl+9 and Ctrl+0 to save the numbered slots. Add Shift to load. Slots use `.s0` through `.s9` beside the cartridge. When the primary file is missing, the loader also tries the corresponding `.sn0` through `.sn9` filename. A damaged primary file produces an error; it does not silently select another file.

Load a native state file through File > Load State File or drop a `.s0` through `.s9` or `.sn0` through `.sn9` file onto an open cartridge. State files are rejected as cartridge inputs.

Writes use an atomic file replacement. Loading checks the complete cartridge identity and hardware model. A state captured with an active boot overlay requires the matching local boot binary. Failed validation preserves the running session and its host audio queue. A successful load replaces the emulated state, reapplies current physical input, clears queued host audio, and realigns pacing.

New files use CupidGB native version 29. Earlier cartridge states, including both supported version 19 development layouts, remain loadable on their original hardware profiles when they contain the required hardware history and SRAM. GBS programs require version 23 or newer and the matching music file already loaded. Desktop loads retain current listening settings, reconstruction and filter history, and the receiving startup synthesis and queued batches. Saved hardware startup phase, host-only time and command-receiver history still restore. Interoperable BESS state imports remain pending.

Host camera images and pending device requests stay with the receiving session. Saving a pending host capture writes an idle camera to the file while the live request continues. After full validation, a load into host-camera mode cancels obsolete requests and clears imported busy state while retaining the current image. This also applies to a saved generated countdown. Without a provider, generated captures retain their countdown, while a host-origin snapshot starts idle. The existing 64-byte camera section and strict busy/countdown validator are unchanged. See [Camera cartridges](host-camera.md) for reset, rewind and embedding rules.

The version identifies the wire encoding. Subsequent execution follows the current engine. Version 3 records pending CGB DMA work, CPU speed-switch sequencing, and audio clock, held-output, envelope, sweep, and noise latches. Version 2 has no values for these latches. Its loader initializes them deterministically after validating the old payload; it never takes missing hardware history from the target's running session. Pending DMA and speed-switch delays start cleared, while an old armed HBlank transfer retains its saved block count. On color hardware, pulse clock startup follows the saved channel enable, and held pulse output follows the saved duty position. Monochrome pulse history uses the import rules described under version 11 below. Noise gates and held output derive from the saved channel/DAC and LFSR state, with a canonical prescaler phase. The old LFSR, timer and queued PCM remain intact. Version 14 initializes missing reconstruction and filter history as described below. Keep the original state if you need to revisit its earlier build.

SGB states continue under the selected model's corrected NTSC clock. Legacy audio import derives output coefficients from that clock and the saved sample rate while preserving queued samples. Version 21 separately records host-only startup time alongside the existing model and guest cycle count. Older files restore startup as completed, with zero additional host time; they do not replay the introduction. Exact continuation comparisons require the same qualified build and inputs.

Older builds could save the untouched SGB or SGB2 post-boot profile with LY at `FF` and an uninitialized pixel pipeline. Continuing that state produced an invalid internal scanline. The loader recognizes the initial CPU, divider-register, and display signature at guest cycle zero and restores the modeled VBlank entry on line 144 at dot 200. It changes only LY, STAT, and the display phase; the ROM-dependent DIV subphase, memory, host registrations, and battery bookkeeping remain intact. Other states retain their literal saved values. This recovery uses the existing version 12 layout.

Version 1 execution states cannot continue in the changed pixel and audio core. File > Recover Battery from Older State validates the older file and exports supported battery data to a separate file. Recovery preserves the original file and running session. It does not restore execution. See [battery saves](battery-saves.md).

## Connected consoles

A state slot belongs to one cartridge window. Loading it restores that console while retaining its current cable connection, the peer's state and the cable's host-owned outgoing bit history. The shared scheduler rebases the restored clock origin while preserving the relative instruction lead between endpoints. Live infrared input also stays with the receiving session. These host values add no fields to the native format.

Two separately saved files are not an atomic snapshot of the pair. Restoring one side during a game's transfer can disrupt its protocol. Rewind and debugger Backstep are unavailable while connected because their history restores only one console. Disconnecting starts a new independent history. See [Link play](link-play.md) for the desktop workflow and [the core API](core-api.md#paired-console-execution) for ownership rules.

## Rewind

Hold Tab to move backward through captured frames. Rewind restores and removes history entries, then executes the selected state forward for one replay interval. With sufficient history, normal rewind first returns to the current frame position and subsequently moves back one frame per interval. Holding Space consumes one additional history entry, moving back one frame initially and two thereafter. The selected Turbo cap and slow-motion rate also apply to replay pacing. Insufficient history ends replay and waits for release; it does not invent another complete frame.

Replay uses current physical input and produces a new image and audio. Entering or leaving rewind clears stale host playback. Live restoration retains ordinary and startup output history together, rebases the ordinary fractional phase, then discards the complete stale PCM stream. Only newly generated replay samples enter playback. Disk loading and debugger Backstep use the same audio ownership rules. Releasing rewind resumes forward execution with current input. Replay samples do not enter the forward-only WAVE recording; startup-origin samples are excluded even during forward execution.

Settings control the requested history duration and memory budget. The default requests up to 120 seconds within a 256 MiB budget. The buffer evicts its oldest snapshots when either limit is reached. History age uses positive presentation-time progress at the selected model's base-clock frequency. This includes a held startup interval even though its guest counter does not advance. Automatic frame capture skips intervals made entirely of held startup calls; explicit state capture remains valid there. Once guest execution resumes, enabled rewind captures each completed frame before applying the history limits. Actual retained duration depends on how well the cartridge's state compresses; the interface reports shorter history when the memory limit is reached. Set the duration to zero to disable capture.

Ordinary Reset, incompatible cartridge or firmware replacement, successful disk-state loading, and relevant configuration changes clear frame history. Quick Reset and Reload retain history when cartridge bytes, model and firmware match. Native console resets keep the loaded assets and their frame history. Failed resets and loads preserve it. Clock-policy changes keep snapshots; replay restores guest clocks while retaining the current policy. Rewind stays in memory.

Rewind age advances independently of the guest cycle counter. A retained-history reset restarts the core timeline without making existing snapshots appear older by an unsigned wraparound. Restoring a snapshot restores its history age and presentation-time baseline together. Identical replacement cartridge bytes can retain snapshots, but their trusted codec context must be rebound to the new allocation.

The debugger has a separate, smaller instruction snapshot history. It captures only explicit guest debugger stepping and is invalidated when ordinary playback or session-changing operations resume. Held startup calls advance presentation without adding instruction snapshots. Breakpoints, symbols, tool windows, and pending host commands are excluded from native state files. See [debugger history](debugging.md#instruction-history-and-playback).

The rewind path caches the identity of the frontend's immutable cartridge. The core context is bound to the instance, ROM pointer, ROM size, successful-load generation, and hardware model. Loading cartridge bytes invalidates an earlier context even when the same allocation and length are reused. Failed loads and native resets preserve the binding. A caller that patches ROM bytes directly must still discard and recreate the context. Disk-state APIs always recompute the full cartridge hash. Future cheat, patch, or debugger features must honor that cache invalidation rule.

## Native format

All numeric payload fields use explicit widths and little-endian encoding. Booleans use validated zero or one bytes. Signed timestamps and PCM samples use explicit two's-complement encoding. The format never stores C structure padding, native pointers, native `size_t`, or native boolean representations.

The 128-byte header contains:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 8 | `CUPIDGB` followed by a zero byte |
| 8 | 4 | Format version |
| 12 | 4 | Complete encoded length |
| 16 | 4 | Stable hardware model tag |
| 20 | 4 | Cartridge byte length, or prepared image length for GBS |
| 24 | 4 | Required boot length, zero when the saved overlay is inactive |
| 28 | 4 | Flags; bit 0 means an active boot overlay, bit 1 identifies GBS since version 23 |
| 32 | 32 | Program identity: cartridge SHA-256 or the version 23 GBS digest below |
| 64 | 32 | SHA-256 of the required boot binary, otherwise zero |
| 96 | 32 | SHA-256 of header bytes 0 through 95 followed by the payload |

Word arrays check their complete byte range once, then read or write individual little-endian elements. This accepts unaligned buffers and preserves all array values without relying on host byte order. Encoding and loading still require the exact final schema offset, and loading retains every integrity and hardware-domain check.

The fixed ordinary PCM array includes consumed storage beyond its queued-frame count. Draining moves the unread frames and leaves retired slots intact, so different drain capacities can produce different native bytes after delivering identical samples. Exact continuation comparisons therefore use the same drain history. The version 21 startup extension has a separate encoding that stores only unread mono samples.

The payload follows the explicit field order in `src/gb/state.c`. Version 2 occupies 300,518 bytes including the header. Its payload layout remains the version 3 prefix. It contains audio sample integration, filter state, pixel fetch queues, pending object/window work, the latched CGB object-priority selection, and the CPU's first HALT polling phase. Version 3 occupies 300,568 bytes and appends the following explicit fields without inserting bytes into that prefix:

| File offset | Bytes | Added state |
| --- | --- | --- |
| 300,518 | 3 | DMA ready flag, HBlank delay and sleep-entry wake permission |
| 300,521 | 2 | APU power-relative phase and deferred envelope edge |
| 300,523 | 16 | Two pulse-channel startup, held-output and envelope timing records |
| 300,539 | 9 | Sweep setup/calculation/restart counters, deltas and zero-shift latches |
| 300,548 | 12 | Noise divider, phase, prescaler and eight edge/output flags |
| 300,560 | 8 | Speed-switch wait/toggle/freeze, STOP divider hold and LCD alignment |

Call `cupid_gb_state_size` instead of hard-coding these lengths in a host. Adding or reordering hardware fields requires a format revision and a deliberate fixture update. The older recovery reader uses a separate frozen 300,349-byte version 1 layout.

Version 4 occupies 300,572 bytes. It preserves the complete version 3 prefix and appends three active-low SGB player masks at offsets 300,568 through 300,570, then the canonical MBC5 motor boolean at 300,571. The existing `joypad` byte remains player 1. Version 2/3 imports release players 2–4 and clear the motor latch, which those versions never recorded. They preserve known player 1 input and the older payload. The loader rejects a motor asserted on an unsupported cartridge and rumble-cartridge RAM banks outside 0–7; the older core had already discarded the motor control bit before saving.

Version 5 appends the sections required by the selected model and cartridge after the version 4 payload. SGB and SGB2 append 10,119 bytes for the host frame phase, pending transfer, border transition, and staged border. MMM01 appends four bytes for ROM selection, RAM and upper ROM selection, mode, and RAM mask. MBC7 appends eight bytes for the serial command, output shift, pending data count, pins and access/programming flags. Camera appends 64 bytes for its registers, capture number, clock phase, busy countdown, and register mapping. TPP1 appends 21 bytes for bank registers, mode, status, clock values, and the clock baseline. When both SGB and mapper sections apply, the SGB section comes first. Earlier snapshots seed the staged border from the visible border and reset transfer timing or missing mapper latches. MBC7 EEPROM data stays in the cartridge RAM portion of the state.

Version 6 also carries a 25-byte HuC3 section on applicable cartridges. It stores the clock and alarm, command index and response, and infrared sensing state. Every version 6 file ends with the LCD row phase and first-frame flag, then the cartridge bus hold time in half-dots and the held byte. An expired bus is encoded as `00 FF`. Older snapshots restore an expired bus and derive the initial LCD flags from the saved model and startup state.

Version 7 preserves that prefix and appends the OAM DMA phase as one byte. Its active values are 0 through 3 CPU T-cycles. Idle transfers encode zero. Version 2 through 6 imports initialize phase 2 and project the older pending-start and restart counters into the current scheduler. The older formats did not retain the exact subcycle history.

Version 8 appends eleven bytes after the V7 tail: the remaining JOYP row-release delay and requested select bits, little-endian 32-bit presentation-cycle and CGB repeat countdowns, and the presentation event type. Idle JOYP fields are zero. Older imports clear these fields; they cannot reconstruct a pending row release or display callback that the file did not record. Version 8 also permits the existing main-bus timing bytes on monochrome models, whose WRAM/OAM share that bus. Older V6/V7 files still require their original zero monochrome bus history. The older payload offsets remain unchanged.

Cheat collections, global enable, color correction, and memory-search history are host configuration outside snapshots. Loading keeps the current rules and display settings.

Device handles, assignments, bindings, rumble and infrared callbacks, live infrared input, feedback duty counters, serial link registrations, and the printer feed are host data outside the wire format. Loading emits no motor or infrared callback. The frontend stops and resynchronizes host feedback; a later forward interval may resume the saved motor state. Version 12 separately records the built-in printer's emulated state while preserving its live callback registrations and the host's connection choice.

Loading first verifies version, length, integrity, cartridge/model/boot identity, and field domains. It decodes into a separate candidate that retains the current session's ROM ownership and immutable metadata. Only a completely validated candidate replaces the live state.

Version 9 appends six bytes after the V8 tail: a little-endian retained CGB VRAM row, a little-endian OAM search word, the sampled STOP VRAM access gate, and the object fetch stage. The row uses `FFFF` when no active CGB VRAM-source OAM transfer can use it; assigned rows range from `0000` through `1FFF`. HALT, STOP and LCD power state do not invalidate an active row. Saving canonicalizes unused row bytes without changing the running machine. The search word retains OAM Y/X or sprite tile/flags history across scanlines, LCD transitions and DMA restarts. It is never reconstructed from selected objects. Older snapshots initialize the row to `FFFF`, the search word to zero and the STOP gate to false.

The object stage stores idle, background wait, pending OAM word read, or legacy completion. New stages use an object-data countdown no larger than five; background wait and pending word read require zero. A legacy completion retains the older countdown from one through eleven and the saved object address, then returns to the current scheduler. Older snapshots with an in-flight object import through that completion stage; idle snapshots import idle.

Version 10 appends four bytes after the V9 tail: the sampled STOP OAM and palette gates, followed by the little-endian last-pixel dot. The gates preserve PPU access while the CPU is stopped; waking clears them. The last-pixel dot retains the palette port's delayed release independently of readable STAT. Older files clear the missing STOP gates and last-pixel dot. These defaults cannot recover the original access history.

Version 11 appends two pulse DAC codes, one byte per channel, each restricted to 0 through 15. An ordinary file without model or cartridge extensions occupies 300,600 bytes including the header. The existing pulse timing records now retain monochrome clock gates, sampled duty output and trigger suppression as well as color timing. DAC disconnect can preserve a digital code through re-enabling the DAC; these two bytes retain that history.

Versions 2 through 10 did not maintain the monochrome pulse latches. After validating an older file, the loader derives held duty output from its duty position and starts a clock only when the APU and DAC are enabled and the saved channel or timer indicates activity. It clears unknown trigger suppression and uses a canonical edge age. Disconnected pulse DAC codes default to zero on every model. Saved oscillators and queued PCM remain intact. Missing reconstruction/filter history uses the version 14 import rules. A deterministic starting point cannot recover an earlier duty write, expired clock, or disconnected DAC sample.

Pixel queues and fetch phases recorded by older formats remain intact. Current execution uses the current fetch timing; importing an older snapshot cannot recover bus or scheduling history that the file did not record. Exact continuation comparisons require the same qualified build and inputs.

### Printer state in version 12

Version 12 keeps the hardware payload and version 11 tail in their existing order. Non-HuC3 cartridges then append six bytes of infrared history: little-endian 16-bit receiver charge and decay counters, followed by one-byte cartridge-output and detection flags. HuC3 retains these fields in its existing 25-byte section and adds no duplicate record. Older non-HuC3 files initialize the absent history to zero while keeping the current host input and callback.

An eight-byte accessory descriptor follows that hardware history. Offsets in this table are relative to the descriptor:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 1 | Device kind: zero for no saved device, one for the built-in printer |
| 1 | 1 | Device codec: zero without a device, one for the printer |
| 2 | 2 | Reserved, both zero |
| 4 | 4 | Little-endian device payload length: zero or 32,687 bytes |

The printer record follows the descriptor. Its explicit field order is in `src/gb/printer_state.c`: packet phase and header, decoded data and indices, checksums and compressed-run history, status and image rows, the image buffer, input/output bit history, and the fractional clock and remaining job time. It contains no pointers, callbacks or host paper. An ordinary state without model or cartridge extensions occupies 300,614 bytes without a printer and 333,301 bytes with one. Hosts must still query `cupid_gb_state_size` for each capture; connecting a printer changes the required length.

| Saved attachment | Current attachment | Load result |
| --- | --- | --- |
| Printer | Printer | Restore the saved packet, bits, image buffer and job timing; keep current host callbacks |
| No device, or a version 2–11 file | Printer | Start a fresh printer at the loaded guest time |
| Printer | No device or an external cable | Validate the saved printer record, retain the current attachment and discard that device history |
| No device | No device or an external cable | Retain the current attachment |

Capture leaves the printer's clock untouched. Elapsed guest cycles can legitimately remain pending until its next update, and matching-device restoration preserves that debt and its fractional tick. Fresh-device imports start their clock at the loaded guest time. If a detached printer is later attached to an earlier console timeline, attachment rebases its clock and clears the fractional tick without consuming or discarding its pending job. It can then be captured immediately. Reset clears printer history at the reset boundary.

The loader validates the entire console and accessory before committing either. An invalid device record leaves both live objects unchanged even when that device is not attached. Loading emits no image or completion callback. Save output, its written-length result and any trusted context must occupy storage separate from the live printer, core and cartridge.

The Qt feed remains host output. A successful disk load, rewind or debugger Backstep preserves paper already emitted into the current feed; executing a print request again can append another strip. Closing the feed still starts a new roll when the next image arrives. PNG export and system printing use their explicit controls. Queued widget work renders the current feed and cannot reopen it for a replacement cartridge.

### Cartridge clocks and divider phase in version 13

Version 13 inserts six hardware-history bytes after the version 12 infrared history and before the accessory descriptor. They are present on every cartridge:

| Relative offset | Bytes | Added state |
| --- | --- | --- |
| 0 | 4 | Little-endian RTC phase in half-base-clock units |
| 4 | 1 | TPP1 motor strength, zero through three; zero on other controllers |
| 5 | 1 | Unheld CPU T-cycles since the last divider edge, zero through three |

An ordinary version 13 state without model or cartridge extensions occupies 300,620 bytes without a printer and 333,307 bytes with one. Hosts must query the size for each capture. The earlier hardware field order and accessory descriptor remain intact.

RTC phase can exceed the receiver's current clock period. Loading restores that literal debt without polling time, advancing counters or applying a mode change. Clock mode, period multiplier, time provider and provider context remain those of the receiving instance. Rewind and debugger Backstep follow the same ownership rule. TPP1 motor strength is separate from its stored clock status, so motor commands no longer overwrite those status bits.

The divider remainder belongs to guest execution even when a CPU instruction has completed. A partial DMA interval can leave two CPU T-cycles toward its next edge. Restoring this remainder preserves later timer, serial and audio scheduling. It is independent of the CPU's temporary bus cursor, LCD phase, STOP hold and RTC fraction. DIV writes restart it at zero. Invalid divider or motor bytes reject the complete load without changing the live core or attached device.

Versions 2 through 12 initialize the missing RTC phase and divider remainder to zero. They preserve the literal legacy `timer_counter` field. If an older file contains a TPP1 section, its stored status remains intact and its low two bits also initialize motor strength. A missing section starts with zero motor strength. Native timestamp values remain literal, including zero; loading does not reinterpret zero as an uninitialized host clock. The old formats cannot recover subcycle or fractional-clock history they never recorded.

### Audio reconstruction in version 14

Version 14 adds 4,434 bytes after the RTC/divider history and before the accessory descriptor. It preserves the earlier hardware offsets. The added section contains four channel reconstruction rings and integrals, input voltages, held output/quiet counters, DAC charge and the 32-frame monochrome delay, filter capacitors and interference history, listening policy, fractional reconstruction phase, render spacing, deterministic noise state, digital DAC samples and pending APU clocks. Its final two bytes retain the monochrome noise feedback latch and trigger delay.

Floating-point fields use explicit little-endian IEEE 754 binary64 encoding. Booleans, channel/cursor indexes, gain delays and rate-derived caches are checked. Nonfinite histories and clock-credit combinations that could exceed the reconstruction window are rejected. The retired integer mixer and capacitor slots in the old prefix are reserved zeroes in new files; older files retain their original bounded validation.

An ordinary version 14 state without model or cartridge extensions occupies 305,054 bytes without a printer and 337,741 bytes with one. Query the size for every capture.

The ordinary load APIs restore the entire recorded audio output for exact continuation, including optional interference. The `_live` load APIs finish the saved APU's deferred clocks on the candidate with its own output credit, then discard its queued audio and retain the receiving output history, rate and policy. They reset the reconstruction fraction at the restored guest boundary. Qt uses these APIs for disk loading, rewind and Backstep. Different saved and receiving sample rates do not require discarding pending guest clocks or rejecting an otherwise valid snapshot. Validation and candidate work finish before either the live console or attached printer changes.

Early version 14 writers used a nearest-rounded rate cache. The decoder recognizes its exact expected value and matching span, then replaces that derived cache with the bounded increment while preserving captured history. This prevents an imported SGB stream from resuming positive phase drift. Other cache mismatches remain invalid.

Versions 2 through 13 preserve queued PCM, rational sample credit, the saved rate and the disabled-filter flag. Import validates the former integer mixer/filter history, then initializes missing reconstruction and DAC history from settled saved channels. It cannot reconstruct past edges or analog history those files did not store. Exact old-build waveform continuation therefore requires the original build and state.

### Display, pixel and VRAM history in version 15

Version 15 adds a 20-byte display record, a four-byte pixel record and a ten-byte VRAM record after the version 14 audio section and before the accessory descriptor. Every new file carries all three records. The display record preserves LCD waits, work deferred across CPU deliveries and work forced by a guest bus access. That history affects VRAM reads on every model and infrared sensing where the receiver is available. DIV alone cannot recover it: resetting DIV or reading VRAM can change a later receiver transition without changing the elapsed guest time.

| Relative offset | Bytes | Added state |
| --- | --- | --- |
| 0 | 4 | Signed little-endian display credit in half-dots |
| 4 | 4 | Little-endian frame age in dots |
| 8 | 2 | Little-endian row position in half-dots |
| 10 | 2 | Little-endian deferred batch budget in half-dots |
| 12 | 1 | Internal scanline, zero through 153 |
| 13 | 1 | Display wait phase |
| 14 | 1 | Position within that phase |
| 15 | 1 | Window-check alignment, zero through seven |
| 16 | 1 | Startup, window and forced-processing flags |
| 17 | 1 | Remaining fine-fetch wait in half-dots |
| 18 | 2 | Reserved, both zero |

The following four bytes preserve pixel-fetch timing on every model, including models without infrared:

| Relative offset within pixel record | Bytes | Added state |
| --- | --- | --- |
| 0 | 2 | Signed little-endian logical horizontal position, minus sixteen through 160 |
| 2 | 1 | Fractional-scroll, first-window-fetch, CGB WX-read, monochrome window-hold and pending background-insertion flags |
| 3 | 1 | Reserved, zero |

The logical position includes discarded pixels before the visible row. The CGB WX-read flag is invalid on other hardware. A monochrome window hold requires a matching active window, empty fetch queue and idle object engine. Pending background insertion retains a zero-valued window-edge pixel across an intervening object fetch while leaving the fetched background tile queued. Version 15 also permits a pending monochrome object-fetch abort in the existing object-stage byte. This stage retains the selected object until the next pixel step consumes the abort; earlier versions reject that stage value. These fields preserve fetch work that the visible horizontal coordinate cannot describe.

The final ten bytes preserve the VRAM read circuit and pending fetch addresses:

| Relative offset within VRAM record | Bytes | Added state |
| --- | --- | --- |
| 0 | 2 | Little-endian last tile-map address, zero or `1800` through `1FFF` |
| 2 | 2 | Little-endian last tile-data address, including the PPU bank bit, below `4000` |
| 4 | 1 | Last byte driven by a successful CPU or OAM DMA VRAM read |
| 5 | 1 | Reserved, zero |
| 6 | 2 | Little-endian pending tile-map address, with the same bounds as the committed map address |
| 8 | 2 | Little-endian pending tile-data address, with the same bounds as the committed data address |

The first HBlank read can use a committed fetch address or expose the held byte. Fine display processing commits pending addresses as it consumes fetch work. A completed quiet batch retains the earlier committed addresses, so the pending pair must remain independent through save/load. CPU VRAM bank selection remains independent of the PPU bank recorded in the tile address. An LCD power transition does not reset the VRAM latches, and a VRAM write does not replace the held byte. The display synchronization preceding either guest write can commit pending addresses. Blocked CPU reads and passive inspection do not replace the held byte.

An ordinary version 15 state without model or cartridge extensions occupies 305,088 bytes without a printer and 337,775 bytes with one. Earlier payload offsets and the accessory descriptor retain their encodings. Query the complete size for each capture.

Every supported hardware model maintains the display record, including cartridges without an infrared receiver. Validation checks credit and age bounds, phase-specific cursor limits, batch/phase combinations, flag bits, address ranges and reserved bytes. Pending and committed VRAM addresses may legitimately differ after a quiet row or LCD power-off. An invalid record rejects the whole console and printer transaction. Capture leaves the live display and receiver untouched; it does not consume a pending batch to simplify the saved state.

Versions 2 through 14 have no exact record of this scheduling history. After all other migrations finish, the loader derives a deterministic starting phase and logical pixel position from the saved LCD power, scanline, pixel pipeline, scroll and frame age. Missing batches and window notifications start cleared. The saved working fetch address supplies the known tile-map or tile-data address; the overwritten address and held read byte start at zero. Pending addresses begin equal to those projected committed addresses. The result depends on the loaded file and cartridge, never the receiving session's previous timing history. This permits continued execution but cannot reproduce a prior forced bus access or fetch latch that the older format did not record. All four exact/live and ordinary/trusted loading paths retain the current infrared input and host callbacks.

### TPP1 mapping and input sampling in version 16

Version 16 separates TPP1 ROM-control readback from its currently mapped ROM bank. The existing generic mapped-bank field stores the 16-bit mapping. A native reset exposes control value `0000` while bank `0001` remains mapped. The next controller write commits the raw bank latches to mapping.

Versions 2 through 15 selected ROM from the TPP1 control register and could leave the generic field stale. Import first validates that older field under its original bounds, then projects the control register into the mapped-bank field. Other controllers keep their existing bounds. V15 retains its recorded timing fields; versions below 15 receive the missing-timing projections above. The absent tile-row latch follows the version 17 import rule below. Older payload bytes and frozen V3/V4 fixture hashes stay unchanged.

Version 16 appends one pending-input byte after the complete V15 display, pixel and VRAM records and before the accessory descriptor. Only `00` and `01` are valid. The sampled JOYP register remains in its existing I/O payload slot. Exact and live loads preserve both the sample and pending update, including a capture made after a host event but before hardware sampling. Their separate audio ownership rules remain unchanged.

Versions 2 through 15 stored input masks without a sampled JOYP cache. The loader reconstructs that cache from the saved masks, active row and SGB player state, then clears the absent pending flag. It preserves recorded row-settling history and emits no interrupt or callback. This migration uses only the loaded state. It cannot recover an input event that the older file did not distinguish from its settled sample.

An ordinary version 16 state without model or cartridge extensions occupies 305,089 bytes without a printer and 337,776 bytes with one. Query the complete encoded size for each capture.

### Tile-row retention in version 17

Version 17 uses the previously reserved fourth byte of the pixel-timing record for the retained tile row. The current CGB profile selects this row during the tile-map address half of a background or window fetch. Both tile-data planes use that selection, including vertical flipping. A later SCY write does not change the row of a fetch already in progress. This also applies when color hardware runs a monochrome cartridge.

The encoded size and all other field offsets stay the same as version 16. The loader still requires that byte to be zero in version 15 and 16 files. Older versions reconstruct the missing row from their saved scanline and SCY, or the saved window line when the window is active. That deterministic projection cannot recover a prior SCY value changed after the original tile-map selection. Keep the older build and state when exact historical continuation is required.

The row latch belongs to guest state. Motor sample callback registrations and the frontend's accumulated samples remain host state and add no serialized fields. Exact, live and trusted loading paths retain registrations without emitting samples; the frontend clears its pending feedback at a successful timeline change.

### Serial clock selection in version 18

Version 18 adds one byte after the pending-input byte and before the accessory descriptor. Zero selects the slow serial divider tap; one selects the fast tap. A fast selector is valid only on physical CGB hardware, including a saved state that has subsequently entered monochrome compatibility mode. Other values reject the whole load without changing the console or an attached device. Earlier hardware offsets stay fixed; the descriptor and any printer payload move forward by exactly one byte.

An ordinary version 18 state without model or cartridge extensions occupies 305,090 bytes without a printer and 337,777 bytes with one. The MBC7 extension adds eight bytes. Hosts must continue to query `cupid_gb_state_size` for each capture.

The selector records the last SC write separately from the stored SC byte, master-clock phase, remaining bits and transmit latch. Reset selects the slow tap even though SC reads `7E`. Writing SC selects a tap using the operating mode at that write, including when no transfer is active. A later KEY0 mode change, boot-overlay removal, DIV reset or CPU speed change does not select another tap. DIV and CPU clocks still drive edges of the retained tap. The approximate startup profile used without a boot binary also starts slow; it cannot recover serial writes from firmware that was never executed.

Versions 2 through 17 lack this byte. Import selects fast only when the decoded state has CGB mode enabled and stored SC bit 1 set. It uses the loaded values, never the receiving machine's selector, and preserves the saved master phase, bit count and transmit latch. This projection matches the old engine's tap selection at the saved boundary, but cannot recover earlier SC writes or mode transitions that the file did not record.

Import preserves every stored SC bit and does not issue a control write. Current execution reads SC bit 1 from its retained value on physical CGB hardware, including compatibility mode. For example, a legacy compatibility state with raw SC `7C` now reads `7C`; older builds forced that read to `7E`. Reserved-bit read masks still apply, and monochrome hardware still forces bit 1 high. Rewriting the saved byte would discard recorded state without recovering its absent write history. The same current-engine continuation rule applies to exact, live and trusted loaders.

See [serial selection validation](validation-serial-selection.md) for the original guest programs, retained baseline differences, regression coverage and remaining checks. The existing format fixtures remove the additional byte when constructing earlier versions; their original lengths and frozen hashes remain fixed.

### Wave fetch history in version 19

The wave-history development layout of version 19 adds one hardware byte after the version 18 serial selector and before the accessory descriptor. It records the complete byte last fetched by CH3. All 256 values are valid. The existing raw sample nibble, wave RAM, sample cursor and reconstructed output remain separate fields. A later RAM write can change the next fetch without changing the byte already held by the channel. Earlier hardware offsets stay fixed; the descriptor and any printer payload move forward by one byte.

An ordinary wave-history version 19 state without model or cartridge extensions occupies 305,091 bytes without a printer and 337,778 bytes with one. The MBC7 extension still adds eight bytes. A separate version 19 development layout stores SRAM geometry and its extension without this wave byte. Version 20 combines both additions, as described below. Hosts must query `cupid_gb_state_size` for each capture.

Versions 2 through 18 recorded only the selected four-bit sample. Import duplicates that decoded nibble into both halves of the missing byte: a sample of `9` becomes `99`. It preserves the recorded raw sample and RAM, and never takes missing history from the receiving machine. The unselected half of an earlier fetch cannot be recovered. Duplicating the known nibble gives the next trigger a deterministic starting value until a real fetch replaces it; it does not claim exact continuation of an earlier engine.

Live loads establish the imported byte before executing the saved pending audio clocks. Those clocks use the captured output credit and can fetch a different byte. Only after they finish does the loader restore the receiving output queue, reconstruction history, listening policy and sample rate. The retained fetch byte belongs to guest hardware and survives that output replacement. Ordinary and trusted exact loads preserve the pending clocks without running them. Rejected files leave both hardware and receiving output unchanged.

The startup profile used without a boot binary begins with a zero fetch byte. APU power transitions and native resets also clear it. This does not reconstruct firmware fetches that were never executed. See [wave fetch history validation](validation-wave-history.md) for the original guest proof, regression definitions and outstanding checks. Historical fixture builders remove both the new byte and any other fields absent from their selected version; frozen V2–V4 lengths and digest expectations stay unchanged.

### Complete cartridge SRAM in version 20

Version 20 preserves the hardware prefix through the version 18 serial selector. It then writes the retained CH3 byte, a four-byte little-endian active SRAM length, any SRAM beyond the historical 128 KiB prefix, and the accessory descriptor with its optional printer payload. The declared length must match the receiving cartridge. A file cannot change its hardware geometry or request an allocation of a different size.

The first 128 KiB stays at its established offset. Small cartridges also retain the unused bytes in that fixed prefix. Large TPP1 cartridges store their active first 128 KiB there and append the remaining bytes. The loader prepares independent SRAM and validates the complete console and accessory record before committing either. Failure preserves the live bytes, allocations, camera ownership and attached printer. Successful loading can replace the SRAM allocation, so core callers must reacquire borrowed RAM pointers.

Two development layouts used version 19. The wave layout has one byte after the serial selector and cannot restore cartridges above 128 KiB. The storage layout has a four-byte SRAM length and the extended bytes, without the wave byte. The reader recognizes them by receiving geometry, exact file length and a valid accessory descriptor at the corresponding offset. Exactly one layout must match. The storage layout and versions 2 through 18 reconstruct the missing wave byte from the validated saved nibble; the wave layout retains its recorded byte. Older storage experiments that reused version 18 are not accepted as the published version 18 format.

Versions 2 through 18 cannot restore cartridges above 128 KiB. A version 19 storage state or version 20 state must contain the full selected population. All four load APIs use these rules, including trusted rewind and live-audio restoration. Existing version 2 through 18 field meanings and frozen compatibility digests remain unchanged.

Qt samples eight independent 32 KiB regions before compressing snapshots of at least 512 KiB. If the samples predict little space saving, it uses a stored compression stream; otherwise it compresses the full snapshot at level one. Both paths decode to the exact input bytes and use the same rewind envelope. Larger encoded states can shorten history within the configured memory budget. Ordinary smaller snapshots keep their existing compression path.

### Adapter startup in version 21

Version 21 preserves every version 20 hardware/RAM field and appends a startup extension before the accessory descriptor on physical SGB and SGB2. Cartridge enhancement support does not select this extension. Other models append zero startup bytes and require completed phase 200, zero host-only elapsed time, and canonical empty startup output. The file header still records the new version and complete digest.

The extension begins with this 65-byte header. Offsets are relative to the extension, not the file:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 2 | Signed introduction phase, -10 through 200 |
| 2 | 8 | Unsigned host-only elapsed base clocks |
| 10 | 28 | Seven unsigned Q0.32 voice phases |
| 38 | 4 | Unsigned Q0.32 sweep phase |
| 42 | 4 | Signed Q16 sweep-filter history, -1048576 through 1048576 |
| 46 | 8 | Nonzero private noise state |
| 54 | 8 | Saturating count of dropped startup frames |
| 62 | 2 | Ordinary queued frames that precede the startup batches |
| 64 | 1 | Startup batch count, zero through two |

All declared batch descriptors follow the header before any sample payload. Each ten-byte descriptor contains a signed two-byte generation phase, four-byte generation rate, two-byte original length, and two-byte cursor. Generation phases range from -10 through 199. Rates must be supported and equal the decoded effective ordinary output rate. Length must equal `floor(rate * 70224 / physical_model_clock)` and cannot exceed 3,214. Each cursor is independently less than its batch length.

The unread mono samples then follow in queue order, two signed little-endian bytes per sample. Only `length - cursor` samples are stored for each batch. A negative-phase batch contains only zero samples. Decoding clears consumed and unused storage while preserving every unread value and descriptor, so re-encoding an untouched state reproduces its bytes. The complete extension ranges from 65 to 12,941 bytes. Query `cupid_gb_state_size` for every capture because partial drains change its size.

The ordinary prefix count cannot exceed the ordinary queue's occupied frames and must be zero when no startup batch remains. A complete hardware phase may coexist with a queued phase-199 batch. More generally, queued generation phases and synthesis history are independent of current hardware phase after a live import. Validation does not force them to agree. The existing `display_cycles_since_frame` remains presentation age; version 21 adds no duplicate age counter or pending-presentation flag. The older adapter-transfer age remains a separate retained field.

Before allocating, the loader bounds startup metadata and locates the accessory at the exact computed end. It never infers payload length from the receiver's phase, rate, queue or guest time. Full validation checks the decoded output relationship and requires host-only time plus saved guest time to fit in 64 bits. Invalid extension data rejects the console/accessory transaction without changing live bytes, allocations, camera ownership or callbacks.

| Load path | Hardware startup state | Startup output history |
| --- | --- | --- |
| Version 21 exact, ordinary or trusted | Restore saved phase and host-only time | Restore saved synthesis, diagnostics, descriptors and unread samples |
| Version 21 live, ordinary or trusted | Restore saved phase and host-only time | Retain receiving ordinary and startup owners together |
| Version 2 through 20 exact | Completed phase 200 and zero host-only time | Empty queues and synthesis history with canonical noise seed one |
| Version 2 through 20 live | Completed phase 200 and zero host-only time | Retain receiving ordinary and startup owners together |

Live loading decodes the saved startup state before finishing the candidate's pending APU clocks. Those clocks use saved credit and suppression state; they generate no new presentation event or intro batch. Only afterward does the loader replace both output owners with receiving history and rebase the ordinary reconstruction fraction. A host then discards all stale output and resets conversion/pacing. Exact loading does not execute pending clocks. Neither path restarts startup merely because a state is loaded.

The writer keeps the version 20 CH3/SRAM ordering, and the reader retains the two strict version 19 layout decisions. Older fixture lengths and digests remain historical contracts. The [startup guide](sgb-startup.md) covers execution, reset and playback behavior. The [startup validation record](validation-sgb-startup.md) distinguishes portable-format regression checks from actual state exchange between Windows and Linux.

### Adapter header protocol in version 22

Version 22 preserves the earlier hardware, SRAM and startup layout, then appends an 85-byte protocol record before the accessory descriptor. This record is present on every model. Its position follows the complete variable-length startup extension on physical SGB/SGB2 and directly follows the earlier hardware/RAM fields on other models.

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 1 | Command receiver disabled, canonical boolean `00` or `01` |
| 1 | 84 | Received boot-header sections in cache order |

Physical SGB/SGB2 retain every cache byte, including incomplete, repeated and out-of-order sections. The codec does not require a valid cartridge header, infer missing sections, or equate the disable latch with cartridge declaration metadata. Both exact and live loading restore this hardware history. Existing prefix fields retain the partial command, bit position, pulse/stop flags, player count and selected controller.

Other models require an all-zero protocol record. Invalid booleans, noncanonical non-adapter data, truncated records and invalid accessory boundaries reject the complete load transaction before changing the live machine. Saving and loading use explicit bytes rather than the host structure's alignment or padding.

Versions 2 through 21 never stored this latch or cache. Their migration clears the cache and disables reception only when physical SGB/SGB2 had a false saved enhancement declaration. This preserves the old format's receiver eligibility. It cannot reconstruct header packets received before a mapped-firmware checkpoint, determine whether an earlier checksum failed, or recover later cached replacements. New version 22 captures retain those distinctions.

The [boot-header contract](sgb-startup.md#boot-headers-and-command-permission) describes reception, handoff, controller visibility and reset behavior. The protocol and state regression targets exercise the new behavior; their platform and paired-runtime qualification is separate from the historical version 21 results below.

### GBS program identity in version 23

Version 23 retains the version 22 hardware payload and adds a program-kind flag to the existing header. An ordinary cartridge still uses the SHA-256 of its complete ROM bytes. A GBS state sets flag bit 1 and uses SHA-256 over the following bytes, in order:

1. The ASCII domain `cupidgb/gbs-program/v1`, including one terminating NUL.
2. One byte for the loaded program's effective MBC3 wiring: `00` for seven bank bits or `01` for eight.
3. The accepted music payload length as an unsigned, eight-byte little-endian value.
4. All 112 original GBS header bytes.
5. The complete prepared ROM image, including erased padding and executable entry code.

The receiver must already own the same program kind, header, payload and bank wiring. The file cannot turn a cartridge into a music program or replace the receiver's metadata. Versions 2 through 22 reject the GBS flag; versions 23 and 24 reject every flag outside bits 0 and 1. Ordinary older cartridge imports retain their existing identity rules.

### Cartridge bus after firmware entry in version 24

Version 24 keeps the version 23 layout. The existing two-byte cartridge-bus record accepts `00 00` for a bus that has not been driven since firmware entry. That byte stays at zero until a driven access starts the hold timer. An expired bus still uses `00 FF`. Versions 6 through 23 recorded only the expired value at a zero hold time, so imports retain `FF` for that encoding. Older files cannot recover the initial byte they never stored.

### MBC6 flash in version 25

Version 25 places an MBC6-only record after the SGB receiver record and before the serial accessory descriptor. The record contains the split ROM and SRAM selectors, flash chip and write-enable gates, sector protection, current flash mode, command phase, status and buffered page. The flash array and hidden bytes follow. The payload is fixed at 1,048,978 bytes. Other controllers keep their version 24 payload.

The same record now retains arbitrary-order program buffers. Current builds
can resume earlier states with a sequential buffer. Reopening a state saved
during an arbitrary-order program operation requires a build that supports
the new buffer modes.

An older MBC6 state that contains the board's 32 KiB SRAM maps its historical 16 KiB ROM selection and 8 KiB SRAM selection into adjacent halves. Flash starts erased with its command latches clear because those states do not contain flash data.

### TAMA5 in version 26

TAMA5 states use a 68-byte record before the serial accessory descriptor. It contains the clock baseline, page registers and timer/alarm flags, followed by the selected register, command parameter, high address nibble, response, cached minute and hour, and readiness flag. The core snapshot already carries the ROM bank, 32 save bytes, and fractional clock phase. TAMA5 requires version 26 or later because earlier builds had no TAMA5 controller.

GBS timer-control bit 7 permits double-speed execution on the selected hardware, including monochrome and adapter profiles. On non-CGB hardware, a saved double-speed bit grants no permission on its own: the receiving GBS header must already request it. Physical CGB retains its normal saved speed behavior, including guest speed switches. CGB operating mode remains restricted to physical CGB. The effective bank wiring is part of identity because an extended MBC3 cartridge can leave that configuration attached to a later music session; equal music bytes alone do not establish equivalent execution.

The Qt track selector belongs to the current music session. Loading a state restores the saved guest execution while retaining the displayed selection. Restart then starts that selected track; Reset reloads the music file and uses its initial track. Exact core restoration restores saved reconstruction history, while desktop loading retains the receiving audio history under the live contract above. See [GBS music](gbs-music.md).

Trusted state contexts cache this program digest. Successful music loads advance the load generation; starting another track retains it. An embedding host that directly edits the supposedly immutable GBS header, prepared image or bank wiring must discard and recreate its context.

### CGB revision memory in version 27

States for CGB revisions 0 through C store 24 bytes of extra OAM. Revision D stores 48. The bytes follow any mapper extension and precede the serial accessory descriptor. Each byte stores one writable cell, including cells shared by multiple addresses.

The model field identifies the exact revision. Existing CGB states use model ID 3, which selects revision E. Cells on revisions 0 through D survive Quick Reset and both exact and desktop state loads.

### PCM reads in version 28

Version 28 adds two bytes after extra OAM on CGB revisions 0 through C. They hold the bits suppressed in the current FF76 and FF77 readback. Exact and desktop loads restore the latch and any pending APU clocks. Version 27 imports start with the latch clear and can still resolve an edge in their saved pending clocks.

### CGB envelopes in version 29

CGB states store the noise envelope clock and endpoint lock in three previously unused bits of the noise period byte. The existing envelope delay can hold four base clocks when a DIV reset restarts a pending edge. Exact and desktop loads preserve both values. For older files, the loader uses the saved noise countdown, frame sequencer phase, and DIV tap to infer whether the clock was high. Endpoint lock is inferred only for an armed edge.

## Validation scope

The established regression and fixture results below retain their earlier checkpoint scope. Version 21 state, startup and audio checks passed in the complete Windows, Linux and sanitizer gates. Native exchange, frontend restoration and runtime results have their own scope in the [startup checkpoint](validation-sgb-startup.md); passing a codec regression alone does not qualify those workflows or the underlying hardware behavior.

Generated-ROM tests exercise deterministic continuation across the current six hardware profiles, active boot overlays, LCD modes, CGB double speed, pending timer reloads, DMA starts/restarts, and serial phases. Another 64 continuation scenarios capture pending background/object fetches and window work, then apply scroll and palette writes after restoration. Checks compare CPU and memory state, framebuffers, emitted PCM, and the encoded continuation. Additional tests reject every truncation boundary, damaged or invalid fields, wrong inputs, and stale cache bindings while preserving the live instance. Word-array tests construct expected bytes at every v2 array boundary, cover values crossing byte and sign-bit boundaries, and check unaligned input/output with guard bytes. They compare complete encoded files rather than relying only on encode/decode symmetry.

The frozen 300,572-byte version 4 fixture has SHA-256 `652af4f40504c86dd59eff09f0997f5b60b654816a0908d7802704f981b43be5`, checked by Windows and Linux regressions. The frozen 300,568-byte version 3 fixture has SHA-256 `e4fdd3d7ebfdd04aec94ce7b85f0f2d7d83de1416242fa89f26ccf5dfaf71eb2`. The earlier portable version 2 fixture has SHA-256 `6106c038f80d9ef4ca4417f31305dc05bb714d9252e6caff3eec0205ea4ec0eb`. Older-format import tests cover active and inactive boot overlays, ordinary and trusted-context loaders, preservation of known recorded state, and unrelated live history. Native continuation checks do not establish state interoperability or complete underlying hardware accuracy.

Continuation tests also capture LCD startup before its comparison refresh and a pending HBlank transition after a coincidence interrupt. Restored state must preserve the same mode change, combined-line behavior, and IF bits. The interrupt-entry bus cursor remains local to CPU execution. Version 13 separately retains the divider remainder that can survive a completed instruction.

Additional captures cover delayed and ready CGB blocks, HALT/STOP wake permission, pulse startup combined with DMA, 128 audio pipeline phases, full-width noise-divider carry, and speed-switch freeze/toggle/wait expiry. The timed-switch cases include both directions, LCD on/off, IME clear/set, EI maturation and early interrupt cancellation. They compare complete resumed state and pending PCM, rather than only a final CPU register.

Forward runtime measurements must be distinguished from state loading and rewind, which change the guest timeline. A guest-clock rate measured during those operations represents net timeline progress rather than forward emulation throughput.

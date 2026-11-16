# Save states and rewind

CupidGB native states preserve CPU, timer, DMA, serial, graphics, cartridge, SGB, and audio state at an instruction boundary. They include pending core PCM samples. They exclude ROM binaries, boot binaries, host paths, Qt resources, serial observer pointers, and queued host audio. Loading a state preserves the current serial observer registration and does not replay earlier host notifications.

## Desktop workflows

Use the state menus or Ctrl+1 through Ctrl+9 and Ctrl+0 to save the numbered slots. Add Shift to load. Slots use `.s0` through `.s9` beside the cartridge. When the primary file is missing, the loader also tries the corresponding `.sn0` through `.sn9` filename. A damaged primary file produces an error; it does not silently select another file.

Load a native state file through File > Load State File or drop a `.s0` through `.s9` or `.sn0` through `.sn9` file onto an open cartridge. State files are rejected as cartridge inputs.

Writes use an atomic file replacement. Loading checks the complete cartridge identity and hardware model. A state captured with an active boot overlay requires the matching local boot binary. Failed validation preserves the running session and its host audio queue. A successful load replaces the emulated state, reapplies current physical input, clears queued host audio, and realigns pacing.

New files use CupidGB native version 12. Versions 2 through 11 remain loadable. Interoperable BESS state imports remain pending.

The version identifies the wire encoding, not a promise to preserve emulation bugs between builds. Version 3 records pending CGB DMA work, CPU speed-switch sequencing, and audio clock, held-output, envelope, sweep, and noise latches. Version 2 has no values for these latches. Its loader initializes them deterministically after validating the old payload; it never takes missing history from the target's running session. Pending DMA and speed-switch delays start cleared, while an old armed HBlank transfer retains its saved block count. On color hardware, pulse clock startup follows the saved channel enable, and held pulse output follows the saved duty position. Monochrome pulse history uses the import rules described under version 11 below. Noise gates and held output derive from the saved channel/DAC and LFSR state, with a canonical prescaler phase. The old LFSR, timer, queued PCM, partial integration, and filter state remain intact. This cannot reproduce timing history the old format did not record. Keep the original state if you need to revisit its earlier build.

Subsequent execution follows the current engine. SGB states continue under the selected model's corrected NTSC clock. A legacy cached SGB filter coefficient is refreshed without discarding pending samples or partial sample integration. The wire format stores the model and guest cycle count, not a separate clock-frequency field. Exact continuation comparisons must use the same qualified build and inputs.

Older builds could save the untouched SGB or SGB2 post-boot profile with LY at `FF` and an uninitialized pixel pipeline. Continuing that state produced an invalid internal scanline. The loader recognizes the initial CPU, divider-register, and display signature at guest cycle zero and restores the modeled VBlank entry on line 144 at dot 200. It changes only LY, STAT, and the display phase; the ROM-dependent DIV subphase, memory, host registrations, and battery bookkeeping remain intact. Other states retain their literal saved values. This recovery uses the existing version 12 layout.

Version 1 execution states cannot continue in the changed pixel and audio core. File > Recover Battery from Older State validates the older file and exports supported battery data to a separate file. Recovery preserves the original file and running session. It does not restore execution. See [battery saves](battery-saves.md).

## Rewind

Hold Tab to move backward through captured frames. Rewind restores and removes history entries, then executes the selected state forward for one replay interval. With sufficient history, normal rewind first returns to the current frame position and subsequently moves back one frame per interval. Holding Space consumes one additional history entry, moving back one frame initially and two thereafter. The selected Turbo cap and slow-motion rate also apply to replay pacing. Insufficient history ends replay and waits for release; it does not invent another complete frame.

Replay uses current physical input and produces a new image and audio. Entering or leaving rewind clears stale host playback. During replay, the frontend discards PCM already pending in the restored snapshot and queues only newly generated samples; ordinary disk-state loading retains the codec's pending-PCM continuation. Releasing rewind resumes forward execution with current input. Replay samples do not enter the forward-only WAVE recording.

Settings control the requested history duration and memory budget. The default requests up to 120 seconds within a 256 MiB budget. The buffer evicts its oldest snapshots when either limit is reached. History age uses the model's base T-cycle frequency, so the duration limit represents guest seconds on SGB as well as the other profiles. Enabled rewind captures each completed frame before applying the history limits. Actual retained duration depends on how well the cartridge's state compresses; the interface reports shorter history when the memory limit is reached. Set the duration to zero to disable capture.

History resets on cartridge replacement, reset, successful disk-state loading, or relevant configuration changes. Failed disk-state loading preserves history. Rewind snapshots stay in memory.

The debugger has a separate, smaller instruction snapshot history. It captures only explicit debugger stepping and is invalidated when ordinary playback or session-changing operations resume. Breakpoints, symbols, tool windows, and pending host commands are excluded from native state files. See [debugger history](debugging.md#instruction-history-and-playback).

The rewind path caches the identity of the frontend's immutable cartridge. The core context is bound to the instance, ROM pointer, ROM size, and hardware model. A caller that patches ROM bytes in place must discard and recreate this context. Disk-state APIs always recompute the full cartridge hash. Future cheat, patch, or debugger features must honor that cache invalidation rule.

## Native format

All numeric payload fields use explicit widths and little-endian encoding. Booleans use validated zero or one bytes. Signed timestamps and PCM samples use explicit two's-complement encoding. The format never stores C structure padding, native pointers, native `size_t`, or native boolean representations.

The 128-byte header contains:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 8 | `CUPIDGB` followed by a zero byte |
| 8 | 4 | Format version |
| 12 | 4 | Complete encoded length |
| 16 | 4 | Stable hardware model tag |
| 20 | 4 | Cartridge byte length |
| 24 | 4 | Required boot length, zero when the saved overlay is inactive |
| 28 | 4 | Flags; bit 0 means an active boot overlay |
| 32 | 32 | SHA-256 of the complete cartridge |
| 64 | 32 | SHA-256 of the required boot binary, otherwise zero |
| 96 | 32 | SHA-256 of header bytes 0 through 95 followed by the payload |

Word arrays check their complete byte range once, then read or write individual little-endian elements. This accepts unaligned buffers and preserves all array values without relying on host byte order. Encoding and loading still require the exact final schema offset, and loading retains every integrity and hardware-domain check.

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

Versions 2 through 10 did not maintain the monochrome pulse latches. After validating an older file, the loader derives held duty output from its duty position and starts a clock only when the APU and DAC are enabled and the saved channel or timer indicates activity. It clears unknown trigger suppression and uses a canonical edge age. Disconnected DAC codes default to zero on every model. The saved oscillators, queued PCM, partial integration and filter state remain intact. This preserves a deterministic starting point, but cannot reconstruct a previous duty write, an expired clock, or a disconnected DAC's sample.

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

## Validation scope

Generated-ROM tests exercise deterministic continuation across the current six hardware profiles, active boot overlays, LCD modes, CGB double speed, pending timer reloads, DMA starts/restarts, and serial phases. Another 64 continuation scenarios capture pending background/object fetches and window work, then apply scroll and palette writes after restoration. Checks compare CPU and memory state, framebuffers, emitted PCM, and the encoded continuation. Additional tests reject every truncation boundary, damaged or invalid fields, wrong inputs, and stale cache bindings while preserving the live instance. Word-array tests construct expected bytes at every v2 array boundary, cover values crossing byte and sign-bit boundaries, and check unaligned input/output with guard bytes. They compare complete encoded files rather than relying only on encode/decode symmetry.

The frozen 300,572-byte version 4 fixture has SHA-256 `652af4f40504c86dd59eff09f0997f5b60b654816a0908d7802704f981b43be5`, checked by Windows and Linux regressions. The frozen 300,568-byte version 3 fixture has SHA-256 `e4fdd3d7ebfdd04aec94ce7b85f0f2d7d83de1416242fa89f26ccf5dfaf71eb2`. The earlier portable version 2 fixture has SHA-256 `6106c038f80d9ef4ca4417f31305dc05bb714d9252e6caff3eec0205ea4ec0eb`. Older-format import tests cover active and inactive boot overlays, ordinary and trusted-context loaders, preservation of known recorded state, and unrelated live history. Native continuation checks do not establish state interoperability or complete underlying hardware accuracy.

Continuation tests also capture LCD startup before its comparison refresh and a pending HBlank transition after a coincidence interrupt. Restored state must preserve the same mode change, combined-line behavior, and IF bits. The interrupt-entry scheduler consumes its local subcycle cursor before returning to the host; it adds no persistent half-dot field or wire-format revision.

Additional captures cover delayed and ready CGB blocks, HALT/STOP wake permission, pulse startup combined with DMA, 128 audio pipeline phases, full-width noise-divider carry, and speed-switch freeze/toggle/wait expiry. The timed-switch cases include both directions, LCD on/off, IME clear/set, EI maturation and early interrupt cancellation. They compare complete resumed state and pending PCM, rather than only a final CPU register.

Forward runtime measurements must be distinguished from state loading and rewind, which change the guest timeline. A guest-clock rate measured during those operations represents net timeline progress rather than forward emulation throughput.

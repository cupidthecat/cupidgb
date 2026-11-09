# Save states and rewind

CupidGB native states preserve CPU, timer, DMA, serial, graphics, cartridge, SGB, and audio state at an instruction boundary. They include pending core PCM samples. They exclude ROM binaries, boot binaries, host paths, Qt resources, serial observer pointers, and queued host audio. Loading a state preserves the current serial observer registration and does not replay earlier host notifications.

## Desktop workflows

Use the state menus or Ctrl+1 through Ctrl+9 and Ctrl+0 to save the numbered slots. Add Shift to load. Slots use `.s0` through `.s9` beside the cartridge. When the primary file is missing, the loader also tries the corresponding `.sn0` through `.sn9` filename. A damaged primary file produces an error; it does not silently select another file.

Load a native state file through File > Load State File or drop a `.s0` through `.s9` or `.sn0` through `.sn9` file onto an open cartridge. State files are rejected as cartridge inputs.

Writes use an atomic file replacement. Loading checks the complete cartridge identity and hardware model. A state captured with an active boot overlay requires the matching local boot binary. Failed validation preserves the running session and its host audio queue. A successful load replaces the emulated state, reapplies current physical input, clears queued host audio, and realigns pacing.

New files use CupidGB native version 3. Version 2 files remain loadable. Interoperable BESS state imports remain pending. A filename fallback does not imply support for another state format.

The version identifies the wire encoding, not a promise to preserve emulation bugs between builds. Version 3 records pending CGB DMA work, CPU speed-switch sequencing, and audio clock, held-output, envelope, sweep, and noise latches. Version 2 has no values for these latches. Its loader initializes them deterministically after validating the old payload; it never takes missing history from the target's running session. Pending DMA and speed-switch delays start cleared, while an old armed HBlank transfer retains its saved block count. Pulse clock startup follows the saved channel enable, and held pulse output follows the saved duty position. Noise gates and held output derive from the saved channel/DAC and LFSR state, with a canonical prescaler phase. The old LFSR, timer, queued PCM, partial integration, and filter state remain intact. This cannot reproduce timing history the old format did not record. Keep the original state if you need to revisit its earlier build.

Subsequent execution follows the current engine. SGB states continue under the selected model's corrected NTSC clock. A legacy cached SGB filter coefficient is refreshed without discarding pending samples or partial sample integration. The wire format stores the model and guest cycle count, not a separate clock-frequency field. Exact continuation comparisons must use the same qualified build and inputs.

Version 1 execution states cannot continue in the changed pixel and audio core. File > Recover Battery from Older State validates the older file and exports supported battery data to a separate file. Recovery preserves the original file and running session. It does not restore execution. See [battery saves](battery-saves.md).

## Rewind

Hold Tab to move backward through captured frames. Space with Tab moves backward two snapshots per host interval. Releasing rewind resumes forward emulation with current physical input. Rewind playback intentionally clears host audio; ordinary forward playback retains normal audio generation and queue backpressure.

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
| 8 | 4 | Format version, currently 3 |
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

Loading first verifies version, length, integrity, cartridge/model/boot identity, and field domains. It decodes into a separate candidate that retains the current session's ROM ownership and immutable metadata. Only a completely validated candidate replaces the live state.

## Validation scope

Generated-ROM tests exercise deterministic continuation across the current six hardware profiles, active boot overlays, LCD modes, CGB double speed, pending timer reloads, DMA starts/restarts, and serial phases. Another 64 continuation scenarios capture pending background/object fetches and window work, then apply scroll and palette writes after restoration. Checks compare CPU and memory state, framebuffers, emitted PCM, and the encoded continuation. Additional tests reject every truncation boundary, damaged or invalid fields, wrong inputs, and stale cache bindings while preserving the live instance. Word-array tests construct expected bytes at every v2 array boundary, cover values crossing byte and sign-bit boundaries, and check unaligned input/output with guard bytes. They compare complete encoded files rather than relying only on encode/decode symmetry.

The current 300,568-byte version 3 fixture has SHA-256 `e4fdd3d7ebfdd04aec94ce7b85f0f2d7d83de1416242fa89f26ccf5dfaf71eb2`, checked by Windows and Linux regressions. The earlier portable version 2 fixture has SHA-256 `6106c038f80d9ef4ca4417f31305dc05bb714d9252e6caff3eec0205ea4ec0eb`. Version 2 import tests cover active and inactive boot overlays, ordinary and trusted-context loaders, preservation of recorded audio state, and unrelated live timing history. Native continuation checks do not establish state interoperability or complete underlying hardware accuracy.

Continuation tests also capture LCD startup before its comparison refresh and a pending HBlank transition after a coincidence interrupt. Restored state must preserve the same mode change, combined-line behavior, and IF bits. The interrupt-entry scheduler consumes its local subcycle cursor before returning to the host; it adds no persistent half-dot field or wire-format revision.

Additional captures cover delayed and ready CGB blocks, HALT/STOP wake permission, pulse startup combined with DMA, 128 audio pipeline phases, full-width noise-divider carry, and speed-switch freeze/toggle/wait expiry. The timed-switch cases include both directions, LCD on/off, IME clear/set, EI maturation and early interrupt cancellation. They compare complete resumed state and pending PCM, rather than only a final CPU register.

Forward runtime measurements must be distinguished from state loading and rewind, which change the guest timeline. A guest-clock rate measured during those operations represents net timeline progress rather than forward emulation throughput.

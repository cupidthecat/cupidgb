# Debugger and hardware inspectors

CupidGB provides a CPU debugger and memory/graphics inspectors for an open cartridge. Use Tools > Debugger, or Ctrl+C with the game window active, to stop at an instruction boundary. Tools > Memory and Graphics opens the inspectors and pauses execution. The debugger shortcut can be changed in Settings.

These tools inspect the current emulated session. They do not establish hardware accuracy by themselves. The [parity checklist](parity.md) separates the implemented controls from remaining debugger requirements.

## Execution controls

The debugger shows CPU registers, flags, interrupt-enable state, HALT/STOP state, base-speed T-cycles, disassembly, symbols, breakpoints, and memory watchpoints. Its buttons and console operate on the same session.

| Control | Behavior |
| --- | --- |
| Break | Pause at the next host instruction boundary |
| Continue | Resume normal playback and honor execution breakpoints and memory watchpoints |
| Step | Execute one guest hardware step, entering calls or interrupt dispatch; a sleeping CPU may execute an idle cycle |
| Next | Step over calls and interrupt handlers, stopping at the next appropriate instruction boundary |
| Finish | Run until the current function or interrupt handler returns |
| Backstep | Restore the preceding retained instruction boundary |

Next and Finish run in small event-loop slices so Break and the rest of the interface remain available. A command stops after one million guest hardware steps if its target has not been reached. HALT, STOP, an endless loop, a missing return, or modified stack contents can prevent a target from being reached. The status message distinguishes a completed command, breakpoint, exhausted budget, illegal opcode, and execution error.

During SGB or SGB2 startup, the adapter can advance its display while holding the CPU. Step, Next and Finish advance this hold in bounded slices without consuming the guest-step budget or creating instruction history. Execution breakpoints and watchpoints apply when guest execution becomes eligible. A breakpoint at address zero therefore waits for the hold to finish, and its one-use continue skip remains available for that instruction.

An illegal primary opcode completes its fetch, clears IE, and halts the CPU. An explicit execution command stops with `Illegal opcode: CPU halted`, keeping its pre-step snapshot available for Backstep. Continue resumes peripheral clocks while the CPU remains halted; setting IF alone cannot wake it. A deliberate IE edit can enable wakeup. Ordinary gameplay also keeps clocks running after this hardware halt, without reporting it as a host execution failure. A frozen game still requires diagnosis and is not a successful compatibility result.

With IME clear, a HALT wake can consume one hardware step without fetching an opcode. Step exposes that event with an unchanged PC. Next continues through the wake and then tracks the instruction actually fetched, including calls or a newly blocked memory region. Expiry of the timed CGB speed-switch wait can fetch the next opcode in the expiring step.

Breakpoints stop before the matching instruction or idle step. Continuing from an actual breakpoint skips that occurrence once, then honors the breakpoint on a later visit. A breakpoint can match every mapped bank at an address or one specific bank. Up to 128 entries are retained in the session. Closing the debugger does not delete them.

The disassembly describes underlying mapped memory. CPU execution still obeys bus restrictions: an instruction fetched while a region is blocked can differ from the raw bytes shown by an inspector. Execution tracking uses the opcode the CPU actually fetched, including a bus-lock transition during HALT wake.

## Console commands

Type `help` or `?` for the supported command list. Console numbers are decimal unless prefixed with `$` or `0x` for hexadecimal. Loaded symbols resolve as names. Memory counts and breakpoint numbers are decimal. Up/Down recall up to 100 previous commands. Symbol-file addresses and the inspector's explicitly labeled hexadecimal fields follow their own formats.

| Example | Result |
| --- | --- |
| `break $0100` | Break at address 0100 in any mapped bank |
| `break $01:$4000` | Break at address 4000 when bank 01 is mapped |
| `break GameLoop` | Resolve a loaded symbol and use its bank/address |
| `break GameLoop if a == $42` | Stop at the symbol only when A equals 42 hex |
| `print (hl + 2)` | Evaluate a pure 16-bit expression |
| `print [$c000]` / `print {$c000}` | Read a byte or little-endian word without advancing hardware |
| `break` or `b` | Pause execution |
| `breakpoints` | List the current breakpoint count and refresh the breakpoint view |
| `delete 1` / `delete all` | Remove one listed breakpoint or all breakpoints |
| `watch $c000` | Stop after a guest write to C000; writes are the default access type |
| `watch/r $c000` / `watch/rw $c000` | Watch reads, or both reads and writes |
| `watch/w $02:$d000` | Watch writes to D000 when WRAM bank 2 is mapped |
| `watch/w $c000 to $c010` | Watch C000 through C00F; add `inclusive` to include C010 |
| `watch/w $c000 if (old == $11) && (new == $12)` | Stop when the old bus value is 11 and the attempted write is 12 |
| `watchpoints` / `list` | Refresh the watch list, or the combined breakpoint/watchpoint counts |
| `unwatch 3` / `unwatch all` | Remove a stable watchpoint ID, or clear all watches |
| `continue` / `c` | Resume normal playback |
| `step` / `s`, `next` / `n`, `finish` / `f` | Run the corresponding execution command |
| `backstep` / `bs` | Restore one retained instruction boundary |
| `registers` / `r` | Print the register readout |
| `disassemble $0100` / `dis $0100` | Print 16 decoded instructions from the address |
| `x $c000 32` / `memory $c000 32` | Read 32 bytes from mapped memory |
| `write $c000 $42` | Write byte 42 to the current writable mapping |
| `set PC $0100` | Change a CPU register while paused |
| `symbols "path with spaces/game.sym"` | Load a symbol file |
| `reset` | Hard-reset the loaded console, preserving pause choice and rewind history |
| `reset quick` | Quick-reset the loaded console with retained physical memory and rewind history |
| `reset reload` | Refresh cartridge, firmware and companion symbols; preserve the active model and compatible rewind |

Register edits accept AF, BC, DE, HL, SP, PC, A, F, B, C, D, E, H, L, and IME. The unused low bits of F remain zero; IME accepts only 0 or 1. A bank-qualified memory/disassembly request must refer to the bank currently mapped at that address. The commands do not switch hardware banks or patch ROM bytes.

The native `reset` and `reset quick` commands operate on the loaded assets. They do not reread cartridge, firmware or symbol files. Both keep host registrations, breakpoints, symbols and active recording while clearing instruction history. `reset reload` uses the same asset workflow as File > Reload Cartridge.

## Expressions and conditional stops

Expressions accept CPU registers, loaded symbols, decimal literals, `$`/`0x` hexadecimal literals, parentheses, and byte `[address]` or little-endian word `{address}` reads. Arithmetic wraps to 16 bits. Division or remainder by zero and shifts by 16 or more yield zero. Both logical operands are evaluated. A bank qualifier selects a side-effect-free ROM, VRAM, or WRAM view without switching guest banks; bank zero selects the mapped view. ROM banks alias through the cartridge's physical address lines, and unpopulated bytes at the end of a truncated image read as `FF`. Conditions are true when their value is nonzero. A condition that loses a symbol or otherwise fails evaluation pauses execution with a debugger error. Invalid new definitions leave existing breakpoints intact.

MMM01 uses the same logical ROM-bank numbering for menu-first and menu-last dumps. The file remains in its original order. Before the controller locks, its menu banks display as `FFFE` and `FFFF`; expression values and the `break` command use nine-bit bank qualifiers. Use an address without a bank qualifier to stop in either startup menu bank. Symbol lookup and the raw core breakpoint API retain the full menu-bank identities.

Precedence from lowest to highest is: comparisons; `+ - | ||`; `* / % & ^ &&`; shifts; bank qualification with `:`. Operators in a group associate left to right. These groups differ from C, so parenthesize individual comparisons when combining them. Expressions are limited to 255 bytes and 32 nested levels. Assignment, unary operators, and execution range breakpoints remain unsupported.

## Memory watchpoints

Use the Watchpoint address field and its Write, Read, or Read/write selector, or enter a `watch` command. A watch can cover one address or an increasing range within a mapped bank. Up to 128 definitions are retained. Removing a watch does not renumber surviving IDs. A new definition follows the last live ID, so deleting the tail permits ID reuse and clearing the collection starts numbering at 1. `unwatch` without an argument also clears the collection.

Put a bank qualifier on the starting address to select a bank. An ending qualifier alone leaves the range unrestricted; if both endpoints specify a bank, they must agree. A range end must exceed its start even with `inclusive`. Use a single address for a one-byte watch.

A matching access is recorded at its original bus address, before DMA redirection or memory-access restrictions. A blocked write can therefore trigger a watch even though RAM does not change. Instruction fetches and DMA source reads use the same observed guest read path. The first matching access is reported after the entire hardware step finishes. Step, Next, Finish, and normal playback all honor watches. Next and Finish stop early on a match; explicit debugger execution keeps the triggering instruction available for Backstep.

Conditions use the expression syntax above and also accept `old` and `new`. For a write, `new` is the attempted byte. Each evaluated occurrence of `old` reads the guest bus at the original address before the write, including its bus restrictions and device read behavior. An unconditional watch adds no such read. For a read watch, both tokens are zero because matching precedes the read result. Ordinary bracketed expression reads remain passive inspector reads. Watch evaluation suppresses recursive watch hits.

Memory inspection, register queries, host memory edits, and rewind replay do not trigger watches. A condition that can no longer resolve a symbol stops with an error after the triggering step and retains that step for Backstep. Watch definitions are host metadata: state loading and compatible resets retain them, while replacing the cartridge starts a fresh debugger session. Native save files contain neither definitions nor host callback pointers.

## Symbols

Load Symbols accepts RGBDS-style `.sym` text with hexadecimal `bank:address name` lines and optional semicolon comments:

```text
; Entry points
00:0100 Entry
01:4000 GameLoop
01:4010 GameLoop.wait
```

Loading replaces the symbol table only after complete validation. A malformed file leaves existing symbols intact and reports a line number. Duplicate names resolve to their last definition. Files are limited to 4 MiB and 16,384 symbols, with at most 127 bytes per name. The symbol loader does not read object files.

Symbols, breakpoints, and watchpoints are host debugger data. They are not embedded in native save states. Native console resets keep them while clearing instruction history. A replacement clears the previous session's debugger data and loads a matching basename `.sym` file if available. The first debugger open also checks for that companion file. A failed cartridge open preserves the current session.

Menu Reset, Quick Reset, Reload and `reset reload` refresh cartridge, firmware and companion symbols. An absent companion file clears the old symbols. Malformed symbols or unreadable assets preserve the session and its history. Existing breakpoints remain for compatible sessions. Ordinary Reset clears frame rewind; Quick Reset and Reload retain it when cartridge bytes, model and firmware match. All clear instruction history. Use `symbols` to replace only the symbol table without reloading the cartridge.

## Instruction history and playback

With a cable connected, debugger execution advances the peer on the shared physical timeline. Opening the debugger pauses both games, and a breakpoint or watchpoint in either console stops the pair. Step, Next and Finish operate on the selected console while the peer catches up as needed. Backstep is unavailable while connected because the instruction history cannot restore both consoles together. State files still restore only their selected console. See [Link play](link-play.md).

Explicit Step, Next, and Finish execution captures instruction snapshots. Backstep restores one instruction boundary, including CPU, timer, graphics, cartridge, and sound-generator state. History retains at most 128 snapshots within a 32 MiB budget for compressed buffer capacity and stored entries; the oldest entries are evicted when either limit is reached. Container overhead and temporary capture buffers use additional memory. Instruction history is separate from ordinary frame rewind.

Continuing normal playback or advancing a frame clears instruction history. Cartridge replacement, reset, successful state loading, frame rewind, and debugger edits also invalidate it. An execution failure can still have changed hardware state; its pre-step snapshot remains available for Backstep. No instruction snapshots are captured while the debugger is unused.

Debugger stepping advances real guest hardware and can generate samples. Forward handheld audio remains eligible for an active WAVE recording, while stepped execution is silent at the host output. Adapter startup output, including its silent lead-in, is excluded from the recording. Backstep clears queued playback from both output owners. It does not remove audio already written to a recording or undo a battery file already flushed to disk. In-memory battery changes can schedule a later save.

Opening tools releases physical key bindings while preserving the stopped guest input for inspection. Normal playback reconciles input to the currently released host state. Ordinary focus-loss handling outside paused tool inspection continues to release input.

Closing the debugger, including with Escape, while Next or Finish is active cancels that command and leaves execution paused. Closing it during normal continued playback does not disable breakpoints or stop playback. Closing a paused debugger clears its instruction history. Use Continue or the ordinary pause action when ready to resume.

## Memory and graphics

The inspector has Memory, Tiles, Maps, OAM, and Palettes tabs. Refresh updates the active tab. Live refresh checks it four times per second while the window is visible. Read-only inspection does not execute guest cycles or change hardware state. An active memory edit survives refresh; replacing the session cancels the old editor so its pending text cannot modify another cartridge.

Memory displays a 256-byte page with hexadecimal navigation, byte editing, and mapped-bank context. It reads underlying VRAM, OAM, wave RAM, and ordinary cartridge RAM even when the CPU cannot currently access those bytes. A bank context describes the selected mapping; it is not a physical-bank selection control.

Writes require a loaded session and pause execution. ROM and unusable address ranges are rejected. Writable RAM, VRAM, OAM, and wave RAM can be edited directly; I/O writes use device semantics, so an edit can reset DIV, change banking/register state, or start DMA. Battery-backed edits participate in dirty tracking. Register and memory edits clear instruction/frame history and pending host playback as appropriate.

Tiles shows the 384 tiles in either physical VRAM bank or both banks. Maps selects the background/window map, either map address explicitly, and signed/unsigned tile addressing. CGB attributes control bank selection, flips, palettes, and displayed metadata. The map cell readout identifies its address and attributes.

Tile grid adds eight-pixel cell boundaries in Tiles and Maps. The tile view also marks its larger tile groups. Scrolling viewport in Maps outlines the current 160-by-144 background viewport and shades the area outside it. The viewport follows SCX and SCY and wraps across either edge of the 256-by-256 map. Both overlays start disabled, retain their choices while the inspector is open, and leave decoded pixels and emulated memory unchanged.

OAM lists all 40 entries, including entries outside visible lines. The table shows screen-relative X/Y positions, tile/flags, bank/palette, and 8-by-8 or 8-by-16 previews; tooltips include raw coordinates. A line-limit indication means the entry loses selection on at least one visible line after ten earlier objects; it does not mean that the whole object is necessarily invisible. Palettes exposes register values and raw color entries.

Inspector raw grayscale mode bypasses color correction. Palette-selected color previews and swatches use the active CGB correction and ambient temperature. Numeric entries retain their RGB555 values. Object previews retain a visible distinction for otherwise uniform monochrome object palettes.

## Remaining debugger scope

Use Audio > Audio Channels or Alt+1 through Alt+4 to listen to individual channel combinations. Muting changes output mixing while the emulated channel continues running. These choices persist through state loading, rewind and Backstep for the current session.

A full expression language, saved debugger sessions, continuous-playback instruction replay, arbitrary physical-bank memory editing, ROM patching, interactive palette editing, and graphics layer toggles remain pending. Inspector rendering and the bounded instruction history are useful diagnostic tools, not complete debugger or hardware parity.

# Debugger and hardware inspectors

CupidGB provides a CPU debugger and memory/graphics inspectors for an open cartridge. Use Tools > Debugger, or Ctrl+C with the game window active, to stop at an instruction boundary. Tools > Memory and Graphics opens the inspectors and pauses execution. The debugger shortcut can be changed in Settings.

These tools inspect the current emulated session. They do not establish hardware accuracy by themselves. The [parity checklist](parity.md) separates the implemented controls from remaining debugger requirements.

## Execution controls

The debugger shows CPU registers, flags, interrupt-enable state, HALT/STOP state, base-speed T-cycles, disassembly, symbols, and breakpoints. Its buttons and console operate on the same session.

| Control | Behavior |
| --- | --- |
| Break | Pause at the next host instruction boundary |
| Continue | Resume normal playback and honor execution breakpoints |
| Step | Execute one hardware step, entering calls or interrupt dispatch; a sleeping CPU may execute an idle cycle |
| Next | Step over calls and interrupt handlers, stopping at the next appropriate instruction boundary |
| Finish | Run until the current function or interrupt handler returns |
| Backstep | Restore the preceding retained instruction boundary |

Next and Finish run in small event-loop slices so Break and the rest of the interface remain available. A command stops after one million hardware-step attempts if its target has not been reached. HALT, STOP, an endless loop, a missing return, or modified stack contents can prevent a target from being reached. The status message distinguishes a completed command, breakpoint, exhausted budget, and execution error.

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
| `continue` / `c` | Resume normal playback |
| `step` / `s`, `next` / `n`, `finish` / `f` | Run the corresponding execution command |
| `backstep` / `bs` | Restore one retained instruction boundary |
| `registers` / `r` | Print the register readout |
| `disassemble $0100` / `dis $0100` | Print 16 decoded instructions from the address |
| `x $c000 32` / `memory $c000 32` | Read 32 bytes from mapped memory |
| `write $c000 $42` | Write byte 42 to the current writable mapping |
| `set PC $0100` | Change a CPU register while paused |
| `symbols "path with spaces/game.sym"` | Load a symbol file |

Register edits accept AF, BC, DE, HL, SP, PC, A, F, B, C, D, E, H, L, and IME. The unused low bits of F remain zero; IME accepts only 0 or 1. A bank-qualified memory/disassembly request must refer to the bank currently mapped at that address. The commands do not switch hardware banks or patch ROM bytes.

## Expressions and conditional stops

Expressions accept CPU registers, loaded symbols, decimal literals, `$`/`0x` hexadecimal literals, parentheses, and byte `[address]` or little-endian word `{address}` reads. Arithmetic wraps to 16 bits. Division or remainder by zero and shifts by 16 or more yield zero. Both logical operands are evaluated. A bank qualifier selects a side-effect-free ROM, VRAM, or WRAM view without switching guest banks; bank zero selects the mapped view. Conditions are true when their value is nonzero. A condition that loses a symbol or otherwise fails evaluation pauses execution with a debugger error. Invalid new definitions leave existing breakpoints intact.

Precedence from lowest to highest is: comparisons; `+ - | ||`; `* / % & ^ &&`; shifts; bank qualification with `:`. Operators in a group associate left to right. These groups differ from C. Expressions are limited to 255 bytes and 32 nested levels. Assignment, unary operators, watchpoints, and range breakpoints remain unsupported.

## Symbols

Load Symbols accepts RGBDS-style `.sym` text with hexadecimal `bank:address name` lines and optional semicolon comments:

```text
; Entry points
00:0100 Entry
01:4000 GameLoop
01:4010 GameLoop.wait
```

Loading replaces the symbol table only after complete validation. A malformed file leaves existing symbols intact and reports a line number. Duplicate names resolve to their last definition. Files are limited to 4 MiB and 16,384 symbols, with at most 127 bytes per name. The symbol loader does not read object files.

Symbols and breakpoints are host debugger data. They are not embedded in native save states. Resetting the same cartridge path, data, and model preserves them while clearing execution history. A replacement clears the previous session's debugger data and loads a matching basename `.sym` file if available. The first debugger open also checks for that companion file. A failed cartridge open preserves the current session. Use the explicit symbol loader when a file needs to be reloaded or changed.

## Instruction history and playback

Explicit Step, Next, and Finish execution captures instruction snapshots. Backstep restores one instruction boundary, including CPU, timer, graphics, cartridge, and sound-generator state. History retains at most 128 snapshots within a 32 MiB budget for compressed buffer capacity and stored entries; the oldest entries are evicted when either limit is reached. Container overhead and temporary capture buffers use additional memory. Instruction history is separate from ordinary frame rewind.

Continuing normal playback or advancing a frame clears instruction history. Cartridge replacement, reset, successful state loading, frame rewind, and debugger edits also invalidate it. An execution failure can still have changed hardware state; its pre-step snapshot remains available for Backstep. No instruction snapshots are captured while the debugger is unused.

Debugger stepping advances real guest hardware and can generate samples. Forward generated audio remains eligible for an active WAVE recording, while stepped execution is silent at the host output. Backstep clears queued host playback. It does not remove audio already written to a recording or undo a battery file already flushed to disk. In-memory battery changes can schedule a later save.

Opening tools releases physical key bindings while preserving the stopped guest input for inspection. Normal playback reconciles input to the currently released host state. Ordinary focus-loss handling outside paused tool inspection continues to release input.

Closing the debugger, including with Escape, while Next or Finish is active cancels that command and leaves execution paused. Closing it during normal continued playback does not disable breakpoints or stop playback. Closing a paused debugger clears its instruction history. Use Continue or the ordinary pause action when ready to resume.

## Memory and graphics

The inspector has Memory, Tiles, Maps, OAM, and Palettes tabs. Refresh updates the active tab. Live refresh checks it four times per second while the window is visible. Read-only inspection does not execute guest cycles or change hardware state. An active memory edit survives refresh; replacing the session cancels the old editor so its pending text cannot modify another cartridge.

Memory displays a 256-byte page with hexadecimal navigation, byte editing, and mapped-bank context. It reads underlying VRAM, OAM, wave RAM, and ordinary cartridge RAM even when the CPU cannot currently access those bytes. A bank context describes the selected mapping; it is not a physical-bank selection control.

Writes require a loaded session and pause execution. ROM and unusable address ranges are rejected. Writable RAM, VRAM, OAM, and wave RAM can be edited directly; I/O writes use device semantics, so an edit can reset DIV, change banking/register state, or start DMA. Battery-backed edits participate in dirty tracking. Register and memory edits clear instruction/frame history and pending host playback as appropriate.

Tiles shows the 384 tiles in either physical VRAM bank or both banks. Maps selects the background/window map, either map address explicitly, and signed/unsigned tile addressing. CGB attributes control bank selection, flips, palettes, and displayed metadata. The map cell readout identifies its address and attributes.

OAM lists all 40 entries, including entries outside visible lines. The table shows screen-relative X/Y positions, tile/flags, bank/palette, and 8-by-8 or 8-by-16 previews; tooltips include raw coordinates. A line-limit indication means the entry loses selection on at least one visible line after ten earlier objects; it does not mean that the whole object is necessarily invisible. Palettes exposes register values and raw color entries.

Inspector raw grayscale mode bypasses color correction. Palette-selected color previews and swatches use the active CGB correction and ambient temperature. Numeric entries retain their RGB555 values. Object previews retain a visible distinction for otherwise uniform monochrome object palettes.

## Remaining debugger scope

Conditional breakpoints, read/write watchpoints, a full expression language, saved debugger sessions, continuous-playback instruction replay, arbitrary physical-bank memory editing, ROM patching, a map viewport overlay, interactive palette editing, and presentation layer/channel toggles remain pending. Inspector rendering and the bounded instruction history are useful diagnostic tools, not complete debugger or hardware parity.

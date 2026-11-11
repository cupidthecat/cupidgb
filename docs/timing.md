# Timing and clock domains

CupidGB measures guest hardware time separately from host playback time. A timing change must preserve the number of clocks, the order of bus accesses, and the state visible at each interrupt or register edge. Matching an instruction's final registers and total cycle count does not establish that its accesses happened at the right phases.

This guide describes the current timing contracts and the invariants needed when changing them. Recorded test results belong in the [validation record](validation.md), while remaining hardware requirements belong in the [parity checklist](parity.md). Behavioral comparisons use pinned revision `ea667ad79aa28409f034b1fd6da7304e1844750d`; source identities, local paths, and paired traces remain in private receipts.

## Units and physical clocks

| Quantity | Unit and meaning |
| --- | --- |
| CPU T-cycle | One CPU clock; four CPU T-cycles form one CPU machine cycle |
| CPU M-cycle | The unit accepted by `cupid_gb_tick`; advances four base T-cycles at normal speed or two at CGB double speed |
| Base T-cycle / LCD dot | One tick in the physical base-clock domain; the PPU and APU advance in this domain |
| `elapsed_cycles` | Cumulative base T-cycles, including execution with the LCD disabled |
| PCM frame | One left/right output sample pair at the configured sample rate |
| Host deadline | Monotonic host nanoseconds used by Qt to schedule more guest work |

Call `cupid_gb_base_clock_hz(gb)` to convert base T-cycles to physical seconds. The SGB NTSC profile uses 4,295,454 Hz. DMG, DMG0, MGB, CGB, and SGB2 use 4,194,304 Hz. A null instance returns the latter default. SGB PAL is not implemented.

CGB double speed doubles CPU-domain progress relative to the base clock. It does not double the LCD, channel oscillator, or output sample clocks. One double-speed M-cycle consumes two base T-cycles every time it executes. Alternating zero-dot and four-dot batches would preserve an average rate while moving intervening reads and interrupts to the wrong dots.

A regular scanline occupies 456 base T-cycles, and a regular 154-line LCD interval occupies 70,224. Startup, LCD enable/disable, and an interval beginning partway through a frame need not produce an image after another full 70,224 cycles. Geometry, image completion, and elapsed guest time are different observations.

The headless runner's `--frames` limit and both input-trace formats use 70,224 base T-cycles per numbered interval. They do not count displayed images. The same cycle interval has a different physical duration on SGB NTSC.

## Execution and subsystem ordering

The CPU decoder in [cpu.c](../src/gb/cpu.c) schedules bus accesses around calls into [timer.c](../src/gb/timer.c). An ordinary M-cycle advances the CPU divider/timer domain, services OAM DMA, advances the elapsed base-cycle count, and advances the PPU and APU by four or two dots. Helpers that expose a subcycle register edge split that work deliberately while retaining the complete cycle total.

That ordering is part of the implementation contract. A register write between two PPU dots must not advance the divider twice or omit audio integration. Conversely, moving every device update to the end of an instruction can make a CPU read observe a future LCD mode or miss an interrupt that arrived before its sampling point.

`cupid_gb_read_u8` and `cupid_gb_write_u8` apply memory mapping, access restrictions, and register effects at the current phase. They do not supply an instruction's bus schedule. Debugger raw inspection is another interface: it can observe storage that the CPU cannot currently read. Drive CPU execution when testing a CPU-visible bus result.

Timing helpers must preserve these totals independently:

- CPU M-cycles and divider progression.
- Base T-cycles in `elapsed_cycles`.
- PPU dots and persistent pixel-fetch work.
- APU oscillator clocks and integrated output.
- DMA progress and serial divider edges.

Changing only a final cycle counter cannot repair work that occurred in the wrong order.

## Instruction and interrupt bus phases

Count phase labels from the start of the operation being measured, in CPU T-cycles. State whether an earlier HALT polling cycle is included. At CGB double speed, 20 CPU T-cycles occupy 10 base T-cycles; a label such as T18 is not LCD dot 18.

Ordinary stores, memory read-modify-write instructions, PUSH, CALL, RST, and interrupt entry all reach the bus through different instruction schedules. Check both stack bytes and both taken/untaken paths where they exist. A stack address can be an I/O register, including IE, IF, LCDC, or STAT, so treating stack traffic as plain RAM can change interrupt selection and graphics state.

Interrupt entry occupies five CPU M-cycles, or 20 CPU T-cycles. Ordinary stack targets have nominal high-byte and low-byte write boundaries at T12 and T16. The selected request is acknowledged at T18, and the final entry cycle completes at T20. Register-specific conflict phases can expose an earlier or later write within that schedule; the nominal boundaries are not a rule for every I/O register.

IE is the interrupt-enable register, IF holds latched request bits, and IME is the CPU's master interrupt enable. The enable and request samples must remain separate. IE is sampled after the high stack write's register sequence completes. IF is sampled after the low stack write's sequence, except that a low write to IF selects from the value observed before that write. The selected bit is cleared at the acknowledge edge in the current IF value, preserving unrelated requests. If no enabled request survives selection, the entry still consumes its clocks and uses the cancellation target at address zero.

`cupid_gb_interrupt_tail` completes the interval from T8 through T20 on one local timeline. For a nominal write boundary B, its register cases are:

| Stack target | Register sequence and selection sample |
| --- | --- |
| Ordinary target | Commit at B; sample the corresponding IE/IF selection input afterward |
| Non-CGB LCDC | Expose the early LCD-control phase at B-2, then commit and sample at B-1 |
| STAT | Expose the model-specific transient at B, then commit and sample at B+1 |
| CGB normal-speed LCDC | Commit at B; a falling tile-selection bit extends the sample point to B+1 |
| CGB double-speed LCDC | Expose other control bits at B-2 while retaining the previous background/LCD-enable bits until B; commit and sample at B |
| High stack byte targeting IF | Commit and sample at B+1, after that cycle's earlier request events |
| Low stack byte targeting IF | Retain the pre-write IF value for selection, then commit at B |

Here B is T12 for the high byte and T16 for the low byte. The second register plan uses the state left by the completed first write. For example, a low non-CGB LCDC write samples requests at T15, while a low STAT write samples at T17. Both retain T18 acknowledgment and T20 completion. This table describes interrupt stack traffic; it does not establish the same conflict coverage for every ordinary instruction write.

The initial two M-cycles account for the first eight CPU T-cycles. The tail clocks its CPU divider/timer domain and OAM DMA at T10, T14, and T18, after any write or acknowledgment scheduled at that phase and before the following LCD dot. Keep those updates separate from the register-plan cursor: changing a write phase must not add another M-cycle or move the acknowledge behind a later timer request.

At double speed, an odd CPU T-cycle event can fall between two LCD dots. The tail keeps that position in a local cursor while advancing `elapsed_cycles`, the pixel pipeline, and APU clocks only on base-clock dots. It can synchronize the CGB startup comparison between those dots without advancing a pixel or oscillator. It consumes the complete sequence before returning, so no half-dot cursor becomes persistent guest state. This narrow synchronization does not establish general half-cycle accuracy for every device.

Ordinary STAT stores use the same one-CPU-T-cycle transient and late commit as interrupt stack traffic. `CupidGbCpuBus` tracks the borrowed T-cycle within one instruction. A following stack or word-store byte consumes the remaining three T-cycles of its leading M-cycle; the final instruction tail repays the same credit. This cursor is local to execution and is never part of `CupidGb` or a native snapshot. LDH, indirect and absolute stores, memory read-modify-write instructions, PUSH, CALL, RST, and both bytes of `LD (a16),SP` use that schedule.

Non-CGB STAT writes temporarily enable all four interrupt sources, except at the LY boundary when an already enabled HBlank source blocks the transient OAM enable. The other temporary enables still take effect. CGB normal speed retains the old coincidence-enable bit for one CPU T-cycle, while double speed retains the old HBlank-enable bit. The CPU selects this control before the final write M-cycle. A pending KEY1 transition within that cycle changes the clock rate without changing the control already selected for the access. These register phases consume the instruction's existing clocks.

Peripheral requests arriving before and after acknowledgment are distinct cases. A request already present at the acknowledge edge can be cleared; a later edge must remain pending. Divider/timer work after the clear can raise IF again within the same entry cycle. Interrupt tests therefore need phase sweeps around the edge, not only one initial IF value and one final vector.

The CPU disables IME for dispatched entry and retains separate HALT wake behavior. An enabled pending request can wake HALT even when IME does not dispatch it. The instruction executed after that wake must use the bus state at its actual fetch, which can differ from a debugger's earlier raw peek.

## HALT, STOP, and divider edges

HALT polling has model and first-cycle behavior. CGB and the first cycle after entering HALT sample pending requests before advancing that cycle. Later monochrome polls distinguish a midpoint LCD observation from timer/serial events that occur later in the cycle. Keep the first-HALT latch in resets and snapshots; removing it can change the wake instruction.

With IME clear, an enabled request wakes HALT in that poll and leaves the next opcode for the following step. This differs from the HALT bug, where a request already pending when HALT executes prevents sleep and changes the next fetch. Expiry of a timed speed-switch wait follows another rule: the expiring poll can execute the next opcode in the same step.

STOP follows a separate path. The current CPU-domain clock helper suppresses divider/timer progression while the CPU is stopped, while the bounded step path still provides host progress and input checks. Do not infer complete hardware STOP behavior from HALT tests or treat a stopped CPU as an ordinary running instruction.

On physical CGB hardware, prepared KEY1 and no selected held key enter speed-switch sequencing. A no-request switch uses a 131,080-CPU-T timed HALT. Normal-to-double switching has a separate six-CPU-T toggle delay; the reverse direction switches immediately. A short freeze suppresses LCD/returned base-clock progress while the divider and wait countdown can continue. Each call still exposes a HALT polling boundary, so interrupts and input can affect the wait. An enabled request cancels the timed wait without canceling its pending toggle or freeze.

STOP resets DIV before switching. IME clear holds that reset for four further CPU T-cycles; EI maturing into STOP supplies the IME-set control behavior. A selected held key suppresses switching and leaves KEY1 prepared. A request sampled within STOP can leave the pad byte unconsumed. Ordinary STOP wakes from its selected input lines; the timed HALT instead uses enabled interrupt requests. Keep these paths separate from host focus and key-event policies.

Short freeze intervals require care when observing sound. Divider work can queue audio clocks even while LCD progress is frozen. The current core advances that audio work eagerly. In seeded 44,100/192,000-Hz controls, it exposes one host PCM frame during a fully frozen poll where the comparison build defers its callback until a sound-register read or later poll. Guest PCM and total delivered frames agree after that flush. Exact frozen-poll host delivery ordering remains an open comparison gap. The pinned reverse-switch odd-mode rules also lack separate physical-hardware qualification.

TIMA uses falling edges of its selected divider input. Resetting DIV or changing TAC can therefore create a timer pulse without waiting for the next ordinary divider increment. Overflow exposes zero before the delayed reload and interrupt. TIMA and TMA writes in that reload window have different effects. Preserve the pending reload latch and its write rules when splitting a cycle.

The divider also drives serial timing and the APU frame sequencer. A DIV write is not only an update to the readable DIV byte. Moving it across an instruction boundary can change all three consumers.

## PPU state and the shared STAT line

[ppu.c](../src/gb/ppu.c) owns scanline progression, visible access modes, LY/LYC comparison, and interrupt edges. [pixels.c](../src/gb/pixels.c) retains background/object fetch queues, emitted position, window state, and pending object work. Already fetched data must remain stable across later memory writes; palette selection happens at the pipeline's defined output point.

Keep three observations distinct: the readable STAT mode bits, the interrupt condition presented by the PPU, and the request already latched in IF. A visible mode change does not by itself establish a new STAT interrupt. Mode and coincidence sources share the `stat_irq_line` edge latch. An interrupt request occurs when the combined line rises. Clearing IF while that line stays high does not create another rising edge.

The line-tail OAM interrupt input becomes available to explicit STAT/LYC writes before the readable mode changes. Automatic latch edges remain separate: recomputing the combined line from readable HBlank mode on every intervening dot would create an extra interrupt after some mask changes. OAM mode also does not imply a continuously asserted OAM interrupt input. The automatic line-start update presents that source and then removes it while mode 2 stays readable. CGB double speed has an additional explicit STAT-write edge at the first OAM-search position.

The LY=LYC interrupt input retains its previous value while the comparison is invalid. During that phase, monochrome profiles clear the readable coincidence bit and CGB retains it. These boundaries occur during line changes, the transition to VBlank, and line 153. During a STAT write that spans a speed switch, the divider's speed selection determines the audio clocks, even if the LCD consumes part of the cycle at each rate.

At the final pixel, `stat_irq_delay` records a pending one-dot HBlank transition. Normal speed exposes mode 0 before that delayed interrupt input. Double speed keeps mode 3 visible until the delay is consumed, then exposes mode 0. A STAT or LYC write can raise the coincidence source during this interval. That independent edge must not cancel the pending HBlank transition or leave mode 3 latched after the transfer has finished.

The PPU consumes the delay; LCD reset or disable clears it. If coincidence keeps the combined line high when HBlank arrives, the mode transition still completes without inventing a duplicate rising-edge interrupt. Snapshot restore must preserve both the pending transition and the combined-line latch.

LCD power changes have their own startup state. The first enabled line skips the ordinary OAM search and uses a shortened startup interval. Access restrictions can change before a visible mode bit, so tests must check memory access as well as STAT readback. STAT's unused top bit reads high through the bus even when its stored power-on byte is zero.

Disabling and re-enabling the LCD preserves the coincidence and combined STAT latches until a startup refresh. Reading STAT immediately after enable does not force a new comparison. Automatic refresh occurs after the second processed LCD dot on non-CGB profiles and at the first advancing edge on CGB. During a double-speed interrupt tail, that CGB edge can be the first half-dot. An explicit STAT or LYC write still updates comparison immediately, even before the automatic refresh.

`cupid_gb_sync_lcd_startup` exposes that CGB startup edge without advancing the pixel pipeline or counters. Clearing the fetch pipeline must retain `pixels.lyc_irq`; otherwise power cycling can fabricate a falling and rising comparison sequence. The HBlank delay and the frozen comparison latch have different reset rules.

## Audio sample and filter units

The APU's channel state advances in base-clock dots. Divider events separately advance its frame sequencer, with the CGB speed-dependent divider selection preserving that sequencer's physical cadence. Host sample rate is not an oscillator speed control.

The [audio subsystem guide](audio.md) describes channel power, DAC gating, held output, envelope and sweep pipelines, wave access, noise clocks, and model-specific limits.

For an output rate R and physical base clock H, each base dot contributes R sample-phase units. One output stereo frame spans H phase units. `sample_acc` stores the current phase; `mix_left` and `mix_right` integrate channel amplitude over that phase. Splitting a dot interval at a CPU register edge must retain the partial sample, including both sides of a sample boundary.

The filter retains fixed-point capacitor state separately from channel power and uses a coefficient derived from the model clock and output rate. The default output is 44,100 stereo frames per physical second, including SGB NTSC. A double-speed CPU does not produce twice as much audio. Output-rate changes discard output that belongs to the old rate while retaining oscillators; ordinary timing splits must not perform that reset.

Drain the finite PCM buffer during LCD-off execution as well as ordinary images. A host that waits indefinitely for VBlank can lose samples even when the emulated channel timing is correct. Core PCM, host conversion, the audio-device queue, and a recording are separate stages. Clearing a host playback queue after a state load must not erase the snapshot's pending core PCM.

## DMA and serial consequences

OAM DMA advances through CPU M-cycle calls, including startup/restart delay counters and at most one copied byte per call. CGB double speed changes the base-time distance between those calls. Moving a subcycle write must not duplicate or skip a DMA call. Bus restrictions during DMA must be tested through CPU-visible reads and writes, not only by comparing the final OAM array.

The CGB block-transfer paths and their HBlank triggers are separate from OAM DMA. Writing FF55 queues a request; it does not synchronously replace VRAM. The next CPU opcode is fetched and latched before pending transfer work runs. DMA may overwrite its backing memory while the CPU still executes the fetched byte.

A block copies sixteen bytes at two base dots per byte. Its atomic scheduler advances the CPU divider, OAM DMA, serial, LCD, and sound clocks through two-dot slots. Startup and tail overhead differ with CPU speed. For an otherwise ordinary NOP and one ready block, the complete step consumes 40 base T-cycles at normal speed or 36 at double speed. A general transfer can run several blocks in that step; an armed HBlank transfer consumes one block per request. No frame skipping or reduced sound work substitutes for these stalls.

The HBlank request settles three base dots after the final pixel. This delay is separate from STAT's readable mode and interrupt delay. Sleeping HALT/STOP suppresses newly arming that request. A sleep-entry latch records whether entry occurred outside HBlank; waking in HBlank can then request one block. Disabling the LCD while an armed transfer was in a nonzero mode requests one block. Canceling through FF55 clears ready work and delay, and the new control value supplies the canceled remaining-count status.

These paths do not establish complete bus accuracy. The current transfer-local byte does not reproduce shared main-bus decay for undriven sources. Mode-3 PPU fetch-address interference with DMA destinations also remains incomplete. Match full VRAM, clocks, CPU state, and status independently: matching only transfer duration can hide incorrect destination bytes. Changes to final-pixel and HBlank ordering need DMA regressions as well as interrupt and image checks.

Serial shifts on selected internal-clock divider edges. An unplugged incoming line is high. Completion clears SC's start bit and requests the serial interrupt before notifying the optional host observer with the byte latched at transfer start. The callback occurs within the emulation step; later work in the enclosing M-cycle can still be pending. It must not mutate or reenter the core.

The core performs no serial stream output or flushes. The runner and other hosts own buffering and diagnostics. Changing output policy must not change divider edges, received data, or interrupt timing. The observer is not a bit-level external cable implementation.

## Snapshot and host boundaries

Save or restore at an instruction boundary with exclusive core access. An interrupt tail helper and its register transients are atomic within a hardware step; a host callback is not permission to capture a partially completed instruction. Preserve the guest latches needed for continuation: timer reload state, serial phase, DMA delays, fetch queues, HBlank transition, combined STAT line, HALT phase, partial audio, and pending PCM.

The native codec uses explicit field widths and byte order. A phase repair can change generated field values without changing a schema. Version 3 adds persistent DMA and audio latches that version 2 did not record. Do not add host-only helper cursors or temporary event lists to the guest structure merely to implement an atomic sequence. If a new persistent hardware latch is required, update the codec and validate its continuation deliberately. See [save states and rewind](save-states.md).

CPU, PPU, and APU execution never sleep or use elapsed host time to decide how many hardware clocks to skip. Cartridge calendar time is a separate domain: MBC3 register/latch and battery helpers can use the host calendar, and explicit `_at` battery functions accept a supplied timestamp. Deterministic RTC checks must control that input. This exception must not turn host scheduling into a source of CPU or LCD clocks.

## Qt deadlines and measurements

Qt calculates playback time from the base T-cycles consumed by its bounded work interval. `GuestClockPacer` divides by the selected physical model clock and playback rate, carrying the fractional nanoseconds across ordinary intervals. A clock/rate change or timeline rebase resets the applicable fraction. Partial LCD intervals are charged only for their consumed cycles.

The work loop stops at a completed image or a bounded base-cycle interval, with a separate step budget. This keeps LCD-off programs and unusual instruction streams responsive. Turbo and rewind use explicit host policies. Audio backpressure can delay more guest work; it must not discard guest clocks or disable audio generation to hide timing errors.

State loads and rewind replace the guest timeline. Reset deadlines and host audio/conversion history, reconcile physical input, and reject stale session measurements. A negative net cycle delta after a restore is not forward throughput. Rewind's duration limit converts stored cycle differences through the model clock.

Runtime reports record `requested_model`, actual `hardware_model`, and `base_clock_hz`. Compute guest speed from base-cycle progress divided by H and measured host seconds. Report widget paints, completed images, audio samples, and physical presentation separately. Neither 60 widget paints per second nor a near-100% bounded guest rate establishes complete gameplay or audible accuracy.

## Validating a timing change

Start with the source that produces the edge and the source that consumes it. Record the pinned comparison revision, hardware model, boot method, and cycle origin. Keep traces and private inputs outside public commits. A useful paired probe records the values immediately before and after an event as well as the final instruction result.

For a bus or interrupt change, cover normal speed and CGB double speed, register targets on both stack-byte positions, pending requests on either side of sampling/acknowledgment, and CPU/APU/PPU totals. For a STAT change, check visible mode, coincidence state, combined line, IF, and CPU memory access. Include a snapshot captured before the edge and compare its resumed continuation.

Run the focused native targets before broader suites:

```sh
ctest --test-dir build --output-on-failure \
    -R 'cupidgb_(cpu_timing|ppu|stat|apu|serial|state|core_interop|guest_clock_pacer)$'
```

For a Visual Studio build, add `-C Release`. Also run the Qt workflow smoke for deadline, state, audio, and session integration. Sanitizers and Release IPO exercise different host-code paths; neither replaces the other.

Use equivalent explicit models, boot binaries, settings, and inputs for external comparisons. Require the ROM's explicit success protocol. A bounded run is inconclusive. Compare complete state/PCM data where appropriate and inspect the resulting images. Broaden to commercial graphics, sound, input, saving, and sustained playback before making gameplay claims. [Testing and measurements](testing.md) gives the runner and frontend commands.

## Remaining hardware work

The timing helpers cover tested observations rather than every register conflict or hardware revision. Ordinary instruction writes and interrupt stack writes need separate coverage. HBlank/OAM boundary STAT-write qualification, fine LCDC/palette conflicts, OAM corruption, DMA contention/stalls, STOP behavior, serial clock changes and external transport, and revision-specific startup remain incomplete. Grouped CGB and monochrome profiles do not qualify each physical revision. SGB PAL and full SGB host integration remain pending.

Preserve failing and inconclusive cases. A narrow phase fix must not be described as complete interrupt, graphics, or audio accuracy without corresponding evidence.

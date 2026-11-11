# Audio subsystem

CupidGB separates emulated sound hardware from host playback. The core advances channels from guest clocks and produces signed 16-bit stereo PCM. Qt handles device selection, conversion, buffering, volume and recording. A change to a host device must not change channel state or guest timing.

This guide describes the implemented behavior and its limits. The [parity checklist](parity.md) tracks remaining work, and [validation](validation.md) records the evidence for a particular build.

## Code and ownership

| Files | Responsibility |
| --- | --- |
| `src/gb/apu.c`, `include/cupid/gb/apu.h` | Channel clocks, register side effects, digital taps, mixing, sample integration and filtering |
| `include/cupid/gb/gb.h` | `CupidGbApu`, its timing structures and the PCM drain API |
| `src/gb/timer.c` | DIV edges and conversion from CPU machine cycles to base T-cycles |
| `src/gb/gb.c`, `src/gbc/cgb.c` | CPU-visible register masks, wave RAM access and color PCM registers |
| `src/gb/state.c` | Portable snapshots, hardware-domain checks and older-state import |
| `src/frontend/qt/main_window.cpp` | Playback queues, pacing, device changes, pause/load/rewind transitions and recording control |
| `src/frontend/qt/audio_converter.cpp`, `wave_recorder.cpp` | Host-format conversion and RIFF/WAVE encoding |

The core has no Qt dependency and does not open an audio device. Run it and inspect or change its audio state on the same serialized execution path. Snapshot operations require an instruction boundary without concurrent core access. The host audio backend must not read or mutate `CupidGb` from its playback thread.

## Clock domains

`cupid_gb_tick_apu` accepts **base T-cycles**, not CPU machine cycles. Ordinary emulator stepping already advances it through the timer subsystem. Adding a second APU tick after an instruction advances sound twice.

| Clock | Implemented relationship |
| --- | --- |
| Base clock | `cupid_gb_base_clock_hz(gb)`: 4,295,454 Hz for SGB; 4,194,304 Hz for the other current models |
| CPU machine cycle | Four base T-cycles normally; two in CGB double speed |
| Pulse and sweep timing phases | Retained phase modulo four base T-cycles |
| Color noise prescaler | One tick per two base T-cycles, with a separate phase modulo eight |
| Frame sequencer | One primary DIV edge per 8,192 base T-cycles |
| Output sampling | Independent phase accumulator at the configured PCM rate |

The source sometimes calls divided clocks 1 MHz and 2 MHz. Those names describe the handheld clock domains. Their wall-time rates follow the selected model's oscillator. CGB double speed changes CPU throughput without doubling sound pitch or the frame-sequencer rate.

`timer.c` supplies the sequencer's falling DIV edges and a secondary rising edge used by color envelope timing. It selects the appropriate divider bit for normal or double CPU speed. A DIV reset can itself create a falling edge and must use the same edge handling as an ordinary divider transition. Calling `cupid_gb_tick_apu` alone does not generate DIV events; it still advances calculations or delays that an earlier event scheduled.

The sequencer has eight phases. Length counters clock at phases 0, 2, 4 and 6, sweep at 2 and 6, and the ordinary envelope event at 7. At the handheld clock these correspond to 256, 128 and 64 Hz; use the model clock when converting them to elapsed seconds. Powering on while the DIV input is high inhibits one edge before sequencer phase zero resumes. The retained gate state matters across snapshots and register writes.

During monochrome STOP, the implemented oscillator path holds while host sample integration continues. The color-hardware path continues its oscillator clocks. HALT, STOP and speed switching also depend on the CPU/timer implementation; an isolated APU check does not establish their complete system behavior.

The short CGB speed-switch freeze suppresses LCD progress while divider work can queue sound clocks. CupidGB processes those sound clocks eagerly. A seeded frozen-poll control exposes a host sample before the comparison build's deferred callback, with matching guest PCM and delivered totals after a register read or subsequent poll. Exact callback delivery at that boundary remains unqualified. See [HALT, STOP, and divider edges](timing.md#halt-stop-and-divider-edges).

## Power, DACs and register access

NR52 bit 7 controls APU power. A real power transition resets channel and timing state. Power-off clears sound control registers through NR51, but wave RAM remains separate. On monochrome hardware, the permitted length-register writes while powered off can survive the following power-on. Color hardware ignores those powered-off channel writes. Rewriting the existing power state does not perform another reset.

Power transitions preserve the output pipeline: queued PCM frames, a partially integrated output frame, sample-rate configuration and high-pass capacitor state. Already generated sound cannot be erased by a later guest register write. Resetting the whole emulator or entering a boot ROM has a different lifecycle from a guest NR52 write.

Keep these states distinct:

- **DAC enabled:** NR12, NR22 and NR42 enable their DAC when any of bits 3 through 7 is set. NR30 bit 7 controls the wave DAC. Disabling a DAC also disables its channel.
- **Channel active:** Trigger, length and sweep behavior determine channel status. NR52 reads rebuild the four status bits from current channel state.
- **Held digital output:** A channel can retain a sampled duty or LFSR result independently of its current counter and register values.

An inactive channel contributes digital zero through an enabled DAC. That is different from disconnecting the DAC: the signed transfer maps digital value `d` to `15 - 2*d`, so digital zero has a positive DC level before filtering. Do not silence an inactive channel by bypassing its still-enabled DAC in the mixer.

Route guest accesses through the memory map. Sound-register read masks expose unused bits as high, and readback is not a dump of internal fields. Writing `io_registers` directly bypasses trigger, length, DAC and timing side effects. Calling the low-level write handler after replacing the old register value can also lose the previous-value information needed for live-write behavior.

Hardware model checks use `gb->model`. A monochrome cartridge on CGB hardware still uses the color APU paths. The cartridge's color-mode flag is not a substitute for the physical model when selecting these behaviors.

## Pulse channels and sweep

CH1 and CH2 each have an eight-step duty waveform, an 11-bit frequency, a length counter and an envelope. The four duty patterns produce nominal high fractions of 1/8, 2/8, 4/8 and 6/8. An ordinary duty step spans `4 * (2048 - frequency)` base T-cycles. Eight steps form a complete waveform; trigger settling and live writes can change the first interval.

The CGB path keeps frequency-clock startup, the last edge's age, a held duty bit and output suppression separately in `CupidGbPulseTiming`. An inactive start holds output low until the appropriate clock edge. Retriggering an active channel has different settling behavior and retains duty phase. Changing the duty register does not justify recomputing a held output immediately. Frequency writes near a counter reload can affect that reload or the duty clock, so the last edge's age is part of the state.

The monochrome path has simpler pulse counters. It must not inherit the newer color timing rules merely because a shared helper is convenient. Its remaining model-specific quirks need separate validation.

CH1 adds sweep. Live frequency, sweep shadow frequency, a calculated delta and a completed delta have different roles. The color path schedules setup and calculation work on the divided clock and retains a short restart hold in base T-cycles. A calculation can finish after the DIV event that started it. Overflow can disable CH1, and changing sweep controls while work is pending can affect the result. Zero shift, subtraction history and restart behavior therefore need more than one periodic frequency update.

Length-enable writes can clock length immediately at particular sequencer phases. A trigger reloads an exhausted length, and that reload interacts with the immediate length clock. The same principle applies to the wave and noise channels, with their respective length capacities. Tests must check register writes at the relevant phase rather than relying only on a long-running tone.

## Envelopes

CH1, CH2 and CH4 have four-bit volume and programmable direction/period. Triggers reload their initial volume and envelope timing. A zero period suppresses ordinary automatic volume changes; it does not make later envelope-register writes inert.

The color pulse envelopes retain rising/falling clock state, endpoint locking and a pending lock. A register write between clock edges can change volume or direction without undoing the endpoint already sampled by the clock. Color double-speed processing also retains a possible two-base-clock delay. These facts live in the timing structures and `envelope_delay`; reconstructing them from the current volume loses information.

CH4 and the monochrome pulse paths still use simpler envelope counters. Their implementation should not be described as complete coverage of every live-write or phantom-trigger case. In particular, digital table acceptance for the color pulse paths does not establish all noise-envelope or monochrome behavior.

## Wave channel

CH3 reads 16 bytes of wave RAM as 32 four-bit samples. NR32 selects mute, full level, half level or quarter level. Each ordinary sample interval is `2 * (2048 - frequency)` base T-cycles.

A trigger starts a fetch sequence with startup delay. `ch3_sample` is the held output nibble, not an immediate read of wave RAM. The implementation tracks the fetched byte, sample position, startup state, access window and pending frequency change. On the color path, an active frequency change can wait for the next fetch before replacing the active period source.

While CH3 is active, CPU wave RAM accesses alias the current fetched byte. Monochrome hardware additionally restricts access to the modeled fetch window; a blocked read returns `0xff` and a blocked write does nothing. Color hardware uses its own access path without that monochrome window restriction. When the channel is inactive, the addressed wave RAM byte is available normally.

Monochrome retriggering near a fetch can corrupt wave RAM. The current implementation has a bounded corruption model, but wave-bus timing and model/revision differences remain incomplete. Do not generalize a passing waveform test to every retrigger or access-conflict case.

## Noise channel

CH4 uses a retained 15-bit LFSR. The implementation stores its complemented representation; its normal feedback is XOR, and a zero in stored bit 0 corresponds to a high logical digital output. Seven-bit mode changes the feedback destination without truncating the retained upper stages. Those stages matter when software changes width.

The CGB path uses `CupidGbNoiseTiming` rather than a single full-period timer. Its prescaler reload is two divided-clock ticks for divisor code zero, otherwise four times the divisor code. A selected rising bit of the retained counter clocks the LFSR. Ordinary counter increments mask to 14 bits, so selectors 14 and 15 do not produce ordinary divider edges.

Trigger delay depends on power-relative phase, divisor encoding and previous channel/gate activity. Equivalent nominal frequencies can therefore have different initial delays. Retriggering retains divider history, resets the LFSR's trigger state and forces the held output low. The held output can differ from the LFSR's current low bit, so PCM reads and mixing must use the latch.

Foreground and background counting gates are separate. DAC disable can leave a background counter running, while re-enabling a DAC alone does not trigger the channel. A DAC-disable edge can also increment the held counter without the ordinary 14-bit mask. Repeated start/stop transitions can retain carries across the full 16-bit storage range. Do not reject such snapshots solely because the counter exceeds `0x3fff`.

NR43 writes can change a selected bit from low to high and clock the LFSR without elapsed guest time. Changing the divisor exactly on a prescaler reload edge also differs from changing it between edges. `reload_edge`, counter history and the held sample preserve these distinctions. The older `ch4_timer` remains in the legacy state prefix and serves the simpler non-CGB path; it is not the color prescaler's remaining count.

The implemented selector-edge path does not cover every high-selector corruption. Retained width and pre-shift history are present, but the remaining corruption paths still need implementation and validation. This is separate from passing the basic alignment, frequency-change, width-switch and restart tables.

## Digital taps, mixing and sample generation

FF76 exposes CH1 in its low nibble and CH2 in its high nibble. FF77 similarly exposes CH3 and CH4. These are digital samples before routing, master gain, filtering and host volume. They remain readable on CGB hardware in monochrome compatibility mode; other hardware returns `0xff`.

NR51 routes each DAC independently to the left and right mixer. NR50's two three-bit gain fields select factors one through eight, so a field value of zero is the lowest gain rather than mute. The current mixer does not provide an external VIN signal. Qt mute and volume act after core mixing and do not change PCM register reads or guest registers.

The core integrates the mixer over output-sample boundaries. Each base T-cycle adds the configured sample rate to the sample phase, splitting a contribution when it crosses the model clock's boundary. `mix_left` and `mix_right` retain the weighted partial areas. This avoids taking only one instantaneous value from a rapidly changing oscillator, but the box integration is not a complete reconstruction filter or an analog-circuit model.

The default rate is 44,100 stereo frames per second. `cupid_gb_apu_set_sample_rate` accepts 8,000 through 192,000 Hz. Passing zero to that setter is invalid; a zero `apu.sample_rate` field is an internal default-rate sentinel understood by the getter. Changing the effective rate discards queued and partial output frames while preserving oscillator state. Setting the same effective rate refreshes the derived filter coefficient without discarding that output.

DC blocking is enabled by default. The filter keeps Q16 capacitor state and a Q30 retention coefficient derived from the model clock and output rate. Disabling it through `cupid_gb_apu_set_highpass` clears the capacitors. With all monochrome DACs disconnected, the implemented output is zero and the capacitor holds its charge; the color path keeps the filter connected. Final samples clamp to signed 16-bit range.

The buffer holds 2,048 interleaved stereo frames. `cupid_gb_apu_drain` copies up to the caller's frame limit, returns a **frame count**, and retains the unread tail. Allocate two `int16_t` values per returned frame. Draining does not advance time. A headless host must drain regularly too: when the buffer is full, new output frames are not retained, although hardware and integration/filter state continue advancing. The queue is bounded storage, not an unlimited recording facility.

## Host playback and recording

Qt requests stereo signed 16-bit playback at the default core rate, then uses the selected device's preferred format when necessary. `AudioConverter` performs linear interpolation with persistent fractional position and boundary samples. It averages stereo for a mono sink and fills extra output channels with zero. The final source sample can remain pending until its right neighbor arrives in the next block.

The host queue primes with generated audio before starting the sink. Its current target is 30 ms, below the 50 ms execution-backpressure threshold. When the host cannot accept more audio, normal playback waits before advancing more guest work. It does not skip guest cycles or discard core samples to reach a presentation rate. Device fallback, idle events and queue metrics need separate runtime validation from core channel timing.

Pause, successful state load, rewind transitions and other timeline discontinuities clear stale host audio, reset conversion and re-prime playback. Failed state loads preserve the live session. Output-device changes replace the sink while preserving the guest hardware. The discarded sink is reset without requesting a drain of obsolete queued audio. Pause and timeline changes do not wait for those samples to play. The ordinary turbo path generates and drains core audio while omitting playback; it must not be used as evidence for normal-audio performance.

Slow-motion playback changes the converter's effective input rate. WAVE recording receives core PCM before that conversion and before host mute or volume. It records signed 16-bit, two-channel, little-endian RIFF/WAVE at the core rate. Pauses and backward rewind add no audio. Forward debugger execution can append generated samples while paused playback remains silent.

Loading or rewinding does not roll back bytes already written to a recording. Later forward execution appends its output. Reset, cartridge replacement, close and a changed core sample rate end the current file. The recorder bounds RIFF's 32-bit size fields, reports write failures and commits through `QSaveFile`. See [the user guide](user-guide.md) for recording controls.

## Snapshots and continuation

Current native states encode channel registers/counters, held outputs, divided-clock phases, pulse/envelope/sweep/noise timing, pending PCM frames, partial integration and filter state. These are explicit portable fields, not a dump of `CupidGbApu` memory. Device handles, sink queues, conversion positions and recording files belong to the host and are excluded.

Version 2 states lack the newer power/trigger/edge history. Import initializes the missing timing fields from a deterministic approximation of the known prefix. It preserves that prefix, queued PCM, mixer areas and capacitors rather than inheriting unrelated live timing from the destination. It cannot recover an unrecorded counter phase or earlier register-write sequence. Do not describe imported version 2 audio as exact continuation of that missing history.

SGB loading also refreshes a nonzero derived high-pass coefficient for the model clock at the same sample rate. It keeps pending samples, partial integration and the default-rate sentinel intact. Query the state size and use the codec; do not hard-code structure offsets or infer state length from this guide. The full compatibility and host-reset contract is in [save states and rewind](save-states.md).

## Validation and remaining work

The APU regression target covers DIV clock/reset behavior, power transitions, length control, DAC transfer and routing, duty phase, PCM taps, pitch, integration, filters, rates, partial drains, STOP behavior and physical model clocks. Color-specific cases cover pulse reload windows, envelope edges, sweep latency, noise prescaler alignment, retriggers, selector writes and the held-counter carry. State tests check continuation and older-state initialization. Frontend converter/recorder tests and Qt workflows exercise different parts of the path.

With the relevant targets built, run:

```sh
ctest --test-dir build --output-on-failure -R 'cupidgb_(apu|state|audio_converter|wave_recorder|qt_workflows)$'
```

Qt-dependent targets require a frontend-enabled build. The ordinary offscreen workflow test does not prove that a real audio device played sound. Native audio smoke checks, device changes and sustained gameplay belong in the platform validation described in [testing](testing.md).

Authored digital result tables establish particular register/timing observations for the modeled CGB profile. They do not establish every hardware revision, all commercial audio, complete analog behavior or host audio stability. Wider non-CGB quirks, unaligned and other-revision pulse-trigger cases, high-selector noise corruption, filtering/interference behavior and broader APU acceptance remain open. Report digital-tap comparisons, generated PCM, host playback and listening results separately so one result does not stand in for another.

The current color pulse model retains the old timer and frequency through a trigger. A qualifying old counter phase can immediately advance duty, while a suppressed retrigger at the maximum frequency adds four base dots. Inactive channels retain reload qualification for later frequency writes. A fresh trigger clears the previous duty-cancellation opportunity without inventing a new persistent latch.

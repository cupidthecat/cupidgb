# Audio subsystem

CupidGB separates emulated sound hardware from host playback. The core advances handheld channels from guest clocks and produces signed 16-bit stereo PCM. SGB startup has its own presentation clock and sound history. Qt handles device selection, conversion, buffering, volume and recording. A change to a host device must not change channel state or guest timing.

This guide describes the implemented behavior and its limits. The [parity checklist](parity.md) tracks remaining work. [Audio and native-state validation](validation-audio.md) records the earlier audio checkpoint; the [validation record](validation.md) tracks subsequent checks. The [SGB startup guide](sgb-startup.md) distinguishes its new implementation from qualification still pending.

## Code and ownership

| Files | Responsibility |
| --- | --- |
| `src/gb/apu.c`, `include/cupid/gb/apu.h` | Channel clocks, register side effects, digital taps and pending-clock delivery |
| `src/gb/apu_noise.c` | Noise divider, feedback latches, trigger delays and selector transitions |
| `src/gb/audio_synthesis.c`, `audio_kernel.inc` | Fractional transition reconstruction and the generated coefficient bank |
| `src/gb/audio_output.c`, `audio_filter.c` | Output scheduling, DAC response, listening policy, filters and PCM draining |
| `src/gb/sgb_startup_audio.c`, `include/cupid/gb/sgb_startup_audio.h` | Original startup synthesis, retained mono batches and tagged stream order |
| `include/cupid/gb/gb.h`, `audio_output.h`, `audio_types.h` | Separate hardware, output-history and frame structures |
| `src/gb/timer.c` | DIV edges and conversion from CPU machine cycles to base T-cycles |
| `src/gb/gb.c`, `src/gbc/cgb.c` | CPU-visible register masks, wave RAM access and color PCM registers |
| `src/gb/state.c`, `audio_state.c`, `state_startup.c`, `state_io.h` | Portable snapshots, hardware/output checks and older-state import |
| `src/frontend/qt/main_window_audio.cpp`, `main_window_session.cpp` | Device changes, pacing, pause/load/rewind transitions and recording control |
| `src/frontend/qt/core_audio_collector.cpp` | Bounded collection of the complete stream with origin spans for recording |
| `src/frontend/qt/audio_buffer.h` | Generated-sample queue and reentrant host-write transactions |
| `src/frontend/qt/audio_converter.cpp`, `wave_recorder.cpp` | Host-format conversion and RIFF/WAVE encoding |
| `src/frontend/qt/turbo_audio_window.cpp` | Whole-frame Turbo playback selection and phase retention |

The core has no Qt dependency and does not open an audio device. Run it and inspect or change its audio state on the same serialized execution path. Snapshot operations require a completed hardware-step boundary without concurrent core access; this includes a held startup step. The host audio backend must not read or mutate `CupidGb` from its playback thread.

Backend writes can dispatch Qt events, including telemetry and session changes. The queue releases its mutex before calling the device and retains the pending bytes until that call returns. A nested write cannot send the same prefix again. A timeline clear invalidates the pending transaction's removal, so it cannot consume replacement samples. Resetting telemetry also starts a new counter generation. An earlier write cannot add its accepted bytes to the new window.

The caller retains both the queue and the sink during a backend call. Pausing or replacing a session disconnects and detaches the old sink immediately; its final lease resets and destroys it after the call returns. This keeps the sink-owned device alive without draining obsolete audio. A completed interval commits its display and rewind bookkeeping before calling the backend, then returns without accessing guest or presentation state. The scheduler checks the session, timeline and pacing generation before charging that interval to a deadline. Mode changes inside a guarded backend call defer their audio reset until the leases and execution guard have been released.

## Clock domains

`cupid_gb_tick_apu` accepts **base T-cycles**, not CPU machine cycles. Ordinary emulator stepping already advances it through the timer subsystem. Adding a second APU tick after an instruction advances sound twice.

| Clock | Implemented relationship |
| --- | --- |
| Base clock | `cupid_gb_base_clock_hz(gb)` returns 4,295,454 Hz for SGB NTSC, 4,256,274 Hz for SGB PAL and 4,194,304 Hz for the other models |
| CPU machine cycle | Four base T-cycles normally; two in CGB double speed |
| Pulse and sweep timing phases | Retained phase modulo four base T-cycles |
| Noise prescaler | One tick per two base T-cycles, with a separate phase modulo eight |
| Frame sequencer | One primary DIV edge per 8,192 base T-cycles |
| Output sampling | Independent phase accumulator at the configured PCM rate |

The source sometimes calls divided clocks 1 MHz and 2 MHz. Those names describe the handheld clock domains. Their wall-time rates follow the selected model's oscillator. CGB double speed changes CPU throughput without doubling sound pitch or the frame-sequencer rate.

`timer.c` supplies the sequencer's falling DIV edges and a secondary rising edge used by color envelope timing. It selects the appropriate divider bit for normal or double CPU speed. A DIV reset can itself create a falling edge and must use the same edge handling as an ordinary divider transition. Calling `cupid_gb_tick_apu` alone does not generate DIV events; it still advances calculations or delays that an earlier event scheduled.

The sequencer has eight phases. Length counters clock at phases 0, 2, 4 and 6, sweep at 2 and 6, and the ordinary envelope event at 7. At the handheld clock these correspond to 256, 128 and 64 Hz; use the model clock when converting them to elapsed seconds. Powering on while the DIV input is high inhibits one edge before sequencer phase zero resumes. The retained gate state matters across snapshots and register writes.

During monochrome STOP, the implemented oscillator path holds while host sample integration continues. The color-hardware path continues its oscillator clocks. HALT, STOP and speed switching also depend on the CPU/timer implementation; an isolated APU check does not establish their complete system behavior.

The short CGB speed-switch freeze suppresses LCD progress while divider work can queue sound clocks. Those clocks and their output credit remain pending until a qualifying APU run or forced register/divider boundary. A queue drain cannot force this work early. Comparisons must check the step that delivers each sample as well as its value and total count. See [HALT, STOP, and divider edges](timing.md#halt-stop-and-divider-edges).

An SGB startup hold has a different clock contract. Before phase 96, a core step advances 114 host-only base clocks through display delivery while CPU, DIV, timer, APU, DMA, serial, input sampling and cartridge clocks remain held. Startup sound comes from actual presentation events. It adds no handheld APU credit. `elapsed_cycles` remains guest time; `cupid_gb_presentation_cycles` includes the additional startup time for host pacing.

## Power, DACs and register access

NR52 bit 7 controls APU power. A real power transition resets channel and timing state. Power-off clears sound control registers through NR51, but wave RAM remains separate. On monochrome hardware, the permitted length-register writes while powered off can survive the following power-on. Color hardware ignores those powered-off channel writes. Rewriting the existing power state does not perform another reset.

Power transitions preserve the output pipeline: queued PCM frames, a partially integrated output frame, sample-rate configuration and high-pass capacitor state. Already generated sound cannot be erased by a later guest register write. Resetting the whole emulator or entering a boot ROM has a different lifecycle from a guest NR52 write.

Keep these states distinct:

- **DAC enabled:** NR12, NR22 and NR42 enable their DAC when any of bits 3 through 7 is set. NR30 bit 7 controls the wave DAC. Disabling a DAC also disables its channel.
- **Channel active:** Trigger, length and sweep behavior determine channel status. NR52 reads rebuild the four status bits from current channel state.
- **Held digital output:** A channel can retain a sampled duty or LFSR result independently of its current counter and register values.

Length or sweep expiry drives digital zero through an enabled DAC. The signed transfer maps digital value `d` to `15 - 2*d`, so digital zero has a positive DC level before filtering. A pulse DAC disconnect instead retains its last digital code. Re-enabling that DAC without triggering can expose the retained code until a later channel-output event replaces it. Color PCM register readback still masks inactive channels to zero. The analog disconnect and reconnect response remains an approximation.

Route guest accesses through the memory map. Sound-register read masks expose unused bits as high, and readback is not a dump of internal fields. Writing `io_registers` directly bypasses trigger, length, DAC and timing side effects. Calling the low-level write handler after replacing the old register value can also lose the previous-value information needed for live-write behavior.

Hardware model checks use `gb->model`. A monochrome cartridge on CGB hardware still uses the color APU paths. The cartridge's color-mode flag is not a substitute for the physical model when selecting these behaviors.

## Pulse channels and sweep

CH1 and CH2 each have an eight-step duty waveform, an 11-bit frequency, a length counter and an envelope. The four duty patterns produce nominal high fractions of 1/8, 2/8, 4/8 and 6/8. An ordinary duty step spans `4 * (2048 - frequency)` base T-cycles. Eight steps form a complete waveform; trigger settling and live writes can change the first interval.

The CGB path keeps frequency-clock startup, the last edge's age, a held duty bit and output suppression separately in `CupidGbPulseTiming`. An inactive start holds output low until the appropriate clock edge. Retriggering an active channel has different settling behavior and retains duty phase. Changing the duty register does not justify recomputing a held output immediately. Frequency writes near a counter reload can affect that reload or the duty clock, so the last edge's age is part of the state.

Monochrome pulse clocks start on trigger and continue after length or sweep expiry until DAC or master-power shutdown. Duty writes take effect at the next frequency edge, so mixing uses the sampled duty bit instead of recomputing it from the live register. Inactive and active triggers have different startup delays tied to the retained power phase. An inactive trigger suppresses output until its first qualifying edge. These clocks share storage with the color implementation, but keep their own trigger and gate behavior.

A combined trigger and length-enable write samples channel activity before the extra length clock. If a one-tick counter expires during that write, the trigger still retains active-channel timing and held output while reloading length to 63. Treating the temporary expiry as an inactive start adds the wrong delay and suppresses the held pulse.

CH1 adds sweep. Live frequency, sweep shadow frequency, a calculated delta and a completed delta have different roles. The color path schedules setup and calculation work on the divided clock and retains a short restart hold in base T-cycles. A calculation can finish after the DIV event that started it. Overflow can disable CH1, and changing sweep controls while work is pending can affect the result. Zero shift, subtraction history and restart behavior therefore need more than one periodic frequency update.

Length-enable writes can clock length immediately at particular sequencer phases. A trigger reloads an exhausted length, and that reload interacts with the immediate length clock. The same principle applies to the wave and noise channels, with their respective length capacities. Tests must check register writes at the relevant phase rather than relying only on a long-running tone.

## Envelopes

CH1, CH2 and CH4 have four-bit volume and programmable direction/period. Triggers reload their initial volume and envelope timing. A zero period suppresses ordinary automatic volume changes; it does not make later envelope-register writes inert.

The color pulse envelopes retain rising/falling clock state, endpoint locking and a pending lock. A register write between clock edges can change volume or direction without undoing the endpoint already sampled by the clock. Color double-speed processing also retains a possible two-base-clock delay. These facts live in the timing structures and `envelope_delay`; reconstructing them from the current volume loses information.

CH4 and the monochrome pulse paths still use simpler envelope counters. Their implementation should not be described as complete coverage of every live-write or phantom-trigger case. In particular, digital table acceptance for the color pulse paths does not establish all noise-envelope or monochrome behavior.

## Wave channel

CH3 reads 16 bytes of wave RAM as 32 four-bit samples. NR32 selects mute, full level, half level or quarter level. Each ordinary sample interval is `2 * (2048 - frequency)` base T-cycles.

A trigger starts a fetch sequence with startup delay. `ch3_sample_byte` retains the full byte read by each active sample fetch, including the second nibble of a byte. `ch3_sample` retains the selected four-bit sample. A trigger with an enabled DAC selects the retained byte's high nibble. Writing wave RAM between a fetch and a trigger does not replace that held byte. On the color path, an active frequency change can wait for the next fetch before replacing the active period source.

An active trigger in the final two clocks before a fetch refreshes the retained byte from wave RAM byte zero after any retrigger corruption, before newly enabled length can expire the channel. The following length reload and startup delay keep their existing order. Version 20 introduced the fetched byte alongside SRAM geometry; version 21 preserves those fields. The version 19 wave-history layout also retains it. Versions 2 through 18 and the version 19 SRAM layout duplicate the saved sample nibble because the missing half of the fetch is unknown. See [wave fetch history validation](validation-wave-history.md).

While CH3 is active, CPU wave RAM accesses alias the current fetched byte. Monochrome hardware additionally restricts access to the modeled fetch window; a blocked read returns `0xff` and a blocked write does nothing. Color hardware uses its own access path without that monochrome window restriction. When the channel is inactive, the addressed wave RAM byte is available normally.

An active NR30 disconnect in the final two base clocks before a fetch refreshes the retained byte from wave RAM using the executing PC's low four bits. On monochrome hardware, a disconnect just after a fetch instead uses NR30's low address bits. These changes preserve wave RAM and the already selected raw sample. Repeating the disconnect after the channel becomes inactive does not replace the byte again.

Monochrome retriggering near a fetch can corrupt wave RAM. The current implementation has a bounded corruption model, but broader wave-bus timing and model/revision differences remain incomplete. The [combined validation record](validation-storage-wave.md) covers the qualified NR30 boundaries. Inactive-channel fetch behavior and other possible latch writers remain open; these checks do not establish every retrigger or access-conflict case.

## Noise channel

CH4 uses a retained 15-bit LFSR. The implementation stores its complemented representation; its normal feedback is XOR, and a zero in stored bit 0 corresponds to a high logical digital output. Seven-bit mode changes the feedback destination without truncating the retained upper stages. Those stages matter when software changes width.

`CupidGbNoiseTiming` retains a divider independently of the nominal noise period. Its prescaler reload is two divided-clock ticks for divisor code zero, otherwise four times the divisor code. A selected rising bit of the retained counter clocks the LFSR. Ordinary counter increments mask to 14 bits, so selectors 14 and 15 do not produce ordinary divider edges. Monochrome hardware captures feedback on the preceding falling edge and shifts that latch on the rise. Color hardware computes feedback at the shift.

Trigger delay depends on power-relative phase, divisor encoding and previous channel/gate activity. Equivalent nominal frequencies can therefore have different initial delays. Retriggering retains divider history, resets the LFSR's trigger state and forces the held output low. The held output can differ from the LFSR's current low bit, so PCM reads and mixing must use the latch.

Foreground and background counting gates are separate. DAC disable can leave a background counter running, while re-enabling a DAC alone does not trigger the channel. A DAC-disable edge can also increment the held counter without the ordinary 14-bit mask. Repeated start/stop transitions can retain carries across the full 16-bit storage range. Do not reject such snapshots solely because the counter exceeds `0x3fff`.

NR43 writes can change a selected bit from low to high and clock the LFSR without elapsed guest time. Changing the divisor exactly on a prescaler reload edge also differs from changing it between edges. `reload_edge`, counter history and the held sample preserve these distinctions. Monochrome selector transitions also update the feedback latch. The older `ch4_timer` remains in the legacy state prefix as a nominal period; it is not the running prescaler count.

High-selector NR43 transitions can generate an intermediate selector pulse and shift only part of the LFSR. The result depends on the old/new selector lines, retained width, and pre-shift history. The deterministic color profile models these transitions separately from ordinary counter edges. Paired digital observations cover the implemented CGB-E profile; they do not qualify instance-dependent silicon variation or other revisions.

## Digital taps, mixing and sample generation

FF76 exposes CH1 in its low nibble and CH2 in its high nibble. FF77 similarly exposes CH3 and CH4. These are digital samples before routing, master gain, filtering and host volume. They remain readable on CGB hardware in monochrome compatibility mode; other hardware returns `0xff`.

NR51 routes each DAC independently to the left and right mixer. NR50's two three-bit gain fields select factors one through eight, so a field value of zero is the lowest gain rather than mute. The current mixer does not provide an external VIN signal. Qt mute and volume act after core mixing and do not change PCM register reads or guest registers.

Each channel schedules a band-limited step when its digital code, routing or gain changes. The output subsystem uses an independently generated constant bank with 256 fractional phases and 64 taps. Each channel retains its pending steps and running voltage separately, so an oscillator edge remains positioned within its output frame. The digital channel clocks stay in the hardware APU; reconstruction, DAC charge and host filtering live in smaller audio modules.

Rational delivery credit and reconstruction phase have separate roles. Delivered base clocks add sample-rate credit; pending digital work is executed at the required register, divider and timing boundaries. Reconstruction tracks fractional positions in Q4.28 units. A frozen CPU interval can add output credit before a sample becomes visible. Draining the queue never executes pending hardware or forces a render.

The cached phase increment rounds down so it cannot gain time against rational sample delivery. The render boundary clamps a small negative remainder to zero. Handheld APU output uses this rule on every physical model, including SGB's distinct oscillator. It keeps delivery counts unchanged and prevents uncommon SGB rates from accumulating positive phase over a long session. Startup batches use the separate per-presentation count described below.

The default rate is 44,100 stereo frames per second. `cupid_gb_apu_set_sample_rate` accepts 8,000 through 192,000 Hz. Passing zero is invalid; zero in `apu.output.sample_rate` is the internal default-rate sentinel. A different rate is rejected without mutation while startup batches remain queued. Drain the stream and retry; selecting the existing rate remains allowed. An accepted change finishes previously delivered hardware clocks at their old rate, then updates the reconstruction increment, maximum render span and filter coefficient. It retains ordinary queued PCM, channel reconstruction, capacitor history and normalized startup synthesis history.

DAC charge changes gradually. Monochrome models also delay gain and connection changes by 32 output frames. Reconnecting the first monochrome DAC restores its full charge against the held capacitor. The color path remains connected when its DACs are disabled. These are modeled output behaviors; they do not establish an analog-circuit match for every hardware revision.

Accurate high-pass filtering is the default. Settings also offers None (Keep DC Offset) and Preserve Waveform. The filter retains binary64 capacitor history and applies explicit signed conversion at the modeled mixing boundaries. Selecting None changes policy immediately; its next connected output frame clears the capacitor. A fully disconnected monochrome path holds that history. Preserve Waveform estimates the steady DC contribution separately from audible routing. Per-channel and final mixed values use defined signed narrowing; optional interference is added afterward with clipping.

Interference Volume ranges from 0% through 100%. Its model uses CPU halt, LCD state, sound power and color infrared output, plus a bounded pseudorandom component. The independent per-instance random stream is serialized for repeatable native continuation. Zero volume leaves both the random stream and interference history untouched. This option models audible bias; it is not a measurement of a particular console's electrical noise.

Audio > Audio Channels toggles the two square channels, wave and noise with Alt+1 through Alt+4 by default. These shortcuts can be reassigned. Channel muting acts before reconstruction and recording without changing guest registers or digital PCM taps. Global listening mute and volume remain host controls after core audio generation.

The ordinary buffer holds 2,048 interleaved stereo frames. Startup has two additional batches of at most 3,214 mono frames each. A retained prefix count places older ordinary samples before those batches; later ordinary output follows them. `cupid_gb_apu_pending` reports the ordinary and startup counts separately. `cupid_gb_apu_drain_chunk` returns one origin per call, either `CUPID_GB_AUDIO_ORIGIN_HANDHELD` or `CUPID_GB_AUDIO_ORIGIN_SGB_STARTUP`. The existing `cupid_gb_apu_drain` combines successive chunks up to the caller's limit. Both return stereo frame counts; allocate two `int16_t` values per frame.

Drain the complete stream whenever startup output is pending before another step, when ordinary occupancy reaches the 1,536-frame watermark, and on every presentation, stop or execution-budget exit. Draining advances neither clock nor synthesis. If a caller fails to drain, ordinary overflow drops new samples while hardware and output history continue. Startup overflow also advances synthesis, preserves older queued output and increments its saturating `dropped_frames` count. A new startup batch cannot be inserted behind an older batch's ordinary suffix; that case follows the same drop rule. These bounded queues require a cooperating host.

## Adapter startup audio

Physical SGB or SGB2 selection controls startup, including cartridges without enhancement support. Executed-firmware entry starts at phase -10. Phases -10 through -1 emit silence, phases 0 through 199 emit the original synthesized introduction, and phase 200 emits no intro batch. CPU execution resumes at phase 96 while the introduction continues. Ordinary DAC and reconstruction history then advance, but handheld filtering, interference and PCM delivery remain suppressed until startup completes.

Every actual presentation materializes audio for the old phase before incrementing the phase seen by the callback. Each batch has `floor(rate * 70224 / base_clock)` samples, with no fractional carry between batches. At 192 kHz this is 3,138 samples on SGB and 3,214 on SGB2. Batches above 2,048 samples synthesize every second sample and repeat the intervening value; an odd final sample does not add another output frame. Partial drains retain the exact unread samples. The phase-199 batch precedes any handheld output produced after phase 200, without an extra forced APU flush.

The intro uses independent normalized voice phases and a deterministic private noise stream. Channel mute, NR50/NR51 routing, handheld filtering and interference do not change it. Host volume, mute and playback conversion apply to the combined stream. WAVE recording accepts handheld-origin chunks only, excluding both intro sound and its initial silence. Startup reset clears its synthesis and queued batches; exact and live state restoration have the distinct ownership rules below. [SGB startup](sgb-startup.md) documents phases, reset behavior and pending sound qualification.

## Host playback and recording

Qt requests stereo signed 16-bit playback at the default core rate, then uses the selected device's preferred format when necessary. `AudioConverter` performs linear interpolation with persistent fractional position and boundary samples. It averages stereo for a mono sink and fills extra output channels with zero. The final source sample can remain pending until its right neighbor arrives in the next block.

The host queue primes with generated audio before starting the sink. Its current target is 30 ms, below the 50 ms execution-backpressure threshold. When the host cannot accept more audio, normal playback waits before advancing more guest work. It does not skip guest cycles or discard core samples to reach a presentation rate. Device fallback, idle events and queue metrics need separate runtime validation from core channel timing.

At normal speed, the host converter measures sink consumption against the host performance clock. It combines the count of frames accepted by the backend with the change in the sink's free capacity so filling a buffer does not count as playback. A smoothed estimate adjusts host resampling. A small queue correction maintains about 45 ms of combined playback lead when the backend reports its capacity. A brief underrun restarts the measurement window while retaining a recent estimate for the same sink. Playback-mode and device changes reset calibration. WAVE recording continues to receive PCM before host conversion.

Pause, successful state load, playback-mode transitions and other timeline discontinuities clear stale host audio, reset conversion and re-prime playback. Failed state loads preserve the live session. Output-device changes replace the sink while preserving the guest hardware. The discarded sink is reset without requesting a drain of obsolete queued audio. Focus loss releases keyboard modes through the same transition path, including when the backend dispatches that event during a write.

Turbo generates and drains core samples, converts the combined playback stream into the negotiated host format, then selects periodic segments for playback. At host rate `F`, the window has `floor(F / 8)` frame positions. Its phase advances before selection and wraps to zero. A cap represented by `q` quarter-speed units retains positions through `floor(4 * floor(F / 8) / q)`; uncapped Turbo uses `floor(F / 16)`. Both bounds include position zero. Selection always copies complete host frames. It does not alter the core PCM or the recorder's eligible handheld input.

The window phase survives block boundaries, mode/cap changes and sink resets. Normal playback bypasses selection without advancing that phase. A changed host sample rate scales the phase proportionally into the new window; an unchanged rate leaves it intact. Capacity drops still advance the window, so a full queue cannot shift the next segment.

Turbo bounds pending software-queue data at `floor(F / 8) + 1` frames. The native sink separately requests 50 ms of buffering. This software bound is not a total device-latency guarantee, and backend queues can produce different drop traces. Normal playback retains its 50 ms backpressure policy and does not use Turbo's sample selection or capacity drops.

Rewind restores and consumes history, then runs the restored core forward for one replay interval. Live restoration retains ordinary and startup output history together, rebases the ordinary reconstruction fraction, and discards stale queued playback. Only samples generated by the replay enter the host stream. Positive presentation-time progress during replay determines the next host deadline, including a startup hold with no guest-cycle progress. Current input applies to replay. Disk loads and debugger Backstep use the same live-audio ownership rules.

Slow-motion playback changes the converter's effective input rate. This host resampling does not establish waveform equivalence to changing core sample timing. WAVE recording receives forward handheld PCM before that conversion and before host mute or volume. It records signed 16-bit, two-channel, little-endian RIFF/WAVE at the core rate. Startup chunks, pauses and rewind replay add no recorded audio, although startup and replay are audible through playback. Forward debugger execution can append eligible handheld samples while paused playback remains silent.

Loading or rewinding does not roll back bytes already written to a recording. Later forward execution appends its output. Reset, Quick Reset, Reload and native console resets retain the active recording at its existing rate. Cartridge replacement, close and an explicit core sample-rate change end the file. The recorder bounds RIFF's 32-bit size fields, reports write failures and commits through `QSaveFile`. See [the user guide](user-guide.md) for recording controls.

Native reset restarts channel and digital execution state while retaining ordinary output history: queued handheld PCM, partial reconstruction, filter and DAC history, rate, and listening policy. Startup synthesis, batches and drop diagnostics are reset separately. Physical SGB/SGB2 firmware entry starts phase -10; a post-boot approximation starts completed at phase 200. Qt clears stale playback and resets pacing after success. When ordinary Reset changes the model clock, it updates oscillator-derived output coefficients without changing the recording rate or discarding ordinary stream history. Initial cartridge loading and firmware startup remain separate lifecycle operations.

## Snapshots and continuation

Version 14 native states encode channel registers/counters, held outputs, divided-clock phases, pulse/envelope/sweep/noise timing, pending digital clocks, queued PCM, reconstruction rings, DAC delays, filter history and audio policy. The format uses explicit portable fields, including little-endian binary64 values. It rejects nonfinite values, invalid cursors and inconsistent timing credit before replacing the receiving state. Device handles, sink queues, conversion positions and recording files remain host resources.

The ordinary core load APIs restore exact recorded audio history for deterministic continuation. The `_live` variants first finish the saved APU's deferred digital clocks on the candidate, using that snapshot's own output credit. They discard the candidate's queued audio, retain the receiving output subsystem and reset its reconstruction fraction. This allows a low-rate snapshot to load into a higher-rate listening session without losing guest work or requiring extra credit from the receiver. Qt uses these variants for disk loading, rewind and Backstep, so loading an older slot cannot silently change current listening choices. A rejected load preserves the entire receiving instance in either mode.

Version 21 adds startup phase, host-only elapsed time, synthesis history and unread queued batches on physical SGB/SGB2. Exact imports restore them with ordinary output. Live imports decode startup before finishing saved pending APU work, then retain both receiving output owners together while restoring the saved hardware phase and time. A retained batch can therefore describe another phase than the restored hardware. Versions 2 through 20 supply completed phase 200, zero host-only time and canonical empty startup history for exact imports; live imports still retain receiving output. The [version 21 codec](save-states.md#adapter-startup-in-version-21) defines bounds and byte order.

Early version 14 snapshots may contain a nearest-rounded rate cache. Loading recognizes only the exact earlier cache and its matching render span, then derives the bounded increment while retaining recorded history. Arbitrary cache values still fail validation. Current snapshots retain byte-exact output continuation through the ordinary load APIs.

Versions 2 through 13 retain their queued PCM, sample-rate selection, rational credit and disabled-filter flag. Import validates retired integer mixer/filter fields, then initializes the missing reconstruction and DAC history from a deterministic settled-channel approximation. Older formats cannot reproduce a waveform history they never recorded. Version 2 also lacks newer power/trigger/edge history, whose hardware defaults remain explicit.

Version 11 retains monochrome pulse clock gates, held duty bits, trigger suppression and two disconnected-DAC codes. Versions 2 through 10 derive the missing monochrome latches from saved channel, DAC, timer and duty state; missing DAC codes start at zero on every model. The loader cannot recover a register-write sequence that those builds never recorded. Current-format continuation tests include active, suppressed, length-expired and disconnected pulse channels.

Legacy output import derives the filter and reconstruction coefficients from the receiving hardware model and saved rate, including the SGB clock. Query the state size and use the codec; do not hard-code structure offsets or infer state length from this guide. The complete compatibility and host-reset contract is in [save states and rewind](save-states.md).

## Validation and remaining work

The APU regression target covers DIV clock/reset behavior, power transitions, length control, DAC transfer and routing, duty phase, PCM taps, pitch, reconstruction, filters, rates, partial drains, STOP behavior and physical model clocks. Color-specific cases cover pulse reload windows, envelope edges, sweep latency, noise prescaler alignment, retriggers, selector writes and the held-counter carry. The [wave-memory regression](validation-wave-retrigger.md) checks all sixteen source and destination bytes around CH3 retriggers, including the Pocket exception, SGB2 behavior, guest stores and native continuation. Separate synthesis, filter and audio-state tests cover output arithmetic and restoration. Frontend converter/recorder tests and Qt workflows exercise the host path.

With the relevant targets built, run:

```sh
ctest --test-dir build --output-on-failure -R 'cupidgb_(apu|wave_retrigger|wave_history|wave_history_state|audio_synthesis|audio_filter|audio_state|audio_clock|sgb_startup|sgb_startup_audio|sgb_startup_state|state|audio_converter|audio_buffer|turbo_audio_window|wave_recorder|qt_workflows|qt_sgb_workflows)$'
```

Qt-dependent targets require a frontend-enabled build. The ordinary offscreen workflow test does not prove that a real audio device played sound. Native audio smoke checks, device changes and sustained gameplay belong in the platform validation described in [testing](testing.md).

Set `CUPIDGB_AUDIO_TRACE=1` for a native run with `--run-seconds` and `--report` to record the host audio queue, playback state, clock ratios and pacing lead. The `audio_clock_trace` section stores pump samples at up to 100 Hz and records each sink state change. State changes carry the last pump's queue readings. The trace caps at 16,384 rows and counts any samples beyond that cap. CupidGB writes the trace with the final report so it can collect samples without writing to disk during play.

Authored digital result tables establish particular register/timing observations for the modeled CGB profile. Wider non-CGB quirks, unaligned and other-revision pulse-trigger cases, other-revision noise transitions and broader APU acceptance remain open. The [SGB startup checkpoint](validation-sgb-startup.md) records complete Windows/Linux/sanitizer gates, paired hold timing, output-origin and recording checks, and bounded Windows playback. Its native Linux audio results and sustained-speed limit are reported separately. Original startup timbre remains unqualified. Report digital taps, generated PCM, native playback and listening results separately; none alone establishes all-game audio quality or complete analog behavior.

Uncommon SGB rates require sustained phase checks as well as short waveform comparisons. The earlier nearest-rounded increment made reachable snapshots invalid after about 407.43 guest seconds at 8 kHz and 344.91 seconds at 192 kHz. The bounded increment addresses that positive drift. These output-rate cases are explicit comparison exceptions; short default-rate waveform agreement does not establish identical samples at every rate or complete analog accuracy.

The current color pulse model retains the old timer and frequency through a trigger. A qualifying old counter phase can immediately advance duty, while a suppressed retrigger at the maximum frequency adds four base dots. Inactive channels retain reload qualification for later frequency writes. A fresh trigger clears the previous duty-cancellation opportunity without inventing a new persistent latch.

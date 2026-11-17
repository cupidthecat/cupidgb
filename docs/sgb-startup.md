# SGB startup

CupidGB models adapter startup separately from cartridge enhancement commands. A physical SGB or SGB2 runs its introduction when entering an installed boot binary, including with a cartridge that does not request SGB enhancements. The approximate post-boot path starts with the introduction complete. Hiding the border changes presentation only; it does not disable the physical host or its startup timing.

This guide describes the implemented core, output and Qt contracts. The [startup validation record](validation-sgb-startup.md) separates build gates, paired clock observations, native workflows and bounded runtime measurements. Original artwork and sound remain distinct from timing and output-ownership evidence. See [Validation status](#validation-status).

## Phases and clocks

The startup phase is a signed value from -10 through 200. Its value before an actual presentation determines the audio batch generated at that presentation:

| Phase | CPU execution | Startup output |
| --- | --- | --- |
| -10 through -1 | Held | Silent mono batch |
| 0 through 95 | Held | Introduction synthesis |
| 96 through 199 | Runs normally | Introduction synthesis; ordinary handheld output remains suppressed |
| 200 | Runs normally | Introduction complete; any already queued startup samples drain before later handheld samples |

`cupid_gb_sgb_host_present` reports the physical model. `cupid_gb_sgb_startup_active` reports the interval before phase 200, and `cupid_gb_sgb_startup_held` reports the interval before phase 96. Cartridge enhancement flags do not change these queries.

During the hold, a successful core step advances 114 base clocks of host-only time and advances display delivery. It executes no CPU instruction and supplies no DIV, timer, APU, DMA, serial, input or RTC clocks. Pixel/display delivery continues in its own domain. The observed CPU event remains idle. A held step therefore makes positive presentation progress without claiming that a guest instruction completed.

The existing `elapsed_cycles` remains guest base-clock time. `sgb_startup.elapsed_cycles` retains the additional host-only time, and `cupid_gb_presentation_cycles` returns their sum. Hosts use presentation time for pacing, frame budgets and rewind age. Input traces, instruction accounting and guest throughput continue to use guest time. Both counters can move backward on reset or state load, so a host must rebase its timeline after those operations.

The existing `display_cycles_since_frame` is the authoritative presentation age. Actual complete or incomplete presentation events advance startup. A redraw advances neither that age nor the startup phase. The older adapter-transfer age remains separate; a restored state need not make the two ages equal. A valid loaded display state can publish more than once within one hardware step, which is why output storage permits two introduction batches.

## Presentation order and artwork

The common presentation path performs these operations in order:

1. Generate audio for the old startup phase using the effective output rate and physical model clock.
2. Advance the startup phase by one, stopping at the completed state.
3. Apply adapter transfer, border and frozen-view work for the presentation.
4. Set the frame-ready latch and notify the presentation observer.

An observer sees the updated phase and its corresponding picture. It runs within the enclosing hardware step and must not reenter the core or perform host audio/device work. Snapshot capture and playback delivery belong after that step returns.

At the 199 to 200 transition, the final introduction batch is queued before ordinary output can resume. Later handheld PCM follows it in the same ordered stream. The transition does not force pending APU clocks to run early. Ordinary channel and reconstruction work continues through the established scheduler; suppression prevents its samples from entering the ordinary filter/interference and output queue while startup is active.

The startup renderer uses original CupidGB letter and heart artwork. It redraws the 160 by 144 viewport from the current phase without changing clocks, synthesis, queues, transfer state or callbacks. The existing adapter border compositor retains its transparency and priority rules over that viewport. During startup, cartridge display masks do not replace the introduction, and the ordinary border-transition fade is suppressed. Repainting a paused window or changing its size therefore cannot replay the introduction or generate more sound.

The artwork is a branding choice. Its implementation is not evidence of image identity or complete visual parity.

## Audio generation and draining

Startup synthesis has its own normalized voice phases, sweep/filter history and private noise generator. Its seed derives from the current power-on generation with a separate domain tag. It consumes neither power-on RAM noise nor handheld interference noise. The voices and waveform construction are original CupidGB code; exact timbre remains unqualified.

Each presentation generates `floor(rate * 70224 / physical_model_clock)` mono samples. This uses a fresh integer count for each presentation, without carrying a fractional remainder into the next batch. The model clocks are 4,295,454 Hz for SGB and 4,194,304 Hz for SGB2. Supported rates remain 8,000 through 192,000 Hz. For example:

| Output rate | SGB samples per batch | SGB2 samples per batch |
| --- | --- | --- |
| 8,000 Hz | 130 | 133 |
| 44,100 Hz | 720 | 738 |
| 48,000 Hz | 784 | 803 |
| 96,000 Hz | 1,569 | 1,607 |
| 192,000 Hz | 3,138 | 3,214 |

When a batch exceeds 2,048 frames, synthesis advances once per pair of output samples and repeats that value for the second sample. An odd final frame advances only once and does not produce an extra sample. Once materialized, the batch retains its samples and rate until consumed. Partial drains never regenerate it.

The core retains the ordinary 2,048-frame stereo queue and two separate startup batches of at most 3,214 mono samples each. `normal_prefix_frames` identifies ordinary samples queued before the introduction batches. Delivery order is that ordinary prefix, all startup batches, then any later ordinary suffix. Each startup mono sample becomes identical left and right output values during delivery.

`cupid_gb_apu_pending` reports unread handheld and startup frames separately. `cupid_gb_apu_drain_chunk` returns one contiguous origin at a time, up to the caller's capacity, and tags it as `CUPID_GB_AUDIO_ORIGIN_HANDHELD` or `CUPID_GB_AUDIO_ORIGIN_SGB_STARTUP`. The return value counts stereo frames. The existing `cupid_gb_apu_drain` remains available and combines consecutive chunks up to its capacity, without exposing origin tags. Neither drain API advances hardware time, presentation time or synthesis history.

Cooperating hosts drain before another step whenever startup output is pending or the ordinary queue reaches the 1,536-frame watermark. They also drain after presentation and on every stop or budget exit. This leaves room for output produced within one legal hardware step. Qt collects the complete ordered stream in a bounded 16,384-frame interval buffer and retains its origin spans for recording. A collection failure pauses execution with an error instead of silently truncating the interval.

If a caller leaves both startup slots occupied, generation still advances its synthesis history and clocks, but drops the new batch and adds its frame count to the saturating `dropped_frames` diagnostic. The same rule applies if ordinary suffix samples already follow pending startup batches, because inserting another introduction batch there would reorder output. Older unread samples are never overwritten. The ordinary queue retains its existing full-queue behavior. Hosts must drain promptly; bounded storage does not make arbitrary deferred consumption lossless.

A request to change the core sample rate is rejected while any startup batch remains queued. Rejection happens before pending APU clocks or either output owner changes. Requesting the existing rate is allowed. Drain the complete ordered stream, then retry a different rate. Accepted changes preserve normalized synthesis history; Qt's device-format conversion remains a separate host concern.

Handheld channel mutes, filter selection and interference level apply to handheld output. They do not alter startup synthesis. Host playback mute and volume apply to the combined stream. WAVE recording accepts only handheld-origin spans: both the introduction and its silent lead-in play through the host output but are omitted from the recording, without replacement silence. Rewind replay also remains excluded from forward-only recording. See [Audio](audio.md).

## Reset and reentry

Cold initialization, Hard Reset and Quick Reset restart the introduction at phase -10 when they enter an installed adapter boot binary. A successful explicit boot-entry request does the same. Without executed firmware, approximate post-boot setup uses completed phase 200. Non-adapter models always use the completed state.

Each startup reset clears host-only elapsed time, synthesis history, queued batches, prefix count and dropped-frame diagnostics. The new private noise stream comes from the reset generation. Native resets retain the ordinary reconstruction/filter owner and the user's listening policy under the existing reset contract. Retaining that ordinary owner does not retain the old introduction. Model changes also clear startup output.

Qt clears queued device playback and rebases pacing at timeline changes. Quick Reset and Reload can retain compatible rewind history under the existing cartridge/model/firmware rules. The new timeline uses its own presentation baseline, so resetting the core counter cannot add an unsigned wraparound to history age. These reset policies do not turn startup audio into recorded handheld audio.

## Native states and debugger behavior

Version 21 writes an explicit portable startup extension on physical SGB and SGB2, after the version 20 CH3/SRAM fields and before the accessory descriptor. It records hardware phase and host-only time, synthesis history, diagnostics, queue descriptors and only unread mono samples. All widths and signed encodings are explicit; no native structure layout or pointers are written. Non-adapter models add no startup bytes. The extension ranges from 65 to 12,941 bytes, so hosts query state size for each capture. See the [version 21 layout](save-states.md#adapter-startup-in-version-21).

Exact loading restores the saved hardware and both saved output owners. Live loading restores hardware phase and host-only time but retains the receiving ordinary and startup output owners together. It decodes saved startup state before finishing the saved candidate's pending APU clocks, then installs the receiving output history and rebases the ordinary fraction. The host discards the complete stale output stream before resuming playback.

Queued generation phases are deliberately independent of the restored hardware phase after a live load. A completed hardware introduction can still have unread phase-199 audio. Both batch cursors are independently validated; neither must be at its beginning. Legacy versions 2 through 20 restore phase 200 and zero host-only time. Exact legacy imports use an empty startup owner with canonical noise seed one; live imports retain the receiving owner. Loading a file does not itself restart the introduction.

Capturing after a completed held step is valid. Qt does not add automatic frame-rewind entries for an interval made entirely of held startup calls, but explicit state capture remains available. Once guest execution resumes, ordinary frame capture continues and history age includes positive presentation progress. Debugger instruction history likewise excludes held calls.

While startup holds the CPU, debugger execution-breakpoint checks, pending breakpoint skips, instruction quotas and call-stack history wait for a guest instruction. Held calls still count against finite hardware-call and UI slice budgets. Their output is drained normally. The first eligible guest fetch after phase 96 honors its breakpoint and stepping rules. There is no successful zero-time output-service step; draining is a separate operation.

## Validation status

The complete version 21 integration gate passed 92 Windows, 90 Linux and 65 sanitizer checks. Each Release gate includes the desktop, storage and SGB Qt workflow groups. Startup regressions cover the hold/release boundary, rendering without mutation, presentation order and reset behavior. Audio regressions cover per-model sample counts, partial and tagged drains, queue overflow accounting, rate rejection, suppression and output ownership. State regressions cover explicit encoding, exact/live ownership, malformed inputs and legacy migration.

Twelve paired SGB/SGB2 startup cases matched the held interval and first guest instruction; three DMG controls had no hold. Separate Windows checks cover four 120-second commercial sessions using post-boot setup and a 12-second synthetic SGB2 firmware run. Their model, duration, clocks, audio scope and limitations are recorded in the [startup checkpoint](validation-sgb-startup.md). These observations do not establish commercial firmware compatibility, complete startup parity, image identity or acoustic fidelity. Linux sustained-speed qualification remains blocked by the recorded host-clock disagreement.

The implementation is split across [startup timing](../src/gb/sgb_startup.c), [original rendering](../src/gb/sgb_startup_render.c), [startup synthesis](../src/gb/sgb_startup_audio.c), [ordinary audio output](../src/gb/audio_output.c), [the portable startup codec](../src/gb/state_startup.c) and [Qt's tagged audio collector](../src/frontend/qt/core_audio_collector.cpp). Focused regressions live in [core startup tests](../tests/test_sgb_startup.c), [startup audio tests](../tests/test_sgb_startup_audio.c) and [startup state tests](../tests/test_sgb_startup_state.c).

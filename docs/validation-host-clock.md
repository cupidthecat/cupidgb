# Desktop pacing clock

This checkpoint addresses a measured disagreement between the Linux clock used by the desktop and an independent Windows host timer. The guest hardware clock and instruction timing are unchanged. The comparison revision remains `ea667ad79aa28409f034b1fd6da7304e1844750d`.

## Measured clock discrepancy

A private parent/child probe started each measurement after the Linux child signaled readiness. The Windows parent measured from sending the request through receiving the child's result. Each child interval requested a 20-second sleep. The host interval includes that exchange overhead.

| Sample | Linux monotonic seconds | Linux raw seconds | Windows host seconds |
| --- | ---: | ---: | ---: |
| 1 | 20.000096 | 21.162040 | 21.162495 |
| 2 | 20.000072 | 21.369955 | 21.370331 |
| 3 | 20.000105 | 20.732687 | 20.733125 |

The raw clock agreed with the Windows performance counter to within half a millisecond in these intervals. The adjusted monotonic clock undercounted them. This establishes a clock-rate discrepancy on the tested WSL host; it does not identify the kernel or virtualization cause. The probe measures clocks, not emulation throughput.

Earlier Linux application reports used the adjusted clock for both pacing and elapsed time. Their near-100% guest ratios therefore do not establish correct speed against the Windows host. Those reports retain their original executable identities and are not relabeled as measurements of this change.

Linux documents that `CLOCK_MONOTONIC` can receive frequency adjustments, while `CLOCK_MONOTONIC_RAW` excludes those adjustments. Both exclude system suspend time. See the [Linux clock interface documentation](https://man7.org/linux/man-pages/man2/clock_gettime.2.html).

## Implementation and checks

The desktop uses a shared elapsed-time interface for guest pacing, runtime reports, paint intervals and rewind-capture timing. Linux reads `CLOCK_MONOTONIC_RAW`; Windows retains the Qt elapsed timer. Reports identify the selected source. Bounded runs and warmup samples check deadlines in the same host-clock domain instead of assuming a Qt timer delay is an equivalent duration.

The targeted clock regression encloses the start and elapsed-time calls with independent platform samples. It checks initial validity, repeated starts and elapsed duration after sleeping, with only a microsecond allowed for clock-conversion rounding. Scheduling delay widens the measured bounds rather than introducing a fixed timing tolerance.

The integrated Release builds pass 75/75 tests on Windows and 73/73 on Linux. The Linux AddressSanitizer/UndefinedBehaviorSanitizer build passes 53/53 core tests. These gates include the platform clock check, finite command-line duration checks and the post-warmup report. Sustained playback and throughput measurements follow below. The Linux sustained run fails its speed requirement, despite the passing build and workflow tests.

## Native audio readiness

A separate native workflow failure occurred after the Linux audio service was available. Its test had stopped the guest timer and supplied 1,654 stereo host frames at 44,100 Hz. The first readiness snapshot already recorded 26,779 microseconds processed and writable capacity. The test then waited for another capacity increase without supplying the 5,192 bytes still queued in the application. The device drained the supplied 37,505 microseconds of audio and correctly reported an underrun.

The corrected check accepts consumption already visible in its initial snapshot. It still requires complete accepted host frames, a valid device, positive processed duration, writable capacity, active playback and no audio error. If consumption has not yet occurred, the original bounded wait remains. This changes the test's readiness observation; it does not increase playback buffers or relax the runtime audio assertions. The [Qt audio interface](https://doc.qt.io/qt-6.8/qaudiosink.html) defines the processed-duration and writable-capacity observations used here.

Native workflow reruns pass on Windows and Linux with real audio output and 16 captures each. The Linux run waited for a stable WSLg audio endpoint before starting; its endpoint identity remained unchanged throughout the run. Debugger and settings captures were inspected on both platforms. These checks establish the tested workflows and device readiness, not audible quality or physical device-disconnect behavior.

| Native executable | SHA-256 |
| --- | --- |
| Windows Release | `aa4094a24b546e95ce36ebe119a2c2fe383c6f4ce32b25235a980ccfd520c3d3` |
| Linux Release | `870266137f136e87de631e3c690583b3753732541954b18aa3656e69b0040b06` |

## Sustained native playback

Four Windows runs each cover 120 seconds measured inside the application, with
a five-second warmup, fixed zero power-on seed, scripted input, normal audio and
rewind capture. All four finish with an active audio sink, no audio errors or
post-warmup idle events, every scheduled input applied, saved captures and a
successful battery flush. Inputs and executables retain their hashes.

| Windows workload | Guest speed after warmup | Paint interval p95 | Peak resident memory |
| --- | ---: | ---: | ---: |
| DMG ROM-only | 100.0057% | 18.651 ms | 111.0 MiB |
| CGB MBC5 | 100.0000% | 18.821 ms | 216.4 MiB |
| SGB MBC1 | 100.0015% | 18.496 ms | 215.0 MiB |
| CGB MBC5 motor hardware | 99.9947% | 19.165 ms | 253.2 MiB |

Guest speed counts emulated base-clock cycles. Paint intervals describe host
presentation and are a separate measurement. Whole-process speed is lower
because its interval also includes startup and shutdown. The inspected CGB
captures show gameplay. Nineteen bounded core replays separately inspect the
DMG and SGB input routes: the DMG route reaches moving pieces, game over and a
new game; the SGB route reaches play after its introduction and changes
character and object positions. Every interval contains nonzero PCM, and each
longer replay reproduces the shorter replay's PCM prefix exactly. These
observations explain the final captures without changing the input traces.
They establish game activity at the sampled points, while native pacing remains
the separate measurement above. These four cases do not establish compatibility
or performance for every game.

The first Linux run used the same DMG workload and failed: guest speed after
warmup was 93.3935%, and only 161 of 173 scheduled input events were reached.
The application measured 120.004754 seconds with the raw clock. Its native
audio device reported 111.979398 seconds processed, no errors and no idle events
after warmup. The application queue ended at 10,168 bytes, above the 8,820-byte
execution-backpressure limit for 44,100 Hz stereo signed 16-bit output. Paint
interval p95 was 28.256 ms and peak resident memory was 138.6 MiB. The remaining
three Linux cases were not started after this failure.

The failure is retained. Device readiness and an active audio state do not prove
correct playback speed. The build-input manifest remained unchanged across all
of these checks: 264 inputs, SHA-256
`619e6b0f76e768403e6a49a13fdd348a1e8a9b023301d2f920274d59443d5852`.

## Independent Linux audio diagnosis

A separate PulseAudio client sent 20 seconds of stereo PCM to the same RDP sink
and waited for playback to drain, with CupidGB closed. A Windows parent bracketed
each request with its performance counter. Both runs completed successfully,
without changing the endpoint or the client executable.

| PCM sample rate | Linux raw seconds | Windows host seconds | Linux monotonic seconds | PulseAudio clock seconds |
| --- | ---: | ---: | ---: | ---: |
| 44,100 Hz | 21.405509 | 21.407828 | 20.006668 | 20.006656 |
| 48,000 Hz | 20.501453 | 20.504226 | 20.024476 | 20.024477 |

The first stream therefore drained at approximately 93.43% of its declared
rate against independent host time. The second reached approximately 97.55%.
The clock discrepancy changed during the experiment, so these sequential runs
do not establish a sample-rate-specific effect. The second interval also
contained a wall-clock adjustment.

The installed WSLg version was 1.0.65, with PulseAudio revision
`6f045ff0dca233a939a2aba815f84d177e294122`. Its
[RDP sink implementation](https://github.com/microsoft/pulseaudio-mirror/blob/6f045ff0dca233a939a2aba815f84d177e294122/src/modules/rdp/module-rdp-sink.c)
schedules rendered samples against the PulseAudio clock. That clock selects
`CLOCK_MONOTONIC` on Linux in the corresponding
[clock implementation](https://github.com/microsoft/pulseaudio-mirror/blob/6f045ff0dca233a939a2aba815f84d177e294122/src/pulsecore/core-rtclock.c).
Qt 6.8.3 exposes the server's writable capacity through
[`QPulseAudioSink::bytesFree()`](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/multimedia/pulseaudio/qpulseaudiosink.cpp).
CupidGB's queue correctly retains samples until that capacity is available.
Its processed-duration query is telemetry and does not control guest execution.

Together, the standalone result and source inspection identify a host audio
clock constraint on this WSLg setup. They do not identify why the kernel clock
rate differs from independent host time. Correct sustained Linux playback
remains unverified until the host clock/audio path is corrected or the same
checks pass in another Linux environment. Increasing the queue would only delay
backpressure and cannot resolve a sustained rate mismatch.

## Throughput with unchanged guest work

The integrated runner was compared with preserved builds at `4b562e3` and
`546edf9`. Four local workloads run for 3,000 nominal guest intervals with normal
stereo generation. Three rounds rotate each executable through every order
position. Windows and Linux run sequentially after builds and hardware probes
finish: 36 executions per platform, 72 total. Windows uses its performance
counter; Linux uses `CLOCK_MONOTONIC_RAW`.

Every compared guest field matches across all three versions and repetitions,
including cycle counts, presentation counts, image checksums and PCM counts and
checksums. Executables, cartridge inputs, baseline receipts and the harness
retain their hashes. Positive time changes below mean a slower run.

| Host | Workload | Current median seconds | Time change from `4b562e3` | Time change from `546edf9` |
| --- | --- | ---: | ---: | ---: |
| Windows | DMG ROM-only | 11.362874 | +7.70% | +2.14% |
| Windows | CGB MBC5 | 11.413468 | -3.45% | -1.84% |
| Windows | SGB MBC1 | 8.120253 | -0.59% | +1.85% |
| Windows | CGB MBC5 motor hardware | 11.035187 | +0.92% | +2.25% |
| Linux | DMG ROM-only | 7.757755 | +0.87% | -3.46% |
| Linux | CGB MBC5 | 10.133785 | -3.55% | +5.30% |
| Linux | SGB MBC1 | 7.873491 | -2.28% | +4.64% |
| Linux | CGB MBC5 motor hardware | 8.975888 | -3.61% | +3.51% |

The memory-observation path gives GCC and Clang a hint that ordinary playback
usually has no observer. Callback order and guest accesses are unchanged. The
MSVC fallback remains fully inlined: retained Windows binaries have the same
312-byte bus-read function and instruction sequence, with field displacements
changed by the new allocation metadata. This rules out an added helper call in
that path; it does not establish a cause for the measured timing differences.

The results are mixed. Three repetitions do not isolate all host variation;
the current Windows CGB MBC5 runs alone range from 10.648586 to 14.645429 seconds.
The integrated matrix does not establish a uniform speedup or attribute a result
to the branch hint alone. These headless measurements also do not measure native
presentation pacing or audible playback quality.

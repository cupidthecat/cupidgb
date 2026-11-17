# Testing and measurements

CupidGB uses portable core regressions, Qt workflow checks, externally supplied ROM suites, and bounded gameplay measurements. Each establishes a different result. A successful build does not establish runtime correctness, and an image at one point in time does not establish complete game compatibility.

Recorded results belong in [the validation record](validation.md). Keep outstanding acceptance requirements in [the parity checklist](parity.md).

## Local regression checks

Build and run CTest as described in the [build guide](building.md):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

For a Visual Studio build, use `ctest --test-dir build -C Release --output-on-failure`. Run Windows and Linux configurations separately. Core-only checks do not cover the desktop frontend.

Hash regressions cover known vectors, span partitions, large inputs, unaligned bytes, and digest/input aliasing. Test the portable path separately with `-DCUPIDGB_ENABLE_NATIVE_HASH=OFF`; compare its native-state fixture bytes with the enabled build.

Core tests exercise lifecycle, CPU/memory behavior, serial, timer/bus phases, graphics, audio, cartridge persistence, native snapshots, older-state battery recovery, and SGB behavior. Frontend checks cover sample conversion, WAVE file output, display processing, and integrated desktop workflows as those targets are present in the current build. Query the exact list with `ctest --test-dir build -N`; avoid carrying test counts from an older revision into a new result.

CTest runs desktop, larger-SRAM and SGB startup workflows as three separate offscreen checks, each with a 30-second deadline. Select one with `--smoke-test --smoke-workflow desktop`, `storage` or `sgb`. Plain `--smoke-test` runs all three. Add `--audio-check` to the full, desktop or SGB workflow to require a real audio sink; the storage-only workflow does not qualify audio devices. Command-line selection and cartridge/archive persistence have separate test groups, each retaining its own 90-second deadline.

The `state_ram` target covers every supported TPP1 SRAM code on DMG and CGB, full-span persistence and continuation, allocation failures, and transactional rejection. Its format cases exercise current and older layouts through all four load APIs, with different receiver histories and printer attachments. The `mbc3_bus` target distinguishes driven disabled-bank reads from absent SRAM and checks subsequent bus decay. The Qt storage workflow covers battery reopen, state slots, reset/reload, rewind budgets, debugger Backstep and physical memory search on both sides of the old 128 KiB boundary. These generated checks do not establish compatibility with every commercial cartridge.

The `sgb_startup`, `sgb_startup_audio` and `sgb_startup_state` targets cover the held display clock, reset and re-entry policies, presentation ordering, pure redraw, debugger release, bounded sound batches and native continuation. The timing matrix includes SGB and SGB2 with ordinary and enhanced cartridges. Audio checks exercise partial drains, output ordering, rate changes and queue overflow; native checks distinguish exact and live ownership and retain old-format fixtures. Runtime playback, physical output and paired observations need separate evidence.

The `sgb_protocol` target sends boot-header packets through JOYP on SGB/SGB2 with ordinary and enhanced cartridge declarations. It checks checksum rejection, repeated and reordered sections, stop-pulse commit, ignored post-handoff headers, rendered palette effects, retained controller reads after rejection and both reset policies. `sgb_protocol_state` covers the version 22 cache/latch record, partial reception, exact/live continuation, rejected data and the explicit empty-cache migration from older formats. The [header checkpoint](validation-sgb-protocol.md) records the completed platform gates, paired observations and gameplay scope. These protocol checks are separate from startup artwork and sound qualification.

The CPU lockup target covers all eleven illegal primary bytes, model/speed/IME combinations, fetch timing, IE clearing, pending requests, explicit debugger release, and state continuation. It also checks CB operands, the actual fetched address during the HALT bug, peripheral progress, and OAM DMA suspension and release. Runner regressions require an explicit illegal-opcode result even when a protocol success or duration limit occurs on the same fetch. Qt workflows cover debugger inspection and Backstep as well as continued graphics/audio generation during ordinary halted playback.

The cartridge-clock targets separate policy and counter arithmetic (`rtc`), logical CPU and bus delivery (`rtc_timing`, `rtc_dma`), and native continuation (`rtc_state`). Exercise both host policies with a controlled time provider; changing the operating-system clock is unnecessary. Include stopped and invalid writable digits, rollback, phase wrap and debt, battery imports, and mode changes before cartridge binding. The state target checks all divider remainders, legacy defaults and invalid history through ordinary and trusted APIs. Qt workflow checks cover Settings cancellation and validation, cartridge replacement, state loading, rewind and debugger Backstep with the receiving policy retained.

The audio targets separate digital clocks and register effects (`apu`), transition reconstruction (`audio_synthesis`), DAC/filter response (`audio_filter`), snapshot ownership (`audio_state`), and sustained phase bounds (`audio_clock`). Continuation checks compare emitted stereo samples and the complete encoded state across models, sample rates and filter modes. Include pending clocks, monochrome noise trigger/feedback history, interference, rate changes, mixed-rate live restoration, and transactional rejection of malformed or incompatible histories. Live loading must keep the receiving output stream and listening policy; exact loading must reproduce the saved stream. The clock target checks every supported integer output rate against both model clocks, retained failure boundaries and older derived caches. Qt workflows exercise Settings cancellation and persistence, all four channel actions, disk loads, rewind and debugger Backstep.

The standalone `cupidgb_turbo_audio_window` target checks complete-frame selection, phase and block boundaries, every cap, normal bypass, host-rate changes, capacity drops and transactional rejection. It has no Qt dependency. Integrated playback workflows check history consumption and forward replay, exhaustion/release, current input, recording isolation, invalid snapshots, combined pacing, integer/float host formats, queue boundaries and focus-loss resets. These checks do not establish audible output or total device latency.

Run the generated mapper, persistence, and SGB continuation checks together:

```sh
ctest --test-dir build --output-on-failure \
    -R 'cupidgb_(gb|cartridge|rtc|rtc_timing|rtc_dma|rtc_state|divider_phase|battery|camera|mbc3_bus|mbc7|huc3|infrared|tpp1|sgb|state|state_ram|printer_state)$'
```

For a library scan, inventory cartridge type bytes separately from the selected hardware model. Give each ROM a fixed guest-clock budget and retain its exact result, including an execution error or incomplete boot. Run banking and peripheral probes for hardware absent from the library. Use separate gameplay and persistence checks before claiming compatibility for a title.

Controller checks run only in a desktop build with SDL2:

```sh
ctest --test-dir build --output-on-failure \
    -R 'cupidgb_(controller_backend|controller_workflows|qt_workflows)$'
```

The backend check attaches two identical standardized virtual controllers and one raw joystick. It verifies device identity/capabilities, axes/buttons/hats, transient raw and normalized edges, hotplug/reconnect, focus/background/master blocking, remapping, unrelated SDL events, rumble callbacks, subsystem ownership and teardown through a Qt event loop. The callback before each poll must apply current delivery gates, bound recursive polling, and permit a safe stop during polling or startup. SDL video and audio must remain uninitialized.

The workflow check uses actual native Qt controls for capture, wrong-device rejection, Cancel/OK, player/dead-zone changes, settings-file reload and detach during capture. The application smoke adds generated guest programs for immediate SGB player-count transitions in both directions, held/disabled/preferred device policy, keyboard/controller contribution merging, per-player rapid controls, selected-player reads, shortcuts, paused-tool input, state reconciliation and MBC5 feedback boundaries. Menu/settings tests process timers while checking cycle invariance and restoration of the running or paused state.

Idle-timer workflows check immediate active/idle interval selection, zero guest progress while paused or behind a menu, controller gating before a poll, rewind exhaustion/release, and preservation of a deliberately stopped timer. A transient state-operation guard must leave the active interval available after it unwinds. Timer-event measurements belong in separate native runs; interval selection alone does not establish CPU use or physical wakeups.

Virtual devices are detached before exit. These tests do not qualify a physical USB/Bluetooth device, motor force/duration, actual sensor output, real window-system focus behavior, input latency or commercial play. Run native Windows/Linux smoke and real-device gameplay separately. A WSL environment without exposed input devices cannot establish physical Linux controller support. Do not infer those results from a virtual rumble callback or an offscreen dialog.

C++/C integration checks exercise shared hardware records, initialization/reset, serial observers, and native-state restoration. Run them with Release IPO enabled as well as any fallback configuration being qualified. IPO comparisons must record the effective setting and, for GCC C++, the `-fno-strict-aliasing` flag. The build disables IPO with sanitizers.

The serial and printer targets use generated transfers to check both clock directions, interrupt timing, reply bytes, compressed packets and image output. The printer-state target compares interrupted transfers and jobs against independently continued devices through ordinary and trusted state APIs. It also covers malformed records with valid integrity hashes, both sides of the load transaction, attachment/callback retention, storage overlap, reset origins, older-format imports, and immediate capture after a retained printer is reattached to an earlier console timeline.

The infrared target checks register masks and sensor availability across cartridge headers and all six model tags, HuC1 RAM isolation, CGB compatibility-mode writes, output callback ordering, normal/double-speed clocks and clock freezes, and live host input. The `infrared_guest` target executes generated instructions around receiver transitions. The `infrared_state` target checks retained display credit, receiver continuation, older-format defaults, all four load APIs and rejection without changing the receiving instance. Use matched divider, display and frame-age origins in paired probes; stable sensing alone does not qualify transition timing.

The `pixel_timing` target covers window start edges, hidden color objects, monochrome object-fetch aborts, and VRAM/STAT ownership. `vram_bus` checks both physical banks, held-byte ownership, OAM-DMA source reads and passive peeks. `vram_quiet` checks fixed guest-byte expectations after quiet rows, repeated synchronization and a partially interrupted row. `pixel_timing_state` restores those histories mid-transfer and compares independently continued instances through exact and live, ordinary and trusted APIs. Its malformed fixtures carry valid integrity hashes, so failures must come from semantic validation. Cross-host byte and continuation comparisons remain a separate gate.

The Qt smoke sends jobs over the emulated serial registers and checks the resulting feed, PNG, and PDF output. It restores partial packets and active jobs through disk files, rewind and debugger Backstep; checks failed loads and paper already emitted before queued widget work; and checks that an old session's queued update cannot reopen the feed in a new session. The command-line workflow checks direct cartridge loading and archive selection and rejection. Set `CUPIDGB_REQUIRE_ARCHIVE_TESTS=1` to fail if 7-Zip is unavailable.

Reload workflows modify generated cartridge and symbol files, then exercise File > Reload Cartridge and `reset reload`. They check refreshed ROM/symbol content, missing and malformed companions, preserved breakpoints/model/pause state, cleared history, Unicode paths, battery persistence, and failures that leave the running session intact.

The cheat core target checks code formats, bank selection, comparisons, duplicate priority, cloning and `.cht` bytes. The search target checks physical regions, all preset conditions, byte/word data, overlapping words, type changes, and bank-boundary writes. Qt workflows exercise actual rule editing and toggles, file persistence and invalid imports, save-state continuity, blocked save destinations, search-to-rule creation, and state-file protection. Color tests hash the complete RGB555 domain for every correction mode and temperature.

Release tests explicitly keep their assertions enabled. Run the GCC/Clang sanitizer configuration as a separate check, with leak detection where available. Preserve the failing command and complete diagnostic when a sanitizer reports a defect.

## Qt workflow checks

```sh
./build/cupidgb --smoke-test
./build/cupidgb --smoke-test --audio-check
```

The first command runs all three generated workflow groups with isolated temporary settings and no real audio device. CTest registers desktop, storage and SGB startup separately, each with a 30-second deadline.

The second command requires an actual output device and exercises playback around state loading and rewind. It also dispatches focus loss, state loading and cartridge close from backend writes, checking deferred mode resets and the absence of stale guest execution. The SGB group checks startup-only playback while guest clocks remain held and WAVE recording remains empty. Its consumption check requires the same active sink, device and queue, with no re-prime or write during the observed interval. Also run native visible checks on each target platform. Offscreen checks do not establish dialogs, font/layout behavior, input focus, actual device playback, or physical display presentation under the native backend.

Generated inputs protect normal user data and allow repeatable failure cases. Commercial-game checks still need real cartridge execution, visual inspection, input, sound, and saving.

Set `CUPIDGB_UI_CAPTURE_DIR` when running the smoke check to retain private images of the generated-cartridge debugger and all five inspector tabs. The optional hook creates the supplied directory and writes fixed `debugger.png`, `memory.png`, `tiles.png`, `maps.png`, `objects.png`, and `palettes.png` files. It also writes `tiles-grid.png`, `maps-overlay.png`, `messages.png`, and `messages-startup.png` for optional overlays and startup identity. Settings workflows add `rtc-settings.png`, `audio-settings.png`, and `audio-channels.png`. Capture failures fail the check. Use the native platform to inspect actual Qt layout and fonts; a widget capture does not measure physical display presentation.

The SGB group writes `sgb/startup.png`, `redraw.png`, `cropped.png` and `complete.png`, plus `sgb-workflows.json` and generated WAVE files. Its result separates guest cycles, presentation cycles, handheld PCM and startup PCM for each physical model and cartridge-header combination. Retain those distinctions in runtime measurements. Recorded WAVE lengths must follow handheld output alone; displayed frames and startup batches do not establish guest speed.

The Windows offscreen plugin does not discover installed fonts through the native font backend. CTest gives the message glyph test a process-local `QT_QPA_FONTDIR` derived from the existing Windows Fonts directory. This keeps the glyph assertions active without copying fonts or changing the system environment. Native captures remain the check for actual desktop typography.

The separate `cupidgb_controller_workflows` executable uses the same variable to save `controllers.png` from its native dialog and generated SDL devices. Run it without an offscreen platform override when reviewing the platform's layout and fonts. This capture contains test device state and does not need a game or physical controller.

For example, from a Linux desktop session:

```sh
CUPIDGB_UI_CAPTURE_DIR=.local-research/ui ./build/cupidgb --smoke-test --audio-check
```

On Windows, also set `QT_FORCE_STDERR_LOGGING=1` when capturing diagnostic output from the GUI executable.

## Headless ROM runner

`cupidgb_rom_runner` accepts one uncompressed cartridge and explicit model, startup, verdict, and duration options:

```sh
./build/cupidgb_rom_runner --model dmg --protocol serial --frames 600 /private/test.gb
./build/cupidgb_rom_runner --model cgb --boot /private/cgb_boot.bin \
    --protocol none --frames 1800 --image .local-research/frame.ppm \
    --audio .local-research/audio.pcm /private/cartridge.gbc
```

Create the output directory first. Model names are `dmg`, `mgb`, `cgb`, `sgb`, and `sgb2`. The runner defaults to `dmg` and protocol `none`; choose the actual test's model and success protocol deliberately. `--boot` accepts one local binary. Without it, the run uses the approximate post-boot profile.

Both the runner and Qt frontend accept `--power-on-seed auto|UINT64`. Fixed values use unsigned decimal or `0x`/`0X` hexadecimal syntax, up to `18446744073709551615`; leading decimal zeros do not select octal. Signs, whitespace, malformed digits, and overflow are rejected. Zero selects a reproducible noisy generation. The default is `auto`. The Qt override lasts for the session, including reset, reload, and opening another cartridge, without changing persistent settings.

Reports contain `power_on_seed_hex`, exactly sixteen lowercase hexadecimal digits for the realized generation. Replay an automatic run by prefixing that value with `0x` in the option. Pin the build and hardware profile too: the generator's byte sequence is not a cross-version compatibility promise. Explicit firmware startup reconstructs the same generation's cold memory before executing the loaded binary. Keep the firmware hash and startup mode equivalent across comparisons.

`--frames N` limits elapsed guest time to approximately `N * 70,224` base-speed T-cycles, including LCD-off execution. It does not request N displayed images. Those intervals have different physical durations for SGB NTSC at 4,295,454 Hz and the other profiles at 4,194,304 Hz. The runner does not accept the Qt-only `auto`, `dmgabc`, or `dmg0` selections. `--max-steps N` separately limits calls to the stepping API, including held startup calls; the default is derived from the frame limit. SGB startup advances host presentation while guest time stays still, so a small call budget can end before firmware begins.

| Protocol | Required observation |
| --- | --- |
| `serial` | Explicit success or failure text emitted by the cartridge's serial transfer protocol |
| `register` | A completed fetch of the selected debug-marker opcode with the expected success/failure register pattern |
| `fibonacci` | The register convention above, or six serial bytes `03 05 08 0d 15 22` for success and six `42` bytes for failure |
| `memory` | The expected memory signature, observed running status, and final result code |
| `none` | Measurement only; no test-pass verdict |

These protocols are opt-in interpretations for test ROMs. They must not affect ordinary guest execution. A ROM using a different result mechanism needs a suitable observation method; it cannot be called passed because it reached the duration limit.

The `fibonacci` serial form is binary, independent of success/failure text. It requires all six bytes in order and can follow an unrelated serial prefix. A partial signature remains inconclusive. Use it only for a cartridge with that verified reporting convention; the older `register` and `serial` selections keep their separate meanings.

The runner always observes completed serial bytes, reports their count as `serial_bytes`, and evaluates serial verdict text when that protocol is selected. It suppresses raw serial output by default. Add `--serial-output` to emit the transmitted bytes to stdout; the host buffers them and flushes at the report boundary. This diagnostic choice does not change serial clocks or verdict detection. Output errors are reported as `output_error`.

An illegal primary fetch ends the runner with status `illegal_opcode`. The report includes its numeric byte and fetch address in `illegal_opcode` and `illegal_address`; both fields are `-1` when no illegal fetch occurred. Its completed step, clocks, presentation event, and generated PCM are included before termination. This failure takes priority over a test-protocol success or a duration/step bound reached on that fetch. The register protocol likewise counts its completed marker instruction. Compare reports from the same runner revision when using terminal step counts or throughput: observed stepping adds host work that ordinary frontend playback does not request.

The runner prints a `CUPIDGB_RESULT` JSON object containing status, model, protocol, executed steps, base cycles, `base_clock_hz`, VBlank and presentation-event counts, audio-frame and serial-byte counts, output checksums, and elapsed host time. Memory-protocol results include the code and encoded diagnostic text. Raw bytes requested with `--serial-output` can appear before that record, so consumers should locate the prefixed result rather than treating all stdout as JSON.

Exit codes are zero for an explicit pass or a completed measurement, one for a test failure or illegal opcode, two for input/core/output errors, and three for a verdict-enabled run without an explicit pass. Measurement status `bounded` or `step_limit` remains inconclusive even when exit code is zero. Inspect both status and process exit code.

The image output is binary PPM at native dimensions. Raw audio output is interleaved little-endian signed 16-bit stereo PCM, without a file header, at the configured core rate. Checksums help detect differences; matching checksums alone do not explain their cause or establish hardware accuracy.

## External suite wrapper

The Python wrapper accepts individual ROMs or directories containing `.gb` and `.gbc` files:

```sh
python3 scripts/run_rom_suite.py ./build/cupidgb_rom_runner /private/tests \
    --model dmg --protocol register --frames 600 --timeout 30 \
    --boot /private/dmg_boot.bin --power-on-seed 0 --output .local-research/suite.json
```

The suite default protocol is `serial`, unlike the bare runner's `none`. The wrapper hashes each ROM, enforces a host timeout, checks the result record against the exit code, retains bounded output, and writes a private JSON report. It returns success only when every case explicitly passes. `bounded`, `step_limit`, `illegal_opcode`, `core_error`, `timeout`, launch errors, and missing or invalid result records are separate outcomes.

The wrapper validates and forwards `--power-on-seed` with the same syntax. Use an explicit seed for reproducible case comparisons; retain each result's realized seed when testing automatic startup.

Do not apply one protocol blindly to unrelated ROMs. Group cases by model, startup requirements, and result mechanism. Preserve failing and inconclusive cases in reports rather than excluding them from aggregate counts.

## Scripted input

Both the frontend measurement mode and headless runner accept `--input-trace FILE`. Each non-comment line contains a decimal guest-frame timestamp and a hexadecimal active-low button mask:

```text
# Press Start, then release it ten guest intervals later.
300 7f
310 ff
```

Bits 0 through 7 are Right, Left, Up, Down, A, B, Select, Start. All released is `ff`; A alone is `ef`. Timestamps are ascending and use 70,224 base-speed T-cycles per interval. The parsers accept at most 4,096 events. Use equivalent startup conditions because the time spent in a boot binary changes when a menu receives the same input.

An input trace is repeatable host input, not evidence that a game reached active play. Inspect and identify the resulting scene.

## Desktop runtime reports

```sh
./build/cupidgb --boot-directory /private/boot-roms \
    --run-seconds 60 --report .local-research/frontend.json \
    --input-trace /private/start.trace /private/cartridge.gbc
```

Use `--model dmg`, `--model cgb`, or another supported Qt profile to avoid automatic hardware selection in comparable runs. Explicit `--model auto` deliberately requests automatic selection. The frontend isolates its settings, opens a normal audio device, and exits after the requested duration. A report path and cartridge are required. Supported durations are greater than zero and no more than 86,400 seconds. The report has a companion core-image PNG and a `.window.png` capture of the Qt interface.

Durations must be finite. `requested_duration_seconds` records the request, and `host_clock` identifies the elapsed-time source: `linux_monotonic_raw` on Linux or `qt_elapsed_timer` on Windows. Completion waits until that measured interval reaches the request; a Qt timer notification alone does not end the run. Compare an independent process interval using the same physical clock domain when qualifying speed.

`window_visible`, `window_capture_saved`, and the capture dimensions describe the Qt widget and its saved interface image. Inspect that image to verify menus and presentation as well as the game pixels. A visible widget under the `offscreen` platform remains a component test; require `windows` on Windows or a native `xcb`/`wayland` session on Linux for the corresponding runtime scope. In-process capture avoids blank external readbacks from compositors, but does not measure physical display presentation.

`requested_model` is null when the option is absent, `"auto"` for explicit automatic selection, or the canonical selected profile name for a fixed request. The `dmg` alias reports `"dmgabc"`. `hardware_model` records the actual hardware chosen for the cartridge. `base_clock_hz` records that model's physical clock, and the whole-run and post-warmup speed calculations use it. This distinction matters when comparing automatic, fixed DMG, and SGB runs.

The frontend also records `power_on_seed_mode`, `power_on_seed_scope`, and `power_on_seed_is_state_ram_provenance`. Seed metadata belongs to the receiving session's last cold generation. Native-state loading restores the snapshot's actual memory while retaining that host metadata, so it must not be used to infer the seed that originally produced restored RAM.

Reports describe guest-clock progress, images submitted to the widget, widget paint counts/intervals, host audio format, generated samples, queue depth, backend idle/error events, rewind storage/capture work, and input progress. Separate counters record replayed guest cycles, replay presentation events, Turbo window drops and Turbo queue-capacity drops. Dropped host playback frames are not skipped guest cycles. Check the exact report emitted by the current build rather than assuming a fixed schema.

An executed SGB startup holds the guest clock while its host display and sound continue. Pacing includes that host-only interval, but guest-speed measurements and input-trace timestamps keep their guest-time meaning. A measurement that includes startup therefore differs from a post-boot gameplay sample. Match the startup inputs and measurement window before comparing rates.

The `post_warmup` object reports a separate window after a five-second host warmup. It records the actual sampling time, counter baselines, measured duration, guest-clock rate, image/paint/sample deltas, audio events, and paint intervals wholly inside that window. Runs of five seconds or less report it as unavailable. A blocked event loop can also miss the sampling window before the final deadline; that result reports `warmup_sample_unavailable`. This is a time window, not proof of steady gameplay or audible correctness. Whole-run fields retain startup observations. Cartridge or timeline replacement cancels the measurement instead of combining different sessions. Ordinary playback collects no paint-interval history; collection runs only for bounded reports.

Record process exit status and sample resource usage separately if those observations are needed. State whether measurements include startup, stream priming, menus, dialogs, or steady gameplay. Backend idle events are observations requiring investigation; zero reported errors alone does not establish audible correctness.

## Optimization measurements

Separate state encoding, hashing, compression, complete capture, and restoration costs. Preserve a baseline before changing production code, use the same captured state or generated hardware state, and alternate variants to reduce ordering effects. Compare complete encoded bytes and continuation behavior as well as runtime. Codec-only timings do not establish the cost of capture plus compression, and neither establishes gameplay speed.

Compare IPO on and off using the same revision, compiler, optimization level, model, boot inputs, input trace, cycles, and generated audio. Verify images and PCM before reporting a speed ratio. A headless throughput comparison excludes Qt presentation, real audio-device behavior, and rewind capture unless those paths were part of the measurement. [The validation record](validation.md#stream-snapshot-and-build-performance) records the measured component scopes.

## Comparable evidence

For each behavioral comparison, record:

- Product commit, executable hash, build configuration, compiler, operating system, and Qt version.
- Pinned comparison revision and build configuration.
- Cartridge and boot hashes, explicit hardware model, settings, input trace, and duration.
- Success protocol or comparison method, exact outcomes, unresolved differences, and process exit status.
- Images, PCM/traces, pacing, resource samples, and audio-device readiness needed to support the claim.

Keep the comparison source identity and local paths in private receipts. The public checklist records its pinned revision. Match models, boot inputs, palettes/correction, audio settings, and input timing before interpreting differences. Independently written code can match an observation without establishing every surrounding hardware behavior.

Measure guest emulation speed from base-clock progress divided by the selected model's nominal clock and host elapsed time. Use `cupid_gb_base_clock_hz` or the report's `base_clock_hz`, rather than assuming every profile runs at 4,194,304 Hz. Report VBlank counts and host paint events separately. A paint event does not prove physical display presentation. State loads and rewind change the guest timeline, so net cycle progress during those operations is not a forward-throughput benchmark.

Commercial validation should exercise active play, graphics, audio, input, in-game save/reload, native-state continuation, and sustained operation on private disposable cartridge/save copies. A title screen, menu, passing unit test, or bounded session is a narrower result. Preserve existing user saves and retain unresolved gaps in the validation record.

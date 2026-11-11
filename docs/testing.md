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

Run the generated mapper, persistence, and SGB continuation checks together:

```sh
ctest --test-dir build --output-on-failure \
    -R 'cupidgb_(gb|cartridge|battery|camera|mbc7|huc3|tpp1|sgb|state)$'
```

For a library scan, inventory cartridge type bytes separately from the selected hardware model. Give each ROM a fixed guest-clock budget and retain its exact result, including an execution error or incomplete boot. Run banking and peripheral probes for hardware absent from the library. Use separate gameplay and persistence checks before claiming compatibility for a title.

Controller checks run only in a desktop build with SDL2:

```sh
ctest --test-dir build --output-on-failure \
    -R 'cupidgb_(controller_backend|controller_workflows|qt_workflows)$'
```

The backend check attaches two identical standardized virtual controllers and one raw joystick. It verifies device identity/capabilities, axes/buttons/hats, transient raw and normalized edges, hotplug/reconnect, focus/background/master blocking, remapping, unrelated SDL events, rumble callbacks, subsystem ownership and teardown through a Qt event loop. It checks that SDL video and audio remain uninitialized.

The workflow check uses actual native Qt controls for capture, wrong-device rejection, Cancel/OK, player/dead-zone changes, settings-file reload and detach during capture. The application smoke adds generated guest programs for immediate SGB player-count transitions in both directions, held/disabled/preferred device policy, keyboard/controller contribution merging, per-player rapid controls, selected-player reads, shortcuts, paused-tool input, state reconciliation and MBC5 feedback boundaries. Menu/settings tests process timers while checking cycle invariance and restoration of the running or paused state.

Virtual devices are detached before exit. These tests do not qualify a physical USB/Bluetooth device, motor force/duration, actual sensor output, real window-system focus behavior, input latency or commercial play. Run native Windows/Linux smoke and real-device gameplay separately. A WSL environment without exposed input devices cannot establish physical Linux controller support. Do not infer those results from a virtual rumble callback or an offscreen dialog.

C++/C integration checks exercise shared hardware records, initialization/reset, serial observers, and native-state restoration. Run them with Release IPO enabled as well as any fallback configuration being qualified. IPO comparisons must record the effective setting and, for GCC C++, the `-fno-strict-aliasing` flag. The build disables IPO with sanitizers.

The serial and printer targets use generated transfers to check both clock directions, interrupt timing, reply bytes, compressed packets and image output. The Qt smoke sends a job over the emulated serial registers and checks the resulting feed, PNG, and PDF output. The command-line workflow checks direct cartridge loading and archive selection and rejection. Set `CUPIDGB_REQUIRE_ARCHIVE_TESTS=1` to fail if 7-Zip is unavailable.

The cheat core target checks code formats, bank selection, comparisons, duplicate priority, cloning and `.cht` bytes. The search target checks physical regions, all preset conditions, byte/word data, overlapping words, type changes, and bank-boundary writes. Qt workflows exercise actual rule editing and toggles, file persistence and invalid imports, save-state continuity, blocked save destinations, search-to-rule creation, and state-file protection. Color tests hash the complete RGB555 domain for every correction mode and temperature.

Release tests explicitly keep their assertions enabled. Run the GCC/Clang sanitizer configuration as a separate check, with leak detection where available. Preserve the failing command and complete diagnostic when a sanitizer reports a defect.

## Qt workflow checks

```sh
./build/cupidgb --smoke-test
./build/cupidgb --smoke-test --audio-check
```

The first command uses isolated temporary settings and a generated disposable cartridge. It checks application integration without opening a real audio device. CTest runs it using the offscreen platform plugin.

The second command requires an actual output device and exercises playback around state loading and rewind. Also run native visible checks on each target platform. Offscreen checks do not establish dialogs, font/layout behavior, input focus, actual device playback, or physical display presentation under the native backend.

Generated inputs protect normal user data and allow repeatable failure cases. Commercial-game checks still need real cartridge execution, visual inspection, input, sound, and saving.

Set `CUPIDGB_UI_CAPTURE_DIR` when running the smoke check to retain private images of the generated-cartridge debugger and all five inspector tabs. The optional hook creates the supplied directory and writes fixed `debugger.png`, `memory.png`, `tiles.png`, `maps.png`, `objects.png`, and `palettes.png` files. Capture failures fail the check. Use the native platform to inspect actual Qt layout and fonts; a widget capture does not measure physical display presentation.

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

`--frames N` limits elapsed guest time to approximately `N * 70,224` base-speed T-cycles, including LCD-off execution. It does not request N displayed images. Those intervals have different physical durations for SGB NTSC at 4,295,454 Hz and the other profiles at 4,194,304 Hz. The runner does not accept the Qt-only `auto`, `dmgabc`, or `dmg0` selections. `--max-steps N` adds a separate instruction/work bound; the default is derived from the frame limit.

| Protocol | Required observation |
| --- | --- |
| `serial` | Explicit success or failure text emitted by the cartridge's serial transfer protocol |
| `register` | The selected debug-marker opcode with the expected success/failure register pattern |
| `fibonacci` | The register convention above, or six serial bytes `03 05 08 0d 15 22` for success and six `42` bytes for failure |
| `memory` | The expected memory signature, observed running status, and final result code |
| `none` | Measurement only; no test-pass verdict |

These protocols are opt-in interpretations for test ROMs. They must not affect ordinary guest execution. A ROM using a different result mechanism needs a suitable observation method; it cannot be called passed because it reached the duration limit.

The `fibonacci` serial form is binary, independent of success/failure text. It requires all six bytes in order and can follow an unrelated serial prefix. A partial signature remains inconclusive. Use it only for a cartridge with that verified reporting convention; the older `register` and `serial` selections keep their separate meanings.

The runner always observes completed serial bytes, reports their count as `serial_bytes`, and evaluates serial verdict text when that protocol is selected. It suppresses raw serial output by default. Add `--serial-output` to emit the transmitted bytes to stdout; the host buffers them and flushes at the report boundary. This diagnostic choice does not change serial clocks or verdict detection. Output errors are reported as `output_error`.

The runner prints a `CUPIDGB_RESULT` JSON object containing status, model, protocol, executed steps, base cycles, `base_clock_hz`, VBlank and presentation-event counts, audio-frame and serial-byte counts, output checksums, and elapsed host time. Memory-protocol results include the code and encoded diagnostic text. Raw bytes requested with `--serial-output` can appear before that record, so consumers should locate the prefixed result rather than treating all stdout as JSON.

Exit codes are zero for an explicit pass or a completed measurement, one for a test failure, two for input/core/output errors, and three for a verdict-enabled run without an explicit pass. Measurement status `bounded` or `step_limit` remains inconclusive even when exit code is zero. Inspect both status and process exit code.

The image output is binary PPM at native dimensions. Raw audio output is interleaved little-endian signed 16-bit stereo PCM, without a file header, at the configured core rate. Checksums help detect differences; matching checksums alone do not explain their cause or establish hardware accuracy.

## External suite wrapper

The Python wrapper accepts individual ROMs or directories containing `.gb` and `.gbc` files:

```sh
python3 scripts/run_rom_suite.py ./build/cupidgb_rom_runner /private/tests \
    --model dmg --protocol register --frames 600 --timeout 30 \
    --boot /private/dmg_boot.bin --output .local-research/suite.json
```

The suite default protocol is `serial`, unlike the bare runner's `none`. The wrapper hashes each ROM, enforces a host timeout, checks the result record against the exit code, retains bounded output, and writes a private JSON report. It returns success only when every case explicitly passes. `bounded`, `step_limit`, `timeout`, launch errors, and missing or invalid result records are separate outcomes.

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

`window_visible`, `window_capture_saved`, and the capture dimensions describe the Qt widget and its saved interface image. Inspect that image to verify menus and presentation as well as the game pixels. A visible widget under the `offscreen` platform remains a component test; require the native `windows` or `xcb` backend for the corresponding runtime scope. In-process capture avoids blank external readbacks from compositors, but does not measure physical display presentation.

`requested_model` is null when the option is absent, `"auto"` for explicit automatic selection, or the canonical selected profile name for a fixed request. The `dmg` alias reports `"dmgabc"`. `hardware_model` records the actual hardware chosen for the cartridge. `base_clock_hz` records that model's physical clock, and the whole-run and post-warmup speed calculations use it. This distinction matters when comparing automatic, fixed DMG, and SGB runs.

Reports describe guest-clock progress, images submitted to the widget, widget paint counts/intervals, host audio format, generated samples, queue depth, backend idle/error events, rewind storage/capture work, and input progress. Check the exact report emitted by the current build rather than assuming a fixed schema.

The `post_warmup` object reports a separate window after a five-second host warmup. It records the actual sampling time, counter baselines, measured duration, guest-clock rate, image/paint/sample deltas, audio events, and paint intervals wholly inside that window. Runs of five seconds or less report it as unavailable. This is a time window, not proof of steady gameplay or audible correctness. Whole-run fields retain startup observations. Cartridge or timeline replacement cancels the measurement instead of combining different sessions. Ordinary playback collects no paint-interval history; collection runs only for bounded reports.

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

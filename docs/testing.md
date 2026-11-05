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

Core tests exercise lifecycle, CPU/memory behavior, serial, timer/bus phases, graphics, audio, cartridge persistence, native snapshots, older-state battery recovery, and SGB behavior. Frontend checks cover sample conversion, WAVE file output, display processing, and integrated desktop workflows as those targets are present in the current build. Query the exact list with `ctest --test-dir build -N`; avoid carrying test counts from an older revision into a new result.

Release tests explicitly keep their assertions enabled. Run the GCC/Clang sanitizer configuration as a separate check, with leak detection where available. Preserve the failing command and complete diagnostic when a sanitizer reports a defect.

## Qt workflow checks

```sh
./build/cupidgb --smoke-test
./build/cupidgb --smoke-test --audio-check
```

The first command uses isolated temporary settings and a generated disposable cartridge. It checks application integration without opening a real audio device. CTest runs it using the offscreen platform plugin.

The second command requires an actual output device and exercises playback around state loading and rewind. Also run native visible checks on each target platform. Offscreen checks do not establish dialogs, font/layout behavior, input focus, actual device playback, or physical display presentation under the native backend.

Generated inputs protect normal user data and allow repeatable failure cases. Commercial-game checks still need real cartridge execution, visual inspection, input, sound, and saving.

## Headless ROM runner

`cupidgb_rom_runner` accepts one uncompressed cartridge and explicit model, startup, verdict, and duration options:

```sh
./build/cupidgb_rom_runner --model dmg --protocol serial --frames 600 /private/test.gb
./build/cupidgb_rom_runner --model cgb --boot /private/cgb_boot.bin \
    --protocol none --frames 1800 --image .local-research/frame.ppm \
    --audio .local-research/audio.pcm /private/cartridge.gbc
```

Create the output directory first. Model names are `dmg`, `mgb`, `cgb`, `sgb`, and `sgb2`. The runner defaults to `dmg` and protocol `none`; choose the actual test's model and success protocol deliberately. `--boot` accepts one local binary. Without it, the run uses the approximate post-boot profile.

`--frames N` limits elapsed guest time to approximately `N * 70,224` base-speed T-cycles, including LCD-off execution. It does not request N displayed images. `--max-steps N` adds a separate instruction/work bound; the default is derived from the frame limit.

| Protocol | Required observation |
| --- | --- |
| `serial` | Explicit success or failure text emitted by the cartridge's serial transfer protocol |
| `register` | The selected debug-marker opcode with the expected success/failure register pattern |
| `memory` | The expected memory signature, observed running status, and final result code |
| `none` | Measurement only; no test-pass verdict |

These protocols are opt-in interpretations for test ROMs. They must not affect ordinary guest execution. A ROM using a different result mechanism needs a suitable observation method; it cannot be called passed because it reached the duration limit.

The runner prints a `CUPIDGB_RESULT` JSON object containing status, model, protocol, executed steps, base cycles, VBlank counts, sample counts, output checksums, and elapsed host time. Memory-protocol results include the code and encoded diagnostic text. Serial/debug text can appear before that record, so consumers should locate the prefixed result rather than treating all stdout as JSON.

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

The frontend isolates its settings, opens a normal audio device, and exits after the requested duration. A report path and cartridge are required. Supported durations are greater than zero and no more than 86,400 seconds. The report has a companion PNG.

Reports describe guest-clock progress, images submitted to the widget, widget paint counts/intervals, host audio format, generated samples, queue depth, backend idle/error events, rewind storage/capture work, and input progress. Check the exact report emitted by the current build rather than assuming a fixed schema.

Record process exit status and sample resource usage separately if those observations are needed. State whether measurements include startup, stream priming, menus, dialogs, or steady gameplay. Backend idle events are observations requiring investigation; zero reported errors alone does not establish audible correctness.

## Comparable evidence

For each behavioral comparison, record:

- Product commit, executable hash, build configuration, compiler, operating system, and Qt version.
- Pinned comparison revision and build configuration.
- Cartridge and boot hashes, explicit hardware model, settings, input trace, and duration.
- Success protocol or comparison method, exact outcomes, unresolved differences, and process exit status.
- Images, PCM/traces, pacing, resource samples, and audio-device readiness needed to support the claim.

Keep the comparison source identity and local paths in private receipts. The public checklist records its pinned revision. Match models, boot inputs, palettes/correction, audio settings, and input timing before interpreting differences. Independently written code can match an observation without establishing every surrounding hardware behavior.

Measure guest emulation speed from base-clock progress divided by nominal guest clock and host elapsed time. Report VBlank counts and host paint events separately. A paint event does not prove physical display presentation. State loads and rewind change the guest timeline, so net cycle progress during those operations is not a forward-throughput benchmark.

Commercial validation should exercise active play, graphics, audio, input, in-game save/reload, native-state continuation, and sustained operation on private disposable cartridge/save copies. A title screen, menu, passing unit test, or bounded session is a narrower result. Preserve existing user saves and retain unresolved gaps in the validation record.

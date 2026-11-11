# CupidGB

CupidGB is a Game Boy and Game Boy Color emulator with a C11 hardware core and a Qt 6 desktop interface for Windows and Linux. Development is ongoing. Complete hardware accuracy, feature parity, and compatibility with every cartridge have not been established.

The desktop supports cartridge open/recent files and drag/drop, configurable keyboard and controller input, shortcuts, pause/reset/frame advance, fullscreen and display settings, audio playback and WAVE recording, battery RAM/clock persistence, ten native state slots, and bounded rewind history. It can load cartridge archives through 7-Zip and display, save, or print Game Boy Printer output. Cheat controls support Game Genie and GameShark codes, banked rules, and memory searches. CPU debugging and memory/graphics inspectors help examine a running cartridge. The hardware core remains separate from Qt and the SDL2 device backend.

## Start playing

Open an extracted `.gb` or `.gbc` cartridge with Ctrl+O, drag it onto the window, or supply its path:

```sh
cupidgb /path/to/cartridge.gbc
```

Use `--model dmg`, `--model cgb`, or another supported profile to fix hardware for one session. `--model auto` requests automatic selection regardless of the saved preference. The override survives reset and opening another cartridge, but does not change the saved hardware preference. See [hardware selection](docs/user-guide.md#open-reset-and-close) for accepted names.

| Control | Default |
| --- | --- |
| Direction pad | Arrow keys |
| A / B | X / Z |
| Select / Start | Backspace / Enter |
| Turbo / rewind / slow motion | Hold Space / Tab / Shift |
| Open / reset / pause | Ctrl+O / Ctrl+R / Ctrl+P |
| Save / load numbered slot | Ctrl+digit / Ctrl+Shift+digit |
| Fullscreen / mute / screenshot | Ctrl+F / Ctrl+M / F12 |
| Settings / debugger | Ctrl+, / Ctrl+C |

Emulation > Settings controls hardware, the optional boot-ROM directory, input, display, volume, and rewind limits. Model and boot choices apply on the next reset or open. Without a boot binary, startup uses an approximate post-boot profile. Boot binaries and game inputs are not distributed.

Emulation > Controllers selects devices and configures buttons, axes, hats, player assignments, dead zones, hold controls, shortcuts and cartridge rumble. Recognized controllers have a default layout; generic joysticks need layout verification. See [the controller guide](docs/controllers.md) for capture, focus, menus, reconnects and SGB input. Physical devices and motion input remain unqualified.

Battery data uses `.sav` beside the cartridge. State slots use `.s0` through `.s9`, with the zero key selecting slot 10. Archive storage names include the archive extension, such as `game.zip.sav`. New states use CupidGB native version 7. Versions 2 through 6 remain loadable with defaults for fields those versions did not record. File > Recover Battery from Older State can export supported battery data from version 1. Keep backups of saves you need to preserve.

Physical controller/motion qualification, advanced debugger commands/watchpoints, paired cable sessions and other accessories, custom cheat expressions, state interchange, individual hardware revisions, uncommon cartridge behavior, and advanced display/audio features remain incomplete. [The parity checklist](docs/parity.md) records the full scope.

## Build

Requirements: CMake 3.21 or newer, C11/C++17 compilers, Qt 6.4 or newer with Widgets, Multimedia, and PrintSupport, SDL2 2.24 or newer, and Python 3.9 or newer for tests. Core-only builds need neither Qt nor SDL2.

Release builds enable interprocedural optimization when the compiler and linker support it. Other configurations and sanitizer builds retain ordinary compilation. Use `-DCUPIDGB_ENABLE_IPO=OFF` to disable it. On Linux, optional OpenSSL 3 development files enable faster large state hashes. Windows uses its system hashing API. Both retain portable hashing as a fallback.

On Debian or Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build python3 qt6-base-dev qt6-multimedia-dev libsdl2-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/cupidgb /path/to/cartridge.gb
```

On Windows, use Visual Studio 2022 x64 and a matching MSVC Qt package. Replace the example Qt path with your installation:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64;C:/Libraries/SDL2-2.28.5"
cmake --build build --config Release --parallel 4
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
./build/Release/cupidgb.exe C:/Games/cartridge.gbc
```

See [build and installation](docs/building.md) for Ninja, core-only builds, sanitizers, Linux installation, and Windows folder deployment. A build or dependency-copy step alone does not qualify native runtime behavior or packaging.

## Documentation

[The documentation index](docs/index.md) covers the whole application and core:

- [User guide](docs/user-guide.md): controls, settings, display/audio, boot, capture, and save workflows.
- [Controllers](docs/controllers.md): layouts, capture, devices, player routing, native menus, feedback and qualification limits.
- [Hardware support](docs/hardware.md): current models/controllers and their limits.
- [Debugger and inspectors](docs/debugging.md): execution controls, breakpoints, symbols, memory editing, and hardware views.
- [Cheats and memory search](docs/cheats.md) covers codes, banked rules, persistence, RAM searches, and freeze controls.
- [Battery saves](docs/battery-saves.md) and [save states/rewind](docs/save-states.md): locations, encodings, continuation, and recovery.
- [Troubleshooting](docs/troubleshooting.md): loading, input, sound, saves, and Qt problems.
- [Architecture](docs/architecture.md) and [core API](docs/core-api.md): subsystem boundaries and embedding contracts.
- [Testing and measurements](docs/testing.md): regression checks, external suites, scripted input, and comparable runtime evidence.
- [Contributing](docs/contributing.md): formatting, behavioral changes, privacy, and review.

## Validation

```sh
./build/cupidgb --smoke-test
./build/cupidgb --smoke-test --audio-check
python3 scripts/run_rom_suite.py ./build/cupidgb_rom_runner /private/test-roms \
    --model dmg --protocol register --output .local-research/suite.json
./build/cupidgb --run-seconds 60 --report .local-research/frontend.json /private/cartridge.gb
```

The smoke check isolates settings and uses a generated cartridge. Add `--audio-check` to require a real output device. External suites require explicit success verdicts; bounded runs remain inconclusive. Runtime reports record the requested and selected model and its base clock. Guest-speed percentages use 4,295,454 Hz for the SGB NTSC profile and 4,194,304 Hz for the other current profiles; SGB PAL is not implemented. Reports separate guest base-clock progress, VBlank/image counts, widget paint events, and audio/backend measurements. Widget paint events do not establish physical display presentation FPS.

Create private output directories before using capture/report paths. Keep ROMs, boot binaries, saves, captured game content, active builds, and comparison receipts out of public commits. [The validation record](docs/validation.md) lists the tested environments, results, and remaining limits. Use the supplied `.clang-format` for project C/C++ files. The project license is in [LICENSE](LICENSE).

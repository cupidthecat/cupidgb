# CupidGB

CupidGB is a Game Boy and Game Boy Color emulator with a C hardware core and a Qt 6 desktop interface for Windows and Linux. Development is ongoing. Complete accuracy, feature parity, and commercial-game compatibility have not been established.

## Desktop

Open a cartridge with Ctrl+O, drag a `.gb` or `.gbc` file onto the window, or supply its path on the command line. The frontend provides recent files, pause/reset, frame advance, fullscreen, screenshots, configurable keys and shortcuts, volume/mute, WAVE recording, battery RAM and clock persistence, ten native state slots, and rewind. Settings are stored per user.

| Control | Default |
| --- | --- |
| Direction pad | Arrow keys |
| A / B | X / Z |
| Select / Start | Backspace / Enter |
| Turbo / rewind | Hold Space / hold Tab |
| Slow motion | Hold Shift |
| Rapid A / B | Assign keys in Settings |
| Save / load slot | Ctrl+digit / Ctrl+Shift+digit |
| Open / reset / pause | Ctrl+O / Ctrl+R / Ctrl+P |
| Fullscreen / mute | Ctrl+F / Ctrl+M |
| Screenshot | F12 |

Use Settings to select a hardware profile, configure keys, choose a boot-ROM directory, change volume, or select integer/aspect scaling. Boot ROMs are optional local inputs; none are distributed. Without one, the core uses an approximate startup profile.

`--boot-directory /path/to/boot-roms` selects a directory for the current session without changing the saved preference. The directory must contain the file for the selected model. Accepting a boot directory in Settings saves that explicit choice.

Battery data is saved next to the cartridge as `.sav` after a quiet period following writes, and when resetting, changing, or closing the cartridge. Timer-equipped MBC3 cartridges include live and latched clock registers and a host timestamp. A rejected save leaves the current session and existing file intact. See [battery saves](docs/battery-saves.md) for the file contract.

State files and rewind use native version 2, which includes pending pixel fetches and audio integration. Older version 1 files can export their battery data through File > Recover Battery from Older State; they cannot restore execution in the changed core. See [save states and rewind](docs/save-states.md) for controls and recovery.

Use Audio > Start Audio Recording to write stereo 16-bit PCM to a WAVE file. Recording captures forward emulation before host mute, volume, or format conversion. Pause and rewind do not append audio. Reset, cartridge replacement, closing, and sample-rate changes finish the recording. A state load at the same sample rate continues it with newly generated audio.

Controllers, debugger tools, interoperable state imports, and advanced display/audio features remain incomplete. [The parity checklist](docs/parity.md) tracks the full scope.

## Build

Requirements: CMake 3.21 or newer, a C11/C++17 compiler, Python 3.9 or newer for tests, and Qt 6.4 or newer with Widgets and Multimedia.

On Debian or Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build python3 qt6-base-dev qt6-multimedia-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/cupidgb /path/to/cartridge.gb
```

On Windows, use an x64 Visual Studio 2022 developer shell and a matching MSVC Qt installation:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
ctest --test-dir build --output-on-failure
./build/cupidgb.exe C:/Games/cartridge.gbc
```

The Qt path is an example; use the actual location of your installation. Run Qt's `windeployqt` on the executable when preparing a portable Windows folder. A local build alone does not qualify packaging or deployment.

For core-only tools and tests, configure with `-DCUPIDGB_BUILD_QT=OFF`. GCC/Clang builds can enable AddressSanitizer and UndefinedBehaviorSanitizer with `-DCUPIDGB_ENABLE_SANITIZERS=ON`.

## Validation tools

```sh
./build/cupidgb --smoke-test
./build/cupidgb --smoke-test --audio-check
python3 scripts/test_rom_runner.py ./build/cupidgb_rom_runner
python3 scripts/run_rom_suite.py ./build/cupidgb_rom_runner /private/test-roms --model dmg --protocol register --output .local-research/suite.json
./build/cupidgb_rom_runner --model cgb --protocol none --frames 600 --image .local-research/frame.ppm --audio .local-research/audio.pcm /private/cartridge.gbc
```

The frontend smoke check uses an isolated settings directory and a generated disposable cartridge. Add `--audio-check` to require a real audio device and exercise playback across state loading and rewind. The default check uses no audio device. It checks a bounded workflow; it does not establish real-game playability, audible output quality, or sustained pacing.

For a real frontend measurement with host audio enabled:

```sh
./build/cupidgb --run-seconds 60 --report .local-research/frontend.json /private/cartridge.gb
```

This mode isolates settings and records guest speed, widget paint counts, host audio format, queue depth, and reported backend idle/error events. The JSON report and companion PNG are private evidence. Optional `--input-trace` files contain ascending decimal guest frames and hexadecimal active-low button masks, one pair per line. Bits 0 through 7 represent Right, Left, Up, Down, A, B, Select, Start. Frame timestamps use 70,224 base-speed T-cycles. For example, `300 7f` presses Start at frame 300 and `310 ff` releases all buttons at frame 310.

The suite runner requires an explicit serial, register, or memory success protocol. A bounded run without a verdict is inconclusive. Headless output records guest base-clock progress, VBlank counts, audio sample counts, checksums, and host elapsed time separately. These measurements do not establish presentation FPS or full-speed commercial gameplay.

Use the supplied `.clang-format` for project C/C++ files. Keep local ROMs, boot ROMs, saves, build outputs, and private evidence out of commits. [Validation record](docs/validation.md) lists tested scopes and remaining gaps. [Architecture notes](docs/architecture.md) describe subsystem boundaries. The project license is in [LICENSE](LICENSE).

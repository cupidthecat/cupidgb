# CupidGB

CupidGB is a Game Boy and Game Boy Color emulator with a C hardware core and a Qt 6 desktop interface for Windows and Linux. Development is ongoing. Complete accuracy, feature parity, and commercial-game compatibility have not been established.

## Desktop

Open a cartridge with Ctrl+O, drag a `.gb` or `.gbc` file onto the window, or supply its path on the command line. The frontend provides recent files, pause/reset, frame advance, fullscreen, screenshots, keyboard configuration, volume/mute, and battery SRAM persistence. Settings are stored per user.

| Control | Default |
| --- | --- |
| Direction pad | Arrow keys |
| A / B | X / Z |
| Select / Start | Backspace / Enter |
| Turbo | Hold Space |
| Open / reset / pause | Ctrl+O / Ctrl+R / Ctrl+P |
| Fullscreen / mute | Ctrl+F / Ctrl+M |
| Screenshot | F12 |

Use Settings to select a hardware profile, configure keys, choose a boot-ROM directory, change volume, or select integer/aspect scaling. Boot ROMs are optional local inputs; none are distributed. Without one, the core uses an approximate startup profile.

Battery SRAM is saved next to the cartridge as `.sav`, periodically and when resetting, changing, or closing the cartridge. Save states, rewind, controllers, debugger tools, RTC persistence, and advanced display/audio features remain incomplete. See [the parity checklist](docs/parity.md) for the full scope.

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
python3 scripts/test_rom_runner.py ./build/cupidgb_rom_runner
python3 scripts/run_rom_suite.py ./build/cupidgb_rom_runner /private/test-roms --model dmg --protocol register --output .local-research/suite.json
./build/cupidgb_rom_runner --model cgb --protocol none --frames 600 --image .local-research/frame.ppm --audio .local-research/audio.pcm /private/cartridge.gbc
```

The frontend smoke check uses an isolated settings directory and a generated disposable cartridge. It checks a bounded workflow; it does not establish real-game playability, audible output quality, or sustained pacing.

For a real frontend measurement with host audio enabled:

```sh
./build/cupidgb --run-seconds 60 --report .local-research/frontend.json /private/cartridge.gb
```

This mode isolates settings and records guest speed, widget paint counts, host audio format, queue depth, and reported backend idle/error events. The JSON report and companion PNG are private evidence. Optional `--input-trace` files contain ascending decimal guest frames and hexadecimal active-low button masks, one pair per line. Bits 0 through 7 represent Right, Left, Up, Down, A, B, Select, Start. Frame timestamps use 70,224 base-speed T-cycles. For example, `300 7f` presses Start at frame 300 and `310 ff` releases all buttons at frame 310.

The suite runner requires an explicit serial, register, or memory success protocol. A bounded run without a verdict is inconclusive. Headless output records guest base-clock progress, VBlank counts, audio sample counts, checksums, and host elapsed time separately. These measurements do not establish presentation FPS or full-speed commercial gameplay.

Use the supplied `.clang-format` for project C/C++ files. Keep local ROMs, boot ROMs, saves, build outputs, and private evidence out of commits. [Validation record](docs/validation.md) lists tested scopes and remaining gaps. [Architecture notes](docs/architecture.md) describe subsystem boundaries. The repository license and notices in `LICENSES` apply to source and distributed builds.

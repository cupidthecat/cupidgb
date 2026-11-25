# CupidGB

CupidGB is a Game Boy and Game Boy Color emulator with a C11 hardware core and a Qt 6 desktop interface for Windows and Linux. Development is ongoing. Complete hardware accuracy, feature parity, and compatibility with every cartridge have not been established.

The desktop supports multiple cartridge windows with local cable and infrared connections, configurable keyboard and controller input for two linked players, shortcuts, pause/reset/frame advance, fullscreen and display settings, audio playback and WAVE recording, battery RAM/clock persistence, ten native state slots, and bounded rewind history for independent sessions. It also plays GBS music in a compact track player with a live waveform. It can load cartridge and music archives through 7-Zip and display, save, or print Game Boy Printer output. Cheat controls support Game Genie and GameShark codes, banked rules, and memory searches. CPU debugging and memory/graphics inspectors help examine a running program. The hardware core remains separate from Qt and the SDL2 device backend.

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
| New cartridge window | Ctrl+Shift+N |
| Quick Reset | Ctrl+Alt+R |
| Save / load numbered slot | Ctrl+digit / Ctrl+Shift+digit |
| Fullscreen / mute / screenshot | Ctrl+F / Ctrl+M / F12 |
| Audio channels 1 through 4 | Alt+1 through Alt+4 |
| Settings / debugger | Ctrl+, / Ctrl+C |

Emulation > Settings controls hardware, the optional boot-ROM directory, input, display, volume, and rewind limits. Hardware choices apply on ordinary Reset or cartridge open. Quick Reset and Reload keep the active model; all three refresh boot inputs. Without a boot binary, startup uses an approximate post-boot profile. Boot binaries and game inputs are not distributed.

Power-on RAM varies by hardware profile. For a reproducible session, add `--power-on-seed 0x1234`; decimal values and zero are also valid. The option applies to each cold reset/open in that session and does not change saved preferences. Omit it or use `auto` for automatic choices. [Testing and runtime reports](docs/testing.md#headless-rom-runner) explains how to capture and replay the realized seed.

Emulation > Controllers selects devices and configures buttons, axes, hats, player assignments, dead zones, hold controls, shortcuts and cartridge rumble. Recognized controllers have a default layout; generic joysticks need layout verification. See [the controller guide](docs/controllers.md) for capture, focus, menus, reconnects and SGB input. Physical devices and motion input remain unqualified.

Battery data uses `.sav` beside the cartridge. State slots use `.s0` through `.s9`, with the zero key selecting slot 10. Archive storage names include the archive extension, such as `game.zip.sav`. Settings offers system-time or Accurate emulated-time policies for supported [cartridge clocks](docs/rtc.md).

New states use CupidGB native version 27. They retain hardware and peripheral state, including the writable OAM cells on CGB revisions 0 through D, complete cartridge SRAM, MBC6 flash, TAMA5 clock and command latches, and SGB startup and boot-header receiver history. They also identify GBS programs without storing their source files. Older cartridge formats remain loadable when they contain the storage required by the receiving cartridge. Desktop loads keep current listening settings. File > Recover Battery from Older State can export supported battery data from version 1. See [save states and rewind](docs/save-states.md) for compatibility and backup guidance.

Audio settings include Accurate, waveform-preserving and disabled high-pass filtering, optional interference and individual channel toggles. [The audio guide](docs/audio.md) explains how these controls affect playback and recording.

Camera cartridges can use the system camera or a generated pattern. Choose **Camera input** in Settings. System capture starts at the guest's first shutter request, and a failed request retains the previous image. Host images stay outside save files and rewind history. See [Camera cartridges](docs/host-camera.md) for device and save-state behavior.

For [link play](docs/link-play.md), open a second cartridge with File > New Window, then choose Emulation > Connect > Link Cable. Use separate writable cartridge copies for two players using the same game. Pause and playback speed apply to the pair; saves and recordings belong to each window.

Open a `.gbs` file through the same file picker, drag/drop, or command line to play music. The player provides track selection, previous/next, restart and pause. Reset reloads the file's initial track; Restart repeats the selected track. Volume, channel controls and WAVE recording use the existing audio path. See [GBS music](docs/gbs-music.md) for model selection, state loading and format limits.

Physical controller/motion qualification, advanced debugger commands, broader linked-game qualification and other accessories, custom cheat expressions, state interchange, individual hardware revisions, uncommon cartridge behavior, and advanced display/audio features remain incomplete. [The parity checklist](docs/parity.md) records the full scope.

Ordinary Reset restarts play with the selected hardware settings and clears rewind. Quick Reset retains physical memory and compatible rewind; installed firmware executes again and can overwrite that memory. Reload refreshes assets while keeping the active model and pause choice. All reset workflows retain an active recording. See [the user guide](docs/user-guide.md#open-reset-and-close) for storage and firmware details.

## Build

Desktop builds need CMake 3.21 or newer, C11 and C++17 compilers, SDL2 2.24 or newer, and Qt 6.4 or newer with Widgets, Multimedia, Network, and PrintSupport. Linux also needs Qt DBus. Tests need Python 3.9 or newer. Core-only builds need neither Qt nor SDL2.

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
- [Link play](docs/link-play.md): paired windows, two-player controls, shared playback and independent save storage.
- [GBS music](docs/gbs-music.md): opening music, track transport, waveform display, recording and native states.
- [Hardware support](docs/hardware.md): current models/controllers and their limits.
- [SGB startup](docs/sgb-startup.md): adapter introduction, guest and presentation clocks, ordered audio and state continuation.
- [SGB header validation](docs/validation-sgb-protocol.md): checked command reception, native continuation, platform gates and bounded gameplay results.
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

The smoke check isolates settings and uses a generated cartridge. Add `--audio-check` to require a real output device. External suites require an explicit success result. Runtime reports record the requested and selected hardware and its base clock. Guest speed uses 4,295,454 Hz for SGB NTSC, 4,256,274 Hz for SGB PAL and 4,194,304 Hz for the other profiles. Presentation time includes the adapter's initial CPU hold. Reports also track VBlank events, widget paints and audio backend activity. Widget paints do not measure physical display FPS.

Create private output directories before using capture/report paths. Keep ROMs, boot binaries, saves, captured game content, active builds, and comparison receipts out of public commits. [The validation record](docs/validation.md) lists the tested environments, results, and remaining limits. Use the supplied `.clang-format` for project C/C++ files. The project license is in [LICENSE](LICENSE).

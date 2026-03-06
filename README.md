# cupidgb

`cupidgb` is a Game Boy (DMG) emulator written in C, targeting Linux with an SDL2 frontend. The architecture is designed to grow from original Game Boy support into Game Boy Color and Game Boy Advance later.

## Status

- **CPU** — Full Sharp SM83 instruction set (all unprefixed and CB-prefixed opcodes)
- **PPU** — Background, Window, and Sprite (OBJ) layers; 4-shade DMG palette; accurate scanline rendering; STAT/VBlank interrupts
- **APU** — All 4 channels: CH1 (pulse + frequency sweep), CH2 (pulse), CH3 (arbitrary wave), CH4 (noise/LFSR); volume envelopes; per-channel length counters; frame sequencer; stereo mixing via NR50/NR51; SDL2 push-audio at 44100 Hz stereo int16
- **Timer** — DIV, TIMA, TMA, TAC with correct overflow/reload behavior
- **Interrupts** — VBlank, LCD STAT, Timer, Serial; IME/EI/DI/HALT/STOP
- **Joypad** — P1/JOYP register with D-pad and action button selection
- **MBC** — ROM-only, MBC1 (with ROM/RAM banking modes); MBC3 and MBC5 stubs
- **Serial** — Blargg test ROM output piped to terminal
- **Passes** Blargg `cpu_instrs` tests 01 and 02 (all individual instruction groups and interrupt behavior)
- **Plays** Tetris (ROM-only cart)

## Controls

| Key | Game Boy |
|---|---|
| Arrow keys | D-Pad |
| `Z` | A |
| `X` | B |
| `Enter` | Start |
| `Backspace` | Select |

## Dependencies

Build tools:

- `gcc`
- `make`
- `cmake`
- `pkg-config`

Runtime:

- `libsdl2-dev` (SDL2)

Recommended development tools:

- `clangd`
- `clang-format`
- `cppcheck`
- `gdb`
- `valgrind`

## Quick start

### 1. Install development tools

```sh
./scripts/setup_dev_env.sh --install
```

### 2. Configure

```sh
cmake -S . -B build
```

### 3. Build

```sh
cmake --build build
```

### 4. Run

```sh
./build/cupidgb path/to/rom.gb
```

The emulator opens a 640×576 SDL2 window (4× scaled from the 160×144 GB screen). Log output stays in the terminal.

### 5. Test

```sh
ctest --test-dir build --output-on-failure
```

## Make targets

- `make configure`
- `make build`
- `make run`
- `make test`
- `make clean`
- `make debug`
- `make release`

## Project layout

```text
cupidgb/
├── docs/                 # Architecture notes
├── include/cupid/        # Public headers
│   ├── common/           # Logging
│   ├── core/             # Emulator and system interfaces
│   ├── gb/               # Game Boy core API
│   └── platform/         # SDL app interface
├── scripts/              # Dev environment helpers
├── src/
│   ├── common/           # log.c
│   ├── core/             # emulator.c, system.c
│   ├── gb/               # gb.c — CPU, PPU, timer, interrupts, MBC, joypad
│   ├── main.c
│   └── platform/sdl/     # sdl_app.c — SDL2 window, event loop, rendering
├── tests/
│   ├── test_smoke.c      # Sanity / link test
│   └── test_gb.c         # Unit tests: opcodes, DAA, jumps, banking, interrupts
├── CMakeLists.txt
└── Makefile
```

## Architecture

- `src/gb/gb.c` — self-contained GB core: CPU, PPU, APU (4 channels), timer, interrupt controller, DMA, serial, MBC
- `src/core/emulator.c` — thin wrapper that owns a `CupidGb` instance
- `src/platform/sdl/sdl_app.c` — SDL2 frontend: streaming texture upload, keyboard joypad, frame-paced loop
- Platform code has no knowledge of emulation internals beyond the public `cupid/gb/gb.h` API

## Roadmap

- [ ] APU accuracy improvements (channel 3 timing quirks, zombie mode)
- [ ] MBC3 RTC and MBC5 full support
- [ ] APU / audio output
- [ ] Save states and battery-backed SRAM persistence
- [ ] Game Boy Color (GBC) support
- [ ] More Blargg test ROM coverage (`instr_timing`, `mem_timing`)


## Goals

- Start with a solid, testable C core.
- Keep platform code separate from emulation logic.
- Support `GB` first, then extend to `GBC` and `GBA` without restructuring the whole codebase.
- Make the project easy to build in VS Code, `cmake`, or plain `make`.

## Recommended Linux dependencies

Minimum build tools:

- `gcc`
- `make`
- `cmake`
- `pkg-config`
- `gdb`
- `libsdl2-dev`

Recommended development tools:

- `clangd`
- `clang-format`
- `cppcheck`
- `valgrind`

Optional future runtime libraries:

- `SDL2` or `SDL3` for video, audio, and input

## Quick start

### 1. Install development tools

Use the helper script:

```sh
./scripts/setup_dev_env.sh --install
```

If you only want to see what would be installed:

```sh
./scripts/setup_dev_env.sh
```

### 2. Configure

```sh
cmake -S . -B build
```

### 3. Build

```sh
cmake --build build
```

### 4. Run

```sh
./build/cupidgb
```

This opens a basic SDL2 window for display output. Logging stays in the same terminal where the program was started.

### 5. Test

```sh
ctest --test-dir build --output-on-failure
```

## Make targets

- `make configure`
- `make build`
- `make run`
- `make test`
- `make clean`
- `make debug`
- `make release`

## Project layout

```text
cupidgb/
├── .vscode/              # VS Code tasks, launch config, recommendations
├── docs/                 # Architecture notes and roadmap
├── include/cupid/        # Public headers
│   ├── common/
│   └── core/
├── scripts/              # Dev environment helpers
├── src/                  # Application and emulator sources
│   ├── common/
│   ├── core/
│   └── platform/
├── tests/                # Small test programs
├── .clang-format
├── .editorconfig
├── CMakeLists.txt
└── Makefile
```

## Architecture direction

- `src/core`: emulation logic and system-agnostic orchestration
- `src/common`: utilities such as logging and shared helpers
- `src/platform/sdl`: SDL2 window creation, event loop, and future display/input/audio integration
- `include/cupid/core`: public core interfaces
- future `src/platform/sdl`: Linux frontend for windowing, audio, and input
- future `src/core/gb`, `src/core/gbc`, `src/core/gba`: system-specific CPU, memory, PPU, APU, and cartridge logic

## Linux note

This project is set up to work with `gcc`, `make`, and `cmake` by default. 

## Todo 

1. Add cartridge loading and ROM header parsing.
2. Add a memory bus abstraction.
3. Implement the `GB` CPU core and timing loop.
4. Add SDL-based video/input frontend.
5. Split out dedicated `gb`, `gbc`, and `gba` subsystems as features mature.

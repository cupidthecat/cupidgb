# cupidgb

`cupidgb` is a C-based emulator project targeting Linux first, with a clean architecture that can grow from original Game Boy support into Game Boy Color and Game Boy Advance later.

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

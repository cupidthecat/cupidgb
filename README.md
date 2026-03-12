# cupidgb

`cupidgb` is a multi-system Game Boy emulator written in C, targeting Linux with an SDL2 frontend. It currently supports the original Game Boy (DMG) and Game Boy Color (GBC), with ongoing work toward Game Boy Advance (GBA).

## Status

### Game Boy (DMG / GBC)

| Component | Status |
|---|---|
| **CPU** | Full Sharp SM83 instruction set - all unprefixed and CB-prefixed opcodes |
| **PPU** | Background, Window, and Sprite (OBJ) layers; 4-shade DMG palette; scanline rendering; STAT/VBlank interrupts |
| **APU** | All 4 channels: CH1 (pulse + sweep), CH2 (pulse), CH3 (wave), CH4 (noise/LFSR); volume envelopes; length counters; frame sequencer; stereo mixing; SDL2 audio at 44100 Hz stereo int16 |
| **Timer** | DIV, TIMA, TMA, TAC with correct overflow/reload behavior |
| **Interrupts** | VBlank, LCD STAT, Timer, Serial; IME/EI/DI/HALT/STOP |
| **Joypad** | P1/JOYP register with D-pad and action buttons |
| **Cartridge** | ROM-only, MBC1 (ROM/RAM banking, multicart detection), MBC2, MBC3 (with RTC latch/update), MBC5 (9-bit ROM bank, rumble detection), MBC6, MBC7, MMM01, HuC1, HuC3; battery-backed SRAM load/save to `.sav` |
| **Serial** | Blargg test ROM output piped to terminal |
| **Tests** | Passes Blargg `cpu_instrs` tests 01–02 |
| **GBC** | VRAM/WRAM banking, color palettes (BCPS/OCPS/OCPD/OBPD), HDMA/GDMA, color scanline rendering with per-tile attributes (flip, palette, tile bank), CGB compatibility palettes for DMG ROMs with Nintendo licensee codes, double-speed mode (KEY1) |
| **SGB / SGB2** | Full JOYP packet protocol; PAL01/23/03/12, PAL_SET, PAL_TRN (512-palette RAM); ATTR_BLK, ATTR_LIN, ATTR_DIV, ATTR_CHR, ATTR_TRN, ATTR_SET (per-tile attribute map); CHR_TRN, PCT_TRN (256×224 border tiles and tilemap); MLT_REQ (multi-player controller cycling); MASK_EN (disabled/freeze/black/color0 modes); compatibility palette fallback for non-SGB-enhanced ROMs |

## Controls

| Key | Game Boy button |
|---|---|
| Arrow keys | D-Pad |
| `Z` | A |
| `X` | B |
| `Enter` | Start |
| `Backspace` | Select |

## Building

### Dependencies

| Package | Purpose |
|---|---|
| `gcc` | C compiler |
| `make` | Build orchestration |
| `cmake` | Build system |
| `pkg-config` | Library flags |
| `libsdl2-dev` | Video, audio, and input |

Optional development tools: `clangd`, `clang-format`, `cppcheck`, `gdb`, `valgrind`.

### Quick start

```sh
# Install dependencies (Debian/Ubuntu)
./scripts/setup_dev_env.sh --install

# Configure
cmake -S . -B build

# Build
cmake --build build

# Run
./build/cupidgb path/to/rom.gb
```

The emulator opens a 640×576 window (4× scale from the 160×144 native resolution). Log output goes to the terminal.

### Running tests

```sh
ctest --test-dir build --output-on-failure
```

### Make shortcuts

| Target | Action |
|---|---|
| `make configure` | Run CMake configuration |
| `make build` | Build all targets |
| `make run` | Build and run |
| `make test` | Build and run tests |
| `make debug` | Debug build |
| `make release` | Release build |
| `make clean` | Remove build artifacts |

## Project layout

```text
cupidgb/
├── docs/                   # Architecture notes
├── include/cupid/          # Public headers
│   ├── common/             # Logging
│   ├── core/               # Emulator and system interfaces
│   ├── gb/                 # Game Boy core API
│   └── platform/           # SDL app interface
├── scripts/                # Dev environment helpers
├── src/
│   ├── common/             # log.c
│   ├── core/               # emulator.c, system.c
│   ├── gb/                 # cpu.c, ppu.c, apu.c, timer.c, cartridge.c, gb.c, sgb.c
│   ├── gbc/                # cgb.c - Game Boy Color extensions
│   ├── platform/sdl/       # sdl_app.c - SDL2 window, event loop, audio
│   └── main.c
├── tests/                  # Unit and integration tests
├── CMakeLists.txt
└── Makefile
```

## Architecture

The codebase is split into three layers:

- **`src/gb/`** - self-contained GB/GBC hardware: CPU, PPU, APU, timer, DMA, serial, MBC
- **`src/core/`** - thin orchestration wrapper (`emulator.c`, `system.c`)
- **`src/platform/sdl/`** - SDL2 frontend: texture upload, keyboard input, frame pacing, audio

Platform code has no knowledge of emulation internals beyond the public `cupid/gb/gb.h` API.

## Roadmap

- [ ] APU accuracy improvements (CH3 timing quirks, zombie mode)
- [ ] Save states
- [X] Game Boy Advance support - implemented in [cupidgba](https://github.com/cupidthecat/cupidgba)
- [ ] Expanded Blargg test ROM coverage (`instr_timing`, `mem_timing`)

# Architecture notes

## Design goals

- Keep emulator logic independent from Linux-specific frontend code.
- Make each hardware family a separate subsystem when complexity grows.
- Prefer small modules with explicit headers over giant source files.

## Suggested long-term layout

```text
include/cupid/
├── common/
├── core/
├── gb/
├── gbc/
└── gba/

src/
├── common/
├── core/
├── gb/
├── gbc/
├── gba/
└── platform/
    └── sdl/
```

## Core boundaries

### `core`

Owns top-level orchestration:

- emulator lifecycle
- configuration
- system selection
- main stepping loop
- shared scheduling and timing

### `gb`, `gbc`, `gba`

Own system-specific hardware implementations:

- CPU
- MMU / bus
- PPU
- APU
- timer
- interrupts
- cartridge / mapper support

### `platform/sdl`

Owns Linux-facing application services:

- window creation
- input mapping
- audio output
- frame presentation
- file dialogs or save-path handling

## Recommended implementation order

1. ROM header parser
2. cartridge mapper abstraction
3. bus / memory map
4. `GB` CPU instruction decoding
5. timers and interrupts
6. PPU scanline pipeline
7. SDL frontend
8. `GBC` feature expansion
9. `GBA` architecture split into dedicated modules

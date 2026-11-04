# Architecture

CupidGB keeps hardware emulation separate from host services.

- `src/gb` owns the SM83 CPU, memory map, timer, serial clock, DMA, graphics, audio, cartridge controllers, and SGB behavior.
- `src/gbc` owns CGB banking, palettes, DMA extensions, double-speed behavior, and color rendering.
- `src/core` owns the thin emulator lifecycle and GB/GBC system selection.
- `src/frontend/qt` owns windows, input mapping, host audio, settings, ROM workflows, and presentation.
- `tools/rom_runner.c` runs bounded ROM checks without a GUI. `scripts/run_rom_suite.py` records explicit test verdicts and private evidence.

The `cupidgb_core` static library has no Qt dependency. Configure with `-DCUPIDGB_BUILD_QT=OFF` for core tools and tests. The desktop executable links Qt Widgets and Multimedia.

The core uses M-cycles for CPU/timer work. `CupidGb.elapsed_cycles` counts base-speed T-cycles, including execution with the LCD disabled. CGB double speed advances CPU and timer work twice as often as the PPU/audio domain. Frontend pacing must use guest time rather than assuming one completed image per instruction budget.

The public C API currently exposes hardware state through `CupidGb`. Portable save states need an explicit versioned format; serializing the struct would persist pointers and ABI-dependent padding. Until such a format exists, the frontend must not present save-state support.

Remaining hardware and desktop requirements are tracked in [the parity checklist](parity.md). Source presence alone does not establish runtime compatibility.

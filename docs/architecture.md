# Architecture

CupidGB keeps hardware emulation separate from host services.

- `src/gb` owns the SM83 CPU, memory map, timer, serial clock, DMA, graphics, audio, cartridge controllers, and SGB behavior.
- `src/gbc` owns CGB banking, palettes, DMA extensions, double-speed behavior, and color rendering.
- `src/core` owns the thin emulator lifecycle and GB/GBC system selection.
- `src/frontend/qt` owns windows, input mapping, host audio, settings, ROM workflows, and presentation.
- `tools/rom_runner.c` runs bounded ROM checks without a GUI. `scripts/run_rom_suite.py` records explicit test verdicts and private evidence.

The `cupidgb_core` static library has no Qt dependency. Configure with `-DCUPIDGB_BUILD_QT=OFF` for core tools and tests. The desktop executable links Qt Widgets and Multimedia.

The core uses M-cycles for CPU/timer work. `CupidGb.elapsed_cycles` counts base-speed T-cycles, including execution with the LCD disabled. CGB double speed advances CPU and timer work twice as often as the PPU/audio domain. Frontend pacing must use guest time rather than assuming one completed image per instruction budget.

The public C API exposes hardware state through `CupidGb`. `src/gb/state.c` encodes an explicit, versioned native snapshot format rather than serializing that structure. The loader checks identity and integrity, decodes into a separate candidate, and commits only after complete validation. Host resources and input binaries stay with the current session. `src/common/sha256.c` supplies portable hashing for file identity and integrity.

Qt owns atomic state-file writes and compressed, bounded rewind history. Trusted rewind contexts cache immutable cartridge identity; disk operations retain complete hashing. Cartridge/model replacement and future ROM patching must invalidate the context. [The state format and workflows](save-states.md) describe the contract.

Remaining hardware and desktop requirements are tracked in [the parity checklist](parity.md). Source presence alone does not establish runtime compatibility.

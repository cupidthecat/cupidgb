# CupidGB parity checklist

The goal is matching observable hardware behavior and desktop workflows. This checklist records gaps, not a claim of complete compatibility.

Reference source revision: `ea667ad79aa28409f034b1fd6da7304e1844750d`. The local comparison manifest records its checkout, origin, source files, and build logs outside version control. Comparisons must use this revision or record a replacement revision explicitly.

## Hardware

| Area | Current implementation | Acceptance still required |
| --- | --- | --- |
| Models | Grouped DMG profiles, Pocket, one CGB profile, SGB and SGB2 | Separately qualified DMG-B, CGB 0/A/B/C/D/E, SGB NTSC/PAL and host-integration variants, SGB2 variants, later hardware in GB compatibility mode |
| CPU | SM83 decoder, control-flow and interrupt regressions, model-specific HALT polling, STOP input behavior, normal-store LCDC write phases | Complete instruction/timing suites, invalid opcodes, interrupt races, read-modify-write and stack LCDC conflicts, equivalent-model coverage |
| Memory | ROM/RAM/VRAM/WRAM banks, echo and I/O maps | Open bus, model-specific read/write conflicts, bank-switch timing |
| Cartridges | ROM-only, MBC1/2/3/5, partial uncommon controllers | MBC6 flash, MBC7 EEPROM/motion, MMM01, HuC1/HuC3, camera, other source-audited controller variants |
| Timers | DIV/TIMA reload and write behavior | Complete timer acceptance corpus and model differences |
| Serial | Internal slow and CGB fast clock, disconnected input | External link transport, infrared, clock-change races and broad serial suite coverage |
| DMA | OAM DMA and CGB HDMA/GDMA | Bus contention, restart/collision timing, double-speed cases |
| Graphics | Persistent dot-driven background/object queues, window activation, raw-index priority, latched CGB object priority, palettes, SGB border | Complete FIFO/fetcher timing, sprite/window conflicts, broad pixel comparisons, color correction and blending |
| Audio | Four channels, DIV-driven frame sequencer, CGB hardware quirks, stereo 44100 Hz samples | Sweep/envelope/wave quirks, complete APU corpus, filtering and interference, measured audio stability |
| Input | Eight active-low buttons | JOYP edge behavior, controller devices, motion, four-player SGB input |
| Boot | HLE profiles and optional local boot ROM | Model-specific startup comparisons and verified boot-ROM provenance |
| RTC and saves | Battery RAM and MBC3 live/latched clock persistence, halt/carry arithmetic, transactional loads, explicit older-native-state battery recovery | Wider file interoperability, other clock/EEPROM/flash controllers, long-term real-game save qualification |
| Sustained operation | Bounded headless measurement tool | Commercial gameplay, long-running graphics/audio/input/save checks and resource measurements |

## Desktop workflows

The Qt frontend has runtime checks on Windows and Linux. The table separates implemented workflows from pending requirements.

| Area | Implemented | Pending parity work |
| --- | --- | --- |
| ROM lifecycle | Open, recent files, drag/drop, reset, close; failed opens preserve current cartridge | Separate cartridge instances, hot swap semantics, explicit reload workflow |
| Defaults | Automatic model, 2x integer scale, arrows, X=A, Z=B, Backspace=Select, Enter=Start, Space=turbo, Tab=rewind with up to 120 seconds requested, Shift=slow motion | Exact palettes, color correction, blending |
| Keyboard | Persistent button/special-key/shortcut remapping, duplicate/conflict checks, modifier bindings, release on deactivation or modifier changes, optional rapid A/B | Full physical-layout and platform shortcut coverage |
| Display | Integer/aspect scaling, fullscreen, native framebuffer screenshots | Fill mode, scale presets, filters, correction, temperature, blending, palettes, configurable borders, OSD, vsync |
| Audio | Volume/mute, host-format negotiation and streaming conversion, generated-sample priming, bounded queue with backpressure, WAVE recording, runtime metrics | Device selection, filter/interference controls, broad idle-event reduction and audible quality validation |
| Saving | Battery RAM/clock flush after guest writes settle, atomic writes, ten native-v2 slots, dropped states, transactional rejection, bounded rewind, older-native-state battery export | Interoperable state imports, older execution-state continuation, automatic model handling |
| Controllers | None | Device/axis mapping, hotkeys, rumble, background behavior, four SGB players |
| Tools | Bounded headless runner and frontend measurement reports | Debugger console, break/continue/step/next/finish/backstep, symbols, memory editor, tiles/maps/OAM/palette views, layer/channel toggles |
| Cheats | None | Import/create/edit/delete, global/per-cheat toggles, persistence, value search |
| Connections | Disconnected serial behavior | Link cable, infrared, printer feed/export/print, keyboard accessory |
| Capture/music | PNG screenshots, stereo PCM WAVE recording of forward audio | GBS playback and track controls, other recording formats |
| Platform | Qt dialogs/settings/window geometry; local Windows deployment check | File associations, Linux desktop entry, release packaging and installation coverage |

Default desktop shortcuts: Ctrl+O open, Ctrl+R reset, Ctrl+P pause, Ctrl+F fullscreen, Ctrl+M mute, F12 screenshot. State workflows use Ctrl+0 through Ctrl+9 to save and Ctrl+Shift+0 through Ctrl+Shift+9 to load. Unimplemented actions must remain absent or clearly disabled.

## Evidence rules

Run portable core regressions and desktop workflow checks on Windows and Linux. External suites must produce an explicit success protocol; a timeout, screenshot, boot screen, or process exit alone does not establish a pass. Optional suites use externally supplied paths, never hard-coded developer directories or program counters.

Record ROM hashes, model/revision, boot behavior, settings, inputs, duration, reference revision, and build revision with private comparison output. Keep ROMs, boot binaries, saves, and captured game content out of public commits. Report guest base-clock progress separately from VBlank counts and host presentation rate. A bounded timing measurement does not establish commercial gameplay performance.

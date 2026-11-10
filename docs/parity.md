# CupidGB parity checklist

The goal is matching observable hardware behavior and desktop workflows. This checklist records gaps, not a claim of complete compatibility.

Reference source revision: `ea667ad79aa28409f034b1fd6da7304e1844750d`. The local comparison manifest records its checkout, origin, source files, and build logs outside version control. Comparisons must use this revision or record a replacement revision explicitly.

## Hardware

| Area | Current implementation | Acceptance still required |
| --- | --- | --- |
| Models | Grouped DMG profiles, Pocket, one CGB profile, SGB NTSC base clock and SGB2 | Separately qualified DMG-B, CGB 0/A/B/C/D/E, complete SGB NTSC behavior, SGB PAL and host-integration variants, SGB2 variants, later-host GB/GBC compatibility profiles |
| CPU | SM83 decoder, control-flow and interrupt regressions, model-specific HALT polling and IME0 wake, timed CGB speed switching, DIV reset/hold and STOP pad control, T18 interrupt acknowledgment, staged LCDC and STAT stores, and model-specific interrupt stack phases for STAT/LCDC/IF | Complete instruction/timing suites, invalid opcodes, JOYP propagation and related IF timing, other register conflicts and interrupt races, equivalent-model coverage |
| Memory | ROM/RAM/VRAM/WRAM banks, echo and I/O maps, physical-CGB register dispatch and PCM reads | Open bus, model-specific read/write conflicts, bank-switch timing |
| Cartridges | Physical ROM address lines and erased padding, 2 KiB RAM aliases, MBC1/multicart, MBC2, MBC3/extended MBC3, MBC5 enable decoding and rumble, HuC1 banking | MBC7 EEPROM/motion, MMM01, HuC1 infrared, HuC3, camera, TPP1, disabled-RAM bus behavior, unbanked RAM comparison gaps; legacy partial MBC6 behavior has separate scope |
| Timers | DIV/TIMA reload and write behavior | Complete timer acceptance corpus and model differences |
| Serial | Internal slow and CGB fast clock, disconnected input, optional completed-byte host observation without core stream I/O | External link transport, infrared, clock-change races and broad serial suite coverage |
| DMA | OAM DMA, fetched-opcode retention during CGB GDMA, two-dot block slots, delayed HBlank requests, sleep/wake and LCD-off scheduling, canceled-count status | Shared main-bus decay for undriven sources, mode-3 PPU destination-address conflicts, broader OAM restart/collision and revision coverage |
| Graphics | Dot-driven background/object queues, window activation, raw-index priority, latched CGB object priority, palettes, SGB border composition, double-speed HBlank readback, and distinct readable STAT and IRQ phases | Complete FIFO/fetcher timing, sprite/window conflicts, SGB transfer timing, broad pixel comparisons, color correction and blending |
| Audio | Four channels, DIV-driven frame sequencer, CGB power-relative pulse/noise clocks, held output, envelope edges, delayed sweep calculation, model-clock sample scheduling/filtering, stereo 44100 Hz samples | Phantom pulse-trigger cases, high-selector noise corruption, frozen-poll host delivery ordering, wider non-CGB quirks and complete APU corpus, filtering and interference, measured audio stability |
| Input | Four active-low player masks, SGB player selection, selected JOYP falling-edge interrupts, direction-only opposing-contact policy, Qt controller devices and typed bindings | Contact bounce, monochrome row-switch settling, physical controller/multiplayer qualification, motion and faux analog input |
| Boot | HLE profiles and optional local boot ROM, power-on divider and DMA/object palette defaults, FF50 readback and lockout, CGB boot-time register exposure and KEY0 compatibility selection | Wider model/revision startup comparisons and independently written startup firmware |
| RTC and saves | Battery RAM and MBC3 live/latched clock persistence, halt/carry arithmetic, transactional loads, explicit older-native-state battery recovery | Wider file interoperability, other clock/EEPROM/flash controllers, long-term real-game save qualification |
| Sustained operation | Headless reports and bounded native gameplay samples with normal audio | Long-term commercial graphics/audio/input/save checks, wider resource and pacing measurements |

## Desktop workflows

The Qt frontend has runtime checks on Windows and Linux. The table separates implemented workflows from pending requirements.

| Area | Implemented | Pending parity work |
| --- | --- | --- |
| ROM lifecycle | Open, recent files, drag/drop, reset, close, session-only command-line hardware selection; failed opens preserve current cartridge | Separate cartridge instances, hot swap semantics, explicit reload workflow |
| Defaults | Automatic model, 2x integer scale, lime monochrome palette, arrows, X=A, Z=B, Backspace=Select, Enter=Start, Space=turbo, Tab=rewind with up to 120 seconds requested, Shift=slow motion | Hardware color correction, accurate scanline blending |
| Keyboard | Persistent button/special-key/shortcut remapping, duplicate/conflict checks, modifier bindings, release on deactivation or modifier changes, optional rapid A/B | Full physical-layout and platform shortcut coverage |
| Display | Integer/aspect/fill scaling, logical 1x-8x window sizes, physical-pixel integer scaling, fullscreen, native screenshots, four monochrome palettes and custom colors, gamma-correct simple blending, SGB border visibility | Accurate scanline blending, additional filters, correction, temperature, palette-file import, broader border policies, OSD, vsync |
| Audio | Volume/mute, output-device selection with default fallback, host-format negotiation and streaming conversion, generated-sample priming, bounded queue with backpressure, WAVE recording, runtime metrics | Physical device-disconnect qualification, filter/interference controls, broad idle-event reduction and audible quality validation |
| Saving | Battery RAM/clock flush after guest writes settle, atomic writes, ten native-v4 slots, v2/v3 imports with missing-field defaults, dropped states, transactional rejection, bounded model-clock rewind, checked little-endian word arrays, older-native-state battery export | Interoperable state imports, v1 execution-state continuation, automatic model handling |
| Controllers | SDL2 raw and standardized devices, buttons/axes/hats, persistent capture/settings and dead zones, preferred devices, local SGB assignments with single-player folding, hold/command bindings, native menu navigation, focus/background policy, MBC5 motor feedback | Physical devices/motors, stable identical-device qualification, motion/tilt, faux analog patterns, analog speed controls, synthetic feedback and advanced cartridge motors |
| Tools | Bounded headless runner/reports, CPU debugger console, break/continue/step/next/finish, bounded instruction Backstep, bank-aware execution breakpoints, symbols, register/memory commands, mapped-memory editor, tile/map/OAM/palette inspectors | Conditional breakpoints/watchpoints, complete expression syntax, persistent debugger sessions, instruction replay from continuous playback, physical-bank editing/ROM patches, map viewport overlay, palette editing, layer/channel toggles |
| Cheats | None | Import/create/edit/delete, global/per-cheat toggles, persistence, value search |
| Connections | Disconnected serial behavior | Link cable, infrared, printer feed/export/print, keyboard accessory |
| Capture/music | PNG screenshots, stereo PCM WAVE recording of forward audio | GBS playback and track controls, other recording formats |
| Platform | Qt dialogs/settings/window geometry, local Windows folder deployment, Linux desktop launcher/MIME declarations/icon installed through CMake | Windows file associations, broader Linux desktop registration, release packaging and installation coverage |

Default desktop shortcuts: Ctrl+O open, Ctrl+R reset, Ctrl+P pause, Ctrl+F fullscreen, Ctrl+M mute, F12 screenshot. State workflows use Ctrl+0 through Ctrl+9 to save and Ctrl+Shift+0 through Ctrl+Shift+9 to load. Unimplemented actions must remain absent or clearly disabled.

Ctrl+C opens the debugger from the game window. [The debugger guide](debugging.md) describes commands, snapshot limits, memory scopes, and inspector refresh behavior. These tools do not establish complete debugger parity or physical hardware accuracy.

[The controller guide](controllers.md) describes capture, routing, temporary menu/settings suspension and feedback. Virtual-device workflow checks do not establish physical controller support or complete controller parity.

## Source-audited scope

The pinned hardware model list includes DMG-B, Pocket, CGB 0/A/B/C/D/E, SGB NTSC/PAL, SGB2, and SGB host-integration variants. It also defines AGB/Player compatibility profiles for GB/GBC execution; these remain unimplemented. Current CupidGB profile tags group several of these behaviors. An early monochrome profile is also present locally; its existence does not establish a separately qualified revision.

The cartridge audit identifies ROM-only, MBC1, MBC2, MBC3 and its extended wiring, MBC5, MBC7, MMM01, HuC1, HuC3, camera, and TPP1 as comparison requirements. A controller name in CupidGB's decoder is not evidence of complete behavior. In particular, the retained partial MBC6 implementation must not be mistaken for a verified comparison requirement.

Frontend requirements come from actual menus, settings, shortcuts, debugger commands, and accessory workflows. Audio-device selection is an additional Qt convenience; the audited desktop output backends use the system default. The current guides describe the implemented Qt controls, while the pending entries above remain part of the parity goal.

## Evidence rules

Run portable core regressions and desktop workflow checks on Windows and Linux. External suites must produce an explicit success protocol; a timeout, screenshot, boot screen, or process exit alone does not establish a pass. Optional suites use externally supplied paths, never hard-coded developer directories or program counters.

Record ROM hashes, model/revision, boot behavior, settings, inputs, duration, reference revision, and build revision with private comparison output. Keep ROMs, boot binaries, saves, and captured game content out of public commits. Report guest base-clock progress separately from VBlank counts and host presentation rate. A bounded timing measurement does not establish commercial gameplay performance.

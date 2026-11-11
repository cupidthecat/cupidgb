# CupidGB parity checklist

The goal is matching observable hardware behavior and desktop workflows. This checklist records gaps, not a claim of complete compatibility.

Reference source revision: `ea667ad79aa28409f034b1fd6da7304e1844750d`. The local comparison manifest records its checkout, origin, source files, and build logs outside version control. Comparisons must use this revision or record a replacement revision explicitly.

## Hardware

| Area | Current implementation | Acceptance still required |
| --- | --- | --- |
| Models | Grouped DMG profiles, Pocket, one CGB profile, SGB NTSC base clock and SGB2 | Separately qualified DMG-B, CGB 0/A/B/C/D/E, complete SGB NTSC behavior, SGB PAL and host-integration variants, SGB2 variants, later-host GB/GBC compatibility profiles |
| CPU | SM83 decoder, control-flow and interrupt regressions, model-specific HALT polling and IME0 wake, timed CGB speed switching, DIV reset/hold and STOP pad control, T18 interrupt acknowledgment, staged LCDC and STAT stores, model-specific interrupt stack phases for STAT/LCDC/IF, and timed immediate, store, JR and CB accesses | Complete instruction/timing suites, invalid opcodes, other access phases and conflicts, contact bounce and remaining JOYP/IF races, interrupt races, equivalent-model coverage |
| Memory | ROM/RAM/VRAM/WRAM banks, echo and I/O maps, physical-CGB register dispatch, PCM reads, and shared main-bus hold and decay, with monochrome WRAM/OAM and CGB bus separation | Other open-bus paths, model-specific read/write conflicts, bank-switch timing |
| Cartridges | Erased fresh-storage defaults, ROM address lines and erased padding, 2 KiB RAM aliases, MBC1/multicart, MBC2, MBC3/extended MBC3 with write-triggered clock snapshots, MBC5/rumble, MMM01 lock and multiplexing, MBC7 EEPROM and motion latches, HuC1 banking, HuC3 clock and infrared registers, Camera banking and generated sensor captures, TPP1 banking and clock | Physical MBC7 motion, HuC1 infrared, HuC3 transition noise and host infrared and alarm delivery, camera input and analog gain, TPP1 beyond 128 KiB SRAM and variable host feedback, broader undriven RAM bus coverage and partial MBC6 flash |
| Timers | DIV/TIMA reload and write behavior | Complete timer acceptance corpus and model differences |
| Serial | Internal slow and CGB fast clock, external-clock bit sampling, host link callbacks, completed-byte observation, printer command and image handling | Shared desktop timeline for two linked games, infrared transport, clock-change races, printer error cases, and broader serial suite coverage |
| DMA | CPU-T-cycle OAM warmup, first-byte and final-tail timing, OAM/color-DMA ownership, CPU access redirection, restart alignment, fetched-opcode retention during CGB GDMA, two-dot block slots, delayed HBlank requests, sleep/wake and LCD-off scheduling, canceled-count status, CGB main-bus decay, shared VRAM destination addressing, retained PPU/OAM source-port contention and instruction-boundary clock ordering | Unprimed cartridge-bus reset value, final-tail palette effects, broader collision and revision coverage |
| Graphics | Dot-driven background/object queues with initial discarded pixels, delayed OAM word sampling, background fetches during object alignment, window activation, raw-index priority, latched CGB object priority, palettes, SGB screen transfers and staged borders, double-speed HBlank readback, distinct STAT and IRQ phases, LCD-off/STOP presentation periods, and first-frame blank/repeat events | Complete FIFO/fetcher timing, sprite/window conflicts, sampled STOP OAM/palette access gates, remaining SGB host phase differences, broad pixel comparisons, broader model blend-cadence qualification and display filters |
| Audio | Four channels, DIV-driven frame sequencer, CGB power-relative pulse/noise clocks, held output, high-selector noise partial shifts, pulse phantom edges and suppressed maximum-frequency retrigger delays, envelope edges, delayed sweep calculation, model-clock sample scheduling/filtering, stereo 44100 Hz samples | Unaligned and other-revision pulse-trigger cases, other-revision noise transitions, frozen-poll host delivery ordering, wider non-CGB quirks and complete APU corpus, filtering and interference, measured audio stability |
| Input | Four active-low player masks, SGB player selection, selected JOYP falling-edge interrupts, model-specific monochrome row release, direction-only opposing-contact policy, Qt controller devices and typed bindings, MBC7 pointer and analog-axis tilt | Contact bounce, physical controller/multiplayer qualification, physical motion sensors and faux analog input |
| Boot | HLE profiles and optional local boot ROM, power-on divider and DMA/object palette defaults, FF50 readback and lockout, CGB boot-time register exposure and KEY0 compatibility selection | Wider model/revision startup comparisons and independently written startup firmware |
| RTC and saves | Battery RAM and MBC3 live/latched clock persistence, halt/carry arithmetic, HuC3 clock and alarm persistence, transactional loads, explicit older-native-state battery recovery | Wider file interoperability, other clock/EEPROM/flash controllers, long-term real-game save qualification |
| Sustained operation | Headless reports and bounded native gameplay samples with normal audio | Long-term commercial graphics/audio/input/save checks, wider resource and pacing measurements |

## Desktop workflows

The Qt frontend has runtime checks on Windows and Linux. The table separates implemented workflows from pending requirements.

| Area | Implemented | Pending parity work |
| --- | --- | --- |
| ROM lifecycle | Open, recent files, drag/drop, reset, close, single-cartridge ZIP/7z/RAR through 7-Zip, and session-only command-line hardware selection. Failed opens preserve the current cartridge | Separate cartridge instances, hot swap semantics, explicit reload workflow |
| Defaults | Automatic model, 2x integer scale, lime monochrome palette, Accurate frame blending, Balanced color correction, neutral ambient temperature, arrows, X=A, Z=B, Backspace=Select, Enter=Start, Space=turbo, Tab=rewind with up to 120 seconds requested, Shift=slow motion | Broader model-specific default qualification |
| Keyboard | Persistent button/special-key/shortcut remapping, duplicate/conflict checks, modifier bindings, release on deactivation or modifier changes, optional rapid A/B | Full physical-layout and platform shortcut coverage |
| Display | Integer/aspect/fill scaling, logical 1x-8x window sizes, physical-pixel integer scaling, centered scale changes and default-size image transitions, fullscreen, corrected native screenshots, four monochrome palettes, four custom shades and a separate LCD-off color, gamma-correct simple and row-weighted blending, seven CGB/SGB correction modes and 21 temperature steps, SGB border visibility and repeat-aware history | Broader SGB blend timing, additional filters, palette-file import, borrowed-border/later-hardware correction paths, broader border policies, OSD, vsync |
| Audio | Volume/mute, output-device selection with default fallback, host-format negotiation and streaming conversion, generated-sample priming, bounded queue with backpressure, WAVE recording, runtime metrics | Physical device-disconnect qualification, filter/interference controls, broad idle-event reduction and audible quality validation |
| Saving | Battery RAM/clock flush after guest writes settle, atomic writes, ten native state slots, v2–v7 imports with missing-field defaults, dropped states, transactional rejection, bounded model-clock rewind, checked little-endian word arrays, older-state battery export | Interoperable state imports, v1 execution-state continuation, automatic model handling |
| Controllers | SDL2 raw and standardized devices, buttons/axes/hats, persistent capture/settings and dead zones, preferred devices, local SGB assignments with single-player folding, hold/command bindings, native menu navigation, focus/background policy, MBC5 motor feedback, configurable MBC7 tilt axes and optional mouse control | Physical devices/motors, stable identical-device qualification, physical motion-sensor streams, faux analog patterns, analog speed controls, synthetic feedback and advanced cartridge motors |
| Tools | Bounded headless runner/reports, CPU debugger console, break/continue/step/next/finish, bounded instruction Backstep, bank-aware conditional execution breakpoints, bounded pure expressions, symbols, register/memory commands, mapped-memory editor, tile/map/OAM/palette inspectors | Memory watchpoints, complete expression syntax, persistent debugger sessions, instruction replay from continuous playback, physical-bank editing/ROM patches, map viewport overlay, palette editing, layer/channel toggles |
| Cheats | Game Genie/GameShark code import, banked conditional read replacement, add/edit/delete, global/per-rule toggles, compatible atomic `.cht` persistence, file import/export, physical RAM searches with byte/LE/BE word comparisons, edits and freeze rules | Custom expressions, wider commercial cheat-code qualification |
| Connections | Printer attachment, scrollable feed, PNG export and system print dialog | Desktop link cable, infrared transport, keyboard accessory, commercial printer workflows and physical printer qualification |
| Capture/music | PNG screenshots through Save As, stereo PCM WAVE recording of forward audio | Timestamped screenshots beside the cartridge, GBS playback and track controls, other recording formats |
| Platform | Qt dialogs/settings/window geometry, local Windows folder deployment, Linux desktop launcher/MIME declarations/icon installed through CMake | Windows file associations, broader Linux desktop registration, release packaging and installation coverage |

Default desktop shortcuts: Ctrl+O open, Ctrl+R reset, Ctrl+P pause, Ctrl+F fullscreen, Ctrl+M mute, F12 screenshot. State workflows use Ctrl+0 through Ctrl+9 to save and Ctrl+Shift+0 through Ctrl+Shift+9 to load. Unimplemented actions must remain absent or clearly disabled.

Ctrl+C opens the debugger from the game window. [The debugger guide](debugging.md) describes commands, snapshot limits, memory scopes, and inspector refresh behavior. These tools do not establish complete debugger parity or physical hardware accuracy.

[The controller guide](controllers.md) describes capture, routing, temporary menu/settings suspension and feedback. Virtual-device workflow checks do not establish physical controller support or complete controller parity.

## Source-audited scope

The pinned hardware model list includes DMG-B, Pocket, CGB 0/A/B/C/D/E, SGB NTSC/PAL, SGB2, and SGB host-integration variants. It also defines AGB/Player compatibility profiles for GB/GBC execution; these remain unimplemented. Current CupidGB profile tags group several of these behaviors. An early monochrome profile is also present locally; its existence does not establish a separately qualified revision.

The cartridge audit identifies ROM-only, MBC1, MBC2, MBC3 and its extended wiring, MBC5, MBC7, MMM01, HuC1, HuC3, camera, and TPP1 as comparison requirements. A controller name in CupidGB's decoder is not evidence of complete behavior. In particular, the retained partial MBC6 implementation must not be mistaken for a verified comparison requirement.

ROM+RAM cartridge headers `08` and `09` retain direct RAM read/write support. This is an explicit comparison exception; those observations must stay separate from exact matches. Partial MBC6 behavior is likewise outside the pinned mapper comparison scope.

Frontend requirements come from actual menus, settings, shortcuts, debugger commands, and accessory workflows. Audio-device selection is an additional Qt convenience; the audited desktop output backends use the system default. The current guides describe the implemented Qt controls, while the pending entries above remain part of the parity goal.

## Evidence rules

Run portable core regressions and desktop workflow checks on Windows and Linux. External suites must produce an explicit success protocol; a timeout, screenshot, boot screen, or process exit alone does not establish a pass. Optional suites use externally supplied paths, never hard-coded developer directories or program counters.

Record ROM hashes, model/revision, boot behavior, settings, inputs, duration, reference revision, and build revision with private comparison output. Keep ROMs, boot binaries, saves, and captured game content out of public commits. Report guest base-clock progress separately from VBlank counts and host presentation rate. A bounded timing measurement does not establish commercial gameplay performance.

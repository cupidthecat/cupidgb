# CupidGB documentation

CupidGB emulates Game Boy and Game Boy Color hardware with a C core and a Qt desktop interface. The documentation describes the code currently in the repository. Hardware accuracy, complete desktop parity, and compatibility with every cartridge remain development goals.

| Guide | Contents |
| --- | --- |
| [Build and installation](building.md) | Dependencies, Windows and Linux builds, Release IPO, deployment, core-only builds, and sanitizers |
| [User guide](user-guide.md) | Opening cartridges, controls, settings, display, audio, capture, and save workflows |
| [Controllers](controllers.md) | Device selection, capture, mappings, dead zones, player routing, focus, native menus and cartridge feedback |
| [Link play](link-play.md) | Two local cartridge windows, cable and infrared connections, paired controls and separate saves |
| [GBS music](gbs-music.md) | Music loading, compact track controls, waveform display, recording, model policy and native states |
| [Camera cartridges](host-camera.md) | Default-camera capture, image conversion, request lifetime, generated input and save-state behavior |
| [Hardware support](hardware.md) | Models, startup, cartridge controllers, hardware subsystems, and known limits |
| [SGB startup](sgb-startup.md) | Host introduction timing, original artwork, ordered sound, reset and native continuation |
| [Debugger and inspectors](debugging.md) | CPU execution controls, breakpoints, memory watchpoints, symbols, memory editing, and graphics views |
| [Cheats and memory search](cheats.md) | Codes, banked rules, persistence, physical memory searches, and host integration |
| [Battery saves](battery-saves.md) | Save locations, clock behavior, file encoding, and recovery |
| [Cartridge clocks](rtc.md) | Host and emulated clock policies, counter behavior, time providers, and period controls |
| [Save states and rewind](save-states.md) | Numbered slots, validation, rewind limits, and the native state format |
| [Troubleshooting](troubleshooting.md) | Loading, input, display, sound, saving, and build problems |
| [Architecture](architecture.md) | Subsystem ownership, clock domains, and host integration |
| [Timing and clock domains](timing.md) | CPU bus phases, interrupt acknowledgment, STAT latches, audio units, host deadlines, and timing validation |
| [Audio subsystem](audio.md) | Channel and divider clocks, register behavior, sample reconstruction, filters, host playback, snapshots, and audio limits |
| [Core API](core-api.md) | Lifecycle, model clocks, stepping, serial observation, snapshots, and embedding contracts |
| [Testing and measurements](testing.md) | Regression checks, external ROM suites, comparable runtime evidence, and reports |
| [Contributing](contributing.md) | Source changes, formatting, documentation, private inputs, and review requirements |
| [Parity checklist](parity.md) | Implemented behavior and remaining hardware and desktop requirements |
| [Validation record](validation.md) | Recorded environments, test results, runtime measurements, and their limits |
| [Cartridge-clock validation](validation-rtc.md) | Counter comparisons, clock policies, divider timing, native continuation, and remaining qualification |
| [Audio and native-state validation](validation-audio.md) | Reconstruction, live restoration, SGB phase bounds, Camera processing, mapper comparisons, and native continuation |
| [Display and infrared validation](validation-timing.md) | Display delivery, window/object edges, VRAM ownership, retained timing history and scoped comparison results |
| [Fetch and feedback validation](validation-fetch-feedback.md) | Retained color tile rows, motor sampling, guest speed switches and bounded desktop measurements |
| [Watchpoint validation](validation-watchpoints.md) | Guest access observation, debugger lifecycle checks, source conventions and current validation scope |
| [Cartridge storage validation](validation-cartridge-storage.md) | Extended bank populations, failed-load preservation, mapper model coverage and allocation lifetime |
| [Desktop clock validation](validation-host-clock.md) | Independent host-clock measurements, pacing deadlines and native audio readiness |
| [Wave-memory validation](validation-wave-retrigger.md) | CH3 source and destination corruption, model controls, guest stores and native continuation |
| [Wave fetch-history validation](validation-wave-history.md) | Isolated CH3 fetch and length-expiry comparisons, guest PCM reads and the earlier version 19 layout |
| [Serial selection validation](validation-serial-selection.md) | Retained divider selection, compatibility-mode readback and native version 18 migration |
| [Hardware integration validation](validation-hardware-selection.md) | Combined hardware gates, state interchange, mapper limits, native workflows and Windows gameplay |
| [Storage, wave and camera validation](validation-storage-wave.md) | Combined SRAM ownership, native version 20, NR30 disconnect behavior and camera timeline checks |
| [SGB startup validation](validation-sgb-startup.md) | Host-only startup clocks, ordered output, version 21 continuation and scoped desktop/runtime observations |
| [SGB protocol validation](validation-sgb-protocol.md) | Boot-header permission, version 22 continuation, paired observations and bounded gameplay |
| [Link and LY validation](validation-link.md) | Two-window cable workflows, ignored LY writes, mapper tests, library scan and native paired gameplay |
| [GBS player validation](validation-gbs.md) | Music loading, audio history, double-speed timing, version 23 states and native player workflows |

Start with the build guide if you have a source checkout. For an executable that already runs, use the user guide. A result in the validation record applies to the stated revision, inputs, settings, and duration; it does not qualify every later build.

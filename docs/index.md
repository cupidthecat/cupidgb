# CupidGB documentation

CupidGB emulates Game Boy and Game Boy Color hardware with a C core and a Qt desktop interface. The documentation describes the code currently in the repository. Hardware accuracy, complete desktop parity, and compatibility with every cartridge remain development goals.

| Guide | Contents |
| --- | --- |
| [Build and installation](building.md) | Dependencies, Windows and Linux builds, Release IPO, deployment, core-only builds, and sanitizers |
| [User guide](user-guide.md) | Opening cartridges, controls, settings, display, audio, capture, and save workflows |
| [Hardware support](hardware.md) | Models, startup, cartridge controllers, hardware subsystems, and known limits |
| [Debugger and inspectors](debugging.md) | CPU execution controls, breakpoints, symbols, memory editing, and graphics views |
| [Battery saves](battery-saves.md) | Save locations, clock behavior, file encoding, and recovery |
| [Save states and rewind](save-states.md) | Numbered slots, validation, rewind limits, and the native state format |
| [Troubleshooting](troubleshooting.md) | Loading, input, display, sound, saving, and build problems |
| [Architecture](architecture.md) | Subsystem ownership, clock domains, and host integration |
| [Timing and clock domains](timing.md) | CPU bus phases, interrupt acknowledgment, STAT latches, audio units, host deadlines, and timing validation |
| [Audio subsystem](audio.md) | Channel and divider clocks, register behavior, sample integration, host playback, snapshots, and audio limits |
| [Core API](core-api.md) | Lifecycle, model clocks, stepping, serial observation, snapshots, and embedding contracts |
| [Testing and measurements](testing.md) | Regression checks, external ROM suites, comparable runtime evidence, and reports |
| [Contributing](contributing.md) | Source changes, formatting, documentation, private inputs, and review requirements |
| [Parity checklist](parity.md) | Implemented behavior and remaining hardware and desktop requirements |
| [Validation record](validation.md) | Recorded environments, test results, runtime measurements, and their limits |

Start with the build guide if you have a source checkout. For an executable that already runs, use the user guide. A result in the validation record applies to the stated revision, inputs, settings, and duration; it does not qualify every later build.

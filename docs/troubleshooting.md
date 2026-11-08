# Troubleshooting

Record the CupidGB version or commit, operating system, selected hardware, startup method, and exact error before changing settings. Keep a copy of battery saves and states when investigating persistence. The [user guide](user-guide.md) explains normal workflows; [the parity checklist](parity.md) lists incomplete features.

## The cartridge does not open

Use an extracted cartridge image. ZIP/archive loading is not an implemented workflow. A battery save or native state is not a cartridge: open the matching game first, then load its state.

Check that the file is readable and its size/header are supported. The frontend accepts files from 336 bytes through 8 MiB. Physical ROM banks follow the actual loaded length rather than the header's ROM-size field, with missing tail bytes reading `0xff`. A checksum warning does not by itself establish a corrupted or compatible image.

If the game requires color hardware, select Automatic or Game Boy Color in Settings and retry. Hardware changes apply on reset/open. Automatic selection can choose color hardware for some monochrome cartridges; use an explicit model for comparisons.

A configured boot directory makes the model's boot file mandatory. Clear the field to use approximate post-boot startup, or supply the correct filename and byte length listed in the [boot table](user-guide.md#boot-behavior). A correct size alone does not validate binary provenance or behavior.

An invalid existing `.sav` can also prevent a cartridge from opening. Preserve that file, inspect the reported size/format error, and test using a disposable cartridge copy in another folder. Do not overwrite the original save to make the error disappear.

## The game opens but stops, draws incorrectly, or behaves differently

Check the hardware model and whether startup used a boot binary. Comparisons made with different startup states are not equivalent. Color palettes and host display processing can change visible colors without changing guest pixels.

Try to reproduce from a fresh cartridge launch with the same inputs. If the problem occurs only after loading a state, retain that state and the matching cartridge/boot hashes privately. If it occurs during ordinary play, record the scene and the shortest repeatable input sequence. Controllers and fine timing behavior are incomplete; a successful open is not a compatibility guarantee.

When the core reports that CPU execution stopped, CupidGB pauses with the cartridge still open. Preserve the diagnostic. Repeated reset or turbo runs do not establish a fix.

## Keys do not respond or remain held

Click the game area and make sure emulation is not paused. Check button and special-key bindings in Settings. Empty bindings disable an action. Buttons accept individual keys; command shortcuts can use modifiers. Duplicate or conflicting mappings are rejected on acceptance.

CupidGB releases inputs on ordinary pause or application deactivation. Paused [debugger inspection](debugging.md) preserves the guest latch while releasing physical bindings; resuming playback reconciles the two. Test a reported stuck key by pressing/releasing it while the game has focus, then switching away and back. Include the modifier sequence and keyboard layout in a bug report. Controllers and four-player mapping remain pending.

## Sound is missing or unstable

Check Audio > Mute, Settings volume, and Settings > Audio output. System Default follows the host default device. A saved unavailable device falls back to the default while retaining the saved request. Turbo, pause, and rewind intentionally suppress host playback. WAVE recording captures the core stream before mute and volume, so a nonempty recording does not prove the host device is audible.

Run the native frontend with `--smoke-test --audio-check` from a working desktop session. The default smoke test and offscreen CTest workflow do not open a real device. An unavailable or unsupported device is reported by the application.

On Linux, confirm that the desktop audio service is reachable from the same session that launches CupidGB. Under WSLg, an installed Qt package alone does not establish that the forwarded audio service is ready. Keep a report from the actual audio-enabled run.

For stutter, collect a [bounded frontend report](testing.md#desktop-runtime-reports) with normal audio and default speed. Include guest-clock progress, queue information, backend idle/error counts, and whether the interval included startup. Do not classify widget paint counts as presentation FPS. Host scheduling and hardware audio accuracy require separate investigation.

## Saves are not written or a session will not close

Battery files live beside the cartridge. Verify that the containing folder is writable and has free space. Closing, reset, or replacement can stop when the current battery file cannot be saved; inspect the error instead of discarding the session.

Not every cartridge has battery storage. Clock progression alone does not repeatedly mark a save dirty. State loading and rewind can restore earlier battery contents and schedule a write. See [battery saves](battery-saves.md) for timing and encoding details.

Keep cartridge, save, state, screenshot, and recording paths distinct. Existing cartridge/save data is protected in workflows that would otherwise replace an input. Use a private copy to investigate file aliasing or malformed content.

## A state will not load

Open the exact matching cartridge and select the state file through File > Load State File, drag/drop, or the numbered Load State menu. A primary `.sN` file that exists but fails validation is not silently replaced by a `.snN` alternative.

Native states check cartridge identity, hardware model, format, integrity, field domains, and any required boot overlay. A different region/revision of a game is a different input even when its title looks the same. A state made before the current format revision can require explicit recovery rather than execution restore.

Version 1 states can export their supported battery data through File > Recover Battery from Older State. They cannot resume execution in the changed core. Export to a separate file and preserve the original. [The state guide](save-states.md) explains supported formats and failure behavior.

## Rewind history is short or empty

Check Settings > Rewind history and Rewind memory limit. Disabled history captures nothing. The requested duration is capped by compressed storage; a game with less compressible state can reach the memory limit earlier. Settings show the actual retained duration and size.

History resets after cartridge replacement, reset, successful disk-state loading, or relevant configuration changes. A newly opened game has no earlier frames. Rewind is an in-memory history and does not persist across application launches.

## Qt cannot be found, the executable does not launch, or a platform plugin fails

Point `CMAKE_PREFIX_PATH` at the matching Qt installation when configuring. Match architecture and compiler. Use a fresh build directory when switching generator or toolchain. For a Visual Studio build, run the selected configuration's executable and pass `-C Release` to CTest.

On Windows, check launch with Qt's `bin` directory in the process environment during development. For deployment, use the matching `windeployqt` and test the staged folder without development paths. On Linux, install the matching runtime/platform dependencies and use a graphical session. `QT_DEBUG_PLUGINS=1` can reveal plugin loading failures; keep that diagnostic separate from normal test output.

An offscreen smoke check is useful for isolating logic from a native platform problem, but it does not qualify the native window or audio backend. Follow [the build guide](building.md) before interpreting a launch failure as a core defect.

## Report a reproducible problem

Include the CupidGB revision, build/tool versions, OS and display/audio backend, model and boot method, cartridge hash and header type, exact steps or input trace, expected result, actual result, and logs. Identify whether the scene is gameplay, a menu, or startup. Keep ROMs, boot binaries, saves, captured game content, and private paths out of public reports. Public test source or generated cases can provide a reproducible substitute when appropriate.

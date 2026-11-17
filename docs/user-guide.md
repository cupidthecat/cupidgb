# User guide

CupidGB runs one cartridge at a time. It keeps desktop settings per user and stores cartridge battery data and numbered save states beside the cartridge file. Use an ordinary writable folder for games whose saves you want to keep.

## Open, reset, and close

Choose File > Open ROM, press Ctrl+O, drag a cartridge onto the window, or pass its path on the command line:

```sh
cupidgb /path/to/cartridge.gbc
```

Open a `.gb`, `.gbc`, or `.sgb` file directly. With 7-Zip installed, CupidGB also opens `.zip`, `.7z`, and `.rar` archives containing exactly one unencrypted cartridge. Compressed RAR files require a 7-Zip installation with the RAR codec. CupidGB reads the cartridge into memory. Battery saves and state slots keep the archive extension in their names, such as `game.zip.sav` and `game.zip.s1`. File > Open Recent keeps up to ten successful cartridge paths.

Automatic hardware selection uses the cartridge header. It selects color hardware for color-capable cartridges, SGB for SGB-enhanced monochrome cartridges, and may select color hardware for licensed monochrome cartridges to provide their compatibility palette. Select a specific model in Emulation > Settings when reproducing model-dependent behavior. A color-only cartridge cannot open under a selected monochrome model.

For a hardware choice that lasts only until the application exits, pass `--model auto|dmg|dmgabc|dmg0|mgb|cgb|sgb|sgb2`. Here `dmg` and `dmgabc` select the same grouped Game Boy profile. An omitted option uses the saved preference; explicit `auto` performs automatic selection for every cartridge even when a fixed preference is saved. A fixed override applies on reset, close/reopen, and opening another cartridge. Settings displays the session choice in a disabled hardware selector and does not overwrite the saved model when accepted.

The SGB profile uses its NTSC base clock of 4,295,454 Hz. SGB2 and the other current profiles use 4,194,304 Hz. Normal-speed playback and rewind duration follow the selected clock. A higher SGB image rate is part of that profile; SGB PAL is not available.

SGB and SGB2 show a CupidGB introduction when executing a configured boot ROM, including with ordinary monochrome cartridges. The adapter holds the guest CPU for the first 106 presentations, then lets firmware execute while the introduction finishes. Approximate post-boot startup skips the introduction. Hiding the SGB border keeps the introduction inside the game viewport.

Emulation > Reset saves the current battery and clock history, refreshes the cartridge, firmware and companion symbols, and applies the selected hardware settings. It starts execution again and clears rewind history. File > Close ROM saves the current battery and closes the session. A failed battery write prevents reset, reload, closing or replacement and keeps the session and any recording available for retry.

Emulation > Quick Reset, or Ctrl+Alt+R, keeps the active hardware model and physical work RAM, high RAM, object memory and color palette memory while restarting the console. It refreshes the cartridge, firmware and companion symbols. VRAM and execution state restart. Quick Reset resumes play and retains rewind when the cartridge bytes, model and firmware are unchanged. Installed firmware executes again and can overwrite retained memory. Without firmware, the approximate color handoff clears WRAM bank 2 and object memory.

File > Reload Cartridge refreshes the cartridge, firmware and companion symbols while keeping the active hardware model, breakpoints and running or paused choice. It performs a hard reset and retains compatible rewind history. A missing companion file clears the old symbols; malformed symbols or unreadable assets leave the session intact. Reload has no default shortcut; assign one in Settings if needed.

All reset workflows keep an active audio recording and held input. Menus suspend guest execution while preserving existing holds and accepting releases. Closing a menu reconciles current controller state before play resumes. Battery storage survives both reset kinds; volatile cartridge storage survives Quick Reset and is erased by a hard reset. MBC3 and TPP1 clock values survive. HuC3 clock and alarm digits reset with their controller and are saved through the normal autosave path.

## Controls

| Action | Default |
| --- | --- |
| Direction pad | Arrow keys |
| A / B | X / Z |
| Select / Start | Backspace / Enter |
| Turbo | Hold Space |
| Rewind | Hold Tab |
| Slow motion | Hold Shift |
| Rapid A / B | Unassigned; configure in Settings |
| Open / close cartridge | Ctrl+O / Ctrl+W |
| Reload cartridge | Unassigned; configure in Settings |
| Reset / pause | Ctrl+R / Ctrl+P |
| Quick Reset | Ctrl+Alt+R |
| Advance one interval | Ctrl+N |
| Debugger | Ctrl+C |
| Settings | Ctrl+, |
| Fullscreen | Ctrl+F |
| Mute | Ctrl+M |
| Screenshot | F12 |
| Save slot 1 through 9 | Ctrl+1 through Ctrl+9 |
| Load slot 1 through 9 | Ctrl+Shift+1 through Ctrl+Shift+9 |
| Save / load slot 10 | Ctrl+0 / Ctrl+Shift+0 |
| Quit | Ctrl+Q |

Pause releases held game inputs and clears queued playback. Advance Frame pauses first, then runs until the next image or a bounded guest-clock interval when the LCD is off. It clears host audio rather than playing one isolated audio fragment.

Turbo preserves guest hardware execution while increasing the host speed target. Emulation > Turbo Speed Cap and Settings offer Uncapped or 150% through 400% in 25% steps. Uncapped is the default. A cap is an upper target; it cannot make a busy host run faster than it can emulate. Turbo plays selected segments of generated audio through a bounded queue. Slow motion gradually changes the target to half normal speed and adjusts playback conversion; releasing it returns to normal. With a turbo cap selected, slow motion scales that cap too. Rapid A and B alternate two forward intervals pressed and two released while their assigned keys are held.

Ordinary focus loss releases input so returning to the window does not leave a key stuck. Paused debugger inspection preserves the stopped guest input while releasing physical bindings; normal playback reconciles it before resuming. See [debugging](debugging.md) for that inspection behavior. Click the game area before playing if a dialog or another window has focus.

## Configure keys and shortcuts

Open Emulation > Settings. Each game button and hold mode accepts one key, including a standalone modifier or Tab. Empty bindings disable that input. Command shortcuts may combine a key with modifiers. The dialog rejects duplicate mappings and conflicting unmodified command keys.

Changes apply when you accept the dialog. Hardware settings take effect on ordinary Reset or cartridge open. Quick Reset and Reload retain the active model; all three refresh boot inputs. Cancel keeps the previous settings. Keyboard layouts and platform shortcut handling need broader qualification.

Open Emulation > Controllers for device selection, button/axis/hat capture, dead zones, player assignments, hold controls, command shortcuts, background input and cartridge rumble. Layouts and preferred devices persist separately from cartridge state. Keyboard and controller contributions combine. Single-player sessions accept eligible controllers regardless of their saved SGB player assignment; a multiplayer request switches to the assigned input slots at the next hardware-step boundary. [The controller guide](controllers.md) explains defaults, capture, weak device identities, native menu navigation and remaining physical/motion limits.

MBC7 games use the horizontal and vertical tilt axes selected in Controllers. Emulation > Motion-Controlled Games > Allow mouse control enables pointer tilt and mouse-button A for those cartridges; it starts off. Center the pointer or release the stick when the game asks to calibrate. [Motion controls](controllers.md#motion-cartridges) explains axis selection and input ownership.

Settings and controller dialogs explicitly pause emulation and restore the prior pause choice on close. Open menus temporarily block execution and playback without changing that choice; nested menus retain the block until all menus close. Selecting Pause or resuming from a menu keeps the selected state afterward.

## Boot behavior

An empty Boot ROM directory starts from an approximate post-boot profile. To run a local boot binary, select its containing directory in Settings or supply a session override:

```sh
cupidgb --boot-directory /path/to/boot-roms /path/to/cartridge.gb
```

| Selected hardware | Filename | Bytes |
| --- | --- | --- |
| Game Boy or Game Boy (DMG0) | `dmg_boot.bin` | 256 |
| Game Boy Pocket / Light | `mgb_boot.bin` | 256 |
| Game Boy Color | `cgb_boot.bin` | 2,304 |
| Super Game Boy | `sgb_boot.bin` | 256 |
| Super Game Boy 2 | `sgb2_boot.bin` | 256 |

Selecting a directory makes the corresponding file mandatory. A missing or wrong-sized binary produces an error and preserves the current session. The session override does not change the saved preference. Accepting the Settings dialog saves its displayed boot directory as an explicit preference.

Boot binaries are separate inputs. Their presence does not establish that every hardware revision or power-on state is accurate.

## Display

Use View > Fullscreen to enter or leave fullscreen mode. Display choices affect host presentation; they do not alter the guest's memory, pixel generation, or timing. The display has a 160 by 144 pixel game area, with a 256 by 224 image when the SGB border is shown.

View > Display Scaling offers Integer Scale, Retain Aspect Ratio, and Fill Window. Integer Scale is the default and keeps pixel edges on an integer device-pixel scale. Retain Aspect Ratio fits the image without changing its proportions. Fill Window stretches it to the available area. Rendering uses nearest-neighbor sampling; additional filters remain pending.

View > Window Size selects 1x through 8x in logical window units and stores the default scale; new settings start at 2x. High-DPI desktops can map one logical unit to several physical pixels. Integer Scale independently keeps the rendered image on whole physical-pixel factors. Restored window geometry can override the initial size on a later launch. The corresponding settings are Display scaling and Default window size in the Settings dialog. Visible scale changes preserve the window center. A window still at its default scale follows changes between the handheld image and an SGB border. Custom sizes and fullscreen geometry stay in place, subject to the native minimum image size.

View > Color Correction offers Disabled, Correct Color Curves, Modern - Balanced, Modern - Accurate, Modern - Boost Contrast, Reduce Contrast, and Harsh Reality. New settings use Modern - Balanced. View > Ambient Light Temperature provides 21 positions from cooler to warmer light, with neutral at the center. Both controls are also in Settings. Disabled with neutral temperature preserves the expanded hardware color. Temperature still applies when correction is disabled. These choices affect CGB colors, color-hardware compatibility output, SGB colors, and screenshots. CGB palette previews use the same settings; numeric inspector values remain raw. Native monochrome palettes are unchanged.

View > Monochrome Palette offers Lime (Game Boy), Grayscale, Olive (Pocket), Teal (Light), and Custom. Lime is the default. A custom palette uses four `#RRGGBB` entries in Settings, ordered from the lightest shade 0 to the darkest shade 3. Invalid entries are rejected. These choices affect monochrome output; color-hardware compatibility palettes and SGB colors remain owned by the emulated hardware.

View > Frame Blending and Settings offer Disabled, Simple, and Accurate. Simple gives the two most recent completed LCD images equal weight. Accurate alternates the weighting by LCD row and guest frame phase. Both mix in linear light using gamma 2.2. New settings default to Accurate, which uses Simple on SGB and SGB2. Existing on/off preferences keep their previous behavior. Cartridge changes, reset, state loads, rewind, and palette changes clear the frame history.

Settings > SGB display controls whether to show the Super Game Boy border. Hiding it crops the image to the game viewport and does not disable SGB commands or palette behavior. Host palettes and blending do not establish hardware color accuracy. See [the parity checklist](parity.md).

View > On-screen Messages and Settings > Notifications enable brief messages inside the game image. They start disabled. Opening a cartridge shows the CupidGB version, cartridge header title and CRC-32. The CRC describes the logical cartridge image, including erased padding and MMM01 menu ordering, so it can differ from the raw file's CRC. Saving or loading a state, taking a screenshot, and starting or stopping audio recording can also display a message. Holding turbo, slow motion or rewind refreshes its mode message. Message time follows guest presentation events, so pausing or repainting the window does not consume it. Messages follow the selected frame blending; toggling the setting while paused preserves the submitted image until the next guest frame. Screenshots exclude these messages.

The custom monochrome palette has four hardware shades and a separate LCD-off color. Older settings with only four shades use their lightest shade for the missing off color. LCD-off and first-blank events use the off color; monochrome STOP uses the lightest hardware shade. Repeat events retain the displayed image and blend history.

View > Monochrome Palette > Saved Palettes selects a named palette from your library and activates Custom. Edit Palette Library opens the library and color controls. Add duplicates the selected palette, Name renames it, and Delete removes it. Duplicate names receive numbered suffixes. Deleting the last entry keeps its colors as an Untitled Palette. Restore Built-in Palettes restores the four built-in color sets while preserving entries with other names. The library holds up to 256 palettes. If missing built-ins would exceed that limit, restoration reports an error and leaves the entire library unchanged. Delete unused entries before trying again.

The editor applies changes immediately. Choose each shade manually enables four individual hardware colors. With Separate LCD-off color disabled, the off color follows the lightest shade. Automatic mode instead uses the Darkest and LCD off colors as endpoints. Brightness bias adjusts the middle shades, while Hue bias and Hue strength control their color balance. Automatic mode with a separate off color leaves the lightest hardware shade slightly below the off endpoint. The preview shows the four shades followed by LCD off. Done closes the editor and keeps your changes. Selecting a palette or changing host colors does not advance the game.

Import Palette reads `.sbp` files, adds a named library entry using the filename, and selects Custom. The import retains stored RGB samples, including resolved colors from automatic palettes, without regenerating them. Automatic editor settings remain available for later changes. Four-color files use their lightest shade when no off color is stored. A malformed or truncated file leaves the current palette unchanged. Export Palette writes the selected colors and applicable automatic settings to a `.sbp` file. Library names, selection, and editor controls persist across application restarts. The custom colors in the ordinary Settings dialog remain available; later manual edits update the selected library entry when the library is next used.

## Audio and recording

Audio > Mute and the Settings volume slider control host playback. The frontend requests stereo signed 16-bit audio at 44,100 Hz and converts generated samples when the output device needs a different supported format.

Settings > Audio output selects System Default or an enumerated device. A saved device that is unavailable appears as Unavailable device (using System Default); playback falls back while retaining the requested device ID. Device-list changes renegotiate playback while preserving the core rate and active recording. Real hardware unplug/replug behavior still needs broader validation.

Settings > Highpass Filter offers None (Keep DC Offset), Accurate and Preserve Waveform. Accurate is the default. Interference Volume adds modeled console noise from 0% through 100% and defaults to zero. These settings persist across application restarts. Loading a state, rewinding or using Backstep keeps your current choices.

Audio > Audio Channels enables or mutes Square Channel 1, Square Channel 2, Wave Channel and Noise Channel. Alt+1 through Alt+4 are the default shortcuts; Settings can reassign them. Channel choices last for the current application session and also affect recordings. They do not change the game's sound registers. Global listening mute and volume remain separate.

Audio > Start Audio Recording writes stereo 16-bit PCM into a WAVE file. Choose the action again to stop. Recording captures forward handheld output before host volume, mute, and resampling. Pause and rewind add no samples. The adapter introduction and its silent lead-in play through the selected audio device but add no samples to the recording. Reset, Quick Reset, Reload and native console resets keep the recording and its sample rate. Cartridge replacement, closing, or an explicit core sample-rate change ends the file. Loading a state continues the recording with the restored forward timeline.

## Game Boy Printer

With a cartridge open, choose Emulation > Connect > Game Boy Printer, then use the game's print command. The printer feed opens when the game sends an image. Save PNG exports the feed at its original 160-pixel width and closes the feed window. Print opens the system print dialog. Choose Emulation > Connect > None to disconnect. Closing the feed starts a fresh roll when the next image arrives.

Current states retain pending printer packets and jobs when a printer is attached. Loading, rewind and debugger Backstep restore that work while keeping paper already emitted into the feed. Replaying a print request can add another strip. Loading a version 2–11 state, or a state saved without a printer, starts a fresh connected printer. State loading preserves your current connection choice.

Reset clears pending printer work while retaining the attachment and existing paper, including when it refreshes cartridge assets. Opening a separate cartridge or closing the session disconnects the printer. Paper remains host output, and PNG export and system printing use their explicit controls.

The interface reports unavailable audio devices. A successful file recording proves sample capture, not audible host playback quality. Backend idle events and hardware sound differences remain under investigation.

## Screenshots

View > Color Correction offers Disabled, Correct Color Curves, Modern - Balanced, Modern - Accurate, Modern - Boost Contrast, Reduce Contrast, and Harsh Reality. Ambient Light Temperature has 21 steps, with a neutral midpoint. New settings use Modern - Balanced. These controls apply to CGB, CGB compatibility palettes, and SGB output. Native monochrome palettes keep their selected colors. Color correction applies to screenshots and selected inspector previews before frame blending. Raw inspector values remain RGB555.

View > Save Screenshot or F12 saves a PNG beside the cartridge without opening a dialog. The filename contains the cartridge basename and local capture time, such as `game 2026-11-15 13.02.03.png`. Further captures in the same second receive numbered suffixes starting at `2`; existing captures are preserved. A read-only cartridge folder produces an error, so use Save Screenshot As to choose another folder.

View > Save Screenshot As or Shift+F12 opens a PNG destination dialog and can replace a selected capture. Copy Screenshot or Ctrl+Shift+C puts the image on the clipboard. These shortcuts can be changed in Settings. Screenshots use the unblended native image, with the selected monochrome palette, color correction and SGB border visibility. Window scaling, black bars, on-screen messages, menus, and decorations are excluded. Screenshot files are ordinary user outputs, separate from battery saves and states.

## Battery saves, states, and rewind

Settings > Real Time Clock controls timer-equipped MBC3, HuC3 and TPP1 cartridges. Sync to System Clock is the default; scheduled updates use elapsed host time. Accurate uses emulated hardware clocks, so pause, Turbo and slow motion affect its progress. Changing this setting preserves the cartridge's counter and latch values and starts a new timing baseline. Canceling Settings leaves it unchanged. HuC3 has a specific Accurate-mode minute-counter quirk described in [cartridge clocks](rtc.md).

Battery saves preserve a game's own save memory and supported cartridge clock. They use `<cartridge-name>.sav` beside the cartridge. The frontend saves after writes settle and before reset, reload, cartridge replacement or close. These session changes save current clock data even when the game has not written to save memory. Natural clock progression alone does not repeatedly write the file during play. Keep backups before moving saves between versions or tools. [Battery saves](battery-saves.md) explains the exact accepted encodings and recovery workflow.

Native save states preserve execution at a point in time. Use Emulation > Save State or Load State for ten numbered slots. Slot 10 uses the zero key and `.s0` suffix. File > Load State File and state-file drag/drop require the matching cartridge to be open. State loads validate input identity and preserve the session on rejection. Native states do not provide a general state interchange format. See [save states and rewind](save-states.md).

State loading, rewind and debugger Backstep restore saved clock history while keeping your current clock policy. Changing that policy keeps rewind history. Battery imports have separate timestamp rules and can account for elapsed host time even when Accurate is selected.

Hold Tab to rewind. With enough history, the first interval returns to the current frame position, then later intervals move back one frame at a time. Hold Space as well to move back one frame initially, then two per interval. The Turbo cap and slow-motion controls also set replay pacing. Rewind runs each restored interval forward to produce its image and sound using current input. At the end of the available history it waits for you to release rewind. Releasing resumes forward play with current physical input. Replay audio is excluded from WAVE recordings.

Automatic rewind snapshots begin once guest execution resumes after an adapter startup hold. Manual save states can capture the introduction itself and restore its progress.

Settings let you request 10, 30, 60, 120, 300, or 600 seconds of history within a 16, 32, 64, 128, or 256 MiB budget. The default requests 120 seconds within 256 MiB. Select Disabled to stop capture. The oldest snapshots are evicted when either limit is reached; available duration depends on compression.

Rewind changes emulated battery data and can schedule a later battery save. Make an external copy of a save if you need to preserve a particular in-game timeline. The complete snapshot and recovery contracts are documented in the state guide.

## Settings and automation

Tools > Cheats opens the cheat list. Add or edit a rule by hexadecimal address, optional bank, value, and optional original-value comparison. Import Code accepts Game Genie and GameShark codes. The global Enable Cheats control starts disabled. Each rule has its own enabled checkbox. Import File and Export File use `.cht` collections.

Cheat files use the cartridge storage name, such as `game.cht` or `game.zip.cht`. Edits apply immediately and save automatically with atomic file replacement. A failed automatic save keeps the edits active and displays an export warning. An invalid existing cheat file is skipped while the cartridge opens. Reset keeps the current collection, including edits awaiting export. Native states and rewind keep the host's current cheat configuration.

Tools > Cheat Search scans physical work RAM, cartridge RAM, and high RAM. Choose bytes or little-/big-endian words, then narrow by a value or by changes since the previous search. Values are hexadecimal. Add Cheat creates a rule for the selected result, with two banked rules for a word. Write Value changes the actual RAM. Search values come from raw memory. Custom expressions remain pending. [The cheat guide](cheats.md) describes banking, persistence, and API limits.

CupidGB uses Qt's per-user native settings backend with organization and application name `CupidGB`. Settings include window geometry, recent paths, hardware choice, boot directory, bindings, shortcuts, display choices, audio output/volume/mute, and rewind limits. Their physical storage location depends on platform and environment.

`--model` and `--boot-directory` are session overrides for ordinary launches; neither requires a bounded measurement. `--smoke-test` rejects either override so its generated workflows remain controlled.

`--smoke-test` and `--run-seconds` use isolated temporary settings. They do not overwrite normal desktop preferences. For command-line checks, input traces, and bounded runtime reports, see [testing and measurements](testing.md).

Tools > Debugger (Ctrl+C) pauses for CPU stepping, breakpoints, symbols, register/memory commands, and bounded instruction Backstep. Tools > Memory and Graphics opens memory, tile, map, OAM, and palette inspectors. Read [the debugger guide](debugging.md) before editing a session. Advanced debugger commands, paired link sessions, keyboard accessories, custom cheat expressions, and music-file playback remain incomplete.

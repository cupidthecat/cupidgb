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

Emulation > Reset reloads the cartridge and battery data and applies the selected hardware and boot settings. It discards rewind history. File > Close ROM flushes battery data and closes the current session. Failed cartridge or battery loads preserve the current session; failed battery writes are reported and can prevent closing or replacement.

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
| Reset / pause | Ctrl+R / Ctrl+P |
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

Turbo removes normal host pacing while preserving guest hardware execution. Host playback is suppressed during turbo. Slow motion gradually changes the host target to half normal speed and adjusts playback conversion; releasing it returns to normal. Rapid A and B alternate two forward intervals pressed and two released while their assigned keys are held.

Ordinary focus loss releases input so returning to the window does not leave a key stuck. Paused debugger inspection preserves the stopped guest input while releasing physical bindings; normal playback reconciles it before resuming. See [debugging](debugging.md) for that inspection behavior. Click the game area before playing if a dialog or another window has focus.

## Configure keys and shortcuts

Open Emulation > Settings. Each game button and hold mode accepts one key, including a standalone modifier or Tab. Empty bindings disable that input. Command shortcuts may combine a key with modifiers. The dialog rejects duplicate mappings and conflicting unmodified command keys.

Changes apply when you accept the dialog. Hardware and boot settings take effect on the next reset or cartridge open. Cancel keeps the previous settings. Keyboard layouts and platform shortcut handling need broader qualification.

Open Emulation > Controllers for device selection, button/axis/hat capture, dead zones, player assignments, hold controls, command shortcuts, background input and cartridge rumble. Layouts and preferred devices persist separately from cartridge state. Keyboard and controller contributions combine. Single-player sessions accept eligible controllers regardless of their saved SGB player assignment; a multiplayer request switches to the assigned input slots at the next hardware-step boundary. [The controller guide](controllers.md) explains defaults, capture, weak device identities, native menu navigation and remaining physical/motion limits.

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

View > Window Size selects 1x through 8x in logical window units and stores the default scale; new settings start at 2x. High-DPI desktops can map one logical unit to several physical pixels. Integer Scale independently keeps the rendered image on whole physical-pixel factors. Restored window geometry can override the initial size on a later launch. The corresponding settings are Display scaling and Default window size in the Settings dialog.

View > Monochrome Palette offers Lime (Game Boy), Grayscale, Olive (Pocket), Teal (Light), and Custom. Lime is the default. A custom palette uses four `#RRGGBB` entries in Settings, ordered from the lightest shade 0 to the darkest shade 3. Invalid entries are rejected. These choices affect monochrome output; color-hardware compatibility palettes and SGB colors remain owned by the emulated hardware.

View > Frame Blending and Settings offer Disabled, Simple, and Accurate. Simple gives the two most recent completed LCD images equal weight. Accurate alternates the weighting by LCD row and guest frame phase. Both mix in linear light using gamma 2.2. New settings default to Accurate, which uses Simple on SGB and SGB2. Existing on/off preferences keep their previous behavior. Cartridge changes, reset, state loads, rewind, and palette changes clear the frame history.

Settings > SGB display controls whether to show the Super Game Boy border. Hiding it crops the image to the game viewport and does not disable SGB commands or palette behavior. Host palettes and blending do not establish hardware color accuracy. Color correction/temperature and custom palette-file import remain pending. See [the parity checklist](parity.md).

## Audio and recording

Audio > Mute and the Settings volume slider control host playback. The frontend requests stereo signed 16-bit audio at 44,100 Hz and converts generated samples when the output device needs a different supported format.

Settings > Audio output selects System Default or an enumerated device. A saved device that is unavailable appears as Unavailable device (using System Default); playback falls back while retaining the requested device ID. Device-list changes trigger stream renegotiation. This changes host conversion/output, not the core's sample rate or an active recording. Real hardware unplug/replug behavior still needs broader validation. Advanced filtering controls remain pending.

Audio > Start Audio Recording writes stereo 16-bit PCM into a WAVE file. Choose the action again to stop. Recording captures forward core output before host volume, mute, and resampling. Pause and rewind add no samples. Reset, cartridge replacement, closing, or a core sample-rate change ends the recording. Loading a state at the same rate continues the recording with the new forward timeline.

## Game Boy Printer

With a cartridge open, choose Emulation > Connect > Game Boy Printer, then use the game's print command. The printer feed opens when the game sends an image. Save PNG exports the feed at its original 160-pixel width and closes the feed window. Print opens the system print dialog. Choose Emulation > Connect > None to disconnect. Closing the feed starts a fresh roll when the next image arrives.

Reset and successful state loads reset the printer's protocol. Paper already in the feed stays visible. Changing cartridges or closing the session disconnects the printer.

The interface reports unavailable audio devices. A successful file recording proves sample capture, not audible host playback quality. Backend idle events and hardware sound differences remain under investigation.

## Screenshots

View > Save Screenshot or F12 opens a PNG destination dialog. Screenshots use the unblended native image, with the selected monochrome palette and SGB border visibility. Window scaling, black bars, menus, and decorations are excluded. Verify the selected filename before replacing an existing capture. Screenshot files are ordinary user outputs, separate from battery saves and states.

## Battery saves, states, and rewind

Battery saves preserve a game's own save memory and supported cartridge clock. They use `<cartridge-name>.sav` beside the cartridge. The frontend saves after writes settle and before reset, cartridge replacement, or close. Natural clock progression alone does not repeatedly write the file. Keep backups before moving saves between versions or tools. [Battery saves](battery-saves.md) explains the exact accepted encodings and recovery workflow.

Native save states preserve execution at a point in time. Use Emulation > Save State or Load State for ten numbered slots. Slot 10 uses the zero key and `.s0` suffix. File > Load State File and state-file drag/drop require the matching cartridge to be open. State loads validate input identity and preserve the session on rejection. Native states do not provide a general state interchange format. See [save states and rewind](save-states.md).

Hold Tab to rewind; hold Space as well to restore two snapshots per host interval. Releasing rewind resumes with current physical input. Playback during rewind is silent. Settings let you request 10, 30, 60, 120, 300, or 600 seconds of history within a 16, 32, 64, 128, or 256 MiB budget. The default requests 120 seconds within 256 MiB. Select Disabled to stop capture. The oldest snapshots are evicted when either limit is reached; available duration depends on compression.

Rewind changes emulated battery data and can schedule a later battery save. Make an external copy of a save if you need to preserve a particular in-game timeline. The complete snapshot and recovery contracts are documented in the state guide.

## Settings and automation

CupidGB uses Qt's per-user native settings backend with organization and application name `CupidGB`. Settings include window geometry, recent paths, hardware choice, boot directory, bindings, shortcuts, display choices, audio output/volume/mute, and rewind limits. Their physical storage location depends on platform and environment.

`--model` and `--boot-directory` are session overrides for ordinary launches; neither requires a bounded measurement. `--smoke-test` rejects either override so its generated workflows remain controlled.

`--smoke-test` and `--run-seconds` use isolated temporary settings. They do not overwrite normal desktop preferences. For command-line checks, input traces, and bounded runtime reports, see [testing and measurements](testing.md).

Tools > Debugger (Ctrl+C) pauses for CPU stepping, breakpoints, symbols, register/memory commands, and bounded instruction Backstep. Tools > Memory and Graphics opens memory, tile, map, OAM, and palette inspectors. Read [the debugger guide](debugging.md) before editing a session. Advanced debugger commands, cheats, connected link devices, printer support, and music-file playback remain incomplete; absent controls do not imply those features are implemented.

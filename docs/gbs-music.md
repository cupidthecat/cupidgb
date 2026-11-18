# GBS music

CupidGB plays GBS version 1 files through its Game Boy CPU, timers and four-channel audio hardware. Open a `.gbs` file with Ctrl+O, drag it onto a window, choose it from recent files, or pass its path to `cupidgb`. With 7-Zip installed, an archive can contain one supported cartridge or music file. Opening music replaces the current program only after the new file and session inputs are ready.

## Player controls

The compact player shows the file's title, author and copyright text, a waveform and track controls. It preserves Latin-1 metadata, including empty fields. Long text is shortened visually and remains available in its tooltip. Returning to a cartridge restores the saved game-window geometry.

| Control | Behavior |
| --- | --- |
| Track selector | Starts the selected numbered track |
| Previous / Next | Moves through the declared tracks, wrapping at either end |
| Restart track | Starts the current selected track again |
| Pause / Play | Pauses or resumes the current execution |
| Reset | Reloads the file, applies the selected hardware settings and starts its initial track |
| Quick Reset | Reloads the file with the active hardware model and starts its initial track |

Choosing or restarting a track resumes a paused session. A running track change retains already-generated audio and the waveform tail. Reset rereads the source, so changed metadata and the file's initial track take effect together. A failed reload leaves the current program usable. Reload Cartridge is disabled while music is loaded.

Track numbers in the interface start at one. A file declaring zero tracks leaves the selector and previous/next controls disabled. An initial track outside the declared list leaves the selector empty and shows a notice; Restart retains that raw initial choice until a valid track is selected. CupidGB does not invent an extra track or silently clamp the header.

## Audio and display

Volume, mute, output-device selection, high-pass filtering, interference and the four channel toggles use the normal audio controls. WAVE recording continues across track changes and resets. Music has no battery RAM or cartridge-clock save file.

A fresh music session starts with silent audio history. Changing tracks keeps queued samples and lets the new track's register setup affect the ongoing audio stream.

The waveform retains the last 1,024 stereo sample frames and combines their left and right values. It receives core output before host volume and mute, so muting playback does not erase the visualization. Its colors follow the selected monochrome palette. A new waveform is published only after that program completes a presentation frame; an unrelated window repaint does not expose a partial frame.

The music window uses a fixed compact size. Game display capture actions do not expose an old cartridge image while music is loaded. Audio recording remains available. CPU debugging and native execution states operate on the music program's actual guest execution.

## Hardware and state loading

Automatic model selection examines the prepared program's capability bytes. A fixed model preference or command-line override remains available. Music starts at its format-defined entry point without executing a boot ROM. SGB and SGB2 skip their introduction and disable cartridge command reception for music. Timer settings come from the GBS header, including its double-speed request.

Native states use version 23 and require the same loaded music program and effective bank wiring. States contain guest execution, not the source file or the track selector. Loading a state restores its saved execution while retaining the current selected track. Pressing Restart afterward starts the displayed track; Reset returns to the freshly loaded file's initial track. Older cartridge states cannot be imported as music states. See [save states and rewind](save-states.md#gbs-program-identity-in-version-23).

## File limits and qualification

The desktop reads the 112-byte header and at most 4 MiB of payload, including when opening an archive. A larger source has an ignored tail. It validates the header and load address before replacing the live program. The embedding buffer API has a separate, larger bound described in [the core API](core-api.md#gbs-music-programs).

Generated programs exercise loading, track initialization, timers, bank selection, audio and native continuation. Those checks do not establish compatibility with every distributed music file, complete audio equivalence or uninterrupted playback on every host. The existing [Linux host-clock limitation](validation-host-clock.md) also applies to music playback.

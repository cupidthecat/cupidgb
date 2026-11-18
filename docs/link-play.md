# Link play

CupidGB can connect two cartridge windows in the same application. Each game keeps its own display, audio output, battery file and state slots. The connection carries serial cable transfers and infrared changes between the two emulated consoles.

## Connect two games

1. Open the first cartridge.
2. Choose **File > New Window**, or press **Ctrl+Shift+N**, and open the second cartridge there.
3. In either window, choose **Emulation > Connect > Link Cable** and select the other cartridge.
4. Use each game's multiplayer or transfer menu. Follow any connection instructions shown by the game.

For two copies of the same game, put each cartridge copy in a separate writable folder. CupidGB rejects opening the same cartridge or battery storage in two windows, preventing competing writes to one save. Archives retain their extension in storage names, such as `game.zip.sav`.

Choose **Emulation > Connect > None** to disconnect. Closing either cartridge also disconnects the pair. The remaining window continues as an independent session. A failed save that prevents closing keeps the cartridge and connection available for retry.

A console has one serial accessory connection. Connecting a Game Boy Printer replaces that window's cable connection. Connecting another cartridge replaces the printer or previous cable connection.

## Controls for both players

The focused window receives the ordinary game keys. In **Emulation > Settings**, the eight **Link player 2** fields assign keys to the other game. These fields start unassigned. They accept the same single-key bindings as the ordinary buttons, with duplicate and shortcut conflict checks. Accepted input bindings apply to all open cartridge windows.

The player numbers follow the focused window: player 1 is that window, and player 2 is its partner. Switching focus swaps those roles. Focus changes, pause and disconnect release routed inputs so a held key cannot remain stuck in the other game. Opening a menu releases held game controls in both linked windows.

In **Emulation > Controllers**, assign one eligible device to player 1 and another to player 2. During a cable session these feed the focused game and its partner. Each source retains its own button contribution; an inactive partner window does not erase a button routed from the focused window. Device enablement, preferred-device selection and background-input policy still apply. Changes apply across open windows. Each game's cartridge rumble follows its assigned controller, and stopping one motor preserves the other's output. Local SGB player routing and physical-device limits are described in [Controllers](controllers.md).

When background controller input is enabled and no CupidGB window is active, the most recently focused open cartridge retains those player roles. Closing that window transfers background delivery to the next open cartridge in focus order. Keyboard input still releases on focus loss.

## Pause, speed and debugging

Pause affects both linked games. Menus and session operations suspend paired execution until the operation finishes. Turbo or slow motion in either window changes the shared playback rate. The connection's pacing owner supplies the turbo cap; changing that cap from either partner updates the pair. Disconnecting releases turbo and restores independent pacing.

Advance Frame keeps the consoles on a shared timeline and stops at the next presentation boundary or a bounded interval when the LCD is off. The two LCDs can reach their boundaries on different instructions. A debugger operation in either window also advances the peer as needed to preserve cable timing. A breakpoint or watchpoint stops the pair.

Rewind and debugger Backstep are unavailable while connected. Their single-console history cannot restore the other game's matching timeline. Disconnect before using those controls.

## Saves, resets and output

Saving or loading a native state affects the selected console. It preserves the current connection and the other console's running state. It does not provide a synchronized snapshot of both games. Loading an old state during a transfer can therefore disrupt the game's protocol, just as resetting one console can.

Reset, Quick Reset and Reload keep the current connection when the replacement session succeeds. Rejected assets or failed battery writes leave the active session available. Opening a different cartridge disconnects that window before replacing it.

Each window retains its own audio device, volume, mute, channel controls and WAVE recording. Paired execution collects both consoles' generated audio before reporting a recording failure. Screenshots and printer output also remain associated with their originating window.

## Scope

The current connection supports two local windows. Network transport, additional cable participants, keyboard accessories and physical infrared devices remain pending. A working connection does not establish compatibility with every multiplayer game or hardware revision. See the [parity checklist](parity.md) and [link validation record](validation-link.md) for measured coverage and remaining gaps.

The [core API](core-api.md#paired-console-execution) describes instruction scheduling, callback ownership and endpoint replacement for other frontends.

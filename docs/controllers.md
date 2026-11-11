# Controllers

CupidGB uses SDL2 for controller devices and Qt for configuration, menus, display and audio. Open **Emulation > Controllers** to select devices and edit their layouts. Connect a controller before configuring it; the list updates when devices arrive or disappear. Keyboard controls remain available.

## Choose and configure a device

Gameplay devices selects all enabled controllers or one preferred device. Configure device selects the device whose layout you are editing. Its name, connection ID, axis/button/hat counts and mapping status appear in the dialog. The preview shows live raw inputs while gameplay is blocked.

A recognized standardized controller starts with its directional pad and left stick, A/B, Back for Select, and Start. Right shoulder holds Turbo, left shoulder holds Rewind, and Guide opens the menu. Slow motion, rapid buttons and command shortcuts can be assigned. A generic joystick starts with axes 0/1, hat 0, buttons 0/1 for A/B, button 4 for Select and button 6 for Start. Raw layouts vary between devices; verify or replace those defaults before playing.

Select an action row and choose Capture. Press a button, move a stick beyond the press threshold, or move a hat in one cardinal direction. Capture accepts only the selected device. Mapped devices capture standardized buttons and axes. Generic devices capture raw buttons and axes. Both can capture hats. Release the control before capturing another row. Skip cancels capture and advances to the next row. Clear removes the selected action's bindings. Restore layout defaults restores the selected device profile.

Capture replaces a row's bindings. A default direction can contain both a stick direction and a directional-pad binding; those sources combine. Unsupported or absent control indexes remain inactive. Mappings can intentionally overlap, so one control may activate several assigned actions.

Axis press and release thresholds provide hysteresis. The defaults engage beyond 16,384 and retain a direction down to 14,336. Returning below the release threshold releases it. A direction at exactly the press threshold does not engage. The release threshold must not exceed the press threshold. Smaller values react to smaller movements and can expose stick drift. An axis centered at zero releases even with a zero release threshold.

OK applies and saves the draft settings. Cancel leaves the saved configuration and live layout unchanged. The dialog explicitly pauses guest execution, releases gameplay delivery, stops ordinary cartridge feedback and remembers whether the session was already paused. Closing it restores that pause choice. Device discovery and capture continue through the Qt event loop while emulation is paused. Controller game buttons and shortcuts cannot reach the cartridge during capture.

## Gameplay and shortcuts

Buttons, axes and hats contribute independently. Several controllers can hold one game button, and keyboard and controller input combine. Releasing one source cannot release a button another source still holds. Rapid A/B adds its pressed intervals to ordinary button holds; its off intervals do not override another held source.

The action table also includes Menu, hold controls and the application's registered commands. A command binding triggers the existing Qt action once on a new press, subject to that action's current availability. Save/load slots, pause, mute, reset and other listed commands use the same workflows as their keyboard shortcuts. Short presses that begin and end between input polls retain both edges. Enabling delivery after a pause in host input resynchronizes current holds without turning an old press into a new command.

Directional bindings navigate an open native menu or dialog, A accepts, and B or Menu goes back. Turbo and Rewind bindings move to the next and previous native control; the standard defaults use the shoulder buttons. Movement requires a new directional press; stick jitter beyond a held threshold does not repeatedly move the selection. A mapping capture owns its input until completed or skipped. Keyboard and mouse navigation remain available.

Open menus temporarily suspend guest execution and clear queued playback without changing the user's Pause choice. Nested menus keep that suspension active until all menus close. Choosing Pause or resuming through the menu changes the actual pause state and is not undone when the menu closes. Settings dialogs separately save, pause and restore the prior session state.

## Focus and reconnects

Background input is off by default. Losing application focus releases controller gameplay and hold actions. The background option allows gameplay while another application has focus; it does not let a controller feed a settings dialog's game inputs. Keyboard focus-release behavior stays separate. Paused debugger inspection retains stopped guest input while physical controller state continues to update; normal resume and explicit state loading reconcile live input.

Disconnecting a held device releases its contributions. A reconnect gets a new live connection ID and starts from current physical state. Saved layouts use a GUID plus a serial when available, otherwise a device path, otherwise the device class. Paths can change between ports. Identical devices without a serial or path share a saved layout and default player. The dialog retains independent player overrides for current connections; those weak identities cannot promise the same individual assignment after reconnect. Choose the device again if an ambiguous preferred-device match selects another identical unit.

Invalid saved controller data falls back to defaults and reports that fallback. Accepting the dialog replaces it with a valid configuration. Controller identities, layouts and preferences are host settings. They do not enter cartridge saves or native hardware snapshots.

## SGB players

The Player selector assigns the device to one of four input slots. Keyboard input belongs to player 1. While the guest has one player, all eligible controllers feed player 1 regardless of those saved assignments. Disabled devices remain disabled, and a preferred-device choice still limits which devices contribute. Once a Super Game Boy cartridge requests multiple players, devices feed their assigned slots. Current held masks stay available through changes in either direction, and routing updates after the hardware step that changes the player count. Assign distinct devices as needed. This is local SGB input, not cable link emulation or network multiplayer.

The core stores four independent active-low masks. Changing a nonselected mask leaves the interrupt line alone. Selecting a held player's lines can request the normal JOYP interrupt. The hardware's three-count command is also recognized. It selects the first or third slot and leaves that selection stable when the guest polls for the next player.

## Cartridge feedback

Cartridge rumble enables MBC5 motor output on eligible controllers in single-player, or assigned player 1 controllers in multiplayer. Strength scales the requested amplitude. The frontend integrates motor-on time over each forward guest interval, then sends bounded host requests. Normal pause, menus, background deactivation, state loading, rewind, target changes and closing stop or reset host feedback. A later forward interval can resume feedback when the restored cartridge motor latch is on. Host mute and playback volume do not change the cartridge motor.

Test rumble sends a short request to the selected device while the settings dialog owns gameplay input. The test is explicit and still requires application focus. A successful request means the driver accepted it; it does not establish physical force, motor duration or compatibility with every controller.

After a device is already disconnected, SDL may reject a stop command. CupidGB clears the host request, closes its handles and never replays that request on reconnect. A physical motor's behavior after unplugging needs device-specific validation. Software request duration is bounded; this does not prove a disconnected device's firmware obeys that duration.

Synthetic feedback for other games, advanced cartridge motors, physical motion-sensor streams, faux analog direction patterns and analog speed controls remain pending. Recognizing a sensor capability does not enable a physical motion stream.

## Motion cartridges

MBC7 games accept tilt from configured controller axes. Horizontal and vertical tilt have separate selectors in Emulation > Controllers. Defaults use the left stick on a mapped controller, or joystick axes 0 and 1 when available. Unassigned disables that component. The dialog rejects unavailable axes and two selections that use the same physical axis, including mapped/raw aliases. Cancel retains the current layout.

Tilt uses the full axis range without a dead zone. Gameplay button thresholds still apply to ordinary directional bindings. During motion gameplay, the selected tilt axes do not also press directional buttons; digital buttons and hats remain available. With multiple eligible controllers, the lowest live connection ID supplies the cartridge's single sensor. Preferred-device and enabled-device settings still apply, and single-player routing accepts any saved player assignment.

For a mouse, enable Emulation > Motion-Controlled Games > Allow mouse control. It starts off and affects only MBC7 cartridges. The center of the displayed handheld image is neutral. Move the pointer away from the center to tilt. Letterboxing, scaling, borders and display pixel density use the same viewport as rendering. Any mouse button acts as A and combines with the normal keyboard/controller A binding. Start the game's calibration with the pointer centered or the stick released.

A changed controller sample or pointer move becomes the current tilt source. Unchanged controller polling does not replace a newer pointer position. Controller fallback with different coordinates counts as a new sample. Pause, menus, settings and cartridge changes release mouse input; mouse control always requires the active game window. Controller tilt follows the explicit background-input choice. Native state loading restores guest sensor latches while retaining current host tilt and held input. These controls provide analog input; they do not read a controller's physical accelerometer.

## Backend and validation

The backend initializes SDL joystick, game-controller and haptic subsystems, without SDL video, SDL audio or SDL2main. Qt keeps the application entry point and event loop. Input polling uses an 8 ms timer on the application thread. Raw device transport, typed binding evaluation, Qt workflows and the emulation core remain separate.

The backend owns SDL joystick/controller event streams and preserves unrelated events. It retains no emulator pointer. Callbacks are synchronous; destructive or lifecycle work is queued until they return. It closes its own handles and subsystem references, preserving references another SDL client owns. A future SDL client must coordinate event-stream and process-wide hint ownership.

Regression checks use real SDL virtual devices with Qt event loops. They cover multiple and identical devices, normalized and raw input, axis boundaries, hats, transient edges, hotplug, reconnect, capture isolation, Cancel/OK, persistent settings, independent players, focus/background gates, menus, selected-line interrupts, state continuation and virtual rumble callbacks. Application smoke checks use generated cartridges, not commercial game content.

Virtual success does not establish physical device drivers, Bluetooth reconnects, actual motor behavior, physical motion input, controller latency or sustained commercial gameplay. The development WSL environment has no exposed physical input devices. See [testing](testing.md), [the validation record](validation.md) and [build/deployment instructions](building.md) for the tested environments and exact limits.

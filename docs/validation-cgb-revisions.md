# Game Boy Color revisions

Game Boy Color revisions 0, A, B, C, D and E are selectable in Qt and the ROM runner. Automatic color selection and the existing `cgb` option select E. Revision E also retains the model ID used by older save states.

## Measured differences

| Revisions | Extra OAM at FEA0–FEFF with the LCD off | Infrared port after writing C0 with no signal |
| --- | --- | --- |
| 0, A, B, C | Eight writable cells in each 32-address block. Bits 3 and 4 of the address do not select a different cell. | FE |
| D | FEA0–FEBF select 32 separate cells. FEC0–FEFF share the 16 cells selected by the low address nibble. | FE |
| E | Reads repeat the upper address nibble. Writes are ignored. | EE |

The core test writes all 96 addresses on each model and checks both aliases and independent cells. It also checks Quick Reset, exact and desktop state restoration, rejection of damaged states, and all 30 distinct source and receiver revision pairs. The native version 27 record stores the writable cells. The Qt reset workflow checks preservation with and without firmware.

On revisions 0, A and B, an extra length clock can occur when a pulse or wave control write leaves the length-enable bit clear. The preceding enable bit must also be clear, and the next frame-sequencer step must lack a regular length clock. The core test samples 16 phases for all four channels on all six revisions. The noise channel serves as a control.

## Guest and firmware checks

The six generated extra OAM and infrared cases passed using the same minimal synthetic boot overlay on both builds. All six ended at 660 base cycles. CupidGB also passed all six cases using its post-boot profiles, at 624 base cycles.

Windows and Linux wrote matching native snapshots for each of the six revisions. Each host restored its own snapshot and the other host's snapshot, applied the same timed OAM accesses, and saved again. The 12 original snapshots and 24 resumed snapshots matched byte for byte within each revision and stage.

Using a local color firmware image for A through E, both builds passed the boot register and divider tests at matching terminal cycle counts. With a generated revision-0 firmware image, both passed the register test and failed the divider test, with matching counts for both results. A second run with generated firmware on A through E likewise passed the register tests and failed the divider tests on CupidGB. The available fixtures leave revision-0 divider behavior and accurate per-revision startup unqualified.

The profiles share much of the existing color implementation. They still need revision-specific display, DMA, oscillator, startup-memory and audio qualification, and broader infrared measurements. The generated tests cover the stated behavior and leave physical verification open.

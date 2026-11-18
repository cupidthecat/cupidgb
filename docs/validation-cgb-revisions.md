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

## Vertical scroll during a tile fetch

Revisions 0 through C sample the current vertical coordinate at the low and high tile-data address phases. Revisions D and E use the coordinate selected at the tile-map address phase for both reads. The tile-map lookup itself continues to sample the current scroll.

A 96-case comparison varied the revision, normal or double speed, color or compatibility mode, vertical flip, and whether the guest scroll value changed between fetch phases. Before the correction, all 32 cases with a changed scroll value on revisions 0 through C differed on the tile-data addresses and bytes. Afterward, all 96 matched the observed map address, data addresses, bytes and phase clocks. The comparison drove hardware writes directly. CPU-timed scroll writes and collisions with window, object and DMA activity require separate checks.

## Line 153 and STAT interrupts

Revisions 0 through C clear the readable LY earlier at normal speed. The LCD can carry the previous equality result into the combined STAT interrupt while the visible coincidence bit is clear. During the 12-dot LYC write interval after the zero comparison begins, the previous LYC can briefly match line 153 before the new value is compared with zero. The mode 1 interrupt input can hold the combined line high across that write.

The comparison matched 3,600 direct LYC traces across all six revisions and both speeds, including writes at the edges of that interval. All 252 CPU-timed write cases matched the guest's final interrupt flag. A separate 2,592-sample comparison exercised STAT enable changes, overlap with the mode 1 input, and the visible coincidence bit. The regression suite also reloads a native state immediately before the affected write and compares the continuation.

## Digital audio readback

On revisions 0 through C, a pulse step at the final oscillator clock of an audio run can hold the preceding zero on its FF76 nibble. Noise can do the same on FF77 at normal speed. Repeated reads preserve the latch until new clock delivery. The wave nibble and the underlying channel codes keep their current values. Double-speed startup also changes the inactive pulse delay and the noise prescaler reload.

All 576 synthetic cartridge cases matched 128 guest-read values each across the six revisions. Cases cover isolated pulse, wave and noise channels, two pulses together, and wave mixed with noise, with eight instruction alignments at both speeds. The same guest values repeated at 8, 48 and 192 kHz host output rates. Native continuation tests cover a pending pulse edge, an already latched read, a desktop load with a different receiving rate, and the version 27 import.

A separate set of 1,152 noise cases matched all 128 reads per case. The set covers all eight prescaler codes, four instruction alignments before the trigger, both speeds, and cold, DAC-off background and active starts. The earlier implementation differed in 100 cases across the three double-speed groups. Native states now accept the longer 35- and 38-tick countdowns and resume ticking from those values. Other selector settings and additional retrigger alignments still need qualification.

## Pulse and noise envelopes

Both pulse channels and noise sample the envelope endpoint on the rising audio divider edge. They update volume on the falling edge. At double speed, revisions D and E defer the update by four CPU T-cycles after a natural fall or eight after a DIV write. Writing DIV again restarts the pending delay. Revisions 0 through C update immediately and can hold some PCM bits low until the next CPU delivery. Repeated reads during the same delivery return the same bits.

All 576 falling-edge and 396 register-write guest cases matched 128 PCM reads each. The comparisons cover all six revisions and both speeds, including the revision 0 pulse-2 transition, active noise writes, and endpoint lock. Another 288 cases matched volume and PCM observations at each CPU T-cycle from the triggering edge through the following 12 cycles. Regression tests exercise repeated DIV writes, exact and desktop continuation, and version 28 import.

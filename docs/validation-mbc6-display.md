# MBC6 banking, flash and display filters

MBC6 now maps two independent 8 KiB program windows and two independent 4 KiB SRAM windows. Each program window can select the shared flash chip. The chip-enable gate controls bus access, while the write-protect gate limits operations on sector zero and hidden storage. The fixed ROM window stays in place. The board population and register behavior come from the [cartridge inspection](https://gbhwdb.gekkio.fi/cartridges/CGB-BMVJ-0/gekkio-2.html) and [MBC6 hardware notes](https://gbdev.io/pandocs/MBC6.html).

The generated DMG and CGB cases execute guest ROM and SRAM accesses, select every ROM and SRAM bank on the documented board, and exercise ID mode, 128-byte programming, aborted writes, sector and chip erases, and hidden data. The test also checks both reset kinds, battery round trips and rejection, native and live continuation, and import of the previous single-window state layout. The cartridge geometry suite covers alternate ROM populations and erased file tails. Debugger edits use the selected 4 KiB SRAM window even while guest SRAM access is off.

Version 25 captures the flash contents and partial command transaction. Battery files preserve committed flash, protection, and SRAM. See [save states](save-states.md#mbc6-flash-in-version-25) and [battery saves](battery-saves.md) for the formats.

MBC6 has no working controller in the pinned comparison build. The local library has no matching commercial cartridge. Flash completion currently reports ready or timeout without modeling elapsed chip time. Power-on selectors, undocumented register bits, protection status, command timing, and the physical cartridge workflow still need hardware qualification. TAMA5 and Mobile Adapter transfers remain open.

Qt exposes Nearest Neighbor, Bilinear, and Smooth Bilinear through View and Settings. The component test checks pixel-center interpolation in linear light, edge clamping, native-size identity, and solid colors including the darkest shades. The desktop workflow checks visible widget rendering, resize and cache invalidation, persisted settings, Cancel and OK, and unfiltered native-size clipboard and screenshot output. Other scaling filters and the interaction with row-weighted frame blending remain open.

## Tests

Windows Release passed 103 checks. Linux Release passed 101 checks. Linux AddressSanitizer and UndefinedBehaviorSanitizer passed 74 core checks. Native Qt desktop smoke tests passed on Windows and WSLg. The WSLg run required a real audio sink. The external ROM runner passed 177 cases, including 56 mapper tests. All 121 paired firmware cases matched the pinned comparison revision's terminal cycle counts. The 177 cases also retained their previous instruction counts, guest cycle counts, framebuffer checksums, and audio checksums. The external suite does not exercise MBC6.

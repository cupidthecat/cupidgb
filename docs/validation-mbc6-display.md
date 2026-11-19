# MBC6 banking, flash and display filters

MBC6 now maps two independent 8 KiB program windows and two independent 4 KiB SRAM windows. Each program window can select the shared flash chip. The chip-enable gate controls bus access, while the write-protect gate limits operations on sector zero and hidden storage. The fixed ROM window stays in place. The board population and register behavior come from the [cartridge inspection](https://gbhwdb.gekkio.fi/cartridges/CGB-BMVJ-0/gekkio-2.html) and [MBC6 hardware notes](https://gbdev.io/pandocs/MBC6.html).

The generated DMG and CGB cases execute guest ROM and SRAM accesses, select every ROM and SRAM bank on the documented board, and exercise ID mode, 128-byte programming, aborted writes, sector and chip erases, and hidden data. The test also checks both reset kinds, battery round trips and rejection, native and live continuation, and import of the previous single-window state layout. The cartridge geometry suite covers alternate ROM populations and erased file tails. Debugger edits use the selected 4 KiB SRAM window even while guest SRAM access is off.

Version 25 captures the flash contents and partial command transaction. Battery files preserve committed flash, protection, and SRAM. See [save states](save-states.md#mbc6-flash-in-version-25) and [battery saves](battery-saves.md) for the formats.

MBC6 has no working controller in the pinned comparison build. The local library has no matching commercial cartridge. Power-on selectors, ID address aliases, undefined status bits, elapsed command timing, and physical cartridge workflows still need hardware qualification. TAMA5 and Mobile Adapter transfers remain open.

Qt exposes Nearest Neighbor, Bilinear, and Smooth Bilinear through View and Settings. The component test checks pixel-center interpolation in linear light, edge clamping, native-size identity, and solid colors including the darkest shades. The desktop workflow checks visible widget rendering, resize and cache invalidation, persisted settings, Cancel and OK, and unfiltered native-size clipboard and screenshot output. Other scaling filters remain open.

## Tests

Windows Release passed 103 checks. Linux Release passed 101 checks. Linux AddressSanitizer and UndefinedBehaviorSanitizer passed 74 core checks. Native Qt desktop smoke tests passed on Windows and WSLg. The WSLg run required a real audio sink. The external ROM runner passed 177 cases, including 56 mapper tests. All 121 paired firmware cases matched the pinned comparison revision's terminal cycle counts. The 177 cases also retained their previous instruction counts, guest cycle counts, framebuffer checksums, and audio checksums. The external suite does not exercise MBC6.

## Flash program buffer and protection

The flash reports ready while collecting program data and after a protected
operation has finished. The 128-byte buffer accepts bytes in any order.
Repeating the last buffer index selects the destination page and starts
programming. Repeating that index with `F0` aborts instead. The hidden map uses
the same buffer and selects one of its two 128-byte blocks. Flash write
protection suppresses the hidden erase, hidden program and sector protection
commands. The last protect or unprotect write selects any address in sector
zero. [Physical flash command and status measurements](https://iceboy.a-singer.de/doc/np_gb_memory.html#flash_commands)

DMG-ABC and CGB-E tests cover the guest ready loop, sparse programming across
pages, the hidden map, protected writes, `F0` as both data and abort, and
battery dirty tracking. Sixteen pending states from the earlier executable
reopened and saved byte for byte before the next write. Each completed
correctly after resuming. The state layout also preserves a sparse pending
buffer across a save and load. Programming and erasing still complete
immediately in the emulation.

## Frame blending with bilinear filters

Bilinear and Smooth Bilinear now filter the current and previous images in
linear light before blending them. Accurate mode uses the nearest source
row to choose the previous frame's weight. This keeps the alternating LCD
row weights distinct through scaling. Simple mode uses an even mix.

The scaler caches the two horizontally filtered rows used by consecutive
output rows. At 8×, the component took 7.67–8.02 ms per frame across three
synthetic patterns on WSL. Two adjacent Donkey Kong framebuffer captures
took 3.90–3.95 ms per frame at 8× across the two filters, including the
native-size blend. Unblended output matched the earlier scaler across
24,341,056 pixels.

The Qt workflow paints both filters with each row polarity and changes the
viewport size. Closing a session clears both frames. Screenshot exports use
the native-size image. Widget checks also covered device pixel
ratios of 1 and 1.25.

## Nearest scaling at fractional viewport sizes

Nearest Neighbor samples the source pixel that contains each output pixel
center. At an exact tie it selects the pixel with the larger coordinate.
Accurate blending uses the weight that belongs to that source row. At
exact integer multiples in physical pixels, the widget draws the source
image directly and skips the scaled image cache.

An offscreen Qt comparison checked source coordinates and viewport
coverage across 696 cases at device pixel ratios of 1, 1.25, 1.5 and 2.
The cases covered Fill, Aspect and Integer modes for GB and SGB image
sizes. All 158,967,683 sampled pixels matched the pixel-center calculation,
and the viewport coverage matched the previous paint path. The desktop
workflow checks tagged source rows and columns at two window sizes.

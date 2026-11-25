# Initial display sizing, 2026-11-16

The Qt window now creates its status bar before calculating the initial guest
canvas size. Previously, opening the first cartridge could create the bar after
that calculation and take height from the display. On the tested Linux desktop,
the resulting canvas could fit only a 1x integer image despite the 2x preference.
Saved window geometry still takes precedence over the default size.

This correction follows clock checkpoint
`04da2e8310e7e604d58804b440c703c419bd1779`. The two changed build inputs are
`main_window.cpp` and `main_window_session_tests.cpp`. The 208-input source
manifest has SHA-256
`33280bfc28fe941691a773cdd9753eb6ee40f3074222b899a3b6ed3d4a62f104`.
The combined validation receipt has SHA-256
`d0cff3e2d1a197de988aa5a910256713f2a5952f9af9feb885537d161631fd13`.
Both core archives and both ROM runners retain their clock-checkpoint hashes.

## Regression and installed checks

The workflow checks the canvas before and after the first cartridge open. Both
dimensions must accommodate a 320 by 288 logical image at the 2x default. A
separate fixture saves a 517 by 389 custom window size and checks that a normally
constructed window restores it. Existing explicit-scale, centering, fullscreen,
custom-size and SGB transition assertions remain active.

A private Linux executable links the retained old constructor with the new
workflow fixture. Compilation and linking succeed, then the new first-cartridge
assertion rejects a 320 by 266 canvas. The original wrapper expected the earlier
empty-window diagnostic and therefore also failed its classifier. Its original
logs and failure remain intact; a separate interpretation identifies the actual
new assertion. No application rerun was used to change that result. All 54
protected link inputs, 208 source inputs and both core archives remain unchanged.

| Check | Windows | Linux/Xcb |
| --- | --- | --- |
| Release build | Passed | Passed |
| Display pipeline, Qt CLI and Qt workflows | 3/3 passed, 37.69 seconds | 3/3 passed, 48.69 seconds |
| Installed native UI workflow with required real audio | Passed, 18.91 seconds | Passed, 27.42 seconds |
| Retained native UI captures | 11 | 11 |

The fresh installations contain 295 files on Windows and 22 on Linux. The
native runs verify the expected runtime paths and complete under the unchanged
180-second guard. Source and installed-file hashes remain unchanged throughout
the checks. The installed Windows executable has SHA-256
`d0d725af026f8a272b73311d737e3c8761a347a0d80b4e87a7a1e6bb7e3ea84d`.
The installed Linux executable has SHA-256
`4270f9542ce3244e14368f1e2c8477526cf1d35f48df9c3e963e2c5b104fbc90`;
installation removes the build's runtime search path, so its hash differs from
the build executable.

## Visible sizing result

A generated solid-frame cartridge runs for three application seconds with
isolated default settings, the DMG profile and startup seed zero. Its raw guest
capture is identical before and after the correction on both hosts. Window
captures distinguish the rendered image from menus, status text and black bars.

| Host | Guest image before | Guest image after | Window capture before | Window capture after |
| --- | --- | --- | --- | --- |
| Linux/Xcb | 160 by 144 | 320 by 288 | 320 by 307 | 320 by 329 |
| Windows, 120 DPI | 320 by 288 | 320 by 288 | 483 by 401 | 483 by 428 |

The captures show the full guest image with readable menus and status text.
Windows retains integer physical-pixel scaling on this desktop; the logical
window preference and resulting physical-pixel factor are distinct. The Linux
capture now fills the intended 2x guest canvas.

## Scope

The comparison source revision remains
`ea667ad79aa28409f034b1fd6da7304e1844750d`. Its default-scale and resizing rules
were inspected, but this correction has no paired native reference-UI run.
The checks above qualify the tested local Windows and Linux environments, not
every desktop theme, DPI setting or window manager.

No 120-second gameplay measurement was repeated for this geometry correction.
The earlier installed folders and performance receipt remain unchanged, and the
[clock validation record](validation-rtc.md) retains its original render-size
mismatch. Its timings do not become measurements of these new executables.
The unchanged cores retain their earlier validation evidence; this focused
frontend run does not add hardware, mapper, audible-quality or full-speed
qualification.

## Super Game Boy border modes, 2026-11-24

The reference at `ea667ad79aa28409f034b1fd6da7304e1844750d` reported 160 × 144 for
handheld profiles in SGB Only and Never, and 256 × 224 in Always. Always asked for
SGB donor firmware once for an eligible cartridge. The external SGB profile
reported 256 × 224 in Always and left the surround to its caller. Qt fills this
area with black. Handheld sessions use a CupidGB bezel until a border is ready.

The production renderer matched the full 34,304-pixel surround for a generated
donor and a commercial Donkey Kong cartridge. The comparison used linear and
corrected SGB colors on Windows and Linux. All eight samples matched, and all
23,040 viewport pixels retained their sentinel. The comparison receipt SHA-256 is
`2a9b1794edb9213ae31f8b0941ad34a3895f43d29e14d157fe29a409c6e3244d`.

The Qt workflow uses generated cartridge and firmware bytes. It checks both
settings controls, migration from the old checkbox, native dimensions, integrated
overlays, the handheld and external-host surrounds, and borrowing while paused.
It checks that in-flight results from a prior cartridge are discarded and that
the live guest state stays byte-identical during the donor run. It also covers
cache retention through resets, a model change, native state loads and a failed
open, plus cache removal on close. The core render and capture tests passed under
ASan and UBSan. The complete Release suites passed 111/111 on Windows and 109/109
on Linux. The Windows run used the working local 7-Zip executable. The Linux run
used an isolated PulseAudio sink.

## Pixelated and Scale filters

The comparison at `ea667ad79aa28409f034b1fd6da7304e1844750d` uses two generated
7 × 5 RGB frames with different diagonal patterns. It checks reduction, native
size, fractional enlargement, 2× and 4×, with blending disabled, Simple, and
both Accurate LCD phases. Windows and Linux matched all 120 RGBA outputs for
Pixelated, Scale2x, Scale4x, and both antialiased variants.

A second Linux comparison used two 160 × 144 frames from Super Mario Bros.
Deluxe. It checked a reduction to 127 × 117, native size and enlargement to
733 × 551. Fifty-two of 60 outputs matched exactly. Every remaining channel
value differed by at most one on the 8-bit scale.

The renderer keeps edge decisions separate for each frame, then mixes the
selected colors in linear light. Native-sized Scale filters still sample the
edges. The Qt workflow covers settings persistence, fractional device-pixel
rendering, the integer Pixelated fast path and native screenshot dimensions.
Release passed 111 tests on Windows and 109 on Linux. The display-pipeline
tests also passed under ASan and UBSan on Linux.

A Linux rendering benchmark used two generated 256 × 224 frames with Accurate
blending at 1919 × 1079. Reusing sampled rows and looking up blends for flat
pixels lowered the median render time from 24.8 to 7.8 ms and p95 from 36.7 to
12.5 ms across alternating runs. These figures time the filter itself.

### Parallel Scale rendering

The four Scale filters can split an output of at least 160,000 pixels into
independent row bands. The caller renders part of the image and waits for every
band. The shared pool provides at most two workers. When both are busy, the
caller renders the remaining bands itself.

The regression test checks complete RGBA output against the serial renderer on
both sides of the threshold and at integer and fractional sizes. Eight callers
start together to exercise a busy pool. The test also checks both input images
for mutation after rendering. All 68 generated captures matched on Windows and
Linux, including repeated rows across band boundaries. A separate Linux run
matched all 60 retained Mario output frames byte for byte. The display tests
passed ASan and UBSan on Linux. Full Release passed 111 tests on Windows and
109 on Linux.

The native replay used Super Mario Bros. Deluxe on CGB-0 with antialiased
Scale4x, Accurate blending, a 733 × 601 window, audio and rewind. Each host
ran the same 30-second input trace twice per build in serial, parallel,
parallel, serial order. The table shows medians after the five-second warmup.

| Host | Serial guest speed | Parallel guest speed | Serial widget paints/s | Parallel widget paints/s |
| --- | ---: | ---: | ---: | ---: |
| Windows | 93.48% | 99.99% | 28.07 | 50.35 |
| Linux/Xcb | 99.99% | 99.99% | 59.89 | 59.88 |

The median Windows p95 paint interval fell from 46.9 to 36.2 ms. Median peak
working set was 77.8 MiB serial and 79.7 MiB parallel on Windows, and 97.5 MiB
serial and 97.0 MiB parallel on Linux. Every run had zero audio idle events
and errors after warmup. The isolated Linux PulseAudio sink captured nonzero
game samples in every run. Guest speed and widget paints measure different
parts of playback.

## LCD display filters

LCD Display uses two horizontal taps per channel. It evaluates the vertical
and temporal weights once per source column for each output row. Monochrome
LCD combines a pixel grid, bloom, and a shadow. Its shadow depends on the
brightness of each frame, so the renderer applies it before frame blending.
The monochrome renderer processes color channels together and reuses the
current-frame calculation where every sampled source row is unchanged.
Both filters use the shared row worker pool for large outputs.

The comparison at `ea667ad79aa28409f034b1fd6da7304e1844750d` checked six output sizes with blending disabled,
Simple, and both Accurate phases. All 48 generated RGBA outputs matched
exactly on Windows and Linux. Regression tests also cover single-pixel
dimensions, unusable frame history, and eight callers rendering concurrently
without modifying either input image.

A Linux comparison used two pairs of 160 × 144 frames from Super Mario Bros.
Deluxe, including a scroll in the first stage. It checked both filters at
127 × 117, native size, and 733 × 551. Twenty-eight of 48 outputs matched
exactly. Each remaining output differed in at most three channels, each by
one 8-bit level. The Qt display tests passed ASan and UBSan on Linux.

The Qt workflow checks both menu and Settings selections, settings reload,
blending with separate frames, and native-size screenshots. It passed at the
default device scale and at 125% on both platforms. Full Release passed 111
tests on Windows and 109 on Linux.

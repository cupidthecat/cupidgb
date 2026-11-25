# Hardware and playback validation

This checkpoint adds GBS playback, a PAL adapter profile, monochrome OAM read corrections, a quiet-blanking optimization and wider MMM01 coverage. Hardware comparisons use revision `ea667ad79aa28409f034b1fd6da7304e1844750d`. ROMs, firmware, native states, recordings and captures used in the checks stay private.

## Build and regression checks

| Gate | Passed |
| --- | ---: |
| Windows x64 MSVC Release with Qt 6.8.3 | 101/101 |
| Linux x64 GCC Release with Qt 6.8.3, run in WSL | 99/99 |
| Linux Debug with ASan, UBSan and leak detection | 72/72 |
| External mapper, timing and serial cases | 99/99 on each platform |

The 99 external cases use 49 distinct inputs. They include 56 mapper cases, timer and interrupt checks, two serial checks at post-boot entry and one serial check with shared DMG firmware. All deterministic result fields match the preceding checkpoint. The [MMM01 checks](validation-mmm01.md) also match all 1,280 generated scenarios across both layouts and four state continuations.

Native Qt workflows passed with real audio sinks on Windows and WSLg. Reviewed captures include the GBS transport and waveform, adapter startup, linked windows and display tools. The combined adapter state check covers 240 pulse continuations across native versions 22 and 23, 840 generated older imports and 260 rejection checks. [SGB PAL validation](validation-sgb-pal.md) and [GBS validation](validation-gbs.md) record their individual checkpoints and earlier failures.

A separate seven-second native GBS run selects PAL on each host. Both use the 4,256,274 Hz base clock and the fixture's double-speed mode, publish a visible waveform and report actual sink consumption with zero audio errors.

## OAM and blanking

The CPU bus applies monochrome OAM read corruption once for each qualifying access. Passive debugger reads leave OAM alone. The OAM module owns the row effects and address-selected reads at the ends of the scan. The original guest matrix checks ordinary reads, incrementing and decrementing reads, idle and WRAM controls on DMG, Pocket and native CGB. The first candidate matches 348 of 360 full arrays and exposes two monochrome boundary differences. The corrected candidate matches all 360 arrays. A separate direct comparison matches 19,456 normal-row observations and 3,112,960 OAM bytes.

The older finite OAM timing-effect ROM remains bounded on baseline and candidate at 252,806,400 guest clocks and 38 VBlanks. Both return the same cached memory status and final image and audio checksums. A full OAM suite pass still needs a terminating result and broader write, stack and DMA timing coverage.

The LCD scheduler advances the interior of a quiet blanking interval as one span. It retains per-dot processing around line boundaries, interrupts, transfers and DMA delays. The regression compares complete state after batched and one-dot delivery on monochrome, normal-speed color and double-speed color hardware. Two 900-interval commercial headless workloads match baseline steps, guest clocks, VBlank and presentation counts, final image and audio checksums on both hosts. Timings vary with concurrent host work, so these runs establish output equality rather than a stable throughput estimate.

## Commercial sessions

Each native Qt session runs for 60 seconds with normal audio, a fixed startup seed, post-boot entry and a guest-clock input trace. All 117 Tetris and 123 Super Mario Bros. Deluxe input events are applied on each host. Reviewed captures show falling blocks and the opening platform stage. The copied color battery opens and flushes successfully. Its bytes match the supplied save after these sessions.

| Host | Game | Guest speed after warmup | Widget paint interval p95 | Peak resident memory |
| --- | --- | ---: | ---: | ---: |
| Windows | Tetris | 100.010% | 18.10 ms | 75.9 MiB |
| Windows | Super Mario Bros. Deluxe | 99.996% | 18.61 ms | 133.9 MiB |
| WSLg | Tetris | 97.999% | 34.70 ms | 84.1 MiB |
| WSLg | Super Mario Bros. Deluxe | 97.997% | 26.55 ms | 138.9 MiB |

All four samples report zero audio error events and zero post-warmup idle events. The native sink reports about 60.01 seconds processed on Windows and 58.74 seconds on WSLg. The guest clock runs below nominal speed in both WSLg cases. Widget intervals describe Qt paint events. Windows working-set peaks and Linux maximum RSS use their respective process counters. Linux uses 16.02 and 19.89 CPU seconds for the two sessions. Windows CPU samples varied substantially and do not establish a comparable result. [Host-clock measurements](validation-host-clock.md) record the earlier independent audio clock diagnosis.

The core also captures both games at 1,040 intervals and continues them through 1,800 under native version 23. Windows and Linux initial states, audio and images match. All four sender/receiver routes for each title re-encode the input state byte for byte after load. Their final states, PCM and framebuffers match, including battery bytes for the color title. These are controlled core replays. The native frontend workflow separately exercises slot loading, rewind and battery transactions.

The local library scan repeats the fixed-seed 420-interval startup check on 878 distinct inputs. The same 874 reach the bound and the same four CGB inputs stop at identical illegal instruction addresses and opcodes. Image checksums, clock totals and all other reported guest fields are unchanged. Audio checksums differ on 83 DMG and 57 CGB cases. Repeating those 140 inputs with the isolated GBS and audio build reproduces every changed checksum. The scan covers startup and the first few seconds of execution. It cannot establish successful gameplay or compatibility for the full library.

## Private receipt identifiers

| Record | SHA-256 |
| --- | --- |
| External suite and input guard | `10c509e0930b14269c2fdbf72fbbd74203a510755eacda95b6f5e2204e10bf8f` |
| Commercial state exchange | `4a03ebde26c7773c0746112e60c5a60c58a79a8340c885a2dc1435794d51712b` |
| Local library startup scan | `d53c2372f07f930fbee4085b725ac4c0255b99bcd2f2ba85756aedaa1a74bcb6` |
| OAM guest comparison | `04cd8e60a11ebba817f93c14df0e98741487f32e5697b18820dd342384c012cd` |
| Finite OAM timing-effect baseline and candidate | `446b6a21ad88b89564766fc9d75e3caaab087ec539f233d45ea1ec3c7d70d912` |

Remaining requirements include model-specific firmware and startup behavior, wider graphics and audio comparisons, complete mapper coverage, physical peripherals and correct sustained Linux playback. See the [parity checklist](parity.md).

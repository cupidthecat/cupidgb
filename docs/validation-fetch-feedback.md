# Fetch-row and controller feedback checkpoint

This checkpoint covers CGB tile-row retention, native version 17 states and cartridge motor sampling. Complete hardware and desktop parity remains open. The comparison source revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`; private manifests retain source, executable, cartridge, firmware and input identities.

## Behavior

The current CGB profile retains the vertical row selected when the fetcher addresses the tile map. Writes to SCY between that address and the two tile-data reads no longer change either plane's row. This also applies when the physical CGB runs a monochrome cartridge. Monochrome hardware retains its separate live-SCY behavior. Window rows and vertical tile flipping have their own regression coverage.

Native version 17 stores that retained row in the pixel timing record without increasing the state size. Versions 2 through 16 remain readable. Their missing row is reconstructed from saved window or line/SCY state. Versions 15 and 16 must still have zero in their former reserved byte; a nonzero byte is rejected before changing the running core. An older state cannot recover a row that its format never recorded after an intervening SCY write.

The motor observer receives one strength sample before the first display work in each delivered hardware span. Unequal spans have equal sample weight, fully frozen spans contribute none, and a speed transition can split one request into two deliveries. The observer is host state: registration survives reset and state loading, is excluded from snapshots, and does not advance the guest. A second host observer reports each presentation after its handheld pixel buffers are finalized; surrounding CPU, interrupt and SGB host-frame work can still be pending.

Qt registers these observers only for motor-capable cartridges. Each presentation closes the current sample average; later deliveries in the same instruction contribute to the next interval. Qt queues the finished averages and sends device requests after execution returns, applying the nonlinear strength setting without the former 16 ms throttle. During playback, a step without a presentation retains its partial average. Reset and timeline changes clear both partial samples and queued requests. Paused debugger execution discards that feedback after each slice, including stops and early exits.

The controller dialog treats a 0% test as a stop request and asks the user to increase Strength. It no longer reports that it sent a pulse. Separate negative controls reproduce the misleading message and retained debugger feedback before their fixes. Generated-cartridge and virtual-device regressions cover Step, multi-slice Next/Finish, resume, idle and active zero-strength tests, positive requests and focus rejection.

The resume check also retains samples delivered after a presentation within the same instruction. Its first nonzero interval includes two earlier zero-level samples. The regression requires amplitude 43,685 for that mixed interval and 43,690 for the following constant interval. The initial test expected the constant value one interval too early. A retained diagnostic confirmed the sample counts, completed mean, successful frame execution and open delivery gates before correcting that expectation.

## Builds and runtime checks

| Environment | Configuration | Result |
| --- | --- | --- |
| Windows x64, MSVC 19.43, Qt 6.8.3 | Earlier Release build with IPO, SDL2, Qt and core | 72 of 72 CTest checks passed before the final dialog/debugger review |
| Windows x64, final local source | Release, IPO, one compiler job | The corrected resume regression could not be rebuilt locally: MSVC stopped with `C1060: compiler is out of heap space` before the test suite |
| Ubuntu under WSLg, GCC 13.3, Qt 6.8.3 | Earlier Release desktop build with IPO and SDL2 | 69 of 69 CTest checks passed before the final presentation-observer integration; final desktop revalidation remains pending |
| Linux GCC | Debug core, AddressSanitizer, UndefinedBehaviorSanitizer and leak detection | 51 of 51 checks passed |
| Linux GCC, portable hashing | Debug with the same sanitizers | State, SHA-256 and core interoperability checks all passed |

Earlier Windows and Linux executables passed the native Qt workflow suite while requiring a real audio sink. Each produced fifteen private captures, including settings and debugger images that were visually inspected. The checks exercise reset, state loading, audio restoration, recording, input, controller routing, inspectors and motor behavior. The Windows receipt predates the final dialog/debugger review. The Linux receipt predates the final presentation-observer integration. Neither receipt qualifies its newer desktop executable.

The first Linux native attempt failed because the WSLg audio-server socket was absent. Its receipt remains in the private evidence. A later run, after the real server became available and with the live runtime directory selected, passed without changing product code. Neither a successful sink request nor the absence of backend errors establishes audible quality on every device. Physical controller motors and device-disconnect behavior remain unqualified.

The final local Linux rebuild linked the core, runner and Qt executable, then stopped after 80 of 175 build steps. It did not reach the full test suite. Recovery attempts subsequently failed in WSL with RPC/GPU setup errors and `Wsl/Service/E_UNEXPECTED`; a brief successful startup did not restore reliable command execution. The final core archive retains the hash used by the state-continuation probes below. The newer desktop executable still needs a completed build/test run and native audio workflow check.

Windows compilation used one Ninja job and one [LTCG code-generation thread](https://learn.microsoft.com/en-us/cpp/build/reference/cgthreads-compiler-threads). Release interprocedural optimization remained enabled. Earlier attempts encountered compiler heap exhaustion and a code-generation stack overflow; a broad optional dependency search also picked up incompatible headers. The retained failure logs identify those attempts. The final configuration selects the Qt SDK and SDL2 package explicitly and limits build concurrency. It does not change guest timing or disable audio.

The post-review Windows gate passed 71 of 72 checks before reaching the resume test's incorrect constant-amplitude expectation described above. After that expectation was corrected, the next compilation failed with `C1060`. The host had about 1.7 GiB of remaining commit space at the subsequent inspection, despite having free physical RAM. Hosted checks for the committed source are recorded on the draft pull request. Final local native workflows and new throughput/runtime measurements remain pending; older measurements retain their original executable identities.

## Paired fetch comparisons

An original private probe compared 220 fetch cases with the pinned source build. They cover the current CGB model in color and monochrome compatibility modes, normal and double speed, unchanged and changed SCY values, vertical flipping, wrapping rows and monochrome controls. Every paired map address, low/high data address, fetched byte and subsequent map address matched. Forty-four additional comparison-only cases confirm that an earlier color revision still reads live SCY; they do not qualify that revision as a supported CupidGB profile.

The product regressions also cover window rows, direct pixel execution, scheduled display delivery and save/load while a fetch is pending. A negative control against the earlier implementation exposes the row-selection failure. These address and continuation checks do not establish complete frame-image or FIFO timing parity.

## Paired motor comparisons

| Probe | Paired cases | Observations |
| --- | ---: | --- |
| Instruction delivery | 3,072 | Sample count, program counter and reported clock progress |
| I/O instruction delivery | 10,062 | The same observations across thirteen instruction forms and 129 I/O addresses |
| HALT, STOP, interrupts, DMA and speed transitions | 349 | Sample count, effective strength sum, program counter and clock progress |
| MBC5 guest motor writes | 3,072 | Samples before and after real controller writes |
| Graded controller guest motor writes | 3,072 | Samples before and after changing motor levels |

All 19,627 paired cases matched sample counts and program counters. All 6,493 cases that also record effective strength sums matched those sums. Eighty speed-transition cases differ by one reported half-dot clock unit. The probe compares CupidGB's whole-dot elapsed counter, multiplied by two, with a native half-dot counter. Every differing comparison value is odd, and every product value is even. The other recorded clock results match.

CPU and DMA delivery paths round odd half-dot spans differently, so a guest phase error remains possible. These cases inject countdown fields directly with the LCD off; 54 satisfy the individual state-field bounds and 26 exceed them. Those bounds do not establish that real guest STOP execution can produce each combination. The next comparison must drive both speed transitions through guest instructions and trace display, interrupt and DMA timing through subsequent instructions. The retained motor results do not qualify complete timing equivalence.

An unequal-span fixture distinguishes the corrected mean, two thirds, from the previous time-weighted result, about 0.7333. Qt checks use an SDL virtual device to verify constant and changing levels, motor capability, zero/half/full strength, rapid requests and restoration after reset, state loading and pause. Virtual-device results establish the requests sent by the application, not the response of a physical motor.

A second paired probe runs a generated program that changes motor levels through guest writes. Forty-eight initial instruction phases are exercised for each combination of DMG, normal-speed CGB and double-speed CGB with the LCD on or off. Three presentations per case produce 864 intervals. All sample counts, strength sums and normalized amplitudes match. The LCD-off cases reach their boundaries through normal clock delivery with the scheduler's retained state intact.

The earlier end-of-instruction flush differs in 324 of the 432 LCD-on amplitude observations; the presentation observer removes those differences. Internal program-counter values observed inside the two callbacks differ in 408 of the 864 current intervals. The probe qualifies motor grouping and output, not complete CPU tracing or equality of transient register state. Other hardware revisions, wider display interactions and physical motor behavior remain outside these results.

## Cross-platform state continuation

The final core libraries produced sixteen native-version-17 captures: DMG, native CGB, CGB monochrome compatibility and SGB, at two execution points on each host. The capture targets were 21,067,200 and 126,403,200 base cycles, equivalent to 300 and 1,800 nominal 70,224-cycle intervals. These later checkpoints use deterministic button traces, seed zero and a fixed clock provider. They extend the earlier startup-only checks.

All eight Windows/Linux capture pairs have identical state bytes and metadata. Each captured state resumes toward a further 12,640,320-cycle target on both platforms; capture and resume endpoints fall on instruction boundaries. All 32 continuations agree on final encoded state, monochrome and color framebuffers, PCM bytes and sample counts, guest clocks and recorded metadata. The eight SGB continuations also agree on their composited ARGB images. The matrix includes eight Windows-to-Windows control continuations; it tests cross-platform restoration consistency, not comparison-build image or audio fidelity.

The run's broad source guard reported one concurrent edit: the Qt regression test's C-linkage include fix. Its private receipt preserves that nonzero guard result. Both linked core libraries, the probe executables, public-header snapshot, cartridges and input traces remained unchanged, and there were no continuation mismatches. The edited Qt test is not linked into the state probe. Its own final application build and runtime results are recorded separately above.

A separate generated CGB case saves during a vertically flipped tile fetch after SCY changes from seven to eight. The two host captures are byte-identical and retain row seven. All four origin/destination continuations match the expected low/high fetch addresses and data, complete final state, framebuffers, audio and guest clocks. This probe's source and input guards remained unchanged throughout its run.

## External suites

The current and previous Windows builds ran the same 260 cases with matching model choices, fixed seed, firmware hashes and limits.

| Group | Explicit passes | Explicit failures | Bounded without a verdict |
| --- | ---: | ---: | ---: |
| Acceptance | 125 | 11 | 0 |
| Mapper | 56 | 0 | 0 |
| CPU | 22 | 0 | 0 |
| Audio | 24 | 0 | 0 |
| Timing | 14 | 0 | 0 |
| OAM | 7 | 0 | 1 |
| Total | 248 | 11 | 1 |

The eleven startup failures also produce explicit failure signatures in the pinned comparison build with the same firmware and limits. They concern divider, startup I/O and serial alignment expected from original hardware startup. The supplied comparison firmware has different startup behavior. These results remain failures in the table. One OAM timing case remains bounded without success or failure, and three early-monochrome cases were excluded because the pinned source does not expose an equivalent active hardware model.

The private suite manifest records revisions `31510e12eea6286d36eea060a6adde755e1067aa` and `c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15`, the assembler revision, build commands and input hashes. All 161 protected suite and firmware inputs remained unchanged.

## Mapper regression coverage

Each controller family enumerated by the pinned comparison source has product regression coverage. The table identifies the exercised operations; it does not certify every board revision, peripheral or commercial game. The 56 external mapper passes supplement these generated checks.

| Controller | Product regression coverage | Test sources |
| --- | --- | --- |
| ROM-only | Header storage capabilities, absent and mirrored RAM, physical ROM address lines, fresh storage and restoration | `test_cartridge.c`, `test_gb.c` |
| MBC1 and multicart wiring | ROM/RAM modes, bank selection, multicart address wiring, storage restoration | `test_cartridge.c`, `test_gb.c` |
| MBC2 | Address-bit decoding, internal four-bit storage, readback and restoration | `test_gb.c`, `test_cartridge.c` |
| MBC3 and extended MBC3 | RAM aliases, extended ROM/RAM banks, clock-register selection, write-triggered snapshots and restoration | `test_cartridge.c`, `test_gb.c` |
| MBC5 and motor variants | ROM address lines, RAM-enable decoding, motor/RAM independence, guest motor writes and presentation sampling | `test_cartridge.c`, `test_gb.c`, `test_rumble_sampling.c`, `test_presentation_observer.c` |
| MMM01 | Both menu layouts, initial mapping, lock and multiplex modes, RAM selection and native restoration | `test_cartridge.c` |
| MBC7 | Mapping, serial EEPROM commands, battery storage and continuation during an unfinished operation | `test_mbc7.c` |
| HuC1 | ROM/RAM selection, infrared mode, retained sensor history and restoration | `test_cartridge.c`, `test_infrared.c` |
| HuC3 | Banking, SRAM access modes, command bus, clock/battery storage, infrared and legacy-state continuation | `test_cartridge.c`, `test_huc3.c`, `test_infrared.c` |
| Camera | Banking, generated capture, sensor processing and state-related storage coverage | `test_camera.c`, `test_cartridge.c` |
| TPP1 | ROM/RAM banking, supported storage populations, clock commands, battery/native restoration and graded motor levels | `test_tpp1.c`, `test_rumble_sampling.c`, `test_presentation_observer.c` |

These files live in [`tests`](../tests). The shared cartridge matrix also checks all 256 header-type bytes across eight RAM-size codes on monochrome and color hardware. Address-line checks cover multiple ROM populations and erased padding. MBC6 remains a partial additional implementation and is not included in the pinned comparison source's controller list; its flash behavior is not qualified by these results. Physical motion, camera input, infrared transport and motor response remain open.

## Local cartridge inventory

The inventory contains 880 cartridge entries with 878 unique content hashes, drawn from 894 protected source files. Both Windows builds ran every unique cartridge to a limit of 420 presentation intervals using the same model and seed. Results expand back to 877 bounded entries and three illegal-opcode stops, or 875 bounded and three stops by unique hash. A bounded run is startup coverage, not a gameplay pass.

| Header classification | Entries | Bounded | Illegal opcode |
| --- | ---: | ---: | ---: |
| ROM-only | 12 | 12 | 0 |
| MBC1 | 272 | 272 | 0 |
| MBC2 | 11 | 11 | 0 |
| MBC3 | 81 | 81 | 0 |
| MBC5 | 487 | 484 | 3 |
| MBC5 with motor | 13 | 13 | 0 |
| MBC7 | 1 | 1 | 0 |
| HuC3 | 1 | 1 | 0 |
| Camera | 1 | 1 | 0 |
| Unsupported, malformed header | 1 | 1 | 0 |

Every deterministic scan field matched the previous build: status, instruction count, guest clocks, VBlank and presentation counts, PCM count and checksum, image checksum, serial observations, protocol output and illegal-opcode location. Host elapsed times were excluded from that comparison and are not used as a performance benchmark. All 894 original files remained unchanged.

The three illegal-opcode inputs were also run against the pinned source with the same CGB firmware. All three reached the same illegal opcode and address. Two also matched instruction and clock counts exactly; the third had an earlier execution timing difference. These shared stops remain failures and do not establish that the files are valid commercial dumps.

The unsupported header advertises controller `0x54`, has an invalid header checksum, lacks the standard logo sequence and has a size inconsistent with its header. Both parsers select a ROM-only fallback. Reaching the bound for this input does not establish support for an additional mapper. The local commercial set does not qualify every subtype or provide commercial coverage for HuC1, MMM01, extended MBC3 and the graded controller.

See the [parity checklist](parity.md) for remaining model, graphics, audio, accessory, controller and desktop requirements. Physical hardware qualification, long-term saving, broad commercial gameplay and full-speed operation across all games remain open.

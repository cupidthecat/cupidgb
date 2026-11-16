# Display, infrared and native-state validation

This record covers the 2026-11-16 display-delivery and VRAM work. The pinned comparison revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`. It separates generated port observations, completed pixels, intermediate surface writes, native continuation and desktop operation. Those checks qualify their stated inputs and builds; they do not establish complete graphics parity or compatibility with every game.

The final product-input manifest contains 240 files and has SHA-256 `0fd1749d2a67fef2e0f223c4299317e2b4d3bd94fa6ffccb82cb42f16d22c7d2`. It covers source, headers, tests, build inputs and supporting product files; documentation and private evidence are separate. The source audit checks all 217 first-party C/C++ files, including the packaging tool. Every file has the requested purpose and GPL header and stays below 2,000 lines; the largest is 1,980 lines. All 217 files pass the supplied formatter's check. Third-party files and generated icons are unchanged.

## Changes under test

The core retains display delivery independently of eager pixel work. CPU spans and guest bus accesses determine the pending display phase, which affects both infrared transition sampling and VRAM address history. Ordinary cartridges retain the same display schedule even when they have no infrared receiver. Monochrome LCDC stores sample the old value through the active bus path before advancing their leading clocks.

The pixel pipeline retains fractional window starts, early-window holds, pending zero-pixel insertion and interrupted monochrome object fetches. Physical CGB continues hidden object fetching in monochrome compatibility mode. Separate VRAM history records the map and tile-data addresses committed by fine work, the addresses pending from eager work, and the byte driven by a successful CPU or OAM-DMA read. A quiet row can finish without committing those pending addresses.

Native format 15 adds 34 bytes after the existing audio section: 20 for display delivery, four for pixel timing and ten for VRAM history. Exact and live loads restore that hardware history while retaining their existing output-ownership rules. Formats 2 through 14 remain readable with explicit defaults for missing history. An ordinary current state occupies 305,088 bytes, or 337,775 with a printer; model and cartridge extensions can add more. The detailed field and validation rules are in the [state guide](save-states.md).

## Generated pixel and VRAM comparisons

The following development probes linked five captured current runtime objects over a preserved earlier V15 archive. Each receipt binds the source objects, headers, archive, fixture and reference output. These are scoped differential results, separate from a complete product build or state-loader gate.

| Probe | Inputs and observations | Result |
| --- | --- | --- |
| Detailed pixel timing | 150 cases, 273,100 observation points, 72,000 final raw pixel values across three scanlines | No differences in checked STAT, VRAM, conditional OAM, display credit or completed scanlines |
| Quiet-row history | 360 cases with unchanged generated inputs | Returned bytes, CPU clocks, STAT, held byte, committed addresses and display credit agree |
| Expanded quiet-row history | 2,304 cases across grouped monochrome, pocket, adapter and color profiles; ROM, MBC1, HuC3 and HuC1 cartridges | All checked guest bytes, registers, clocks, retained addresses and completed-row hashes agree |
| Permanent quiet-row regression | 288 generated-memory cases with fixed expected guest bytes | The preserved implementation fails a returned-byte assertion; the corrected implementation passes |

The expanded quiet-row probe covers both native and monochrome-compatibility CGB at normal and double speed. It does not establish a separate DMG0 comparison: that model is not active in the pinned comparison source. Each generated fixture uses explicit matching clocks and display origins. Addresses, data and cartridge types are generated inputs; commercial images and firmware are unnecessary for these checks.

The original quiet-row probe found 180 differing rows, including 20 cases with different returned VRAM bytes. Separating pending fetch addresses from committed bus history removes those guest-byte differences. Always-fine fetching remains covered by the detailed pixel matrix, so a correction that only bypassed quiet execution would not satisfy both gates.

Intermediate surface publication still differs at 288 observations immediately before normal-speed row completion in the expanded quiet matrix and at 22,770 odd CPU-T observations in the double-speed pixel matrix. Completed-row comparisons agree. These diagnostics retain their original coordinates; they are not relabeled as completed-image failures or discarded as exact matches. The probes do not qualify complete frames, color conversion, SGB borders, physical presentation or every midline register interaction.

## VRAM bank and ownership checks

A separate read probe uses common explicit CPU-T schedules for 68 cases and 1,224 observation rows. It covers monochrome and physical-CGB hardware, native and compatibility modes, both clock speeds, both CPU-selected banks, both PPU banks, tile-address choices and scroll offsets. Returned bytes, held data, retained fetch addresses and clocks agree throughout that captured prototype run.

The first version of its reference fixture omitted the required pixel-output and color callbacks and crashed. A corrected fixture exposed a second comparison error: stopping each implementation at its own internal edge compared different CPU clocks in fast mode. The final probe uses the same explicit clock budget for both. The failed attempts remain recorded separately. Neither failure is counted as a passing comparison.

The permanent read target also checks blocked CPU accesses, OAM-DMA source ownership, repeated first-HBlank reads, passive peeks, LCD-off retention and writes that leave the held read byte unchanged. Whole-instance comparison around a passive peek guards against accidental display synchronization or bus-latch mutation.

## Integration findings

The first combined Windows gate passed 21 of 22 tests. Its failure was an older CPU-fetch fixture that stopped at the first readable STAT mode 3 and assumed VRAM was already blocked. A preserved native diagnostic showed that normal-speed CGB still returned the stored opcode at that point, matching the separately inspected startup interval. The fixture now advances into established transfer before asserting the blocked fetch; its expected byte, decoded instruction, destination and cycle checks remain intact.

Reviewing that interval also exposed a real write-path mismatch. The old CPU VRAM write gate used STAT alone, and the CGB DMA mirror path used the same assumption. A paired write probe recorded 3,184 observations with 20 stored-byte and readback differences: five startup positions across native and compatibility CGB in both physical banks. Read and write ownership require separate predicates because monochrome hardware blocks reads before writes near the end of OAM search.

The corrected write probe matches all 3,184 observations. A separate 36-case DMA probe compares the complete 16 KiB VRAM allocation after each transfer, totaling 589,824 checked bytes. Its preserved baseline has ten unintended writes into the opposite bank across six normal-speed startup cases. The corrected write-ownership path matches every checked byte, with status and other controls retained.

STOP introduces another observation point: it samples the retained display access gate without forcing a guest VRAM read. Quiet OAM work and synchronized work can therefore retain different gates at the same eager-renderer position. Generated STOP and wake probes check those histories separately, including fast CGB positions between complete LCD dots. Their existing saved display phase and STOP latches are sufficient; this repair adds no wire fields.

The final paired STOP probe matches 356 observations: 176 STOP entries, 176 wake continuations and four stopped monochrome LCD power cycles. A power cycle must release the CPU read port while retaining the PPU's complementary STOP latch. Earlier failed probes remain retained, including an SGB fixture whose introduction had not finished and a wake helper that exceeded the reference clock API's byte-sized span. The corrected helper delivers the continuation in equal bounded spans. The permanent CPU-write and DMA startup regressions fail on the preserved implementation and pass with the correction. These STOP comparisons cover the listed startup and OAM histories and controls; they do not qualify every HBlank or VBlank entry edge.

The next full Windows gate passed 64 of 66 tests, including every Qt target. Both failures were stale fixtures, with the original failures retained. HuC3's speed-switch test incorrectly equated whole elapsed dots with receiver half-dots across a delivery that changed clock speed. The fixed assertions check ten half-dots at commit and thirteen at completion, alongside five and seven whole elapsed dots, speed, countdown and borrowed credit. An actual guest store and an explicit matching CPU-T sequence independently confirmed those values.

The debugger fixture injected an OAM position where monochrome VRAM reads were already blocked and left the retained display scheduler at its LCD-off phase. It now reaches the earlier ordinary OAM boundary through LCD enable and bounded hardware ticks. The original HALT, interrupt, next-instruction and state assertions remain. Additional checks confirm that wake crosses the passive read boundary while raw debugger inspection still returns the stored byte.

Only those two test files and a read-map documentation comment changed between the preceding 240-input freeze, SHA-256 `908610008629554f8eb6c840b72f28dd87b943e4c920d488fcca629e9cfd1d0c`, and the final freeze. Reversing that single comment reproduces the earlier source hash exactly. The retained delta receipt has SHA-256 `5f69dd9b64e44af8a617a67f83083edfa484fb8004960ee48bb4d57fd2905a5a`. There are no runtime-body changes between those freezes.

## Native continuation and guest execution

The preceding freeze passed all 25 focused targets on Windows and Linux. Its native-state probes completed 48 executions and 84 full-byte comparisons across the two hosts. Infrared and printer continuation accounted for four executions and seven comparisons; eleven pixel-history cases accounted for 44 executions and 77 comparisons. Independently created states and both directions of cross-host loading agreed. These receipts retain their original freeze identity and are not relabeled as runs of the final build.

The state suites include 536 pixel continuations, 36 legacy pixel projections, 472 malformed pixel states and 207 direct codec checks on each host. Infrared coverage adds 780 continuations, six distinct retained histories, 400 legacy imports and 440 malformed-state rejections. Exact and live loads, ordinary and trusted contexts, output ownership and transactional failure behavior remain separate assertions.

A whole-core Windows guest-instruction probe on that preceding freeze checks 14,400 generated cases and 86,400 observation rows. Checked CPU execution, clocks, infrared receiver state and display credit agree with the pinned comparison. Raw internal LY/STAT storage differs in 85,680 rows; those values are diagnostic storage observations, not guest register reads. The result does not establish internal-state identity.

The unchanged guest fixture was then linked against each final desktop core archive. Windows and Linux each passed all 14,400 cases and 86,400 observations for CPU execution, clocks, infrared receiver state and display credit. The same 85,680 raw LY/STAT differences remain on each host. All 115,200 case, entry and observation records match between hosts after line-ending normalization, including those diagnostic values. The combined receipt has SHA-256 `571fc70534ab5a536042da06b477ea99a4f760ab83658ccfd5fefcb5d8e1ad3e`. This comparison binds the final desktop builds separately from the earlier focused build.

## Full-product gates

All complete configured test inventories passed without filters or skips:

| Build | Tests | Scope |
| --- | ---: | --- |
| Windows release, MSVC, Qt 6.8.3 | 66/66 | Core, frontend, command-line, archive and Windows association workflows |
| Linux release, GCC, Qt 6.8.3 | 64/64 | Core, frontend, command-line and archive workflows |
| Linux debug, address and undefined-behavior sanitizers | 45/45 | Core and runner; leak detection enabled, undefined behavior stops execution |

The Windows core archive has SHA-256 `3d5ac076a4b2ea1adb7e360b4fea5feb9ebc1d823c4efcc39587900fa8761363`; its Qt executable has SHA-256 `3e23f988e944893002eab3a69bc7a84f80d9e69ec8e8689a420a89dedd4d4c6e`. The Linux core archive has SHA-256 `f67f3d64ec04c73612fa1807bbd5611723ac206ff0aa56590761df68a3abe57f`; its build-tree Qt executable has SHA-256 `7bbdb796add8e8be88ab1edc7f53035c9a51a753ae91d267bfc243e1e11e512a`. Build and test receipts bind those binaries to the final freeze and verify unchanged inputs.

## External ROM suite

The final Windows runner executed all 243 retained cases with fixed seeds and private local firmware. It reported 242 explicit passes and one bounded OAM logger case, with no verdict changes from the preceding delivered checkpoint.

| Group | Explicit passes | Bounded without verdict |
| --- | ---: | ---: |
| Acceptance | 119 | 0 |
| Audio | 24 | 0 |
| CPU | 22 | 0 |
| Mapper | 56 | 0 |
| OAM | 7 | 1 |
| Timing | 14 | 0 |

The bounded logger still reports memory status 128 and runner exit 3; it is not counted as a pass. The comparison verifies the same cases, ROM and firmware bytes, frame limits, seeds, suite revisions and pinned comparison revision. All 147 protected originals and all 240 product inputs remained unchanged. The external comparison receipt has SHA-256 `14e3dcc87865cccf1aba496b519e1c7a80bdab2fa49c8071f1f1789e75231daf`. These explicit suite verdicts do not establish coverage of every mapper variant or commercial title.

## Commercial native-state continuation

Eight private commercial images supply nine retained scenarios across ROM-only, MBC1, MBC2, MBC3, MBC5, MBC7, HuC3 and Camera cartridges. Each scenario runs locally on both hosts and imports the other host's saved state, for 36 successful executions. A fixed power-on seed, post-boot profile, virtual clock and copied input traces keep the runs reproducible.

The checks compare 319,582,692 hardware steps, 21,608 presentation events, 432 complete native states and 15,949,064 stereo PCM frames. Independently created saved and final states, captured frames and continuation results agree across hosts and after both import directions. The sampled save boundary retains 17–19 queued PCM frames. The Camera scenario advances its completed capture count from 174 to 182. All 979 protected prior files, the frozen archives and the product inputs remain unchanged.

Visual review confirms active ROM-only and MBC5 play, a paused MBC1 case and a separate active MBC1 continuation. Other retained scenes cover name entry, dialogue, a motion instruction page and Camera output/menu transitions. The comparison is between uninterrupted and restored CupidGB execution; it is not a paired commercial-game comparison with the pinned source. Local commercial HuC1, MMM01, TPP1 and MBC6 images were unavailable for this corpus. The continuation receipt has SHA-256 `7d3487c0c45c3eb65856956518cef8aae3e297d153747e502aae36fe46127bf1`.

## Installed native Qt workflows

Fresh installed applications pass `--smoke-test --audio-check` on Windows and Linux/Xcb with Qt 6.8.3. Both report all seven required workflow markers, including actual audio activity around save/load and rewind, reentrant cartridge close, startup seed and firmware lifecycle, clock settings, battery ordering and clean autosave. The checks verify loaded runtime modules, the native platform plugin, unchanged product inputs and unchanged installed payloads. Each successful run retains thirteen captures; visual review is recorded separately from the automated results.

The Windows stage contains 295 verified payload files. The Linux stage contains 22 and uses the persistent Qt runtime whose 9,808 files, links and modes are verified before and after installation and validation. Linux installation changes the executable's runtime path: its installed SHA-256 is `fb895ff7d01dcbbd3c6758241780651c2f9d41fe36e0d5cd374db4ba0d99bdef`, separately bound to the build-tree executable above.

The first Linux attempt was interrupted by a confirmed WSL restart before either the wrapper or application wrote a final result. Its partial logs and captures remain retained and are not counted as a pass. A fresh installation and isolated retry completed successfully without product changes. During that retry, 42 Windows samples recorded a minimum of 815,939,584 bytes of commit headroom while roughly 9.96 GiB of physical memory remained available. Thirty-seven guest samples recorded application resident memory up to 110,576 KiB and an unchanged boot identity. The samples establish limited commit headroom, not the cause of the earlier restart or exact memory extrema. No global settings or unrelated processes were changed.

Settings, the four-channel menu, debugger and memory inspector were visually reviewed on both hosts. Text and controls are legible without overlap or unexpected clipping; the settings viewport intentionally scrolls. The separate visual receipts have SHA-256 `c15e1bd17fd4a20e7050dff4842b58dbe80ec6ca144c8d34aba5898ef3bd0d88` for Windows and `e466a08780fc74e2262408a6595ae3414c66e5b56c2fc67fba2d767a10c55af9` for Linux. These generated-cartridge workflows qualify native integration and the stated audio transitions. Physical input devices, audible quality, sustained frame pacing and commercial full-speed operation require separate evidence.

## Controlled core throughput

The final desktop runners were compared with the preserved `ff55590` runners after task builds, replays and native applications had stopped. Each host ran three alternating baseline/candidate pairs for each of four retained workloads, with 3,000 guest intervals per execution: 24 executions per host. Inputs, power-on seed and button traces were identical. Ordinary stereo synthesis, PCM draining and checksumming remained enabled. The final image checksum was computed outside the timed loop. No firmware, battery writes, Qt rendering or physical audio playback was included.

Every recorded guest result other than host elapsed time agrees across the paired runs. The ratios below divide the baseline median time by the candidate median time, so values below one indicate lower candidate throughput. Guest multiples use emulated base cycles divided by the selected model clock and measured host elapsed time; they are separate from display FPS.

| Workload | Windows throughput ratio | Windows guest-clock multiple | Linux throughput ratio | Linux guest-clock multiple |
| --- | ---: | ---: | ---: | ---: |
| ROM-only, DMG | 0.946 | 5.96–6.11× | 0.980 | 7.56–7.98× |
| MBC1, DMG | 0.965 | 5.84–6.00× | 0.997 | 8.04–8.40× |
| MBC5, CGB | 0.924 | 4.73–4.81× | 0.962 | 5.99–6.14× |
| ROM-only, SGB | 0.953 | 5.86–5.99× | 1.002 | 5.86–6.27× |

The timing corrections cost 3.5–7.6% of measured Windows throughput in these workloads. Linux medians range from a 3.8% reduction to a 0.2% increase; the SGB samples vary enough that the near-unity ratio does not establish an improvement. This checkpoint establishes no general speedup. The runners use Windows performance-counter timing and Linux monotonic timing; these measurements do not qualify independent wall-clock calibration, physical presentation, audible stability or full speed for every game. The remaining optimization work must preserve the verified guest results and retained timing history.

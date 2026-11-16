# Reset, input and native-state checkpoint

This checkpoint covers native console reset, desktop reset workflows, sampled input, version 16 state continuation and graded cartridge feedback. Complete hardware or frontend parity remains open. The comparison revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`.

The current product freeze contains 249 inputs with manifest SHA-256 `30856d7d2d71adca6160cb4dfb4070450f5b49a6c0e39edaf7be8343121bc02e`. The Windows and Linux Release builds passed their complete CTest inventories, with 69 and 67 tests respectively. Both installed Qt applications passed the native audio and reset workflows. The Windows executable has SHA-256 `81933d13ac4ea65ebb8604aeb88eb05912ee0bc2cd0a354cae1818881bbe729f`; the distinct Linux build and installed identities are recorded below.

## Behavior

Native hard and quick reset prepare a detached core before committing. Both retain loaded cartridge ownership, battery storage, host registrations, held input, pending input sampling and audio output history. Quick reset also retains physical WRAM, HRAM, object memory and color palettes. VRAM and execution state restart. Installed firmware begins again without executing inside the reset call. The approximate color handoff clears WRAM bank 2 and object memory, so that handoff must be distinguished from the immediate firmware-entry boundary.

Ordinary desktop Reset refreshes cartridge, firmware and companion symbols, applies the selected hardware and clears frame rewind. Quick Reset keeps the active model and physical memory. Reload keeps the active model and pause choice while performing a hard reset. Quick Reset and Reload retain rewind only when cartridge bytes, model and firmware remain compatible. A separate history-age clock prevents the reset guest counter from expiring retained snapshots. Trusted snapshot identity is rebound after an identical cartridge receives a new allocation.

These workflows retain an active WAVE recording at its existing rate. Qt retires the old playback stream and starts a fresh stream from generated guest PCM. Failed preparation preserves the live session and recording. Native console reset also clears the attached printer protocol after commit while retaining callbacks and emitted paper.

MBC3 and TPP1 clocks retain their data and fractional history. HuC3 clock and alarm digits reset with the controller. Clearing persistent HuC3 digits marks the battery dirty so autosave can commit the change.

Menus suspend execution while retaining existing keyboard, mouse and controller holds. Releases still reach the stored host state. New navigation presses and command shortcuts do not enter the guest while the menu owns input. Closing the last menu reconciles the controller before execution resumes. Unchanged held masks are not submitted as fresh samples during reset or menu closure.

The core input setter stores the host mask and schedules a sample for the next delivered hardware span. A changed JOYP row selection samples immediately without consuming a pending host event. Passive inspection and guest reads retain their separate row-settling behavior.

## Native version 16

Version 16 appends the pending-input flag after the complete version 15 timing records. It also keeps TPP1's mapped bank independent of its control readback. Immediately after reset, control readback can be zero while ROM bank one remains mapped.

Versions 2 through 15 keep their original payload interpretation and bounds checks before migration. Older TPP1 states project the saved control register into the mapping. Older input states rebuild a deterministic contact sample without introducing an interrupt. Version 15 display, pixel and VRAM history remains literal. Exact and live loads preserve their existing audio ownership rules.

## Validation status

| Check on the current freeze | Status |
| --- | --- |
| Windows Release desktop build | Passed in 25.305 seconds; executable identity recorded above |
| Complete Windows CTest inventory | 69/69 passed in 85.940 seconds, unfiltered with no skipped tests |
| Installed Windows Qt workflows with native audio | Passed in 20.241 seconds; 295 installed files and 15 retained captures |
| Linux Release desktop build | Passed in 163.583 seconds; source and installed identities recorded below |
| Complete Linux CTest inventory | 67/67 passed in 35.093 seconds, unfiltered with no skipped tests |
| Linux Debug sanitizer inventory | 48/48 passed in 125.957 seconds with address/undefined-behavior sanitizers and leak detection; unfiltered with no skipped tests |
| Installed Linux Qt workflows with native audio | Passed in 23.477 seconds; 22 installed files, an external verified Qt runtime and 15 retained captures |
| Visual review of the native captures | Reset, Quick Reset, Audio Settings and Debugger reviewed on each host; 11 other captures per host remain unreviewed |
| Windows/Linux commercial native-state continuation | 36 local/import runs across nine scenarios and eight ROMs passed; cross-host state and frame bytes matched |
| External test corpus | 242 explicit passes out of 243; one OAM case reached its bound without a pass |
| First-party source audit | 226 sources and headers checked; 50 changed files passed formatting; largest file 1,995 lines |
| Windows core throughput | 24 runs with matching guest work; median throughput was 1.20% to 4.74% lower than the preserved baseline |
| Linux core throughput | 24 runs with matching guest work; median changes were -7.66%, +0.41%, +1.34% and +3.14% across the four cases |
| Sustained native commercial runtime | Three requested 120-second samples passed on each host for DMG, CGB and SGB with normal audio; Linux application/raw clocks remain separate |
| Exact-current-commit hosted checks | Pending |

The preceding 249-input freeze, `e4a2eade5c75b4dc4389f6bbb99cd5c76a58e45722dc8d857ebaf6c947215179`, passed 69 of 69 Windows CTest checks. Its installed native run reached the pre-reset audio proof, then failed while the diagnostic snapshot queried capacity on a newly created stopped sink. That failed attempt remains separate from the successful current run.

[Qt 6.8.3 documents buffer capacity as valid only for an active or idle stream](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/multimedia/audio/qaudiosink.cpp#L237-L245). The corrected diagnostic records capacity as unavailable before a writable device exists. The production pump also checks a failed start and stream state before reading capacity. These changes preserve the original playback and reset assertions.

The native reset proof requires real generated PCM, progress in accepted input and reported processed time, and a separate interval with no writes or guest execution in which sink capacity returns by at least one host frame. It retains the same sink, device, queue, session and recording during each proof. The stream must remain active without errors or another prime. Each proof is bounded by 500 ms and 48 generated intervals. Immediate reset observations require an empty new playback queue, a released old sink, preserved recording/output history and the correct rewind boundary. Subsequent replay must cross that boundary without appending recorded audio.

All eight normal-reset, quick-reset and rewind observation phases passed on the current Windows executable. The five active-consumption proofs completed in 7 to 40 ms, used 1 to 6 generated intervals and each observed 3,840 bytes of returned native capacity during its no-write interval. The quick-reset replay crossed seven retained intervals. The resulting WAVE file contains 19,184 stereo frames at 48,000 Hz and occupies 76,780 bytes, including its header. The recorder remained active across both resets and did not append replayed audio. The installed run completed within its unchanged 180-second guard and observed the expected native platform, Qt and SDL modules.

The installed Linux application passed the same eight phases within the unchanged 180-second guard. Its five active-consumption proofs completed in 16 to 38 ms, used 1 to 6 generated intervals and returned 4,408 to 4,412 bytes of capacity during the independent no-write observations. The host stream used 44,100 Hz. Quick-reset replay crossed seven retained intervals without appending recording data. The WAVE file contains 17,577 stereo frames at 48,000 Hz and occupies 70,352 bytes. Source, installed-file, helper, Qt-cache and runtime-module guards passed.

Visual review found the Reset, Quick Reset, Audio Settings and Debugger captures coherent and readable on both hosts. The reset captures show the immediate blank reset boundary and do not establish gameplay. The Linux settings form retains its scrollbar and action buttons, and the debugger has readable registers, disassembly, breakpoint and console panes. Eleven other captures from each host remain retained but unreviewed. The separate Linux visual-review receipt preserves the original runtime receipt and its pending flag. Audible quality remains unqualified.

The retained focused input comparison contains 144 DMG/CGB cases and 684 matching observations. That result belongs to its earlier input build and excludes contact bounce, other hardware revisions, complete firmware execution and physical devices. It is not a substitute for current full-product gates.

The Windows installed run used explicit local-only gate verification while Linux remained incomplete. The later Linux installation and native run used the default two-host gate verification, binding both complete Release build/test receipts to the same frozen inputs. Each installed native run records only its own host's execution; together the two receipts establish the stated Windows and Linux workflow coverage. The earlier incomplete Linux build and early WSLg readiness observations remain preserved separately.

The Linux installation contains 22 files and uses the verified external Qt 6.8.3 cache plus the host SDL library. Installation removes the build runtime search path, so the installed executable has its own identity. This result qualifies that recorded host environment; standalone Linux runtime packaging remains unverified.

| Linux artifact | SHA-256 |
| --- | --- |
| Release core archive | `7ae0461a7dbc630484b8ab2fa0ed12dc25a4120e0b888279224065de4d20305e` |
| Release build executable | `664b19ca8526aefa4baeba2dc1e72bdde7ab40770c06f5d5165397a61781efaf` |
| Installed executable | `b6415042ef1d0f3a6c946dc500e91f444bd79129394e533dc6e2d11d82f20919` |
| Native validation receipt | `1c0bad873bae8f430e214be1c1f9dc009e7899efbed8e4b019aa842d2686cf44` |
| Separate visual-review receipt | `85929065000c8b05d8a4af8041de97dcc0ca78eaf4081553b429470d83e06e84` |

The original Linux readiness guard verified the canonical WSLg PulseAudio socket and its real RDP sink. A retained endpoint check became ready in 4.133 seconds within the existing 15-second limit. No service, endpoint or audio setting was changed. The installed native run retained the same readiness and runtime guards.

The current Linux Debug sanitizer build passed in 41.454 seconds. All 48 discovered core and runner checks passed with `ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1`, without filters or skips. Frozen inputs and built artifacts remained unchanged through the run. Its test receipt has SHA-256 `99dc165208105f9eec7c6dd781a890ecb8d20b073758f2667ca135a718b9e6eb`. Earlier cross-platform timing, audio, Camera, startup and clock results retain the source identities in their respective validation records.

## External suites and source audit

The current Windows runner recorded 242 explicit passes across 243 external cases. The groups were acceptance 119, audio 24, CPU 22, mapper 56, OAM seven passes plus one bounded case, and timing 14. The remaining case, `oam_bug/rom_singles/7-timing_effect.gb`, reached its 3,600-frame bound with exit code 3 and last recognized status `0x80`. The runner caches that status while the protocol signature is visible; it does not establish a valid running signature at the endpoint. The final text capture contains only `0xff` bytes. The case remains unresolved and did not report a pass.

The runner, suite and frozen sources remained unchanged. All 147 protected originals retained their recorded contents, and no seed mismatch occurred. These cases cover the recorded protocols and limits; they do not establish every cartridge or game. The external summary has SHA-256 `562b1d8dad344352de6af4b94e1bfb7b1d9a5e1a20b538f96482385463e826e7`.

The source audit checked 226 first-party source and header files. All required file headers were present, all 50 changed C/C++ files passed the format check, and no file exceeded 2,000 lines. The largest was `tests/test_state.c` at 1,995 lines. The repository format configuration is byte-identical to the supplied Downloads file. The audit found no third-party edits, prohibited branding or changed frozen inputs. Its receipt has SHA-256 `0eb2064a343f0458e0ec705ad219069892cceca25cad4613f720839cf0c51be5`.

## Commercial continuation

The current Windows and Linux cores completed 36 local and cross-host import runs across nine scenarios and eight private ROMs. The families are ROM-only, MBC1, MBC2, MBC3, MBC5, MBC7, HuC3 and Camera, with a second active-play MBC1 scenario. The checks compared 319,582,692 hardware steps, 15,949,064 stereo PCM frames, 21,608 presentation buffers and 432 native states.

Every local and imported run matched the recorded CPU, memory, PCM and presentation continuation, and all compared native-state and frame bytes agreed between hosts. Each run restored a pending host-input event and consumed it on the first subsequent hardware step. Source and core-library guards passed; all 1,296 protected files retained their contents and modification times. The final receipt has SHA-256 `bb83d29f5eebf296c433dfed6348909bad2b622137ea8cf25f97922f6f621909`.

These runs used the approximate post-boot profile without a boot ROM, fixed power-on seed `0x4355504944474231`, a virtual clock and private copies of the inputs. They qualify deterministic restoration and cross-host native continuation for the recorded scenarios. Paired commercial gameplay accuracy, in-game battery reload, sustained interactive Qt play and performance remain outside this result. Commercial HuC1, MMM01, TPP1 and MBC6 cases are absent from this fixture set.

## Core throughput

The Windows comparison used the preserved runner from `acaf16149621221b0f338811980344f415f707cd` and the current runner. Four cases each ran three baseline/candidate pairs, for 24 serialized runs with 3,000 requested guest frames per run. Each pair used the same fixed seed, inputs and ordinary stereo synthesis and draining. Every recorded guest result agreed, including cycle counts and PCM checksums; protected inputs remained unchanged.

| Case | Baseline median, seconds | Current median, seconds | Current/baseline throughput | Change |
| --- | ---: | ---: | ---: | ---: |
| ROM-only, DMG | 11.0129 | 11.1468 | 0.987989 | -1.20% |
| MBC1, DMG | 11.4335 | 11.8468 | 0.965108 | -3.49% |
| MBC5, CGB | 14.1989 | 14.9047 | 0.952650 | -4.74% |
| ROM-only, SGB | 10.9977 | 11.4756 | 0.958357 | -4.16% |

The Windows samples show lower median throughput. They also show run-to-run variance: the MBC1 baseline ranged from 10.808 to 12.093 seconds and the current runner from 11.720 to 12.829 seconds. Three pairs per case do not isolate the cause of the difference. Current guest-clock multiples ranged from 3.351 to 4.522 across the runs. The Windows performance receipt has SHA-256 `30d6f14b485cc4e01f1b3796d9c92d7b3473ac159eee996a643d6cc5f863fcef` and binds the full run manifest `96f0d7e391818de3e43aae17e71cb271fb8dc861f2f17e950bc6924fa73255d5`.

A separate Linux comparison used the same preserved revision and four cases, again with three baseline/current pairs and 24 serialized runs. All recorded guest signatures agreed, and every guarded input remained unchanged.

| Linux case | Baseline median, seconds | Current median, seconds | Current/baseline throughput | Change |
| --- | ---: | ---: | ---: | ---: |
| ROM-only, DMG | 6.6540 | 7.2062 | 0.923369 | -7.66% |
| MBC1, DMG | 6.5374 | 6.5109 | 1.004067 | +0.41% |
| MBC5, CGB | 8.4325 | 8.3212 | 1.013370 | +1.34% |
| ROM-only, SGB | 7.0685 | 6.8535 | 1.031377 | +3.14% |

The Linux results are mixed and retain run-to-run variance. For ROM-only DMG, the baseline ranged from 6.598 to 7.409 seconds and the current runner from 6.756 to 7.232 seconds. Current guest-clock multiples ranged from 5.602 to 7.954. These three-pair samples do not establish a general speedup or isolate the cause of each difference. The Linux receipt has SHA-256 `9ffc332cd83e14173ce374a0bf8c7f8a63c3311e80e8c6e169fdff972948d89a` and binds the full run manifest `03f47824a5c6def81e56628c9021660f881f6c44588c0c32e0c9a3d2071b0418`.

Both comparisons measure unthrottled core throughput with audio synthesis enabled. The timed loop excludes Qt, host audio playback, physical presentation, firmware and battery writes; it does not qualify frontend FPS, frame pacing or universal full-speed gameplay. Each comparison qualifies its own recorded host environment.

## Sustained native runtime

Three installed Windows samples ran serially for 120 requested application seconds each, using two private cartridges and normal audio. Each retained approximately 115 seconds after the five-second warmup.

| Model | Whole-run guest speed | Post-warmup guest speed | Widget paints/second | Paint interval p95/max, ms | Peak resident bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| DMG | 99.884% | 100.002% | 59.609 | 17.934 / 38.051 | 112,832,512 |
| CGB | 99.865% | 99.990% | 59.679 | 17.808 / 41.202 | 236,769,280 |
| SGB | 99.877% | 100.006% | 61.122 | 17.413 / 37.825 | 113,901,568 |

Guest speed uses the application monotonic clock and base clocks of 4,194,304 Hz for DMG/CGB and 4,295,454 Hz for SGB. The CGB sample ended with double speed enabled. Widget paints measure application presentation events separately from physical refresh.

All three samples reported zero audio errors and zero idle events after startup, with one initial prime each. They applied every scheduled input event: 14 for DMG, 22 for CGB and 14 for SGB. Source, installed-file, helper and protected-original guards passed. The private battery-flush checks passed, including an 8,192-byte CGB save; no commercial save was reloaded.

The final Windows window captures were reviewed. DMG and SGB show a readable game-over screen across the full game canvas; CGB shows a course scene. These are bounded commercial-runtime samples, not 120 seconds of continuous active play. They do not qualify enhanced SGB borders or every game. The audio counters establish accepted PCM and reported sink behavior; audible quality remains unreviewed.

The installed Linux application completed the same three requested 120-second samples serially with normal 44,100 Hz audio through the verified RDP sink. Each retained approximately 115 application seconds after warmup and passed all 23 recorded consistency checks.

| Linux model | Whole-run guest speed | Post-warmup guest speed | Widget paints/second | Paint interval p95/max, ms | Peak resident bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| DMG | 100.011% | 100.002% | 59.748 | 17.786 / 24.031 | 132,456,448 |
| CGB | 100.016% | 100.002% | 59.773 | 17.881 / 22.750 | 254,271,488 |
| SGB | 100.012% | 100.007% | 61.191 | 17.423 / 22.623 | 133,435,392 |

The Linux speed and paint measurements use the application's adjusted monotonic clock. Independently sampled raw-clock timing differed during these runs:

| Linux model | Whole-process monotonic seconds | Whole-process raw seconds | Sampled monotonic/raw rate range |
| --- | ---: | ---: | ---: |
| DMG | 120.190128 | 122.499039 | 0.916666–1.000001 |
| CGB | 120.239006 | 122.403849 | 0.961779–0.993140 |
| SGB | 120.239203 | 122.100536 | 0.919511–1.000006 |

These clock differences prevent a claim of physical 100% guest speed from the application counter. Raw process duration includes startup and shutdown and cannot be substituted for the shorter post-warmup observation window. The two clocks remain separate evidence.

All three Linux samples reported zero audio errors and zero idle events after startup, used one initial prime and applied all 14, 22 and 14 scheduled inputs respectively. Source, installation, helper, Qt-cache and original-input guards passed. Private battery flushing passed, including an 8,192-byte CGB save, without a commercial save reload. The CGB sample ended in double speed.

The three final Linux window captures were reviewed separately: DMG/SGB show matching game-over screens and CGB shows a course scene, with readable canvas, menus and status. The separate visual receipt leaves the runtime receipts and their original pending flags unchanged. The same limits on active play, enhanced SGB borders, audible quality and physical presentation apply to these Linux samples.

| Native receipt | SHA-256 |
| --- | --- |
| Windows DMG | `414a7c9d62249cfd16605dad5278d5e3eb02dfff2314a2771ce6ac4aa3f8f86d` |
| Windows CGB | `4ee2c2db6c6ca2d5c5c87e5b52ebdbc7be82acb99053709de860f3e8a6f122ce` |
| Windows SGB | `982f4d6da3c1b5652357e1003878f8cd68151d424eab4a28eb15b68e536a0cfa` |
| Linux DMG | `63c684107d9868c5eeb0f5ea124657facea0358aee137fb7a97ecfd8d293b513` |
| Linux CGB | `bf4dda71dad5af598dd703729c1d8f9e61dc02b5f674d071b7286ec58490f808` |
| Linux SGB | `48b7118b721bb46ebcff482ad91769259e772876d01058b0dfb3a3832d5b25a4` |
| Separate Linux runtime visual review | `c523b776536341774a654f3d21b71f349b60cf0fff6d3e957aeb92a4386a75ee` |

## Rumble

TPP1 motor output respects its declared capability and supports three nonzero levels. Boolean callbacks retain their existing on/off meaning; strength callbacks report changes between nonzero levels. Reset and state restoration require the host to stop and resynchronize feedback.

A separate probe of the pinned comparison core captured 207 rows: 19 manual windows, 180 isolated timing spans and eight display windows. Its eight constant-level/capability cases confirm zero, one-third, two-thirds and full amplitude, with output suppressed when the cartridge does not declare a motor. That constant-level mapping corresponds to maximum Strength.

Nine manual windows differ from the analytical prediction of CupidGB's elapsed-cycle averaging. For example, four NOPs at level one followed by one jump at level three produce equal-duration level intervals, but the captured amplitude is 13/21 rather than the elapsed-cycle prediction of 2/3. The probe captures the comparison core; it does not capture the current Qt product or provide a synchronized frontend differential. Host timekeeping was disabled, so skipped turbo frames and host pacing are outside its scope. Original guarded inputs remained unchanged and no physical motor was tested.

CupidGB currently averages by elapsed guest cycles and applies Strength as a linear percentage. Exact variable-level averaging, delivery cadence and nonmaximum strength response remain parity gaps across mixed CPU access patterns, DMA, HALT and CGB speed transitions. Virtual controller requests do not establish physical motor behavior.

## Remaining accuracy and qualification gaps

A separate instruction-level display probe used the current frozen Windows core archive, with copied headers and renderer sources verified against the 249-input freeze. Across 1,152 generated CGB cases, it found 54 visible divergences and four additional fetch-address differences with equal pixels. All 384 unchanged-SCY controls matched, and observation hooks preserved both engines' outputs. The first visible witness changes six pixels when a SCY write occurs between tile-map selection and a later data-plane fetch: the current engine selects the two planes from different rows.

The comparison targets CGB-E and treats the product's single CGB profile as that target. All 54 visible divergences occur at normal speed in native or compatibility mode. The double-speed delay range did not reach the same fetch phases, so those cases do not establish corresponding coverage. The diagnostic manifest is `762a8fd90eb0e80788c6a733ab0e6c6812ab36a012fa6bd4c3bb5735296a9182`; the results have SHA-256 `4e0b4fb6ff4fc81a5c4b6ae5b9f8fe49451f4d1f34f676466ee7c0979b57ca13`. The separate row-retention correction and native-state migration are excluded from this checkpoint's passing results.

Complete startup firmware and SGB introduction timing, wider hardware revisions, the remaining CPU/PPU/APU interactions, physical devices and transport, uncommon cartridge behavior and sustained commercial gameplay remain open. Native counters do not establish audible quality, physical presentation rate, every-game compatibility or universal full-speed operation. Keep PR #16 in draft and issue #5 open.

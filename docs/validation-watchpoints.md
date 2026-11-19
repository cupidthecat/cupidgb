# Memory watchpoints and source conventions

This checkpoint adds memory watchpoints to the core debugger and Qt frontend. It also makes source-header checks part of the Windows and Linux test gates. The behavioral comparison revision remains `ea667ad79aa28409f034b1fd6da7304e1844750d`. Complete hardware, debugger and desktop parity remains open.

## Behavior covered

Watches select reads, writes or both, with an inclusive core address range and an optional mapped bank. The Qt console accepts single addresses, exclusive or inclusive ranges, and conditions. The address field and access selector use the same command path. Surviving IDs retain their values; additions follow the last live ID, including reuse after tail deletion or clearing the collection.

The observer records the original bus address before CPU DMA redirection and access restrictions. A rejected write can still match. Instruction fetches and DMA source reads participate, including the special VRAM source path used by OAM DMA. STOP input polling stays outside observation because the audited CPU path samples its input latch directly.

The first matching access stops execution after the current hardware step finishes. Step, Next, Finish and ordinary continued gameplay honor the watch. Explicit debugger commands retain the triggering step for Backstep. A failed condition also retains that step; an execution-breakpoint condition failure occurs before a step and does not create a fictitious history entry.

Conditions can use `old` and `new`. Read conditions see zero for both. Write conditions see the attempted byte as `new`; each evaluated `old` token performs a guest-bus read before the write, including device read behavior and bus restrictions. Recursive watches are suppressed during that evaluation. Unconditional watches do not add a guest read, and ordinary expression reads remain passive inspector operations.

Observation is scoped to guest execution. Inspection, editor writes and rewind replay do not consume watches. Scope exit restores any prior memory observer, including when the caller supplies a different end argument. Cleanup also detaches an armed observer. Definitions and callback pointers remain host data and are absent from native states; the native format remains version 17.

## Regression coverage

The new core regression exercises CPU fetch/data accesses, first-hit selection, access masks, banks, inclusive ranges, duplicate definitions and ID reuse, blocked writes, repeated `old` evaluation, passive inspection, callback restoration, cleanup, reset persistence and byte-identical native states with an unconditional watch enabled. DMA cases cover ROM, WRAM and VRAM source reads and the distinction between a CPU's attempted address and its redirected destination.

Qt runtime coverage drives the actual controls and console. It checks false and true conditions, read conditions, normal playback, inclusive/exclusive ranges, parser rejection, start-address bank selection, symbol-loss errors, Next/Finish interruption, Backstep, state loads, ordinary/quick resets and cartridge replacement. A native capture records the populated watch list and stop message.

The source checker scans the current checkout, including new files, rather than relying on a historical inventory. It validates filename, purpose, author, date, copyright and complete GPL prologues. It includes owned C/C++ files, included tables, Windows resource templates, scripts, build entry points and workflows. Dependencies and generated build output are excluded. The current inventory contains 249 files: 234 C/C++/resource sources and 15 script/build files. All checked C-family files stay within 2,000 lines. Separate negative controls reject missing headers, incomplete notices and oversized sources across the supported file types.

## Build record

The initial Windows Release build compiled the new core, then stopped with MSVC `C1060` while compiling an existing Qt input file. The incremental retry completed and passed all 74 tests. Those results precede the final ID-reuse, armed-cleanup and VRAM-DMA additions; their logs remain separate from final validation.

The final builds use a 261-input source freeze with manifest SHA-256 `935fd7a290d8a5c302c0dfe179e1cfc625cbcb49ab608828ec61e95e92166c22`.

| Gate | Result |
| --- | --- |
| Windows Release | 74/74 passed; 66.58 seconds |
| Linux Release | 72/72 passed; 86.97 seconds |
| Linux address/undefined-behavior sanitizers | 53/53 passed; 129.96 seconds; leak detection and halt-on-error enabled |
| Windows native Qt with real audio | Passed; 16 captures including watchpoint controls and trigger output |
| Linux native Qt with real audio | Incomplete; startup readiness and a later audio-test underrun are retained below |

The Windows native executable SHA-256 is `daa4c4b25301542a5dd03352eddf337f041d6bad951d48bf19393770ce9194fb`. The Linux executable SHA-256 is `ed694420905bf5355c5d1ac1eb5b706af72d5726d616912f2c9f9d8a57a2e8aa`. Both remained unchanged during their recorded attempts.

The first Linux attempt started before WSLg exposed its audio server. A held-session probe observed that endpoint appearing about nine seconds after the display endpoint. Waiting for a stable audio server and output device allowed the unchanged application to proceed, but its mode-change test later starved a short prime while guest execution was stopped. The snapshot already showed audio consumption and writable capacity; the test waited for another increase without delivering its remaining queued samples. That failure does not establish a product playback regression, and it is not counted as a passing native workflow. A separate UI-only attempt also stopped at the reset-input check; the audio-ready attempt passed that earlier section. The later [pacing-clock checkpoint](validation-host-clock.md) records the readiness correction and complete native reruns with their own executable identities. The attempts above retain their original outcomes.

## External corpus and throughput

The final Windows runner completed all 260 planned external cases: 248 explicit passes, 11 existing startup failures and one bounded OAM case. All 56 mapper cases passed. Every compared guest field and ROM/firmware identity matches the preserved `546edf9` result, including cycles, presentation counts, PCM counts/checksums and image checksums. All 161 protected inputs and executables remained unchanged. This is a regression comparison; the unchanged failures remain open.

Throughput uses four alternating baseline/current pairs for each of four local workloads, with 3,000 guest intervals per run and normal stereo generation. The Windows run completed 32 executions with identical guest reports and unchanged executable hashes. Its median elapsed-time changes were -0.59% for DMG ROM-only, +0.53% for CGB MBC5, +2.30% for SGB MBC1 and +3.07% for CGB MBC5 motor hardware. Positive values mean more host time. These mixed results do not establish an overall speedup. The disabled memory-observation path already checks a nullable callback before dispatch; no speculative hot-path change is included.

Linux uses `CLOCK_MONOTONIC_RAW` for this comparison because an independent host-clock probe found that the adjusted monotonic clock undercounted elapsed time on this WSL host. Its 32 executions also retain identical guest reports and unchanged executables. Median elapsed time increased by 6.67% for DMG ROM-only, 5.03% for CGB MBC5, 0.38% for SGB MBC1 and 4.01% for CGB MBC5 motor hardware. The measurements justify further investigation of disabled-watchpoint overhead; they do not establish improved throughput.

The closing source check found all 261 inputs unchanged, with the same manifest hash and no source-header or line-limit failures. The recorded throughput, native attempts and full test gates therefore share the stated source freeze. Before publication, one trailing blank line was removed from the source-header checker to satisfy the staged whitespace check. Its Python syntax tree is identical, and the 249-file check passed again. Compiled inputs and executables are unchanged by that cleanup. Later repairs require their own executable identities and validation.

## Retained evidence and limits

The [fetch and feedback record](validation-fetch-feedback.md) now includes the recovered native workflow results, four 120-second sessions per host, headless throughput measurements and 72 guest-driven speed-switch comparisons. Those results retain their original executable identities and do not qualify this later watchpoint build. The Linux application/raw-clock discrepancy and the motor cartridge's menu-only capture remain explicit.

The new feature is bounded to 128 watches and the existing 255-byte expression language. It does not complete expression syntax, saved debugger sessions, continuous-playback instruction replay or physical-bank editing. Source checks establish the requested file conventions; they do not establish hardware accuracy. Use the [parity checklist](parity.md) for the remaining scope.

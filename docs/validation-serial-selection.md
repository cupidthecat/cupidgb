# Serial selection and snapshot continuation

This change retains the serial divider tap selected by an SC write and corrects SC readback when physical CGB hardware enters monochrome compatibility mode. The integrated build passes all 78 Windows, 76 Linux and 56 sanitizer tests. Its paired probe completes all 18 executions with zero guest-value or compared clock-phase differences. Version 18 files also pass Windows/Linux interchange and continuation checks.

## Observable baseline

An original boot program starts a fast internal transfer with `SC=83`, writes `KEY0=04`, waits 192 CPU T-cycles and executes reads of SB, SC, IF and DIV. It stores those read results in HRAM. A second sample follows 4,096 more CPU T-cycles. Separate programs retain CGB mode, explicitly restart the transfer after switching mode, or select an idle clock. These controls distinguish active byte timing from idle clock phase and register readback.

The baseline ran nine programs on both cores: 18 complete runs, 19,948 verified instructions and 144 executed guest read/store pairs. Five guest values differed. All six mode-retaining or explicit-reselection controls agreed at their sampled guest results. The three active-transfer differences and two idle-readback differences were:

| Program | Guest read | CPU T-cycles at instruction completion | Previous CupidGB result | Required result |
| --- | --- | --- | --- | --- |
| Active fast transfer, then compatibility mode | SB | 408 | `01` | `FF` |
| Active fast transfer, then compatibility mode | SC | 432 | `FF` | `7F` |
| Active fast transfer, then compatibility mode | IF | 456 | `E0` | `E8` |
| Inactive slow selection, then compatibility mode | SC | 432 | `7E` | `7C` |
| Inactive slow selection, then compatibility mode | SC | 4,624 | `7E` | `7C` |

Times identify completed CPU instructions. They do not claim subinstruction bus-edge precision. In the active switch case, the first observed completed byte occurred at 3,488 T-cycles in the previous implementation and 304 T-cycles in the comparison. Both late samples eventually contained `FF`, `7F`, `E8`. Checking only that late result would miss the delay.

Idle fast selection followed by a mode change also changed internal clock phase in the baseline, but its sampled guest bytes agreed. That phase difference alone is not an observable failed transfer. In the restart control, the old transfer can sample one more bit while SC is rewritten; the program clears SB, IF and DIV before measuring the fresh transfer. Agreement after that clear does not imply that the aborted byte histories were identical.

## Corrected behavior

`src/gb/timer.c` finishes an old high clock phase before committing the new SC value and selector. Every SC write selects the slow or fast tap, including inactive and external-clock control values. Divider changes consume the retained selection. KEY0 and boot mapping do not repeat an SC write; CPU speed changes preserve the selected tap while changing elapsed base-clock time.

`src/gb/gb.c` applies SC read masks according to physical hardware. CGB compatibility mode preserves the stored speed bit until a later SC write replaces it. Reset and the approximate startup profile choose the slow tap independently of SC's `7E` readback. Running a real boot binary remains necessary to establish firmware-specific serial history.

Native version 18 stores the selector separately. Versions 2 through 17 project it from their saved operating mode and raw SC bit 1, preserving phase and raw register bytes. A legacy state can therefore continue with corrected register reads while retaining its recorded byte. This is the native format's existing current-engine continuation contract; an older file cannot recover a missing sequence of SC and KEY0 writes. See [the format description](save-states.md#serial-clock-selection-in-version-18).

## Regression design

`tests/test_serial_selection.c` embeds the nine original guest cases. The guest instructions produce the HRAM assertions; the host checks those bytes without synchronizing display or audio to observe them. The cases also check exact instruction counts and elapsed CPU clocks. Additional checks cover idle selection, callback ordering on an SC rewrite, DIV reset, overlay removal, both CPU speed directions, hard and quick reset, all six approximate startup profiles and physical SC masks.

`tests/test_serial_state.c` covers active and inactive fast selection before and after KEY0, using all four ordinary/trusted and exact/live load APIs. Each saved case restores into machines with opposite previous selector values, continues executed guest reads and compares encoded continuation. Separate legacy cases cover all versions 2 through 17, reset readback that differs from reset selection, arbitrary accepted raw SC bytes, active transfer history and missing selector history. Invalid boolean values, a missing selector byte and a fast selector on monochrome hardware must reject atomically without callbacks.

The earlier audio, input, infrared, display, clock, printer, reset and mapper fixtures account for exactly one additional current-format byte. Version 16 fixtures remove only the serial selector; version 15 fixtures also remove pending input. Reserved pixel bytes retain their historical meaning. The frozen version 3 and 4 fixture explicitly supplies its historical idle selection before executing its existing preparation sequence, preserving the original payload and digest rather than changing the archived expectation.

## Verified result

The isolated Linux Release build enabled interprocedural optimization and used one build job. All 17 selected checks passed: serial, serial selection, serial state, native state, startup, reset, RTC state, divider phase, audio state, infrared state, input sampling, MBC7, pixel timing state, printer state, the SCY latch, C++ core integration and source headers. The new state test completed 16 native and 384 legacy continuation scenarios through all four load APIs, each with two opposite receiving selectors, plus 20 rejected loads. The historical version 3 and 4 fixture assertions passed without changing their digest constants.

The corrected paired run reused the original C observer and every original guest input file. It verified 19,948 instructions, 144 guest reads and their 144 stores across 18 executions. All 72 paired guest values agreed. All nine pairs also agreed at the compared stored-phase checkpoints. The early active-switch sample was `FF 7F E8 01` on both cores; the idle slow-switch SC reads remained `7C` at both samples. The original baseline and its five differing guest values remain preserved separately.

Source, binary, generated-input and protected-helper guards stayed unchanged throughout the corrected run. Its 267-input source manifest was `6dd75a2613cb57ba53064275ad981260451dd6573590f786bc787e1d822e50c3`. The completed build receipt has SHA-256 `675ceb3bde553b94123a91488ac681a51d3d510b327b9268adfd0e1ec0d10bb1`; the corrected paired receipt has SHA-256 `1f02c01ae71b5e15167fdbb95c623ff4110dca68c388b3e7d7940d10c80d3f62`. Full logs and private input identities remain with those receipts. The build reported the existing sign-conversion warning in `src/gb/pixels.c`; the changed serial and state code produced no compiler warnings.

## Integrated validation

The complete Windows and Linux Release suites include Qt frontend checks. The Linux core suite runs with AddressSanitizer and UndefinedBehaviorSanitizer. All three suites passed on 2026-11-17. Their source inventory remained fixed at 268 inputs, manifest `7107714e00265ed509375b2bfd42549ab35fdf25f97ef0361e6375c7f645c2f6`. The completed build receipt has SHA-256 `9ee730531db425e8dca4ea1dd487474907bfd0abde6a93023754e02bd83e6faa`.

The final paired probe repeats the original nine programs against the integrated archive and behavioral revision `ea667ad79aa28409f034b1fd6da7304e1844750d`. All 18 runs, 19,948 instructions, 144 guest reads and corresponding stores passed their protocol checks. All 72 paired guest values agreed, and all nine scenario pairs agreed at their compared phase checkpoints. Its receipt has SHA-256 `38f78848664103584e650e367683194e14d76fb30ca3eceb4b64c2bd2d422688`; protected sources, binaries, helpers and original generated inputs remained unchanged.

The file-exchange check captures two synthetic CGB states on each platform: reset with the slow selector despite SC's `7E` readback, and an active fast transfer retained after entering compatibility mode. It loads every capture on Windows and Linux, including the originating platform, after priming the receiver with the opposite selector. Four captures and eight restores produce 70 exact comparisons with no mismatches. Each restored machine matches its uninterrupted control for 512 instructions, including encoded state, executed guest reads, per-instruction serial observations and nonzero 44,100 Hz PCM.

The exchange uses the ordinary native load API; the four load APIs remain covered by the permanent continuation tests. LCD output is disabled in the synthetic exchange fixture, so its framebuffer equality does not establish rendering accuracy. Its receipt has SHA-256 `594297e05209f5888e99c1e04772b2c6e6e140ac365a491b2472060ff3b07449`. The same six qualified Release executables and core archives, matching headers, helpers and source inventory remained unchanged throughout.

This bounded probe covers the current physical CGB profile at normal CPU speed. Product regressions separately exercise speed changes and reset paths. It does not establish link-cable compatibility with every peripheral, behavior of unimplemented hardware revisions, commercial-game coverage, sustained speed or complete hardware parity. No ROM, boot binary, save or private runtime trace is part of this document.

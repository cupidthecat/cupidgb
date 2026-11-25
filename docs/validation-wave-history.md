# CH3 retained fetch history

An active wave-channel retrigger can expose the high nibble of a previously fetched byte before the next RAM fetch. CupidGB previously kept only the selected nibble for this operation. When the channel held the low nibble of `69`, a trigger left the guest reading `9` instead of `6`.

The correction keeps a separate full-byte hardware latch. Each active nibble fetch replaces it, and a trigger with an enabled DAC selects its high half. Changing wave RAM after a fetch does not replace the retained byte. A trigger at the active fetch boundary refreshes byte zero after any wave-memory corruption and before a newly enabled length counter can expire. The established startup delay and length reload accounting remain intact.

## Retained guest proof

The original generated CGB program writes NR34, executes an actual PCM34 read, and stores the returned accumulator in HRAM. The read observer records the address, program counter and clock only; it runs before the core returns data. The completed instruction and its following store establish the observed value.

| Case | CupidGB before correction | CupidGB after correction | Purpose |
| --- | ---: | ---: | --- |
| Retained low nibble of `69` | 9 | 6 | Select the held byte's high half on trigger. |
| Retained high nibble of `A6` | 10 | 10 | Keep the already selected high half. |
| Trigger at the fetch boundary | 9 | 9 | Refresh from byte zero at the boundary. |
| Trigger bit clear | 10 | 10 | Leave ordinary fetch execution in control. |

The accepted baseline contains four cases and twenty instruction-boundary or bus observations. One case differed in two actual guest values: the completed read and its HRAM store. The other three controls matched. The baseline receipt has SHA-256 `6801ba448b0f39a7b6b6cc728a2ae4f021334c777c5297d4742b1be336136682`. Its generated inputs, source identities and complete logs remain private. The corrected run retains all twenty reference observations and resolves both differing guest values.

An earlier attempt misinterpreted the read callback's pre-access placeholder as returned data. That attempt remains preserved as invalid evidence. The corrected observer records the same execution and derives the value from the guest accumulator and store. No core callback semantics changed.

## Regression coverage

`cupidgb_wave_history` checks twenty guest-result cases across physical CGB color and compatibility modes at normal and double CPU speed. Four cases write `E1` to the RAM byte after fetching `69`, then require a trigger to return `6`. A further four cases change RAM between the high and low nibble fetches and require the low nibble to come from the new byte.

The test also checks eight combined length-boundary cases for DMG, the existing DMG0 profile, Pocket, SGB, SGB2, CGB, CGB compatibility mode and CGB double speed. They start with an active one-tick length, enable length at an odd sequencer phase, and trigger two base clocks before a fetch. The expected complete wave image, retained byte, selected nibble, channel status and final length are explicit. Level changes, a DAC off/on sequence away from a boundary, APU power transitions and native hard/quick resets have separate controls.

`cupidgb_wave_history_state` completed these restore checks in the isolated Linux build:

| Group | Passed cases | Required behavior |
| --- | ---: | --- |
| Current-format continuation | 128 | Capture before trigger or before PCM read, with and without credited pending clocks; restore through all four APIs into different receiving histories. |
| Full-byte round trips | 1,024 | Preserve every eight-bit latch value independently of the raw output nibble and current RAM, then expose its high half through guest code. |
| Pending fetches | 24 | Execute captured clocks using captured credit; for V14–V18, project missing history before a real pending fetch replaces it. |
| V2–V18 projection | 1,088 scenarios, two receivers each | Duplicate every possible saved sample nibble, preserve raw RAM and sample state, and avoid inheriting the receiving latch. |
| Rejected loads | 40 | Reject damaged, missing, extra, truncated or mis-versioned data and invalid sample nibbles without changing the receiving core. |

The isolated version 19 wave-history layout appends one byte after the serial selector, before the accessory descriptor. Existing hardware offsets remain fixed. Historical fixture builders remove that byte when producing older layouts; frozen format lengths and digest expectations are retained. See [the native state contract](save-states.md#wave-fetch-history-in-version-19) for its relationship to the later combined format.

The complete V2–V18 migration matrix uses a valid MBC7 cartridge. V5 requires a model or mapper section, so this fixture includes MBC7's established eight-byte extension from that version onward. Ordinary byte-history and pending-clock fixtures retain their original ROM-only inputs. The first isolated attempt passed 17 of 18 tests, then rejected a test-created ROM-only V5 payload. That failed attempt and its build remain preserved. Correcting the fixture required no change to the production codec.

## Validation status

The isolated Linux Release build passed on 2026-11-17 with IPO, native state hashing and one compile job. All 18 selected CTest targets passed, including the new history tests, the established APU and wave-retrigger tests, state and mapper fixtures, reset/startup checks and the source-header audit. The frozen V3 and V4 state digest assertions also passed.

| Fresh paired check | Result |
| --- | --- |
| Original guest PCM program | Four cases, twenty observations and eight actual guest values match; the earlier `9` read and store now both return `6`. |
| Length-order matrix | 360 cases and 1,440 observations match. All 320 earlier stored digital-code differences are resolved, with reference observations unchanged. |
| Wave-memory matrix | 25,600 cases and 102,400 observations match. Complete output is byte-identical to the qualified earlier wave-memory run. |

All three probes use their unchanged original source with the new core archive and matching headers. The completed receipt has SHA-256 `faa7712f8f1bb9fea49244fd66048f7e669fa1cda00c8090dbdaf8022674faf4` and binds the 271-input manifest `77153627079b92a96bbfe5025c8e4d89ace924bb3e3e918e9e77704b1602ecc1`. Inputs and retained baseline evidence were unchanged before and after execution. Full logs, executable identities and the failed attempt remain with the private receipts.

These results apply to the isolated Linux checkpoint. The later [combined version 20 record](validation-storage-wave.md) reports fresh Windows, Linux and sanitizer gates, NR30 comparisons and Windows/Linux file exchange on its own frozen source. Those later results retain their separate build identities.

This isolated work covers active fetch-byte retention and retrigger selection. NR30 disconnect boundaries are qualified separately in the combined record. Inactive-channel fetch behavior and other latch writers remain open. The startup profile used without a boot binary begins with a zero byte and cannot recover firmware history that was never executed. These isolated fixtures do not establish physical-unit behavior, complete audio accuracy, all-game compatibility, sustained speed or cross-platform continuation.

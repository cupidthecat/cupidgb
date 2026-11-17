# CH3 retrigger wave memory

Retriggering an active wave channel during its final two clocks before a fetch can change wave RAM. For source blocks above the first four bytes, the first source byte changes before the block is copied to the start of wave RAM. The previous implementation copied the block without that source change. It also used the ordinary low-offset byte copy for Pocket, which retains the first block in this case.

The correction keeps the existing trigger window and channel gates. It changes the source byte from the three existing byte values, then copies the four-byte block. Pocket has an explicit low-offset exception. SGB2 retains the ordinary monochrome behavior. Physical CGB hardware remains excluded, including when running a monochrome cartridge.

## Regression coverage

The `cupidgb_wave_retrigger` test target uses generated inputs and complete sixteen-byte expected readbacks. The expected byte tables do not calculate the mutation formula. Every observation reads backing storage directly, so it cannot synchronize the APU through a wave-port read.

| Group | Cases defined | Purpose |
| --- | ---: | --- |
| Complete readbacks | 3,072 | All sixteen offsets, both upcoming nibble positions, two nonuniform byte patterns, and six boundaries before and after the fetch. |
| Bit inputs | 768 | All eight input combinations in each of the three nonzero source blocks, at both clocks inside the window. |
| Controls | 640 | Inactive channel, disabled DAC, trigger bit clear, idle operation, and delayed DAC-off, inside and outside the window. |
| Guest stores | 3,072 | Real CPU writes to NR34, matched NOP controls, register timing, and the bytes retained after the instruction. |
| Native continuation | 256 | Capture before or after the guest store, then restore through ordinary, trusted-context, live, and live-context APIs. |

The profiles are DMG, the existing DMG0 compatibility profile, Pocket, SGB, SGB2, CGB, CGB running a monochrome cartridge, and CGB double speed. DMG0 coverage protects the local profile; it does not establish a separate physical-revision qualification.

The fixture frequency is 0x7f9. The initial fetch occurs after twenty base clocks and later fetches are fourteen clocks apart. SGB uses 4,295,454 base clocks per second; the other profiles use 4,194,304. The digital fetch schedule is expressed in those clocks. Guest instruction durations also account for CGB double speed.

The native tests capture at safe instruction boundaries. A restore made before the store must reproduce the mutation once. A restore made after the store must retain the changed source and destination bytes. Ordinary restores compare serialized state and queued PCM. Live restores retain the receiving audio queue and settings while checking the restored guest state. The wave-memory correction adds no serialized fields. The integrated build uses version 18 for the separate [serial-selector correction](validation-serial-selection.md).

## Validation status

The integrated Release builds passed on 2026-11-17 with IPO, native state hashing, and one compile job. Windows passed 78/78 CTest targets, Linux passed 76/76, and the Linux AddressSanitizer/UndefinedBehaviorSanitizer build passed 56/56. The wave-memory target completed every case listed above on all three configurations.

| Check | Status |
| --- | --- |
| Linux build and focused wave-memory regression | Passed; 7,808 checks across the five groups above. |
| Windows build and focused wave-memory regression | Passed; all five regression groups. |
| APU, audio-state, and shared hardware regressions | Passed on Windows, Linux, and sanitizers: `cupidgb_apu`, `cupidgb_audio_state`, and `cupidgb_gb`. |
| Sanitized regression run | Passed with AddressSanitizer and UndefinedBehaviorSanitizer. |
| Generated model comparisons after integration | Passed; 25,600 cases and 102,400 observations, with no differences or invalid fixtures. |

The generated comparisons cover DMG, Pocket, CGB, SGB, and SGB2. Each model has 3,840 component cases and 1,280 guest-program cases. Both nibble phases, all sixteen byte offsets, eight bit-input combinations, two nonuniform patterns, timing boundaries, and active/inactive controls are represented. SGB and SGB2 use their handheld APU behavior and physical base clocks; host-console startup and transport are outside this fixture.

The retained pre-correction run provides a negative control for the 7,680 original cases. The correction resolves all 324 failing cases, covering 648 differing observations. The remaining 7,356 cases retain the same observations, and all recorded non-wave fields stay unchanged. Strict output validation rejects unexpected lines and checks each fixture's instruction timing, channel gates, and complete wave memory. Source, headers, archives, helpers, and retained negative-control evidence stayed unchanged throughout the comparison.

The final comparison uses behavioral revision `ea667ad79aa28409f034b1fd6da7304e1844750d` and the integrated 268-input source manifest `7107714e00265ed509375b2bfd42549ab35fdf25f97ef0361e6375c7f645c2f6`. Its receipt has SHA-256 `64077f9b077171a6a830b12576f09724d59de043352aba7efe07313cbaec3823`. The complete output is byte-identical to the earlier isolated corrected run. Full logs and protected input identities remain with the private receipt.

These tests target wave-memory mutation and continuation. They do not establish complete audio accuracy, physical-unit behavior, compatibility with every game, or sustained emulation speed.

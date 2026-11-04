# Validation record

This record covers the Qt desktop and GB/GBC timing milestone on 2026-11-04. It does not establish complete hardware or frontend parity. The comparison source revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`.

## Builds and automated checks

| Environment | Configuration | Result |
| --- | --- | --- |
| Windows x64, MSVC 19.43, Qt 6.8.3 | Release desktop/core | Nine CTest checks, visible frontend workflow smoke, local deployed executable |
| Ubuntu 24.04 under WSLg, GCC 13.3, Qt 6.4.2 | Release desktop/core | Nine CTest checks, native Xcb frontend workflow smoke |
| Ubuntu 24.04, GCC 13.3 | Debug core with ASan/UBSan | Seven CTest checks, leak detection enabled |

The checks cover core lifecycle, CPU execution, serial timing, sprite priority, DIV-driven audio sequencing, CPU bus phases, generated-ROM runner protocols, host audio conversion, and Qt workflows. The Qt workflow check uses an isolated settings directory and generated cartridge; it covers Unicode paths, pause, input release, failed-open preservation, screenshots, battery reset/load, settings persistence, and boot-overlay selection. It does not open an audio device.

The native Windows audio run exposed and verified a fix for a shutdown callback that accessed a destroyed audio sink. Deployed smoke checks require no Qt installation paths in the application environment. This is local folder-deployment evidence, not an installer qualification.

## External ROM checks

- Eleven CPU instruction cases produced explicit serial success results.
- Sixty-four of the same sixty-six selected DMG acceptance cases produced explicit register success results. Two remain inconclusive: the interrupt transition case and the boot-divider case.
- Twelve DMG and twelve CGB sound cases produced explicit memory-signature success results. Paired runs used identical ROM hashes and boot binaries and also passed in the pinned comparison build.
- Both acid2 final images matched the pinned comparison build with zero differing pixels out of 23,040, using matching hardware models, shared boot binaries, grayscale, and disabled color correction. These are stable final-image comparisons, not timing coverage.

The remaining interrupt-transition case passes in the comparison build with either tested boot binary. The boot-divider case passes there with the local boot binary and remains inconclusive with the generated boot binary. CupidGB still has startup and interrupt/PPU timing gaps.

## Commercial runtime checks

Private input traces reached active Tetris and Kirby gameplay, the first Super Mario Land stage, and Pokemon dialogue. Five cartridges were checked in bounded headless comparisons using shared boot files, models, and inputs. Final-image matches varied by case and source revision; these runs do not establish sustained image/audio parity.

Native desktop samples ran with normal audio and no turbo. They recorded guest base-clock progress separately from images submitted to the widget and widget paint events. A paint event does not prove host display presentation.

Windows Release samples ran Tetris and a CGB cartridge sequence for sixty seconds each. Their guest speed measurements were approximately 99.9% of the nominal clock. Both processes exited successfully with zero audio backend errors, but reported idle events. Tetris showed active gameplay; the CGB sequence remained in intro dialogue. The host used 48 kHz stereo float output converted from the core's 44.1 kHz signed samples.

Linux native runs also used the actual audio device. Audio-backend readiness matters under WSLg: a run with no usable sink is excluded from audio evidence. Backend idle events occurred in successful samples, so glitch-free playback and audible quality remain unverified.

The private receipts retain source manifests, executable and ROM hashes, models, boot hashes, traces, screenshots, host audio formats, queue sizes, paint intervals, process resource samples, elapsed time, and exit codes. Runtime samples preceding final input/save guards or operand-bus fixes remain identified by their own source manifests. CI checks the exact pull-request head separately.

Save-state loading, RTC persistence, controllers, debugger tools, link devices, uncommon cartridges, all hardware revisions, full PPU timing, and long-term resource stability remain open. See [the parity checklist](parity.md).

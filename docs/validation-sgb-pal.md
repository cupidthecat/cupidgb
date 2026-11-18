# SGB PAL validation

CupidGB provides a separate PAL adapter profile with a 4,256,274 Hz base clock. The desktop hardware selector, `--model sgbpal`, the ROM runner and suite runner select the same profile. PAL and NTSC share `sgb_boot.bin` while retaining separate native model identities. Existing model IDs 0 through 5 keep their values, and PAL uses ID 6.

## Behavioral comparisons

Comparisons use pinned revision `ea667ad79aa28409f034b1fd6da7304e1844750d`. A local generated firmware probe compares NTSC, PAL and SGB2 with equivalent initial execution. Each model holds the CPU through 65,296 calls and 106 presentations. The next call executes one NOP, reaches PC 1 and brings total presentation time to 14,887,496 half-base-clock units. The observed clock rates are 4,295,454, 4,256,274 and 4,194,304 Hz respectively. All reported fields match. These generated inputs establish the measured startup boundary and regional clocks. Commercial firmware and sustained PAL gameplay remain unqualified.

## Regression coverage

The Linux Release core passes 68 of 68 checks. Expanded PAL coverage includes startup hold and release, hard and quick reset, queued startup audio, native continuation, receiver permission and packet history, RTC timing and model identity. Protocol state checks complete 120 pulse continuations, 840 generated legacy imports and 260 API rejection checks. The legacy records are generated format projections.

The audio clock regression checks every supported integer sample rate on handheld, NTSC and PAL clocks. PAL at 64,489 Hz exposes an exact-divisor boundary where truncating phase advance needs a 68-clock reconstruction span. The scheduling span now derives from that truncated increment. A separate 12,800,000-clock run at that rate preserves valid output state and the exact earned sample count. Existing handheld and NTSC rate combinations retain their scheduling spans.

## Desktop checks

Both Qt 6.8.3 Release builds complete. The Windows matrix passes 95 of 96 checks and times out its desktop workflow at the existing 30-second limit during concurrent build and CLI work. The unchanged desktop check then passes alone in 29.47 seconds with the same limit. This retains the failed full-matrix result alongside its successful isolated rerun. All other checks pass, including command-line PAL selection and the expanded adapter workflow.

Linux passes all six selected Qt command-line and workflow groups, including desktop, storage, adapter startup and local linking. The full Linux core matrix remains a separate 68-check result.

Native adapter workflows also pass on Windows and WSLg, with ordinary and enhanced fixtures for NTSC, PAL and SGB2. Each PAL fixture records 665,070 startup PCM frames, 3,168 subsequent handheld frames, 7,373,520 guest clocks and 14,817,264 presentation clocks. Retained captures show the CupidGB introduction and desktop controls on both platforms. The detailed SGB2 route confirms real audio consumption on each host with a no-write interval. That short sink observation does not qualify sustained PAL playback.

Wider PAL graphics/audio comparisons, physical presentation pacing, commercial firmware and commercial playback remain on the parity checklist.

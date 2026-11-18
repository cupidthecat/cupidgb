# TAMA5 cartridge and host audio clock validation

TAMA5 connects a nibble command port to the ROM bank latch, 32 indirect save bytes and a four-page calendar. The generated cartridge checks readiness, both bank nibbles, all 32 ROM banks, and every save byte. Tests exercise batch clock advancement, register masks, calendar and leap-year boundaries, measured invalid minute digit transitions, and direct writes that leave the microcontroller's cached value in place until rollover. Physical cartridge observations and the clock datasheet establish the tested register behavior. [Cartridge measurements](https://gbdev.gg8.se/forums/viewtopic.php?id=469) and [Toshiba TC8521AM datasheet](https://www.alldatasheet.com/datasheet-pdf/pdf/32098/TOSHIBA/TC8521AM.html)

State tests capture each interrupted save command and a fractional clock interval. They check byte-for-byte continuation and verify that a correctly rehashed state with a malformed calendar leaves the receiver unchanged. Battery tests check both native and raw saves, clock catch-up, reset retention and rejection before any live storage changes.

The pinned comparison revision does not implement this controller, and the local library has no matching commercial image. Command latency, undocumented commands, physical alarm output, and parts of invalid date and 12-hour behavior remain unverified. Readiness and command completion are immediate in the current model.

Qt now estimates the playback clock using accepted frames and native writable capacity. The host converter resamples independently of guest clocks and source PCM. The component test varies native queue capacity, applies a 2.8% rate mismatch, changes the device rate, and checks convergence, queue correction, underrun recovery and waveform duration. The first native trial measured 99.91% guest speed for DMG and 97.75% for CGB on WSLg, with two post-warmup idle events each. A follow-up change retains recent estimates across brief underruns and uses bounded queue feedback.

## Runtime results

Linux Release passed 102 checks and the Linux ASan/UBSan build passed 75. The native desktop workflow passed on WSLg with real audio. Three generated guest ROMs covering readiness, every ROM bank and every save address passed on DMG, CGB and SGB2. The 177-case acceptance and mapper matrix also passed during integration. All 121 paired firmware cases matched terminal cycle counts.

Both WSLg playback runs used the nearest-neighbor display filter, a fixed seed, normal audio and a 105-event input trace. They ran for 60 seconds with five seconds of warmup. Each reached active gameplay and flushed its battery file.

| Game and model | Guest speed | Post-warmup idle events | 95th-percentile Qt paint interval | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| Tetris, DMG | 99.280% | 1 | 17.85 ms | 100,440 KiB |
| Super Mario Bros. Deluxe, CGB | 99.988% | 2 | 18.02 ms | 158,452 KiB |

Both runs reported zero backend errors. The final estimates ranged from 0.9608 to 0.9828 with 57 and 58 updates. The process used 14.02 and 16.19 CPU seconds respectively. Paint intervals measure the Qt widget. They do not measure physical display presentation. The earlier WSLg samples for these games measured 97.21% guest speed, with zero idle events after warmup. The new runs improve speed, and the idle events leave uninterrupted playback unqualified.

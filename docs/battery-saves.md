# Battery saves


Fresh cartridge storage starts at erased `FF`, after controller-specific RAM or EEPROM sizes are resolved. Loading a battery or native state replaces that cold data with the saved bytes. The Qt reset/reopen workflow restores the battery after loading the cartridge; entering the boot overlay leaves existing storage intact. The cold fill does not count as a guest battery write.
CupidGB stores battery files beside the cartridge with a `.sav` extension. Validation runs use disposable copies of cartridges so existing saves remain untouched.

RAM-only files contain exactly the configured cartridge RAM bytes. Timer-equipped MBC3 cartridges append 48 bytes: ten little-endian 32-bit register values followed by a little-endian 64-bit Unix timestamp. The first five values are live seconds, minutes, hours, day low, and day high; the next five are their latched copies. Loading also accepts a 44-byte trailer with a 32-bit timestamp, or exact RAM-only data. Other sizes and register values that exceed one byte are rejected before either RAM or the clock changes.

Saving advances a temporary live clock to the supplied host time. It leaves the running cartridge and its latch unchanged. Loading advances only live registers; the stored latch remains observable until the game latches again. Halt, day carry, out-of-range register counter states, and backward host time have explicit regression coverage. Missing, future, or implausibly old timestamps establish a fresh baseline and set the live carry bit so a game can detect a clock requiring initialization.

The Qt frontend saves after a quiet period following guest writes and flushes before reset, cartridge replacement, and close. Clock reads and elapsed host time alone do not mark a cartridge dirty. State loading and rewind mark the restored battery state dirty. Writes use atomic file replacement. Load, write, and replacement failures produce errors and preserve the existing file; a failed load also preserves the current cartridge session.

File > Recover Battery from Older State validates a native version 1 snapshot's size, integrity, and cartridge identity, then exports its RAM and supported clock data to a separate file. It does not change the running cartridge or restore older execution state. Cartridge, battery, and input snapshot paths are protected from accidental replacement during recovery.

MBC7 files store the 256 EEPROM bytes in address order. An oversized file from an older CupidGB build is accepted when its length matches the RAM size declared by that cartridge header. Only the first 256 bytes contain usable EEPROM data. Camera cartridges persist their banked SRAM as a RAM-only file.

Battery-enabled TPP1 cartridges append a 20-byte clock trailer to SRAM. It stores the `TPP1` signature, version, status, Unix timestamp, and four live clock bytes. Loading also accepts exact SRAM-only data.

HuC3 files append 17 bytes to SRAM for the Unix timestamp, current minute and day counters, alarm time, and alarm flags. CupidGB also accepts SRAM-only HuC3 files from earlier builds. Those files start with a fresh clock baseline. Flash persistence remains pending.

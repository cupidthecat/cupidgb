# Battery saves

CupidGB stores battery files beside the cartridge with a `.sav` extension. Validation runs use disposable copies of cartridges so existing saves remain untouched.

RAM-only files contain exactly the configured cartridge RAM bytes. Timer-equipped MBC3 cartridges append 48 bytes: ten little-endian 32-bit register values followed by a little-endian 64-bit Unix timestamp. The first five values are live seconds, minutes, hours, day low, and day high; the next five are their latched copies. Loading also accepts a 44-byte trailer with a 32-bit timestamp, or exact RAM-only data. Other sizes and register values that exceed one byte are rejected before either RAM or the clock changes.

Saving advances a temporary live clock to the supplied host time. It leaves the running cartridge and its latch unchanged. Loading advances only live registers; the stored latch remains observable until the game latches again. Halt, day carry, out-of-range register counter states, and backward host time have explicit regression coverage. Missing, future, or implausibly old timestamps establish a fresh baseline and set the live carry bit so a game can detect a clock requiring initialization.

The Qt frontend saves after a quiet period following guest writes and flushes before reset, cartridge replacement, and close. Clock reads and elapsed host time alone do not mark a cartridge dirty. State loading and rewind mark the restored battery state dirty. Writes use atomic file replacement. Load, write, and replacement failures produce errors and preserve the existing file; a failed load also preserves the current cartridge session.

File > Recover Battery from Older State validates a native version 1 snapshot's size, integrity, and cartridge identity, then exports its RAM and supported clock data to a separate file. It does not change the running cartridge or restore older execution state. Cartridge, battery, and input snapshot paths are protected from accidental replacement during recovery.

Clock persistence currently covers MBC3. Other controllers with clocks, EEPROM, flash, motion sensors, or camera hardware still require separate implementation and validation. Battery-file interoperability is limited to the layouts described here.

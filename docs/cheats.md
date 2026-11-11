# Cheats and memory search

Tools > Cheats manages the current cartridge's rules. The global Enable Cheats control starts disabled. A rule also needs its own enabled checkbox. Rule edits and code imports preserve the underlying ROM and RAM bytes. The core replaces a matching read value after bank selection and before the guest bus is driven. Rules stay inactive while a boot ROM is mapped.

Add and Edit accept a hexadecimal address or `bank:address`, an eight-bit value, and an optional original-value comparison. An address without a bank applies to any bank. Import Code accepts Game Genie and GameShark formats. Descriptions use up to 127 UTF-8 bytes. The first enabled rule matching the address, bank, and comparison wins. Duplicate priority survives unrelated edits and file imports.

The selected ROM bank is compared before physical ROM-size aliasing. Cartridge RAM uses its wired bank mapping, and CGB work RAM uses its current hardware bank. Echo and other address regions retain their own matching rules. A banked rule only applies when that bank is visible to the guest.

## Files and session changes

The application reads `<storage-name>.cht` beside the cartridge. An archive retains its extension, such as `game.zip.cht`. Import File appends a compatible collection, and Export File writes the current rules. The file codec accepts either byte order and writes little endian. Global enable belongs to the running host session and is outside the file.

Edits apply immediately. Successful automatic saves replace the file atomically. If saving fails, the rules remain active and the window asks you to export them. An unreadable or invalid file produces a warning while the game opens. Cartridge, battery and state destinations are protected from cheat exports and automatic writes. A failed import preserves the current rules.

Reset preserves current rules and global enable, including edits awaiting export. Reopening the cartridge after closing the session reads its saved file. Native states and rewind restore guest hardware while keeping the current host rules. Closing a session with an export warning discards edits that could not be saved.

## Memory search

Tools > Cheat Search scans physical work RAM, cartridge storage, and the 127 high-RAM bytes. CGB hardware includes all eight physical work-RAM banks. The search reads actual bytes, including banks currently hidden from the CPU.

Choose an eight-bit value or a 16-bit little-/big-endian word. Filters support any value, six comparisons with a supplied hexadecimal value, and changed, unchanged, increased, or decreased comparisons with the previous snapshot. The first snapshot starts at zero. Each successful filter captures all current bytes after evaluating every candidate, so overlapping words use one consistent previous snapshot. Reset clears the history. A search following zero results starts a fresh candidate set.

Words can cross physical bank boundaries, but stop at the end of work RAM, cartridge storage, or high RAM. Add Cheat generates two separately banked byte rules for a word. Write Value edits its physical bytes and schedules battery persistence when cartridge storage changes. Custom expressions and decimal arithmetic remain pending.

## Embedding

`cupid/gb/cheats.h` exposes a host-owned collection. Attach it with `cupid_gb_set_cheats` and detach it before destruction. Mutations require an instruction boundary with no concurrent core access. Cloning preserves address-bucket priority as well as display order. Codec errors preserve the collection, and the host owns filesystem policy and atomic replacement.

Cheat collections and search history are outside native snapshots. The Qt frontend leaves the core's cheat pointer null when the collection is globally disabled or empty.

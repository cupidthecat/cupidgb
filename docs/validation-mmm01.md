# MMM01 upper-bank wiring checks

This checkpoint adds the `cupidgb_mmm01` regression against an 8 MiB generated cartridge with all 512 physical ROM banks populated. Each bank has an independent two-byte identity, so aliases across the ninth ROM address bit are visible. The same physical identities are exercised with the menu banks stored first and last.

## Generated guest coverage

The test runs 320 scenarios for each DMG/CGB and dump-layout combination, totaling 1,280 scenarios. Every scenario starts with a quick console reset. Guest instructions execute from work RAM so changing either cartridge window cannot replace the test program. They program the menu's controller registers, set the irreversible lock and attempt further configuration writes through ordinary CPU stores.

The matrix covers all four upper-ROM selections, four middle-bit selections, five held low-bit masks, multiplexed and ordinary wiring, and the mode-disable latch. Six lower-bank selections run in both requested mapping modes. Initial menu and selected-window observations produce 33,280 independent bank assertions. Four additional state checks save the final configuration, reset the receiver, import its native state and execute further selector writes, producing eight more bank assertions.

Both window identities are checked after every selection. Expected values retain the physical bank numbering across both file layouts. The locked configuration, writable low selector, multiplexed selector and coincident-window increment are exercised through the guest memory bus.

## Validation

The behavioral source revision is `ea667ad79aa28409f034b1fd6da7304e1844750d`. A private adapter executes the same generated instruction sequences and expected bank assertions against that revision. All 1,280 scenarios and four state continuations pass on each engine. This matrix demonstrated no controller mismatch requiring a core repair.

Windows MSVC Release and Linux GCC Release builds each pass the six selected checks below. The new regression takes less than one second in either Release build. A Linux Debug build also passes the new regression with AddressSanitizer, UndefinedBehaviorSanitizer and leak detection enabled. Fresh external mapper runs each pass 28 cases on DMG and 28 on CGB on both hosts, covering MBC1, MBC2 and MBC5. Those runs use explicit Fibonacci success, a 1,800-interval limit and fixed decimal startup seed `26154`. The formatting configuration matches the supplied source byte for byte, and the source-header check reports zero errors.

```sh
ctest --test-dir build-linux --output-on-failure \
    -R 'cupidgb_(mmm01|cartridge|mbc3_bus|mbc7|huc3|tpp1)$'
```

For the Windows configuration, use `--test-dir build-windows -C Release` with the same expression. Build those targets first when using a partial build.

These generated checks establish the stated register and native-continuation behavior. They do not qualify a commercial MMM01 compilation, every possible register sequence, physical cartridge hardware or sustained gameplay. The broader requirements remain in [the parity checklist](parity.md).

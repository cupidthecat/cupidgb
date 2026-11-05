# Contributing

CupidGB's goal is matching observable hardware behavior and desktop workflows while keeping the emulation core independent from Qt. Work should improve a demonstrated gap, preserve existing user data, and describe exactly what validation establishes.

## Before changing files

Verify the repository root, current branch, remotes, worktrees, existing changes, and applicable project instructions. Inspect existing issues and pull requests before creating another tracker. Preserve unrelated work, private evidence, ROMs, boot binaries, and saves.

Read the code that owns the behavior and the relevant sections of [the parity checklist](parity.md). Inspect the pinned behavioral reference before implementing corresponding behavior. Record revision identity and observations in private comparison receipts. Product code must be independently written CupidGB code; copying, renaming, vendoring, or linking another emulation core is outside the project workflow. General-purpose libraries remain permitted under their licenses.

Keep ordinary source, UI, docs, commits, issues, and release prose focused on CupidGB. Retain the root project license and the notices required for any dependency actually used or shipped. Do not remove a required notice merely because an implementation was renamed or edited.

## Code organization and formatting

Place hardware work in the owning core subsystem and host workflows in `src/frontend/qt`. Keep Qt types, device handles, dialogs, paths, host pacing, and playback queues out of `cupidgb_core`. The [architecture guide](architecture.md) describes the existing boundaries.

Use the repository `.clang-format` for project C/C++ files:

```sh
clang-format -i src/path/to/changed.c include/cupid/path/to/changed.h
clang-format --dry-run --Werror src/path/to/changed.c include/cupid/path/to/changed.h
git diff --check
```

Format only the changed project sources. Do not reformat generated files or third-party libraries. Preserve unrelated edits. Explain hardware quirks, clock boundaries, invariants, and non-obvious decisions in comments; avoid comments that restate ordinary operations.

## Behavioral changes

Derive requirements from observed source/runtime behavior, then implement them in CupidGB's own structure. Avoid special cases for test filenames, program counters, individual games, or expected output signatures in the emulation core. Test-success protocols belong in explicit validation tools.

Performance changes must preserve guest execution, timing, sound generation, and capture cadence. Compare equivalent settings and report the work performed. Faster host presentation with fewer emulated events is not an accuracy-preserving optimization.

When adding hardware state, audit native save encoding, domain validation, continuation, reset/boot profiles, and rewind. Preserve persistent model tags. Revise the native format deliberately when bytes or continuation semantics change. A cached rewind identity must be invalidated by cartridge/model replacement or any ROM patch.

File workflows should validate before replacing a live session or user file. Check short reads/writes, wrong inputs, invalid states, and cancellation. Use atomic replacement for saved files and retain existing data on failure.

## Validation and review

Add targeted regressions for behavioral fixes. Choose tests that exercise a hardware observable or failure contract, including meaningful boundary cases. Frontend workflows need runtime checks where isolated functions cannot establish the behavior.

Build and run appropriate Windows and Linux configurations and the sanitizer checks described in [testing and measurements](testing.md). Run external suites with explicit verdicts. For gameplay changes, use private disposable inputs to check graphics, audio, input, in-game saves, states, and sustained operation. Keep failures and inconclusive cases visible.

Review the final diff, formatting, source provenance, private-file exclusions, and validation receipts before committing. Group related fixes into coherent reviewable commits and a substantial issue/PR scope where practical. Before merging, confirm required checks passed on the exact current PR head; earlier green runs do not cover later edits. Respect explicit draft, pause, and no-merge boundaries.

Close an issue only when its acceptance criteria are met. Link implementation and validation evidence, and keep broader gaps open.

## Documentation

Update the relevant user/developer guide when controls, settings, formats, build steps, or integration contracts change. Write clear prose with concrete behavior and scope. Distinguish implemented behavior from tested behavior and pending work. Do not call a boot screen, screenshot match, unit test, bounded prefix, or menu session complete gameplay compatibility.

Use [the documentation index](index.md) to keep links and ownership clear. Validation results belong in the validation record; the user guide should describe how the current application works rather than accumulate milestone reports. Keep the README useful as an introduction and navigation point.

## Private inputs and cleanup

Keep local ROMs, boot files, saves, recordings, captured game images, comparison sources, and receipts outside public commits. `.local-research/` is ignored, but still review every staged path. The ignore file is a convenience, not a substitute for inspecting the diff.

Remove only task-created temporary outputs that are no longer used and have been checked against the intended workspace. Preserve active builds/caches, reference checkouts, unrelated worktrees, and user inputs. Cleanup must not erase the evidence needed to reproduce an unresolved result.

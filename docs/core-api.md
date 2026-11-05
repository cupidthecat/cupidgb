# Core API

`cupidgb_core` is a C11 static library with no Qt dependency. Public declarations live under `include/cupid`. The core owns emulated hardware; callers own host files, windows, device queues, pacing, and lifecycle coordination.

## Instance lifetime

Use a zero-initialized instance. `CupidGb` is large, so heap allocation avoids small platform stack limits. Its ROM pointer is owned by the core after initialization. Do not initialize an uninitialized stack object: initialization preserves selected fields, including the ROM pointer and model.

`CupidEmulator` provides a thin GB/GBC lifecycle wrapper:

```c
#include "cupid/core/emulator.h"

#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }

    CupidEmulator *emulator = calloc(1, sizeof(*emulator));
    if (!emulator) {
        return 1;
    }
    cupid_emulator_init(emulator, CUPID_SYSTEM_GB);
    if (!cupid_emulator_load_rom_file(emulator, argv[1])) {
        cupid_emulator_shutdown(emulator);
        free(emulator);
        return 1;
    }

    int result = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        if (!cupid_emulator_step(emulator)) {
            result = 1;
            break;
        }
    }

    cupid_emulator_shutdown(emulator);
    free(emulator);
    return result;
}
```

This example checks lifecycle and bounded execution only. A complete host must consume output and provide pacing, input, persistence, and error handling. Link the target through CMake with `target_link_libraries(your_host PRIVATE cupidgb_core)`.

For explicit hardware selection, call `cupid_gb_set_model` before loading the cartridge. The direct API in `cupid/gb/gb.h` exposes initialization, cleanup, header parsing, ROM loading, stepping, and memory access. `cupid_gb_load_rom` copies supplied cartridge bytes into its own storage. The wrapper selects CGB when initialized with `CUPID_SYSTEM_GBC`; `CUPID_SYSTEM_GB` uses the monochrome default unless a more specific model is selected.

When calling the C core from C++, wrap its C header includes in an `extern "C"` block, as the Qt frontend does. Some codec headers provide their own linkage guards, while the general lifecycle and hardware headers rely on the caller's wrapper:

```cpp
extern "C" {
#include "cupid/core/emulator.h"
#include "cupid/gb/input.h"
}
```

Loading a cartridge resets hardware. The direct loader is not a transactional session-replacement API. To preserve a running session after a failed load, build a separate candidate and swap it in only after cartridge, battery, and boot inputs validate. The Qt frontend uses that pattern.

## Execution and clocks

`cupid_gb_step` and `cupid_emulator_step` execute one instruction, interrupt entry, or bounded HALT/STOP work. They return false when execution cannot continue. They do not sleep or synchronize with a display.

`CupidGb.elapsed_cycles` counts base-speed T-cycles. At normal hardware speed the nominal clock is 4,194,304 Hz; a regular LCD frame spans 70,224 base-speed T-cycles. Do not infer guest time from CPU instruction count or host paint count. CGB double speed changes CPU-domain work while the base-speed clock remains the timekeeping domain.

Use both a cycle bound and an execution-step bound for unattended runs. The LCD can be disabled, so waiting only for `frame_ready` can leave a host loop without a useful presentation bound. Host stalls must not be corrected by silently dropping guest hardware work.

## Pixels and sound

| Output | Representation |
| --- | --- |
| `frame_buffer` | 160 by 144 monochrome shade indices |
| `frame_buffer_color` | 160 by 144 RGB555 values for color or compatibility palettes |
| SGB render API | 256 by 224 ARGB image with the game viewport and border |
| `cupid_gb_apu_drain` | Interleaved signed 16-bit stereo, left then right; returns stereo frame count |

Check and clear `frame_ready` when consuming completed images. Keep color/model selection consistent with the core's compatibility behavior; a monochrome cartridge can still use a color hardware palette. The Qt frontend's display pipeline is a host component and is not part of the C core.

Drain audio regularly, including while the LCD is disabled. The core's sample buffer is finite. A host that drains only on VBlank can lose output during prolonged LCD-off execution. `tools/rom_runner.c` drains per instruction; another host can use a bounded interval small enough to preserve every generated sample.

`cupid/gb/apu.h` exposes output sample-rate and high-pass controls. Changing the output rate discards queued and partial output frames while preserving oscillators. Clear or recreate corresponding host conversion state when changing formats or loading a state. Host volume, mute, resampling, and playback-device control belong outside the core.

## Input and memory access

Buttons use an active-low byte, ordered Right, Left, Up, Down, A, B, Select, Start. All released is `0xff`; pressing A alone is `0xef`. Call `cupid_gb_set_buttons(gb, released_mask)` from `cupid/gb/input.h` when supplying physical button state so selected-line interrupt behavior remains in the core. Selecting a row containing an already-held key can also request an interrupt. Releases and unchanged selected-line levels do not.

The direction-only scan resolves opposing direction contacts consistently; when both rows are selected, it preserves their wired-AND result. Monochrome row-switch settling and physical contact bouncing remain unimplemented. Do not interpret the immediate input API as complete electrical input timing.

`cupid_gb_read_u8` and `cupid_gb_write_u8` perform normal device accesses at the current hardware phase. They apply banking and access restrictions. They are not unrestricted debugger peek/poke operations and do not execute a CPU instruction's bus schedule by themselves. Tests that need bus phases should drive CPU execution or the relevant subsystem interface deliberately.

Do not mutate internal fields from another thread. The public structures expose implementation state but do not make concurrent access safe. Pause and serialize access at an instruction boundary before saving, loading, inspecting, or changing host integration settings.

## Battery data

`cupid/gb/battery.h` provides size, encode, decode, result-name, and clock helpers. The ordinary save/load functions use host time; `_at` variants accept an explicit timestamp for reproducible checks.

`cupid_gb_battery_size` returns zero when the loaded cartridge has no supported battery payload. Allocate sufficient independent storage and check both the result and returned length. Rejected loads preserve the instance. The caller handles file I/O and atomic replacement; the codec does not choose paths. Prefer this explicit result-bearing API for new hosts over the older convenience file-save helpers.

Battery data differs from execution state. See [battery saves](battery-saves.md) for encodings and clock semantics.

## Snapshots and rewind

Use `cupid_gb_state_size`, `cupid_gb_save_state`, and `cupid_gb_load_state` at an instruction boundary with no concurrent access. States exclude ROM/boot binaries and host resources. A restored active boot overlay needs the matching binary already available to the target session.

Normal disk-state APIs recompute cartridge identity. A rejected load leaves the complete target unchanged. After a successful load, discard queued host audio, reset conversion/pacing baselines, reconcile physical input, and redraw. The core's pending samples are part of the restored state; the host playback queue is not.

`CupidGbStateContext` caches cartridge identity for trusted in-memory rewind. It binds to one owner, ROM pointer, ROM size, and model. Discard it after cartridge/model changes or any in-place ROM patch. Binding checks cannot detect a patch that preserves the pointer and size. Never use the cached context to validate an untrusted disk file.

The core supplies snapshot bytes. The host supplies compression, history limits, storage, and UI. The complete wire-format and continuation contract is in [save states and rewind](save-states.md).

## Errors and compatibility

Lifecycle/step functions use booleans; snapshot and battery operations return typed result codes with readable name helpers. Logging reports additional loading and execution details. File loading through narrow C paths can depend on host filesystem encoding; the Qt frontend reads local cartridge and boot files through Qt before handing bytes to the core.

The exposed structures are an internal integration surface, not a promised stable binary ABI. Preserve persistent model tags and version serialized formats deliberately when hardware state changes. API source presence, unit tests, and a bounded run do not establish complete hardware compatibility.

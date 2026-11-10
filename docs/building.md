# Build and installation

CupidGB uses CMake for the hardware core, command-line ROM runner, Qt desktop application, and regression checks. Keep build outputs separate from source files. The commands below run from the repository root.

## Requirements

| Component | Requirement |
| --- | --- |
| CMake | 3.21 or newer |
| Compilers | C11 and C++17 |
| Qt | 6.4 or newer, Widgets and Multimedia modules |
| SDL2 | 2.24 or newer for the Qt desktop controller backend |
| Python | 3.9 or newer when tests are enabled |
| Build tool | Ninja, Make, or the matching Visual Studio generator |

CMake enables both C and C++ languages even in a core-only configuration. Qt and SDL2 are discovered only when `CUPIDGB_BUILD_QT` is enabled. The core and command-line runner do not link either library. Windows uses its system hashing API for large one-shot hashes. On Linux, CMake enables the optional OpenSSL 3 Crypto backend when its development files are found; otherwise the portable SHA-256 implementation remains available. ROMs and boot ROMs are local inputs and are not distributed with the application.

## Linux

On Debian or Ubuntu, install the development packages:

```sh
sudo apt-get install build-essential cmake ninja-build python3 qt6-base-dev qt6-multimedia-dev libsdl2-dev
```

Configure, compile, and run the tests:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/cupidgb /path/to/cartridge.gb
```

Install `libssl-dev` as well to enable the optional native hashing path on Debian/Ubuntu. A build without it remains supported and uses the same hash format.

If Qt is outside the system search paths, add `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64` when configuring. Use the actual installation path.

The desktop application needs a working graphical session and a usable audio output backend. An offscreen workflow check can run without a visible desktop:

```sh
QT_QPA_PLATFORM=offscreen ./build/cupidgb --smoke-test
```

That check does not establish visible presentation or real audio playback. From a desktop session, also run:

```sh
./build/cupidgb --smoke-test --audio-check
```

## Windows

Use an x64 Visual Studio 2022 compiler, a matching MSVC Qt package and an x64 SDL2 development package. The paths below are placeholders. The [SDL2 2.28.5 release](https://github.com/libsdl-org/SDL/releases/tag/release-2.28.5) supplies `SDL2-devel-2.28.5-VC.zip`; extract its complete directory, including `cmake`, `include`, `lib/x64` and `COPYING.txt`. The verified archive SHA-256 is `4ac4ba2208410b7b984759ee12e13e0606bd62032b5ddc36fb7d96b9ade78871`. CI checks that archive before extraction. A compatible newer SDL2 package can also be selected through its CMake configuration; SDL3 is a different API.

From PowerShell with Visual Studio installed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64;C:/Libraries/SDL2-2.28.5"
cmake --build build --config Release --parallel 4
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
./build/Release/cupidgb.exe C:/Games/cartridge.gbc
```

For Ninja, first open an x64 Visual Studio developer shell so compiler, linker, Windows SDK, and runtime paths agree:

```powershell
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64;C:/Libraries/SDL2-2.28.5"
cmake --build build-ninja --parallel 4
ctest --test-dir build-ninja --output-on-failure
./build-ninja/cupidgb.exe C:/Games/cartridge.gb
```

Do not reuse one build directory for different generators, architectures, or compiler families. A Qt package built for a different compiler is not interchangeable with the selected MSVC package.

If package discovery fails, pass `-DSDL2_DIR=C:/Libraries/SDL2-2.28.5/cmake`, using the actual directory containing `sdl2-config.cmake`. The application links the SDL2 shared target and keeps Qt's entry point; it does not use SDL2main. The Windows build copies the selected SDL2 DLL beside the application and controller test executables. It does not install or modify a system-wide SDL library.

An existing MSYS2 SDL2 2.28.5 SDK was also qualified with MSVC. Its shared import library links successfully. The project excludes that SDK's general MinGW runtime include directory so it cannot shadow MSVC's standard headers, and disables the incompatible `strings.h` feature macro for MSVC. This is a compatibility path for that tested SDK, not a promise that arbitrary MinGW headers or static libraries work with MSVC. Prefer the matching VC package for deployment.

## Core-only build

Disable the desktop target when running hardware checks without Qt:

```sh
cmake -S . -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DCUPIDGB_BUILD_QT=OFF
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
./build-core/cupidgb_rom_runner --help
```

This still builds `cupidgb_core` and `cupidgb_rom_runner` without finding or linking Qt or SDL2. It does not test the Qt frontend.

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `CUPIDGB_BUILD_QT` | `ON` | Build the desktop executable and frontend checks |
| `CUPIDGB_ENABLE_TESTS` | `ON` | Build and register regression checks; requires Python |
| `CUPIDGB_ENABLE_IPO` | `ON` | Enable compiler/linker interprocedural optimization in Release when supported; disabled with sanitizers |
| `CUPIDGB_ENABLE_NATIVE_HASH` | `ON` | Use Windows system hashing or detected OpenSSL 3 Crypto on Linux for large hashes; `OFF` selects the portable path |
| `CUPIDGB_ENABLE_WARNINGS` | `ON` | Enable the core's compiler warning flags |
| `CUPIDGB_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer with GCC or compatible Clang command-line options |

CMake probes interprocedural optimization support for both C and C++. A successful probe enables it only for `Release`; an unsupported toolchain falls back to ordinary compilation. `CUPIDGB_ENABLE_IPO=OFF` also forces that fallback. This option does not change guest accuracy settings or remove validation.

For GCC Release builds with IPO, project C++ targets also use `-fno-strict-aliasing`. This protects shared C hardware records when calls across the C/C++ boundary copy or replace an aggregate. The C/C++ integration regression checks reset, memory access, state restoration, and serial observation under optimization. Keep this flag when reproducing or embedding that build configuration.

For a sanitizer build on Linux:

```sh
cmake -S . -B build-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCUPIDGB_BUILD_QT=OFF -DCUPIDGB_ENABLE_SANITIZERS=ON
cmake --build build-sanitized --parallel 4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-sanitized --output-on-failure
```

The MSVC configuration rejects this sanitizer option. Enabling sanitizers suppresses IPO even in Release. Keep sanitizer results separate from Release runtime measurements, and record the effective IPO setting in performance comparisons.

## Installation and deployment

The desktop configuration installs the executable, project license and supplied SDL2 notices. Windows installation also copies the selected SDL2 runtime. Linux installation supplies a desktop entry, GB/GBC MIME descriptions, and an SVG application icon:

```sh
cmake --install build --prefix /path/to/install
```

For a multi-configuration generator, add `--config Release`. The install rules do not assemble all Qt libraries, plugins, platform dependencies, or an installer. Linux desktop registration files make CupidGB available as an opener; they do not set the system's default association automatically.

For a Windows folder deployment, use a Visual Studio x64 developer shell so `windeployqt` can locate the compiler runtime. Copy the Release executable into a separate staging folder, then run the matching Qt deployment tool on that copy:

```powershell
New-Item -ItemType Directory -Force package
Copy-Item build/Release/cupidgb.exe package/cupidgb.exe
Copy-Item build/Release/SDL2.dll package/SDL2.dll
C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe --release package/cupidgb.exe
Copy-Item LICENSE package/LICENSE
Copy-Item third_party/SDL2-LICENSE.txt package/SDL2-LICENSE.txt
Copy-Item third_party/SDL2-COPYING.txt package/SDL2-COPYING.txt
./package/cupidgb.exe --smoke-test --audio-check
```

Include the notices and license material required by the libraries actually shipped. Test the staged folder with development Qt paths removed from its environment. Check launch, dialogs, images, audio, and clean shutdown; dependency copying alone does not qualify deployment.

Qt continues to own video, audio and its event loop. SDL2 initializes only controller-related subsystems. The controller virtual-device tests need no physical gamepad, but their success does not qualify real drivers, motors or motion devices. See [controllers](controllers.md) for the input boundary and test limits.

Review deployment warnings before distributing the folder. The local deployment check resolves the Windows SDK's x64 Direct3D compiler directory through `PATH` and the compiler runtime through the developer shell. A successful launch on a development machine does not establish that a clean installation has every required runtime.

On Linux, the executable can use distribution-provided Qt and runtime packages. For a per-user installation:

```sh
cmake --install build --prefix "$HOME/.local"
```

Make sure `$HOME/.local/bin` is in your session's `PATH`, because the desktop entry launches `cupidgb` by name. If the desktop does not refresh its application or MIME databases automatically, run the tools provided by your distribution:

```sh
update-desktop-database "$HOME/.local/share/applications"
update-mime-database "$HOME/.local/share/mime"
```

These commands require the corresponding desktop-file and shared-MIME utilities. They update the user's installed metadata; they do not make CupidGB the default handler. The repository does not currently supply an installer or portable Linux bundle. Staged-install checks do not establish behavior in every desktop environment. See [troubleshooting](troubleshooting.md) for plugin and runtime path problems.

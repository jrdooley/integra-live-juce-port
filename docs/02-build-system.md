# 02 — CMake build of libpd, the Pd externals and libIntegra

Before this port libIntegra could only be built with the Xcode project (macOS,
linking prebuilt static libraries committed to the repo) or the Visual Studio
solution (Windows, prebuilt DLLs). There was no Linux build. This step adds a
single CMake build that compiles everything from source.

## Layout

```
CMakeLists.txt                  top level: options, dependencies, subdirectories
cmake/IntegraDependencies.cmake finds system libraries, falls back to FetchContent
third_party/CMakeLists.txt      integra_libpd, integra_flext, integra_pdexternals,
                                integra_minizip, integra_tmpfileplus
libIntegra/CMakeLists.txt       Integra (static library) + generated version.h
libIntegra/tests/CMakeLists.txt GoogleTest unit tests + headless smoke test
```

Options: `INTEGRA_BUILD_TESTS` (default ON), `INTEGRA_BUILD_APP` (default ON —
the JUCE app, see later docs).

## Dependencies

| Library | Used by | Resolution order |
|---|---|---|
| libxml2 | libIntegra (IXD/IID parsing, schema validation) | `find_package(LibXml2)` |
| zlib | minizip | `find_package(ZLIB)` |
| Lua 5.2 | libIntegra script engine | pkg-config `lua5.2`/`lua-5.2`/`lua52`, else FetchContent lua-5.2.4 |
| libsndfile | `fsplay~` | pkg-config `sndfile`, else CMake `SndFile` |
| FFTW3 (float) | `partconv~` | pkg-config `fftw3f`, else CMake `FFTW3f` |
| PortAudio | libIntegra audio engine | pkg-config `portaudio-2.0`, else FetchContent v19.7.0 |
| PortMidi | libIntegra MIDI engine | pkg-config / `find_library`, else FetchContent v2.0.4 |
| GoogleTest | unit tests | `find_package(GTest)`, else FetchContent v1.14.0 |

On Ubuntu 24.04:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
    libxml2-dev liblua5.2-dev libsndfile1-dev libfftw3-dev \
    portaudio19-dev libportmidi-dev libasound2-dev libjack-jackd2-dev libgtest-dev
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## How libpd and the externals are built

* `integra_libpd` compiles the same file list as libpd's own Makefile
  (Pd 0.43.3 core + `libpd_wrapper` + `libpd_wrapper/util` + the C++ `PdBase`),
  with `PD USEAPI_DUMMY HAVE_UNISTD_H HAVE_LIBDL`. The four Pd "extra"
  externals Integra registers (`bonk~`, `fiddle~`, `lrshift~`, `expr`/`expr~`/`fexpr~`)
  are compiled into the same library.
* `integra_flext` is flext built static and multi-threaded for Pd
  (`FLEXT_SYS=2 FLEXT_THREADS`), equivalent to the old prebuilt
  `libflext-pd_t.a`. The optional SndObj/STK bindings are excluded.
* `integra_pdexternals` contains `partconv~`, `freeverb~`, `copy`,
  `soundfile_info` and `fsplay~` (libsndfile backend).
* All of these are linked statically into libIntegra, whose
  `CDspEngine::register_externals()` calls their `*_setup()` functions — the
  same mechanism as the original macOS build, so no Pd search paths or
  dynamically loaded externals are involved.

## Source changes required for Linux / GCC 13 / C++17

| File | Change | Why |
|---|---|---|
| `libIntegra/api/common_typedefs.h` | include `<cstdint>`; `INTEGRA_API` blank when `INTEGRA_STATIC` | GUID struct uses `uint32_t`; static library on Windows must not use `dllimport` |
| `libIntegra/api/polling_notification_sink.h` | include `command_source.h` | public header used `CCommandSource` without including it |
| `libIntegra/src/platform_specifics.h` | `sprintf_s` shims and `version.h` for every non-Windows platform (was `__APPLE__` only); include `<algorithm> <cmath> <limits>` | Linux needs the same POSIX shims; the deleted Xcode prefix header used to provide those standard headers |
| `libIntegra/src/file_helper.cpp` | `get_resources_path()` now: `$INTEGRA_RESOURCES_PATH` → `<binary dir>/Resources/` (macOS) or `resources/` (Linux/Windows) → build-tree `libIntegra/data/` | was macOS-only, returned "" elsewhere, so the XSD schema and legacy id table could not be found |
| `libIntegra/src/scratch_directory.cpp` | fall back to `$HOME`, then `/tmp`, when `$TMPDIR` is unset | the old fallback was the literal string `"~/."` – tilde is never expanded by `mkdir`/`fopen`, so on Linux the host patch could not be written and Pd had nothing to load |
| `libIntegra/src/*.{h,cpp}` | `#include "zip.h"` instead of `"../externals/minizip/zip.h"` | third-party code moved to `third_party/` |
| `third_party/pd-externals/copy/copy.c` | define `O_BINARY` wherever it's missing (was `__APPLE__` only) | Linux has no `O_BINARY` |
| `third_party/pd-externals/fsplay~/main.cpp` | leak the static `flext::ThrCond` | glibc's `pthread_cond_destroy` blocks while the fsplay reader thread is waiting on it, so every process using libIntegra hung at exit on Linux |
| `libIntegra/tests/main.cpp` | module directory and VERSION file paths injectable via compile definitions | tests hard-coded `Integra.framework/Resources/modules` |

## Tests

* **`libIntegraTests`** — the original GoogleTest suite from integra-framework
  (session start/stop, version, module list, new/delete/set/rename/move/load/
  save commands, loading older and refusing newer `.integra` files).
  **All tests pass on Linux.** (≈90 s, mostly PortAudio probing for ALSA/JACK
  devices on every session start in a container without sound hardware.)
* **`integra_smoke`** (new) — starts a session, creates `AudioSettings`,
  instantiates **every** module with a DSP implementation (63 modules) inside a
  container, lets the no-device audio thread run the Pd patch, pings every
  module through libpd, then loads all six demo blocks and pings again.
  Run with `INTEGRA_SMOKE_VERBOSE=1` to see libIntegra progress and the Pd
  console.

### Findings from the smoke test

* No `... couldn't create` errors: every abstraction and every external used by
  the module library resolves in the new build.
* `Delay` and `VibratoChorus` do not answer pings: their
  `handlers/ntg_receive.pd` sends the reply to `$0-integra` instead of
  `integra` (all other 61 modules use `integra`). This is a pre-existing bug in
  those two module files, not a port regression; it only affects the ping
  diagnostic, not audio or control. Left unchanged to keep module checksums
  identical to shipped Integra Live projects.
* The Pd console also shows messages produced by the module patches themselves
  when run with silent input and no files loaded (e.g. granular synth
  `throw~` without `catch~` until a voice is allocated, soundfilers reading an
  empty path, analysers emitting values outside their declared ranges which
  libIntegra rejects). They are platform-independent behaviour of the
  unchanged Pd patches and were invisible in the original app because the Pd
  console was never shown.

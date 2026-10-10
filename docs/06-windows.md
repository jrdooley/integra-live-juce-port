# 06 — Windows (and macOS) build

Integra Live 1.x shipped on Windows using a Visual Studio solution, pthreads-win32 and a
`dirent.h` shim, with libIntegra built as a DLL. The JUCE port builds on Windows
with the same CMake project as Linux, using MSVC and vcpkg for the C library dependencies.
This document records what was needed. Most of the fixes also made the macOS build work.

## Building

Requirements: Visual Studio 2022 (MSVC, x64), CMake ≥ 3.22, and
[vcpkg](https://vcpkg.io).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
      -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config RelWithDebInfo --parallel
ctest --test-dir build -C RelWithDebInfo -E gui_test
```

The dependencies come from `vcpkg.json` (manifest mode): libxml2, zlib, libsndfile, FFTW,
PortAudio, PortMidi and GoogleTest. All of them are restricted to `"platform": "windows"`,
so the manifest is ignored on Linux and macOS, which use system packages. The
`x64-windows-static-md` triplet links them statically against the DLL C runtime, so
`Integra Live.exe` needs no DLLs next to it. JUCE and Lua are fetched by CMake as on
the other platforms.

The executable is `build/app/IntegraLive_artefacts/RelWithDebInfo/Integra Live.exe`. The
`modules/`, `blocks/` and `resources/` directories are copied next to it, which is
where the app looks for them.

## What had to change

### Threads and files in libIntegra (commit c58947b)

libIntegra used pthreads (`pthread_mutex_t`, `pthread_t`), POSIX and macOS named
semaphores, `gettimeofday`/`usleep`, and `dirent`/`stat`/`mkdir`. These were all
replaced with standard C++17:

* `std::mutex`, `std::thread`, `std::condition_variable` and `std::chrono`
* a small counting semaphore (`integra_semaphore.h`)
* new `std::filesystem` helpers in `file_helper` (`make_directory`, `list_directory`,
  `is_directory`, `delete_directory`; paths are UTF-8 via `u8path`)

The server lock's owner check now uses `std::thread::id` and is race-free. The macOS
build also stopped sharing named semaphores between processes. The tests use
`std::filesystem` as well, so the bundled `dirent.h` shim was removed.

### Pd / libpd

* **Threads**: Pd 0.43 needed only a few pthread calls, which a small
  `win32-pthread/pthread.h` shim mapped onto Win32. Pd 0.56 (see
  [10-pd-upgrade.md](10-pd-upgrade.md)) uses more of pthreads, so Windows builds now link
  vcpkg's `pthreads` (PThreads4W) and the shim is gone.
* **`t_int` on Win64**: `t_int` was `long`, which is 32 bits on 64-bit Windows. Pd keeps
  pointers in `t_int` (DSP chains, pointer arguments), so they were truncated and every
  session crashed on start. It is now `long long` on `_WIN64`, as in Pd 0.47+.
* **Apple arm64** (macOS): the `mess0`–`mess5` macros called methods through the
  variadic `t_gotfn`. On Apple Silicon, variadic arguments are passed on the stack, so the
  `dsp` message gave `canvas_dsp` a garbage signal pointer, and every test crashed when
  the first module was created. The macros now cast to fixed-arity function types
  (backported from Pd 0.51).
* `MSW`, `PD_INTERNAL` and `WINVER`/`_WIN32_WINNT` are defined for Windows builds. The Pd
  headers need `MSW` to pick their Windows paths (for example `int32` in iemlib).

### Externals

* **flext** (used by `fsplay~`) now uses native Win32 threads on Windows
  (`FLEXT_THR_WIN32`) instead of pthreads. Its lock-free queue implemented
  compare-and-swap with 32-bit-only MSVC inline assembly, which doesn't exist on x64.
  It now uses `_InterlockedCompareExchange{,64,128}`. The single-word CAS also handles
  64-bit pointers now, and `atomic_ptr` is aligned for the 16-byte CAS.
* The bundled Boost (used by `fsplay~`) predates C++17. `BOOST_NO_AUTO_PTR` is defined,
  because libc++ (macOS) no longer has `std::auto_ptr`.
* `iemlib`, `copy` and `tmpfileplus` include `<unistd.h>` only on POSIX. iemlib no longer
  defines a second copy of `sys_noloadbang` on Windows.

### libIntegra source fixes

* `windows_build_stuff.h` (not in the original repository) is replaced by MSVC warning
  pragmas in `platform_specifics.h`. The version string comes from `LIBINTEGRA_VERSION`
  instead of the old DLL version resource. The `DllMain` stub is gone, because libIntegra
  is a static library.
* `CFileIO::convert_input()` called a libxml2 encoding handler's `input` function
  directly. That member became a union in libxml2 2.14 (the vcpkg version). The only
  encoding used is ISO-8859-1, so it now converts Latin-1 to UTF-8 itself. This also
  fixes a `new[]`/`free()` mismatch.
* `std::binary_function` bases were removed (it was removed in C++17, and MSVC and libc++
  no longer have it).
* `libIntegra/VERSION` was renamed `VERSION.txt`. On case-insensitive file systems
  (macOS, Windows), the extensionless file in a public include directory was being
  picked up in place of the standard `<version>` header.

### CMake

* `cmake/IntegraDependencies.cmake` accepts several candidate target names per package,
  because vcpkg's config packages export different names (`portaudio_static`,
  `PortMidi::portmidi`, …) from the pkg-config ones used on Linux.
* Third-party C code is compiled with warnings off (`/w` on MSVC, `-w` elsewhere).

## Continuous integration

`.github/workflows/build.yml` has a `windows` job on `windows-2022`. It configures with the
vcpkg toolchain and triplet above, builds `RelWithDebInfo` and runs CTest. The GUI
interaction test is excluded: it needs an interactive desktop session. The vcpkg binary
cache is restored before configure and saved right after configure, so the dependencies
(about 10 minutes from scratch) are built only once per `vcpkg.json` change, even when a
later step fails. If a test crashes, the job reruns `integra_host_audio_test` under
`cdb` to put a symbolised stack trace in the log.

## Verification

GitHub Actions, `windows-2022` runner, MSVC 2022 x64, `RelWithDebInfo`:

| Test | Result |
|---|---|
| `libIntegraTests` (upstream GoogleTest suite, 29 tests) | pass |
| `integra_smoke` (all 63 modules instantiate and answer pings, 6 demo blocks load) | pass |
| `integra_host_audio_test` (signal through libpd, host-driven) | pass |
| `IntegraLive_self_test` (48 end-to-end checks of the app core, including the JUCE audio callback path) | pass |
| `IntegraLive_gui_test` | not run in CI (needs an interactive desktop session) |

The same commits also turned the macOS job (macOS 14, Apple Silicon) green: build plus
tests 1–4. Before them, macOS failed to compile (`<version>` shadowing, `auto_ptr`,
`binary_function`), and then crashed on the first module (the arm64 variadic-call issue
above).

Not verified: the GUI with real audio and MIDI devices on Windows (WASAPI/ASIO via JUCE,
PortMidi). CI machines have no audio hardware; the app falls back to its no-device
processing thread, as it does on Linux.

### How it got there (CI iterations)

1. vcpkg dependencies and CMake configure: fine on the first run.
2. flext's lock-free CAS used x86 inline assembly → MSVC intrinsics.
3. libxml2 2.14 API change in `convert_input()` → own Latin-1 → UTF-8 conversion.
4. Everything built, but every test crashed at session start (access violation) →
   32-bit `t_int` on Win64 → `long long`.
5. Green.

# 10 — Upgrade to libpd 0.16.1 (Pd 0.56-5)

The port shipped the Pd the original Integra Live used: Pd 0.43.3 (2010) inside an
old libpd. The robustness pass ([09](09-robustness.md)) found that every serious bug was
in that old C code, and many were already fixed upstream, so Pd was upgraded to the
latest libpd, **0.16.1**, which bundles **Pd 0.56-5**.

## What was replaced

`third_party/libpd/` now holds libpd 0.16.1: `pure-data/src` (Pd 0.56-5), the
`pure-data/extra` objects libpd registers itself (`bob~ bonk~ choice fiddle~ loop~
lrshift~ pd~ pique sigmund~ stdout`), `libpd_wrapper` and the header-only `cpp`
wrapper. The `integra_libpd` CMake target uses the same file list and definitions as
libpd's own `CMakeLists.txt`. On Windows, Pd 0.56 needs real pthreads, so it links
vcpkg's `pthreads` (PThreads4W) instead of the old `win32-pthread` shim, and defines
`PD_LONGINTTYPE=long long` as libpd does for 64-bit Windows. `expr`/`expr~`/`fexpr~`
are now part of Pd itself, and `bonk~`, `fiddle~` and `lrshift~` are registered by
`libpd_init()`, so libIntegra no longer sets them up. Integra's own externals
(`partconv~`, `freeverb~`, `soundfile_info`, `fsplay~`, `copy`) and flext 0.6 build
unchanged against the new headers.

## libIntegra changes

| Change | Why |
|---|---|
| `pd_message_queue.h`: the old `pd::Message` type and a queue filled from libpd's receiver callbacks | libpd 0.16's C++ wrapper dropped the message queue (`numMessages()`/`nextMessage()`) that libIntegra's feedback path is built on. The callbacks run on the thread that processes or sends to Pd, which always holds `CDspEngine`'s mutex, as before. Capped at 1000 messages like the old queue. |
| Modules are deleted with `glist_delete()` (DSP suspended around it) instead of sending the host canvas `find +<id>` and `cut` | Since Pd 0.47 `find` opens the canvas and searches subpatches, and `cut` is ignored on a canvas with no window: **deleted modules stayed in the DSP graph**, running and sending feedback for nodes that no longer existed. The patch id libIntegra keeps is the object's index in the host canvas. |
| The engine closes its host patch when the session ends | Old libpd's `clear()` reset Pd; the new one only detaches callbacks, so every earlier session's patch kept running (the unit tests, which start many sessions, ran out of memory). |
| `pd compatibility 0.43` is sent at start-up | Pd keeps the old behaviour of objects it has since changed behind this level, so the module patches behave as they were written. |

## Pd fixes carried in the vendored copy

Marked `Integra Live port` in the source.

| File | Fix |
|---|---|
| `x_connective.c` | `[makefilename]` output bounded with `snprintf`. Still unbounded upstream: a long symbol (module patches build them from user text) overflowed the stack buffer — the stress test's 5000-character text crashed Pd 0.56 too. Upstream now rejects multi-conversion formats itself. |
| `x_time.c` | `[metro]` honours the compatibility level for its 0.45 change: below 0.45 an interval under 1 ms is raised to 1 ms, as Pd 0.43 did. Otherwise a tiny interval made the scheduler fire the metro endlessly within one tick (the stress test hung). |
| `x_vexp.c` | `fexpr~`'s `$y` shorthand (for `$y1`) recorded a type for inlet 1, so `fexpr~ if($x1 >= $y[-1], $x1, $y[-1]*$f2)` (used by a module) failed to create with "inlets can only have one type". |
| `m_pd.c`, `g_canvas.c` | A freed canvas unbinds itself from `#A`. `canvas_popabstraction()` leaves the last loaded abstraction bound there; Integra loads modules as abstractions and deletes them, and the next array deletion read the freed canvas (use-after-free, found by ASan). |
| `s_inter.c` | With no GUI, GUI commands are dropped (as in 0.43) instead of written to stderr (flext's Tcl code filled the log). |
| `d_osc.c`, `d_soundfile.c`, `d_array.c` | Signed-overflow undefined behaviour in `noise~`, 16/24/32-bit soundfile decoding and `tabsend~`'s redraw counter. |

The fixes from the robustness pass to Pd 0.43's `q8_rsqrt`, soundfile float I/O and the
libpd C++ wrapper are not needed: upstream rewrote that code.

## Remaining differences in the Pd console

Compared with Pd 0.43 on the same smoke test (every module and demo block), the new Pd
also prints, harmlessly:

- `expr: deprecated old func("$s#") format detected` — four modules use
  `size("$s1")`; Pd 0.56 still accepts it.
- `makefilename: invalid format string '%s%s'` — a placeholder in the soundfile modules'
  `join_path` that is always replaced (`set $1%s`) before use.

## Verification

- All six ctest suites pass, on the normal build and under ASan + UBSan.
- **Module fingerprints** (`integra-live --fingerprint-modules [module]`): every module at
  its defaults is wired between the block's input and output and fed the same signal (220,
  440 and 1320 Hz partials plus a click every 0.5 s) for 2 s, with the engine driven only by
  the test so runs are repeatable. Modules that need more get it: synths are banged
  (`test`) every 0.25 s, file inputs get a generated WAV with one channel per output (then
  `play` and `loop`), the auto-panner's `auto` is on, and all audio outputs are summed.
  Players stream from disk on a thread, so modules with files run in real time. The RMS
  and peak of the last second are printed, then every control value the module reported
  back, then the same for each demo block played from the start. The same code was built
  against Pd 0.43 (the previous commit) and Pd 0.56:

  | | Pd 0.43 vs Pd 0.56 |
  |---|---|
  | Audio output (66 modules, RMS and peak to 5 digits) | **63 identical** (StereoGranularSynthesizer and SpectralVocoder load their file on a thread, so a full run can catch them at a different moment; run on their own three times each they match exactly); PianoReverbMSP 0.01% and Convolution 0.1% apart (Convolution varies run to run by as much on either version); **PhaseVocoder 9% louder in RMS (peak 5% lower) — see below** |
  | Reported values (609 endpoints: analyser readings, levels, states) | **all identical** |
  | Demo blocks (6, played from the start) | **all identical** (ScriptedFreezer 0.01% in peak) |

  The analysers' readings are real (e.g. BrightnessAnalyser reports 220 for the 220 Hz
  input). The Pd 0.43 output is kept in
  [data/module-fingerprints-pd0.43.txt](data/module-fingerprints-pd0.43.txt) for later
  comparisons.

  Silent on both versions with this input: the 11 analysers and Metronome (they report
  values, not audio — compared above), HarmonicFilter, and QuadSoundfiler/Octosoundfiler,
  whose players never receive the file in this test; not investigated further.

  **PhaseVocoder** is consistently 9% louder on Pd 0.56 (0.2218 vs 0.2033 RMS). Ruled out
  by testing each on both Pd versions with the same input: `rfft~`/`rifft~`, `sqrt~`,
  `rsqrt~`, `tabsend~`/`tabreceive~`, `tabread4~`, `[soundfiler] read`, `expr` (including
  `size("$s1")`) and `[sin]`. One difference found, `samplerate~` before DSP starts (0 on
  Pd 0.43, the real rate on 0.56), is not reached in the module's path here. Worth a
  listen when testing on real hardware.

### Found along the way: no sound file playback

The fingerprints showed that **`fsplay~` could open no file at all in this port** — on Pd
0.43 as well — so Soundfiler, StereoSoundfiler and the demo blocks that use them played
silence. `fsp_libsndfile.cpp` registers the libsndfile reader through a global's
initialiser; built into a static library with nothing referencing that object file, the
linker left it out. `fsplay::Setup` now references the global. All six demo blocks play
since (four were silent). Also fixed: the app never set `EndpointInfo::isInputFile`
(libIntegra's public API had no way to ask; `IStateInfo::get_is_input_file()` added).

### Platform fixes after the upgrade

- **macOS and Linux**: `copy` and `partconv~` called `error()`, which Pd 0.56 no longer
  declares. Apple clang refused; on Linux the call silently linked against glibc's
  unrelated `error(status, errnum, fmt)`, which exits the process. They use `pd_error()`,
  and implicit function declarations are now compile errors for Pd and the externals on
  every compiler.
- **Windows**: `iemlib.h` relied on Pd 0.43's `int32`; and since Pd 0.56's `m_pd.h`
  declares everything `dllimport` outside libpd itself, the static build defines
  `EXTERN=extern` (libpd's own CMake has the same switch, `PD_DEFINE_EXTERN`).
- **Convolution on macOS**: CI's stress test still saw NaN from `partconv~` after its
  file was set to an empty or invalid value. Not reproducible on Linux, so the exact
  origin is unconfirmed; `partconv~` now replaces non-finite impulse-response samples with
  0 and, if a block's output is ever non-finite, outputs silence and clears its
  accumulators so it recovers instead of staying NaN.

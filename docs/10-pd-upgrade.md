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
- **Module fingerprints** (`integra-live --fingerprint-modules`): every module at its
  defaults is wired between the block's input and output, fed the same signal (a 220 Hz
  sine plus a click every 0.5 s) for 2 s with the engine driven only by the test, and its
  output RMS and peak are printed, followed by every control value it reported back. The
  same code was built against Pd 0.43 (the previous commit) and Pd 0.56:

  | | Pd 0.43 vs Pd 0.56 |
  |---|---|
  | Audio output (66 modules, RMS and peak to 5 digits) | **65 identical**; PianoReverbMSP differs by 0.005% in RMS |
  | Reported values (609 endpoints: analyser readings, levels, states) | **all identical** |

  The analysers' readings are real (e.g. BrightnessAnalyser reports 220 for the 220 Hz
  input). The Pd 0.43 output is kept in
  [data/module-fingerprints-pd0.43.txt](data/module-fingerprints-pd0.43.txt) for later
  comparisons. Modules that stay silent with this input on both versions (synths and
  players that need a note or a file, the quad panners' other outputs) are covered by the
  stress test's value sweep rather than by this comparison.

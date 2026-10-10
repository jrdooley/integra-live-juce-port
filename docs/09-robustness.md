# 09 — Robustness pass

Goal: find crashes and memory errors before a live performance does, without
relying on anyone's taste. Three tools were added and run on Linux.

## Tools

| Tool | How to run | What it does |
|---|---|---|
| Stress test | `integra-live --stress-test [seconds] [project files...]` (ctest: `IntegraLive_stress_test`, 20 s, runs in CI on all three platforms) | See below |
| Sanitizer build | `cmake -S . -B build-asan -G Ninja -DINTEGRA_SANITIZE=ON` | AddressSanitizer + UndefinedBehaviorSanitizer for the app, libIntegra, libpd and every external (GCC/Clang only) |
| Self/GUI tests under the sanitizers | `build-asan/.../integra-live --self-test`, `--gui-test`, `--stress-test 60` | The existing 76 + 227 checks, now also memory-checked |

The stress test (`app/Source/Core/StressTest.cpp`):

1. **Every project** — each demo block (and any `.integra` file given on the command
   line) is opened, played (every scene activated), saved, reopened (same tracks and
   scenes) and imported into a new project as a block. After each step every node in
   the model must exist in libIntegra.
2. **Every module, every control** — each of the 66 user modules is added to a block,
   wired between the block's input and output, and each control endpoint is set to its
   minimum, maximum, middle, default, far below and far above its range, 0, -1, 10⁶,
   every allowed state; text endpoints get an empty string, 5000 characters,
   `{}[];,\"' %s%d`, a missing file and non-ASCII text; bangs are banged. Audio is
   pulled after every value and must stay finite. ~6,200 values in all.
3. **Random edits** — 600 seeded edits (tracks, blocks, modules, connections,
   envelopes, scenes, removals, moves, transport), checking the model against the
   server after each. Each undo is followed by redo and undo again; the project's
   structure must round-trip exactly. At the end everything reachable is undone and
   redone, which must give back the final state.
4. **Long playback** — the demo project plays for N seconds of audio (callbacks of
   100 samples, not a multiple of Pd's 64) while the playhead jumps and scenes switch.

## What it found and what was fixed

All in the vendored C code that the original Integra Live also shipped; none were
in the new JUCE code.

| Bug | Effect | Fix |
|---|---|---|
| Pd 0.43 `[makefilename]` takes its format from a `set` message; some modules build it from user text | Typing text with two `%` conversions (e.g. `%s%d`) into a module's text field **crashed the app**; long text overflowed a 1000-byte stack buffer | Format sanitised (only the first conversion kept, other `%` escaped), output bounded with `snprintf` (`x_connective.c`) |
| iemlib `soundfile_info` copies the path into a 400-byte buffer | A long file name **crashed the app** (buffer overflow) | Bounded (`soundfile_info.c`) |
| Pd `q8_rsqrt`, `q8_sqrt`, `rsqrt~`, `sqrt~` read a float through `long` | On 64-bit Linux/macOS `long` is 8 bytes: stack over-read and wrong results (used by `bonk~`) | `memcpy` into `int32_t` (`d_math.c`) |
| Pd soundfile reading/writing of 32-bit float files does the same | Reading wrote 8 bytes into each 4-byte sample — a heap overflow at the end of every buffer; writing over-read the stack | `memcpy` through `uint32_t` (`d_soundfile.c`) |
| flext attribute setters allocated their argument list with `new` and freed it with `delete[]` | Undefined behaviour on every module delete | `new[]` (`flattr.cpp`) |
| libpd C++ `PdContext` read `bInited` before setting it | Uninitialised read at start-up | Flags set before `clear()` (`PdBase.cpp`) |
| Signed overflow in `noise~` and 24/32-bit soundfile decoding | Undefined behaviour (worked by luck) | Unsigned arithmetic |

Windows was unaffected by the `long` bugs (`long` is 4 bytes there), which is why
they never showed up in the original Windows builds.

## Results after the fixes

- Normal build: all 6 ctest suites pass, including the stress test.
- Sanitizer build (ASan + UBSan): self test (76), GUI test (227) and a 60 s stress test
  all pass with **no memory errors and no undefined behaviour reported**. Two
  use-after-free reports were in the tests themselves (a model pointer held across an
  edit that rebuilds the model; the colour wheel read after it deletes itself) and were
  fixed there.
- Leaks: LeakSanitizer reports ~43 MB still allocated at exit, almost all Pd's global
  state (classes, symbols, loaded patches) that Pd never frees. It doesn't grow during a
  session, so it isn't a running leak; it was left alone.

## Not covered

- Real audio and MIDI hardware (the tests drive the audio callback directly).
- macOS and Windows under sanitizers (the stress test itself runs in CI on both).
- Projects other than the demo blocks: run `--stress-test 60 /path/to/project.integra`
  on your own projects to include them.

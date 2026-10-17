# 05 — Status, verification and next steps

## Summary

| Area | State |
|---|---|
| libIntegra (Pd/libpd backend) | Kept. Builds from source with CMake on Linux, macOS and Windows; threads/files ported to standard C++17; new host-driven audio mode |
| libpd + externals | libpd 0.16.1 (Pd 0.56-5), upgraded from the original Pd 0.43.3 with the module patches checked — see [10-pd-upgrade.md](10-pd-upgrade.md); all externals compiled in; every module instantiates |
| Module library, demo blocks | Unchanged, all load |
| Flex GUI → JUCE | Arrange, Module and Live views, 18 widget types, properties (info, routing, scripting, MIDI), transport, settings, menus, undo/redo |
| integra_server (XML-RPC/OSC) | Removed — libIntegra runs in-process |
| File compatibility with Integra Live 1.x | Same node conventions and `userData` XML; Flex-saved demo blocks open with their layouts |
| Audio I/O | JUCE `AudioDeviceManager` (ALSA/JACK/CoreAudio/WASAPI/ASIO) driving libpd |
| MIDI | libIntegra's PortMidi engine via the `MidiSettings` module |
| Build/CI | CMake (JUCE via FetchContent; vcpkg manifest on Windows), GitHub Actions building and testing Linux, macOS and Windows, install rules, CPack, Windows zip artifact |
| Platforms | **Linux, macOS (Apple Silicon) and Windows (x64, MSVC): built and tested in CI** — see [06-windows.md](06-windows.md) |

## How it was verified

On Linux (Ubuntu 24.04, GCC 13, JUCE 8.0.8) locally and in CI; on macOS 14 (Apple
Silicon, Apple clang) and Windows Server 2022 (MSVC 2022, x64) in CI, which runs tests 1–4
there (the GUI test needs a desktop session). A fourth CI job runs every suite under
AddressSanitizer + UndefinedBehaviorSanitizer ([09-robustness.md](09-robustness.md)).
CI is paused to stay within the free Actions minutes: it runs only when started by hand
(Actions → build → Run workflow).

`ctest --test-dir build` — all 6 pass:

1. `libIntegraTests` — the upstream GoogleTest suite (session, commands, load/save, versioning).
2. `integra_smoke` — instantiates all 63 DSP modules in Pd, pings them, loads all 6 demo blocks.
3. `integra_host_audio_test` — signal through libpd in host-driven mode (thru, disconnect, generator).
4. `IntegraLive_self_test` — 76 end-to-end checks of the app core, including audio through the
   JUCE callback path with non-power-of-two buffers, block activation by the playhead, undo/redo
   (including undoing deletes), rename, routing, MIDI control, envelopes, scenes, Lua, block moves,
   save/reopen, and importing all six Flex-saved demo blocks. `IntegraLive_self_test_relocated`
   runs it again with libIntegra's start-up guess of its resources folder pointing nowhere, as in
   the macOS bundle ([11-macos-testing-fixes.md](11-macos-testing-fixes.md)).
5. `IntegraLive_stress_test` — every demo block opened, played, saved, reopened and imported;
   every control of all 66 modules at extreme values and odd text; 600 seeded random edits with
   undo/redo round-trip checks; playback with playhead jumps and scene changes. Fails on any
   NaN/infinity from the engine ([09-robustness.md](09-robustness.md)).
6. `IntegraLive_gui_test` (under `xvfb-run`) — 227 interaction checks with synthesized mouse and
   keyboard events across all three views.

The Pd upgrade was also checked with `--fingerprint-modules` against the Pd 0.43 build: module
output and reported values match ([10-pd-upgrade.md](10-pd-upgrade.md)).

Screenshots in `docs/images/` are produced by the app itself (`--screenshot`).

Not verifiable in the build container: actual sound through hardware (no audio device — the app
falls back to its real-time no-device thread, exactly as libIntegra's PortAudio engine did) and
real MIDI devices.

## Known issues

* `Delay` and `VibratoChorus` modules don't answer pings (bug in their Pd patches, pre-existing,
  harmless; see doc 02).
* Some module patches print Pd console errors with silent input or no files loaded (pre-existing
  module behaviour; see doc 02).
* Pd was upgraded to 0.56-5 (libpd 0.16.1); see [10-pd-upgrade.md](10-pd-upgrade.md). PhaseVocoder
  comes out about 9% louder than on Pd 0.43 (cause not found yet), and QuadSoundfiler and
  Octosoundfiler stayed silent in the fingerprint test on both versions (not investigated).
* Sound file playback (`fsplay~`) did not work in this port until the fix described in doc 10;
  it now does, but has only been tested offline, not on audio hardware.

## Next steps

1. **Packaging**: code signing/notarisation for the macOS bundle; a Windows installer
   (CI currently uploads a zip-able folder). The GUI interaction test runs on Linux only in
   CI (it needs a desktop session on macOS/Windows).
2. Remaining Flex features and bugs: see [08-flex-parity.md](08-flex-parity.md) for the
   full comparison with the Flex GUI.
3. Optionally route MIDI through JUCE as well (a host-driven MIDI engine analogous to the audio
   one), so device handling is uniform.

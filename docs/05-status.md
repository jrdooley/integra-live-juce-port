# 05 — Status, verification and next steps

## Summary

| Area | State |
|---|---|
| libIntegra (Pd/libpd backend) | Kept. Builds from source with CMake on Linux; Linux portability bugs fixed; new host-driven audio mode |
| libpd + externals | Pd 0.43.3 as used by the module library; all 9 externals compiled in; every module instantiates |
| Module library, demo blocks | Unchanged, all load |
| Flex GUI → JUCE | Arrange, Module and Live views, 18 widget types, properties (info, routing, scripting, MIDI), transport, settings, menus, undo/redo |
| integra_server (XML-RPC/OSC) | Removed — libIntegra runs in-process |
| File compatibility with Integra Live 1.x | Same node conventions and `userData` XML; Flex-saved demo blocks open with their layouts |
| Audio I/O | JUCE `AudioDeviceManager` (ALSA/JACK/CoreAudio/WASAPI/ASIO) driving libpd |
| MIDI | libIntegra's PortMidi engine via the `MidiSettings` module |
| Build/CI | CMake (JUCE via FetchContent), GitHub Actions (Linux blocking, macOS experimental), install rules, CPack |
| Platforms | **Linux: built and tested.** macOS: expected to work (CI job included, unverified). Windows: not yet (see below) |

## How it was verified (Linux, Ubuntu 24.04, GCC 13, JUCE 8.0.8)

`ctest --test-dir build` — all 5 pass:

1. `libIntegraTests` — the upstream GoogleTest suite (session, commands, load/save, versioning).
2. `integra_smoke` — instantiates all 63 DSP modules in Pd, pings them, loads all 6 demo blocks.
3. `integra_host_audio_test` — signal through libpd in host-driven mode (thru, disconnect, generator).
4. `IntegraLive_self_test` — 48 end-to-end checks of the app core, including audio through the
   JUCE callback path with non-power-of-two buffers, block activation by the playhead, undo/redo
   (including undoing deletes), rename, routing, MIDI control, envelopes, scenes, Lua, block moves,
   save/reopen, and importing all six Flex-saved demo blocks.
5. `IntegraLive_gui_test` (under `xvfb-run`) — 27 interaction checks with synthesized mouse and
   keyboard events across all three views.

Screenshots in `docs/images/` are produced by the app itself (`--screenshot`).

Not verifiable in the build container: actual sound through hardware (no audio device — the app
falls back to its real-time no-device thread, exactly as libIntegra's PortAudio engine did) and
real MIDI devices.

## Known issues

* `Delay` and `VibratoChorus` modules don't answer pings (bug in their Pd patches, pre-existing,
  harmless; see doc 02).
* Some module patches print Pd console errors with silent input or no files loaded (pre-existing
  module behaviour; see doc 02).
* libIntegra's unit tests take ~90 s because every session start probes PortAudio host APIs.
* The libpd used is the 2013 version bundled with Integra (Pd 0.43). Upgrading to a current libpd is
  possible but would need the module patches re-validated; deliberately not done here.

## Next steps

1. **Windows**: libIntegra uses pthreads throughout (`pthread_mutex_t`, `sem_t`) and `dirent.h`.
   Either build with vcpkg's `pthreads` + a `dirent` shim, or replace the threading primitives with
   `std::mutex`/`std::condition_variable` (cleaner; touches ~10 files). The Pd sources also need
   the MSVC defines already present in `third_party/CMakeLists.txt`.
2. **macOS**: confirm the experimental CI job; add code signing/notarisation for the bundle.
3. Remaining Flex features (see doc 04): module version upgrade/switch dialog, block library
   panel, multi-select in the module graph, envelope curvature gesture, track collapse.
4. Optionally route MIDI through JUCE as well (a host-driven MIDI engine analogous to the audio
   one), so device handling is uniform.

# 01 — Importing the upstream sources

The upstream Birmingham Conservatoire repositories were **copied**, not forked
or modified. This file records exactly what was copied, from where, and why.

| Path in this repo | Source | Notes |
|---|---|---|
| `libIntegra/{api,src,data,tests,doc,LICENSE.txt}` | `integra-framework@623ecf71:libIntegra/` | Newest libIntegra (1.8.2). GPL-2.0-or-later. |
| `libIntegra/VERSION` | `integra-framework@623ecf71:VERSION` | |
| `third_party/libpd/` | `integra-framework@623ecf71:libIntegra/externals/libpd/` | Only `pure-data/src`, the four `extra` externals Integra registers (`bonk~`, `expr~`, `fiddle~`, `lrshift~`), `libpd_wrapper`, `cpp`. Pd 0.43.3 — the version the module patches were written for. BSD-3-Clause. |
| `third_party/pd-externals/` | `integra-framework@623ecf71:libIntegra/externals/extra/` | `partconv~` (bsaylor), `copy`, `freeverb~`, `fsplay~`, `soundfile_info` (iemlib). |
| `third_party/flext/` | `jamiebullock/flext@cbc44012` (git submodule of integra-framework) | `source/` + licence files only; needed by `fsplay~`. GPL-2.0-or-later. |
| `third_party/minizip/`, `third_party/tmpfileplus/` | `integra-framework@623ecf71:libIntegra/externals/` | Zip I/O for `.integra`/`.module` files; portable temp files. |
| `third_party/boost/` | `integra-framework@623ecf71:libIntegra/externals/boost/` | Header subset used by `fsplay~`. |
| `modules/` | `integra-framework@623ecf71:modules/` | 82 `.module` files; identical to IntegraLive `develop` except the fixed `metronome.module`. Unlicense. |
| `blocks/` | `IntegraLive@91d85f41:blocks/` | Demo block library (`index.xml` + 6 `.integra` files). |
| `reference/integra2-juce-prototype/` | `IntegraLive@b8cef433 (integra2):JUCE-libIntegra-Client-1/` | The 2018 JUCE prototype, kept for provenance only — **not built**. |

## Deliberately not imported

* `libMarkdownParser` from the `integra2` branch — it wraps the `sundown`
  C library, which was never committed upstream, so it cannot be built. The
  JUCE app renders the (simple) Markdown used in Integra *info* texts itself.

* `externals/portaudio`, `externals/portmidi` (≈70 MB of vendored, partly
  generated sources) — the CMake build uses the system packages or fetches
  them with `FetchContent`.
* `externals/macosx`, `externals/win32` — prebuilt binaries (static libs,
  DLLs) for libsndfile, FFTW, Lua, libxml2, pthreads, etc. Replaced by system
  packages / package managers in the new build.
* The Flex GUI (`GUI/`), `integra_server` (`server/`), the old Xcode/MSVC
  projects, the SDK and the module-creator tooling. The GUI is reimplemented
  in JUCE (see later docs); the server is unnecessary with an in-process
  library. The original repositories remain the reference for those.

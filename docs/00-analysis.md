# 00 — Analysis of the upstream repositories

This document records what was found when analysing the original Integra Live
code bases before starting the port. The upstream repositories were cloned
**read-only** into a scratch area; nothing was ever pushed to them. All work
happens in this repository (`jrdooley/integra-live-juce-port`).

## 1. Repositories examined

| Repository / ref | Commit | What it contains |
|---|---|---|
| `BirminghamConservatoire/IntegraLive` `develop` | `91d85f41` | Integra Live 1.7.11: Flex/AIR GUI (ActionScript + MXML), `integra_server` (XML-RPC + OSC front end to libIntegra), libIntegra, module library, demo blocks, documentation |
| `BirminghamConservatoire/IntegraLive` `integra2` | `b8cef433` | The 2018 "Integra 2" JUCE work: `JUCE-libIntegra-Client-1` (JUCE test app linking libIntegra in-process) and `libMarkdownParser` |
| `BirminghamConservatoire/integra-framework` `develop` | `623ecf71` | libIntegra split out as a standalone framework (2018–2020): newest libIntegra source, GoogleTest unit tests, fixed Metronome module, resource-directory helpers |
| `BirminghamConservatoire/IntegraLive2` | — | **Not publicly accessible** (GitHub returns 404 both anonymously and through the session's credentials). The JUCE port effort it refers to is the `integra2` branch above plus `integra-framework`, which were produced by the same people (getdunne, J-Bledsoe, Jamie Bullock) in the same period. |

## 2. Architecture of the original Integra Live (1.x)

```
 ┌───────────────────────────┐   XML-RPC (commands, queries)   ┌──────────────────────────┐
 │ Flex / Adobe AIR GUI       │ ───────────────────────────────▶│ integra_server            │
 │ ~76k lines AS3/MXML        │ ◀─────────────────────────────── │  xmlrpc_server.cpp        │
 │ (IntegraLive.mxml)         │   OSC (value-change feedback)    │  osc_client.cpp           │
 └───────────────────────────┘                                  │  links libIntegra         │
                                                                └────────────┬─────────────┘
                                                                             │ C++ API (integra_api)
                                                                ┌────────────▼─────────────┐
                                                                │ libIntegra                │
                                                                │  node tree, commands,     │
                                                                │  logic (scenes, player,   │
                                                                │  envelopes, scalers, MIDI │
                                                                │  learn, Lua scripting),   │
                                                                │  file I/O (.integra zip)  │
                                                                │  DSP engine ─▶ libpd      │
                                                                │  PortAudio / PortMidi     │
                                                                └──────────────────────────┘
```

* **Flex GUI** – depends on Adobe Flex SDK 4.x and Adobe AIR; both are
  obsolete (Flash end-of-life 2020). It cannot be built or run on a modern OS
  without legacy tooling, which is the primary reason for the port.
* **integra_server** – a thin process wrapping libIntegra behind XML-RPC
  (xmlrpc-c) with OSC notifications back to the GUI. With an in-process JUCE
  GUI this layer is unnecessary.
* **libIntegra** – the heart of the system and is retained unchanged in
  spirit. Key concepts:
  * *Interface definitions* (`.iid` XML inside `.module` zip files) describe
    each module's endpoints (control/stream), constraints, scales, state
    labels and **widgets** (GUI layout hints).
  * *Nodes* form a tree (`Project.Track.Block.Module.endpoint`). Commands
    (`INewCommand`, `ISetCommand`, `IDeleteCommand`, `IRenameCommand`,
    `IMoveCommand`, `ILoadCommand`, `ISaveCommand`) are processed under a
    `CServerLock`.
  * *Logic classes* implement non-DSP modules in C++: Container, Connection,
    Player, Scene, Envelope, ControlPoint, Scaler, Script (Lua 5.2),
    MidiControlInput, MidiRawInput, AudioSettings, MidiSettings.
  * *DSP engine* hosts libpd (Pd 0.43.3). Each module with an implementation
    is instantiated as a Pd abstraction in a host patch; values are sent with
    `integra-broadcast-receive` and fed back via the `integra` receiver.
  * Audio I/O is PortAudio (with a "no device" timer thread fallback), MIDI is
    PortMidi.
  * Notifications of endpoint changes go to an `INotificationSink`; a
    `CPollingNotificationSink` is provided for GUI polling.
* **Modules** – 82 `.module` files (zip: `integra_module_data/interface_definition.iid`
  + `implementation/*.pd` + license). Pd externals used by the module patches:
  `bonk~`, `fiddle~`, `expr`/`expr~`/`fexpr~`, `lrshift~` (Pd extra),
  `partconv~` (bsaylor, needs FFTW3), `freeverb~`, `soundfile_info` (iemlib),
  `fsplay~` (flext + libsndfile), `copy`. libIntegra registers these
  statically in `CDspEngine::register_externals()`.

### 2.1 Project structure conventions used by the GUI

The GUI (not libIntegra) imposes a fixed hierarchy on the node tree
(`ModelLoader.as`):

| Level | Allowed node classes |
|---|---|
| 1 | one `Container` (the **project**), one `AudioSettings`, one `MidiSettings` |
| 2 (in project) | `Container` (**tracks**), `Player` (exactly one), `MidiRawInput` (MIDI monitor), `Script`, `Connection`, `Scaler`, `MidiControlInput` |
| 3 (in track) | `Container` (**blocks**), `Envelope` (**block envelopes**), `Script`, `Connection`, `Scaler`, `MidiControlInput`; (in Player) `Scene` |
| 4 (in block) | audio/processing **modules**, `Connection`, `Scaler`, `MidiControlInput`, `Envelope`, `Script`; (in block envelope) `ControlPoint` |
| 5 | `ControlPoint` inside block-level `Envelope`s |

* A block's position on the timeline is not stored on the block. Instead each
  block has a sibling *block envelope* in its track with `startTick` = block
  start and four control points `(-1,0) (0,1) (len-1,1) (len,0)`, driven by a
  project-level connection `Player.tick → BlockEnvelope.currentTick` and a
  track-level connection `BlockEnvelope.currentValue → Block.active`.
* "Routing" between controls uses a `Scaler` between two `Connection`s
  (`source → Scaler.inValue`, `Scaler.outValue → target`).
* GUI state is persisted as XML strings in each container's `userData`
  endpoint: module positions and live-view control layout (blocks), colour /
  height / expanded state (tracks), view mode / timeline zoom / colour scheme
  (project), key bindings (scenes). Persisting the same format keeps `.integra`
  files compatible between the Flex GUI and the JUCE port.

## 3. State of the 2018 JUCE attempt (`integra2` branch)

`JUCE-libIntegra-Client-1` (≈1,250 lines of app code + 18 widget classes):

* Links libIntegra **in-process** (no XML-RPC server) – the right direction,
  and adopted here.
* Starts a session with hard-coded macOS paths (`/Users/shane/...`), creates
  an `AudioSettings` node, can load/save a single `.integra` file, dump
  modules/nodes, and build a small node graph from code.
* `WidgetPanel` instantiates widgets from a module's widget definitions, but
  the value plumbing is broken: `onValueChange` lambdas capture stack locals
  by reference, values are never read back into widgets correctly, and most
  widget `setValue`s are stubs (the last commit is literally "Initial setValue
  implementation for widgets").
* No project / track / block / scene model, no arrange or live view, no module
  graph editing, no routing, no envelopes, no MIDI learn, no preferences, no
  undo, no build system other than a Projucer `.jucer` file with relative
  paths to a sibling JUCE checkout and an Xcode project (macOS only).

In short: a proof-of-concept that libIntegra can be driven in-process from
JUCE, roughly 2–3 % of the way to a replacement GUI.

## 4. State of `integra-framework`

* The most recent libIntegra (version 1.8.2): cleaned-up includes, a
  resources-directory helper, `tmpfileplus` for portable temp files,
  GoogleTest unit tests (`libIntegra/tests`), Windows (MSVC) and macOS (Xcode)
  build systems. **No Linux build and no CMake.**
* macOS build links prebuilt static libraries checked into the repo
  (`externals/macosx/*.a`), Windows uses prebuilt DLLs – neither is
  reproducible on other platforms.
* Porting notes in its README flag libpd, flext, libxml2 and pthreads as the
  awkward dependencies.

## 5. Decisions taken for this port

1. **Keep libIntegra and the Pd/libpd backend.** Base: `integra-framework`
   libIntegra 1.8.2, its bundled libpd (Pd 0.43.3 — the version all module
   patches were authored against) and the same set of statically-registered
   externals.
2. **Replace the Flex GUI and integra_server with a single JUCE application**
   that links libIntegra in-process (as the 2018 prototype did) and talks to it
   through the public `integra_api`.
3. **New CMake build** for libpd, the externals, libIntegra, its tests and the
   JUCE app, replacing Xcode/MSVC projects and prebuilt binaries. JUCE is
   fetched with CMake `FetchContent`.
4. **File compatibility**: keep the GUI hierarchy conventions and `userData`
   XML formats from §2.1 so existing `.integra` projects and blocks open in
   the new app and vice versa.
5. The 2018 prototype source is kept under `reference/` for provenance; the
   new app is written fresh, reusing its ideas (in-process session, widget
   builder keyed on the widget type names) but not its broken plumbing.

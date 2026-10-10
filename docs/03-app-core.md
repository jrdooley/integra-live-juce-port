# 03 — The JUCE application core

The Flex GUI talked to a separate `integra_server` process over XML-RPC (commands
and queries) and OSC (value feedback), and kept a complete client-side mirror of
the node tree. The JUCE port links libIntegra **in-process** (as the 2018
prototype did) and replaces all of that with a small core layer under
`app/Source/Core/`. Everything in it is exercised headlessly by
`IntegraLive --self-test` (registered with CTest as `IntegraLive_self_test`).

## libIntegra change: host-driven audio

`CServerStartupInfo::host_driven_audio` (new) makes libIntegra create a
`CHostAudioEngine` instead of the PortAudio engine. It opens no devices; the
embedding application calls `CIntegraSession::process_audio()` (new) from its own
audio callback, which runs one 64-frame interleaved block through
`CDspEngine::process_buffer()` — exactly what the PortAudio callback used to do.
The `AudioSettings` module still exists and simply reports driver/device "Host" and
the sample rate/channel counts the host is processing with. PortAudio mode is
unchanged and still used by the libIntegra tests.

`integra_host_audio_test` verifies this mode: sine → `AudioIn` → `StereoAudioOut`
arrives at the output (RMS 0.283 = 0.354 × the module's default 0.8 out level),
deleting the connection silences it, and `TestSource` generates signal.

## Core classes

| Class | Replaces (Flex GUI / server) | Responsibility |
|---|---|---|
| `IntegraSession` | `integra_server` + `IntegraConnection.as` | Starts libIntegra (`host_driven_audio`), locates modules/blocks/resources next to the binary or in the bundle, thin wrappers for every command, unique child names (`getNewChildName`, same scheme as `IntegraContainer.as`) |
| `NotificationHub` | OSC feedback (`OSCServer.as`) | `INotificationSink` that coalesces set notifications from any libIntegra thread and re-delivers them on the JUCE message thread; also broadcasts "structure changed" after app-initiated new/delete/rename/move/load |
| `ModuleCatalogue` | `query.interfacelist/interfaceinfo/endpoints/widgets` | Message-thread copy of interface definitions: names, tags, core flag, audio pin counts |
| `AudioHost` | PortAudio engine + AudioSettings dialog | `juce::AudioIODeviceCallback` re-blocking JUCE buffers of any size into libpd's 64-frame blocks; runs a real-time "no device" thread when no device is open so Pd keeps processing control messages (as libIntegra's PortAudio engine did) |
| `UserData` | `model/userData/*.as` | Read/write the `userData` XML of projects, tracks, blocks and scenes in the **exact Flex format** (module positions, live-view controls, track colours/heights, view mode, timeline zoom/scroll, scene key bindings); unknown elements round-trip unchanged |
| `ProjectModel` | `IntegraModel.as` + `ModelLoader.as` | Snapshot of the node tree interpreted with the GUI hierarchy conventions: tracks ordered by `zIndex`, blocks with timeline extent resolved from their block envelopes, envelopes resolved to their targets and player connections, scalers resolved to source/target, MIDI inputs to their outputs. Rebuilt when structure or a structural endpoint changes (not for `Player.tick`, module parameters, VU meters...) |
| `IntegraActions` | per-command `generateInverse()` | Five undoable primitives: create, delete, set value, rename, move |
| `ProjectController` | `controller/serverCommands/*.as` (~70 classes) | High-level operations, each one `juce::UndoManager` transaction composed from the primitives |

### Undo design

The Flex GUI hand-wrote an inverse for each of its ~70 commands. Here every
operation is composed from five primitives that know their own inverse:

* `CreateNodeAction` ↔ delete
* `DeleteNodeAction` saves the node's subtree with `ISaveCommand` to a temporary
  `<NodeName>.integra` just before deleting; undo reloads it with
  `ILoadCommand` — restoring children, all values and embedded modules. (The file
  must be named after the node because libIntegra renames a loaded top-level
  node after its file name — found by the self-test.)
* `SetValueAction` ↔ previous value; consecutive sets of one endpoint within a
  second (slider drags) coalesce into one undo step
* `RenameNodeAction`, `MoveNodeAction` ↔ rename/move back (libIntegra itself
  re-targets connection paths on rename and move)

Deleting anything also deletes what the GUI conventions tie to it: connections
referring into it, scalers whose routing it cuts, envelopes targeting it, a
block's block envelope, and its entries in the block's `userData` — all in the
same transaction, so one undo restores everything.

### Operations ported

New/open/save project (with Flex defaults: `Untitled`, `Player1` at 40 ticks/s,
`MidiMonitor`, one track with a 20-second block containing `AudioIn` and
`StereoAudioOut` at the Flex default grid positions); add/remove/reorder/recolour
tracks; add/remove/reposition blocks (block envelope + `Player.tick →
currentTick` + `currentValue → active` connections, the same node pattern as
`AddBlock.as`/`RepositionBlock.as`, including its end-point ordering trick);
move blocks between tracks; import/export blocks; add/remove/position modules;
audio connections; routing via scalers with ranges initialised from endpoint
constraints; MIDI control inputs (with learn); envelopes and control points;
scenes with key bindings (first unused key, as `AddScene.as`); transport (play,
stop, home, position, next/previous scene); Lua scripts; live-view control
visibility.

## Self-test results

`IntegraLive --self-test` runs 55 checks against the real libIntegra + libpd,
all passing on Linux, including:

* new project matches the Flex defaults; first track gets the first Flex palette colour
* audio through the **JUCE callback path with 100-sample buffers** (not a
  multiple of 64) → libpd → out: RMS 0.283
* block `active` follows the playhead via the block envelope, also after moving
  the block to another track
* undo/redo of add module, set value; undo of delete restores module, its
  connections and values; rename re-targets connections and moves the
  userData position
* routing through a scaler, MIDI control input, scene + key binding, envelope
  interpolation while the playhead moves, Lua script (`Echo.feedback = 0.5`)
* save → reopen round trip (project is renamed after the file, as in the
  original application)
* importing all six Flex-saved demo blocks from `blocks/`

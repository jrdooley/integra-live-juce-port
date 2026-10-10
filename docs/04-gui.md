# 04 — The JUCE user interface

The Flex GUI (~76k lines of ActionScript/MXML) is replaced by a JUCE interface
under `app/Source/UI/` built on the core described in
[03-app-core.md](03-app-core.md). It keeps the original's three views and
workflow, reads and writes the same project files, and is exercised by an
automated interaction test.

## Views

### Arrange view — `ArrangeView.{h,cpp}` (Flex: `ArrangeView/`, `Timeline/`)

![Arrange view](images/arrange-view.png)

* **Timeline ruler** with time grid that adapts to zoom, **scene bar**,
  playhead (click/drag the ruler to locate; this deselects the scene, as in
  Flex), loop range.
* **Scenes** behave as in Flex (`Timeline.as`, `SceneBar.as`): drag in empty
  space to create one; each scene has a 10 px resize handle inside either edge
  (resize cursor) and moves when dragged in the middle (hand cursor). Edges snap
  to block starts/ends within 10 px, with snap lines drawn across the lanes;
  neighbouring scenes are pushed aside so scenes never overlap
  (`doSceneRescheduling`); the timeline auto-scrolls when dragging at its
  edges; the dragged scene is drawn in place while dragging. A drag, including
  the scenes it pushed, is one undo step. Right-click for mode
  (hold/play/loop), rename, key binding, delete.
* **Tracks**: coloured headers (right-click: rename, colour from the Flex
  palette, height, move up/down, import block, delete); "+ Add track".
* **Blocks** positioned by their block envelopes: double-click an empty lane to
  add a block (up to the next block), drag to move (also vertically to another
  track), drag either edge (10 px handles) to resize, double-click to open in
  the Module View, right-click for rename / export / delete / *Edit envelope*.
  Blocks don't overlap within a track: as in Flex (`doBlockRescheduling`), a
  dragged or resized block pushes its neighbours along, keeping their order;
  edges snap to blocks on other tracks and to scenes within 10 px; the whole
  drag is one undo step. Delete/Backspace removes the selected block (tracks
  are deleted from their context menu only).
* **Envelopes** inside blocks: all of a block's envelopes are drawn; choose one
  with *Edit envelope* to edit it — double-click to add a control point, drag
  to move (a point stays between its neighbours and the first one stays at
  tick 0, as in `EnvelopeView.as`; the move is committed on release),
  right-click to delete (not the first point). **Curve mode** (the curve button
  at the bottom right of the selected block, or *Curve mode* in its menu; stored
  in the block's `curvatureMode` userData as in Flex): drag a segment to bend it,
  double-click it to make it straight again. Curvature is drawn exactly as
  libIntegra interpolates it, `t^(2^-curvature)` (`EnvelopeView.drawCurvedTransition`).
* Zoom with Ctrl+wheel or the +/- buttons, scroll with the scrollbar or
  shift+wheel; zoom/scroll persist in the project (`timelineZoom`,
  `timelineScroll` userData, same units as Flex).
* **Properties panel** (below, resizable) for the innermost selection
  (block → track → project), with tabs:
  * *Info* – Markdown rendering of the `info` endpoint with an editor
  * *Routing* – scaled connections (`Connection → Scaler → Connection`):
    source / target endpoint pickers, input & output ranges and scale types
  * *Scripting* – Lua script list, editor with Lua syntax highlighting, Run
  * *MIDI* – MIDI control inputs: device, channel, CC/note, number, learn,
    target

### Module View — `ModuleView.{h,cpp}`, `ModuleProperties.{h,cpp}` (Flex: `ModuleGraph/`, `ModuleLibrary/`, `ModuleProperties/`)

![Module view](images/module-view.png)

* **Module library**: search box, tag filter, pin counts; drag a module onto
  the graph or double-click it to add it.
* **Module graph**: modules at their stored grid positions (Flex grid units,
  one unit = one text row = 18 px), audio pins on each side; drag a module to
  move it (half-unit snapping), drag from an output pin to an input pin (or
  back) to connect (links from a module to itself or that would create a
  feedback loop are refused, as `IntegraModel.canSetAudioLink`), click a
  connection to select it, Delete removes the
  selection, right-click a module to rename / export / delete. The selected
  module is saved as the block's `primarySelectedChild`, as in Flex.
* **Module properties**: the module's widgets from its interface definition,
  at their design positions. Each widget has three toggles:
  **LIVE** (show in Live view), **MIDI** (create a MIDI control input in learn
  mode, routed through a scaler), **ENV** (add/remove a block envelope for that
  control).

Dark colour scheme (per project, as in Flex; Settings → Dark colour scheme):

![Module view, dark](images/module-view-dark.png)

### Live view — `LiveView.{h,cpp}` (Flex: `LiveView/`)

![Live view](images/live-view.png)

Each track shows the live-view controls of the block currently under the
playhead (nothing when no block is, as in Flex). Controls appear where they were placed —
the demo block *LiveViewControls* above was laid out in the Flex GUI and
opens unchanged. Drag a control's title strip to move it, its corner to
resize it; the layout is stored in the block's `userData` relative to the
block area, as Flex stored it. Scene buttons across the top select scenes,
and each scene's key binding (0-9 or A-Z, stored upper-case as in Flex) works
anywhere in the window while the Live view is shown, except in text fields.

### Module Manager — `ModuleManager.{h,cpp}` (Flex: `ModuleManager/`, `UpgradeDialog.as`)

View › Module Manager... (Ctrl/Cmd+4), as in Flex, with two tabs:

![Module Manager, Manage Versions](images/module-manager-versions.png)

* **Manage Versions**: the modules in the project that have other versions
  loaded (shipped, 3rd party or embedded in the project), their alternative
  versions (the *Latest Official* one marked), and a report of what switching
  would change: endpoints added, removed or changed, and the affected modules.
  **Switch Versions** replaces every instance of the selected version, as
  Flex's `SwitchModuleVersion` did: the module is recreated under the same name,
  its values are carried over and fitted to the new version's ranges and allowed
  values, connections, routings and envelopes to endpoints that no longer exist
  are removed (the others stay), and live view controls whose widget is gone are
  removed. **Upgrade All** switches every module to its latest official version
  (shipped before 3rd party before embedded, newest first), saving a backup of
  the project first. Both are one undo step. **Always Upgrade** is stored in the
  application settings.

![Module Manager, Install / Uninstall](images/module-manager-install.png)

* **Install / Uninstall**: installed 3rd party modules (Uninstall: modules in use
  are either deleted or kept embedded in the project, as Flex asked), modules
  embedded in the project (Install To Disk), Install From File..., and an info
  panel for the selected module.

After a project is opened or a module is installed, if any module in the
project isn't its latest official version, the **Upgrade Modules** dialog lists
them and offers *Don't Upgrade*, *Module Manager...*, *Upgrade* and *Always
Upgrade* (with Always Upgrade set, they're upgraded straight away). Upgrades and
installs end with a report window.

## Widgets — `Widgets/IntegraWidgets.{h,cpp}` (Flex: `controls/`, `controlSDK/`)

All 18 widget types found in the module library are implemented and bound to
endpoints through their IID attribute mappings:

| Widget | Attributes | Notes |
|---|---|---|
| Slider, Knob, NumberBox | value | linear / exponential / decibel scaling via the endpoint's value scale (same maths as `ControlScaler.as`); double-click resets to default |
| Checkbox | value | toggles between range minimum and maximum |
| Trigger, StopButton | value (bang) | |
| PlayButton, RecButton | value | toggle |
| RadioGroup | value | from allowed states / state labels / small integer ranges |
| RangeSlider | min, max | two-thumb slider |
| VuMeter | level | follows Pd feedback |
| SoundFileLoadDialog, FileLoadDialog, FileSaveDialog, LocateFolderDialog | value | native file / folder chooser |
| XYPanPad, XYScratchPad | x, y | 2-D pad |
| DryWetBalance, EarlyLateBalance, PanoramaBalance | value | labelled crossfader |

Every widget follows its endpoints through `EndpointWatcher` (a per-path
registry on top of the notification hub), so values changed by envelopes,
scripts, MIDI, routing or Pd feedback update the controls; user edits go
through `ProjectController` and are undoable (drags coalesce into one step).

A control whose endpoint is driven by an envelope, MIDI (once learned) or a
routing is locked, as in Flex's `ControlContainer`: it shows a padlock and the
user can't change it (for a multi-endpoint widget, only the driven attribute is
locked). Clicking the padlock offers *Delete Envelope* / *Delete Routing from
…* for each driver (`model::Project::driversOf`).

## Application shell — `MainComponent`, `RibbonBar`, `PreferencesPanel`

* **Ribbon**: Arrange/Live switch, return-to-start, play/pause, previous/next
  scene, time and current scene display, input/output level meters, MIDI
  activity light, Settings.
* **Menus & keys**: File (New, Open, Save, Save As, Import/Export Block,
  Install Third-Party Module), Edit (Undo/Redo with descriptions, Delete, Add
  Track, Add Scene), Transport (Space play/pause, Home, Ctrl+←/→ scenes), View
  (Ctrl+1/2/3 for Arrange/Live/Module, Settings), Help (Open Demo Project,
  About). Unsaved-changes prompts on New/Open/Quit.
* **Settings**: JUCE's audio device selector (ALSA/JACK on Linux, CoreAudio,
  WASAPI/ASIO on Windows) — the device the libpd engine runs on; MIDI
  input/output devices through libIntegra's `MidiSettings` module (PortMidi);
  colour scheme.

## Command-line options

| Option | Purpose |
|---|---|
| `<file.integra>` | open a project |
| `--demo` | start with the demo project (every block from `blocks/` on its own track) |
| `--self-test` | headless core test (doc 03) |
| `--gui-test` | GUI interaction test (needs a display; CTest runs it under `xvfb-run`) |
| `--screenshot <png> [--view arrange\|live\|module] [--select <path>] [--scheme light\|dark]` | render the window to a PNG and quit (used for these docs) |
| `--quiet` | silence libIntegra's error trace |

## GUI interaction test

`IntegraLive --gui-test` drives the real components with synthesized mouse and
keyboard events and checks the resulting project state (55 checks, all
passing), including: double-click to add a block, drag to move, edge-drag to
resize, overlap prevention, add track, drag a block to another track, drag in
the scene bar to create a scene, click the ruler to move the playhead,
Edit › Undo/Redo, extending and shortening a scene from either edge, a
scene pushing its neighbour along (undone in one step), a scene edge snapping
to a block edge, double-click to open the Module View, drop a module from
the library at a grid position, patch output→input pins, drag a module and
persist its position, the module's 7 widgets appear, moving a knob sets the
endpoint and the knob follows external changes, LIVE and ENV toggles, the
Live view shows the control of the playing block, scene key binding,
Delete key removes a module together with its envelope and live control, and
one undo restores all of it.

Bugs it found and that are fixed:

* `ProjectController` methods took `const String&` paths that often referred
  into the model snapshot, which the same method then rebuilt — a
  use-after-free. Controller methods now take paths by value.
* Live-view key bindings ignored key events that carry only a key code.
* (test harness) JUCE's Linux run loop can lose a quit message posted from
  inside a pumped callback; the test quits from a fresh timer dispatch.

## Not (yet) ported from the Flex GUI

A full feature-by-feature comparison with the Flex GUI is in
[08-flex-parity.md](08-flex-parity.md).

* Module Manager dialog for **upgrading/switching module versions** in a
  project (installing third-party modules *is* supported).
* Block library side panel (blocks are imported through menus instead),
  multi-selection / area selection in the module graph, track collapsing.
* Envelope curvature editing (curvature is honoured on playback and stored,
  but has no editing gesture yet) and envelope lock.
* Hover-driven *Info* view for modules/endpoints (tooltips show the
  descriptions instead), crash reporter, update checker.

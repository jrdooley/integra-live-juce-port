# 08 — Feature parity with the Flex GUI

The original Integra Live 1.7 GUI (Flex/AIR, `BirminghamConservatoire/IntegraLive`
`develop`, `GUI/src`) is the reference for what the JUCE port should do. This document
records a feature-by-feature comparison of the two, done by reading the Flex
ActionScript/MXML (including the in-app help texts in `assets/viewInfo/`) against
`app/Source/`. Earlier docs listed only the larger missing pieces; this audit found
many smaller gestures, constraints and some bugs as well.

Status: **fixed** = done since the audit (each fix has a self-test or GUI test check
unless noted); **missing** = not implemented; **partial** = implemented differently or
incompletely. All bugs the audit found are fixed; what remains are missing features.

Flex paths are relative to `GUI/src/components/`.

## Fixed

| Feature | Flex | Change |
|---|---|---|
| Scene duration editing | `views/Timeline/SceneBar.as`, `Timeline.as:857-1140` | Before: only an invisible 6 px strip at the right edge resized a scene, with no cursor change, no start-edge resize, a preview that showed no change when shortening, and quarter-second quantising. Now as Flex: 10 px handles at both edges with cursors, live drawing, snapping to block edges with snap lines, neighbouring scenes pushed aside (no overlaps, also when creating), auto-scroll at the edges, one undo step per drag, clicking the ruler or empty scene bar deselects the scene. Covered by 7 new GUI test checks. |
| Live view control positions | `LiveViewBlock.as` | Moving or resizing a control saved its position including the 28 px track header, so it drifted down each time and the wrong position was saved. Positions are now stored relative to the block area and clamped to it. |
| Scene key bindings | `SceneUserData.as`, `KeybindingView.as` | Keys are restricted to 0-9 / A-Z and stored upper-case (lower-case keys from earlier port versions are read as upper-case); new scenes get the first unused key from Flex's list; keys work anywhere in the window while the Live view is shown, except in text fields. |
| Live view with no block under the playhead | `LiveViewTrack.as` | The track now shows nothing, as in Flex, instead of the selected block. *(No automated test.)* |
| Delete key | `ArrangeView.as` | Delete/Backspace with only a track selected no longer deletes the track. |
| Block drag onto another block | `ArrangeView.as` `onDragBlock`, `doBlockRescheduling`, `getSnap` | Dropping or resizing a block into another pushes the neighbours along instead of discarding the drop; edges snap to blocks on other tracks and to scenes within 10 px (snap lines shown) instead of a quarter-second grid; 10 px resize handles; minimum length 10 ticks; one undo step. |
| Envelope control points | `EnvelopeView.as` | A dragged point stays between its neighbours; the first point stays at tick 0 and can't be deleted; the move is one command on release. |
| Feedback loops | `IntegraModel.canSetAudioLink` | Audio links that would create a cycle, or connect a module to itself, are refused. |
| Locked (driven) controls | `ControlContainer.as` padlock | Controls driven by an envelope, learned MIDI or a routing show a padlock and can't be changed; the padlock's menu deletes the envelope or routing. |
| File and folder widgets | `FileLoadDialogControl.as`, `LocateFolderDialogControl.as` | FileLoadDialog and LocateFolderDialog are real file/folder choosers instead of a slider; choosers start at the current file. *(No automated test.)* |
| Module library tags | `ModuleLibrary.as` | The tag list is rebuilt when modules are installed. |
| Module library | `ModuleLibrary/`, `utils/libraries/Library.mxml` | Versions grouped by origin behind an expander with source headings; embedded and in-development modules listed; only modules with audio endpoints; Flex's sort order and tints; tag cloud sorted by use with AND filtering; search on labels; search and tags clear each other; clear buttons; Enter, Left/Right; Flex's info text in tooltips. On the right of the graph, as in Flex. |
| Block Library | `BlockLibrary/`, `ArrangeViewTrack.as` drag handlers | Side panel with the shipped blocks (index.xml order) and user blocks; drag onto a track with a drop outline; double-click/Enter imports into the selected track at the playhead; remove user blocks; info tooltips; resizable width. Unlike Flex, an imported block never overlaps others: it starts after a block under its start and pushes later blocks along. |
| Pin drag | — | The connection being dragged no longer points into the graph's module list, which is rebuilt on model changes. *(No automated test.)* |
| Envelope curvature | `EnvelopeView.as`, `BlockView.as` | Curve mode button and menu item (stored per block, as Flex did), drag a segment to bend it, double-click to straighten it; curves drawn as libIntegra interpolates them. |
| Module Manager | `ModuleManager/`, `SwitchModuleVersion.as`, `UpgradeModules.as`, `UninstallModules.as` | Install / Uninstall and Manage Versions tabs, Switch Versions, Upgrade All with a backup, Always Upgrade, the Upgrade Modules dialog after loading a project or installing modules, and installation / switch / upgrade reports. Version reports list endpoint differences more briefly than Flex's. |
| Active buttons and VU meters | `ViewHolder.as`, `ArrangeViewTrack.as`, `LiveViewTrack.as`, `ModuleGraph.as`, `AggregateVUContainer.as` | Project, track and block active buttons (Arrange view corner and track headers, Live view top bar and track headers, Module View title bar) and meters showing the latest VU of the container's audio outputs. Inactive containers are greyed. |
| Scene properties | `ScenePropertiesTitlebar.as`, `ProjectPropertiesTitlebar.as`, `SelectScene.as` | Clicking a scene shows it in the properties panel with State, Key, MIDI learn (routed to `activate`) and Info; the project's title bar has Prev / Next Scene MIDI learn. |
| Scene Shortcuts | `KeybindingView.as`, `KeyIcon.as` | On-screen keyboard under the Live view: scene names on keys, current scene highlighted, click to select, drag to a free key to rebind, collapsible and resizable. |
| Live view editing | `LiveViewBlock.as` | Right-click menu to add a module's control at the click point or remove it; resize from every edge; snapping to the other controls' edges with snap lines; clamped to the block area; no rebuilds during a drag, and only when what is shown changes. |
| MIDI indicator | `MidiInputIndicator.as` | Lights for every incoming message, including repeats of the same message. *(No automated test.)* |

## Arrange view and timeline

**Missing / partial**
* Scenes: inline rename by double-click (and in the properties title); Backspace to delete.
* Timeline: the drag-to-zoom/scroll strip at the top of the ruler (drag sideways to
  scroll, up/down to zoom, Shift locks an axis).
* Tracks: collapse button, drag the bottom edge to
  resize (only 4 preset heights), drag headers to reorder (only move up/down), full colour
  picker (only 7 colours), import/export track, double-click below the tracks to add one,
  Ctrl+Shift+T.
* Blocks: multi-select (Ctrl/Cmd-click) with group move/delete; moving only by the title strip; a new block
  centred on a double-click that pushes later blocks; Ctrl+B / Ctrl+Shift+B; inline rename.
* Envelopes: envelope lock / Shift, click the line to select and add a point
  (now: pick from a menu, then double-click), double-click a point to delete it (now
  right-click), value/time readout while dragging, add/remove envelopes from the block
  title bar.
* Properties panel: clickable breadcrumbs, Ctrl+E to run a script.

## Module view

**Missing / partial**
* Library: a ghost preview while dragging, a resizable tag cloud (it fits its tags,
  up to half the library), **dropping a module onto a link to splice it in**.
* Graph: multi-select and rubber-band selection with group move/delete; snapping to
  other modules' edges with snap lines; in-place rename by double-click; per-module
  "Live View" checkbox; right-click on empty space for Import Module; Change Track Colour;
  track-colour tint and greyed-out inactive blocks; hover info for pins, links and modules.
* Properties: editable title (rename), **typing a value** (double-click the value label;
  double-click currently resets instead), right-click **Revert to default**
  (this control / all controls), MIDI learn for any endpoint of a multi-endpoint widget
  (XY pad y, range slider max).
* Widgets: NumberBox text entry; slider quantising to allowed values; RangeSlider drag
  middle to move both ends, draw a new range, double-click to invert, vertical orientation.

## Live view

**Missing / partial**
* Per-track collapse.
* Read-only timeline above the Live view.
* Snapping while resizing diagonally uses each edge's own snap (Flex's
  `doDiagonalReposition` keeps the aspect of the drag); the maximum control sizes of
  `ControlMeasurer` aren't applied.

## Application

* Ribbon: clickable MIDI indicator with CC/note readout; lighting (light/dark) button.
* **Info view**: hover-driven Markdown help for whatever is under the mouse
  (Ctrl+3, lock with F2), using the help texts in `assets/viewInfo/`; Help menu links and
  module documentation.
* View menu: Lighting (Ctrl+L), High contrast (Ctrl+H), text size (Ctrl+= / Ctrl+- /
  Ctrl+0).
* Shortcuts that differ: plain ←/→ for scenes (Ctrl+←/→ in the port), Enter for
  return-to-start (Home in the port), Ctrl+3 is Module view instead of Info view.
* Files: Save As doesn't rename the project node; the Open dialog doesn't start in the
  current folder; `.ixd` isn't accepted; opening `.module`/`.bundle` files doesn't install
  them; Install accepts `.module` only (no `.bundle`); no "Loading…" indicator.
* Preferences: Reset to Defaults; MIDI device lists don't refresh; panel sizes, last
  folders and font size aren't persisted.
* About box (licence/acknowledgements), update check. The crash reporter is mostly
  unnecessary now that libIntegra runs in-process.

Deliberate differences: the port's undo history covers project edits only (Flex also
recorded transport and view changes), and it adds a few extras (Module view ENV toggle,
ribbon scene buttons and meters, demo project).

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
| Module library tags | `ModuleLibrary.as` | The tag list is rebuilt when modules are installed. *(No automated test.)* |
| Pin drag | — | The connection being dragged no longer points into the graph's module list, which is rebuilt on model changes. *(No automated test.)* |
| MIDI indicator | `MidiInputIndicator.as` | Lights for every incoming message, including repeats of the same message. *(No automated test.)* |

## Arrange view and timeline

**Missing / partial**
* Scene properties: selecting a scene doesn't show scene properties (there is no scene
  page in the properties panel yet).
* Scenes: inline rename by double-click; Backspace to delete; scene properties title bar
  (state combo, key binding combo, **MIDI learn for a scene**); project MIDI learn for
  next/previous scene.
* Timeline: the drag-to-zoom/scroll strip at the top of the ruler (drag sideways to
  scroll, up/down to zoom, Shift locks an axis).
* Tracks: collapse button, active (mute) toggle, VU meter, drag the bottom edge to
  resize (only 4 preset heights), drag headers to reorder (only move up/down), full colour
  picker (only 7 colours), import/export track, double-click below the tracks to add one,
  Ctrl+Shift+T.
* Blocks: multi-select (Ctrl/Cmd-click) with group move/delete; moving only by the title strip; a new block
  centred on a double-click that pushes later blocks; Ctrl+B / Ctrl+Shift+B; inline rename;
  **Block Library panel** with drag onto a track.
* Envelopes: **curvature editing** (drag a segment, double-click to straighten; curves
  are also drawn straight), envelope lock / Shift, click the line to select and add a point
  (now: pick from a menu, then double-click), double-click a point to delete it (now
  right-click), value/time readout while dragging, add/remove envelopes from the block
  title bar.
* Properties panel: clickable breadcrumbs, Ctrl+E to run a script.

## Module view

**Missing / partial**
* **Module Manager** (uninstall, install embedded modules, switch/upgrade versions,
  Upgrade All, Always Upgrade) and the **upgrade dialog on load**.
* Library: grouping of versions with "Show alternative versions", source labels
  (Shipped / 3rd Party / Embedded / In Development), embedded modules, multi-tag
  filtering, clear buttons, Enter to add, a ghost preview while dragging, **dropping a
  module onto a link to splice it in**.
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
* Right-click on the canvas to add/remove a module's control at that point (new controls
  all land at the top left); resize from every edge; snapping to neighbours; clamping.
* Per-track collapse, project/track **active (mute) buttons**, project/track/block VU
  meters.
* Read-only timeline above the Live view; the on-screen **keyboard map** for scene
  bindings with drag-to-rebind.
* Live view is rebuilt on every structural change, interrupting drags.

## Application

* Ribbon: clickable MIDI indicator with CC/note readout; lighting (light/dark) button.
* **Info view**: hover-driven Markdown help for whatever is under the mouse
  (Ctrl+3, lock with F2), using the help texts in `assets/viewInfo/`; Help menu links and
  module documentation.
* View menu: Lighting (Ctrl+L), High contrast (Ctrl+H), text size (Ctrl+= / Ctrl+- /
  Ctrl+0), Module Manager (Ctrl+4).
* Shortcuts that differ: plain ←/→ for scenes (Ctrl+←/→ in the port), Enter for
  return-to-start (Home in the port), Ctrl+3 is Module view instead of Info view.
* Files: Save As doesn't rename the project node; the Open dialog doesn't start in the
  current folder; `.ixd` isn't accepted; opening `.module`/`.bundle` files doesn't install
  them; Install accepts `.module` only and shows no report; no "Loading…" indicator.
* Preferences: Reset to Defaults; MIDI device lists don't refresh; panel sizes, last
  folders and font size aren't persisted.
* About box (licence/acknowledgements), update check. The crash reporter is mostly
  unnecessary now that libIntegra runs in-process.

Deliberate differences: the port's undo history covers project edits only (Flex also
recorded transport and view changes), and it adds a few extras (Module view ENV toggle,
ribbon scene buttons and meters, demo project).

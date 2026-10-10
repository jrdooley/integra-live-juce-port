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
| Typing values, Revert to default | `AttributeLabel.as`, `ModuleProperties.as` | Double-click a control's value to type one (Enter sets, Escape cancels, numbers clamped to the range, enumerations by name); double-click no longer resets a slider. Right-click a control for *Revert to default* > this control / All Controls (one undo step), skipping locked controls. |
| In-place rename | `SceneBar.as`, `BlockView.as`, `ModuleGraphElement.as`, `ViewHolder.as`, `RenameObject.as` | Double-click a scene, a block's name, a track's name, a module's name or the properties / module-controls titles; Enter or clicking away commits, Escape cancels; only legal name characters can be typed; invalid names (leading digit, taken) are dropped, as in Flex. The rename dialogs are gone. |
| Track height and order | `ViewHolder.as`, `ViewTree.as`, `SetTrackHeight`, `SetTrackOrder` | Drag a track header's bottom edge (grip in the corner) to resize it, drag the header to move the track; one undo step each. |
| Multi-selection | `ArrangeView.as`, `ModuleGraph.as`, `Utilities.as` (Cmd on macOS, Ctrl elsewhere) | Ctrl/Cmd-click adds or removes blocks and modules; dragging one moves them all (blocks keep their time and track offsets, and push other blocks aside); Delete removes them all; a plain click on one of a group selects just it. Rubber-band selection on empty space in the module graph (Ctrl/Cmd toggles). |
| Envelope editing | `BlockView.as`, `EnvelopeView.as` | Clicking within 8 px of an envelope selects it; clicking the selected envelope adds a point there and drags it; points are hit 4 px either side; double-click a point to delete it (not the first); envelope lock button (blocks with several envelopes, Shift inverts); "seconds, value" readout while dragging. |
| Files | `IntegraLive.mxml` (saveAs, doLoad, onSelectFileToSave, doAutoloadIfNecessary, displayProgressPopup), `InstallModules.as` | Save As renames the project after the file (illegal characters and leading digits dropped) and always writes `.integra`; Open starts in the project's folder (else the last one used) and accepts bare `.ixd` files; opening a `.module` or `.bundle` file (command line, Finder / Explorer) installs it; Install takes several `.module` and `.bundle` files (a bundle is a zip of modules); "Loading: name..." / "Starting new Project..." messages while a project loads. |
| Info view | `views/InfoView/`, `InfoMarkupForViews.as`, `assets/viewInfo/`, `IntegraView.getInfoToDisplay` | Help for whatever is under the mouse: Flex's 82 Markdown texts (imported as `viewInfo/`) for the ribbon, arrange view, scenes, envelopes, VU meters, properties tabs, module library and graph, Live view and Scene Shortcuts, and the project's own info for blocks, tracks, scenes and modules (each attribute's description for module controls). Ctrl/Cmd+3 shows or hides it (remembered); F2 (Ctrl+3 on macOS) locks it so it can be scrolled, and a locked node's info can be edited. Beside the properties in the Arrange view and Module View, beside Scene Shortcuts in the Live view. Help > Integra Live Website. |
| View menu | `IntegraLive.mxml` menus, `ColorScheme.as`, `FontSize.as` | Lighting (Ctrl/Cmd+L) switches light / dark, High Contrast (Ctrl/Cmd+H, stored in the project), Text Size larger / smaller / reset (Ctrl/Cmd+= / - / 0; five steps, remembered). The port scales the whole window for text size, as its layout is in pixels. |
| Flex shortcuts | `IntegraLive.onStageKeyDown` | Plain ←/→ select the previous / next scene and Enter returns to the start (Ctrl/Cmd+←/→ and Home still work), wherever the focus is: ribbon buttons no longer take the keyboard focus. Ctrl/Cmd+3 is the Info view, as in Flex (the Module View is reached from a block). |
| Splice | `ModuleGraph.onDragDrop`, `onDragOver` | Dragging a module from the library shows it half transparent where it would go, and highlights a connection it covers; dropping it there replaces the connection with links into the module's first audio input and out of its first audio output (one undo step). |
| Track collapse | `ViewHolder.canCollapse`, `CollapseButtonSkin.as`, `SetTrackExpanded` | Collapse button left of the track name (a disc in the track colour with an arrow); double-clicking the rest of the title row toggles it too; a collapsed track shows only its title bar and its blocks can't be hit or dropped on; stored in `arrangeViewExpanded`, undoable. |
| Track colour | `TrackColorPicker.as`, `ArrangeView.as`, `ModuleGraph.as` | *Change Track Colour...* (track menu and Module View menu, for active tracks) opens a 128 px colour wheel at the mouse; dragging shows colours on the track live, releasing sets it in one undo step, a click outside closes it. The 7-colour palette is gone. |
| Import / export track | `ImportTrack.as`, `IntegraController.exportTrack` | File menu and track menu; the imported track goes last, is selected, and its envelopes are connected to the player again; block files are refused. One undo step. |
| New blocks and tracks | `ArrangeView.createBlockOnDoubleClick`, `createBlock`, `createBlockFromMenu`, `onDoubleClick` | A double-click on a lane adds a 20 s block centred on the click (at the end of a block it would start in), pushing later blocks along, in one undo step; Ctrl+B adds one after the selected track's last block; double-clicking below the tracks adds a track, clicking there shows the project; Ctrl+Shift+T deletes the selected track; Ctrl+Shift+B opens the selected block. |
| Block drag zones | `BlockView.getDragInfo` | A block moves only by its name strip, resizes by the 10 px edges below the strip, and anywhere else is only selected. The port's strip is at the top of the block (Flex: bottom). |
| Envelope selection | `BlockPropertiesTitlebar.as`, `AddEnvelope.as`, `RemoveEnvelope.as` | A block's properties title bar: Envelope <module> <attribute> [x]. Picking an attribute selects its envelope or adds one at its current value; attributes driven by routing or MIDI are disabled; x deletes the selected envelope. Kept in step with the envelope selected in the Arrange view. |
| Timeline zoom / scroll strip | `Timeline.as` (`_markingTop`, `_zoomCoefficient`) | A 12 px strip above the scene bar (Flex 15 px) with the scroll indicator: drag sideways to scroll, down / up to zoom in / out about the clicked time (15 px dead zone, doubling per 50 px); Shift locks an axis. |
| Scene delete key, breadcrumbs, Ctrl+E | `Timeline.as`, `Breadcrumbs.as`, `ScriptingView.as` | Backspace deletes the selected scene (ruler focused, scene in view). The properties title's project and track names are links that deselect down to them. Ctrl/Cmd+E runs the selected script, also from inside the editor (Flex only outside it). |
| Module graph selection | `ModuleGraph.onClickModule`, `onClickLink`, `removeSelection` | Selecting a module selects its connections (Ctrl/Cmd-click and the rubber band too); a plain click on a connection selects only it, Ctrl/Cmd-click toggles it; Delete / Backspace removes the selected connections and modules in one undo step. |
| Module snapping | `ModuleGraph.getModuleRepositionSnap` | A dragged module's edges snap to unselected modules' edges within 10 px (nearest first, left / top win), with snap lines; no half-grid snapping. |
| Live View tick | `ModuleGraphElement`, `SetModuleInstanceLiveViewControls`, `UserDataCommand.findNewLiveViewControlPosition` | A tick at each module's top right, in the track colour (grey when inactive), ticked when any of its controls is in the Live view; clicking adds all of them (placed left to right without overlaps) or removes all, one undo step. |
| Module graph menu | `ModuleGraph` context menu, `ImportModule.as` | Remove Selection, Import Module... (`.integra`, `.mixd`; top left at the clicked point; selected; block files refused; one undo step), Export Module..., Change Track Colour... |
| Tag cloud size | `TagCloud.as` | Drag the "Tags..." strip to resize the tag cloud, from nothing to its tags' height, at most half the library (not stored, as in Flex). |
| Widgets | `ControlManager`, `AttributeLabel`, `NumberBoxControl`, `RangeSliderControl`, `KnobControl`, `ControlContainer` | Values go to the nearest allowed value and integers are rounded; sliders step through integer ranges of up to 32. Multi-value controls have a value to type per attribute (XY pad x / y, range min / max). Click a number box to type. Range slider: drag an end (4 px), drag the middle to move both, drag elsewhere to draw a range, double-click the middle to swap the ends; vertical when taller than wide. Knobs: vertical drag, 200 px for the whole range. MIDI learn on a control with several values asks which one. |
| Live view track collapse | `ViewHolder.canCollapse`, `LiveViewTrack.as`, `SetTrackExpanded` | Collapse button and title-bar click, while a block is shown; collapsed tracks show only their title bar; stored in `liveViewExpanded` (Flex's `LIVE_VIEW == ARRANGE_VIEW` bug made it share the Arrange view's state; the port keeps them apart); undoable. |
| Live view timeline | `Timeline(false)`, `MainViewContainer.mxml`, `DoTimelineAutoscroll.as` | A read-only timeline above the Live view, sharing the Arrange view's zoom and scroll: its zoom strip works, clicking a scene selects it, elsewhere moves the playhead. Both timelines follow the playhead while playing (back to 10% when it leaves 10-80% of the width). |
| MIDI readout | `MidiInputIndicator.as` | "cc<number>  <value>" and "note  <pitch>" (c4 = 60) for the last control change and note-on, each for 5 seconds; not clickable, as in Flex. |
| Settings | `Preferences.as`, `ResetAudioAndMidiSettings.as` | Reset to Defaults; the MIDI device lists follow libIntegra's. |
| Remembered panel sizes | `MainViewContainer.mxml` application state | The Arrange view and Module View properties heights (with the Block Library width, Scene Shortcuts height and collapse, Info view state and text size from earlier). |
| About and Help | `AboutBox.mxml`, `ModuleDocumentationWriter.as`, `InfoView.as` | About box with version, copyright, acknowledgements and the GPL page; Help > Module Documentation generates the endpoint reference page; Info view links open in the browser. |
| MIDI indicator | `MidiInputIndicator.as` | Lights for every incoming message, including repeats of the same message. *(No automated test.)* |

## Arrange view and timeline

**Missing / partial**
* Tracks: the track being moved isn't drawn as a floating image; Flex's Live view
  collapse state (which, through an upstream bug, wrote the Arrange view's) isn't used.
* Blocks: a group drag pushes other blocks out of the whole span the group covers on
  each track (Flex pushed around each block); Flex's open-block button in the name
  strip (double-click or Ctrl+Shift+B instead).
* The colour wheel's shading follows Flex's formula but isn't pixel-identical.

## Module view

**Missing / partial**
* Graph: module elements are drawn flat (Flex: gradients and bevels); links are cubic
  curves rather than Flex's arcs; dragging from the library over a link doesn't push
  downstream modules right.
* Widgets: values typed into a number box are sent on Enter (Flex sent every
  keystroke); allowed-value notches aren't drawn on sliders and knobs; the Flex
  controls' shapes and glows are approximated with JUCE sliders.

## Live view

**Missing / partial**
* Collapsing and expanding isn't animated, nor is the change of block under the
  playhead (Flex slid the controls).
* Snapping while resizing diagonally uses each edge's own snap (Flex's
  `doDiagonalReposition` keeps the aspect of the drag); the maximum control sizes of
  `ControlMeasurer` aren't applied.

## Application

* Ribbon: the lighting (light/dark) button is in the View menu instead.
* Text size scales the whole window rather than only the text.
* Settings: the window position and size, the module library width and the last
  block / track / module folders aren't remembered; audio devices are JUCE's selector
  rather than Flex's driver / device / channel / sample-rate rows.
* Help: Flex's local help pages (Integra Live Help, module development guides) and the
  User Forum link aren't shipped; the startup update check (integralive.org, which no
  longer serves it) is left out. The crash reporter is mostly unnecessary now that
  libIntegra runs in-process.

Deliberate differences: the port's undo history covers project edits only (Flex also
recorded transport and view changes), and it adds a few extras (Module view ENV toggle,
ribbon scene buttons and meters, demo project).

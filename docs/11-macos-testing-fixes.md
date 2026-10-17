# 11 — Fixes from testing the macOS build

Bugs found by testing the macOS app (CI build of f3b6687) on a real Mac.

## Saved projects couldn't be reopened: "File validation error", then a loop and a quit

**Report:** a project saved without complaint. Reopening it showed "File validation
error" again and again; clicking OK quit the app. The file itself is fine: it validates
against `CollectionSchema.xsd` and opens on Linux.

There were two separate bugs.

### 1. libIntegra looked for its schema in the wrong place (macOS)

`validator.cpp` built the path of `CollectionSchema.xsd` in a static initialiser, when the
program starts. That is before the app sets `INTEGRA_RESOURCES_PATH` (`IntegraSession`), so
libIntegra used its own guesses (see `CFileHelper::get_resources_path`):

- the folder of the binary containing libIntegra, plus `Resources/` on macOS. libIntegra is
  linked statically into the app, so this was `Contents/MacOS/Resources/`, which doesn't
  exist (the resources are in `Contents/Resources/resources/`);
- the source tree's `libIntegra/data`, baked in at build time. That exists on the machine
  that built the app (so everything worked in local and CI tests), but not on a user's Mac.

The schema never loaded, so every `.integra` file failed validation: opening projects,
importing blocks and tracks, and undoing a delete (which reimports the deleted nodes).

**Fix:** the path is worked out each time a `CValidator` is created, after the app has set
`INTEGRA_RESOURCES_PATH`. **Test:** `IntegraLive_self_test_relocated` runs the self test
(which saves, reopens and imports projects) with `INTEGRA_RESOURCES_PATH` pointing nowhere
at start-up, as in the bundle. It fails without the fix.

### 2. Any failed open looped its error message

The app is built with `JUCE_MODAL_LOOPS_PERMITTED` (the GUI test pumps the message loop).
With that, JUCE runs `AlertWindow::showMessageBoxAsync` **synchronously** when no callback is
passed: a nested modal loop inside the caller. Opening a project runs from a one-shot timer
(so the "Loading..." message is drawn first), and the nested loop of the error box fired
that timer again. The open ran again, failed again and showed another error box, and so on.
The quit when clicking OK most likely came from unwinding these nested loops (not reproduced
exactly here: it needs the macOS window system).

**Fix:**
- `UI/MessageBox.h`: `showMessage()` passes a callback, so the box really is asynchronous.
  Every message box in the app uses it (the one at start-up before quitting stays
  synchronous on purpose).
- `MainComponent::runWithProgressMessage` runs its work once only, even if a nested loop
  fires its timer again.

**Test:** the GUI test opens an invalid project through the normal Open path. It checks that
one error box appears and that the app carries on with a new project. Before the fix this
step hung.

### Note: projects saved while playing

The test project was saved while playing (`Player.play` = 1). Opening it, libIntegra's player
starts during the load and logs `endpoint not found: <project>.Player1.tick` once, before the
Player's `tick` endpoint exists. This is harmless: the project opens and plays.

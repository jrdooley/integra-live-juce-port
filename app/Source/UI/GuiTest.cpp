#include "GuiTest.h"
#include "Widgets/IntegraWidgets.h"
#include "ModuleManager.h"
#include "LiveViewParts.h"
#include "Core/SelfTest.h"

namespace integra
{
    namespace
    {
        struct Checker
        {
            juce::String& log;
            int failures = 0;

            void check (bool ok, const juce::String& what)
            {
                auto line = juce::String (ok ? "ok   " : "FAIL ") + what + "\n";
                log << line;
                std::cout << line << std::flush;
                if (! ok)
                    ++failures;
            }
        };

        /** Lets queued async updates (notifications, change messages) run. */
        void pump (int ms = 150)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
        }

        juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos,
                                    int clicks, bool dragged, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
        {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, pos, mods, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &c, &c, now, downPos, now, clicks, dragged);
        }

        void click (juce::Component& c, juce::Point<float> pos)
        {
            c.mouseDown (makeEvent (c, pos, pos, 1, false));
            c.mouseUp (makeEvent (c, pos, pos, 1, false));
        }

        void doubleClick (juce::Component& c, juce::Point<float> pos)
        {
            c.mouseDown (makeEvent (c, pos, pos, 2, false));
            c.mouseUp (makeEvent (c, pos, pos, 2, false));
            c.mouseDoubleClick (makeEvent (c, pos, pos, 2, false));
        }

        void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
        {
            c.mouseDown (makeEvent (c, from, from, 1, false));
            const int steps = 8;
            for (int i = 1; i <= steps; ++i)
                c.mouseDrag (makeEvent (c, from + (to - from) * ((float) i / steps), from, 1, true));
            c.mouseUp (makeEvent (c, to, from, 1, true));
        }

        template <typename Type>
        void findAll (juce::Component& root, std::vector<Type*>& out)
        {
            for (auto* child : root.getChildren())
            {
                if (auto* t = dynamic_cast<Type*> (child))
                    out.push_back (t);
                findAll (*child, out);
            }
        }
    }

    /** With INTEGRA_GUI_TEST_SHOTS set to a folder, saves a PNG of a component there
        (used for the screenshots in docs/). */
    static void saveShot (juce::Component& c, const juce::String& name)
    {
        auto dir = juce::SystemStats::getEnvironmentVariable ("INTEGRA_GUI_TEST_SHOTS", {});
        if (dir.isEmpty())
            return;
        auto file = juce::File (dir).getChildFile (name);
        file.deleteFile();      // FileOutputStream appends to an existing file
        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (c.createComponentSnapshot (c.getLocalBounds()), *stream);
    }

    int runGuiTest (MainComponent& main, AppContext& context, juce::String& log)
    {
        Checker t { log };
        auto& model = context.model;
        auto& p = model.get();

        t.check (context.controller.newProject().wasOk(), "new project");
        main.showView ("arrange");
        pump();

        auto& arrange = main.getArrangeView();
        auto& lanes = arrange.getLanes();
        auto& mapping = lanes.getMapping();
        const auto track = p.tracks.at (0).path;
        const int rowMiddle = juce::jlimit (40, 600, p.tracks[0].userData.arrangeViewHeight) / 2;
        const int rate = p.player.rate;

        // the Block Library takes the right of the Arrange view: zoom out so the blocks
        // dragged below stay inside the lanes
        arrange.zoom (0.7, (float) mapping.laneX);
        pump();

        // --- Arrange view ---------------------------------------------------------
        doubleClick (lanes, { mapping.tickToX (1200), (float) rowMiddle });
        pump();
        t.check (p.tracks[0].blocks.size() == 2 && p.tracks[0].blocks[1].start == 1200,
                 "double-click empty lane adds a block at the clicked time");

        const auto block2 = p.tracks[0].blocks.size() > 1 ? p.tracks[0].blocks[1].path : juce::String();
        const int length = p.tracks[0].blocks.size() > 1 ? p.tracks[0].blocks[1].length() : 0;
        const float blockMiddle = mapping.tickToX (1200 + length / 2);
        drag (lanes, { blockMiddle, (float) rowMiddle }, { blockMiddle + (float) (10 * rate * mapping.pixelsPerTick), (float) rowMiddle });
        pump();
        t.check (p.findBlock (block2) != nullptr && p.findBlock (block2)->start == 1200 + 10 * rate,
                 "dragging a block moves it 10 s later (start " + juce::String (p.findBlock (block2) ? p.findBlock (block2)->start : -1) + ")");

        const float blockEnd = mapping.tickToX (p.findBlock (block2) ? p.findBlock (block2)->end : 0) - 2.0f;
        drag (lanes, { blockEnd, (float) rowMiddle }, { blockEnd - (float) (5 * rate * mapping.pixelsPerTick), (float) rowMiddle });
        pump();
        t.check (p.findBlock (block2) != nullptr && p.findBlock (block2)->length() == length - 5 * rate,
                 "dragging a block's end edge shortens it by 5 s");

        // dropping a block onto another pushes it along (ArrangeView.doBlockRescheduling)
        const auto block1Path = p.tracks[0].blocks.at (0).path;
        auto blockRange = [&] (const juce::String& path)
        {
            auto* b = p.findBlock (path);
            return b != nullptr ? juce::Range<int> (b->start, b->end) : juce::Range<int>();
        };
        auto describeRange = [] (juce::Range<int> r) { return juce::String (r.getStart()) + ".." + juce::String (r.getEnd()); };
        auto describeBlocks = [&] { return " (" + describeRange (blockRange (block1Path)) + " / " + describeRange (blockRange (block2)) + ")"; };
        drag (lanes, { mapping.tickToX (100), (float) rowMiddle }, { mapping.tickToX (1300), (float) rowMiddle });
        pump();
        t.check (blockRange (block1Path) == juce::Range<int> (1200, 2000) && blockRange (block2) == juce::Range<int> (2000, 2600),
                 "dropping a block onto another pushes it along" + describeBlocks());
        main.invokeCommand (MainComponent::cmdUndo);
        pump();
        t.check (blockRange (block1Path) == juce::Range<int> (0, 800) && blockRange (block2) == juce::Range<int> (1600, 2200),
                 "one undo puts both blocks back" + describeBlocks());

        // Delete with no block selected must not delete the track
        {
            click (lanes, { mapping.tickToX (1000), (float) rowMiddle });   // empty lane: selects the track only
            const int tracksBeforeDelete = (int) p.tracks.size();
            lanes.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
            pump();
            t.check ((int) p.tracks.size() == tracksBeforeDelete, "Delete with only a track selected doesn't delete the track");
        }

        const int tracksBefore = (int) p.tracks.size();
        click (lanes, { 50.0f, (float) lanes.getTotalHeight() - TrackLanes::addTrackHeight / 2.0f });
        pump();
        t.check ((int) p.tracks.size() == tracksBefore + 1, "clicking \"Add track\" adds a track");

        // move block 2 to the new track by dragging vertically
        const auto secondTrackY = (float) juce::jlimit (40, 600, p.tracks[0].userData.arrangeViewHeight)
                                  + juce::jlimit (40, 600, p.tracks[1].userData.arrangeViewHeight) / 2.0f;
        const float b2x = mapping.tickToX (p.findBlock (block2) ? p.findBlock (block2)->start + 40 : 0);
        drag (lanes, { b2x, (float) rowMiddle }, { b2x, secondTrackY });
        pump();
        t.check (p.tracks.size() == 2 && p.tracks[1].blocks.size() == 1 && p.tracks[0].blocks.size() == 1,
                 "dragging a block down moves it to the next track");

        // scene by dragging in the ruler's scene bar
        auto& ruler = arrange.getRuler();
        drag (ruler, { mapping.tickToX (0) + 0.5f, 8.0f }, { mapping.tickToX (8 * rate), 8.0f });
        pump();
        t.check (p.scenes.size() == 1 && p.scenes[0].start == 0 && p.scenes[0].length == 8 * rate,
                 "dragging in the scene bar creates an 8 s scene");

        click (ruler, { mapping.tickToX (20 * rate), 30.0f });
        pump();
        t.check (context.controller.getPlayPosition() == 20 * rate, "clicking the ruler moves the playhead");

        // undo via the Edit menu command
        const int scenesBefore = (int) p.scenes.size();
        main.invokeCommand (MainComponent::cmdUndo);
        pump();
        t.check ((int) p.scenes.size() == scenesBefore - 1, "Edit > Undo removes the scene");
        main.invokeCommand (MainComponent::cmdRedo);
        pump();
        t.check ((int) p.scenes.size() == scenesBefore, "Edit > Redo restores it");

        // scene duration editing (Flex: SceneBar resize areas, Timeline.repositionSceneToMousePosition)
        {
            const auto scene1 = p.scenes.at (0).path;
            auto sceneRange = [&] (const juce::String& path)
            {
                auto* s = p.findScene (path);
                return s != nullptr ? juce::Range<int> (s->start, s->end()) : juce::Range<int>();
            };
            auto describe = [] (juce::Range<int> r) { return " (" + juce::String (r.getStart()) + ".." + juce::String (r.getEnd()) + ")"; };

            drag (ruler, { mapping.tickToX (320) - 3.0f, 8.0f }, { mapping.tickToX (480) - 3.0f, 8.0f });
            pump();
            t.check (sceneRange (scene1) == juce::Range<int> (0, 480), "dragging a scene's end handle extends it" + describe (sceneRange (scene1)));

            drag (ruler, { mapping.tickToX (480) - 3.0f, 8.0f }, { mapping.tickToX (200) - 3.0f, 8.0f });
            pump();
            t.check (sceneRange (scene1) == juce::Range<int> (0, 200), "dragging a scene's end handle shortens it" + describe (sceneRange (scene1)));

            drag (ruler, { mapping.tickToX (0) + 3.0f, 8.0f }, { mapping.tickToX (80) + 3.0f, 8.0f });
            pump();
            t.check (sceneRange (scene1) == juce::Range<int> (80, 200), "dragging a scene's start handle shortens it from the left" + describe (sceneRange (scene1)));

            drag (ruler, { mapping.tickToX (400), 8.0f }, { mapping.tickToX (600), 8.0f });
            pump();
            juce::String scene2;
            for (auto& s : p.scenes)
                if (s.path != scene1)
                    scene2 = s.path;
            t.check (sceneRange (scene2) == juce::Range<int> (400, 600), "a second scene is created" + describe (sceneRange (scene2)));

            drag (ruler, { mapping.tickToX (200) - 3.0f, 8.0f }, { mapping.tickToX (480) - 3.0f, 8.0f });
            pump();
            t.check (sceneRange (scene1) == juce::Range<int> (80, 480) && sceneRange (scene2) == juce::Range<int> (480, 680),
                     "extending a scene pushes the next scene along" + describe (sceneRange (scene1)) + describe (sceneRange (scene2)));

            main.invokeCommand (MainComponent::cmdUndo);
            pump();
            t.check (sceneRange (scene1) == juce::Range<int> (80, 200) && sceneRange (scene2) == juce::Range<int> (400, 600),
                     "one undo restores both scenes" + describe (sceneRange (scene1)) + describe (sceneRange (scene2)));

            // end handle snaps to the end of the first block (800) within 10 pixels
            drag (ruler, { mapping.tickToX (200) - 3.0f, 8.0f }, { mapping.tickToX (790) - 3.0f, 8.0f });
            pump();
            t.check (sceneRange (scene1) == juce::Range<int> (80, 800) && sceneRange (scene2) == juce::Range<int> (800, 1000),
                     "a scene edge snaps to a block edge" + describe (sceneRange (scene1)) + describe (sceneRange (scene2)));
        }

        // --- Module view ----------------------------------------------------------
        const auto block1 = p.tracks[0].blocks.at (0).path;
        doubleClick (lanes, { mapping.tickToX (100), (float) rowMiddle });
        pump();
        t.check (main.getCurrentView() == "module" && main.getModuleView().getBlock() == block1, "double-click a block opens the Module View");

        auto& graph = main.getModuleView().getGraph();
        auto* delay = context.session.getCatalogue().findByName ("TapDelay");
        juce::DragAndDropTarget::SourceDetails details (juce::String (ModuleLibrary::dragPrefix) + toString (delay->guid), nullptr, { 270, 200 });
        t.check (graph.isInterestedInDragSource (details), "module graph accepts library drags");
        graph.itemDropped (details);
        pump();
        const auto delayPath = block1 + ".TapDelay1";
        auto* b1 = p.findBlock (block1);
        t.check (b1 != nullptr && b1->findModule ("TapDelay1") != nullptr
                    && b1->findModule ("TapDelay1")->position.getPosition().toInt() == juce::Point<int> (15, 11),
                 "dropping TapDelay adds it at the drop position (grid 15, 11)");
        graph.refreshLayout();

        auto out = graph.getPinPosition (block1 + ".AudioIn1.out");
        auto in = graph.getPinPosition (delayPath + ".in1");
        t.check (out.has_value() && in.has_value(), "pins located");
        if (out && in)
        {
            drag (graph, *out, *in);
            pump();
        }
        b1 = p.findBlock (block1);
        bool connected = false;
        if (b1 != nullptr)
            for (auto& c : b1->connections)
                connected |= c.sourcePath == block1 + ".AudioIn1.out" && c.targetPath == delayPath + ".in1";
        t.check (connected, "dragging from an output pin to an input pin connects them");

        // drag the module somewhere else
        if (auto bounds = graph.getModuleBounds (delayPath))
        {
            auto start = bounds->getCentre();
            drag (graph, start, start + juce::Point<float> (ModuleGraph::gridSize * 4, ModuleGraph::gridSize * 2));
            pump();
            b1 = p.findBlock (block1);
            t.check (b1 != nullptr && b1->findModule ("TapDelay1") != nullptr
                        && b1->findModule ("TapDelay1")->position.getPosition().toInt() == juce::Point<int> (19, 13),
                     "dragging a module stores its new grid position in userData");
        }

        // selecting the module shows its controls; moving a slider sets the endpoint
        if (auto bounds = graph.getModuleBounds (delayPath))
            click (graph, bounds->getCentre());
        pump();
        std::vector<IntegraWidget*> widgets;
        findAll (main.getModuleView().getProperties(), widgets);
        t.check (widgets.size() == 7, "TapDelay shows its 7 IID widgets (" + juce::String ((int) widgets.size()) + ")");

        IntegraWidget* feedbackKnob = nullptr;
        for (auto* w : widgets)
            if (w->getSpec().label == "feedback")
                feedbackKnob = w;
        std::vector<juce::Slider*> sliders;
        if (feedbackKnob != nullptr)
            findAll (*feedbackKnob, sliders);
        if (! sliders.empty())
        {
            sliders[0]->setValue (0.25, juce::sendNotificationSync);
            pump();
        }
        auto feedback = (double) context.session.getValue (delayPath + ".feedback");
        t.check (std::abs (feedback - 0.25) < 0.01, "moving the feedback knob sets TapDelay1.feedback (" + juce::String (feedback, 3) + ")");

        context.session.setValue (delayPath + ".feedback", 0.75);
        pump();
        t.check (! sliders.empty() && std::abs (sliders[0]->getValue() - 0.75) < 0.01, "the knob follows endpoint changes from elsewhere");

        // "LIVE" toggle -> live view control
        std::vector<juce::Button*> buttons;
        if (feedbackKnob != nullptr)
            findAll (*feedbackKnob->getParentComponent(), buttons);
        for (auto* b : buttons)
            if (b->getButtonText() == "LIVE")
                b->triggerClick();
        pump (300);
        b1 = p.findBlock (block1);
        t.check (b1 != nullptr && b1->userData.findLiveViewControl ("TapDelay1", "feedback") != nullptr,
                 "LIVE toggle adds the control to the block's live view layout");

        // ENV toggle -> envelope
        for (auto* b : buttons)
            if (b->getButtonText() == "ENV")
                b->triggerClick();
        pump (300);
        b1 = p.findBlock (block1);
        t.check (b1 != nullptr && b1->findEnvelopeForTarget (delayPath + ".feedback") != nullptr, "ENV toggle adds an envelope for the control");

        // a control driven by an envelope is locked (ControlContainer padlock)
        {
            std::vector<IntegraWidget*> lockedWidgets;
            findAll (main.getModuleView(), lockedWidgets);
            IntegraWidget* knob = nullptr;
            for (auto* w : lockedWidgets)
                if (w->getModulePath() == delayPath && w->getSpec().label == "feedback")
                    knob = w;
            t.check (knob != nullptr && knob->isFullyLocked(), "the enveloped knob shows as locked");
            const auto before = (double) context.session.getValue (delayPath + ".feedback");
            std::vector<juce::Slider*> knobSliders;
            if (knob != nullptr)
                findAll (*knob, knobSliders);
            if (! knobSliders.empty())
                knobSliders[0]->setValue (before > 0.5 ? 0.1 : 0.9, juce::sendNotificationSync);
            pump();
            t.check (std::abs ((double) context.session.getValue (delayPath + ".feedback") - before) < 1.0e-6,
                     "a locked control doesn't change its endpoint");
        }

        // --- Live view ------------------------------------------------------------
        context.controller.setPlayPosition (100);
        main.showView ("live");
        pump (400);
        std::vector<IntegraWidget*> liveWidgets;
        findAll (main.getLiveView(), liveWidgets);
        t.check (liveWidgets.size() == 1 && liveWidgets[0]->getSpec().label == "feedback", "Live view shows the feedback control of the playing block");

        // moving a control stores its new position relative to the block area (no header offset)
        if (liveWidgets.size() == 1)
        {
            auto* control = liveWidgets[0]->getParentComponent();
            auto storedBounds = [&]
            {
                if (auto* blk = p.findBlock (block1))
                    for (auto& c : blk->userData.liveViewControls)
                        if (c.widgetLabel == "feedback")
                            return c.bounds;
                return juce::Rectangle<float>();
            };
            const auto before = storedBounds();
            control->mouseDown (makeEvent (*control, { 20.0f, 5.0f }, { 20.0f, 5.0f }, 1, false));
            control->mouseDrag (makeEvent (*control, { 50.0f, 25.0f }, { 20.0f, 5.0f }, 1, true));
            control->mouseUp (makeEvent (*control, { 50.0f, 25.0f }, { 20.0f, 5.0f }, 1, true));
            pump (400);
            const auto after = storedBounds();
            t.check (after.getPosition() == before.getPosition() + juce::Point<float> (30.0f, 20.0f),
                     "moving a live control stores exactly the dragged offset ("
                     + before.getPosition().toString() + " -> " + after.getPosition().toString() + ")");
        }

        context.controller.setSceneKeybinding (p.scenes.at (0).path, "q");
        pump();
        t.check (p.scenes.at (0).userData.keybinding == "Q", "scene keys are stored upper-case, as in Flex");
        context.controller.activateScene ({});
        main.getRibbon().grabKeyboardFocus();   // scene keys work wherever the focus is
        if (auto* peer = main.getPeer())
            peer->handleKeyPress (juce::KeyPress ('q'));
        pump();
        t.check (context.session.getValue (p.player.path + ".scene").toString() == p.scenes.at (0).name, "scene key binding selects the scene in Live view");

        // --- delete via keyboard in the module graph --------------------------------
        main.showView ("module");
        if (auto bounds = graph.getModuleBounds (delayPath))
            click (graph, bounds->getCentre());
        graph.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        pump();
        t.check (! context.session.nodeExists (delayPath), "Delete key removes the selected module");
        b1 = p.findBlock (block1);
        t.check (b1 != nullptr && b1->envelopes.empty() && b1->userData.liveViewControls.empty(),
                 "...together with its envelope and live view control");
        main.invokeCommand (MainComponent::cmdUndo);
        pump();
        t.check (context.session.nodeExists (delayPath) && p.findBlock (block1)->envelopes.size() == 1, "undo brings them all back");

        // --- envelope control points (EnvelopeView) ---------------------------------
        main.showView ("arrange");
        pump();
        if (auto* blk = p.findBlock (block1); blk != nullptr && ! blk->envelopes.empty())
        {
            const auto envelope = blk->envelopes.front().path;
            const auto pointA = context.controller.addControlPoint (envelope, 200, 0.2f);
            const auto pointB = context.controller.addControlPoint (envelope, 400, 0.2f);
            pump();
            context.model.selectedBlock = block1;
            lanes.editedEnvelope = envelope;
            lanes.repaint();

            auto pointTick = [&] (const juce::String& path)
            {
                if (auto* b = p.findBlock (block1))
                    for (auto& env : b->envelopes)
                        for (auto& cp : env.points)
                            if (cp.path == path)
                                return cp.tick;
                return -1;
            };

            if (auto pos = lanes.getControlPointPosition (pointA))
            {
                drag (lanes, *pos, { mapping.tickToX (blk->start + 600), pos->y });
                pump();
            }
            t.check (pointTick (pointA) == 399, "a control point can't be dragged past the next one (tick " + juce::String (pointTick (pointA)) + ")");

            juce::String firstPoint;
            if (auto* b = p.findBlock (block1))
                firstPoint = b->envelopes.front().points.front().path;
            if (auto pos = lanes.getControlPointPosition (firstPoint))
            {
                drag (lanes, *pos, { pos->x + 40.0f, pos->y });
                pump();
                if (auto pos2 = lanes.getControlPointPosition (firstPoint))
                    lanes.mouseDown (makeEvent (lanes, *pos2, *pos2, 1, false, juce::ModifierKeys::rightButtonModifier));
                pump();
            }
            t.check (context.session.nodeExists (firstPoint) && pointTick (firstPoint) == 0,
                     "the first control point stays at tick 0 and can't be deleted");

            // curve mode (BlockView curvature button, EnvelopeView curve drag)
            auto curvatureOf = [&] (const juce::String& path)
            {
                if (auto* b = p.findBlock (block1))
                    for (auto& env : b->envelopes)
                        for (auto& cp : env.points)
                            if (cp.path == path)
                                return cp.curvature;
                return -99.0f;
            };
            auto button = lanes.getCurveButtonBounds();
            t.check (! button.isEmpty(), "the edited block shows a curve mode button");
            click (lanes, button.getCentre());
            pump();
            t.check (lanes.isCurveMode() && p.findBlock (block1)->userData.curvatureMode, "clicking it turns curve mode on (stored in the block)");

            // drag the middle of the segment after pointA (399 -> 400 is too short; use first -> pointA)
            auto from = lanes.getControlPointPosition (firstPoint), to = lanes.getControlPointPosition (pointA);
            if (from && to && std::abs (from->y - to->y) < 4.0f)
            {
                // make the segment slope so it can bend
                context.controller.moveControlPoint (pointA, 399, 0.8f);
                pump();
                to = lanes.getControlPointPosition (pointA);
            }
            if (from && to)
            {
                auto middle = (*from + *to) * 0.5f;
                auto bent = juce::Point<float> (from->x + (to->x - from->x) * 0.25f, middle.y);   // pull towards the start
                drag (lanes, middle, bent);
                pump();
            }
            t.check (curvatureOf (firstPoint) != 0.0f, "dragging a segment in curve mode bends it (curvature " + juce::String (curvatureOf (firstPoint), 2) + ")");
            if (from && to)
                doubleClick (lanes, (*from + *to) * 0.5f);
            pump();
            t.check (curvatureOf (firstPoint) == 0.0f, "double-clicking the segment straightens it");
            lanes.toggleCurveMode();
            pump();
            juce::ignoreUnused (pointB);
        }

        // --- Module Manager -------------------------------------------------------------
        {
            auto closeDialogs = [&]
            {
                juce::StringArray titles;
                for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;)
                    if (auto* d = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                    {
                        titles.add (d->getName());
                        d->exitModalState (0);
                    }
                pump();
                return titles;
            };
            auto findDialog = [] (const juce::String& title) -> juce::DialogWindow*
            {
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* d = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if (d->getName() == title)
                            return d;
                return nullptr;
            };
            auto clickButton = [&] (juce::Component& root, const juce::String& text)
            {
                std::vector<juce::Button*> found;
                findAll (root, found);
                for (auto* b : found)
                    if (b->getButtonText() == text && b->isEnabled())
                    {
                        b->triggerClick();
                        pump (300);
                        return true;
                    }
                return false;
            };
            auto guidOf = [&] (const juce::String& path)
            {
                auto* b = p.findBlock (parentPath (path));
                auto* m = b != nullptr ? b->findModule (lastElement (path)) : nullptr;
                return m != nullptr && m->entry != nullptr ? m->entry->guid : GUID {};
            };

            auto& catalogue = context.session.getCatalogue();
            const GUID shippedGuid = catalogue.findByName ("TapDelay")->guid;
            auto altFile = makeAlternativeModuleVersion (context.session.getLocations().systemModules.getChildFile ("TapDelay.module"), "frequency",
                                                         juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TapDelayGuiTest.module"));
            t.check (context.controller.installModule (altFile).wasOk(), "install a 2nd version of TapDelay for the Module Manager");
            altFile.deleteFile();
            GUID altGuid = shippedGuid;
            for (auto* v : catalogue.getVersions (catalogue.findByGuid (shippedGuid)->originGuid))
                if (! guidsEqual (v->guid, shippedGuid))
                    altGuid = v->guid;

            ModuleManager manager (context, [&] { main.getModuleView().getLibrary().refresh(); });
            manager.setSize (900, 560);
            manager.showTab (ModuleManager::Tab::versions);
            pump();

            std::vector<juce::ListBox*> lists;
            findAll (manager, lists);
            const bool listsFound = lists.size() == 2 && lists[0]->getModel()->getNumRows() == 1;
            t.check (listsFound, "Manage Versions lists TapDelay as a module with alternative versions");
            if (listsFound)
            {
                lists[0]->selectRow (0);
                pump();
                t.check (lists[1]->getModel()->getNumRows() == 1, "...and its 3rd party version as the alternative");
                lists[1]->selectRow (0);
                pump();
                saveShot (manager, "module-manager-versions.png");
                t.check (clickButton (manager, "Switch Versions") && guidsEqual (guidOf (delayPath), altGuid),
                         "Switch Versions switches the project's TapDelay");
                t.check (findDialog ("Module Switch Report") != nullptr, "a switch report is shown");
                closeDialogs();
            }

            // the upgrade prompt (shown after opening a project) upgrades back to the shipped version
            ModuleManager::setAlwaysUpgrade (context, false);
            ModuleManager::checkForUpgrades (context, {});
            pump();
            auto* prompt = findDialog ("Upgrade Modules");
            t.check (prompt != nullptr, "a project with an older module version offers to upgrade it");
            if (prompt != nullptr)
            {
                clickButton (*prompt, "Upgrade");
                t.check (guidsEqual (guidOf (delayPath), shippedGuid), "Upgrade switches it to the latest official version");
                t.check (findDialog ("Module Upgrade Report") != nullptr, "an upgrade report is shown");
            }
            closeDialogs();

            // remove the backup the upgrade saved
            for (auto dir : { juce::File::userDesktopDirectory, juce::File::userDocumentsDirectory, juce::File::tempDirectory })
                for (auto& f : juce::File::getSpecialLocation (dir).findChildFiles (juce::File::findFiles, false, "* (backup *).integra"))
                    f.deleteFile();

            manager.showTab (ModuleManager::Tab::install);
            pump();
            lists.clear();
            findAll (manager, lists);
            if (lists.size() == 2)
                lists[0]->selectRow (0);
            pump();
            saveShot (manager, "module-manager-install.png");
            t.check (lists.size() == 2 && lists[0]->getModel()->getNumRows() >= 1, "Install / Uninstall lists the installed 3rd party module");
            // --- Module library: versions, source headings, filters -------------------
            {
                main.getModuleView().setBlock (block1);
                auto& library = main.getModuleView().getLibrary();
                library.setSearchText ({});
                library.clearTags();
                pump();

                auto& rows = library.getRows();
                bool allAudio = ! rows.empty();
                for (auto& r : rows)
                    if (r.entry != nullptr)
                        allAudio &= r.entry->numAudioInputs + r.entry->numAudioOutputs > 0;
                t.check (allAudio, "the module library lists only modules with audio endpoints");

                int delayRow = -1;
                for (size_t i = 0; i < rows.size(); ++i)
                    if (rows[i].kind == ModuleLibrary::Row::Kind::module && rows[i].entry->name == "TapDelay")
                        delayRow = (int) i;
                t.check (delayRow >= 0 && rows[(size_t) delayRow].numVersions == 2 && guidsEqual (rows[(size_t) delayRow].entry->guid, shippedGuid),
                         "TapDelay shows once, as its shipped version, with 2 versions");

                bool sorted = true;
                for (size_t i = 1; i < rows.size(); ++i)
                    if (rows[i].kind == ModuleLibrary::Row::Kind::module && rows[i - 1].kind == ModuleLibrary::Row::Kind::module
                         && rows[i].source == rows[i - 1].source && rows[i].entry->label.toUpperCase() < rows[i - 1].entry->label.toUpperCase())
                        sorted = false;
                t.check (sorted, "library lines are sorted by label");

                if (delayRow >= 0)
                {
                    library.selectRow (delayRow);
                    library.handleKey (juce::KeyPress (juce::KeyPress::rightKey));
                    pump();
                    juce::StringArray headings;
                    int versionRows = 0, altRow = -1;
                    for (size_t i = 0; i < rows.size(); ++i)
                    {
                        if (rows[i].kind == ModuleLibrary::Row::Kind::sourceLabel)
                            headings.add (ModuleLibrary::getSourceHeading (rows[i].source));
                        if (rows[i].kind == ModuleLibrary::Row::Kind::version)
                        {
                            ++versionRows;
                            if (guidsEqual (rows[i].entry->guid, altGuid))
                                altRow = (int) i;
                        }
                    }
                    t.check (versionRows == 2 && headings == juce::StringArray ("LATEST OFFICIAL", "THIRD PARTY"),
                             "Right expands the versions under source headings (" + headings.joinIntoString (", ") + ")");
                    t.check (altRow >= 0 && ModuleLibrary::getInfoMarkdown (*rows[(size_t) altRow].entry, false).contains ("##3rd Party")
                                && ModuleLibrary::getInfoMarkdown (*rows[(size_t) altRow].entry, false).contains ("**Author:**"),
                             "a version's info names its source, date and author");

                    if (altRow >= 0)
                    {
                        library.selectRow (altRow - 1);     // a heading: selection moves on to a module line
                        pump();
                        t.check (library.getSelectedRow() >= 0 && rows[(size_t) library.getSelectedRow()].kind != ModuleLibrary::Row::Kind::sourceLabel,
                                 "source headings can't be selected");
                        library.selectRow (altRow);
                        const auto before = p.findBlock (block1) != nullptr ? p.findBlock (block1)->modules.size() : 0;
                        library.getList().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                        pump();
                        bool added = false;
                        if (auto* b = p.findBlock (block1))
                            for (auto& m : b->modules)
                                added |= m.entry != nullptr && guidsEqual (m.entry->guid, altGuid);
                        t.check (added && p.findBlock (block1)->modules.size() == before + 1, "Enter on a version line adds that version");
                    }

                    library.selectRow (delayRow);
                    library.handleKey (juce::KeyPress (juce::KeyPress::leftKey));
                    pump();
                    t.check (std::none_of (rows.begin(), rows.end(), [] (auto& r) { return r.kind == ModuleLibrary::Row::Kind::version; }),
                             "Left collapses them again");
                }

                const auto all = rows.size();
                library.setSearchText ("delay");
                pump();
                bool matches = ! rows.empty() && rows.size() < all;
                for (auto& r : rows)
                    matches &= r.entry == nullptr || r.entry->label.containsIgnoreCase ("delay");
                t.check (matches, "searching filters on module labels");

                const auto tags = library.getTags();
                library.toggleTag (tags[0]);
                pump();
                bool tagged = ! rows.empty();
                for (auto& r : rows)
                    tagged &= r.entry == nullptr || r.entry->tags.contains (tags[0]);
                t.check (tagged && library.getSelectedTags() == juce::StringArray (tags[0]), "selecting a tag filters on it and clears the search");
                const auto oneTag = rows.size();
                library.toggleTag (tags[1]);
                pump();
                bool both = true;
                for (auto& r : rows)
                    both &= r.entry == nullptr || (r.entry->tags.contains (tags[0]) && r.entry->tags.contains (tags[1]));
                t.check (both && rows.size() <= oneTag, "several tags filter on all of them");
                library.setSearchText ("a");
                pump();
                t.check (library.getSelectedTags().isEmpty(), "searching clears the tags");
                library.setSearchText ({});
                pump();
                t.check (rows.size() == all, "clearing the filters lists every module again");

                if (juce::SystemStats::getEnvironmentVariable ("INTEGRA_GUI_TEST_SHOTS", {}).isNotEmpty())
                {
                    main.showView ("module");
                    library.setSearchText ("delay");
                    if (auto* delay = catalogue.findByGuid (shippedGuid))
                        library.setExpanded (delay->originGuid, true);
                    pump();
                    saveShot (main, "module-library.png");
                    library.setSearchText ({});
                    main.showView ("arrange");
                }
            }

            t.check (context.controller.uninstallModule (altGuid, true).wasOk() && catalogue.getVersions (catalogue.findByGuid (shippedGuid)->originGuid).size() == 1,
                     "uninstall it again");
            pump();
        }

        // --- Block Library ----------------------------------------------------------------
        {
            context.controller.newProject();
            main.showView ("arrange");
            pump();
            auto& library = arrange.getBlockLibrary();
            library.refresh();
            auto& entries = library.getEntries();
            t.check (entries.size() >= 6 && entries[0].title == "AudioThru" && ! entries[0].isUserBlock,
                     "the Block Library lists the shipped blocks in index.xml order");
            int basicDelay = -1;
            for (size_t i = 0; i < entries.size(); ++i)
                if (entries[i].title == "BasicDelay")
                    basicDelay = (int) i;
            t.check (basicDelay >= 0 && entries[(size_t) basicDelay].info.contains ("TapDelay"), "a block's info text is read from the file");

            const auto& tr = p.tracks.at (0);
            const auto trackPath = tr.path;
            const int y = juce::jlimit (40, 600, tr.userData.arrangeViewHeight) / 2;
            const int blockTicks = ProjectController::newBlockSeconds * rate;
            const auto firstEnd = tr.blocks.empty() ? 0 : tr.blocks[0].end;
            const auto file = entries[(size_t) juce::jmax (0, basicDelay)].file;

            juce::DragAndDropTarget::SourceDetails details (juce::String (BlockLibrary::dragPrefix) + file.getFullPathName(), &library.getList(),
                                                            { juce::roundToInt (mapping.tickToX (firstEnd + blockTicks)), y });
            t.check (lanes.isInterestedInDragSource (details), "tracks accept Block Library drags");
            lanes.itemDragEnter (details);
            t.check (std::abs (lanes.getDropOutline().getWidth() - (float) (blockTicks * mapping.pixelsPerTick)) < 1.0f,
                     "dragging over a track outlines a new block");
            lanes.itemDropped (details);
            pump();
            t.check (lanes.getDropOutline().isEmpty() && p.tracks[0].blocks.size() == 2
                        && p.tracks[0].blocks[1].start == firstEnd + blockTicks - blockTicks / 2 && p.tracks[0].blocks[1].length() == blockTicks,
                     "dropping imports the block centred on the drop point");

            // dropped over the first block: starts at its end, later blocks are pushed along
            const auto secondStart = p.tracks[0].blocks[1].start;
            auto result = lanes.importBlockAt (file, { juce::roundToInt (mapping.tickToX (firstEnd)), y });
            pump();
            bool noOverlap = true;
            auto& blocks = p.tracks[0].blocks;
            for (size_t i = 0; i < blocks.size(); ++i)
                for (size_t j = i + 1; j < blocks.size(); ++j)
                    noOverlap &= blocks[i].end <= blocks[j].start || blocks[j].end <= blocks[i].start;
            t.check (result.wasOk() && blocks.size() == 3 && noOverlap
                        && std::any_of (blocks.begin(), blocks.end(), [&] (auto& b) { return b.start == firstEnd; })
                        && std::none_of (blocks.begin(), blocks.end(), [&] (auto& b) { return b.start == secondStart; }),
                     "a block dropped onto others starts after them and pushes later blocks along (" + [&]
                     {
                         juce::StringArray ranges;
                         for (auto& b : blocks)
                             ranges.add (juce::String (b.start) + ".." + juce::String (b.end));
                         return ranges.joinIntoString (", ") + "; first end " + juce::String (firstEnd) + ")";
                     }());
            context.controller.getUndoManager().undo();
            model.rebuild();
            pump();
            t.check (p.tracks[0].blocks.size() == 2 && p.tracks[0].blocks[1].start == secondStart, "one undo removes the import and puts the blocks back");

            model.selectedTrack = trackPath;
            lanes.playPosition = 0;
            library.getList().selectRow (0);
            library.getList().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            pump();
            t.check (p.tracks[0].blocks.size() == 3, "Enter imports the selected block into the selected track");

            // user blocks: saved with "Add to Block Library", removed from its menu
            auto userFile = context.session.getLocations().userBlockLibrary.getChildFile ("Gui Test Block.integra");
            t.check (context.controller.exportNode (p.tracks[0].blocks[0].path, userFile).wasOk(), "save a block to the user Block Library");
            library.refresh();
            int userRow = -1;
            for (size_t i = 0; i < entries.size(); ++i)
                if (entries[i].file == userFile)
                    userRow = (int) i;
            t.check (userRow == (int) entries.size() - 1 && entries[(size_t) userRow].isUserBlock, "user blocks are listed after the shipped ones");
            t.check (! library.removeEntry (0) && library.removeEntry (userRow) && ! userFile.exists(),
                     "only user blocks can be removed from the library");

            library.getList().selectRow (1);
            pump();
            saveShot (main, "block-library.png");
        }

        // --- Active buttons and VU meters ----------------------------------------------
        {
            context.controller.newProject();
            main.showView ("arrange");
            pump();
            const auto trackPath = p.tracks.at (0).path;
            const auto blockPath = p.tracks[0].blocks.empty() ? juce::String() : p.tracks[0].blocks[0].path;
            auto activeOf = [&] (const juce::String& path) { return (int) context.session.getValue (path + ".active") != 0; };

            auto button = lanes.getActiveButtonBounds (trackPath);
            click (lanes, button.getCentre());
            pump();
            t.check (! activeOf (trackPath) && ! ActiveButton::isEffectivelyActive (context, blockPath),
                     "the track's active button switches the track (and its blocks) off");
            click (lanes, button.getCentre());
            pump();
            t.check (activeOf (trackPath), "...and on again");

            arrange.getProjectActiveButton().toggle();
            pump();
            t.check (! activeOf (p.path) && ! ActiveButton::isEffectivelyActive (context, trackPath), "the project's active button switches everything off");
            arrange.getProjectActiveButton().toggle();
            pump();
            t.check (activeOf (p.path), "...and on again");

            // a level reported by the block's audio output shows on the track's meter
            juce::String output;
            if (auto* b = p.findBlock (blockPath))
                for (auto& m : b->modules)
                    if (m.entry != nullptr && m.entry->name == "StereoAudioOut")
                        output = m.path.isNotEmpty() ? m.path : blockPath + "." + m.name;
            if (output.isEmpty() && p.findBlock (blockPath) != nullptr)
                for (auto& m : p.findBlock (blockPath)->modules)
                    if (m.entry != nullptr && m.entry->name == "StereoAudioOut")
                        output = blockPath + "." + m.name;
            // feed the output a test tone
            if (auto* tone = context.session.getCatalogue().findByName ("TestSource"))
            {
                auto source = context.controller.addModule (blockPath, tone->guid, { 1.0f, 8.0f });
                context.controller.connect (blockPath, source + ".out1", output + ".in1");
            }
            context.controller.setPlayPosition (10);
            pump (800);
            auto* level = lanes.getTrackLevel (trackPath);
            t.check (level != nullptr && level->getLevel() > 0.5f,
                     "the track meter follows its audio outputs' VU (" + juce::String (level != nullptr ? level->getLevel() : -1.0f, 2) + ")");
            t.check (arrange.getProjectMeter().getLevel().getLevel() > 0.5f, "...and so does the project meter");

            main.getModuleView().setBlock (blockPath);
            t.check (main.getModuleView().getBlockActiveButton().isActive(), "the Module View has the block's active button");
        }

        // --- Scene properties -------------------------------------------------------------
        {
            auto& ruler = arrange.getRuler();
            auto& properties = main.getProperties();
            const auto scene = context.controller.addScene (rate * 2, rate * 4);
            pump();
            const float sceneX = mapping.tickToX (rate * 4);
            click (ruler, { sceneX, (float) TimelineRuler::sceneBarHeight / 2 });
            pump();
            t.check (properties.getContainerPath() == scene && properties.getTabs().getNumTabs() == 1 && properties.getTitlebar() != nullptr,
                     "clicking a scene shows its properties (" + properties.getContainerPath() + ")");

            std::vector<juce::ComboBox*> combos;
            if (auto* bar = properties.getTitlebar())
                findAll (*bar, combos);
            if (combos.size() == 2)
            {
                for (int i = 0; i < combos[1]->getNumItems(); ++i)
                    if (combos[1]->getItemText (i) == "Q")
                        combos[1]->setSelectedItemIndex (i, juce::sendNotificationSync);
                for (int i = 0; i < combos[0]->getNumItems(); ++i)
                    if (combos[0]->getItemText (i) == "loop")
                        combos[0]->setSelectedItemIndex (i, juce::sendNotificationSync);
                pump();
            }
            auto* sc = p.findScene (scene);
            t.check (combos.size() == 2 && sc != nullptr && sc->userData.keybinding == "Q" && sc->mode == "loop",
                     "the scene's State and Key can be set in its title bar");
            saveShot (main, "scene-properties.png");

            auto findMidi = [&] (const juce::String& endpoint) -> const model::MidiControlInput*
            {
                for (auto& sca : p.scalers)
                    if (sca.targetPath == endpoint)
                        for (auto& m : p.midiControlInputs)
                            if (m.outputConnection == sca.upstreamConnection)
                                return &m;
                return nullptr;
            };
            auto clickButtonIn = [&] (juce::Component* root, const juce::String& text, int index = 0)
            {
                std::vector<juce::TextButton*> buttons;
                if (root != nullptr)
                    findAll (*root, buttons);
                for (auto* b : buttons)
                    if (b->getButtonText() == text && index-- == 0)
                    {
                        b->triggerClick();
                        pump();
                        return true;
                    }
                return false;
            };
            clickButtonIn (properties.getTitlebar(), "Learn");
            auto* learning = findMidi (scene + ".activate");
            t.check (learning != nullptr && learning->autoLearn, "MIDI learn for a scene adds a learning MIDI input routed to its activate");
            clickButtonIn (properties.getTitlebar(), "x");
            t.check (findMidi (scene + ".activate") == nullptr, "...and x removes it");

            click (lanes, { mapping.tickToX (rate * 60), (float) lanes.getTotalHeight() - 10.0f });   // below the tracks
            pump();
            t.check (properties.getContainerPath() == p.path && properties.getTabs().getNumTabs() == 4 && properties.getTitlebar() != nullptr,
                     "clicking the lanes shows the project again, with its title bar");
            clickButtonIn (properties.getTitlebar(), "Learn", 1);
            auto* next = findMidi (p.player.path + ".next");
            t.check (next != nullptr && next->autoLearn, "the project's title bar learns MIDI for Next Scene");
            clickButtonIn (properties.getTitlebar(), "Learn", 1);
            t.check (findMidi (p.player.path + ".next") == nullptr, "...clicking Learn again while learning cancels it");
        }

        // --- Live view: scene shortcuts, controls ---------------------------------------
        {
            main.showView ("live");
            pump();
            auto& live = main.getLiveView();
            auto& keys = live.getKeybindingView();
            live.setKeybindingViewCollapsed (false);
            pump();
            const auto scene = p.scenes.empty() ? juce::String() : p.scenes[0].path;
            context.controller.activateScene ({});
            pump();

            t.check (keys.isVisible() && keys.getSceneForKey ('Q') == scene, "the Scene Shortcuts keyboard shows the scene on its key");
            click (keys, keys.getKeyBounds ('Q').getCentre());
            pump();
            t.check (context.session.getValue (p.player.path + ".scene").toString() == lastElement (scene), "clicking a key selects its scene");
            drag (keys, keys.getKeyBounds ('Q').getCentre(), keys.getKeyBounds ('W').getCentre());
            pump();
            t.check (p.findScene (scene) != nullptr && p.findScene (scene)->userData.keybinding == "W", "dragging a scene's key onto a free key rebinds it");
            live.setKeybindingViewCollapsed (true);
            pump();
            t.check (! keys.isVisible(), "Scene Shortcuts can be collapsed");
            live.setKeybindingViewCollapsed (false);

            // track header: active button and meter
            context.controller.activateScene ({});
            const auto trackPath = p.tracks.at (0).path;
            const auto blockPath = p.tracks[0].blocks.at (0).path;
            context.controller.setPlayPosition (p.tracks[0].blocks[0].start + 1);
            pump (300);
            auto* panel = live.getPanel (trackPath);
            t.check (panel != nullptr && panel->getBlock() == blockPath, "the Live view shows the block under the playhead");
            if (panel != nullptr)
            {
                panel->getActiveButton().toggle();
                pump();
                t.check ((int) context.session.getValue (trackPath + ".active") == 0, "the Live view track header switches the track off");
                panel->getActiveButton().toggle();
                pump();
            }

            // only changes to what is shown rebuild it
            const int rebuilds = live.getNumRebuilds();
            context.controller.setValue (blockPath + ".info", "changed info");
            pump (300);
            t.check (live.getNumRebuilds() == rebuilds, "unrelated model changes don't rebuild the Live view");

            // right-click: add a control at the click point
            panel = live.getPanel (trackPath);
            juce::String modulePath, widgetLabel;
            if (auto* b = p.findBlock (blockPath))
                for (auto& m : b->modules)
                    for (auto& spec : WidgetSpec::forModule (context.session, blockPath + "." + m.name))
                        if (modulePath.isEmpty() && spec.type == "Slider" || (modulePath.isEmpty() && spec.type == "Knob"))
                            modulePath = blockPath + "." + m.name, widgetLabel = spec.label;
            if (panel != nullptr && modulePath.isNotEmpty())
            {
                for (auto& c : p.findBlock (blockPath)->userData.liveViewControls)
                    context.controller.setLiveViewControl (blockPath + "." + c.moduleName, c.widgetLabel, false);
                pump (300);
                panel = live.getPanel (trackPath);
                panel->toggleControl (modulePath, widgetLabel, { 200, LiveView::TrackPanel::headerHeight + 40 });
                pump (300);
                auto& controls = p.findBlock (blockPath)->userData.liveViewControls;
                t.check (controls.size() == 1 && juce::approximatelyEqual (controls[0].bounds.getX(), 200.0f) && juce::approximatelyEqual (controls[0].bounds.getY(), 40.0f),
                         "a control added from the menu appears where the block was right-clicked");

                // a second control, snapping and resizing from the left edge
                juce::String secondLabel;
                for (auto& spec : WidgetSpec::forModule (context.session, modulePath))
                    if (spec.label != widgetLabel && secondLabel.isEmpty() && spec.type != "VuMeter")
                        secondLabel = spec.label;
                panel = live.getPanel (trackPath);
                if (secondLabel.isNotEmpty() && panel != nullptr)
                {
                    panel->toggleControl (modulePath, secondLabel, { 500, LiveView::TrackPanel::headerHeight + 40 });
                    pump (300);
                    panel = live.getPanel (trackPath);
                    auto& cs = panel->getControls();
                    if (cs.size() == 2)
                    {
                        auto* first = cs[0];
                        auto* second = cs[1];
                        auto snapped = second->snapped (second->getBounds().withX (first->getRight() + 6), true, true, true, true, true);
                        t.check (snapped.getX() == first->getRight(), "a moved control snaps to its neighbour's edge");
                        second->clearSnapLines();

                        std::vector<juce::ResizableBorderComponent*> borders;
                        findAll (*second, borders);
                        const auto before = second->getBounds();
                        if (! borders.empty())
                            drag (*borders[0], { 2.0f, (float) before.getHeight() / 2 }, { -38.0f, (float) before.getHeight() / 2 });
                        pump (300);
                        auto* stored = [&]() -> const BlockUserData::LiveViewControl*
                        {
                            for (auto& c : p.findBlock (blockPath)->userData.liveViewControls)
                                if (c.widgetLabel == secondLabel)
                                    return &c;
                            return nullptr;
                        }();
                        t.check (stored != nullptr && stored->bounds.getX() < (float) before.getX() - 20.0f
                                    && juce::approximatelyEqual (stored->bounds.getRight(), (float) before.getRight()),
                                 "dragging a control's left edge resizes it, keeping its right edge");
                    }
                }

                panel = live.getPanel (trackPath);
                if (panel != nullptr)
                    panel->toggleControl (modulePath, widgetLabel, {});
                pump (300);
                bool removed = true;
                for (auto& c : p.findBlock (blockPath)->userData.liveViewControls)
                    removed &= c.widgetLabel != widgetLabel;
                t.check (removed, "choosing a shown control in the menu removes it");
            }

            saveShot (main, "live-view.png");
            main.showView ("arrange");
            pump();
        }

        // --- Batch 2: everyday editing -----------------------------------------------------
        {
            context.controller.newProject();
            main.showView ("arrange");
            pump();
            auto& undo = context.controller.getUndoManager();
            auto undoOnce = [&] { undo.undo(); model.rebuild(); pump(); };
            auto pressKey = [&] (juce::TextEditor* editor, int key)
            {
                if (editor != nullptr)
                    editor->keyPressed (juce::KeyPress (key));
                pump();
            };
            auto typeInto = [&] (juce::TextEditor* editor, const juce::String& text, int key = juce::KeyPress::returnKey)
            {
                if (editor != nullptr)
                    editor->setText (text, false);
                pressKey (editor, key);
            };
            const auto cmdClick = juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::leftButtonModifier);
            const auto trackPath = p.tracks.at (0).path;
            const auto blockPath = p.tracks[0].blocks.at (0).path;

            // ---- typing values and Revert to default (AttributeLabel, ModuleProperties)
            auto* tapDelay = context.session.getCatalogue().findByName ("TapDelay");
            const auto delay = context.controller.addModule (blockPath, tapDelay->guid, { 12.0f, 8.0f });
            main.getModuleView().setBlock (blockPath);
            main.showView ("module");
            main.getModuleView().getGraph().selectModulePath (delay);
            main.getModuleView().getProperties().setModule (delay);
            pump();

            std::vector<IntegraWidget*> widgets;
            findAll (main.getModuleView().getProperties(), widgets);
            IntegraWidget* delayTime = nullptr;
            IntegraWidget* feedback = nullptr;
            for (auto* w : widgets)
            {
                if (w->getSpec().label == "delayTime") delayTime = w;
                if (w->getSpec().label == "feedback")  feedback = w;
            }
            t.check (delayTime != nullptr && feedback != nullptr, "TapDelay's delayTime and feedback controls are shown");
            if (delayTime != nullptr && feedback != nullptr)
            {
                const auto delayEndpoint = delay + ".delayTime", feedbackEndpoint = delay + ".feedback";
                const auto info = EndpointInfo::find (context.session, delayEndpoint);

                doubleClick (*delayTime, delayTime->getValueTextArea().getCentre().toFloat());
                t.check (delayTime->getValueEditor() != nullptr, "double-clicking a control's value lets you type one");
                typeInto (delayTime->getValueEditor(), "0.75");
                t.check (std::abs ((double) context.session.getValue (delayEndpoint) - 0.75) < 1e-4 && delayTime->getValueEditor() == nullptr,
                         "Enter sets the typed value (" + context.session.getValue (delayEndpoint).toString() + ")");

                delayTime->startEditingValue();
                typeInto (delayTime->getValueEditor(), "99999");
                t.check (std::abs ((double) context.session.getValue (delayEndpoint) - info.maximum) < 1e-4, "a typed value is clamped to the range");

                delayTime->startEditingValue();
                typeInto (delayTime->getValueEditor(), "0.2", juce::KeyPress::escapeKey);
                t.check (std::abs ((double) context.session.getValue (delayEndpoint) - info.maximum) < 1e-4, "Escape cancels typing");

                t.check (delayTime->canRevert(), "a changed control can be reverted");
                delayTime->revertToDefault();
                pump();
                t.check (std::abs ((double) context.session.getValue (delayEndpoint) - (double) info.defaultValue) < 1e-4 && ! delayTime->canRevert(),
                         "Revert to default sets the control's default");

                context.controller.setValue (delayEndpoint, 0.5);
                context.controller.setValue (feedbackEndpoint, 0.9);
                pump();
                IntegraWidget::revertAllToDefault (context, delay);
                pump();
                const bool bothDefault = ! IntegraWidget::canRevertAll (context, delay);
                undoOnce();
                t.check (bothDefault && std::abs ((double) context.session.getValue (feedbackEndpoint) - 0.9) < 1e-4
                            && std::abs ((double) context.session.getValue (delayEndpoint) - 0.5) < 1e-4,
                         "Revert to default > All Controls reverts every control, as one undo step");

                // right-clicking a slider opens the menu without moving it
                std::vector<juce::Slider*> sliders;
                findAll (*delayTime, sliders);
                if (! sliders.empty())
                {
                    auto& slider = *sliders[0];
                    const auto far = juce::Point<float> ((float) slider.getWidth() - 2.0f, (float) slider.getHeight() / 2.0f);
                    slider.mouseDown (makeEvent (slider, far, far, 1, false, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
                    slider.mouseUp (makeEvent (slider, far, far, 1, false, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
                    pump();
                    juce::PopupMenu::dismissAllActiveMenus();
                    pump();
                }
                t.check (std::abs ((double) context.session.getValue (delayEndpoint) - 0.5) < 1e-4, "right-clicking a control doesn't change it");
            }

            // ---- rename in place
            auto& graph = main.getModuleView().getGraph();
            graph.renameModule (delay);
            typeInto (graph.getRenameEditor(), "Echo");
            const auto echo = blockPath + ".Echo";
            t.check (context.session.nodeExists (echo) && graph.isModuleSelected (echo), "a module can be renamed in place");

            main.getModuleView().getProperties().renameModule();
            typeInto (main.getModuleView().getProperties().getRenameEditor(), "Echo2");
            t.check (context.session.nodeExists (blockPath + ".Echo2"), "...also from the title of its controls");
            context.controller.rename (blockPath + ".Echo2", "Echo");
            pump();

            main.showView ("arrange");
            pump();
            auto& ruler = arrange.getRuler();
            const auto scene = context.controller.addScene (0, rate * 4);
            pump();
            doubleClick (ruler, { mapping.tickToX (rate * 2), (float) TimelineRuler::sceneBarHeight / 2 });
            typeInto (ruler.getRenameEditor(), "Intro");
            t.check (p.findScene (parentPath (scene) + ".Intro") != nullptr, "double-clicking a scene renames it in place");

            const int rowY = juce::jlimit (TrackLanes::minimumTrackHeight, TrackLanes::maximumTrackHeight, p.tracks[0].userData.arrangeViewHeight);
            const float blockTitleY = 12.0f;
            doubleClick (lanes, { mapping.tickToX (p.tracks[0].blocks[0].start + rate), blockTitleY });
            typeInto (lanes.getRenameEditor(), "Verse");
            const auto verse = trackPath + ".Verse";
            t.check (p.findBlock (verse) != nullptr, "double-clicking a block's name renames it in place");

            doubleClick (lanes, { 40.0f, 14.0f });
            typeInto (lanes.getRenameEditor(), "9bad");
            t.check (p.findTrack (trackPath) != nullptr, "an invalid name is dropped");
            doubleClick (lanes, { 40.0f, 14.0f });
            typeInto (lanes.getRenameEditor(), "Drums", juce::KeyPress::escapeKey);
            t.check (p.findTrack (trackPath) != nullptr, "Escape cancels renaming");

            model.selectedTrack = trackPath;
            model.selectedBlock = verse;
            main.getProperties().selectionChanged();
            main.getProperties().renameShown();
            typeInto (main.getProperties().getRenameEditor(), "Chorus");
            const auto chorus = trackPath + ".Chorus";
            t.check (p.findBlock (chorus) != nullptr && model.selectedBlock == chorus, "the properties title renames the selection in place");

            // ---- track height and order
            const int heightBefore = p.tracks[0].userData.arrangeViewHeight;
            auto grip = lanes.getTrackResizeArea (trackPath).getCentre().toFloat();
            drag (lanes, grip, grip + juce::Point<float> (0.0f, 50.0f));
            pump();
            t.check (p.tracks[0].userData.arrangeViewHeight == heightBefore + 50, "dragging a track's bottom edge resizes it ("
                        + juce::String (p.tracks[0].userData.arrangeViewHeight) + ")");
            undoOnce();
            t.check (p.tracks[0].userData.arrangeViewHeight == heightBefore, "...one undo step");

            context.controller.addTrack();
            pump();
            const auto second = p.tracks.at (1).path;
            const float secondHeaderY = (float) rowY + 20.0f;
            drag (lanes, { 100.0f, secondHeaderY }, { 100.0f, 10.0f });
            pump();
            t.check (p.tracks.size() == 2 && p.tracks[0].path == second, "dragging a track header moves the track");
            undoOnce();
            t.check (p.tracks[0].path == trackPath, "...and undo moves it back");

            // ---- multi-selection of blocks
            const auto b2 = context.controller.addBlock (trackPath, rate * 40, rate * 50);
            pump();
            auto blockCentre = [&] (const juce::String& path)
            {
                auto* b = p.findBlock (path);
                return juce::Point<float> (mapping.tickToX ((b->start + b->end) / 2), 12.0f);
            };
            click (lanes, blockCentre (chorus));
            {
                auto pos = blockCentre (b2);
                lanes.mouseDown (makeEvent (lanes, pos, pos, 1, false, cmdClick));
                lanes.mouseUp (makeEvent (lanes, pos, pos, 1, false, cmdClick));
            }
            t.check (lanes.getSelectedBlocks().size() == 2, "Ctrl/Cmd-click adds a block to the selection");
            const int chorusStart = p.findBlock (chorus)->start, b2Start = p.findBlock (b2)->start;
            {
                auto from = blockCentre (chorus);
                drag (lanes, from, from + juce::Point<float> ((float) (rate * 5 * mapping.pixelsPerTick), 0.0f));
                pump();
            }
            const int shifted = p.findBlock (chorus) != nullptr ? p.findBlock (chorus)->start - chorusStart : 0;
            t.check (shifted > 0 && p.findBlock (b2) != nullptr && p.findBlock (b2)->start - b2Start == shifted,
                     "dragging one selected block moves them all (" + juce::String (shifted) + " ticks)");
            undoOnce();
            t.check (p.findBlock (chorus)->start == chorusStart && p.findBlock (b2)->start == b2Start, "...one undo step");
            {
                auto pos = blockCentre (b2);
                lanes.mouseDown (makeEvent (lanes, pos, pos, 1, false, cmdClick));
                lanes.mouseUp (makeEvent (lanes, pos, pos, 1, false, cmdClick));
            }
            t.check (lanes.getSelectedBlocks().size() == 1, "Ctrl/Cmd-clicking a selected block deselects it");
            {
                auto pos = blockCentre (b2);
                lanes.mouseDown (makeEvent (lanes, pos, pos, 1, false, cmdClick));
                lanes.mouseUp (makeEvent (lanes, pos, pos, 1, false, cmdClick));
            }
            lanes.deleteSelection();
            pump();
            t.check (p.findBlock (chorus) == nullptr && p.findBlock (b2) == nullptr, "Delete removes every selected block");
            undoOnce();
            t.check (p.findBlock (chorus) != nullptr && p.findBlock (b2) != nullptr, "...and undo brings them back");

            // ---- multi-selection of modules: rubber band
            main.getModuleView().setBlock (chorus);
            main.showView ("module");
            pump();
            auto& g2 = main.getModuleView().getGraph();
            std::vector<juce::String> modulePaths;
            if (auto* b = p.findBlock (chorus))
                for (auto& m : b->modules)
                    modulePaths.push_back (chorus + "." + m.name);
            juce::Rectangle<float> all;
            for (auto& m : modulePaths)
                if (auto r = g2.getModuleBounds (m))
                    all = all.isEmpty() ? *r : all.getUnion (*r);
            drag (g2, all.getBottomRight() + juce::Point<float> (10.0f, 10.0f), all.getTopLeft() - juce::Point<float> (5.0f, 5.0f));
            pump();
            t.check (g2.getSelectedModules().size() == modulePaths.size() && modulePaths.size() >= 3,
                     "a rubber band selects the modules it touches (" + juce::String ((int) g2.getSelectedModules().size()) + ")");
            if (modulePaths.size() >= 2)
            {
                auto before0 = g2.getModuleBounds (modulePaths[0]);
                auto before1 = g2.getModuleBounds (modulePaths[1]);
                auto from = before0->getCentre();
                drag (g2, from, from + juce::Point<float> (0.0f, ModuleGraph::gridSize * 2));
                pump();
                auto after0 = g2.getModuleBounds (modulePaths[0]);
                auto after1 = g2.getModuleBounds (modulePaths[1]);
                t.check (after0 && after1 && after0->getY() > before0->getY() && std::abs ((after1->getY() - before1->getY()) - (after0->getY() - before0->getY())) < 0.5f,
                         "dragging one selected module moves them all");
                g2.deleteSelection();
                pump();
                bool allGone = true;
                for (auto& m : modulePaths)
                    allGone &= ! context.session.nodeExists (m);
                t.check (allGone, "Delete removes every selected module");
                undoOnce();
            }

            // ---- envelopes: click the line, double-click a point, lock
            main.showView ("arrange");
            pump();
            model.selectedBlock = {};
            lanes.editedEnvelope = {};
            const auto echoPath = chorus + ".Echo";
            const auto envelope = context.controller.addEnvelope (chorus, echoPath + ".feedback");
            pump();
            juce::String firstPoint;
            if (auto* b = p.findBlock (chorus))
                for (auto& e : b->envelopes)
                    if (e.path == envelope && ! e.points.empty())
                        firstPoint = e.points[0].path;
            auto first = lanes.getControlPointPosition (firstPoint);
            const auto onLine = first ? juce::Point<float> (first->x + 80.0f, first->y) : juce::Point<float>();
            click (lanes, onLine);
            t.check (lanes.editedEnvelope == envelope && model.selectedBlock == chorus, "clicking an envelope's line selects it");
            auto countPoints = [&]
            {
                if (auto* b = p.findBlock (chorus))
                    for (auto& e : b->envelopes)
                        if (e.path == envelope)
                            return (int) e.points.size();
                return 0;
            };
            const int pointsBefore = countPoints();
            drag (lanes, onLine, onLine + juce::Point<float> (0.0f, -10.0f));
            pump();
            t.check (countPoints() == pointsBefore + 1, "clicking the selected envelope's line adds a point there, and drags it");
            juce::String newPoint;
            if (auto* b = p.findBlock (chorus))
                for (auto& e : b->envelopes)
                    if (e.path == envelope && e.points.size() > 1)
                        newPoint = e.points[1].path;
            if (auto pos = lanes.getControlPointPosition (newPoint))
            {
                lanes.mouseDoubleClick (makeEvent (lanes, *pos, *pos, 2, false));
                pump();
            }
            t.check (countPoints() == pointsBefore, "double-clicking a control point deletes it");
            if (first)
            {
                lanes.mouseDoubleClick (makeEvent (lanes, *first, *first, 2, false));
                pump();
            }
            t.check (countPoints() == pointsBefore, "...but not the first one");

            context.controller.addEnvelope (chorus, echoPath + ".delayTime");
            pump();
            model.selectedBlock = chorus;
            lanes.repaint();
            auto lock = lanes.getEnvelopeLockBounds();
            click (lanes, lock.getCentre());
            pump();
            t.check (! lock.isEmpty() && p.findBlock (chorus) != nullptr && p.findBlock (chorus)->userData.envelopeLock,
                     "a block with several envelopes has an envelope lock button");
            saveShot (main, "envelope-editing.png");
        }

        // --- "Loading..." message ------------------------------------------------------
        {
            bool ran = false;
            main.runWithProgressMessage ("Loading: test...", [&] { ran = true; });
            const bool shownFirst = main.isShowingProgressMessage() && ! ran;
            pump (300);
            t.check (shownFirst && ran && ! main.isShowingProgressMessage(), "a progress message shows while a project loads");
            t.check (main.getDialogFolder().isDirectory(), "file dialogs start in an existing folder");
        }

        main.showView ("arrange");
        context.controller.newProject();
        pump();
        return t.failures;
    }
}

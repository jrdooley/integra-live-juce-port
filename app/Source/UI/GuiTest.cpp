#include "GuiTest.h"
#include "Widgets/IntegraWidgets.h"

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

        drag (lanes, { mapping.tickToX (100), (float) rowMiddle }, { mapping.tickToX (1300), (float) rowMiddle });
        pump();
        t.check (p.tracks[0].blocks.size() == 2 && p.tracks[0].blocks[0].start == 0, "a block can't be dragged onto another block");

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
                    && b1->findModule ("TapDelay1")->position.getPosition() == juce::Point<float> (15.0f, 11.0f),
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
                        && b1->findModule ("TapDelay1")->position.getPosition() == juce::Point<float> (19.0f, 13.0f),
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

        // --- Live view ------------------------------------------------------------
        context.controller.setPlayPosition (100);
        main.showView ("live");
        pump (400);
        std::vector<IntegraWidget*> liveWidgets;
        findAll (main.getLiveView(), liveWidgets);
        t.check (liveWidgets.size() == 1 && liveWidgets[0]->getSpec().label == "feedback", "Live view shows the feedback control of the playing block");

        auto& live = main.getLiveView();
        context.controller.setSceneKeybinding (p.scenes.at (0).path, "q");
        pump();
        live.keyPressed (juce::KeyPress ('q'));
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

        main.showView ("arrange");
        context.controller.newProject();
        pump();
        return t.failures;
    }
}

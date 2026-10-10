#pragma once

#include "Widgets/IntegraWidgets.h"
#include "ContainerControls.h"
#include "KeybindingView.h"
#include "InfoView.h"
#include "ArrangeView.h"

namespace integra
{
    /**
        Performance view (Flex: LiveView.as, LiveViewTrack.as, LiveViewBlock.as, KeybindingView.as).

        Each track shows the live-view controls of the block under the playhead (nothing
        when the playhead is outside every block, as in Flex), with the track's active
        button and VU meter in its header; the project's are at the top right. Controls can
        be moved by their title strip and resized from any edge, snapping to the other
        controls' edges; right-click the block area to add or remove a module's controls at
        that point. Scene buttons along the top and the Scene Shortcuts keyboard at the
        bottom select scenes; scene keys work anywhere in the window while this view is
        shown. The view is rebuilt only when what it shows changes, and never during a drag.
    */
    class LiveView  : public juce::Component,
                      public InfoProvider,
                      private juce::ChangeListener,
                      private juce::KeyListener,
                      private juce::Timer
    {
    public:
        LiveView (AppContext&, TimelineMapping& sharedTimeline);
        /** Called when the timeline's zoom strip changes the shared zoom / scroll. */
        std::function<void()> onTimelineChanged;
        TimelineRuler& getTimeline() noexcept               { return timeline; }
        ~LiveView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void visibilityChanged() override;
        void parentHierarchyChanged() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        Info getInfoAt (juce::Point<int>) override         { juce::ignoreUnused (getLocalBounds()); return InfoTexts::forView ("LiveView"); }

        class TrackPanel;
        class LiveControl;

        // for tests
        int getNumRebuilds() const noexcept                 { return numRebuilds; }
        TrackPanel* getPanel (const juce::String& trackPath) const;
        KeybindingView& getKeybindingView() noexcept        { return keybindings; }
        ActiveButton& getProjectActiveButton() noexcept     { return projectActive; }
        bool isKeybindingViewCollapsed() const noexcept     { return keybindingsCollapsed; }
        void setKeybindingViewCollapsed (bool);

        static constexpr int keybindingHeaderHeight = 22;

        /** Room for the Info view right of Scene Shortcuts (0 = none). */
        void setInfoWidth (int w)                           { infoWidth = w; resized(); }
        juce::Rectangle<int> getInfoArea() const noexcept   { return infoArea; }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void rebuild();
        void rebuildIfChanged();
        juce::String describeContent() const;
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        bool handleSceneKey (const juce::KeyPress&);
        void updateKeyTarget();
        juce::String blockToShow (const model::Track&) const;
        juce::Rectangle<int> keybindingHeaderBounds() const;

        AppContext& context;
        juce::OwnedArray<juce::TextButton> sceneButtons;
        ActiveButton projectActive { context };
        ContainerMeter projectMeter { context };
        TimelineRuler timeline;
        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<TrackPanel> panels;
        KeybindingView keybindings { context };
        bool keybindingsCollapsed = false;
        int keybindingsHeight = KeybindingView::defaultHeight, dragStartHeight = 0;
        juce::Rectangle<int> keybindingHeader, infoArea;
        int infoWidth = 0;
        bool resizingKeybindings = false;
        int playPosition = -1;
        juce::String shownContent;
        bool rebuildPending = false;
        int numRebuilds = 0;
        juce::Component::SafePointer<juce::Component> keyTarget;   // top-level component we listen to
    };
}

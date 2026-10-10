#pragma once

#include "Widgets/IntegraWidgets.h"

namespace integra
{
    /**
        Performance view (Flex: LiveView.as, LiveViewTrack.as, KeybindingView.as).

        Each track shows the live-view controls of the block under the playhead (nothing
        when the playhead is outside every block, as in Flex).
        Controls sit where they were placed and can be moved/resized by dragging their
        title strip / corner; the layout is stored in the block's userData exactly where
        the Flex GUI kept it. Scene buttons along the top select scenes; their key
        bindings work anywhere in the window while this view is shown.
    */
    class LiveView  : public juce::Component,
                      private juce::ChangeListener,
                      private juce::KeyListener,
                      private juce::Timer
    {
    public:
        explicit LiveView (AppContext&);
        ~LiveView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void visibilityChanged() override;
        void parentHierarchyChanged() override;

    private:
        class TrackPanel;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void rebuild();
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        bool handleSceneKey (const juce::KeyPress&);
        void updateKeyTarget();
        juce::String blockToShow (const model::Track&) const;

        AppContext& context;
        juce::OwnedArray<juce::TextButton> sceneButtons;
        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<TrackPanel> panels;
        int playPosition = -1;
        juce::String shownSignature;
        juce::Component::SafePointer<juce::Component> keyTarget;   // top-level component we listen to
    };
}

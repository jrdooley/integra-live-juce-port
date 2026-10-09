#pragma once

#include "Widgets/IntegraWidgets.h"

namespace integra
{
    /**
        Performance view (Flex: LiveView.as, LiveViewTrack.as, KeybindingView.as).

        Each track shows the live-view controls of the block that is currently playing in
        it (or, when the playhead is outside every block, the selected/first block).
        Controls sit where they were placed and can be moved/resized by dragging their
        title strip / corner; the layout is stored in the block's userData exactly where
        the Flex GUI kept it. Scene buttons along the top select scenes; their key
        bindings work while this view has focus.
    */
    class LiveView  : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
    {
    public:
        explicit LiveView (AppContext&);
        ~LiveView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void visibilityChanged() override;

    private:
        class TrackPanel;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void rebuild();
        juce::String blockToShow (const model::Track&) const;

        AppContext& context;
        juce::OwnedArray<juce::TextButton> sceneButtons;
        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<TrackPanel> panels;
        int playPosition = -1;
        juce::String shownSignature;
    };
}

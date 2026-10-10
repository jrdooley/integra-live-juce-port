#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
#include "InfoView.h"

namespace integra
{
    /**
        "Scene Shortcuts" (Flex: views/LiveView/KeybindingView.as, KeyIcon.as): an on-screen
        keyboard under the Live view. Keys bound to a scene show the scene's name, the
        current scene's key is highlighted, clicking a key selects its scene, and dragging
        a scene's key onto a free key rebinds the scene to it.
    */
    class KeybindingView  : public juce::Component,
                            public juce::TooltipClient,
                            public InfoProvider,
                            private juce::ChangeListener
    {
    public:
        explicit KeybindingView (AppContext&);
        ~KeybindingView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        juce::String getTooltip() override;
        /** A key's scene info, else what Scene Shortcuts do (KeybindingView.getInfoToDisplay). */
        Info getInfoAt (juce::Point<int> p) override
        {
            auto scene = getSceneForKey (keyAt (p.toFloat()));
            return scene.isNotEmpty() ? InfoTexts::forNode (context, scene) : InfoTexts::forView ("SceneShortcuts");
        }

        /** Where a key is drawn (empty if the key isn't on the keyboard). */
        juce::Rectangle<float> getKeyBounds (juce::juce_wchar key) const;
        /** The scene bound to a key ("" if none). */
        juce::String getSceneForKey (juce::juce_wchar key) const;

        static constexpr const char* keyRows[] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
        static constexpr int defaultHeight = 200, minimumHeight = 100, maximumHeight = 400;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        juce::String getCurrentScene() const;
        juce::juce_wchar keyAt (juce::Point<float>, bool inner = false) const;
        bool canDropOn (juce::juce_wchar key) const;

        AppContext& context;
        std::map<juce::juce_wchar, juce::Rectangle<float>> keys;
        std::vector<juce::Rectangle<float>> fillers;
        juce::juce_wchar draggedKey = 0, dragOverKey = 0;
        bool dragging = false;
    };
}

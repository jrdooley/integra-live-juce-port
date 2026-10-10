#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
#include "InfoView.h"

namespace integra
{
    /** Top bar: view switch, transport, scene navigation, meters (Flex: RibbonBar.mxml). */
    class RibbonBar  : public juce::Component,
                       public InfoProvider,
                       private juce::Timer
    {
    public:
        explicit RibbonBar (AppContext&);
        ~RibbonBar() override;

        void setCurrentView (const juce::String& viewName);   // "arrange" | "live" | "module"
        std::function<void (const juce::String&)> onViewRequested;
        std::function<void()> onPreferences;

        void paint (juce::Graphics&) override;
        Info getInfoAt (juce::Point<int>) override;
        void resized() override;

    private:
        void timerCallback() override;

        AppContext& context;
        juce::TextButton arrangeButton { "Arrange" }, liveButton { "Live" };
        juce::TextButton homeButton { "|<" }, playButton { "Play" }, prevScene { "<" }, nextScene { ">" };
        juce::TextButton prefsButton { "Settings" };
        juce::String sceneText, timeText;
        float outLevel = 0.0f, inLevel = 0.0f;
        bool midiActivity = false;
        juce::uint32 lastMidi = 0;
        juce::String watchedMidiMonitor;   // MidiRawInput whose messages light the MIDI indicator
    };
}

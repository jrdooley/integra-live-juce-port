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

        /** MidiInputIndicator: the last control change ("cc7", value) and note-on ("note",
            pitch name like "c4") the project's MIDI monitor received; each row goes back to
            " - " 5 seconds after its last message. */
        void midiReceived (juce::uint32 packedMessage);
        juce::String getControllerText() const;
        juce::String getNoteText() const;
        static juce::String pitchName (int pitch);     // Utilities.midiPitchToName: 60 -> "c4"
        static constexpr juce::uint32 midiReadoutMilliseconds = 5000;

    private:
        void timerCallback() override;
        int lastController = -1, lastControllerValue = 0, lastNote = -1;
        juce::uint32 lastControllerTime = 0, lastNoteTime = 0;

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

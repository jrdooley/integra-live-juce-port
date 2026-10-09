#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/AppContext.h"

namespace integra
{
    /**
        Preferences (Flex: Preferences.as): audio device selection (now JUCE's device
        selector, since JUCE owns the audio device), MIDI devices (libIntegra's
        MidiSettings module, which drives PortMidi), and the colour scheme.
    */
    class PreferencesPanel  : public juce::Component
    {
    public:
        explicit PreferencesPanel (AppContext&);

        void paint (juce::Graphics&) override;
        void resized() override;

        std::function<void (bool dark)> onColourSchemeChanged;

        static void show (AppContext&, std::function<void (bool)> onSchemeChanged);

    private:
        void refreshMidi();

        AppContext& context;
        juce::AudioDeviceSelectorComponent audio;
        juce::Label midiTitle, schemeTitle;
        juce::OwnedArray<juce::ToggleButton> midiInputs, midiOutputs;
        juce::ToggleButton darkScheme { "Dark colour scheme" };
    };
}

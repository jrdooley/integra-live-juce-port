#pragma once

#include "Widgets/IntegraWidgets.h"

namespace integra
{
    /**
        The control panel for one module (Flex: ModuleProperties.as). Lays out the
        module's IID widgets at their design positions. Each widget gets a small strip
        with toggles for "show in Live view", MIDI learn, and block envelope.
    */
    class ModuleProperties  : public juce::Component,
                              private juce::ChangeListener
    {
    public:
        explicit ModuleProperties (AppContext&);
        ~ModuleProperties() override;

        /** Shows the controls of the given module ("" = nothing). */
        void setModule (const juce::String& modulePath);
        const juce::String& getModule() const noexcept  { return modulePath; }

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class WidgetFrame;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void rebuild();

        AppContext& context;
        juce::String modulePath;
        juce::String title, description;
        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<WidgetFrame> frames;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleProperties)
    };
}

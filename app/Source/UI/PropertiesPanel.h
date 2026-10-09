#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
#include "Core/EndpointInfo.h"

namespace integra
{
    /** Renders the small Markdown subset used by Integra info texts into an AttributedString. */
    juce::AttributedString renderMarkdown (const juce::String& markdown, juce::Colour text, juce::Colour accent);

    /** Control endpoints below a container that can act as routing sources or targets. */
    juce::StringArray findRoutableEndpoints (IntegraSession&, const juce::String& containerPath, bool sources);

    /**
        Properties of the selected project / track / block (Flex: ArrangeViewProperties.mxml):
        Info, Routing, Scripting and MIDI tabs, always for the innermost selected container.
    */
    class PropertiesPanel  : public juce::Component,
                             private juce::ChangeListener
    {
    public:
        explicit PropertiesPanel (AppContext&);
        ~PropertiesPanel() override;

        /** Re-targets the panel at the current selection. */
        void selectionChanged();
        juce::String getContainerPath() const;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class InfoTab;
        class RoutingTab;
        class ScriptingTab;
        class MidiTab;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        AppContext& context;
        juce::Label title;
        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
        InfoTab* info = nullptr;
        RoutingTab* routing = nullptr;
        ScriptingTab* scripting = nullptr;
        MidiTab* midi = nullptr;
        juce::String shownContainer;
    };
}

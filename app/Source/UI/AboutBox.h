#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Core/AppContext.h"

namespace integra
{
    /**
        About Integra Live (Flex: AboutBox.mxml): logo, name, version, the copyright notice
        and buttons for the acknowledgements and the licence (GNU GPL v2, shipped in assets/).
    */
    class AboutBox  : public juce::Component
    {
    public:
        explicit AboutBox (AppContext&);

        void paint (juce::Graphics&) override;
        void resized() override;

        static void show (AppContext&);
        /** "Integra Live <version> (libIntegra <version>)". */
        static juce::String getVersionText (AppContext&);
        /** Shows the acknowledgements in the text area (the button toggles back). */
        void showAcknowledgements (bool);
        juce::String getShownText() const       { return text.getText(); }

        static constexpr juce::uint32 borderColour = 0xffe95d0f;    // AboutBox.mxml

    private:
        AppContext& context;
        juce::Image logo;
        juce::String copyright, acknowledgements;
        juce::TextEditor text;
        juce::TextButton acknowledgementsButton { "Acknowledgements" }, licenceButton { "License Agreement" }, closeButton { "Close" };
    };

    /**
        Help > Module Documentation (Flex: ModuleDocumentationWriter.as): an HTML page made
        from the loaded modules' interface definitions - a table of contents, then each
        module's description and endpoint table (endpoint, type, constraint, description),
        source, author and modified date. Written to the app data folder; returns the file.
    */
    juce::File writeModuleDocumentation (AppContext&);
}

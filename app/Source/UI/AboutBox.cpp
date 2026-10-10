#include "AboutBox.h"
#include "IntegraLookAndFeel.h"
#include "Core/EndpointInfo.h"

namespace integra
{
    AboutBox::AboutBox (AppContext& c) : context (c)
    {
        auto assets = context.session.getLocations().assets;
        logo = juce::ImageFileFormat::loadFrom (assets.getChildFile ("icon.png"));
        copyright = assets.getChildFile ("copyrightNotice.txt").loadFileAsString().trim();
        if (copyright.isEmpty())
            copyright = juce::CharPointer_UTF8 ("Copyright \xc2\xa9 2009-2013 Birmingham City University.\nPart-funded by the Culture Programme of the European Union 2006-2011.");
        acknowledgements = assets.getChildFile ("acknowledgements.txt").loadFileAsString().trim();

        text.setMultiLine (true);
        text.setReadOnly (true);
        text.setCaretVisible (false);
        text.setScrollbarsShown (true);
        text.setFont (juce::FontOptions (13.0f));
        showAcknowledgements (false);
        addAndMakeVisible (text);

        acknowledgementsButton.setClickingTogglesState (true);
        acknowledgementsButton.onClick = [this] { showAcknowledgements (acknowledgementsButton.getToggleState()); };
        licenceButton.onClick = [this]
        {
            auto licence = context.session.getLocations().assets.getChildFile ("GNU General Public License v2.0.htm");
            if (licence.existsAsFile())
                licence.startAsProcess();
            else
                juce::URL ("https://www.gnu.org/licenses/old-licenses/gpl-2.0.html").launchInDefaultBrowser();
        };
        closeButton.onClick = [this]
        {
            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                dialog->exitModalState (0);
        };
        for (auto* b : { &acknowledgementsButton, &licenceButton, &closeButton })
            addAndMakeVisible (b);
        setSize (500, 400);
    }

    juce::String AboutBox::getVersionText (AppContext& context)
    {
        return "Version " + juce::String (JUCE_APPLICATION_VERSION_STRING)
             + "  (libIntegra " + toString (context.session.lock()->get_libintegra_version()) + ")";
    }

    void AboutBox::showAcknowledgements (bool show)
    {
        text.setText (show ? acknowledgements : copyright + "\n\nThis is the JUCE port of Integra Live: libIntegra and libpd do the audio "
                                                             "processing, JUCE the user interface and audio devices.", false);
        acknowledgementsButton.setToggleState (show, juce::dontSendNotification);
    }

    void AboutBox::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.fillAll (Theme::isDarkScheme() ? juce::Colours::black : juce::Colours::white);
        g.setColour (juce::Colour (borderColour));
        g.drawRect (getLocalBounds(), 4);

        auto area = getLocalBounds().reduced (20);
        auto top = area.removeFromTop (128);
        if (logo.isValid())
            g.drawImage (logo, top.removeFromLeft (128).toFloat(), juce::RectanglePlacement::centred);
        top.removeFromLeft (20);
        g.setColour (t.text);
        g.setFont (juce::FontOptions (32.0f, juce::Font::bold));
        g.drawText ("Integra Live", top.removeFromTop (70), juce::Justification::bottomLeft);
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (getVersionText (context), top.removeFromTop (24), juce::Justification::centredLeft);
    }

    void AboutBox::resized()
    {
        auto area = getLocalBounds().reduced (20);
        area.removeFromTop (140);
        auto buttons = area.removeFromBottom (28);
        closeButton.setBounds (buttons.removeFromRight (80));
        buttons.removeFromRight (8);
        licenceButton.setBounds (buttons.removeFromRight (140));
        buttons.removeFromRight (8);
        acknowledgementsButton.setBounds (buttons.removeFromRight (140));
        area.removeFromBottom (10);
        text.setBounds (area);
    }

    void AboutBox::show (AppContext& context)
    {
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "About Integra Live";
        options.dialogBackgroundColour = juce::Colour (borderColour);
        options.content.setOwned (new AboutBox (context));
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    //==============================================================================
    juce::File writeModuleDocumentation (AppContext& context)
    {
        auto& catalogue = context.session.getCatalogue();
        std::vector<const ModuleCatalogue::Entry*> modules;
        for (auto& e : catalogue.getEntries())
            if (! e.isCore && e.hasImplementation && catalogue.findByName (e.name) == &e)
                modules.push_back (&e);
        std::sort (modules.begin(), modules.end(), [] (auto* a, auto* b) { return a->label.compareNatural (b->label) < 0; });

        auto escape = [] (const juce::String& s)
        {
            return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\n", "<br>");
        };

        juce::String html;
        html << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>Integra Module Documentation</title>\n";
        const auto css = context.session.getLocations().assets.getChildFile ("module_documentation_styles.css").loadFileAsString();
        if (css.isNotEmpty())
            html << "<style>\n" << css << "\n</style>\n";
        html << "</head><body>\n<h1 id='top'>Integra Module Documentation</h1>\n<div class='toc'>\n";
        for (auto* m : modules)
            html << "<a href='#id-" << toString (m->guid) << "'>" << escape (m->label) << "</a><br>\n";
        html << "</div>\n";

        for (auto* m : modules)
        {
            html << "<h2 id='id-" << toString (m->guid) << "'><a href='#top'>" << escape (m->label) << "</a></h2>\n";
            html << "<p>" << escape (m->description) << "</p>\n";
            html << "<table><tr><th>Endpoint</th><th>Type</th><th>Constraint</th><th>Description</th></tr>\n";
            if (m->definition != nullptr)
                for (auto* ep : m->definition->get_endpoint_definitions())
                {
                    auto i = EndpointInfo::fromDefinition (*ep);
                    juce::String type, constraint;
                    if (i.isAudioStream)            type = i.isAudioInput ? "audio in" : "audio out";
                    else if (i.kind == EndpointInfo::Kind::bang)     type = "bang";
                    else if (i.kind == EndpointInfo::Kind::integer)  type = "integer";
                    else if (i.kind == EndpointInfo::Kind::real)     type = "float";
                    else if (i.kind == EndpointInfo::Kind::text)     type = "string";
                    if (! i.allowedStates.isEmpty())
                        constraint = "allowed values: [" + i.allowedStates.joinIntoString (", ") + "]";
                    else if (i.hasRange)
                        constraint = "min " + i.format (i.minimum) + ", max " + i.format (i.maximum);
                    html << "<tr><td>" << escape (i.name) << "</td><td>" << type << "</td><td>" << escape (constraint)
                         << "</td><td>" << escape (i.description) << "</td></tr>\n";
                }
            html << "</table>\n<p><em>Source:</em> " << escape (m->getSourceLabel()) << "<br>\n";
            if (m->author.isNotEmpty())
                html << "<em>Author:</em> " << escape (m->author) << "<br>\n";
            html << "<em>Modified:</em> " << m->modified.toString (true, false) << "</p>\n<hr>\n";
        }
        html << "</body></html>\n";

        auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("IntegraLive").getChildFile ("Module Documentation.htm");
        file.getParentDirectory().createDirectory();
        file.replaceWithText (html);
        return file;
    }
}

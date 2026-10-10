#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
#include "Core/EndpointInfo.h"
#include "InfoView.h"

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
                             public InfoProvider,
                             private juce::ChangeListener
    {
    public:
        explicit PropertiesPanel (AppContext&);
        ~PropertiesPanel() override;

        /** Re-targets the panel at the current selection. */
        void selectionChanged();
        /** The selected block, track, scene (when sceneInProperties) or the project. */
        juce::String getContainerPath() const;
        juce::String getSelectedScene() const;

        /** The title bar row: scene state / key / MIDI for a scene, previous / next scene
            MIDI for the project (Flex: ScenePropertiesTitlebar, ProjectPropertiesTitlebar). */
        juce::Component* getTitlebar() const noexcept   { return titlebar.get(); }
        juce::TabbedComponent& getTabs() noexcept       { return tabs; }

        class MidiLearnRow;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        Info getInfoAt (juce::Point<int>) override;

        /** Renames the shown project / track / block / scene in place (double-click the
            title, as Flex's editable ViewHolder titles). */
        void renameShown();
        juce::TextEditor* getRenameEditor() const noexcept  { return renameEditor.get(); }

    private:
        class InfoTab;
        class RoutingTab;
        class ScriptingTab;
        class MidiTab;

        class SceneTitlebar;
        class ProjectTitlebar;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void showTabs (bool sceneOnly);

        AppContext& context;
        juce::Label title;
        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
        std::unique_ptr<InfoTab> info;
        std::unique_ptr<RoutingTab> routing;
        std::unique_ptr<ScriptingTab> scripting;
        std::unique_ptr<MidiTab> midi;
        std::unique_ptr<juce::Component> titlebar;
        std::unique_ptr<juce::TextEditor> renameEditor;
        juce::String shownContainer, titlebarFor, watchedPlayer;
        int shownTabs = -1;
    };
}

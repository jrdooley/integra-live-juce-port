#pragma once

#include "Common.h"

namespace modulecreator
{
    /** The "Interface Info" tab (InterfaceInfo.mxml): name, label, description, tags, author, dates. */
    class InterfaceInfoPage  : public Page
    {
    public:
        explicit InterfaceInfoPage (Document&);

        void refresh() override;
        void resized() override;

        juce::TextEditor& getNameEditor()           { return name; }

    private:
        FormLabel nameLabel { "Interface Name" }, labelLabel { "Interface Label" }, descriptionLabel { "Interface Description" },
                  previewLabel { "Description Preview" }, tagsLabel { "Tags" }, libIntegraLabel { "Implemented in libIntegra" },
                  authorLabel { "Author" }, createdLabel { "Created Date" }, modifiedLabel { "Modified Date" };
        juce::TextEditor name, label, description, author, created, modified;
        MarkdownView preview;
        RowsEditor tags { { "tag" }, "Add Tag" };
        juce::ComboBox standardTags;
        juce::ToggleButton implementedInLibIntegra;
        IssueList issues;
        juce::Viewport viewport;
        juce::Component content;

        void layoutContent();
    };
}

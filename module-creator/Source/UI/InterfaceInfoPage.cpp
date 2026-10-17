#include "InterfaceInfoPage.h"

namespace modulecreator
{
    InterfaceInfoPage::InterfaceInfoPage (Document& d) : Page (d)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &labelLabel, &descriptionLabel, &previewLabel, &tagsLabel,
                                                                 &libIntegraLabel, &authorLabel, &createdLabel, &modifiedLabel,
                                                                 &name, &label, &description, &author, &created, &modified,
                                                                 &preview, &tags, &standardTags, &implementedInLibIntegra, &issues })
            content.addAndMakeVisible (c);

        for (auto* e : { &name, &label, &author, &created, &modified })
            styleEditor (*e);
        styleEditor (description, true);

        name.setInputRestrictions (maximumNameLength, letters);
        name.setTooltip ("The module's name, in UpperCamelCase");
        name.setTextToShowWhenEmpty ("UpperCamelCase name", Theme::get().textDim);
        name.onTextChange = [this] { edit ([this] (ModuleDefinition& m) { m.name = name.getText(); }); };

        label.setInputRestrictions (0, printableCharacters);
        label.setTooltip ("Easy to read version of name");
        label.onTextChange = [this] { edit ([this] (ModuleDefinition& m) { m.label = label.getText(); }); };

        description.setInputRestrictions (0, printableCharacters);
        description.setTooltip ("Describe the interface, in plain text or 'markdown'.  The preview shows how the rendered markdown will look");
        description.onTextChange = [this] { edit ([this] (ModuleDefinition& m) { m.description = description.getText(); }); };

        author.onTextChange = [this] { edit ([this] (ModuleDefinition& m) { m.author = author.getText(); }); };

        for (auto* e : { &created, &modified })
        {
            e->setReadOnly (true);
            e->setColour (juce::TextEditor::textColourId, Theme::get().textDim);
        }

        tags.allowedCharacters = "abcdefghijklmnopqrstuvwxyz0123456789 ";
        tags.maximumRows = maximumNumberOfTags;
        tags.onAdd = [this]
        {
            edit ([] (ModuleDefinition& m)
            {
                int n = 1;
                while (m.tags.contains ("new tag " + juce::String (n)))
                    ++n;
                m.tags.add ("new tag " + juce::String (n));
            });
            tags.focusRow (document.definition.tags.size() - 1);
        };
        tags.onRemove = [this] (int row) { edit ([row] (ModuleDefinition& m) { m.tags.remove (row); }); };
        tags.onEdit = [this] (int row, int, const juce::String& text) { edit ([row, text] (ModuleDefinition& m) { m.tags.set (row, text); }); };

        standardTags.setTextWhenNothingSelected ("Add a standard tag...");
        for (int i = 0; i < document.config.standardTags.size(); ++i)
            standardTags.addItem (document.config.standardTags[i], i + 1);
        standardTags.onChange = [this]
        {
            const auto tag = standardTags.getText();
            standardTags.setSelectedId (0, juce::dontSendNotification);
            if (tag.isNotEmpty() && ! document.definition.tags.contains (tag) && document.definition.tags.size() < maximumNumberOfTags)
                edit ([tag] (ModuleDefinition& m) { m.tags.add (tag); });
        };

        implementedInLibIntegra.setTooltip ("The module is implemented in libIntegra");
        implementedInLibIntegra.onClick = [this]
        {
            edit ([this] (ModuleDefinition& m) { m.implementedInLibIntegra = implementedInLibIntegra.getToggleState(); });
        };

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        refresh();
    }

    void InterfaceInfoPage::refresh()
    {
        auto& m = document.definition;
        setTextIfDifferent (name, m.name);
        setTextIfDifferent (label, m.label);
        setTextIfDifferent (description, m.description);
        setTextIfDifferent (author, m.author);
        setTextIfDifferent (created, dateToString (m.createdDate));
        setTextIfDifferent (modified, dateToString (m.modifiedDate));
        preview.setMarkdown (m.label.isNotEmpty() ? m.label : m.name, m.description);

        std::vector<juce::StringArray> rows;
        for (auto& t : m.tags)
            rows.push_back (juce::StringArray (t));
        tags.setRows (rows);

        implementedInLibIntegra.setToggleState (m.implementedInLibIntegra, juce::dontSendNotification);
        libIntegraLabel.setVisible (document.developerMode);
        implementedInLibIntegra.setVisible (document.developerMode);

        issues.setIssues (document.issuesFor (ValidationIssue::Page::interfaceInfo));
        layoutContent();
    }

    void InterfaceInfoPage::resized()
    {
        viewport.setBounds (getLocalBounds());
        layoutContent();
    }

    void InterfaceInfoPage::layoutContent()
    {
        // a centred form, as the Flex version had
        const int visible = viewport.getMaximumVisibleWidth();
        const int width = juce::jlimit (500, 760, visible);
        FormLayout form { { juce::jmax (0, (visible - width) / 2), 12, width - 16, 10000 } };

        name.setBounds (form.row (&nameLabel).removeFromLeft (260));
        label.setBounds (form.row (&labelLabel).removeFromLeft (260));
        description.setBounds (form.row (&descriptionLabel, 110));
        preview.setBounds (form.row (&previewLabel, 110));

        auto tagRow = form.row (&tagsLabel, tags.getIdealHeight() + rowHeight + gap);
        standardTags.setBounds (tagRow.removeFromBottom (rowHeight).removeFromLeft (260));
        tagRow.removeFromBottom (gap);
        tags.setBounds (tagRow.removeFromLeft (260));

        if (document.developerMode)
            implementedInLibIntegra.setBounds (form.row (&libIntegraLabel).removeFromLeft (40));
        author.setBounds (form.row (&authorLabel).removeFromLeft (260));
        created.setBounds (form.row (&createdLabel).removeFromLeft (260));
        modified.setBounds (form.row (&modifiedLabel).removeFromLeft (260));

        auto issueArea = form.row (nullptr, issues.getIdealHeight (form.area.getWidth() - labelWidth));
        issues.setBounds (issueArea);
        issues.setVisible (! issues.isEmpty());

        content.setSize (juce::jmax (visible, width), form.area.getY() + 12);
    }
}

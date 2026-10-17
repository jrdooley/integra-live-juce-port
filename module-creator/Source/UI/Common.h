#pragma once

#include "Core/Document.h"
#include "IntegraLookAndFeel.h"
#include "Markdown.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace modulecreator
{
    using integra::Theme;

    constexpr int rowHeight = 26;
    constexpr int labelWidth = 170;
    constexpr int gap = 6;

    /** Characters allowed in labels and descriptions (Globals.printableCharacterRestrict, plus newlines). */
    inline const juce::String printableCharacters { juce::String::fromUTF8 ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 !\"\xc2\xa3$%^&*()-=_+[]{};'#:@~,./<>?\\|\xc2\xb0\xc2\xb1\xc2\xb9\xc2\xb2\xc2\xb3\xc2\xbc\xc2\xbd\xc2\xbe`\n\t") };
    inline const juce::String letters { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" };
    inline const juce::String lettersAndDigits { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" };
    inline const juce::String numberCharacters { "-.0123456789eE" };
    inline const juce::String integerCharacters { "-0123456789" };

    /** Sets text without notifying, and only when it differs (so a refresh doesn't move the caret). */
    inline void setTextIfDifferent (juce::TextEditor& editor, const juce::String& text)
    {
        if (editor.getText() != text)
            editor.setText (text, juce::dontSendNotification);
    }

    inline void styleEditor (juce::TextEditor& editor, bool multiLine = false)
    {
        editor.setMultiLine (multiLine, true);
        editor.setReturnKeyStartsNewLine (multiLine);
        editor.setScrollbarsShown (true);
        editor.setFont (juce::FontOptions (14.0f));
        editor.setIndents (4, multiLine ? 4 : 5);
    }

    /** A right-aligned label for a form row. */
    class FormLabel  : public juce::Label
    {
    public:
        explicit FormLabel (const juce::String& text = {})
        {
            setText (text, juce::dontSendNotification);
            setJustificationType (juce::Justification::centredRight);
            setFont (juce::FontOptions (14.0f));
        }
    };

    /** Lays out "label | component" rows top to bottom (the Flex HBox/VBox forms). */
    struct FormLayout
    {
        juce::Rectangle<int> area;
        int labelColumn = labelWidth;

        juce::Rectangle<int> row (juce::Label* label, int height = rowHeight)
        {
            auto r = area.removeFromTop (height);
            area.removeFromTop (gap);
            if (label != nullptr)
                label->setBounds (r.removeFromLeft (labelColumn).withHeight (rowHeight));
            else
                r.removeFromLeft (labelColumn);
            return r.withTrimmedLeft (gap);
        }
    };

    /** Validation messages in red (the Flex error tooltips, shown inline). */
    class IssueList  : public juce::Component
    {
    public:
        void setIssues (const std::vector<ValidationIssue>& issues)
        {
            juce::StringArray lines;
            for (auto& i : issues)
                lines.add (juce::String::fromUTF8 ("\xe2\x9a\xa0 ") + i.message);
            if (lines != messages)
            {
                messages = lines;
                repaint();
            }
        }

        int getIdealHeight (int width) const
        {
            int h = 0;
            for (auto& m : messages)
                h += 18 * juce::jmax (1, (int) std::ceil (juce::GlyphArrangement::getStringWidth (juce::FontOptions (13.0f), m) / juce::jmax (1, width - 8)));
            return h + (messages.isEmpty() ? 0 : 8);
        }

        bool isEmpty() const        { return messages.isEmpty(); }

        void paint (juce::Graphics& g) override
        {
            if (messages.isEmpty())
                return;
            g.setColour (juce::Colour (0x22e04040));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            g.setColour (Theme::get().isDark ? juce::Colour (0xffff8080) : juce::Colour (0xffb00000));
            g.setFont (juce::FontOptions (13.0f));
            auto r = getLocalBounds().reduced (6, 4);
            for (auto& m : messages)
            {
                const int lines = juce::jmax (1, (int) std::ceil (juce::GlyphArrangement::getStringWidth (juce::FontOptions (13.0f), m) / juce::jmax (1, getWidth() - 8)));
                g.drawFittedText (m, r.removeFromTop (18 * lines), juce::Justification::topLeft, lines, 1.0f);
            }
        }

    private:
        juce::StringArray messages;
    };

    /** Renders a Markdown description (MarkdownPreviewPanel, shown inline). */
    class MarkdownView  : public juce::Component
    {
    public:
        void setMarkdown (const juce::String& title, const juce::String& markdown)
        {
            const auto text = (title.isNotEmpty() ? "## " + title + "\n" : juce::String()) + markdown;
            if (text != current)
            {
                current = text;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto& t = Theme::get();
            g.setColour (t.panelAlt);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            g.setColour (t.outline);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
            auto text = integra::renderMarkdown (current.trim().isEmpty() ? juce::String ("*No description*") : current, t.text, t.accent);
            juce::TextLayout layout;
            layout.createLayout (text, (float) getWidth() - 16.0f);
            layout.draw (g, getLocalBounds().reduced (8).toFloat());
        }

    private:
        juce::String current;
    };

    /**
        An editable list of rows of one or more text fields, with add and remove buttons
        (the Flex tag list, allowed values list and state labels grid).
    */
    class RowsEditor  : public juce::Component
    {
    public:
        RowsEditor (juce::StringArray columnNames, juce::String addLabel)
            : columns (std::move (columnNames)), addButton (std::move (addLabel))
        {
            addAndMakeVisible (addButton);
            addButton.onClick = [this]
            {
                if (onAdd)
                    onAdd();
            };
        }

        std::function<void()> onAdd;
        std::function<void (int row)> onRemove;
        std::function<void (int row, int column, const juce::String&)> onEdit;
        juce::String allowedCharacters;
        int maximumLength = 0;
        int maximumRows = 1000;

        void setRows (const std::vector<juce::StringArray>& rows)
        {
            if (rows.size() != fields.size())
            {
                fields.clear();
                removeButtons.clear();
                for (size_t r = 0; r < rows.size(); ++r)
                {
                    auto& rowFields = fields.emplace_back();
                    for (int c = 0; c < columns.size(); ++c)
                    {
                        auto* e = rowFields.add (new juce::TextEditor());
                        styleEditor (*e);
                        e->setTextToShowWhenEmpty (columns[c], Theme::get().textDim);
                        if (allowedCharacters.isNotEmpty() || maximumLength > 0)
                            e->setInputRestrictions (maximumLength, allowedCharacters);
                        e->onTextChange = [this, r, c, e]
                        {
                            if (onEdit)
                                onEdit ((int) r, c, e->getText());
                        };
                        addAndMakeVisible (e);
                    }
                    auto* remove = removeButtons.add (new juce::TextButton (juce::String::fromUTF8 ("\xc3\x97")));
                    remove->setTooltip ("Remove");
                    remove->onClick = [this, r]
                    {
                        // removing deletes this button: notify later
                        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RowsEditor> (this), r]
                        {
                            if (safe != nullptr && safe->onRemove)
                                safe->onRemove ((int) r);
                        });
                    };
                    addAndMakeVisible (remove);
                }
                resized();
            }

            for (size_t r = 0; r < rows.size(); ++r)
                for (int c = 0; c < columns.size(); ++c)
                    setTextIfDifferent (*fields[r][c], rows[r][c]);
            addButton.setEnabled ((int) rows.size() < maximumRows);
        }

        void focusRow (int row)
        {
            if (row >= 0 && row < (int) fields.size())
            {
                fields[(size_t) row][0]->grabKeyboardFocus();
                fields[(size_t) row][0]->selectAll();
            }
        }

        int getIdealHeight() const      { return (int) fields.size() * (rowHeight + 2) + rowHeight; }

        void resized() override
        {
            auto r = getLocalBounds();
            for (size_t i = 0; i < fields.size(); ++i)
            {
                auto row = r.removeFromTop (rowHeight);
                r.removeFromTop (2);
                removeButtons[(int) i]->setBounds (row.removeFromRight (rowHeight));
                row.removeFromRight (2);
                const int w = row.getWidth() / juce::jmax (1, columns.size());
                for (int c = 0; c < columns.size(); ++c)
                    fields[i][c]->setBounds (c == columns.size() - 1 ? row : row.removeFromLeft (w).withTrimmedRight (2));
            }
            addButton.setBounds (r.removeFromTop (rowHeight).removeFromLeft (juce::jmin (160, getWidth())));
        }

    private:
        juce::StringArray columns;
        std::vector<juce::OwnedArray<juce::TextEditor>> fields;
        juce::OwnedArray<juce::TextButton> removeButtons;
        juce::TextButton addButton;
    };

    /** A page of the main tabs: shows the document and edits it. */
    class Page  : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit Page (Document& d) : document (d)  { document.addChangeListener (this); }
        ~Page() override                            { document.removeChangeListener (this); }

        /** Shows the document's current state; must not change the document. */
        virtual void refresh() = 0;

    protected:
        Document& document;

        /** Applies an edit to the definition and tells everyone. */
        void edit (const std::function<void (ModuleDefinition&)>& change)
        {
            change (document.definition);
            document.changed();
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override  { refresh(); }
    };
}
